// ============================================================================
// fpBoxLib/fp_backend_softfloat.cpp -- SoftFloat reference backend (impl)
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
// ============================================================================
//
// FILE: fpBoxLib/fp_backend_softfloat.cpp   (task #26, Phase B; 2026-06-10)
//   Implements IFpBackend against Berkeley SoftFloat 3e. Pinned strict-FP by
//   fpBoxLib/CMakeLists (no host FP is used here anyway -- SoftFloat is integer).
//
// SEMANTIC NOTES:
//   - Rounding is resolved from FpExecCtx (variant; or FPCR DYN if /D) and set on
//     SoftFloat's global before each op (or passed as the rounding arg for the
//     int conversions). RNE / chop / -inf / +inf only -- Alpha has no static +inf.
//   - Raw IEEE flags are harvested into FpExc; NO Alpha denormal (DNZ/UNDZ) or
//     trap policy is applied here (that is the grain/FPCR layer's job -- see the
//     DNZ-negative-denormal-sqrt note in fp_backend.h).
//   - S-format: the register image is the single's value as a 64-bit double; we
//     narrow to float32_t (exact for a valid single), run the f32 kernel (single
//     rounding), and widen back -- avoiding the double-rounding bug of doing the
//     op in double and then rounding to single.
//   - cvtTQ maps SoftFloat's invalid-on-overflow to Alpha IOV (integer overflow).
//   - Compare uses SoftFloat quiet predicates; signaling-NaN INV semantics for
//     CMPTxx are a [CONFIRM]/refine point against the AARM once traced.
// ============================================================================

#include "fpBoxLib/fp_backend_softfloat.h"
#include "fpBoxLib/vax_float.h"        // bitwise-faithful VAX F/G integer kernels
#include "coreLib/alpha_fpcr_core.h"   // AlphaFPCR::DYN_RM_* for the /D path

extern "C" {
#include "softfloat.h"   // vendored Berkeley SoftFloat 3e
}

namespace fpBox {
namespace {

// Resolve the effective SoftFloat rounding mode from the decoded qualifier,
// reading the FPCR DYN field (bits 59:58) when the op defers to it (/D).
inline auto resolveRm(FpExecCtx const& ctx) -> uint_fast8_t
{
    coreLib::FpRoundingMode m = ctx.variant.getEffectiveRoundingMode();
    if (m == coreLib::FpRoundingMode::UseFPCR) {
        uint64_t const dyn = (ctx.fpcr & AlphaFPCR::DYN_RM_MASK) >> AlphaFPCR::DYN_RM_SHIFT;
        switch (dyn) {
            case AlphaFPCR::RM_CHOPPED:   return softfloat_round_minMag;
            case AlphaFPCR::RM_MINUS_INF: return softfloat_round_min;
            case AlphaFPCR::RM_PLUS_INF:  return softfloat_round_max;
            case AlphaFPCR::RM_NORMAL:    default: return softfloat_round_near_even;
        }
    }
    switch (m) {
        case coreLib::FpRoundingMode::RoundTowardZero: return softfloat_round_minMag;
        case coreLib::FpRoundingMode::RoundDown:       return softfloat_round_min;
        case coreLib::FpRoundingMode::RoundUp:         return softfloat_round_max;
        case coreLib::FpRoundingMode::RoundToNearest:  default: return softfloat_round_near_even;
    }
}

// Map SoftFloat's global exception flags into FpExc (arithmetic ops). iov is
// left false here; the integer-result conversion sets it explicitly.
inline auto harvest() -> FpExc
{
    uint_fast8_t const f = softfloat_exceptionFlags;
    FpExc e;
    e.inv = (f & softfloat_flag_invalid)   != 0;
    e.dze = (f & softfloat_flag_infinite)  != 0;   // SoftFloat "infinite" == divide-by-zero
    e.ovf = (f & softfloat_flag_overflow)  != 0;
    e.unf = (f & softfloat_flag_underflow) != 0;
    e.ine = (f & softfloat_flag_inexact)   != 0;
    return e;
}

inline auto f64bits(uint64_t v) -> float64_t { float64_t x; x.v = v; return x; }

inline bool isNaNF64(uint64_t u)
{
    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL)
        && ((u & 0x000FFFFFFFFFFFFFULL) != 0);
}

} // namespace

// ---- IEEE T-format (double) ------------------------------------------------

auto SoftFloatBackend::addT(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float64_t const r = f64_add(f64bits(a), f64bits(b));
    return FpResult{ r.v, harvest() };
}

auto SoftFloatBackend::subT(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float64_t const r = f64_sub(f64bits(a), f64bits(b));
    return FpResult{ r.v, harvest() };
}

auto SoftFloatBackend::mulT(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float64_t const r = f64_mul(f64bits(a), f64bits(b));
    return FpResult{ r.v, harvest() };
}

auto SoftFloatBackend::divT(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float64_t const r = f64_div(f64bits(a), f64bits(b));
    return FpResult{ r.v, harvest() };
}

auto SoftFloatBackend::sqrtT(uint64_t a, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float64_t const r = f64_sqrt(f64bits(a));
    return FpResult{ r.v, harvest() };
}

// ---- IEEE S-format (single): narrow -> f32 op -> widen (single rounding) ----

namespace {
// One S-format binary op via the f32 kernel. The register images are exact
// singles-as-doubles, so f64_to_f32 recovers the single without rounding; only
// the f32 op rounds (to single), and we widen the result back to register form.
template <class F32Op>
inline auto sBinary(uint64_t a, uint64_t b, FpExecCtx const& ctx, F32Op op) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float32_t const fa = f64_to_f32(f64bits(a));   // exact for a valid single
    float32_t const fb = f64_to_f32(f64bits(b));
    softfloat_exceptionFlags = 0;                  // discard (zero) input-narrow flags
    float32_t const fr = op(fa, fb);
    FpExc const e = harvest();
    float64_t const dr = f32_to_f64(fr);           // exact widen to register image
    return FpResult{ dr.v, e };
}
} // namespace

auto SoftFloatBackend::addS(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{ return sBinary(a, b, ctx, [](float32_t x, float32_t y){ return f32_add(x, y); }); }

auto SoftFloatBackend::subS(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{ return sBinary(a, b, ctx, [](float32_t x, float32_t y){ return f32_sub(x, y); }); }

auto SoftFloatBackend::mulS(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{ return sBinary(a, b, ctx, [](float32_t x, float32_t y){ return f32_mul(x, y); }); }

auto SoftFloatBackend::divS(uint64_t a, uint64_t b, FpExecCtx const& ctx) -> FpResult
{ return sBinary(a, b, ctx, [](float32_t x, float32_t y){ return f32_div(x, y); }); }

auto SoftFloatBackend::sqrtS(uint64_t a, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float32_t const fa = f64_to_f32(f64bits(a));
    softfloat_exceptionFlags = 0;
    float32_t const fr = f32_sqrt(fa);
    FpExc const e = harvest();
    float64_t const dr = f32_to_f64(fr);
    return FpResult{ dr.v, e };
}

// ---- Compare (T) -----------------------------------------------------------

auto SoftFloatBackend::cmpT(FpCompare k, uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult
{
    softfloat_exceptionFlags = 0;
    float64_t const fa = f64bits(a);
    float64_t const fb = f64bits(b);
    bool res = false;
    switch (k) {
        case FpCompare::Eq: res = f64_eq(fa, fb);       break;   // quiet
        case FpCompare::Lt: res = f64_lt_quiet(fa, fb); break;
        case FpCompare::Le: res = f64_le_quiet(fa, fb); break;
        case FpCompare::Un: res = isNaNF64(a) || isNaNF64(b); break;
    }
    FpExc const e = harvest();
    // Alpha: true -> 2.0 (0x4000...), false -> +0.0.
    return FpResult{ res ? 0x4000000000000000ULL : 0x0000000000000000ULL, e };
}

// ---- Conversions -----------------------------------------------------------

auto SoftFloatBackend::cvtTS(uint64_t a, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float32_t const fr = f64_to_f32(f64bits(a));   // T -> S (rounds)
    FpExc const e = harvest();
    float64_t const dr = f32_to_f64(fr);           // back to register image
    return FpResult{ dr.v, e };
}

auto SoftFloatBackend::cvtST(uint64_t a, FpExecCtx const&) -> FpResult
{
    // S -> T is an exact widening (the single's value as a double).
    softfloat_exceptionFlags = 0;
    float32_t const fa = f64_to_f32(f64bits(a));
    float64_t const dr = f32_to_f64(fa);
    return FpResult{ dr.v, FpExc{} };
}

auto SoftFloatBackend::cvtTQ(uint64_t a, FpExecCtx const& ctx) -> FpResult
{
    uint_fast8_t const rm = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    int_fast64_t const q = f64_to_i64(f64bits(a), rm, /*exact=*/true);
    uint_fast8_t const f = softfloat_exceptionFlags;
    FpExc e;
    e.ine = (f & softfloat_flag_inexact) != 0;
    e.iov = (f & softfloat_flag_invalid) != 0;   // out-of-range / NaN convert -> Alpha IOV
    return FpResult{ static_cast<uint64_t>(q), e };
}

auto SoftFloatBackend::cvtQT(uint64_t a, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float64_t const r = i64_to_f64(static_cast<int64_t>(a));
    return FpResult{ r.v, harvest() };
}

auto SoftFloatBackend::cvtQS(uint64_t a, FpExecCtx const& ctx) -> FpResult
{
    softfloat_roundingMode = resolveRm(ctx);
    softfloat_exceptionFlags = 0;
    float32_t const fr = i64_to_f32(static_cast<int64_t>(a));
    FpExc const e = harvest();
    float64_t const dr = f32_to_f64(fr);
    return FpResult{ dr.v, e };
}

// ---- VAX F_floating / G_floating -------------------------------------------
// A VAX register image read as an IEEE double == 4 * the true VAX value (the
// F and G exponent rebias is exactly 2^2; see FpFormat.h / fp_backend.h).  So
// each kernel rebiases its operands to the true value (exponent - 2, exact),
// runs the SoftFloat op, then rebiases the result back (exponent + 2).  This
// avoids the spurious double overflow a naive register-space multiply would hit
// near the VAX range limit.  INV is raised for reserved operands / dirty zeros.
//
// CAVEATS (flagged for the differential-harness oracle, like the IEEE shallow
//   path): VAX-range OVF/UNF flag detection (double's range is wider than VAX)
//   and F-format directed-rounding are approximate here -- results are correct
//   for in-range values (the OpenVMS common case); validate before conformance.
namespace {

// VAX reserved operand / dirty zero: exp==0 with sign set or a non-zero fraction
// (a clean VAX true zero is all-zero and is NOT reserved).
inline bool vaxReserved(uint64_t reg)
{
    uint64_t const exp = (reg >> 52) & 0x7FFULL;
    return exp == 0 && (reg & 0x800FFFFFFFFFFFFFULL) != 0;
}

// Adjust the IEEE-double exponent field by dexp (exact power-of-2 rescale).
// Zero/denormal operands pass through; under/overflow clamps to signed zero.
inline uint64_t scaleExp(uint64_t reg, int dexp)
{
    uint64_t const exp = (reg >> 52) & 0x7FFULL;
    if (exp == 0) return reg;
    int ne = static_cast<int>(exp) + dexp;
    if (ne <= 0)      return reg & 0x8000000000000000ULL;   // -> signed zero
    if (ne >= 0x7FF)  ne = 0x7FE;                            // clamp below inf
    return (reg & 0x800FFFFFFFFFFFFFULL) | (static_cast<uint64_t>(ne) << 52);
}

// Round a VAX-F register image (52-bit frac) to single (23-bit) precision,
// round-to-nearest-even at bit 29; clears the low 29 fraction bits.
inline uint64_t roundFreg(uint64_t reg)
{
    uint64_t const exp = (reg >> 52) & 0x7FFULL;
    if (exp == 0 || exp == 0x7FF) return reg & ~((1ULL << 29) - 1);
    uint64_t const low   = reg & ((1ULL << 29) - 1);
    uint64_t const half  = 1ULL << 28;
    uint64_t const lsb23 = (reg >> 29) & 1ULL;
    uint64_t base = reg & ~((1ULL << 29) - 1);
    if (low > half || (low == half && lsb23)) base += (1ULL << 29);  // may carry into exp
    return base;
}

// Map VAX trap mask -> FpExc; chop == round-toward-zero (the /C qualifier).
inline FpExc vaxToFpExc(uint32_t m)
{
    FpExc e;
    e.inv = (m & vax::VaxExc::Inv) != 0;
    e.dze = (m & vax::VaxExc::Dze) != 0;
    e.ovf = (m & vax::VaxExc::Ovf) != 0;
    e.unf = (m & vax::VaxExc::Unf) != 0;
    return e;
}
inline bool vaxChop(FpExecCtx const& ctx)
{ return ctx.variant.getEffectiveRoundingMode() == coreLib::FpRoundingMode::RoundTowardZero; }

} // namespace

// VAX F/G arithmetic via the bitwise-faithful integer kernels (vax_float.h):
// native unpack/normalize/round(half-up)/pack with VAX OVF/UNF thresholds.  vax::F
// and vax::G differ only by precision + exponent window, so one kernel set serves
// both.  TODO(fp-vax-validate): differential-harness the ADD/SUB/MUL/SQRT exponent
// constants against SIMH vax_f* before a conformance claim (DIV is the direct port).
auto SoftFloatBackend::addF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, false, vax::F, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::subF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, true,  vax::F, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::mulF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::mul(a, b, vax::F, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::divF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::div(a, b, vax::F, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::sqrtF(uint64_t a, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::sqrt(a, vax::F, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }

auto SoftFloatBackend::addG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, false, vax::G, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::subG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, true,  vax::G, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::mulG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::mul(a, b, vax::G, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::divG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::div(a, b, vax::G, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::sqrtG(uint64_t a, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::sqrt(a, vax::G, vaxChop(c), c.variant.underflow, x); return FpResult{ r, vaxToFpExc(x) }; }

// G compare: the *4 register scaling preserves sign/order, so compare images.
auto SoftFloatBackend::cmpG(FpCompare k, uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult
{
    softfloat_exceptionFlags = 0;
    float64_t const fa = f64bits(a), fb = f64bits(b);
    bool res = false;
    switch (k) {
        case FpCompare::Eq: res = f64_eq(fa, fb);       break;
        case FpCompare::Lt: res = f64_lt_quiet(fa, fb); break;
        case FpCompare::Le: res = f64_le_quiet(fa, fb); break;
        case FpCompare::Un: res = false;                break;  // VAX has no NaN
    }
    return FpResult{ res ? 0x4000000000000000ULL : 0ULL, harvest() };
}

// VAX conversions (true value == image/4; rebias around the IEEE conversion).
auto SoftFloatBackend::cvtGF(uint64_t a, FpExecCtx const& c) -> FpResult
{ FpExc e; if (vaxReserved(a)) { e.inv = true; return FpResult{0,e}; } return FpResult{ roundFreg(a), e }; }
auto SoftFloatBackend::cvtGD(uint64_t a, FpExecCtx const&) -> FpResult { return FpResult{ a, FpExc{} }; } // G<->D image identical here
auto SoftFloatBackend::cvtDG(uint64_t a, FpExecCtx const&) -> FpResult { return FpResult{ a, FpExc{} }; }
auto SoftFloatBackend::cvtGQ(uint64_t a, FpExecCtx const& c) -> FpResult
{
    FpExc e; if (vaxReserved(a)) { e.inv = true; return FpResult{0,e}; }
    softfloat_exceptionFlags = 0;
    int_fast64_t const q = f64_to_i64(f64bits(scaleExp(a, -2)), resolveRm(c), true);
    uint_fast8_t const f = softfloat_exceptionFlags;
    e.ine = (f & softfloat_flag_inexact) != 0;
    e.iov = (f & softfloat_flag_invalid) != 0;
    return FpResult{ static_cast<uint64_t>(q), e };
}
auto SoftFloatBackend::cvtQF(uint64_t a, FpExecCtx const& c) -> FpResult
{
    softfloat_roundingMode = resolveRm(c); softfloat_exceptionFlags = 0;
    float64_t const r = i64_to_f64(static_cast<int64_t>(a));
    return FpResult{ roundFreg(scaleExp(r.v, +2)), harvest() };
}
auto SoftFloatBackend::cvtQG(uint64_t a, FpExecCtx const& c) -> FpResult
{
    softfloat_roundingMode = resolveRm(c); softfloat_exceptionFlags = 0;
    float64_t const r = i64_to_f64(static_cast<int64_t>(a));
    return FpResult{ scaleExp(r.v, +2), harvest() };
}

} // namespace fpBox
