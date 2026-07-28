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
inline FpExecCtx vaxChopped() {
    return FpExecCtx{ FPVariant{ coreLib::FpRoundingMode::RoundTowardZero,
                                 coreLib::FPTrapMode::None, false }, 0 };
}
// /S-family (software completion): FPTrapMode::SU sets suppressUnderflow, so
// FPVariant::hasSoftwareCompletion() is true -- the FV-8 dirty-zero mode key.
inline FpExecCtx vaxSoftware() {
    return FpExecCtx{ FPVariant{ coreLib::FpRoundingMode::RoundToNearest,
                                 coreLib::FPTrapMode::SU, false }, 0 };
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


// ============================================================================
// VAX audit batch 2 pins (FV-4 / FV-6 / FV-7 / FV-8, 2026-07-28).
// ============================================================================

TEST_CASE("VAX CMPG: top-binade G operands compare as numbers, not IEEE NaN (FV-4)") {
    SoftFloatBackend be;
    uint64_t const kTrue = 0x4000000000000000ULL;
    // Register exp11 == 0x7FF is a VALID (huge) G value; read as an IEEE
    // double it is NaN, so the old f64 quiet predicates returned unordered ->
    // false for every relation.  AARM 4.10.7: comparisons are exact.
    uint64_t const gTop = 0x7FF4000000000000ULL;
    uint64_t const gOne = 0x4010000000000000ULL;   // G 1.0
    CHECK(be.cmpG(FpCompare::Lt, gOne, gTop, vaxNormal()).bits == kTrue);
    CHECK(be.cmpG(FpCompare::Eq, gTop, gTop, vaxNormal()).bits == kTrue);
    CHECK(be.cmpG(FpCompare::Le, gTop, gOne, vaxNormal()).bits == 0u);
    // Sign ordering: -huge < 1.0; and no INV for valid operands.
    uint64_t const gNegTop = gTop | 0x8000000000000000ULL;
    CHECK(be.cmpG(FpCompare::Lt, gNegTop, gOne, vaxNormal()).bits == kTrue);
    CHECK_FALSE(be.cmpG(FpCompare::Eq, gTop, gOne, vaxNormal()).exc.inv);
    // Zero orders between the signs.
    CHECK(be.cmpG(FpCompare::Lt, gNegTop, 0u, vaxNormal()).bits == kTrue);
    CHECK(be.cmpG(FpCompare::Lt, 0u, gOne, vaxNormal()).bits == kTrue);
}

TEST_CASE("VAX CMPG: reserved operand signals INV in every trap mode (FV-4/FV-8)") {
    SoftFloatBackend be;
    uint64_t const gResv = 0x8000000000000000ULL;  // sign=1, exp=0: reserved
    uint64_t const gOne  = 0x4010000000000000ULL;
    CHECK(be.cmpG(FpCompare::Eq, gResv, gOne, vaxNormal()).exc.inv);
    CHECK(be.cmpG(FpCompare::Eq, gResv, gOne, vaxSoftware()).exc.inv);
    CHECK(be.cmpG(FpCompare::Eq, gOne, gResv | 1u, vaxNormal()).exc.inv);
}

TEST_CASE("VAX dirty zero: INV in default mode, clean zero under /S (FV-8)") {
    SoftFloatBackend be;
    uint64_t const dirty = 0x0000000000000001ULL;  // exp=0, sign=0, frac!=0
    uint64_t const gOne  = 0x4010000000000000ULL;  // G 1.0
    uint64_t const kTrue = 0x4000000000000000ULL;
    // Default trap mode: dirty zero raises INV (AARM 4.7.7.1 default mode:
    // non-finite operand traps).
    CHECK(be.addG(dirty, gOne, vaxNormal()).exc.inv);
    CHECK(be.cmpG(FpCompare::Eq, dirty, 0u, vaxNormal()).exc.inv);
    CHECK(be.cvtGQ(dirty, vaxNormal()).exc.inv);
    // /S software completion: "a VAX dirty zero is treated as zero".
    auto const s = be.addG(dirty, gOne, vaxSoftware());
    CHECK_FALSE(s.exc.inv);
    CHECK(s.bits == gOne);                          // 0 + 1.0 = 1.0
    auto const sc = be.cmpG(FpCompare::Eq, dirty, 0u, vaxSoftware());
    CHECK_FALSE(sc.exc.inv);
    CHECK(sc.bits == kTrue);                        // dirty zero == true zero
    auto const sq = be.cvtGQ(dirty, vaxSoftware());
    CHECK_FALSE(sq.exc.inv);
    CHECK(sq.bits == 0u);                           // exact 0, not a denormal
}

TEST_CASE("VAX CVTGF: F exponent window checked, Ovf/Unf recorded (FV-6)") {
    SoftFloatBackend be;
    // Register exp11 0x500 (1280) > F max 1151 -> overflow.
    uint64_t const gHuge = 0x5000000000000000ULL;
    CHECK(be.cvtGF(gHuge, vaxNormal()).exc.ovf);
    // Register exp11 0x100 (256) < F min 897 -> flush to 0; Unf only with /U.
    uint64_t const gTiny = 0x1000000000000000ULL;
    auto const u = be.cvtGF(gTiny, vaxUnderflowEnabled());
    CHECK(u.bits == 0u);
    CHECK(u.exc.unf);
    auto const uq = be.cvtGF(gTiny, vaxNormal());
    CHECK(uq.bits == 0u);
    CHECK_FALSE(uq.exc.unf);
}

TEST_CASE("VAX CVTGF: VAX half-up rounding at F precision; /C chops (FV-6)") {
    SoftFloatBackend be;
    // G 1.0 with fraction-field bit 28 set: exactly half an F LSB (F LSB is
    // fraction bit 29).  VAX biased rounding takes the larger magnitude ->
    // fraction bit 29 set.  IEEE ties-to-even (old roundFreg) would round back
    // DOWN to 1.0 -- this vector discriminates the tie rule.  /C chops.
    uint64_t const gHalf = 0x4010000010000000ULL;
    CHECK(be.cvtGF(gHalf, vaxNormal()).bits  == 0x4010000020000000ULL);
    CHECK(be.cvtGF(gHalf, vaxChopped()).bits == 0x4010000000000000ULL);
}

TEST_CASE("VAX CVTQF: rounds to F precision with VAX tie rule; /C chops (FV-6)") {
    SoftFloatBackend be;
    // q = 2^24 + 1: F holds 24 significant bits, so the +1 is exactly halfway
    // between F(2^24) and F(2^24 + 2).  VAX half-up -> 2^24 + 2; RNE would
    // give the even 2^24.  Register image of F(2^24+2): image-as-double is
    // 4*(2^24+2) = 2^26*(1 + 2^-23) -> exp field 1049 (0x419), frac bit 29.
    uint64_t const q = (1ULL << 24) + 1ULL;
    CHECK(be.cvtQF(q, vaxNormal()).bits  == 0x4190000020000000ULL);
    CHECK(be.cvtQF(q, vaxChopped()).bits == 0x4190000000000000ULL);   // 2^24
}

TEST_CASE("VAX CVTGQ overflow stores the true result's low 64 bits (FV-7)") {
    SoftFloatBackend be;
    // G image 0x4400000000000001 read as a double is 4*v with
    //   4*v = 2^65 * (1 + 2^-52)   ->   v = 2^63 + 2^11.
    // v is outside i64 (IOV) and an exact integer; AARM 4.7.7.9 stores
    //   v mod 2^64 = 2^63 + 2^11 = 0x8000000000000800
    // (the old SoftFloat path saturated to INT64_MAX = 0x7FFFFFFFFFFFFFFF).
    uint64_t const gBig = 0x4400000000000001ULL;
    auto const r = be.cvtGQ(gBig, vaxNormal());
    CHECK(r.exc.iov);
    CHECK(r.bits == 0x8000000000000800ULL);
    // Negative mirror: -(2^63 + 2^11) mod 2^64 = 2^64 - 2^63 - 2^11
    //   = 0x7FFFFFFFFFFFF800 (two's complement).
    auto const n = be.cvtGQ(gBig | 0x8000000000000000ULL, vaxNormal());
    CHECK(n.exc.iov);
    CHECK(n.bits == 0x7FFFFFFFFFFFF800ULL);
}

TEST_CASE("IEEE CVTTQ overflow stores the true result's low 64 bits (FV-7)") {
    SoftFloatBackend be;
    // 2^63 (finite, exact integer): mod 2^64 = 0x8000000000000000.
    auto const p = be.cvtTQ(kTwoPow63, rne());
    CHECK(p.exc.iov);
    CHECK(p.bits == 0x8000000000000000ULL);
    // 2^64 + 2^12 (T image 0x43F0000000000001): mod 2^64 = 0x1000.
    auto const q = be.cvtTQ(0x43F0000000000001ULL, rne());
    CHECK(q.exc.iov);
    CHECK(q.bits == 0x0000000000001000ULL);
    // Non-finite operand: no true result exists; only the IOV mapping is
    // pinned (the stored pattern stays SoftFloat's).
    CHECK(be.cvtTQ(kQNaN, rne()).exc.iov);
}
