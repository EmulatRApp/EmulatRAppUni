// ============================================================================
// tests/coreLib/test_iprfields.cpp -- doctest cases for computeVaForm (VA_FORM)
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
// Regression for the three-form VA_FORM computation (Alpha 21264 HRM 5.1.5,
// Figures 5-5/5-6/5-7).  Before the 2026-07-03 fix, computeVaForm implemented
// only the 43-bit and NT-32 forms and ignored VA_48 entirely, always applying
// the 43-bit VPN mask bits[32:3].  In the ES40 4GB SRM (which runs VA_48=1),
// that truncated VA[47:33] of every self-map VPTE address -> wrong PTE ->
// DTBM_SINGLE re-fault -> the 0x8321 kFaultDtbMissDouble storm.
//
// Vector V1 is trace-confirmed: at cyc 250M the memory-fill self-map miss read
// HW_VA_FORM with va=0x080103fb2000, va_ctl=0x02 (VA_48=1, VPTB=0); the buggy
// code returned 0x40FEC8 (bit 33 dropped), the HRM-correct value is 0x20040FEC8.
// Vector V2 (VA[47]=1) exercises the 48-bit SEXT(VA[47]) segment at bits[42:38]
// -- NOT yet seen in a live trace, so it is a predicted guard: without it, the
// "widen the mask to [37:3]" mistake would pass every VA[47]=0 address and
// silently corrupt the first VA[47]=1 one (Bug 1 mutating into a narrower Bug 1).
// ============================================================================

#include "doctest.h"

#include "coreLib/IprFields.h"

#include <cstdint>

namespace {

// Fixed inputs shared by the compile-time and runtime assertions.
constexpr std::uint64_t kV1Va = std::uint64_t{0x0000080103fb2000};  // VA[47]=0
constexpr std::uint64_t kV2Va = std::uint64_t{0x0000880103fb2000};  // VA[47]=1

// --- Compile-time proof (constexpr): the fix is correct at build time -------

// V1 -- 48-bit form, VA[47]=0, VPTB=0 (TRACE-CONFIRMED).  VA[47:13]<<3 = [37:3].
static_assert(coreLib::computeVaForm(0, kV1Va, /*form32=*/false, /*va48=*/true)
                  == std::uint64_t{0x0000000020040FEC8},
              "V1: 48-bit VA_FORM must keep bit 33 (VA[47:13]->[37:3])");

// The SAME VA under the old 43-bit form truncates bit 33 -- this IS the value
// the buggy code returned and the trace captured.  Documents the defect.
static_assert(coreLib::computeVaForm(0, kV1Va, /*form32=*/false, /*va48=*/false)
                  == std::uint64_t{0x000000000040FEC8},
              "43-bit VA_FORM truncates VA[42:13]->[32:3] (the former bug)");

// V2 -- 48-bit form, VA[47]=1 (PREDICTED): SEXT(VA[47]) fills bits[42:38].
static_assert(coreLib::computeVaForm(0, kV2Va, /*form32=*/false, /*va48=*/true)
                  == std::uint64_t{0x000007E20040FEC8},
              "V2: 48-bit VA_FORM must place SEXT(VA[47]) at bits[42:38]");

// Bug-2 guard: in the 43-bit form the VPTB slice is [63:33], NOT [63:30].
// A VPTB with bits[32:30] set must NOT leak into the VPN field.
static_assert(coreLib::computeVaForm(std::uint64_t{0x1C0000000}, 0x2000,
                                     /*form32=*/false, /*va48=*/false)
                  == std::uint64_t{0x8},
              "43-bit VPTB mask must be [63:33]; bits[32:30] must not leak");

} // namespace

TEST_CASE("computeVaForm -- three HRM forms (5-5/5-6/5-7)")
{
    using coreLib::computeVaForm;

    SUBCASE("V1 48-bit VA[47]=0 -- trace-confirmed (ES40 4GB self-map miss)")
    {
        CHECK(computeVaForm(0, kV1Va, false, true) == std::uint64_t{0x20040FEC8});
    }

    SUBCASE("V2 48-bit VA[47]=1 -- SEXT(VA[47]) segment exercised (predicted)")
    {
        CHECK(computeVaForm(0, kV2Va, false, true) == std::uint64_t{0x7E20040FEC8});
    }

    SUBCASE("43-bit form truncates the same VA -- the former defect value")
    {
        CHECK(computeVaForm(0, kV1Va, false, false) == std::uint64_t{0x40FEC8});
    }

    SUBCASE("Bug-2 guard: 43-bit VPTB slice is [63:33], not [63:30]")
    {
        // bits[32:30] of VPTB must be stripped, not OR'd into the VPN field.
        CHECK(computeVaForm(std::uint64_t{0x1C0000000}, 0x2000, false, false)
                  == std::uint64_t{0x8});
    }

    SUBCASE("48-bit VPTB slice [63:43] composes above the VPN field")
    {
        // VPTB bit 44 survives the [63:43] mask and sits above VA[47:13];
        // expressed via shifts so there is no hex zero-count ambiguity.
        constexpr std::uint64_t vptbBit44 = std::uint64_t{1} << 44;
        CHECK(computeVaForm(vptbBit44, kV1Va, false, true)
                  == (vptbBit44 | std::uint64_t{0x20040FEC8}));
    }

    SUBCASE("NT-32 form: VPTB[63:30] | VA[31:13]->[21:3]")
    {
        CHECK(computeVaForm(0, 0x2000, true, false) == std::uint64_t{0x8});
    }
}


// ============================================================================
// composeIsumSw / composeIsum -- SIRR/ISUM software-interrupt + AST
// derivation (SPEC-SIRR-AST-001 T1-T3; HRM 5.2.10/5.2.11, apisrm
// ev6_defs.mar field positions verified spec Sec 8.2).
// ============================================================================

namespace {

using coreLib::composeIsum;
using coreLib::composeIsumSw;
using coreLib::kIerAstenBit;
using coreLib::kIsumAstBit;
using coreLib::kIsumHwMask;
using coreLib::kSirrSirMask;
using coreLib::Mode_Privilege;

// Field-position pins straight from ev6_defs.mar (spec Sec 8.2).
static_assert(kSirrSirMask == std::uint64_t{0x1FFFC000},
              "SIRR SIR<15:1> occupies [28:14] exactly");
static_assert(kIsumHwMask == std::uint64_t{0x7FE0000000},
              "ISUM hardware causes = EI[38:33]|SL[32]|CR[31]|PC[30:29]");
static_assert(kIerAstenBit == (std::uint64_t{1} << 13),
              "IER ASTEN is bit 13 (NOT bit 2 -- the stale-comment defect)");
static_assert(kIsumAstBit[0] == (std::uint64_t{1} << 3)     // ASTK
              && kIsumAstBit[1] == (std::uint64_t{1} << 4)  // ASTE
              && kIsumAstBit[2] == (std::uint64_t{1} << 9)  // ASTS
              && kIsumAstBit[3] == (std::uint64_t{1} << 10),// ASTU
              "ISUM AST summary bits: K=3 E=4 S=9 U=10");

// SIR[n] sits at bit 13+n (SIR<15:1> at [28:14]).
constexpr std::uint64_t sirBit(unsigned n) { return std::uint64_t{1} << (13 + n); }

// asten_sr composite: ASTSR (request) nibble <7:4>, ASTEN (enable)
// nibble <3:0>, K = LSB of each nibble (m = 0 K, 1 E, 2 S, 3 U).
constexpr std::uint64_t astReq(unsigned m) { return std::uint64_t{1} << (4 + m); }
constexpr std::uint64_t astEn(unsigned m)  { return std::uint64_t{1} << m; }

} // namespace

TEST_CASE("composeIsumSw -- SI[n] requires SIRR[n] AND IER.SIEN[n] (T1)")
{
    // Request without enable: nothing pending.
    CHECK(composeIsumSw(sirBit(4), 0, 0, Mode_Privilege::Kernel) == 0);

    // Enable without request: nothing pending.
    CHECK(composeIsumSw(0, sirBit(4), 0, Mode_Privilege::Kernel) == 0);

    // Both: exactly that SI bit.  IPL 4 = IOPOST, the hot VMS source.
    CHECK(composeIsumSw(sirBit(4), sirBit(4), 0, Mode_Privilege::Kernel)
              == sirBit(4));

    // Independent lanes: enable 3+4, request 4+15 -> only 4 composes.
    CHECK(composeIsumSw(sirBit(4) | sirBit(15), sirBit(3) | sirBit(4), 0,
                        Mode_Privilege::Kernel)
              == sirBit(4));

    // Bits outside the SIR field never leak through, even if a caller
    // hands unmasked garbage in both registers.
    CHECK(composeIsumSw(~std::uint64_t{0} & ~kSirrSirMask,
                        ~std::uint64_t{0}, 0, Mode_Privilege::Kernel) == 0);
}

TEST_CASE("composeIsumSw -- AST[m] needs ASTRR+ASTER+ASTEN+(CM >= m) (T2)")
{
    // Full 4x4 table: request mode m x current mode cm.  AST[m] is
    // deliverable iff cm >= m -- the less-privileged-or-equal rule
    // (kernel AST from any mode; user AST only in user mode).
    for (unsigned m = 0; m < 4; ++m) {
        for (unsigned cm = 0; cm < 4; ++cm) {
            auto const mode = static_cast<Mode_Privilege>(cm);
            std::uint64_t const got =
                composeIsumSw(0, kIerAstenBit, astReq(m) | astEn(m), mode);
            std::uint64_t const want = (cm >= m) ? kIsumAstBit[m] : 0;
            CAPTURE(m);
            CAPTURE(cm);
            CHECK(got == want);
        }
    }

    // Each leg of the AND individually load-bearing (m = K, cm = U --
    // the most permissive mode pairing, so only the missing leg can
    // be the reason nothing composes):
    CHECK(composeIsumSw(0, kIerAstenBit, astEn(0),  Mode_Privilege::User) == 0);
    CHECK(composeIsumSw(0, kIerAstenBit, astReq(0), Mode_Privilege::User) == 0);
    CHECK(composeIsumSw(0, 0, astReq(0) | astEn(0), Mode_Privilege::User) == 0);

    // All four modes requested+enabled from User: all four summary
    // bits, at their ev6_defs.mar positions.
    CHECK(composeIsumSw(0, kIerAstenBit, std::uint64_t{0xFF},
                        Mode_Privilege::User)
              == (kIsumAstBit[0] | kIsumAstBit[1]
                  | kIsumAstBit[2] | kIsumAstBit[3]));
}

TEST_CASE("composeIsum -- staged HW causes OR live SW; staged SW filtered (T3)")
{
    constexpr std::uint64_t ei2 = std::uint64_t{1} << 35;   // interval timer

    // Staged EI passes; live SI ORs in beside it.
    CHECK(composeIsum(ei2, sirBit(4)) == (ei2 | sirBit(4)));

    // Stale SI bits in the STAGED word are filtered by kIsumHwMask --
    // only the live-composed SW side may assert SI (spec D2: the PAL
    // clears SIRR and the next ISUM read must show it gone).
    CHECK(composeIsum(ei2 | sirBit(4), 0) == ei2);

    // Empty staged + empty SW = dismiss-clean zero.
    CHECK(composeIsum(0, 0) == 0);
}
