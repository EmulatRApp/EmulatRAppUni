// ============================================================================
// alpha_fpcr_core.h -- Alpha FPCR bit layout + host FP-exception plumbing
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
// ORIGIN: ported 2026-06-10 from V1 coreLib/alpha_fpcr_core.h, trimmed to a
//   V4-native foundation for the fBox FP build-out (task #26).  Converted Qt
//   quint types to <cstdint>.  Dropped the AlphaSSE::Config block (host SSE
//   accel is not on the build-out path).  The V1 source's TRAP_ENABLE_* and
//   ROUNDING_MASK constants were self-flagged "TODO verify" and were wrong
//   (OR-of-masks placeholders); they are not reproduced here.
//
// HRM-VERIFIED 2026-06-11 (was PROVISIONAL): the bit layout below is confirmed
//   against alpha_arch_ref.txt (Table 4-11 / Fig 4-1), the Alpha 21264/EV67 HRM
//   (Table 2-14 / Fig 2-11), and EV6_Specification_Rev_2.0 section 6.1 -- all
//   three agree.  Verification + checklist: journals/fpcr_layout_hrm_verification.md.
//   (The V1 source had the sticky bits wrongly at 49..54; 52..57 here is correct.)
//   The PROVISIONAL bar is lifted: these constants may now back the FpExc -> FPCR
//   sticky fold and trap decode.
//
// FPCR bit layout (architectural; EV6 deviations noted):
//   63    SUM   summary = OR(FPCR<57:52>); recompute when 57:52 change
//   62    INED  inexact disable
//   61    UNFD  underflow disable
//   60    UNDZ  underflow-to-zero
//   59:58 DYN   dynamic rounding mode (00 chop, 01 -inf, 10 nearest, 11 +inf)
//   57    IOV   integer overflow   (sticky; CVTGQ/CVTTQ/CVTQL only)
//   56    INE   inexact            (sticky)
//   55    UNF   underflow          (sticky)
//   54    OVF   overflow           (sticky)
//   53    DZE   divide by zero     (sticky)
//   52    INV   invalid operation  (sticky)
//   51    OVFD  overflow disable
//   50    DZED  divide-by-zero disable
//   49    INVD  invalid-operation disable
//   48    DNZ   denormal operands to zero
//   47    DNOD  *** NOT implemented on EV6 -- reserved (RAZ/IGN); no constant ***
//   24:21 FPCC  IEEE compare condition flags
//   46:0        reserved (RAZ/IGN); on EV6, 47:0 reserved
//
// Sticky-bit rule (arch ref): FPCR<57:52> are set INDEPENDENT of trap-enable
//   state (a disabled condition is still recorded), transition only 0->1 by
//   ops, and are cleared ONLY by MT_FPCR writing zero -- the fold must never
//   clear a sticky bit as a side effect.
// ============================================================================

#ifndef CORELIB_ALPHA_FPCR_CORE_H
#define CORELIB_ALPHA_FPCR_CORE_H

#include <cstdint>
#include <cfenv>

// ----------------------------------------------------------------------------
// FPCR bit constants.  Kept in a top-level namespace AlphaFPCR to match the
// reference helpers' usage (AlphaFPCR::EXC_MASK, etc.).
// ----------------------------------------------------------------------------
namespace AlphaFPCR {

// Sticky exception status bits (set when the condition occurs).
constexpr uint64_t INV = (1ULL << 52);  // Invalid operation
constexpr uint64_t DZE = (1ULL << 53);  // Divide by zero
constexpr uint64_t OVF = (1ULL << 54);  // Overflow
constexpr uint64_t UNF = (1ULL << 55);  // Underflow
constexpr uint64_t INE = (1ULL << 56);  // Inexact
constexpr uint64_t IOV = (1ULL << 57);  // Integer overflow
constexpr uint64_t SUM = (1ULL << 63);  // Summary

// All sticky exception bits.
constexpr uint64_t EXC_MASK = INV | DZE | OVF | UNF | INE | IOV;

// Dynamic rounding-mode field, FPCR<59:58>.
constexpr uint64_t DYN_RM_SHIFT = 58;
constexpr uint64_t DYN_RM_MASK  = (0x3ULL << DYN_RM_SHIFT);  // bits 59:58

// Rounding-mode field VALUES (2-bit; placed at DYN_RM_SHIFT in the register).
constexpr uint64_t RM_CHOPPED   = 0;  // toward zero (chop)
constexpr uint64_t RM_MINUS_INF = 1;  // toward -infinity
constexpr uint64_t RM_NORMAL    = 2;  // nearest, ties to even
constexpr uint64_t RM_PLUS_INF  = 3;  // toward +infinity
constexpr uint64_t ROUNDING_MASK = 0x3ULL;  // 2-bit rounding-mode value mask

// IEEE FP compare condition flags, FPCR<24:21>.
constexpr uint32_t FPCC_LT_BIT = (1u << 21);  // Less than
constexpr uint32_t FPCC_EQ_BIT = (1u << 22);  // Equal
constexpr uint32_t FPCC_GT_BIT = (1u << 23);  // Greater than
constexpr uint32_t FPCC_UN_BIT = (1u << 24);  // Unordered (NaN)

// Trap-DISABLE bits and denormal controls (FPCR<62:60,51:48>). For the deferred
// trap-enable policy (shouldRaiseFPTrap) and the denormal policy; not consulted
// by the sticky fold (sticky bits are recorded regardless of these).
constexpr uint64_t INVD = (1ULL << 49);  // invalid-operation disable
constexpr uint64_t DZED = (1ULL << 50);  // divide-by-zero disable
constexpr uint64_t OVFD = (1ULL << 51);  // overflow disable
constexpr uint64_t UNDZ = (1ULL << 60);  // underflow-to-zero
constexpr uint64_t UNFD = (1ULL << 61);  // underflow disable
constexpr uint64_t INED = (1ULL << 62);  // inexact disable
constexpr uint64_t DNZ  = (1ULL << 48);  // denormal operands to zero
// DNOD (bit 47) is NOT implemented on EV6/21264 -- bit 47 is reserved (RAZ/IGN).
// No DNOD constant is defined and no path may consult it; EV6 denormal-operand
// handling is governed by DNZ + the /S qualifier only.

// Recompute SUM (bit 63) = OR(FPCR<57:52>). SUM is modeled as a STORED bit; call
// this after any change to the sticky exception bits so SUM stays correct (so
// clearing EXC_MASK via MT_FPCR then yields SUM == 0).
inline uint64_t withRecomputedSum(uint64_t fpcr) noexcept {
    return (fpcr & EXC_MASK) ? (fpcr | SUM) : (fpcr & ~SUM);
}

} // namespace AlphaFPCR

namespace coreLib {

// ----------------------------------------------------------------------------
// ArithmeticStatus -- per-operation IEEE exception accumulator.  A grain runs
// the host op, drains the host FPU flags into one of these, then folds it into
// the architectural FPCR (at grain-wiring time, against CpuState.fpcr).
// ----------------------------------------------------------------------------
struct ArithmeticStatus {
    bool invalid     = false;
    bool divByZero   = false;
    bool overflow    = false;
    bool underflow   = false;
    bool inexact     = false;
    bool intOverflow = false;

    // Fold the accumulated conditions into an FPCR value (sets sticky bits,
    // 0->1 only) and recompute SUM. Per the arch ref, sticky bits are recorded
    // regardless of trap-enable state, so this OR is unconditional.
    void applyToFpcr(uint64_t& fpcr) const noexcept {
        if (invalid)     fpcr |= AlphaFPCR::INV;
        if (divByZero)   fpcr |= AlphaFPCR::DZE;
        if (overflow)    fpcr |= AlphaFPCR::OVF;
        if (underflow)   fpcr |= AlphaFPCR::UNF;
        if (inexact)     fpcr |= AlphaFPCR::INE;
        if (intOverflow) fpcr |= AlphaFPCR::IOV;
        fpcr = AlphaFPCR::withRecomputedSum(fpcr);
    }

    bool hasException() const noexcept {
        return invalid || divByZero || overflow || underflow;
    }
};

// ----------------------------------------------------------------------------
// Drain the host FPU exception flags into `status` and clear them.  Call
// immediately after a host FP op when honoring IEEE trap qualifiers.  Pair
// with std::feclearexcept() BEFORE the op so only this op's flags are read.
// ----------------------------------------------------------------------------
inline void checkHostFpExceptions(ArithmeticStatus& status) noexcept {
    int const e = std::fetestexcept(FE_ALL_EXCEPT);
    if (e & FE_INVALID)   status.invalid   = true;
    if (e & FE_DIVBYZERO) status.divByZero = true;
    if (e & FE_OVERFLOW)  status.overflow  = true;
    if (e & FE_UNDERFLOW) status.underflow = true;
    if (e & FE_INEXACT)   status.inexact   = true;
    std::feclearexcept(FE_ALL_EXCEPT);
}

} // namespace coreLib

#endif // CORELIB_ALPHA_FPCR_CORE_H
