// ============================================================================
// alpha_fp_ieee_inl.h - Convert IEEE double to VAX G-format representation
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic) / ChatGPT (OpenAI)
//
// Commercial use prohibited without separate license.
// Contact: peert@envysys.com | https://envysys.com
// Documentation: https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================

#ifndef ALPHA_FP_IEEE_INL_H
#define ALPHA_FP_IEEE_INL_H

#include "coreLib/alpha_fp_helpers_inl.h"
// ============================================================================
// G-FORMAT (VAX DOUBLE PRECISION) COMPARISON HELPERS
// ============================================================================

/**
 * @brief Convert IEEE double to VAX G-format representation
 * @param ieee_val IEEE 754 double precision value
 * @return VAX G-format equivalent (stored as double for compatibility)
 */
#include <cfenv>
#include <cmath>
#include <limits>
#include <qtypes.h>

#include "coreLib/Axp_Attributes_core.h"
#include "coreLib/fp_variant_core.h"
AXP_HOT AXP_ALWAYS_INLINE double ieeeToVaxG(double ieee_val) noexcept
{
    // VAX G-format differences from IEEE:
    // 1. No IEEE infinities or NaNs
    // 2. Different exponent bias
    // 3. Different bit layout

    if (std::isnan(ieee_val) || std::isinf(ieee_val)) {
        // VAX has no NaN/Inf - treat as zero or reserved fault
        return 0.0;
    }

    // For now, use IEEE semantics but with VAX exception handling
    // TODO: Implement full VAX G-format bit conversion if needed
    return ieee_val;
}


// ============================================================================
// F-FORMAT (VAX SINGLE PRECISION) COMPARISON HELPERS (FOR COMPLETENESS)
// ============================================================================

/**
 * @brief Convert IEEE double to VAX F-format representation
 */
AXP_HOT AXP_ALWAYS_INLINE float ieeeToVaxF(double ieee_val) noexcept
{
    if (std::isnan(ieee_val) || std::isinf(ieee_val)) {
        return 0.0f;  // VAX F has no NaN/Inf
    }
    return static_cast<float>(ieee_val);
}

// ============================================================================
// Set IEEE Rounding Mode from Variant
// ============================================================================
// ============================================================================
// applyVariantRoundingMode - Set IEEE rounding mode
// ============================================================================
// Called before FP operation to set host FPU rounding mode.

// This is what alpha_fp_helpers_inl.h already does correctly.
//
AXP_HOT AXP_ALWAYS_INLINE  void applyVariantRoundingMode(const FPVariant& variant) noexcept
{
    switch (variant.roundingMode) {
    case FpRoundingMode::RoundToNearest:
        std::fesetround(FE_TONEAREST);
        break;

    case FpRoundingMode::RoundTowardZero:
        std::fesetround(FE_TOWARDZERO);
        break;

    case FpRoundingMode::RoundUp:
        std::fesetround(FE_UPWARD);
        break;

    case FpRoundingMode::RoundDown:
        std::fesetround(FE_DOWNWARD);
        break;

    case FpRoundingMode::UseFPCR:
        // Leave current rounding mode unchanged
        break;
    }
}


// ============================================================================
// Update FPCR with exceptions (respecting variant suppression)
// ============================================================================
AXP_HOT AXP_ALWAYS_INLINE void updateFPCR_variant(uint64_t& fpcr, const FPVariant& variant) noexcept
{
    int exceptions = std::fetestexcept(FE_ALL_EXCEPT);

    // Set exception bits in FPCR
    if (exceptions & FE_INVALID)   fpcr |= AlphaFPCR::INV;
    if (exceptions & FE_DIVBYZERO) fpcr |= AlphaFPCR::DZE;
    if (exceptions & FE_OVERFLOW)  fpcr |= AlphaFPCR::OVF;

    // Underflow: suppress if /SU or /SUI variant
    if (exceptions & FE_UNDERFLOW) {
        if (!variant.suppressUnderflow) {
            fpcr |= AlphaFPCR::UNF;
        }
    }

    // Inexact: suppress if /SUI variant
    if (exceptions & FE_INEXACT) {
        if (!variant.suppressInexact) {
            fpcr |= AlphaFPCR::INE;
        }
    }

    std::feclearexcept(FE_ALL_EXCEPT);
}

// ============================================================================
// Legacy updateFPCR (no variant - backward compatibility)
// ============================================================================
inline void updateFPCR(uint64_t& fpcr) noexcept
{
    FPVariant defaultVariant;
    updateFPCR_variant(fpcr, defaultVariant);
}


// ============================================================================
// SQUARE ROOT OPERATIONS - SSE-OPTIMIZED WITH ALPHA QUALIFIER SUPPORT
// ============================================================================

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>  // SSE2 intrinsics
#define AXP_HAS_SSE2 1
#else
#define AXP_HAS_SSE2 0
#endif

/**
 * @brief IEEE T-format (double precision) square root
 * @param x Input value (64-bit double)
 * @param fpcr Floating-point control register (updated with exceptions)
 * @param variant FP variant flags (/C, /U, /S, /SU, /SUI qualifiers)
 * @return Square root of x in IEEE 754 double precision
 *
 * Supports Alpha qualifiers:
 * - /C  = Chopped (round toward zero)
 * - /U  = Underflow trap enable
 * - /S  = Software completion enable
 * - /SU = Software + underflow suppression
 * - /SUI = Software + underflow + inexact suppression
 */
AXP_HOT AXP_ALWAYS_INLINE double sqrtT_variant(double x, uint64_t& fpcr, const FPVariant& variant) noexcept
{
    // Apply rounding mode from variant
    applyVariantRoundingMode(variant);
    std::feclearexcept(FE_ALL_EXCEPT);

    // Check for invalid input (negative)
    if (x < 0.0) {
        fpcr |= AlphaFPCR::INV;  // Invalid operation
        if (variant.trapEnabled) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    double result;

#if AXP_HAS_SSE2
    // Use SSE2 SQRTSD instruction for hardware acceleration
    __m128d xv = _mm_set_sd(x);
    __m128d rv = _mm_sqrt_sd(xv, xv);
    result = _mm_cvtsd_f64(rv);
#else
    // Fallback: Newton-Raphson method
    if (x == 0.0) {
        result = 0.0;
    }
    else {
        // Fast initial guess using bit manipulation
        union { double d; uint64_t i; } u = { x };
        u.i = (u.i >> 1) + (0x3FEULL << 52);
        double guess = u.d;

        // 4 Newton iterations for full double precision
        for (int i = 0; i < 4; i++) {
            guess = 0.5 * (guess + x / guess);
        }
        result = guess;
    }
#endif

    // Update FPCR with exceptions (respecting variant suppression)
    updateFPCR_variant(fpcr, variant);

    return result;
}

/**
 * @brief IEEE S-format (single precision) square root
 * @param x Input value (stored as double, treated as float)
 * @param fpcr Floating-point control register
 * @param variant FP variant flags
 * @return Square root in single precision (stored as double)
 */
AXP_HOT AXP_ALWAYS_INLINE double sqrtS_variant(double x, uint64_t& fpcr, const FPVariant& variant) noexcept
{
    applyVariantRoundingMode(variant);
    std::feclearexcept(FE_ALL_EXCEPT);

    // Convert to single precision
    float xf = static_cast<float>(x);

    // Check for invalid input
    if (xf < 0.0f) {
        fpcr |= AlphaFPCR::INV;
        if (variant.trapEnabled) {
            return static_cast<double>(std::numeric_limits<float>::quiet_NaN());
        }
        return static_cast<double>(std::numeric_limits<float>::quiet_NaN());
    }

    float result;

#if AXP_HAS_SSE2
    // Use SSE SQRTSS instruction
    __m128 xv = _mm_set_ss(xf);
    __m128 rv = _mm_sqrt_ss(xv);
    result = _mm_cvtss_f32(rv);
#else
    // Fallback: Newton-Raphson for float
    if (xf == 0.0f) {
        result = 0.0f;
    }
    else {
        union { float f; quint32 i; } u = { xf };
        u.i = (u.i >> 1) + 0x1FBD1DF5;  // Magic constant for float
        float guess = u.f;

        // 3 iterations for single precision
        for (int i = 0; i < 3; i++) {
            guess = 0.5f * (guess + xf / guess);
        }
        result = guess;
    }
#endif

    updateFPCR_variant(fpcr, variant);

    return static_cast<double>(result);
}

/**
 * @brief VAX G-format (double precision) square root
 * @param x Input value (VAX G-format semantics)
 * @param fpcr Floating-point control register
 * @param variant FP variant flags
 * @return Square root in VAX G-format
 *
 * VAX G-format differences:
 * - No IEEE NaN or Infinity
 * - Reserved operand fault for invalid values
 * - Different exception handling
 */
AXP_HOT AXP_ALWAYS_INLINE double sqrtG_variant(double x, uint64_t& fpcr, const FPVariant& variant) noexcept
{
    applyVariantRoundingMode(variant);
    std::feclearexcept(FE_ALL_EXCEPT);

    // VAX G-format: check for reserved operands
    if (std::isnan(x) || std::isinf(x)) {
        if (variant.trapEnabled) {
            fpcr |= AlphaFPCR::INV;  // Reserved operand fault
        }
        return 0.0;  // VAX returns zero for reserved operands
    }

    // Check for negative input
    if (x < 0.0) {
        if (variant.trapEnabled) {
            fpcr |= AlphaFPCR::INV;
        }
        return 0.0;
    }

    double result;

#if AXP_HAS_SSE2
    __m128d xv = _mm_set_sd(x);
    __m128d rv = _mm_sqrt_sd(xv, xv);
    result = _mm_cvtsd_f64(rv);
#else
    if (x == 0.0) {
        result = 0.0;
    }
    else {
        union { double d; uint64_t i; } u = { x };
        u.i = (u.i >> 1) + (0x3FEULL << 52);
        double guess = u.d;

        for (int i = 0; i < 4; i++) {
            guess = 0.5 * (guess + x / guess);
        }
        result = guess;
    }
#endif

    // VAX exception handling (no underflow/inexact for SQRT)
    updateFPCR_variant(fpcr, variant);

    return result;
}

/**
 * @brief VAX F-format (single precision) square root
 * @param x Input value (VAX F-format semantics)
 * @param fpcr Floating-point control register
 * @param variant FP variant flags
 * @return Square root in VAX F-format
 */
AXP_HOT AXP_ALWAYS_INLINE double sqrtF_variant(double x, uint64_t& fpcr, const FPVariant& variant) noexcept
{
    applyVariantRoundingMode(variant);
    std::feclearexcept(FE_ALL_EXCEPT);

    // Convert to VAX F-format
    float xf = ieeeToVaxF(x);

    // Check for reserved operands
    if (xf == 0.0f && (std::isnan(x) || std::isinf(x))) {
        if (variant.trapEnabled) {
            fpcr |= AlphaFPCR::INV;
        }
        return 0.0;
    }

    // Check for negative
    if (xf < 0.0f) {
        if (variant.trapEnabled) {
            fpcr |= AlphaFPCR::INV;
        }
        return 0.0;
    }

    float result;

#if AXP_HAS_SSE2
    __m128 xv = _mm_set_ss(xf);
    __m128 rv = _mm_sqrt_ss(xv);
    result = _mm_cvtss_f32(rv);
#else
    if (xf == 0.0f) {
        result = 0.0f;
    }
    else {
        union { float f; quint32 i; } u = { xf };
        u.i = (u.i >> 1) + 0x1FBD1DF5;
        float guess = u.f;

        for (int i = 0; i < 3; i++) {
            guess = 0.5f * (guess + xf / guess);
        }
        result = guess;
    }
#endif

    updateFPCR_variant(fpcr, variant);

    return static_cast<double>(result);
}

// ============================================================================
// CONVENIENCE WRAPPERS - For backward compatibility without variants
// ============================================================================

AXP_HOT AXP_ALWAYS_INLINE double sqrtT(double x, uint64_t& fpcr) noexcept {
    FPVariant defaultVariant;
    return sqrtT_variant(x, fpcr, defaultVariant);
}

AXP_HOT AXP_ALWAYS_INLINE double sqrtS(double x, uint64_t& fpcr) noexcept {
    FPVariant defaultVariant;
    return sqrtS_variant(x, fpcr, defaultVariant);
}

AXP_HOT AXP_ALWAYS_INLINE double sqrtG(double x, uint64_t& fpcr) noexcept {
    FPVariant defaultVariant;
    return sqrtG_variant(x, fpcr, defaultVariant);
}

AXP_HOT AXP_ALWAYS_INLINE double sqrtF(double x, uint64_t& fpcr) noexcept {
    FPVariant defaultVariant;
    return sqrtF_variant(x, fpcr, defaultVariant);
}

#endif // ALPHA_FP_IEEE_INL_H