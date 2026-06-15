// ============================================================================
//  DIVG/C  —  VAX G_floating divide, chopped (round toward zero)
//
//  Semantics transcribed from the SIMH-derived path in es40/AXPBox
//  (AlphaCPU_vaxfloat.cpp: vax_fdiv / vax_unpack / vax_norm / vax_rpack),
//  which matches the Alpha ARM behaviour for VAX G. No host FP in the
//  canonical path; the optional Berkeley SoftFloat path is a guarded
//  fast path / differential-test oracle.
//
//  ------------------------------------------------------------------------
//  SEAMS — identifiers assumed from the EmulatR side; wire to your headers:
//    g.fa, g.fb            : raw 64-bit register-format G operands
//                            (DIVG computes Fa / Fb : dividend=Fa, divisor=Fb)
//    fpWrite(g, u64)        : write Fc + produce success BoxResult
//    boxFpTrap(g, c, mask)  : deliver VAX arithmetic trap (OR in /S SWC bit
//                            at delivery if this leaf's qualifier set has it)
//    c.fp()                 : IFpBackend& (only if you route via the backend;
//                            the backend's software kernel *is* vaxDivG below)
//    AXP_HOT / AXP_FLATTEN  : your hot/flatten attributes
// ============================================================================

#include <cstdint>

namespace envy::fp::vaxg {

// --- VAX G register-format field geometry -----------------------------------
inline constexpr int      kSignPos  = 63;
inline constexpr int      kExpPos   = 52;
inline constexpr uint32_t kExpField = 0x7FFu;                 // 11-bit exponent
inline constexpr int      kExpBias  = 0x400;                  // excess-1024
inline constexpr uint32_t kExpMaxG  = 0x7FFu;                 // largest finite exp
inline constexpr uint64_t kHidden   = UINT64_C(1) << kExpPos; // implicit MSB
inline constexpr uint64_t kFracMask = (UINT64_C(1) << kExpPos) - 1;
inline constexpr int      kGuard    = 63 - kExpPos;           // = 11 guard bits
inline constexpr uint64_t kNormBit  = UINT64_C(1) << 63;      // UFP normalized bit
inline constexpr uint64_t kRoundBit = UINT64_C(1) << (kGuard - 1); // half-ULP (bit10)

// --- VAX arithmetic-trap mask bits (Alpha EXC_SUM ordering) -----------------
enum VaxExc : uint32_t {
    None = 0u,
    Inv  = 0x02u,   // reserved operand / invalid
    Dze  = 0x04u,   // divide by zero
    Ovf  = 0x08u,   // overflow
    Unf  = 0x10u,   // underflow
};

// --- unpacked form ----------------------------------------------------------
struct Ufp { uint32_t sign; int32_t exp; uint64_t frac; };

// Unpack register-format G -> UFP. Reserved operand (exp==0 && sign==1)
// raises INV; a clean zero (exp==0 && sign==0) becomes a true zero.
inline Ufp unpackG(uint64_t op, uint32_t& exc) noexcept {
    Ufp r;
    r.sign = uint32_t(op >> kSignPos) & 1u;
    r.exp  = int32_t((op >> kExpPos) & kExpField);
    r.frac = op & kFracMask;
    if (r.exp == 0) {
        if (r.sign != 0) exc |= VaxExc::Inv;       // reserved operand
        r.frac = 0; r.sign = 0;                     // else clean zero
        return r;
    }
    r.frac = (r.frac | kHidden) << kGuard;          // restore hidden bit + guard
    return r;
}

inline void normG(Ufp& r) noexcept {
    if (r.frac == 0) { r.sign = 0; r.exp = 0; return; }
    while ((r.frac & kNormBit) == 0) { r.frac <<= 1; --r.exp; }
}

// Restoring fraction divide (mirror of ufdiv64, prec=55).
inline uint64_t ufdiv(uint64_t dvd, uint64_t dvr, int prec) noexcept {
    uint64_t quo = 0; int i = 0;
    for (; i < prec && dvd != 0; ++i) {
        quo <<= 1;
        if (dvd >= dvr) { dvd -= dvr; quo += 1; }
        dvd <<= 1;
    }
    return quo << (63 - i + 1);   // place quotient MSB region; normG finishes it
}

// Round (chop = suppress the round-add) and pack back to register format.
inline uint64_t rpackG(Ufp& r, bool chop, bool unfEnable, uint32_t& exc) noexcept {
    if (r.frac == 0) return 0;
    if (!chop) {                                    // VAX rounding (non-/C leaf)
        r.frac += kRoundBit;
        if ((r.frac & kNormBit) == 0) {             // carry out of MSB
            r.frac = (r.frac >> 1) | kNormBit; ++r.exp;
        }
    }
    if (r.exp > int32_t(kExpMaxG)) { exc |= VaxExc::Ovf; r.exp = int32_t(kExpMaxG); }
    if (r.exp <= 0) {                               // underflow -> flush to 0
        if (unfEnable) exc |= VaxExc::Unf;
        return 0;
    }
    return (uint64_t(r.sign) << kSignPos)
         | (uint64_t(uint32_t(r.exp)) << kExpPos)
         | ((r.frac >> kGuard) & kFracMask);
}

// === Canonical integer kernel ===============================================
// dividend = Fa, divisor = Fb. Accumulates VAX traps into `exc`.
inline uint64_t vaxDivG(uint64_t dividend, uint64_t divisor,
                        bool chop, bool unfEnable, uint32_t& exc) noexcept {
    Ufp a = unpackG(dividend, exc);
    Ufp b = unpackG(divisor,  exc);
    if (b.exp == 0) { exc |= VaxExc::Dze; return 0; }   // divide by zero
    if (a.exp == 0) return 0;                            // 0 / x = 0
    a.sign ^= b.sign;
    a.exp = a.exp - b.exp + kExpBias + 1;
    a.frac >>= 1; b.frac >>= 1;                          // 1 bit of headroom
    a.frac = ufdiv(a.frac, b.frac, 55);
    normG(a);
    return rpackG(a, chop, unfEnable, exc);
}

} // namespace envy::fp::vaxg


// ============================================================================
//  The leaf
// ============================================================================
AXP_HOT AXP_FLATTEN
auto execDivgC(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    using namespace envy::fp::vaxg;

    // DIVG/C : chopped (round toward zero). Dedicated leaf => rounding is fixed
    // here, not decoded from the instruction. /C carries no /U, so underflow
    // flushes to zero silently (unfEnable = false).
    uint32_t exc = 0u;
    uint64_t const q = vaxDivG(g.fa, g.fb, /*chop=*/true, /*unfEnable=*/false, exc);

    if (exc != VaxExc::None) [[unlikely]]
        return boxFpTrap(g, c, exc);    // INV / DZE / OVF (UNF cannot fire on /C)

    return fpWrite(g, q);

    // --- IFpBackend dispatch alternative ------------------------------------
    // If you prefer to route through the backend abstraction, the body becomes:
    //
    //     auto const r = c.fp().divG(g.fa, g.fb, VaxRound::Chop);
    //     if (r.exc != VaxExc::None) [[unlikely]] return boxFpTrap(g, c, r.exc);
    //     return fpWrite(g, r.value);
    //
    // ...and IFpBackend's software kernel is exactly vaxDivG() above. A future
    // host-FP or SoftFloat backend implements the same {value, exc} contract.
}


// ============================================================================
//  Optional: Berkeley SoftFloat path  (guarded fast path / differential oracle)
//
//  VAX G and IEEE binary64 share field widths; magnitudes map by a 2-step
//  exponent rebias:   E_ieee = E_vax - 2   (out)     E_vax = E_ieee + 2  (back)
//  Chopped maps cleanly to softfloat_round_minMag (fraction widths match, so the
//  truncation boundary is bit-exact).
//
//  HARD CAVEAT: the -2 shift pushes VAX G's two smallest binades (exp field 1..2)
//  into binary64 subnormal/zero and eats the hidden bit. So this is valid ONLY
//  while operands AND result stay in binary64 normal range. Guard it; fall back
//  to vaxDivG at the edges. VAX trap thresholds always come from your own checks,
//  never from f64's IEEE flags.
// ============================================================================
#if ENVY_FP_SOFTFLOAT
extern "C" {
  #include "softfloat.h"   // John Hauser's SoftFloat-3x
}

namespace envy::fp::vaxg {

inline uint64_t rebiasGtoIeee(uint64_t g) noexcept {           // assumes exp in [3, 0x7FD]
    uint64_t const sign = g & (UINT64_C(1) << kSignPos);
    uint64_t const exp  = (g >> kExpPos) & kExpField;
    uint64_t const frac =  g & kFracMask;
    return sign | ((exp - 2) << kExpPos) | frac;
}

inline uint64_t rebiasIeeeToG(uint64_t ie, bool unfEnable, uint32_t& exc) noexcept {
    uint64_t const sign = ie & (UINT64_C(1) << kSignPos);
    uint64_t const e    = (ie >> kExpPos) & kExpField;
    uint64_t const frac =  ie & kFracMask;
    if (e == 0x7FF) { exc |= VaxExc::Ovf; return sign | (uint64_t(kExpMaxG) << kExpPos) | kFracMask; }
    if (e == 0)     { if (unfEnable) exc |= VaxExc::Unf; return 0; }   // f64 subnormal/zero
    uint64_t const ve = e + 2;
    if (ve > kExpMaxG) { exc |= VaxExc::Ovf; return sign | (uint64_t(kExpMaxG) << kExpPos) | kFracMask; }
    return sign | (ve << kExpPos) | frac;
}

inline uint64_t vaxDivG_softfloat(uint64_t dividend, uint64_t divisor,
                                  bool chop, bool unfEnable, uint32_t& exc) noexcept {
    // VAX special cases stay authoritative — decide them before touching f64.
    Ufp a = unpackG(dividend, exc);
    Ufp b = unpackG(divisor,  exc);
    if (b.exp == 0) { exc |= VaxExc::Dze; return 0; }
    if (a.exp == 0) return 0;

    // Range guard: keep both operands in binary64 normal range after the -2 shift.
    if (a.exp <= 2 || b.exp <= 2 || a.exp >= 0x7FE || b.exp >= 0x7FE)
        return vaxDivG(dividend, divisor, chop, unfEnable, exc);   // fall back

    float64_t const fa{ rebiasGtoIeee(dividend) };
    float64_t const fb{ rebiasGtoIeee(divisor)  };
    softfloat_roundingMode   = chop ? softfloat_round_minMag : softfloat_round_near_even;
    softfloat_exceptionFlags = 0;
    float64_t const fq = f64_div(fa, fb);
    return rebiasIeeeToG(fq.v, unfEnable, exc);
}

} // namespace envy::fp::vaxg
#endif // ENVY_FP_SOFTFLOAT
