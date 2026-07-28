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

// VAX-op variant of resolveRm.  AARM 4.7.6: normal VAX rounding is BIASED --
// exact halfway cases round to the LARGER magnitude, never to even.  SoftFloat's
// near_maxMag is exactly that.  The dynamic (/D) FPCR modes and the directed
// modes are unchanged; only the "normal" tie rule differs from IEEE.  Used by
// the VAX conversions below (the vax_float.h arithmetic kernels already round
// half-up natively).  Audit FV-2, 2026-07-28.
inline auto resolveRmVax(FpExecCtx const& ctx) -> uint_fast8_t
{
    uint_fast8_t const rm = resolveRm(ctx);
    return (rm == softfloat_round_near_even) ? softfloat_round_near_maxMag : rm;
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

inline bool isFiniteF64(uint64_t u)
{
    return (u & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

// Low-order 64 bits of the integer part of a FINITE f64 (truncated toward
// zero, two's complement for negatives) -- i.e. trunc(v) mod 2^64.  AARM
// 4.7.7.9: "If an integer overflow occurs in CVTxQ ... the true result
// truncated to the low-order 64 ... bits ... is stored in the result
// register" -- NOT the saturated INT64_MAX/MIN pattern SoftFloat produces.
// Overflow implies |v| >= 2^63 > 2^53, so v is an exact integer there and
// rounding mode cannot alter the true result.  Audit FV-7, 2026-07-28.
inline uint64_t f64TruncLow64(uint64_t bits)
{
    int const expF = static_cast<int>((bits >> 52) & 0x7FFULL);
    int const e    = expF - 1023;                       // unbiased exponent
    uint64_t mag = 0;
    if (expF != 0 && e >= 0) {                          // |v| >= 1.0
        uint64_t const sig = (bits & 0x000FFFFFFFFFFFFFULL) | (1ULL << 52);
        int const sh = e - 52;                          // value = sig * 2^sh
        if (sh >= 64)      mag = 0;                     // all bits above bit 63
        else if (sh >= 0)  mag = sig << sh;             // natural mod-2^64 shift
        else               mag = sig >> (-sh);          // truncate the fraction
    }
    return (bits >> 63) ? (0ULL - mag) : mag;
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
    // Integer overflow of a FINITE operand: AARM 4.7.7.9 stores the true
    // result's low-order 64 bits, not SoftFloat's INT64_MAX/MIN saturation
    // (NaN/Inf operands keep the SoftFloat pattern -- no true result exists).
    // Audit FV-7, 2026-07-28.
    uint64_t bits = static_cast<uint64_t>(q);
    if (e.iov && isFiniteF64(a)) bits = f64TruncLow64(a);
    return FpResult{ bits, e };
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
// near the VAX range limit.  Operand-check policy (audit FV-8): reserved
// operands raise INV in every mode; dirty zeros raise INV in the default trap
// modes but are treated as clean zeros under /S software completion.
// F-format results now route through the native vax::rpack (VAX rounding +
// F exponent-window OVF/UNF) -- the old approximate roundFreg is gone (FV-6).
namespace {

// /S software-completion class for this op (AARM 4.7.7.1 mode split: dirty
// zeros are treated as clean zeros under /S-family qualifiers, trap INV in the
// default modes; reserved operands trap INV in every mode).  Audit FV-8.
inline bool vaxSwc(FpExecCtx const& ctx)
{ return ctx.variant.hasSoftwareCompletion(); }

// Does this VAX register image signal INV under the operand-check policy?
// exp==0 && sign==1 (reserved operand): always.  exp==0, sign==0, frac!=0
// (dirty zero): only outside software completion.  Clean zero: never.
inline bool vaxOperandInv(uint64_t reg, bool swc)
{
    uint64_t const exp = (reg >> 52) & 0x7FFULL;
    if (exp != 0) return false;
    if ((reg >> 63) != 0) return true;                        // reserved operand
    return !swc && (reg & 0x000FFFFFFFFFFFFFULL) != 0;        // dirty zero
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

// Round a G register image to F precision + range via the native VAX kernel
// helpers: vax::rpack rounds half-up (or chops) at 23 fraction bits and
// applies the F exponent window (register exp11 897..1151), recording Ovf/Unf.
// Replaces the old roundFreg, which hardcoded IEEE ties-to-even, ignored /C,
// and never range-checked (AARM 4.10.11: CVTGF "rounds or chops to single
// precision, then the 8-bit exponent range is checked for overflow/
// underflow").  Audit FV-6, 2026-07-28.
inline uint64_t packFRange(uint64_t gImage, bool chop, bool unfEnable, bool swc,
                           uint32_t& exc)
{
    vax::Ufp u = vax::unpack(gImage, exc, swc);
    return vax::rpack(u, vax::F, chop, unfEnable, exc);
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
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, false, vax::F, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::subF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, true,  vax::F, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::mulF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::mul(a, b, vax::F, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::divF(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::div(a, b, vax::F, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::sqrtF(uint64_t a, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::sqrt(a, vax::F, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }

auto SoftFloatBackend::addG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, false, vax::G, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::subG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::addsub(a, b, true,  vax::G, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::mulG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::mul(a, b, vax::G, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::divG(uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::div(a, b, vax::G, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }
auto SoftFloatBackend::sqrtG(uint64_t a, FpExecCtx const& c) -> FpResult
{ uint32_t x = 0; uint64_t r = vax::sqrt(a, vax::G, vaxChop(c), c.variant.underflow, vaxSwc(c), x); return FpResult{ r, vaxToFpExc(x) }; }

// G compare via the unpacked VAX fields.  The old image compare through IEEE
// f64 predicates misread valid top-binade G values (register exp11 == 0x7FF)
// as Inf/NaN -> unordered -> false, and never signaled INV for reserved
// operands / dirty zeros (AARM 4.10.7: "an invalid operation trap is signaled
// if either operand has exp=0 and is not a true zero").  Sign + lexicographic
// (exp, frac) ordering is exact for VAX finites; unpack normalizes every
// exp==0 operand to a true zero (sign=0), so zero orders correctly against
// both signs.  Audit FV-4, 2026-07-28.
auto SoftFloatBackend::cmpG(FpCompare k, uint64_t a, uint64_t b, FpExecCtx const& c) -> FpResult
{
    uint32_t x = 0;
    bool const swc = vaxSwc(c);
    vax::Ufp const ua = vax::unpack(a, x, swc);
    vax::Ufp const ub = vax::unpack(b, x, swc);
    int rel;                                            // -1: a<b, 0: a==b, +1: a>b
    if (ua.exp == 0 && ub.exp == 0) {
        rel = 0;                                        // -0/dirty already zeroed
    } else if (ua.sign != ub.sign) {
        rel = (ua.sign != 0) ? -1 : 1;
    } else {
        int mag;                                        // |a| vs |b|
        if (ua.exp != ub.exp)       mag = (ua.exp < ub.exp) ? -1 : 1;
        else if (ua.frac != ub.frac) mag = (ua.frac < ub.frac) ? -1 : 1;
        else                         mag = 0;
        rel = (ua.sign != 0) ? -mag : mag;
    }
    bool res = false;
    switch (k) {
        case FpCompare::Eq: res = (rel == 0); break;
        case FpCompare::Lt: res = (rel <  0); break;
        case FpCompare::Le: res = (rel <= 0); break;
        case FpCompare::Un: res = false;      break;    // VAX has no unordered
    }
    return FpResult{ res ? 0x4000000000000000ULL : 0ULL, vaxToFpExc(x) };
}

// VAX conversions (true value == image/4; rebias around the IEEE conversion).
// CVTGF routes through the native unpack/rpack kernel with vax::F geometry so
// it gets VAX half-up rounding, /C chop, and the F exponent-window check with
// Ovf/Unf recording (audit FV-6; AARM 4.10.11 note: "rounds or chops to
// single precision, then the 8-bit exponent range is checked").
auto SoftFloatBackend::cvtGF(uint64_t a, FpExecCtx const& c) -> FpResult
{
    uint32_t x = 0;
    uint64_t const r = packFRange(a, vaxChop(c), c.variant.underflow, vaxSwc(c), x);
    return FpResult{ r, vaxToFpExc(x) };
}
auto SoftFloatBackend::cvtGD(uint64_t a, FpExecCtx const&) -> FpResult { return FpResult{ a, FpExc{} }; } // G<->D image identical here
auto SoftFloatBackend::cvtDG(uint64_t a, FpExecCtx const&) -> FpResult { return FpResult{ a, FpExc{} }; }
auto SoftFloatBackend::cvtGQ(uint64_t a, FpExecCtx const& c) -> FpResult
{
    FpExc e;
    bool const swc = vaxSwc(c);
    if (vaxOperandInv(a, swc)) { e.inv = true; return FpResult{0,e}; }   // FV-8 policy
    if (((a >> 52) & 0x7FFULL) == 0) return FpResult{ 0, e };  // true/dirty zero -> exact 0
    softfloat_exceptionFlags = 0;
    uint64_t const scaled = scaleExp(a, -2);   // exact /4: register image -> true value
    int_fast64_t const q = f64_to_i64(f64bits(scaled), resolveRmVax(c), true);
    uint_fast8_t const f = softfloat_exceptionFlags;
    e.ine = (f & softfloat_flag_inexact) != 0;
    e.iov = (f & softfloat_flag_invalid) != 0;
    // Overflow: store the true result's low-order 64 bits (AARM 4.7.7.9), not
    // SoftFloat's saturation.  `scaled` is always finite (the *4 register bias
    // keeps valid G exponents below the IEEE Inf/NaN encoding after -2).
    // Audit FV-7, 2026-07-28.
    uint64_t bits = static_cast<uint64_t>(q);
    if (e.iov) bits = f64TruncLow64(scaled);
    return FpResult{ bits, e };
}
auto SoftFloatBackend::cvtQF(uint64_t a, FpExecCtx const& c) -> FpResult
{
    softfloat_roundingMode = resolveRmVax(c); softfloat_exceptionFlags = 0;
    float64_t const r = i64_to_f64(static_cast<int64_t>(a));
    FpExc e = harvest();
    // Round to F precision via the native kernel (VAX half-up / chop) instead
    // of the old ties-to-even roundFreg (audit FV-6).  i64_to_f64 is exact for
    // |q| < 2^53; above that the i64->f64 step rounds to 52 bits before rpack
    // rounds to 23 -- a named double-rounding deviation, reachable only when
    // the 52-bit intermediate lands exactly on a 24-bit halfway point.  The F
    // exponent window cannot over/underflow from an int64 (|q| <= 2^63 <<
    // F max ~1.7e38), so the range check is vacuous-but-correct here.
    uint32_t x = 0;
    uint64_t const f = packFRange(scaleExp(r.v, +2), vaxChop(c),
                                  c.variant.underflow, vaxSwc(c), x);
    FpExc const ve = vaxToFpExc(x);
    e.inv = e.inv || ve.inv; e.ovf = e.ovf || ve.ovf; e.unf = e.unf || ve.unf;
    return FpResult{ f, e };
}
auto SoftFloatBackend::cvtQG(uint64_t a, FpExecCtx const& c) -> FpResult
{
    softfloat_roundingMode = resolveRmVax(c); softfloat_exceptionFlags = 0;
    float64_t const r = i64_to_f64(static_cast<int64_t>(a));
    return FpResult{ scaleExp(r.v, +2), harvest() };
}

} // namespace fpBox
