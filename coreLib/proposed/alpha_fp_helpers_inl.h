// ============================================================================
// alpha_fp_helpers_inl.h - G-format floating-point equal comparison (VAX semantics)
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

#ifndef alpha_fp_helpers_inl_h__
#define alpha_fp_helpers_inl_h__

#include <QtGlobal>
#include <cfenv>
#include <cmath>
#include <QtCompilerDetection>

#include "alpha_fp_ieee_inl.h"
#include "../coreLib/fp_variant_core.h"
#include "../coreLib/Axp_Attributes_core.h"
#include "../coreLib/alpha_fpcr_core.h"



	struct FPStatus {
		bool overflow;
		bool divideByZero;
		bool floatingPointException;

		FPStatus() : overflow(false) {}
		bool hasOverflow() const { return overflow; }
		bool hasDivideByZero() const { return divideByZero; }
		bool hasFloatingPointException() const { return floatingPointException; }
		constexpr bool hasError() const noexcept { return overflow; }
	};

	// ============================================================================
	// FPCR EXCEPTION BIT DEFINITIONS (ADD TO CONSTANTS)
	// ============================================================================
	static constexpr quint64 FPCR_INV_EXCEPTION_BIT = 0x020000ULL;  // Invalid operation
	static constexpr quint64 FPCR_OVF_EXCEPTION_BIT = 0x040000ULL;  // Overflow
	static constexpr quint64 FPCR_UNF_EXCEPTION_BIT = 0x080000ULL;  // Underflow  
	static constexpr quint64 FPCR_INE_EXCEPTION_BIT = 0x100000ULL;  // Inexact


	/**
 * @brief G-format floating-point equal comparison (VAX semantics)
 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEQ_G_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		// Convert to VAX G-format semantics
		const double va = ieeeToVaxG(a);
		const double vb = ieeeToVaxG(b);

		// VAX G-format has no NaN - all values are ordered
		// Reserved operand fault for invalid values
		if (va == 0.0 && (std::isnan(a) || std::isinf(a))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;  // Reserved operand
			}
			return false;
		}

		if (vb == 0.0 && (std::isnan(b) || std::isinf(b))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;  // Reserved operand  
			}
			return false;
		}

		return va == vb;
	}

	/**
	 * @brief G-format floating-point less than comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLT_G_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const double va = ieeeToVaxG(a);
		const double vb = ieeeToVaxG(b);

		// Check for reserved operands
		if ((va == 0.0 && (std::isnan(a) || std::isinf(a))) ||
			(vb == 0.0 && (std::isnan(b) || std::isinf(b)))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;
		}

		return va < vb;
	}

	/**
	 * @brief G-format floating-point less than or equal comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLE_G_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const double va = ieeeToVaxG(a);
		const double vb = ieeeToVaxG(b);

		if ((va == 0.0 && (std::isnan(a) || std::isinf(a))) ||
			(vb == 0.0 && (std::isnan(b) || std::isinf(b)))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;
		}

		return va <= vb;
	}

	/**
	 * @brief G-format floating-point unordered comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpUN_G_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		// VAX G-format has no unordered values - all values are ordered
		// Only reserved operands can cause "unordered" condition
		const bool aReserved = std::isnan(a) || std::isinf(a);
		const bool bReserved = std::isnan(b) || std::isinf(b);

		if ((aReserved || bReserved) && variant.trapEnabled) {
			fpcr |= FPCR_INV_EXCEPTION_BIT;  // Reserved operand fault
		}

		// In VAX, unordered means "reserved operand encountered"
		return aReserved || bReserved;
	}


	// ============================================================================
	// FP Comparisons WITH VARIANT SUPPORT
	// ============================================================================
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEQ_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		(void)variant; // Comparisons don't use rounding mode
		if (std::isnan(a) || std::isnan(b)) {
			fpcr |= AlphaFPCR::INV;
			return false;
		}
		return a == b;
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLT_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		(void)variant;
		if (std::isnan(a) || std::isnan(b)) {
			fpcr |= AlphaFPCR::INV;
			return false;
		}
		return a < b;
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLE_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		(void)variant;
		if (std::isnan(a) || std::isnan(b)) {
			fpcr |= AlphaFPCR::INV;
			return false;
		}
		return a <= b;
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpUN_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		(void)variant;
		bool unordered = std::isnan(a) || std::isnan(b);
		if (unordered) {
			fpcr |= AlphaFPCR::INV;
		}
		return unordered;
	}

	// ============================================================================
	// 64-bit FP Operations WITH VARIANT SUPPORT
	// ============================================================================

	AXP_HOT AXP_ALWAYS_INLINE  double addF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		double result = a + b;  // Compiler: addsd xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  double subF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		double result = a - b;  // Compiler: subsd xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  double mulF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		double result = a * b;  // Compiler: mulsd xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  double divF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		double result = a / b;  // Compiler: divsd xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  double sqrtF64_variant(double a, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		double result = std::sqrt(a);  // Compiler: sqrtsd xmm0, xmm0

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// ============================================================================
	// 32-bit FP Operations WITH VARIANT SUPPORT
	// ============================================================================

	AXP_HOT AXP_ALWAYS_INLINE  float addF32_variant(float a, float b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		float result = a + b;  // Compiler: addss xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  float subF32_variant(float a, float b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		float result = a - b;  // Compiler: subss xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  float mulF32_variant(float a, float b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		float result = a * b;  // Compiler: mulss xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  float divF32_variant(float a, float b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		float result = a / b;  // Compiler: divss xmm0, xmm1

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	AXP_HOT AXP_ALWAYS_INLINE  float sqrtF32_variant(float a, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		float result = std::sqrt(a);  // Compiler: sqrtss xmm0, xmm0

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// ============================================================================
	// BACKWARD COMPATIBILITY: Legacy functions without variant
	// ============================================================================

	AXP_HOT AXP_ALWAYS_INLINE  double addF64(double a, double b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return addF64_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  double subF64(double a, double b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return subF64_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  double mulF64(double a, double b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return mulF64_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  double divF64(double a, double b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return divF64_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  double sqrtF64(double a, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return sqrtF64_variant(a, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  float addF32(float a, float b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return addF32_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  float subF32(float a, float b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return subF32_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  float mulF32(float a, float b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return mulF32_variant(a, b, fpcr, defaultVariant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  float divF32(float a, float b, quint64& fpcr) noexcept
	{
		FPVariant defaultVariant;
		return divF32_variant(a, b, fpcr, defaultVariant);
	}

	// ============================================================================
	// Floating Point Comparisons (Legacy - no variant)
	// ============================================================================
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEqF64(double a, double b, quint64& fpcr) noexcept
	{
		if (std::isnan(a) || std::isnan(b)) {
			fpcr |= AlphaFPCR::INV;
			return false;
		}
		return a == b;
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLtF64(double a, double b, quint64& fpcr) noexcept
	{
		if (std::isnan(a) || std::isnan(b)) {
			fpcr |= AlphaFPCR::INV;
			return false;
		}
		return a < b;
	}




	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLeF64(double a, double b, quint64& fpcr) noexcept
	{
		if (std::isnan(a) || std::isnan(b)) {
			fpcr |= AlphaFPCR::INV;
			return false;
		}
		return a <= b;
	}


	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEqF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		return cmpEQ_variant(a, b, fpcr, variant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLtF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		return cmpLT_variant(a, b, fpcr, variant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLeF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		return cmpLE_variant(a, b, fpcr, variant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpUnF64_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		return cmpUN_variant(a, b, fpcr, variant);
	}

	

	// ============================================================================
	// FP Conversions WITH VARIANT SUPPORT (Opcode 15: F/G format)
	// ============================================================================

	// CVTGF: G-float (double) -> F-float (float, stored as double)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtG_to_F_variant(double g, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		// G-float is internally double; convert to F-float (32-bit, stored as 64-bit)
		float f32 = static_cast<float>(g);
		double result = static_cast<double>(f32);

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// CVTGQ: G-float (double) -> Quadword integer (stored as double bits)
	AXP_HOT AXP_ALWAYS_INLINE  quint64 cvtF64_to_Q(double d, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		// Convert to 64-bit signed integer
		qint64 q = static_cast<qint64>(d);

		updateFPCR_variant(fpcr, variant);
		return static_cast<quint64>(q);
	}

	// CVTQF: Quadword integer -> F-float
	AXP_HOT AXP_ALWAYS_INLINE  double cvtQ_to_F_variant(qint64 q, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		// Convert to F-float (32-bit float stored as 64-bit double)
		float f32 = static_cast<float>(q);
		double result = static_cast<double>(f32);

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// CVTQG: Quadword integer -> G-float (double)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtQ_to_F64(qint64 q, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		double result = static_cast<double>(q);

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// ============================================================================
	// FP Conversions WITH VARIANT SUPPORT (Opcode 16: S/T format)
	// ============================================================================

	// CVTTS: T-float (double) -> S-float (32-bit, stored as double)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtT_to_S_variant(double t, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		float s32 = static_cast<float>(t);
		double result = static_cast<double>(s32);

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// CVTST: S-float -> T-float (double)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtS_to_T_variant(double s, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		// S is stored as double but represents 32-bit precision
		// Convert to actual float then back to double for T-float
		float s32 = static_cast<float>(s);
		double result = static_cast<double>(s32);

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// CVTTQ: T-float (double) -> Quadword integer
	AXP_HOT AXP_ALWAYS_INLINE  quint64 cvtT_to_Q_variant(double t, quint64& fpcr, const FPVariant& variant) noexcept
	{
		return cvtF64_to_Q(t, fpcr, variant); // Same implementation
	}

	// CVTQT: Quadword integer -> T-float (double)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtQ_to_T_variant(qint64 q, quint64& fpcr, const FPVariant& variant) noexcept
	{
		return cvtQ_to_F64(q, fpcr, variant); // Same implementation
	}

	// CVTDG: D-float -> G-float (VAX compatibility, treat as identity for IEEE)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtD_to_G_variant(double d, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		// In IEEE mode, D and G are both double precision - identity conversion
		double result = d;

		updateFPCR_variant(fpcr, variant);
		return result;
	}

	// CVTGD: G-float -> D-float (VAX compatibility, treat as identity for IEEE)
	AXP_HOT AXP_ALWAYS_INLINE  double cvtG_to_D_variant(double g, quint64& fpcr, const FPVariant& variant) noexcept
	{
		applyVariantRoundingMode(variant);
		std::feclearexcept(FE_ALL_EXCEPT);

		// In IEEE mode, G and D are both double precision - identity conversion
		double result = g;

		updateFPCR_variant(fpcr, variant);
		return result;
	}

    // Map to existing functions with correct naming
    AXP_HOT AXP_ALWAYS_INLINE  double cvtTSF64_variant(double t, quint64& fpcr, const FPVariant& variant) noexcept
    {
        return cvtT_to_S_variant(t, fpcr, variant);  // Use existing function
    }

    AXP_HOT AXP_ALWAYS_INLINE  double cvtSTF64_variant(double s, quint64& fpcr, const FPVariant& variant) noexcept
    {
        return cvtS_to_T_variant(s, fpcr, variant);  // Use existing function
    }

    AXP_HOT AXP_ALWAYS_INLINE  quint64 cvtTQF64_variant(double t, quint64& fpcr, const FPVariant& variant) noexcept
    {
        return cvtT_to_Q_variant(t, fpcr, variant);  // Use existing function
    }

    AXP_HOT AXP_ALWAYS_INLINE  double cvtQTF64_variant(quint64 q, quint64& fpcr, const FPVariant& variant) noexcept
    {
        return cvtQ_to_T_variant(static_cast<qint64>(q), fpcr, variant);  // Use existing function
    }

    AXP_HOT AXP_ALWAYS_INLINE  double cvtQSF64_variant(quint64 q, quint64& fpcr, const FPVariant& variant) noexcept
    {
        return cvtQ_to_F_variant(static_cast<qint64>(q), fpcr, variant);  // Use existing function
    }

    AXP_HOT AXP_ALWAYS_INLINE  quint64 cvtGQF64_variant(double g, quint64& fpcr, const FPVariant& variant) noexcept
    {
        return cvtF64_to_Q(g, fpcr, variant);  // Use existing function
    }


	// G-format (VAX double precision) comparison helpers
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEqG_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept {
		// G-format has different NaN handling than T-format
		// VAX floating-point doesn't have IEEE NaN semantics
		return cmpEQ_G_variant(a, b, fpcr, variant);  // Implement VAX G semantics
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLtG_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept {
		return cmpLT_G_variant(a, b, fpcr, variant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLeG_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept {
		return cmpLE_G_variant(a, b, fpcr, variant);
	}

	AXP_HOT AXP_ALWAYS_INLINE  bool cmpUnG_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept {
		return cmpUN_G_variant(a, b, fpcr, variant);
	}
	

	/**
	 * @brief Suppress FP exceptions according to variant flags
	 *
	 * Software completion variants (/SU, /SUI, etc.) suppress certain exceptions.
	 * This clears the suppressed exception bits from FPCR.
	 *
	 * @param fpcr FPCR register value (modified)
	 * @param variant FP variant with suppression flags
	 */
	AXP_HOT AXP_ALWAYS_INLINE void suppressExceptionsAccordingToVariant(
		quint64& fpcr,
		const FPVariant& variant) noexcept
	{
		// /SU - Suppress underflow
		if (variant.suppressUnderflow) {
			fpcr &= ~AlphaFPCR::UNF;  // Clear underflow bit
		}

		// /SUI - Suppress underflow AND inexact
		if (variant.suppressInexact) {
			fpcr &= ~AlphaFPCR::INE;  // Clear inexact bit
		}

		// Note: /SU implies /SUI in Alpha architecture
		// If suppressUnderflow is set, suppressInexact should also be set
	}
	// ============================================================================
// S-FORMAT (IEEE SINGLE PRECISION) COMPARISON HELPERS
// ============================================================================

/**
 * @brief S-format floating-point equal comparison
 * @param a First operand (stored as double but treated as single precision)
 * @param b Second operand (stored as double but treated as single precision)
 * @param fpcr Floating-point control register (updated with exceptions)
 * @param variant FP variant flags
 * @return true if a == b in S-format semantics
 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEqS_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		// Convert to single precision for comparison
		const float fa = static_cast<float>(a);
		const float fb = static_cast<float>(b);

		// IEEE single precision equality
		if (std::isnan(fa) || std::isnan(fb)) {
			// Set invalid operation exception if needed
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;  // NaN != anything (including NaN)
		}

		return fa == fb;
	}

	/**
	 * @brief S-format floating-point less than comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLtS_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const float fa = static_cast<float>(a);
		const float fb = static_cast<float>(b);

		if (std::isnan(fa) || std::isnan(fb)) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;  // NaN comparisons are always false
		}

		return fa < fb;
	}

	/**
	 * @brief S-format floating-point less than or equal comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLeS_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const float fa = static_cast<float>(a);
		const float fb = static_cast<float>(b);

		if (std::isnan(fa) || std::isnan(fb)) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;
		}

		return fa <= fb;
	}

	/**
	 * @brief S-format floating-point unordered comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpUnS_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const float fa = static_cast<float>(a);
		const float fb = static_cast<float>(b);

		// Unordered means at least one operand is NaN
		const bool isUnordered = std::isnan(fa) || std::isnan(fb);

		if (isUnordered && variant.trapEnabled) {
			fpcr |= FPCR_INV_EXCEPTION_BIT;
		}

		return isUnordered;
	}

	

	/**
	 * @brief F-format floating-point equal comparison (VAX single precision)
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpEQ_F_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const float fa = ieeeToVaxF(a);
		const float fb = ieeeToVaxF(b);

		// Check for reserved operands
		if ((fa == 0.0f && (std::isnan(a) || std::isinf(a))) ||
			(fb == 0.0f && (std::isnan(b) || std::isinf(b)))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;
		}

		return fa == fb;
	}

	/**
	 * @brief F-format floating-point less than comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLT_F_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const float fa = ieeeToVaxF(a);
		const float fb = ieeeToVaxF(b);

		if ((fa == 0.0f && (std::isnan(a) || std::isinf(a))) ||
			(fb == 0.0f && (std::isnan(b) || std::isinf(b)))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;
		}

		return fa < fb;
	}

	/**
	 * @brief F-format floating-point less than or equal comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpLE_F_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const float fa = ieeeToVaxF(a);
		const float fb = ieeeToVaxF(b);

		if ((fa == 0.0f && (std::isnan(a) || std::isinf(a))) ||
			(fb == 0.0f && (std::isnan(b) || std::isinf(b)))) {
			if (variant.trapEnabled) {
				fpcr |= FPCR_INV_EXCEPTION_BIT;
			}
			return false;
		}

		return fa <= fb;
	}

	/**
	 * @brief F-format floating-point unordered comparison
	 */
	AXP_HOT AXP_ALWAYS_INLINE  bool cmpUN_F_variant(double a, double b, quint64& fpcr, const FPVariant& variant) noexcept
	{
		const bool aReserved = std::isnan(a) || std::isinf(a);
		const bool bReserved = std::isnan(b) || std::isinf(b);

		if ((aReserved || bReserved) && variant.trapEnabled) {
			fpcr |= FPCR_INV_EXCEPTION_BIT;
		}

		return aReserved || bReserved;
	}


#endif // alpha_fp_helpers_inl_h__
