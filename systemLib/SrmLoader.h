// ============================================================================
// systemLib/SrmLoader.h -- vendor SRM .exe firmware loader for V4
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// V1 path A in V4: load a vendor-supplied SRM .exe firmware image,
// scan for the self-decompressing stub signature, populate a
// descriptor (PAL_BASE, finalPC entry displacement) from the embedded
// metadata, and dual-load the file into guest memory: full payload at
// the mirror PA 0x0 (decompressor's output destination) AND at the
// stub PA 0x900000 (where the decompressor itself executes).
//
// ============================================================================
// V4 boot model -- direct-firmware-image (NOT canonical ROM-emulation)
// ============================================================================
//
// Real EV6 hardware enters PALcode at the architectural reset vector
// palBase + 0x780 (RESET_ENTRY, see coreLib/Ev6EntryVectors.h
// kEntry_RESET).  That assumes a boot ROM is mapped into the guest
// physical address space and contains the firmware (decompressor +
// PALcode + SRM Console) at that location.  V4 does NOT emulate a
// boot ROM.  Instead this loader implements a "direct-firmware-image"
// model:
//
//   1. The vendor SRM .exe (a self-decompressing blob shipped by
//      Digital/Compaq) is loaded into guest RAM at a non-architectural
//      PA -- kDefaultLoadPa = 0x900000 -- well above palBase.
//
//   2. The CPU's start PC is set to (loadPa + sigOffset), pointing
//      directly at the decompressor stub's signature instruction.  The
//      canonical reset vector palBase + 0x780 is NEVER entered in this
//      flow.
//
//   3. The decompressor stub runs in PAL mode at 0x900000+, decompresses
//      the PALcode + SRM payload into the destination region, and ends
//      with an LDA/JSR pair that transfers control to palBase + finalPC
//      (the post-decompression PALcode entry point, encoded in the LDA's
//      disp16 and discovered by the kJsrToFinalPc / kLdaPattern scan).
//
//   4. V4 detects "decompression done" when cpu.pc == descriptor.entryPa()
//      (= palBase + finalPC).  After that, the mirror copy from PA [0,
//      kPalRelocSize) into [palBase, palBase + kPalRelocSize) places the
//      PAL vectors and init entry at their architectural offsets, and
//      execution proceeds normally inside PALcode.
//
// Trade-offs:
//   + Faster bring-up: no boot-ROM emulation, no SROM model, just load
//     the binary and run.
//   + Works with any vendor SRM image V1 successfully booted.
//   - Cycle-accurate ROM/SROM boot behavior is NOT modeled (acceptable
//     for an architectural emulator; not for a hardware-replica).
//   - Warm-restart paths that re-enter through palBase + 0x780 would
//     need a separate code path (out of scope today).
//
// The architectural reset vector palBase + 0x780 is preserved as the
// named constant coreLib::ev6::kEntry_RESET for any future code path
// that requires canonical reset semantics.
// ============================================================================
//
// Phase 2.5 Step A scope: scan + populate + dual-load.  The actual
// decompressor run happens later phases:
//   Step B: trace-driven discovery of missing leaves / IPRs
//   Step C: I-stream fetch override (hold immutable payload bytes
//           for IF reads in the stub PA range while the copy loop
//           overwrites the mutable guest memory copy)
//   Step D: done detection (cpu.pc == descriptor.entryPa()) plus
//           PAL image relocation (mirror PA [0, palRelocSize) to
//           [palBase, palBase + palRelocSize))
//
// Constants (from V1 SrmRomLoader analysis):
//
//   kDecompSig       12-byte signature -- BIS R1,R31,R4 / BIS R2,R31,R5
//                    / BIS R28,R31,R14, the opening triple of every
//                    EV6 SRM stub.
//   kMaxHeaderScan   0x1000 byte ceiling on the signature scan.
//   kDefaultLoadPa   0x900000 -- V1's stub load PA; documented across
//                    every ES40/ES45 V6.2-V7.2 build.
//   kPalRelocSize    0x9200 -- size of the post-decompression mirror-
//                    to-palBase copy (PAL vectors + init entry, sized
//                    to stay below the copy loop's first destination).
//
// Out of scope for v1: ELF section copies, multi-segment image
// formats, snapshot save/load.  Each can layer behind this same shape
// later without churning callers.
//
// ============================================================================

#ifndef SYSTEMLIB_SRMLOADER_H
#define SYSTEMLIB_SRMLOADER_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace memoryLib {
struct GuestMemory;
}

namespace systemLib {

// V1-derived constants.  Static by design -- callers consult them
// rather than passing around magic numbers.
inline constexpr uint8_t  kDecompSig[12] = {
    0x04, 0x04, 0x3F, 0x44,
    0x05, 0x04, 0x5F, 0x44,
    0x0E, 0x04, 0x9F, 0x47
};
inline constexpr size_t   kDecompSigLen   = sizeof(kDecompSig);
inline constexpr size_t   kMaxHeaderScan  = 0x1000;
inline constexpr uint64_t kDefaultLoadPa  = 0x900000ULL;
inline constexpr uint64_t kPalRelocSize   = 0x9200ULL;

// Decompressor-stub LDA/JSR pair scan constants.  V1's
// finalPC-detection logic: locate JSR R31, (R0) immediately preceded
// by LDA R0, disp(R26).  disp16 is the PAL_BASE-relative entry
// displacement.  Scan limited to kJsrScanLimit bytes from sigOffset.
inline constexpr uint32_t kLdaMask         = 0xFFFF0000u;
inline constexpr uint32_t kLdaPattern      = 0x201A0000u;   // LDA R0, disp(R26)
inline constexpr uint32_t kJsrToFinalPc    = 0x6BE04000u;   // JSR R31, (R0)
inline constexpr size_t   kJsrScanLimit    = 0x1000;


// SrmDescriptor captures every value derived from the firmware
// payload that downstream phases need.  Populated by loadSrmFirmware
// on success; consulted by Machine, the future done-detection logic,
// and the PAL relocation step.
struct SrmDescriptor
{
    bool      valid          = false;
    size_t    sigOffset      = 0;     // byte offset of decompressor signature
    size_t    payloadSize    = 0;     // total file size in bytes

    // ------------------------------------------------------------------
    // Two PAL_BASE concepts -- separated 2026-05-19 after AXPBox study.
    // ------------------------------------------------------------------
    // initialPalBase: the value cpu.palBase should hold BEFORE the
    //                  decompressor starts running.  AXPBox's working
    //                  reference sets this to loadPa (= 0x900000),
    //                  matching the address at which the decompressor
    //                  stub is loaded.  The firmware's decompressor
    //                  internally executes HW_MTPR HW_PAL_BASE to
    //                  change this to the targetPalBase value at the
    //                  appropriate point during decompression.
    //
    // targetPalBase:  the value the firmware INTENDS palBase to hold
    //                 once decompression has produced the runtime PAL
    //                 vectors.  Extracted from payload[sigOffset+0x10]
    //                 (little-endian uint64).  Used by entryPa() to
    //                 compute where the final JSR lands post-decompress,
    //                 and by Step D PAL relocation to determine the
    //                 destination of the mirror-to-palBase copy.
    //
    // Prior V4 design conflated these in a single `palBase` field and
    // seeded cpu.palBase with the target value (0x600000).  Empirical
    // result was that the decompressor's destination computation used
    // the wrong base, producing destination addresses that overlapped
    // the running PAL text and corrupted it mid-decompress.  Splitting
    // the field tracks AXPBox's model and prevents that overlap.
    // ------------------------------------------------------------------
    uint64_t  initialPalBase = 0;     // set to loadPa; cpu.palBase seed
    uint64_t  targetPalBase  = 0;     // from payload[0x10..0x18] (LE)
    uint64_t  finalPC        = 0;     // targetPalBase-relative entry displacement
    size_t    jsrOffset      = 0;     // diagnostic: stub-relative offset of the JSR

    // Absolute start PC for the decompressor: loadPa + sigOffset.
    [[nodiscard]] uint64_t startPc(uint64_t loadPa) const noexcept
    {
        return loadPa + static_cast<uint64_t>(sigOffset);
    }

    // Absolute PA where the firmware's final JSR lands, post-decompress.
    // Uses targetPalBase because the firmware computes its final entry
    // relative to the target value, not the initial loadPa.
    [[nodiscard]] uint64_t entryPa() const noexcept
    {
        return targetPalBase + finalPC;
    }

    // Backwards-compatible alias for code paths that still consult the
    // "current/target" palBase (Step D relocation destination, etc.).
    // Returns targetPalBase -- the value that matters once the firmware
    // has issued its HW_MTPR HW_PAL_BASE during decompression.
    [[nodiscard]] uint64_t palBase() const noexcept
    {
        return targetPalBase;
    }
};


// SrmLoadResult mirrors LoadResult's shape for the raw-binary loader
// but carries the SRM descriptor and the immutable payload buffer.
// The payload is held by value because Step C (fetch override) needs
// to serve I-stream reads from it after the decompressor copy loop
// starts overwriting the stub region in guest memory.
struct SrmLoadResult
{
    bool                  ok          = false;
    std::string           error;
    uint64_t              startPc     = 0;     // descriptor.startPc(loadPa), PAL bit cleared
    bool                  palMode     = true;  // SRM stub always runs in PAL
    uint64_t              bytesLoaded = 0;
    SrmDescriptor         descriptor;

    // Frozen file bytes -- safe to range-check and read from for the
    // future I-stream fetch override.  Always populated on ok = true.
    std::vector<uint8_t>  payload;
};


// Load a vendor SRM .exe firmware image into guest memory using V1's
// path A contract:
//   1. Read the entire file into a buffer
//   2. Scan first kMaxHeaderScan bytes (4-byte stride) for kDecompSig
//   3. Read PAL_BASE from payload[sigOffset + 0x10] (little-endian uint64)
//   4. Scan stub for JSR R31,(R0) preceded by LDA R0,disp(R26); the
//      disp16 is finalPC (PAL_BASE-relative entry displacement)
//   5. Copy full payload to mirror PA 0x0
//   6. Copy full payload to stub PA loadPa (V1 default 0x900000)
//   7. Return descriptor + startPc + frozen payload buffer
//
// Failure modes return ok = false with a populated error string;
// payload is empty on failure.
SrmLoadResult loadSrmFirmware(memoryLib::GuestMemory&        mem,
                              std::filesystem::path const&   path,
                              uint64_t                       loadPa = kDefaultLoadPa);

// Load a PRE-DECOMPRESSED SRM console image (AXPBox decompressed.rom
// format: [entry PC u64 LE][PAL_BASE u64 LE][0x200000-byte console image]).
// Copies the console image to PA 0x0; returns startPc (PAL bit cleared),
// palMode, and palBase carried in descriptor.targetPalBase.  No signature
// scan, no decompressor, no Step D: descriptor.valid = false.
SrmLoadResult loadDecompressedRom(memoryLib::GuestMemory&      mem,
                                  std::filesystem::path const& path);

} // namespace systemLib

#endif // SYSTEMLIB_SRMLOADER_H
