// ============================================================================
// tests/coreLib/test_byteops.cpp -- alpha_byteops EXT*/INS*/MSK* semantics
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// WHY (JRN-SCSI-020): the EXTxH family's shift amount is byte_loc<5:0>
// (AARM Sec 4.6.1) -- 64 - 8*Rbv<2:0> truncated to SIX bits, so the
// aligned case Rbv<2:0>=0 is a shift of ZERO (pass-through + width mask),
// NOT a zero result.  EmulatR's earlier bytePos==0 -> 0 special case made
// every pre-BWX byte-load-idiom read at address == 7 (mod 8) yield NUL,
// which broke APB's device-name parse (%APB-F-NOIOVEC).  These tests lock
// the AARM semantics for all 8 offsets and the byte-load idiom itself.
// ============================================================================

#include "doctest.h"

#include "coreLib/alpha_int_byteops.h"

#include <cstdint>
#include <ostream>          // doctest forward-declares basic_ostream

using namespace alpha_byteops;

namespace {

// The AARM Sec 4.6.1 reference model for EXTxH: LEFT_SHIFT by
// (64 - 8*offset<2:0>) truncated to 6 bits, then extract-width mask.
auto refExtH(uint64_t value, uint64_t offset, uint64_t widthMask) noexcept
    -> uint64_t
{
    const int shift = (64 - 8 * static_cast<int>(offset & 0x7)) & 63;
    return (value << shift) & widthMask;
}

constexpr uint64_t kPattern = 0x8877665544332211ULL;

} // namespace

TEST_CASE("byteops: EXTQH aligned Rb (offset 0) passes the value through")
{
    // JRN-SCSI-020 regression: this exact case previously returned 0.
    CHECK(extqh(kPattern, 0) == kPattern);
    CHECK(extqh(0xFFFFFFFFFFFFFFFFULL, 8) == 0xFFFFFFFFFFFFFFFFULL);  // 8&7=0
}

TEST_CASE("byteops: EXTWH/EXTLH aligned Rb apply only the width mask")
{
    CHECK(extwh(kPattern, 0) == (kPattern & 0xFFFFULL));
    CHECK(extlh(kPattern, 0) == (kPattern & 0xFFFFFFFFULL));
}

TEST_CASE("byteops: EXTxH matches the AARM shift model for all offsets")
{
    for (uint64_t off = 0; off < 8; ++off) {
        CAPTURE(off);
        CHECK(extwh(kPattern, off) == refExtH(kPattern, off, 0xFFFFULL));
        CHECK(extlh(kPattern, off) == refExtH(kPattern, off, 0xFFFFFFFFULL));
        CHECK(extqh(kPattern, off) ==
              refExtH(kPattern, off, ~uint64_t{0}));
    }
}

TEST_CASE("byteops: pre-BWX signed byte-load idiom works at every X mod 8")
{
    // GEM idiom for a signed char at address X:
    //   LDQ_U r,0(X); EXTQH r,(X+1); SRA #56
    // The QW holds bytes 0x11..0x88 (byte k = kPattern byte k); the idiom
    // must recover byte k for every k -- k=7 is the JRN-SCSI-020 case.
    for (uint64_t k = 0; k < 8; ++k) {
        CAPTURE(k);
        const uint64_t extracted = extqh(kPattern, k + 1);
        const auto ch =
            static_cast<int64_t>(extracted) >> 56;      // SRA #0x38
        const auto expect = static_cast<int64_t>(
            static_cast<int8_t>((kPattern >> (8 * k)) & 0xFF));
        CHECK(ch == expect);
    }
}

TEST_CASE("byteops: two-LDQ_U unaligned quadword idiom (EXTQL|EXTQH)")
{
    // AARM 4.6.1 example: quadword at unaligned X reconstructed from the
    // two covering QWs.  For every X mod 8 the OR of the parts must equal
    // the logical quadword; the aligned case degenerates to lo==hi==value
    // with an idempotent OR.
    const uint64_t lo = 0x0807060504030201ULL;   // QW at  align(X)
    const uint64_t hi = 0x100F0E0D0C0B0A09ULL;   // QW at  align(X)+8
    for (uint64_t off = 0; off < 8; ++off) {
        CAPTURE(off);
        uint64_t expect = 0;
        for (int b = 0; b < 8; ++b) {
            const uint64_t byteIdx = off + static_cast<uint64_t>(b);
            const uint64_t src = (byteIdx < 8) ? lo : hi;
            const uint64_t byte = (src >> (8 * (byteIdx & 7))) & 0xFF;
            expect |= byte << (8 * b);
        }
        const uint64_t hiPart = (off == 0)
            ? extqh(lo, off)      // same QW covers all 8 bytes
            : extqh(hi, off);
        CHECK((extql(lo, off) | hiPart) == expect);
    }
}

TEST_CASE("byteops: EXTxH non-aligned offsets are unchanged by the fix")
{
    // Spot values computed by hand from the AARM model (regression guard
    // for the shift rewrite: offsets 1..7 must behave exactly as before).
    CHECK(extqh(kPattern, 1) == 0x1100000000000000ULL);
    CHECK(extqh(kPattern, 7) == 0x7766554433221100ULL);
    CHECK(extwh(kPattern, 7) == 0x1100ULL);
    CHECK(extlh(kPattern, 7) == 0x33221100ULL);
    CHECK(extwh(kPattern, 5) == 0x0ULL);       // AARM word example, X%8=5
}

TEST_CASE("byteops: INSxH/MSKxH aligned cases keep their AARM semantics")
{
    // Audited correct in JRN-SCSI-020 (BYTE_ZAP mask empty at Rbv=0 for
    // INSxH; MSKQH zaps nothing) -- locked here so a future "symmetry"
    // edit cannot silently break them.
    CHECK(inswh(kPattern, 0) == 0);
    CHECK(inslh(kPattern, 0) == 0);
    CHECK(insqh(kPattern, 0) == 0);
    CHECK(mskqh(kPattern, 0) == kPattern);
    CHECK(mskwh(kPattern, 0) == kPattern);
    CHECK(msklh(kPattern, 0) == kPattern);
    CHECK(mskql(kPattern, 0) == 0);
}
