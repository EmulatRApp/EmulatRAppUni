// ============================================================================
// tests/chipsetLib/test_ticket02_aar_encoding.cpp
//   Ticket 2 -- AAR encoding byte-correctness (HRM Tables 10-14 / 10-15)
//   plus AAR0-3 RW behaviour (firmware programs the array sizes).
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
// Pins the AAR0-3 reset encoding against HRM Table 10-14 (Tsunami: ASIZ
// 0001=16MB .. 0111=1GB, cap 0x7) and Table 10-15 (Typhoon: adds
// 1000=2GB, 1001=4GB, 1010=8GB), the ADDR<34:24> base field, the
// hardcoded ROWS=2 / BNKS=1 SDRAM geometry, the base+size no-wrap
// invariant, and the new RW semantics (writes stick, per HRM RW).
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiCchip.h"
#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"

#include <cstdint>

using namespace Tsunami21272;

static uint64_t asizOf(uint64_t aar) { return (aar >> 12) & 0xF; }
static uint64_t addrOf(uint64_t aar) { return (aar >> 24) & 0x7FF; }

// ============================================================================
// Tsunami encoding
// ============================================================================

TEST_CASE("Tsunami 1GB: AAR0 = 1GB at base 0, AAR1-3 disabled")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const aar0 = c.read(Cchip::AAR0);
    CHECK(asizOf(aar0) == 0x7);          // 1GB per HRM 10-14
    CHECK(addrOf(aar0) == 0);            // base 0
    CHECK(((aar0 >> 2) & 0x3) == 2);     // ROWS = 13 row bits
    CHECK((aar0 & 0x3) == 1);            // BNKS = 2 banks
    CHECK(c.read(Cchip::AAR1) == 0);     // disabled
    CHECK(c.read(Cchip::AAR2) == 0);
    CHECK(c.read(Cchip::AAR3) == 0);
}

TEST_CASE("Tsunami 4GB: 4 x 1GB arrays at bases 0, 1G, 2G, 3G")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 1, 4ULL << 30);
    for (int i = 0; i < 4; ++i) {
        uint64_t const aar = c.read(Cchip::AAR0 + i * 0x40);
        CHECK(asizOf(aar) == 0x7);
        CHECK(addrOf(aar) == (static_cast<uint64_t>(i) << 6));  // 1GB step = ADDR bit 6
    }
}

// ============================================================================
// Typhoon encoding
// ============================================================================

TEST_CASE("Typhoon 32GB: 4 x 8GB arrays, ASIZ = 0xA")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, 32ULL << 30);
    for (int i = 0; i < 4; ++i) {
        uint64_t const aar = c.read(Cchip::AAR0 + i * 0x40);
        CHECK(asizOf(aar) == 0xA);       // 8GB per HRM 10-15
    }
}

TEST_CASE("Typhoon 2GB and 4GB single-array ASIZ encodings")
{
    TsunamiCchip c2(ChipsetVariant::Typhoon, 1, 2ULL << 30);
    CHECK(asizOf(c2.read(Cchip::AAR0)) == 0x8);   // 2GB
    TsunamiCchip c4(ChipsetVariant::Typhoon, 1, 4ULL << 30);
    CHECK(asizOf(c4.read(Cchip::AAR0)) == 0x9);   // 4GB
}

// ============================================================================
// base + size never wraps past the 35-bit PA space
// ============================================================================

TEST_CASE("AAR base + size never exceeds 2^35")
{
    for (uint64_t mem : {1ULL<<30, 2ULL<<30, 4ULL<<30, 8ULL<<30, 32ULL<<30}) {
        TsunamiCchip c(ChipsetVariant::Typhoon, 4, mem);
        for (int i = 0; i < 4; ++i) {
            uint64_t const aar = c.read(Cchip::AAR0 + i * 0x40);
            if (aar == 0) continue;
            uint64_t const base = addrOf(aar) << 24;
            uint64_t const asiz = asizOf(aar);
            uint64_t const size = (asiz == 0) ? 0 : ((16ULL << 20) << (asiz - 1));
            CHECK((base + size) <= (1ULL << 35));
        }
    }
}

// ============================================================================
// RW behaviour -- firmware programs the array sizes (HRM RW)
// ============================================================================

TEST_CASE("AAR0 honours writes (RW per HRM), not read-only")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // Reprogram AAR0 to a 512MB array at base 0 (ASIZ 0x6).
    uint64_t const programmed = (0x6ULL << 12) | (2ULL << 2) | 1ULL;
    c.write(Cchip::AAR0, programmed);
    CHECK(c.read(Cchip::AAR0) == programmed);
    CHECK(asizOf(c.read(Cchip::AAR0)) == 0x6);
}

TEST_CASE("AAR write/read round-trips through the chipset MMIO surface")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const pa = Base::kCchip_CSR + Cchip::AAR0;
    uint64_t const programmed = (0x7ULL << 12) | (2ULL << 2) | 1ULL;  // 1GB array
    cs.mmioWrite(pa, programmed, 8);
    CHECK(cs.mmioRead(pa, 8) == programmed);
}

// ============================================================================
// M2 consistency (2026-06-12): the AAR total, decoded by the FIRMWARE's own
// formula, must equal the configured memSize.  apisrm memconfig_pc264.c
// get_array_size() computes array size = 2^(ASIZ+3) MB with ASIZ read as
// ((AAR>>12)&7) -- a 3-bit mask -- and the console sums the arrays for the
// "N Meg of system memory" / show config / show memory total.  This pins the
// #6 bug class: a wrong memSize reaching the Cchip yields a wrong reported
// total (the 64 MB-vs-1 GiB defect was an upstream memSize plumbing bug, not
// an encoding bug -- the encoding here is firmware-confirmed).
// ============================================================================

// Mirror of apisrm get_array_size() (memconfig_pc264.c): note the firmware's
// 3-bit `& 7` mask -- Tsunami ASIZ 0x1..0x7 only.  (Typhoon 0x8..0xA needs a
// different read; that is the M3 platform-gating concern, out of scope here.)
static uint64_t firmwareArraySizeBytes(uint64_t aar)
{
    uint64_t const asiz = (aar >> 12) & 0x7;
    return (asiz == 0) ? 0 : ((1ULL << (asiz + 3)) * 1024ULL * 1024ULL);
}

TEST_CASE("M2: AAR total via firmware formula == configured memSize (Tsunami)")
{
    struct Case { uint64_t mem; } const cases[] = {
        {  64ULL << 20 },   // 64 MB  -> ASIZ 0x3  (the #6 bug size)
        { 256ULL << 20 },   // 256 MB -> ASIZ 0x5
        {   1ULL << 30 },   // 1 GB   -> ASIZ 0x7  (the #6 fix)
        {   4ULL << 30 },   // 4 GB   -> 4 x 1 GB
    };
    for (Case const& tc : cases) {
        TsunamiCchip c(ChipsetVariant::Tsunami, 1, tc.mem);
        uint64_t total = 0;
        for (int i = 0; i < 4; ++i)
            total += firmwareArraySizeBytes(c.read(Cchip::AAR0 + i * 0x40));
        CHECK(total == tc.mem);
    }
}

TEST_CASE("M2: 64MB -> AAR0 low word 0x3009; 1GiB -> 0x7009 (firmware-observed)")
{
    TsunamiCchip c64(ChipsetVariant::Tsunami, 1, 64ULL << 20);
    CHECK((c64.read(Cchip::AAR0) & 0xFFFFULL) == 0x3009ULL);
    TsunamiCchip c1g(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    CHECK((c1g.read(Cchip::AAR0) & 0xFFFFULL) == 0x7009ULL);
}
