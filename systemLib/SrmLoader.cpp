// ============================================================================
// systemLib/SrmLoader.cpp -- vendor SRM .exe firmware loader implementation
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "systemLib/SrmLoader.h"

#include "memoryLib/GuestMemory.h"

#include <cstring>
#include <fstream>
#include <string>


namespace systemLib {

namespace {

// Scan up to kMaxHeaderScan bytes from the start of `data` for the
// 12-byte decompressor signature.  4-byte aligned stride matches V1.
// Returns SIZE_MAX on miss.
size_t findDecompressor(uint8_t const* data, size_t size) noexcept
{
    if (data == nullptr || size < kDecompSigLen) {
        return static_cast<size_t>(-1);
    }
    size_t const ceiling = (size < kMaxHeaderScan) ? size : kMaxHeaderScan;
    if (ceiling < kDecompSigLen) {
        return static_cast<size_t>(-1);
    }
    size_t const last = ceiling - kDecompSigLen;
    for (size_t off = 0; off <= last; off += 4) {
        if (std::memcmp(data + off, kDecompSig, kDecompSigLen) == 0) {
            return off;
        }
    }
    return static_cast<size_t>(-1);
}


// Pull the embedded PAL_BASE value out of the stub.  V1 documents
// that DEC/Compaq/HP build-time tooling stamps it at sigOffset + 0x10
// as a little-endian uint64.  Returns 0 if the stub is too short.
uint64_t readPalBase(uint8_t const* payload,
                     size_t         payloadSize,
                     size_t         sigOffset) noexcept
{
    if (payload == nullptr) return 0;
    size_t const fieldOffset = sigOffset + 0x10;
    if (fieldOffset + 8 > payloadSize) return 0;
    uint64_t value = 0;
    std::memcpy(&value, payload + fieldOffset, sizeof(value));
    return value;
}


// Scan the stub for the LDA/JSR pair that exits to PAL_BASE + disp.
// V1's logic: walk forward 4-byte aligned, find a word matching
// kJsrToFinalPc, validate that the previous word matches the
// LDA R0, disp(R26) pattern (kLdaMask & word == kLdaPattern), and
// that disp16 is plausibly < loadPa.  Returns 0 if the scan fails.
// jsrOffsetOut (when non-null) is populated with the matched JSR's
// stub-relative offset for diagnostics.
uint64_t findFinalPc(uint8_t const* payload,
                     size_t         payloadSize,
                     size_t         sigOffset,
                     uint64_t       loadPa,
                     size_t*        jsrOffsetOut) noexcept
{
    if (payload == nullptr) return 0;
    size_t const stubEnd  = (payloadSize > sigOffset) ? (payloadSize - sigOffset) : 0;
    size_t const ceiling  = (stubEnd < kJsrScanLimit) ? stubEnd : kJsrScanLimit;
    if (ceiling < 8) return 0;
    size_t const last = ceiling - 4;
    for (size_t off = 4; off <= last; off += 4) {
        uint32_t word = 0;
        std::memcpy(&word, payload + sigOffset + off, sizeof(word));
        if (word != kJsrToFinalPc) continue;

        uint32_t lda = 0;
        std::memcpy(&lda, payload + sigOffset + off - 4, sizeof(lda));
        if ((lda & kLdaMask) != kLdaPattern) continue;

        uint64_t const disp = static_cast<uint64_t>(lda & 0xFFFFu);
        if (disp == 0)        continue;   // implausible: zero entry
        if (disp >= loadPa)   continue;   // implausible: above stub

        if (jsrOffsetOut) *jsrOffsetOut = off;
        return disp;
    }
    return 0;
}


// Copy `size` bytes of `src` into guest memory at `dstPa`.  Uses
// write1 in a loop -- GuestMemory does not yet have a writeBlock
// primitive, and the v1 sizes are small enough (a few MB) for this
// to be fine.  Returns true on full success.
bool copyToGuest(memoryLib::GuestMemory& mem,
                 uint64_t                dstPa,
                 uint8_t const*          src,
                 size_t                  size) noexcept
{
    for (size_t i = 0; i < size; ++i) {
        if (mem.write1(dstPa + i, src[i]) != memoryLib::MemStatus::Ok) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace


SrmLoadResult loadSrmFirmware(memoryLib::GuestMemory&        mem,
                              std::filesystem::path const&   path,
                              uint64_t                       loadPa)
{
    SrmLoadResult r;

    // ------------------------------------------------------------------
    // File existence + read.
    // ------------------------------------------------------------------
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        r.error = "SrmLoader: file not found: " + path.string();
        return r;
    }
    auto const fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        r.error = "SrmLoader: cannot stat file: " + path.string();
        return r;
    }
    if (fileSize == 0 || fileSize < kDecompSigLen + 0x18) {
        r.error = "SrmLoader: file too small to be an SRM image: "
                + path.string();
        return r;
    }

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        r.error = "SrmLoader: cannot open for read: " + path.string();
        return r;
    }
    r.payload.resize(static_cast<size_t>(fileSize));
    in.read(reinterpret_cast<char*>(r.payload.data()),
            static_cast<std::streamsize>(fileSize));
    if (in.gcount() != static_cast<std::streamsize>(fileSize)) {
        r.error = "SrmLoader: short read: " + path.string();
        r.payload.clear();
        return r;
    }

    // ------------------------------------------------------------------
    // Signature scan.  No match -> not an SRM image.
    // ------------------------------------------------------------------
    size_t const sigOffset = findDecompressor(r.payload.data(),
                                              r.payload.size());
    if (sigOffset == static_cast<size_t>(-1)) {
        r.error = "SrmLoader: decompressor signature not found in first "
                + std::to_string(kMaxHeaderScan)
                + " bytes of " + path.string();
        r.payload.clear();
        return r;
    }

    // ------------------------------------------------------------------
    // PAL_BASE + finalPC extraction.
    // ------------------------------------------------------------------
    uint64_t const palBase = readPalBase(r.payload.data(),
                                         r.payload.size(),
                                         sigOffset);
    if (palBase == 0) {
        r.error = "SrmLoader: PAL_BASE at stub+0x10 is zero in "
                + path.string();
        r.payload.clear();
        return r;
    }

    size_t   jsrOffset = 0;
    uint64_t const finalPc = findFinalPc(r.payload.data(),
                                         r.payload.size(),
                                         sigOffset,
                                         loadPa,
                                         &jsrOffset);
    if (finalPc == 0) {
        r.error = "SrmLoader: LDA/JSR entry pair not found within "
                + std::to_string(kJsrScanLimit) + " bytes of stub start in "
                + path.string();
        r.payload.clear();
        return r;
    }

    // ------------------------------------------------------------------
    // Range check.  Only the stub copy at loadPa needs to fit -- the
    // PA 0 mirror was removed 2026-05-19 (AXPBox reference does not
    // mirror; PA [0, ...) is where the firmware itself writes its
    // decompressed output during the run, and pre-loading our copy
    // there confused the decompressor's destination computation and
    // caused destination addresses to overlap the running PAL text).
    // ------------------------------------------------------------------
    if (loadPa + r.payload.size() > mem.sizeBytes()) {
        r.error = "SrmLoader: image at PA 0x" + std::to_string(loadPa)
                + " (size " + std::to_string(r.payload.size())
                + ") exceeds guest memory size 0x"
                + std::to_string(mem.sizeBytes());
        r.payload.clear();
        return r;
    }

    // ------------------------------------------------------------------
    // Stub-only load: place the firmware payload at loadPa (V1 default
    // 0x900000).  PA [0, ...) is intentionally left zero-initialized;
    // the firmware will write decompressed output there during the
    // decompression run, matching AXPBox's CSystem::LoadROM model.
    //
    // Historical note (V4 phase 2.5 step A): the dual-load (mirror at
    // PA 0 + stub at loadPa) was inherited from V1 and turned out to
    // be wrong -- the mirror's bytes at PA 0 caused the decompressor
    // to compute destination addresses that overlapped the relocated
    // PAL text, corrupting the running PAL during the second
    // decompression pass.  See AXPBox System.cpp::LoadROM for the
    // reference loader pattern.
    // ------------------------------------------------------------------
    if (!copyToGuest(mem, loadPa, r.payload.data(), r.payload.size())) {
        r.error = "SrmLoader: write to stub PA failed";
        r.payload.clear();
        return r;
    }

    // ------------------------------------------------------------------
    // Populate descriptor + result.  initialPalBase = loadPa (AXPBox
    // model); targetPalBase = the constant the firmware embedded at
    // sigOffset+0x10 (the value the firmware will MTPR into the
    // architectural PAL_BASE during decompression).
    // ------------------------------------------------------------------
    r.descriptor.valid          = true;
    r.descriptor.sigOffset      = sigOffset;
    r.descriptor.payloadSize    = r.payload.size();
    r.descriptor.initialPalBase = loadPa;
    r.descriptor.targetPalBase  = palBase;
    r.descriptor.finalPC        = finalPc;
    r.descriptor.jsrOffset      = jsrOffset;

    r.ok          = true;
    r.startPc     = r.descriptor.startPc(loadPa);
    r.palMode     = true;
    r.bytesLoaded = r.payload.size();
    return r;
}


// ============================================================================
// loadDecompressedRom -- load an AXPBox-style pre-decompressed console image.
// ============================================================================
// Format (AXPBox CSystem::LoadROM decompressed-cache):
//   [0x00] entry PC      (u64, little-endian; low bit = PALmode)
//   [0x08] PAL_BASE      (u64, little-endian)
//   [0x10] console image (0x200000 bytes, loaded verbatim to guest PA 0x0)
// The console is already decompressed and relocated to its low-memory base by
// the firmware (validated against AXPBox booting cl67srmrom.exe to P00>>>).
// There is no decompressor stub to run and no Step D relocation: descriptor
// stays invalid so the fetch-override / done-detection paths are no-ops.
SrmLoadResult loadDecompressedRom(memoryLib::GuestMemory&      mem,
                                  std::filesystem::path const& path)
{
    SrmLoadResult r;

    static constexpr uint64_t kConsoleImageSize = 0x200000ULL;
    static constexpr size_t   kHeaderBytes      = 16;   // [PC u64][PAL_BASE u64]

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        r.error = "SrmLoader: decompressed.rom not found: " + path.string();
        return r;
    }
    auto const fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        r.error = "SrmLoader: cannot stat file: " + path.string();
        return r;
    }
    if (fileSize != kHeaderBytes + kConsoleImageSize) {
        r.error = "SrmLoader: decompressed.rom wrong size (got "
                + std::to_string(fileSize) + ", expected "
                + std::to_string(kHeaderBytes + kConsoleImageSize) + "): "
                + path.string();
        return r;
    }

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        r.error = "SrmLoader: cannot open for read: " + path.string();
        return r;
    }
    r.payload.resize(static_cast<size_t>(fileSize));
    in.read(reinterpret_cast<char*>(r.payload.data()),
            static_cast<std::streamsize>(fileSize));
    if (in.gcount() != static_cast<std::streamsize>(fileSize)) {
        r.error = "SrmLoader: short read: " + path.string();
        r.payload.clear();
        return r;
    }

    uint64_t entryPc    = 0;
    uint64_t palBaseVal = 0;
    std::memcpy(&entryPc,    r.payload.data() + 0, sizeof(entryPc));
    std::memcpy(&palBaseVal, r.payload.data() + 8, sizeof(palBaseVal));

    if (kConsoleImageSize > mem.sizeBytes()) {
        r.error = "SrmLoader: console image (0x200000) exceeds guest memory 0x"
                + std::to_string(mem.sizeBytes());
        r.payload.clear();
        return r;
    }

    // Console image -> PA 0x0 (skip the 16-byte header).
    if (!copyToGuest(mem, 0x0ULL, r.payload.data() + kHeaderBytes,
                     static_cast<size_t>(kConsoleImageSize))) {
        r.error = "SrmLoader: write of console image to PA 0 failed";
        r.payload.clear();
        return r;
    }

    // Pre-decompressed: descriptor stays invalid (no decompressor / Step D).
    // Carry palBase via targetPalBase/initialPalBase for the Machine caller.
    r.descriptor.valid          = false;
    r.descriptor.initialPalBase = palBaseVal;
    r.descriptor.targetPalBase  = palBaseVal;

    r.ok          = true;
    r.startPc     = entryPc & ~uint64_t{1};        // PAL bit conveyed via palMode
    r.palMode     = (entryPc & 1ULL) != 0;
    r.bytesLoaded = kConsoleImageSize;
    return r;
}

} // namespace systemLib
