// ============================================================================
// tests/systemLib/test_srmloader.cpp -- doctest cases for SrmLoader
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// Synthetic SRM .exe binaries built in-test: a header section, then
// the 12-byte decompressor signature, then a PAL_BASE field, then a
// stub LDA / JSR pair, then arbitrary tail bytes.  We confirm:
//
//   - Signature located at the embedded offset
//   - PAL_BASE pulled from sigOffset + 0x10
//   - finalPC pulled from the LDA disp16
//   - Dual-load: bytes match payload at both PA 0 and PA loadPa
//   - File too small / signature absent / PAL_BASE zero -> errors
//
// ============================================================================

#include "doctest.h"

#include "memoryLib/GuestMemory.h"
#include "systemLib/SrmLoader.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <vector>


using memoryLib::GuestMemory;
using memoryLib::MemStatus;
using systemLib::loadSrmFirmware;
using systemLib::SrmLoadResult;
using systemLib::kDecompSig;
using systemLib::kDecompSigLen;
using systemLib::kDefaultLoadPa;


namespace {

// Construct a fake SRM .exe in memory: a header of headerSize zeros,
// then the 12-byte signature, then 4 bytes of PAL_BASE filler, then
// 8 bytes of PAL_BASE (little-endian), then an LDA R0,disp(R26)
// followed by JSR R31,(R0), then padding to totalSize.
std::vector<uint8_t> makeFakeExe(size_t   headerSize,
                                 uint64_t palBase,
                                 uint16_t finalPcDisp,
                                 size_t   totalSize)
{
    std::vector<uint8_t> buf(totalSize, 0);

    // Header is whatever; leave zeros.
    size_t const sigOff = headerSize;

    // Signature
    std::memcpy(&buf[sigOff], kDecompSig, kDecompSigLen);

    // PAL_BASE at sigOff + 0x10 (little-endian uint64).  We need at
    // least 8 bytes between the signature and the LDA pair to land
    // PAL_BASE at the documented offset.
    std::memcpy(&buf[sigOff + 0x10], &palBase, sizeof(palBase));

    // Place the LDA / JSR pair just past PAL_BASE.  Stub-relative
    // offset for the JSR will be 0x1C (LDA at 0x18).
    uint32_t const lda = 0x201A0000u | uint32_t{finalPcDisp};   // LDA R0, disp(R26)
    uint32_t const jsr = 0x6BE04000u;                            // JSR R31, (R0)
    std::memcpy(&buf[sigOff + 0x18], &lda, sizeof(lda));
    std::memcpy(&buf[sigOff + 0x1C], &jsr, sizeof(jsr));

    return buf;
}

std::filesystem::path writeTempFile(std::string const& tag,
                                    std::vector<uint8_t> const& bytes)
{
    auto const path = std::filesystem::temp_directory_path() /
                      ("emulatr_srm_" + tag + ".exe");
    std::ofstream out{path, std::ios::binary};
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // anonymous namespace


// =============================================================================
// Happy path: signature at offset 0x300 (Tim's empirical observation
// for ES45 v7.3), palBase = 0x600000, finalPC disp = 0x5C0
// =============================================================================

TEST_CASE("SrmLoader -- signature at 0x300 with palBase + finalPC")
{
    GuestMemory mem{16ULL * 1024ULL * 1024ULL};

    auto bytes = makeFakeExe(/*headerSize*/ 0x300,
                             /*palBase*/    0x600000ULL,
                             /*finalPcDisp*/0x05C0,
                             /*totalSize*/  0x4000);
    auto path  = writeTempFile("happy", bytes);

    SrmLoadResult const r = loadSrmFirmware(mem, path, kDefaultLoadPa);

    CHECK(r.ok);
    CHECK(r.error.empty());
    CHECK(r.descriptor.valid);
    CHECK(r.descriptor.sigOffset == 0x300u);
    // SrmDescriptor split (2026-05-19): targetPalBase holds the
    // firmware-embedded value at sigOffset+0x10; initialPalBase = loadPa.
    CHECK(r.descriptor.targetPalBase == 0x600000u);
    CHECK(r.descriptor.initialPalBase == kDefaultLoadPa);
    CHECK(r.descriptor.palBase() == 0x600000u);   // accessor returns targetPalBase
    CHECK(r.descriptor.finalPC == 0x05C0u);
    CHECK(r.startPc == kDefaultLoadPa + 0x300u);
    CHECK(r.palMode);
    CHECK(r.bytesLoaded == bytes.size());

    // Stub-only load (2026-05-19): PA 0 mirror was removed; PA 0 stays
    // zero so the firmware-driven decompressor can write its output
    // there during the run (matching the AXPBox reference loader).
    // Verify PA 0 + sigOffset is still zero (no mirror artifact).
    uint8_t got = 0xFF;
    CHECK(mem.read1(0x300, got) == MemStatus::Ok);
    CHECK(static_cast<int>(got) == 0);

    // Byte at PA loadPa + sigOffset matches the signature start.
    CHECK(mem.read1(kDefaultLoadPa + 0x300, got) == MemStatus::Ok);
    CHECK(static_cast<int>(got) == static_cast<int>(kDecompSig[0]));

    std::filesystem::remove(path);
}


// =============================================================================
// Failure: file too small for header + signature
// =============================================================================

TEST_CASE("SrmLoader -- file too small returns error")
{
    GuestMemory mem{16ULL * 1024ULL * 1024ULL};
    std::vector<uint8_t> bytes(8, 0);   // way too small
    auto path = writeTempFile("toosmall", bytes);

    SrmLoadResult const r = loadSrmFirmware(mem, path, kDefaultLoadPa);

    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(r.error.find("too small") < r.error.size());

    std::filesystem::remove(path);
}


// =============================================================================
// Failure: signature absent
// =============================================================================

TEST_CASE("SrmLoader -- signature absent returns error")
{
    GuestMemory mem{16ULL * 1024ULL * 1024ULL};

    // 0x4000 zeros -- no signature anywhere.
    std::vector<uint8_t> bytes(0x4000, 0);
    auto path = writeTempFile("nosig", bytes);

    SrmLoadResult const r = loadSrmFirmware(mem, path, kDefaultLoadPa);

    CHECK_FALSE(r.ok);
    CHECK(r.error.find("signature not found") < r.error.size());

    std::filesystem::remove(path);
}


// =============================================================================
// Failure: PAL_BASE zero
// =============================================================================

TEST_CASE("SrmLoader -- PAL_BASE zero returns error")
{
    GuestMemory mem{16ULL * 1024ULL * 1024ULL};

    auto bytes = makeFakeExe(/*headerSize*/ 0x100,
                             /*palBase*/    0x0ULL,        // explicitly zero
                             /*finalPcDisp*/0x05C0,
                             /*totalSize*/  0x2000);
    auto path  = writeTempFile("zeropal", bytes);

    SrmLoadResult const r = loadSrmFirmware(mem, path, kDefaultLoadPa);

    CHECK_FALSE(r.ok);
    CHECK(r.error.find("PAL_BASE") < r.error.size());

    std::filesystem::remove(path);
}


// =============================================================================
// Range: image too large for guest memory
// =============================================================================

TEST_CASE("SrmLoader -- image larger than guest memory range-rejected")
{
    // 4 MiB memory, signature placed at 0x10, but loadPa = 0x900000
    // is beyond the memory size -> reject.
    GuestMemory mem{4ULL * 1024ULL * 1024ULL};

    auto bytes = makeFakeExe(/*headerSize*/ 0x10,
                             /*palBase*/    0x600000ULL,
                             /*finalPcDisp*/0x05C0,
                             /*totalSize*/  0x1000);
    auto path  = writeTempFile("oor", bytes);

    SrmLoadResult const r = loadSrmFirmware(mem, path, kDefaultLoadPa);

    CHECK_FALSE(r.ok);
    CHECK(r.error.find("exceeds") < r.error.size());

    std::filesystem::remove(path);
}
