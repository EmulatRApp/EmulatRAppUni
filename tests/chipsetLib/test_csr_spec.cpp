// ============================================================================
// tests/chipsetLib/test_csr_spec.cpp
//   Phase A doctest exercises for Tsunami21272_CsrSpec.h
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
// Phase A scaffolding does not change runtime behavior, but the spec
// header is consumed at compile time and at runtime by Phase B/C.
// These exercises confirm:
//
//   (1) FieldSpec sanity     -- lsb + width <= 64 for every populated
//                               field; non-overlapping bit positions
//                               within a single register.
//   (2) mask/extract/deposit -- round-trip identity, edge cases at
//                               bit-0 and bit-63 boundaries.
//   (3) Composite masks      -- MISC W1C_MASK / W1S_MASK / WO_MASK
//                               correctly union the per-field masks.
//   (4) roundLog2Nearest     -- matches a known table for the values
//                               Phase C uses (ES40 -> 19, ES45 -> 20).
//   (5) Interval-timer derivation -- kCchipTimerMask is derived
//                               consistently from the profile clock
//                               and the rounding helper.
//   (6) Variant selectors    -- resetMiscRev / resetDchipDrev return
//                               the HRM Table 10-12 / Dchip values.
//
// Per V4 doctest convention: CHECK only, never REQUIRE (exceptions
// are disabled in the build; REQUIRE fails at compile time).
//
// References:
//   - chipsetLib/Tsunami21272_CsrSpec.h
//   - journals/CchipPhaseA_Design_Notes.md
//   - HRM Tables 10-9 (CSC), 10-12 (MISC), 10-20 (IIC)
// ============================================================================

#include "doctest.h"

#include "chipsetLib/Tsunami21272_CsrSpec.h"
#include "chipsetLib/TsunamiVariant.h"

#include <cstdint>

using namespace Tsunami21272;
using Spec::FieldSpec;

// ============================================================================
// 1. FieldSpec sanity
// ============================================================================

TEST_CASE("FieldSpec lsb + width within 64-bit container")
{
    // Spot-check the populated fields.  Phase B will likely add a
    // comprehensive sweep when more registers are filled in.
    auto inRange = [](FieldSpec const& f) {
        return f.lsb < 64 && f.width >= 1 && f.width <= 64
            && (uint32_t(f.lsb) + uint32_t(f.width) <= 64);
    };

    CHECK(inRange(Spec::Cchip::MISC::DEVSUP));
    CHECK(inRange(Spec::Cchip::MISC::REV));
    CHECK(inRange(Spec::Cchip::MISC::NXS));
    CHECK(inRange(Spec::Cchip::MISC::NXM));
    CHECK(inRange(Spec::Cchip::MISC::ACL));
    CHECK(inRange(Spec::Cchip::MISC::ABT));
    CHECK(inRange(Spec::Cchip::MISC::ABW));
    CHECK(inRange(Spec::Cchip::MISC::IPREQ));
    CHECK(inRange(Spec::Cchip::MISC::IPINTR));
    CHECK(inRange(Spec::Cchip::MISC::ITINTR));
    CHECK(inRange(Spec::Cchip::MISC::CPUID));
    CHECK(inRange(Spec::Cchip::IIC::OF));
    CHECK(inRange(Spec::Cchip::IIC::ICNT));
}

TEST_CASE("MISC field positions match HRM Table 10-12")
{
    // Bit positions from HRM Table 10-12.  Verifies the spec did not
    // drift from the document.
    CHECK(Spec::Cchip::MISC::DEVSUP.lsb == 40);
    CHECK(Spec::Cchip::MISC::DEVSUP.width == 4);
    CHECK(Spec::Cchip::MISC::REV.lsb == 32);
    CHECK(Spec::Cchip::MISC::REV.width == 8);
    CHECK(Spec::Cchip::MISC::NXS.lsb == 29);
    CHECK(Spec::Cchip::MISC::NXS.width == 3);
    CHECK(Spec::Cchip::MISC::NXM.lsb == 28);
    CHECK(Spec::Cchip::MISC::NXM.width == 1);
    CHECK(Spec::Cchip::MISC::ACL.lsb == 24);
    CHECK(Spec::Cchip::MISC::ACL.width == 1);
    CHECK(Spec::Cchip::MISC::ABT.lsb == 20);
    CHECK(Spec::Cchip::MISC::ABT.width == 4);
    CHECK(Spec::Cchip::MISC::ABW.lsb == 16);
    CHECK(Spec::Cchip::MISC::ABW.width == 4);
    CHECK(Spec::Cchip::MISC::IPREQ.lsb == 12);
    CHECK(Spec::Cchip::MISC::IPREQ.width == 4);
    CHECK(Spec::Cchip::MISC::IPINTR.lsb == 8);
    CHECK(Spec::Cchip::MISC::IPINTR.width == 4);
    CHECK(Spec::Cchip::MISC::ITINTR.lsb == 4);
    CHECK(Spec::Cchip::MISC::ITINTR.width == 4);
    CHECK(Spec::Cchip::MISC::CPUID.lsb == 0);
    CHECK(Spec::Cchip::MISC::CPUID.width == 2);
}

TEST_CASE("IIC field positions match HRM Table 10-20")
{
    CHECK(Spec::Cchip::IIC::OF.lsb == 24);
    CHECK(Spec::Cchip::IIC::OF.width == 1);
    CHECK(Spec::Cchip::IIC::ICNT.lsb == 0);
    CHECK(Spec::Cchip::IIC::ICNT.width == 24);
}

// ============================================================================
// 2. mask / extract / deposit
// ============================================================================

TEST_CASE("mask(): bit positions are correct for representative fields")
{
    using Spec::mask;

    // 4-bit ITINTR at position 4 -> bits 7..4 set.
    CHECK(mask(Spec::Cchip::MISC::ITINTR) == 0x00000000000000F0ULL);

    // 4-bit IPINTR at position 8 -> bits 11..8 set.
    CHECK(mask(Spec::Cchip::MISC::IPINTR) == 0x0000000000000F00ULL);

    // 8-bit REV at position 32 -> bits 39..32 set.
    CHECK(mask(Spec::Cchip::MISC::REV)    == 0x000000FF00000000ULL);

    // 1-bit NXM at position 28 -> only bit 28 set.
    CHECK(mask(Spec::Cchip::MISC::NXM)    == 0x0000000010000000ULL);

    // 24-bit ICNT at position 0 -> bits 23..0 set.
    CHECK(mask(Spec::Cchip::IIC::ICNT)    == 0x0000000000FFFFFFULL);
}

TEST_CASE("extract / deposit round-trip")
{
    using Spec::extract;
    using Spec::deposit;

    // Place REV (8 bits at pos 32) = 0x10, then read it back.
    uint64_t reg = 0;
    reg = deposit(reg, Spec::Cchip::MISC::REV, 0x10);
    CHECK(extract(reg, Spec::Cchip::MISC::REV) == 0x10);
    // Bits outside REV unchanged.
    CHECK((reg & ~Spec::mask(Spec::Cchip::MISC::REV)) == 0);

    // Place ITINTR (4 bits at pos 4) = 0x5 (CPU0 + CPU2 timer pending),
    // verify extract returns 0x5 and the rest of reg untouched.
    reg = deposit(reg, Spec::Cchip::MISC::ITINTR, 0x5);
    CHECK(extract(reg, Spec::Cchip::MISC::ITINTR) == 0x5);
    CHECK(extract(reg, Spec::Cchip::MISC::REV)    == 0x10);

    // Over-wide value masked to field width.  Deposit 0xFF into a
    // 4-bit field yields only the low 4 bits stored.
    reg = deposit(0, Spec::Cchip::MISC::IPREQ, 0xFF);
    CHECK(extract(reg, Spec::Cchip::MISC::IPREQ) == 0xF);
}

// ============================================================================
// 3. MISC composite masks
// ============================================================================

TEST_CASE("MISC W1C_MASK includes NXM, IPINTR, ITINTR -- and only those")
{
    using namespace Spec::Cchip::MISC;
    constexpr uint64_t expected =
          Spec::mask(NXM) | Spec::mask(IPINTR) | Spec::mask(ITINTR);

    CHECK(W1C_MASK == expected);

    // Specifically: bits 28, 11..8, 7..4.  Spell out the constant
    // rather than relying on a host-specific popcount intrinsic.
    CHECK(W1C_MASK == 0x0000000010000FF0ULL);
}

TEST_CASE("MISC W1S_MASK includes ABT and ABW -- and only those")
{
    using namespace Spec::Cchip::MISC;
    constexpr uint64_t expected = Spec::mask(ABT) | Spec::mask(ABW);

    CHECK(W1S_MASK == expected);
    // Bits 23..20 (ABT) + 19..16 (ABW) -- spelled out.
    CHECK(W1S_MASK == 0x0000000000FF0000ULL);
}

TEST_CASE("MISC WO_MASK includes DEVSUP, ACL, IPREQ -- and only those")
{
    using namespace Spec::Cchip::MISC;
    constexpr uint64_t expected =
          Spec::mask(DEVSUP) | Spec::mask(ACL) | Spec::mask(IPREQ);

    CHECK(WO_MASK == expected);
    // DEVSUP 43..40, ACL 24, IPREQ 15..12 -- spelled out.
    CHECK(WO_MASK == 0x00000F000100F000ULL);
}

TEST_CASE("MISC composite masks do not overlap")
{
    using namespace Spec::Cchip::MISC;
    // Each pairwise intersection is empty -- a field cannot be in
    // multiple categories.
    CHECK((W1C_MASK & W1S_MASK) == 0);
    CHECK((W1C_MASK & WO_MASK)  == 0);
    CHECK((W1S_MASK & WO_MASK)  == 0);
    CHECK((W1C_MASK & RO_MASK)  == 0);
    CHECK((W1S_MASK & RO_MASK)  == 0);
    CHECK((WO_MASK  & RO_MASK)  == 0);
}

// ============================================================================
// 4. roundLog2Nearest
// ============================================================================

TEST_CASE("roundLog2Nearest matches known table")
{
    using Spec::roundLog2Nearest;

    // Powers of two land on themselves.
    CHECK(roundLog2Nearest(1)        == 0);
    CHECK(roundLog2Nearest(2)        == 1);
    CHECK(roundLog2Nearest(4)        == 2);
    CHECK(roundLog2Nearest(1024)     == 10);
    CHECK(roundLog2Nearest(524288)   == 19);
    CHECK(roundLog2Nearest(1048576)  == 20);

    // Mid-points round to the nearer power.  3 is equidistant in
    // linear space but closer to 4 in log space (4/3 < 3/2).
    CHECK(roundLog2Nearest(3)        == 2);

    // ES40 / 600 MHz / 1024 = 585937.  Nearest power of two is
    // 524288 = 2^19.  This is the Phase C derivation; if it ever
    // changes, the static_assert in the spec header catches it.
    CHECK(roundLog2Nearest(585937)   == 19);

    // ES45 / 1 GHz / 1024 = 976562.  Nearest is 1048576 = 2^20.
    CHECK(roundLog2Nearest(976562)   == 20);
}

// ============================================================================
// 5. Interval-timer profile derivation
// ============================================================================

TEST_CASE("kCchipIntervalTimerCycles matches profileAlphaClockHz / 1024")
{
    using namespace Spec;

    CHECK(kCchipIntervalTimerCycles == kProfileAlphaClockHz / 1024);

    // Bit position derived from the rounded interval.
    CHECK(kCchipTimerBit == roundLog2Nearest(kCchipIntervalTimerCycles));

    // Mask is (1 << bit) - 1 -- low `bit` bits set.
    CHECK(kCchipTimerMask == (uint64_t{1} << kCchipTimerBit) - 1);

    // For ES40 (default profile, 600 MHz), expect bit 19, mask 0x7FFFF.
    // The static_assert in the spec header gates this; if the CMake
    // override flips the profile, both checks below adjust.
    if (kProfileAlphaClockHz == 600000000ULL) {
        CHECK(kCchipTimerBit  == 19);
        CHECK(kCchipTimerMask == 0x7FFFFULL);
    }
    if (kProfileAlphaClockHz == 1000000000ULL) {
        CHECK(kCchipTimerBit  == 20);
        CHECK(kCchipTimerMask == 0xFFFFFULL);
    }
}

TEST_CASE("Mask fire predicate fires exactly once per interval")
{
    // Walks the mask test across a synthetic cycle range and counts
    // fires.  Phase C uses (cycleCount & mask) == 0 && cycleCount != 0
    // as the predicate, so cycle 0 is excluded and the next fire is
    // at cycle (1 << bit).
    using namespace Spec;

    uint64_t const interval = uint64_t{1} << kCchipTimerBit;
    uint64_t const sweepStart = 0;
    uint64_t const sweepEnd   = interval * 4 + 1;

    int fires = 0;
    for (uint64_t c = sweepStart; c < sweepEnd; ++c) {
        if (c != 0 && (c & kCchipTimerMask) == 0) {
            ++fires;
        }
    }
    // Exactly 4 fires expected in a sweep covering 4 intervals
    // (excluding cycle 0).
    CHECK(fires == 4);
}

// ============================================================================
// 6. Variant selectors
// ============================================================================

TEST_CASE("MISC REV reset value depends on chipset variant")
{
    CHECK(Spec::resetMiscRev(ChipsetVariant::Tsunami) ==
          Spec::Cchip::MISC::REV_RESET_TSUNAMI);
    CHECK(Spec::resetMiscRev(ChipsetVariant::Typhoon) ==
          Spec::Cchip::MISC::REV_RESET_TYPHOON);

    // HRM Table 10-12 values.
    CHECK(Spec::resetMiscRev(ChipsetVariant::Tsunami) == 0x01);
    CHECK(Spec::resetMiscRev(ChipsetVariant::Typhoon) == 0x08);
}

TEST_CASE("Dchip DREV reset value depends on chipset variant")
{
    CHECK(Spec::resetDchipDrev(ChipsetVariant::Tsunami) == 0x10);
    CHECK(Spec::resetDchipDrev(ChipsetVariant::Typhoon) == 0x20);
}
