// ============================================================================
// tests/fpBoxLib/test_fp_backend.cpp -- SoftFloat backend reference vectors
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Known-vector checks for fpBox::SoftFloatBackend (task #26, Phase B): exact
// ops, inexact, directed-rounding (1-ULP) control, overflow, underflow,
// compares (incl. unordered/NaN), CVTTQ rounding + IOV, and the S-format
// single-rounding discriminator. doctest CHECK only (exceptions disabled in V4).
// ============================================================================

#include "doctest.h"
#include "fpBoxLib/fp_backend_softfloat.h"
#include "coreLib/fp_variant_core.h"
#include "coreLib/alpha_fpcr_core.h"

using namespace fpBox;
using coreLib::FPVariant;

namespace {
// Bit patterns of common doubles.
constexpr uint64_t kD1_0  = 0x3FF0000000000000ULL;
constexpr uint64_t kD2_0  = 0x4000000000000000ULL;
constexpr uint64_t kD3_0  = 0x4008000000000000ULL;
constexpr uint64_t kD6_0  = 0x4018000000000000ULL;
constexpr uint64_t kThirdRne  = 0x3FD5555555555555ULL; // 1/3 round-to-nearest
constexpr uint64_t kThirdUp   = 0x3FD5555555555556ULL; // 1/3 toward +inf
constexpr uint64_t kSqrt2Rne  = 0x3FF6A09E667F3BCDULL; // sqrt(2.0) RNE
constexpr uint64_t kDblMax    = 0x7FEFFFFFFFFFFFFFULL;
constexpr uint64_t kPosInf    = 0x7FF0000000000000ULL;
constexpr uint64_t kQNaN      = 0x7FF8000000000000ULL;
constexpr uint64_t kTwoPow63  = 0x43E0000000000000ULL; // 2^63 = i64 max + 1
constexpr uint64_t kTwoNeg600 = 0x1A70000000000000ULL; // 2^-600

inline FpExecCtx rne()  { return FpExecCtx{ FPVariant::makeIEEE_T_Normal(),   0 }; }
inline FpExecCtx chop() { return FpExecCtx{ FPVariant::makeIEEE_T_Chopped(),  0 }; }
inline FpExecCtx down() { return FpExecCtx{ FPVariant::makeIEEE_T_MinusInf(), 0 }; }
// +inf has no static Alpha qualifier -- reach it via dynamic (/D) + FPCR DYN=11.
// NOTE: must use makeIEEE_T_Dynamic() (sets the /D flag); a default FPVariant
// collapses to RoundToNearest in getEffectiveRoundingMode(), NOT UseFPCR.
inline FpExecCtx up()   { return FpExecCtx{ FPVariant::makeIEEE_T_Dynamic(), (AlphaFPCR::RM_PLUS_INF << AlphaFPCR::DYN_RM_SHIFT) }; }
} // namespace

TEST_CASE("SoftFloatBackend: exact T arithmetic, no flags") {
    SoftFloatBackend be;
    auto add = be.addT(kD1_0, kD2_0, rne());
    CHECK(add.bits == kD3_0);
    CHECK_FALSE(add.exc.ine);
    auto mul = be.mulT(kD2_0, kD3_0, rne());
    CHECK(mul.bits == kD6_0);
    CHECK_FALSE(mul.exc.ine);
}

TEST_CASE("SoftFloatBackend: inexact div + sqrt set INE") {
    SoftFloatBackend be;
    auto d = be.divT(kD1_0, kD3_0, rne());
    CHECK(d.bits == kThirdRne);
    CHECK(d.exc.ine);
    auto s = be.sqrtT(kD2_0, rne());
    CHECK(s.bits == kSqrt2Rne);
    CHECK(s.exc.ine);
}

TEST_CASE("SoftFloatBackend: directed rounding is honored (1 ULP apart)") {
    SoftFloatBackend be;
    auto dn = be.divT(kD1_0, kD3_0, down());   // 1/3 toward -inf
    auto upR = be.divT(kD1_0, kD3_0, up());    // 1/3 toward +inf
    CHECK(dn.bits == kThirdRne);               // round-down of 1/3 == the RNE value
    CHECK(upR.bits == kThirdUp);
    CHECK(upR.bits == dn.bits + 1);            // exactly one ULP apart
    // chop (toward zero) of a positive value equals round-down here.
    CHECK(be.divT(kD1_0, kD3_0, chop()).bits == kThirdRne);
}

TEST_CASE("SoftFloatBackend: overflow -> +inf, OVF+INE") {
    SoftFloatBackend be;
    auto r = be.addT(kDblMax, kDblMax, rne());
    CHECK(r.bits == kPosInf);
    CHECK(r.exc.ovf);
    CHECK(r.exc.ine);
}

TEST_CASE("SoftFloatBackend: underflow to zero -> UNF+INE") {
    SoftFloatBackend be;
    auto r = be.mulT(kTwoNeg600, kTwoNeg600, rne());   // 2^-1200, below subnormal range
    CHECK(r.bits == 0x0000000000000000ULL);
    CHECK(r.exc.unf);
    CHECK(r.exc.ine);
}

TEST_CASE("SoftFloatBackend: compare (T) true/false + unordered") {
    SoftFloatBackend be;
    CHECK(be.cmpT(FpCompare::Eq, kD2_0, kD2_0, rne()).bits == kD2_0);          // 2.0 (true)
    CHECK(be.cmpT(FpCompare::Eq, kD1_0, kD2_0, rne()).bits == 0x0ULL);          // false
    CHECK(be.cmpT(FpCompare::Lt, kD1_0, kD2_0, rne()).bits == kD2_0);
    CHECK(be.cmpT(FpCompare::Le, kD2_0, kD2_0, rne()).bits == kD2_0);
    CHECK(be.cmpT(FpCompare::Un, kQNaN, kD1_0, rne()).bits == kD2_0);           // unordered true
    CHECK(be.cmpT(FpCompare::Un, kD1_0, kD2_0, rne()).bits == 0x0ULL);          // ordered -> false
    CHECK(be.cmpT(FpCompare::Eq, kQNaN, kD1_0, rne()).bits == 0x0ULL);          // NaN != anything
}

TEST_CASE("SoftFloatBackend: CVTTQ rounding + IOV on overflow") {
    SoftFloatBackend be;
    uint64_t const k3_75 = 0x400E000000000000ULL; // 3.75
    CHECK(be.cvtTQ(k3_75, rne()).bits  == 4ULL);   // nearest
    CHECK(be.cvtTQ(k3_75, chop()).bits == 3ULL);   // toward zero
    CHECK(be.cvtTQ(k3_75, rne()).exc.ine);
    auto ov = be.cvtTQ(kTwoPow63, rne());          // 2^63 > i64 max
    CHECK(ov.exc.iov);
}

TEST_CASE("SoftFloatBackend: CVTQT integer->double exact") {
    SoftFloatBackend be;
    auto r = be.cvtQT(5ULL, rne());
    CHECK(r.bits == 0x4014000000000000ULL);        // 5.0
    CHECK_FALSE(r.exc.ine);
}

TEST_CASE("SoftFloatBackend: S-format is single-rounded (not double-rounded)") {
    SoftFloatBackend be;
    // Register images (single value expressed as double): 1.0 and 2^-24.
    uint64_t const regOne     = kD1_0;                    // 1.0
    uint64_t const regTwoN24  = 0x3E70000000000000ULL;    // 2^-24
    // Single ULP at 1.0 is 2^-23; 2^-24 is half a ULP -> RNE ties to even -> 1.0.
    auto s = be.addS(regOne, regTwoN24, rne());
    CHECK(s.bits == kD1_0);          // single result rounds back to 1.0
    CHECK(s.exc.ine);                // but it was inexact
    // The same add in double KEEPS the 2^-24 (representable), proving the S path
    // genuinely rounded to single rather than rounding in double.
    auto t = be.addT(regOne, regTwoN24, rne());
    CHECK(t.bits != kD1_0);
    CHECK_FALSE(t.exc.ine);
}


// ============================================================================
// VAX G-float fidelity pins (audit FV-1 / FV-2 / FV-3, 2026-07-28).
// Register-image convention: a VAX G image read as an IEEE double is 4x the
// true VAX value (exponent rebias is exactly 2^2), so G 1.0 = 0x4010...,
// G 1.5 = 0x4018..., G -0.5 = 0xC000... .
// ============================================================================

namespace {
inline FpExecCtx vaxNormal() {
    return FpExecCtx{ FPVariant{ coreLib::FpRoundingMode::RoundToNearest,
                                 coreLib::FPTrapMode::None, false }, 0 };
}
inline FpExecCtx vaxUnderflowEnabled() {
    return FpExecCtx{ FPVariant{ coreLib::FpRoundingMode::RoundToNearest,
                                 coreLib::FPTrapMode::Underflow, false }, 0 };
}
} // anonymous namespace

TEST_CASE("VAX addsub: same-binade effective subtraction orders |a| >= |b| (FV-1)") {
    SoftFloatBackend be;
    uint64_t const g1_0  = 0x4010000000000000ULL;  // G 1.0
    uint64_t const g1_5  = 0x4018000000000000ULL;  // G 1.5
    uint64_t const gm0_5 = 0xC000000000000000ULL;  // G -0.5
    uint64_t const g0_5  = 0x4000000000000000ULL;  // G  0.5
    // The broken ordering wrapped a.frac - b.frac modulo 2^64 and kept a's
    // sign: SUBG 1.0 - 1.5 returned +1.5.  Correct: -0.5.
    CHECK(be.subG(g1_0, g1_5, vaxNormal()).bits == gm0_5);
    // Control (|a| > |b|, was already correct) and the mirrored operand order.
    CHECK(be.subG(g1_5, g1_0, vaxNormal()).bits == g0_5);
    // Same defect shape through ADDG with a negative operand.
    CHECK(be.addG(g1_0, gm0_5 | 0x0018000000000000ULL, vaxNormal()).bits == gm0_5);
}

TEST_CASE("VAX CVTQG: exact-halfway quad rounds AWAY from zero, not to even (FV-2)") {
    SoftFloatBackend be;
    // 2^53 + 1 is exactly halfway between representable 2^53 and 2^53 + 2.
    // VAX biased rounding (AARM 4.7.6) takes the larger magnitude; IEEE RNE
    // would take the even 2^53.
    uint64_t const q = (1ULL << 53) + 1ULL;
    uint64_t const g_2p53plus2 = 0x4360000000000001ULL;  // G(2^53+2): +2 exp rebias
    CHECK(be.cvtQG(q, vaxNormal()).bits == g_2p53plus2);
    // Negative mirror: away from zero means MORE negative.
    auto const neg = be.cvtQG(static_cast<uint64_t>(-static_cast<int64_t>(q)),
                              vaxNormal());
    CHECK(neg.bits == (g_2p53plus2 | 0x8000000000000000ULL));
}

TEST_CASE("VAX underflow is recorded when the qualifier enables it (FV-3)") {
    // Ctor pin: /U, /SU, /SUI all set the underflow-enable flag.
    CHECK(FPVariant{ coreLib::FpRoundingMode::RoundToNearest,
                     coreLib::FPTrapMode::Underflow, false }.underflow);
    CHECK(FPVariant{ coreLib::FpRoundingMode::RoundToNearest,
                     coreLib::FPTrapMode::SU, false }.underflow);
    CHECK_FALSE(FPVariant{ coreLib::FpRoundingMode::RoundToNearest,
                           coreLib::FPTrapMode::None, false }.underflow);
    // Behavioral pin: G-min * G-min underflows; /U variant must record Unf.
    SoftFloatBackend be;
    uint64_t const gMin = 0x0010000000000000ULL;   // G smallest normal (exp = 1)
    auto const r = be.mulG(gMin, gMin, vaxUnderflowEnabled());
    CHECK(r.bits == 0u);        // VAX flushes the underflowed result to zero
    CHECK(r.exc.unf);           // and the enabled qualifier records it
    auto const rq = be.mulG(gMin, gMin, vaxNormal());
    CHECK(rq.bits == 0u);
    CHECK_FALSE(rq.exc.unf);    // default mode: flush silently
}
