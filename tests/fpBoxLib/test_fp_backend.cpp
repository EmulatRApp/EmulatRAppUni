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
