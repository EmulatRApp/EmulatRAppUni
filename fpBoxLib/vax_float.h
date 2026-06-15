// ============================================================================
// fpBoxLib/vax_float.h -- VAX F_floating / G_floating integer kernels
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// The bitwise-faithful VAX float path: native integer unpack / normalize /
// round / pack, transcribed from the SIMH/SimH-Alpha reference (vax_unpack /
// vax_norm / vax_rpack / ufdiv -- AlphaCPU_vaxfloat.cpp, validated against the
// AARM).  Replaces the earlier rebias-to-double shortcut, which had wrong VAX
// rounding (round-half-up vs round-near-even), flushed the two smallest binades
// to zero, and used IEEE overflow/underflow thresholds.
//
// F vs G UNIFICATION: in REGISTER form (FpFormat.h) both formats are
//   sign<63> | exp11<62:52> | frac<51:0>, with the SAME excess-1024 exponent
//   (Alpha's LDF expands VAX-F's 8-bit excess-128 exponent to exp11 = exp8+896
//   = trueExp + 1024).  G uses all 52 fraction bits; F uses the top 23 (bits
//   51:29).  So ONE kernel parameterized by a VaxGeom (precision + exponent
//   range) serves both -- F just rounds to 23 bits and clamps to a narrower
//   exponent window.
//
// CONTRACT: kernels take register-image operands, return the register-image
//   result, and OR VaxExc bits into `exc` (sticky; fold into FPCR above, real
//   arithmetic-trap delivery is deferred).  No host FP is used.
//
// VALIDATION: ADD/SUB/MUL/SQRT are implemented to the standard VAX normalize/
// round algorithm with these helpers; DIVIDE is the direct SIMH ufdiv port.
// All of it must pass the SoftFloat differential harness (and ideally a SIMH
// cross-check on the per-op exponent constants) before it backs a conformance
// claim -- see TODO(fp-vax-validate).
// ============================================================================

#ifndef FPBOXLIB_VAX_FLOAT_H
#define FPBOXLIB_VAX_FLOAT_H
 
#include <cstdint>
#if defined(_MSC_VER)
#  include <intrin.h>   // _umul128 / __umulh -- MSVC has no __int128
#endif

namespace fpBox::vax {

// Portable unsigned 64x64 -> 128 multiply: returns the low 64 bits and writes
// the high 64 to `hi`.  __int128 is a GCC/Clang extension (absent on MSVC), so
// this is guarded per toolchain/architecture with a 32x32 schoolbook fallback.
inline uint64_t umul128(uint64_t a, uint64_t b, uint64_t& hi) noexcept
{
#if defined(__SIZEOF_INT128__)
    unsigned __int128 const p = static_cast<unsigned __int128>(a) * b;
    hi = static_cast<uint64_t>(p >> 64);
    return static_cast<uint64_t>(p);
#elif defined(_MSC_VER) && defined(_M_X64)
    return _umul128(a, b, &hi);
#elif defined(_MSC_VER) && defined(_M_ARM64)
    hi = __umulh(a, b);
    return a * b;
#else
    uint64_t const aL = a & 0xFFFFFFFFu, aH = a >> 32;
    uint64_t const bL = b & 0xFFFFFFFFu, bH = b >> 32;
    uint64_t const ll = aL * bL, lh = aL * bH, hl = aH * bL, hh = aH * bH;
    uint64_t const mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return (mid << 32) | (ll & 0xFFFFFFFFu);
#endif
}

// ---- Register-format field geometry (shared F/G) ---------------------------
inline constexpr int      kSignPos  = 63;
inline constexpr int      kExpPos   = 52;
inline constexpr uint32_t kExpField = 0x7FFu;                  // 11-bit exponent
inline constexpr int      kExpBias  = 0x400;                   // excess-1024 (register)
inline constexpr uint64_t kHidden   = UINT64_C(1) << kExpPos;  // implicit MSB
inline constexpr uint64_t kFracMask = (UINT64_C(1) << kExpPos) - 1;
inline constexpr int      kGuard    = 63 - kExpPos;            // 11 guard bits
inline constexpr uint64_t kNormBit  = UINT64_C(1) << 63;       // UFP normalized bit

// Per-format parameters.  exp values are register exp11 (excess-1024).
struct VaxGeom {
    int fracBits;   // significant fraction bits (G=52, F=23)
    int expHi;      // largest valid register exp11
    int expLo;      // smallest valid register exp11 (below -> underflow)
};
inline constexpr VaxGeom G{ 52, 0x7FF, 1   };   // G: exp8-native, full range
inline constexpr VaxGeom F{ 23, 1151,  897 };   // F: exp11 = exp8(1..255) + 896

// ---- VAX arithmetic-trap mask bits (Alpha EXC_SUM ordering) ----------------
enum VaxExc : uint32_t { None = 0u, Inv = 0x02u, Dze = 0x04u, Ovf = 0x08u, Unf = 0x10u };

// ---- Unpacked form ---------------------------------------------------------
struct Ufp { uint32_t sign; int32_t exp; uint64_t frac; };

// Unpack register image -> UFP.  Reserved operand (exp==0 && sign==1) raises
// INV; a clean zero (exp==0 && sign==0 && frac==0) becomes a true zero.
inline Ufp unpack(uint64_t op, uint32_t& exc) noexcept
{
    Ufp r;
    r.sign = static_cast<uint32_t>(op >> kSignPos) & 1u;
    r.exp  = static_cast<int32_t>((op >> kExpPos) & kExpField);
    r.frac = op & kFracMask;
    if (r.exp == 0) {
        if (r.sign != 0) exc |= VaxExc::Inv;      // reserved operand / dirty zero
        r.frac = 0; r.sign = 0;
        return r;
    }
    r.frac = (r.frac | kHidden) << kGuard;        // hidden bit -> bit 63, +guard
    return r;
}

inline void norm(Ufp& r) noexcept
{
    if (r.frac == 0) { r.sign = 0; r.exp = 0; return; }
    while ((r.frac & kNormBit) == 0) { r.frac <<= 1; --r.exp; }
}

// Round (round-half-up unless chop) to `g.fracBits` precision and pack back to
// register format, applying VAX overflow/underflow thresholds for the format.
inline uint64_t rpack(Ufp& r, VaxGeom g, bool chop, bool unfEnable, uint32_t& exc) noexcept
{
    if (r.frac == 0) return 0;
    int const roundShift = 63 - g.fracBits;                   // LSB position in UFP
    if (!chop) {
        uint64_t const roundBit = UINT64_C(1) << (roundShift - 1);
        r.frac += roundBit;
        if ((r.frac & kNormBit) == 0) { r.frac = (r.frac >> 1) | kNormBit; ++r.exp; }
    }
    // Clear bits below the format's LSB so F results have a clean 23-bit fraction.
    r.frac &= ~((UINT64_C(1) << roundShift) - 1);
    if (r.exp > g.expHi) { exc |= VaxExc::Ovf; r.exp = g.expHi; }
    if (r.exp < g.expLo) { if (unfEnable) exc |= VaxExc::Unf; return 0; }  // flush to 0
    return (static_cast<uint64_t>(r.sign) << kSignPos)
         | (static_cast<uint64_t>(static_cast<uint32_t>(r.exp)) << kExpPos)
         | ((r.frac >> kGuard) & kFracMask);
}

// Restoring fraction divide (SIMH ufdiv64, prec quotient bits).
inline uint64_t ufdiv(uint64_t dvd, uint64_t dvr, int prec) noexcept
{
    uint64_t quo = 0; int i = 0;
    for (; i < prec && dvd != 0; ++i) {
        quo <<= 1;
        if (dvd >= dvr) { dvd -= dvr; quo += 1; }
        dvd <<= 1;
    }
    return quo << (63 - i + 1);
}

// ---- Operation kernels (a OP b -> register image, exc accumulated) ---------

inline uint64_t addsub(uint64_t opa, uint64_t opb, bool sub, VaxGeom g,
                       bool chop, bool unfEnable, uint32_t& exc) noexcept
{
    Ufp a = unpack(opa, exc);
    Ufp b = unpack(opb, exc);
    if (sub) b.sign ^= 1u;
    if (a.exp == 0) { Ufp t = b; return rpack(t, g, chop, unfEnable, exc); }  // 0 +- b = b
    if (b.exp == 0) return rpack(a, g, chop, unfEnable, exc);                 // a +- 0 = a
    // Align the smaller exponent to the larger (guard bits absorb the shift).
    if (a.exp < b.exp) { Ufp t = a; a = b; b = t; }
    int const sh = a.exp - b.exp;
    b.frac = (sh >= 64) ? 0 : (b.frac >> sh);
    if (a.sign == b.sign) {
        a.frac += b.frac;
        if (a.frac < b.frac) { a.frac = (a.frac >> 1) | kNormBit; ++a.exp; }  // carry
    } else {
        a.frac -= b.frac;                                                     // |a| >= |b|
    }
    norm(a);
    return rpack(a, g, chop, unfEnable, exc);
}

inline uint64_t mul(uint64_t opa, uint64_t opb, VaxGeom g,
                    bool chop, bool unfEnable, uint32_t& exc) noexcept
{
    Ufp a = unpack(opa, exc);
    Ufp b = unpack(opb, exc);
    if (a.exp == 0 || b.exp == 0) return 0;                  // x * 0 = 0
    a.sign ^= b.sign;
    a.exp = a.exp + b.exp - kExpBias - 1;                    // excess-1024 product
    // a.frac, b.frac in [2^63, 2^64) -> product p in [2^126, 2^128).  Mantissa
    // product in [1,4): take p>>63 when <2.0, else p>>64 and bump the exponent
    // (p>>63 alone would overflow 64 bits for the >=2.0 case).
    uint64_t pHi; uint64_t const pLo = umul128(a.frac, b.frac, pHi);
    if ((pHi >> 63) != 0) {                  // bit 127 set: mantissa product >= 2.0
        a.frac = pHi; ++a.exp;               // == p >> 64
    } else {
        a.frac = (pHi << 1) | (pLo >> 63);   // == p >> 63
    }
    norm(a);
    return rpack(a, g, chop, unfEnable, exc);
}

inline uint64_t div(uint64_t opa, uint64_t opb, VaxGeom g,
                    bool chop, bool unfEnable, uint32_t& exc) noexcept
{
    Ufp a = unpack(opa, exc);
    Ufp b = unpack(opb, exc);
    if (b.exp == 0) { exc |= VaxExc::Dze; return 0; }
    if (a.exp == 0) return 0;
    a.sign ^= b.sign;
    a.exp = a.exp - b.exp + kExpBias + 1;
    a.frac >>= 1; b.frac >>= 1;                              // 1 bit of headroom
    a.frac = ufdiv(a.frac, b.frac, 55);
    norm(a);
    return rpack(a, g, chop, unfEnable, exc);
}

inline uint64_t sqrt(uint64_t opa, VaxGeom g, bool chop, bool unfEnable, uint32_t& exc) noexcept
{
    Ufp a = unpack(opa, exc);
    if (a.sign != 0 && a.exp != 0) { exc |= VaxExc::Inv; return 0; }  // sqrt(neg) -> INV
    if (a.exp == 0) return 0;                                         // sqrt(0) = 0
    // Even-exponent setup so the mantissa stays in [1,4): result exp halves.
    int e = a.exp - kExpBias - 1;                            // true exponent
    uint64_t m = a.frac;                                     // in [2^63, 2^64)
    if (e & 1) { m >>= 1; ++e; }                             // make e even, keep m normalized-ish
    a.exp = (e >> 1) + kExpBias + 1;
    // Integer sqrt of the 128-bit-aligned mantissa, producing a 64-bit root.
    // radicand = m << 62 as a 128-bit value (radHi:radLo); compare trial^2 to it.
    uint64_t const radHi = m >> 2;
    uint64_t const radLo = m << 62;
    uint64_t root = 0;
    for (int bit = 63; bit >= 0; --bit) {
        uint64_t const trial = root | (UINT64_C(1) << bit);
        uint64_t tHi; uint64_t const tLo = umul128(trial, trial, tHi);
        if (tHi < radHi || (tHi == radHi && tLo <= radLo)) root = trial;
    }
    a.frac = root;
    norm(a);
    return rpack(a, g, chop, unfEnable, exc);
}

} // namespace fpBox::vax

#endif // FPBOXLIB_VAX_FLOAT_H
