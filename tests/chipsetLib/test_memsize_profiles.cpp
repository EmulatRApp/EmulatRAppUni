// ============================================================================
// tests/chipsetLib/test_memsize_profiles.cpp
//   Guest-RAM sizing profiles -- ES40/Typhoon AAR tiling (memorySize / --mem)
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
// Companion to test_ticket02_aar_encoding.cpp.  That file pins per-array ASIZ
// byte-correctness; this file pins the [System] memorySize / --mem sizing
// PROFILES end to end, following the 2026-07-03 memory-sizing test plan:
//
//     Profile   bytes         hex
//     1 GiB     1073741824    0x40000000
//     4 GiB     4294967296    0x100000000
//     8 GiB     8589934592    0x200000000
//     16 GiB    17179869184   0x400000000
//     24 GiB    25769803776   0x600000000   <- 3 x 8GB (partial array set)
//     32 GiB    34359738368   0x800000000   <- Typhoon ceiling (4 x 8GB)
//
// Two things this file adds over ticket02:
//   1. The model->variant binding that MAKES 32 GB legal: variantFromModel(
//      "ES40") must resolve to Typhoon (8GB arrays, 32GB max).  The 2026-07-03
//      reclassification changed ES40 from Tsunami to Typhoon; if it silently
//      reverts, the profiles below still "pass" on an explicit-variant Cchip
//      but a real ES40 boot would cap at 4GB.  This test ties the two.
//   2. The greedy min-fill tiling for every profile rung (enabled-array count
//      + per-array ASIZ + sum-of-arrays == configured memSize).  No silent
//      truncation within the ceiling.
//
// The OVER-ceiling case (e.g. --mem 68719476736 = 64 GiB on Typhoon) is NOT
// unit-testable here: TsunamiCchip::reset() calls std::abort() on a non-zero
// remainder (charter: no silent degradation), and abort() cannot be caught
// with exceptions disabled.  That guard is validated at runtime by:
//     Emulatr.exe --mem 68719476736
// which must print "FATAL: memory size 0x1000000000 exceeds Typhoon max DRAM
// (4 arrays x 8GB = 0x800000000) ..." and abort.  See the memory-sizing plan.
//
// Per V4 doctest convention: CHECK only, never REQUIRE.  Enum comparisons cast
// to int so doctest never stringifies an enum class.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiCchip.h"
#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"

#include <cstdint>

using namespace Tsunami21272;

namespace {

// ASIZ<15:12> and the decoded array size (HRM Table 10-14 / 10-15).
uint64_t asizOf(uint64_t aar) { return (aar >> 12) & 0xF; }
uint64_t sizeOf(uint64_t aar)
{
    const uint64_t asiz = asizOf(aar);
    return (asiz == 0) ? 0 : ((16ULL << 20) << (asiz - 1));  // 16MB << (ASIZ-1)
}
int iv(ChipsetVariant v) { return static_cast<int>(v); }

// Byte constants -- the exact values a user would put in memorySize / --mem.
constexpr uint64_t k1GiB  = 0x40000000ULL;    // 1073741824
constexpr uint64_t k4GiB  = 0x100000000ULL;   // 4294967296
constexpr uint64_t k8GiB  = 0x200000000ULL;   // 8589934592
constexpr uint64_t k16GiB = 0x400000000ULL;   // 17179869184
constexpr uint64_t k24GiB = 0x600000000ULL;   // 25769803776
constexpr uint64_t k32GiB = 0x800000000ULL;   // 34359738368

// Sum of the four enabled AAR arrays, decoded back to bytes.
uint64_t aarTotalBytes(TsunamiCchip& c)
{
    uint64_t total = 0;
    for (int i = 0; i < 4; ++i)
        total += sizeOf(c.read(Cchip::AAR0 + i * 0x40));
    return total;
}

// Count enabled arrays (non-zero AAR register word).
int enabledArrays(TsunamiCchip& c)
{
    int n = 0;
    for (int i = 0; i < 4; ++i)
        if (c.read(Cchip::AAR0 + i * 0x40) != 0) ++n;
    return n;
}

} // namespace

// ============================================================================
// Model -> variant binding: this is what makes 32 GB legal on an ES40.
// ============================================================================

TEST_CASE("ES40 model binds to Typhoon (8GB arrays, 32GB ceiling)")
{
    // The linchpin: ES40 HW is the high-bandwidth 21272 (Typhoon).  Without
    // this, an ES40 boot caps at the Tsunami 4GB ceiling regardless of --mem.
    CHECK(iv(variantFromModel("ES40")) == iv(ChipsetVariant::Typhoon));

    // Neighbours unchanged: DS10/DS20 stay Tsunami; ES45/DS25 are Titan.
    CHECK(iv(variantFromModel("DS10")) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(variantFromModel("DS20")) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(variantFromModel("ES45")) == iv(ChipsetVariant::Titan));

    // Documented DRAM ceilings behind the two 21272 variants.
    CHECK(variantInfo(ChipsetVariant::Tsunami)->maxMemBytes == k4GiB);   // 4 GB
    CHECK(variantInfo(ChipsetVariant::Typhoon)->maxMemBytes == k32GiB);  // 32 GB
}

// ============================================================================
// Typhoon sizing profiles (greedy 8GB min-fill across up to four arrays).
// ============================================================================

TEST_CASE("Typhoon 1 GiB: one 1GB array (ASIZ 0x7), AAR1-3 disabled")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, k1GiB);
    CHECK(asizOf(c.read(Cchip::AAR0)) == 0x7);
    CHECK(enabledArrays(c) == 1);
    CHECK(aarTotalBytes(c) == k1GiB);
}

TEST_CASE("Typhoon 4 GiB: one 4GB array (ASIZ 0x9), AAR1-3 disabled")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, k4GiB);
    CHECK(asizOf(c.read(Cchip::AAR0)) == 0x9);
    CHECK(enabledArrays(c) == 1);
    CHECK(aarTotalBytes(c) == k4GiB);
}

TEST_CASE("Typhoon 8 GiB: one 8GB array (ASIZ 0xA), AAR1-3 disabled")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, k8GiB);
    CHECK(asizOf(c.read(Cchip::AAR0)) == 0xA);
    CHECK(enabledArrays(c) == 1);
    CHECK(aarTotalBytes(c) == k8GiB);
}

TEST_CASE("Typhoon 16 GiB: two 8GB arrays (ASIZ 0xA), AAR2-3 disabled")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, k16GiB);
    CHECK(asizOf(c.read(Cchip::AAR0)) == 0xA);
    CHECK(asizOf(c.read(Cchip::AAR1)) == 0xA);
    CHECK(enabledArrays(c) == 2);
    CHECK(aarTotalBytes(c) == k16GiB);
}

TEST_CASE("Typhoon 24 GiB: three 8GB arrays (ASIZ 0xA), AAR3 disabled")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, k24GiB);
    CHECK(asizOf(c.read(Cchip::AAR0)) == 0xA);
    CHECK(asizOf(c.read(Cchip::AAR1)) == 0xA);
    CHECK(asizOf(c.read(Cchip::AAR2)) == 0xA);
    CHECK(c.read(Cchip::AAR3) == 0);        // 4th array unpopulated
    CHECK(enabledArrays(c) == 3);
    CHECK(aarTotalBytes(c) == k24GiB);
}

TEST_CASE("Typhoon 32 GiB (ceiling): four 8GB arrays (ASIZ 0xA)")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, k32GiB);
    for (int i = 0; i < 4; ++i)
        CHECK(asizOf(c.read(Cchip::AAR0 + i * 0x40)) == 0xA);
    CHECK(enabledArrays(c) == 4);
    CHECK(aarTotalBytes(c) == k32GiB);  // exact -- no surplus, no truncation
}

// ============================================================================
// Sizing invariant across every in-ceiling profile: the decoded AAR total
// equals the configured memSize (no silent drop of any GiB below the cap).
// ============================================================================

TEST_CASE("Typhoon: decoded AAR total == configured memSize for all profiles")
{
    for (uint64_t mem : { k1GiB, k4GiB, k8GiB, k16GiB, k24GiB, k32GiB }) {
        TsunamiCchip c(ChipsetVariant::Typhoon, 4, mem);
        CHECK(aarTotalBytes(c) == mem);
    }
}

// ============================================================================
// NOTE (negative case, runtime-only): a >32GB Typhoon request (e.g. 64 GiB =
// 0x1000000000) leaves a non-zero tiling remainder and reset() std::abort()s
// -- deliberately not exercised here (abort cannot be caught under
// -fno-exceptions).  Validate with: Emulatr.exe --mem 68719476736
// ============================================================================
