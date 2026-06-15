// ============================================================================
// fp_variant_core.h -- FP instruction-qualifier (trap-mode/rounding) decoding
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
// ORIGIN: ported 2026-06-10 from V1 coreLib/fp_variant_core.h, trimmed to a
//   V4-native foundation for the fBox FP build-out (task #26).  Converted Qt
//   quint types to <cstdint>; wrapped in namespace coreLib.
//
// DROPPED from the V1 source (V1-architecture couplings that do not exist in
//   V4 -- they are reintroduced at grain-wiring time against V4 types):
//     - DecodedInstruction overloads extractFunctionCode(di)/decodeVariant(di)
//       (V4 decodes via InstructionGrain, not DecodedInstruction).
//     - commitLocalFpcr() / globalFloatRegs() (V4 uses per-instance CpuState;
//       FP grains will fold ArithmeticStatus into CpuState.fpcr directly).
//     - shouldRaiseFPTrap() / getExceptionSummary() (deferred until trap
//       delivery is wired and the FPCR layout is HRM-verified -- see the
//       PROVISIONAL note in alpha_fpcr_core.h).
//     - the faultLib/PendingEvent_Refined.h include (unused by this logic).
//
// What remains is pure, self-contained: the FPVariant data model, the
// raw-bits qualifier decoders, and deriveLocalFpcr (rounding-mode shaping).
// ============================================================================

#ifndef CORELIB_FP_VARIANT_CORE_H
#define CORELIB_FP_VARIANT_CORE_H

#include <cstdint>

#include "coreLib/alpha_fpcr_core.h"

namespace coreLib {

// ----------------------------------------------------------------------------
// FpRoundingMode -- effective rounding for one operation.
// ----------------------------------------------------------------------------
enum class FpRoundingMode : uint8_t {
    UseFPCR,          // use architectural FPCR dynamic rounding mode
    RoundToNearest,   // ties to even
    RoundTowardZero,  // chop
    RoundUp,          // toward +infinity
    RoundDown         // toward -infinity
};

// ----------------------------------------------------------------------------
// FPTrapMode -- software-completion / trap-suppression class from the suffix.
// ----------------------------------------------------------------------------
enum class FPTrapMode : uint8_t {
    None      = 0,  // no suffix: use FPCR trap enables
    Underflow = 1,  // /U: enable underflow trap
    Software  = 2,  // /S: software completion
    SU        = 3,  // /SU: software + underflow suppression
    SUI       = 4   // /SUI: software + underflow + inexact suppression
};

// ----------------------------------------------------------------------------
// FPVariant -- unified decode of an FP instruction's qualifier bits.
// ----------------------------------------------------------------------------
struct FPVariant {
    FpRoundingMode roundingMode{ FpRoundingMode::UseFPCR };
    FPTrapMode     trapMode{ FPTrapMode::None };

    // Exception/trap control flags.
    bool suppressUnderflow{ false };   // /SU: suppress underflow exception
    bool suppressInexact{ false };     // /SUI: suppress inexact exception
    bool maskExceptions{ false };      // /M: no traps, but set exception flags
    bool vaxDenorm{ false };           // /D: VAX denormal handling (legacy)

    // Individual variant bit flags (from the function field).
    bool chopped{ false };             // /C bit - round toward zero
    bool minusInf{ false };            // /M bit - round toward -infinity
    bool dynamic{ false };             // /D bit - use FPCR rounding mode
    bool underflow{ false };           // /U bit - underflow trap enable
    bool overflow{ false };            // /V bit - overflow trap enable
    bool software{ false };            // /S bit - software completion
    bool inexact{ false };             // /I bit - inexact trap enable
    bool trapEnabled{ true };          // general trap enable flag
    bool inexactEnable{ false };       // alias for grain compatibility

    FPVariant() noexcept = default;

    FPVariant(FpRoundingMode rm, FPTrapMode tm, bool ie) noexcept
        : roundingMode(rm)
        , trapMode(tm)
        , inexactEnable(ie)
        , inexact(ie)
    {
        suppressUnderflow = (tm == FPTrapMode::SU || tm == FPTrapMode::SUI);
        suppressInexact   = (tm == FPTrapMode::SUI);
    }

    // Effective rounding mode after folding in the individual bit flags.
    inline FpRoundingMode getEffectiveRoundingMode() const noexcept {
        if (roundingMode != FpRoundingMode::UseFPCR)
            return roundingMode;
        if (chopped)  return FpRoundingMode::RoundTowardZero;
        if (minusInf) return FpRoundingMode::RoundDown;
        if (dynamic)  return FpRoundingMode::UseFPCR;
        return FpRoundingMode::RoundToNearest;
    }

    inline bool hasSoftwareCompletion() const noexcept {
        return software || suppressUnderflow || suppressInexact;
    }

    // ------------------------------------------------------------------------
    // Factory methods -- IEEE S-format (single precision).
    // ------------------------------------------------------------------------
    static inline FPVariant makeIEEE_S_Normal() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_Chopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_MinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_Dynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_Underflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_UnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_UnderflowMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_UnderflowDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowInexact() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowInexactChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowInexactMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_S_SoftwareUnderflowInexactDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }

    // ------------------------------------------------------------------------
    // Factory methods -- IEEE T-format (double precision).
    // ------------------------------------------------------------------------
    static inline FPVariant makeIEEE_T_Normal() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_Chopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_MinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_Dynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_Underflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_UnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_UnderflowMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_UnderflowDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowInexact() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowInexactChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowInexactMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareUnderflowInexactDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.software = true; v.suppressUnderflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }

    // ------------------------------------------------------------------------
    // Factory methods -- IEEE T-format overflow variants (for CVTTQ).
    // ------------------------------------------------------------------------
    static inline FPVariant makeIEEE_T_Overflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.overflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_OverflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.overflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_OverflowMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.overflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_OverflowDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.overflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.overflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.overflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.software = true; v.overflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.software = true; v.overflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowInexact() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.overflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowInexactChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.overflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowInexactMinusInf() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundDown; v.minusInf = true; v.software = true; v.overflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeIEEE_T_SoftwareOverflowInexactDynamic() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::UseFPCR; v.dynamic = true; v.software = true; v.overflow = true; v.suppressInexact = true; v.inexact = true; v.trapEnabled = false; return v;
    }

    // ------------------------------------------------------------------------
    // Factory methods -- VAX F-format.
    // ------------------------------------------------------------------------
    static inline FPVariant makeVAX_F_Normal() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_F_Chopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_F_Underflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_F_UnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_F_SoftwareChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeVAX_F_SoftwareUnderflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeVAX_F_SoftwareUnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }

    // ------------------------------------------------------------------------
    // Factory methods -- VAX G-format.
    // ------------------------------------------------------------------------
    static inline FPVariant makeVAX_G_Normal() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_G_Chopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_G_Underflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_G_UnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.underflow = true; v.trapEnabled = true; return v;
    }
    static inline FPVariant makeVAX_G_SoftwareChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeVAX_G_SoftwareUnderflow() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }
    static inline FPVariant makeVAX_G_SoftwareUnderflowChopped() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundTowardZero; v.chopped = true; v.software = true; v.suppressUnderflow = true; v.trapEnabled = false; return v;
    }

    static inline FPVariant makeIEEE_S_Software() noexcept {
        FPVariant v; v.roundingMode = FpRoundingMode::RoundToNearest; v.software = true; v.trapEnabled = false; return v;
    }
};

// ----------------------------------------------------------------------------
// extractFPVariantFromBits -- decode the 11-bit function field qualifier bits
// directly from the raw 32-bit instruction word.
// ----------------------------------------------------------------------------
inline FPVariant extractFPVariantFromBits(uint32_t raw) noexcept {
    FPVariant variant{};

    // FP operate function field is bits 5..15.
    uint16_t const func = static_cast<uint16_t>((raw >> 5) & 0x7FF);

    // Rounding-mode flags.
    variant.chopped  = (func & 0x400) != 0;  // /C bit (bit 10)
    variant.minusInf = (func & 0x200) != 0;  // /M bit (bit 9)
    variant.dynamic  = (func & 0x100) != 0;  // /D bit (bit 8)

    // Trap-enable flags.
    variant.underflow = (func & 0x080) != 0;  // /U bit (bit 7)
    variant.overflow  = (func & 0x040) != 0;  // /V bit (bit 6)
    variant.software  = (func & 0x020) != 0;  // /S bit (bit 5)
    variant.inexact   = (func & 0x010) != 0;  // /I bit (bit 4)
    variant.inexactEnable = variant.inexact;

    // Derive the high-level rounding mode from the individual bits.
    if (variant.chopped)       variant.roundingMode = FpRoundingMode::RoundTowardZero;
    else if (variant.minusInf) variant.roundingMode = FpRoundingMode::RoundDown;
    else if (variant.dynamic)  variant.roundingMode = FpRoundingMode::UseFPCR;
    else                       variant.roundingMode = FpRoundingMode::RoundToNearest;

    // Software-completion derived flags.
    if (variant.software && variant.underflow)
        variant.suppressUnderflow = true;
    if (variant.software && variant.underflow && !variant.inexact)
        variant.suppressInexact = true;

    return variant;
}

// ----------------------------------------------------------------------------
// decodeVariant -- decode from the low 5 bits of an FP function code.
// ----------------------------------------------------------------------------
inline FPVariant decodeVariant(uint16_t functionCode) noexcept {
    uint8_t const variantBits = static_cast<uint8_t>(functionCode & 0x1F);

    bool const inexactEnable = (variantBits & 0x01) != 0;

    uint8_t const roundBits = static_cast<uint8_t>((variantBits >> 1) & 0x03);
    FpRoundingMode roundingMode;
    switch (roundBits) {
        case 0:  roundingMode = FpRoundingMode::RoundTowardZero; break;  // /C
        case 1:  roundingMode = FpRoundingMode::RoundDown;       break;  // /M
        case 2:  roundingMode = FpRoundingMode::RoundToNearest;  break;  // normal
        case 3:  roundingMode = FpRoundingMode::UseFPCR;         break;  // /D
        default: roundingMode = FpRoundingMode::RoundToNearest;  break;
    }

    uint8_t const trapBits = static_cast<uint8_t>((variantBits >> 3) & 0x03);
    FPTrapMode trapMode;
    switch (trapBits) {
        case 0:  trapMode = FPTrapMode::None;      break;
        case 1:  trapMode = FPTrapMode::Underflow; break;
        case 2:  trapMode = FPTrapMode::Software;  break;
        case 3:  trapMode = inexactEnable ? FPTrapMode::SU : FPTrapMode::SUI; break;
        default: trapMode = FPTrapMode::None;      break;
    }

    return FPVariant(roundingMode, trapMode, inexactEnable);
}

// ----------------------------------------------------------------------------
// deriveLocalFpcr -- produce a per-operation FPCR from the architectural FPCR
// and the decoded variant: clears the sticky exception bits and applies any
// rounding-mode override the qualifier dictates.  Pure; no register access.
// ----------------------------------------------------------------------------
inline uint64_t deriveLocalFpcr(uint64_t fpcrArchitectural, const FPVariant& v) noexcept {
    uint64_t local = fpcrArchitectural;

    // Clear the sticky exception flags for this op.
    local &= ~AlphaFPCR::EXC_MASK;

    // Apply a rounding-mode override unless the op defers to the FPCR DYN field.
    FpRoundingMode const effectiveMode = v.getEffectiveRoundingMode();
    if (effectiveMode != FpRoundingMode::UseFPCR) {
        local &= ~AlphaFPCR::DYN_RM_MASK;

        uint64_t rm = 0;
        switch (effectiveMode) {
            case FpRoundingMode::RoundToNearest:  rm = AlphaFPCR::RM_NORMAL;    break;
            case FpRoundingMode::RoundTowardZero: rm = AlphaFPCR::RM_CHOPPED;   break;
            case FpRoundingMode::RoundUp:         rm = AlphaFPCR::RM_PLUS_INF;  break;
            case FpRoundingMode::RoundDown:       rm = AlphaFPCR::RM_MINUS_INF; break;
            default: break;
        }
        local |= (rm << AlphaFPCR::DYN_RM_SHIFT);
    }

    return local;
}

} // namespace coreLib

#endif // CORELIB_FP_VARIANT_CORE_H
