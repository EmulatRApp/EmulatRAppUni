// ============================================================================
// alpha_SSE_fp_inl.h - ============================================================================
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

#ifndef _EMULATRAPPUNI_CORELIB_ALPHA_SSE_FP_INL_H
#define _EMULATRAPPUNI_CORELIB_ALPHA_SSE_FP_INL_H

#include <emmintrin.h>  // SSE2
#include <pmmintrin.h>  // SSE3
#include <QtGlobal>
#include <cfenv>
#include <cmath>
#include "alpha_fpcr_core.h"

// ============================================================================
// SSE Helper Library with FPCR Integration
// ============================================================================
namespace AlphaSSE {

	// ============================================================================
	// 64-bit Integer Operations with Overflow Detection
	// ============================================================================

	inline quint64 add64(quint64 a, quint64 b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		quint64 result;

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128i va = _mm_cvtsi64_si128(a);
			__m128i vb = _mm_cvtsi64_si128(b);
			__m128i vresult = _mm_add_epi64(va, vb);
			result = _mm_cvtsi128_si64(vresult);

			// Check for unsigned overflow
			if (result < a) {
				status.intOverflow = true;
			}
		}
		else {
			result = a + b;
			if (result < a) {
				status.intOverflow = true;
			}
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	inline qint64 addS64(qint64 a, qint64 b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		qint64 result;

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128i va = _mm_cvtsi64_si128(a);
			__m128i vb = _mm_cvtsi64_si128(b);
			__m128i vresult = _mm_add_epi64(va, vb);
			result = _mm_cvtsi128_si64(vresult);
		}
		else {
			result = a + b;
		}

		// Check for signed overflow
		if ((a > 0 && b > 0 && result < 0) ||
			(a < 0 && b < 0 && result > 0)) {
			status.intOverflow = true;
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	inline quint64 sub64(quint64 a, quint64 b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		quint64 result;

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128i va = _mm_cvtsi64_si128(a);
			__m128i vb = _mm_cvtsi64_si128(b);
			__m128i vresult = _mm_sub_epi64(va, vb);
			result = _mm_cvtsi128_si64(vresult);

			// Check for unsigned underflow
			if (a < b) {
				status.intOverflow = true;
			}
		}
		else {
			result = a - b;
			if (a < b) {
				status.intOverflow = true;
			}
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	inline quint64 mul64(quint64 a, quint64 b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		quint64 result = a * b;

		// Check for overflow (if b != 0 and result/b != a, overflow occurred)
		if (b != 0 && result / b != a) {
			status.intOverflow = true;
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	// ============================================================================
	// 64-bit Floating Point Operations with Exception Handling
	// ============================================================================

	inline double addF64(double a, double b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		double result;

		std::feclearexcept(FE_ALL_EXCEPT);

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128d va = _mm_load_sd(&a);
			__m128d vb = _mm_load_sd(&b);
			__m128d vresult = _mm_add_sd(va, vb);
			_mm_store_sd(&result, vresult);
		}
		else {
			result = a + b;
		}

		checkFloatingPointExceptions(status);
		status.applyToFPCR(fpcr);
		return result;
	}

	inline double subF64(double a, double b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		double result;

		std::feclearexcept(FE_ALL_EXCEPT);

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128d va = _mm_load_sd(&a);
			__m128d vb = _mm_load_sd(&b);
			__m128d vresult = _mm_sub_sd(va, vb);
			_mm_store_sd(&result, vresult);
		}
		else {
			result = a - b;
		}

		checkFloatingPointExceptions(status);
		status.applyToFPCR(fpcr);
		return result;
	}

	inline double mulF64(double a, double b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		double result;

		std::feclearexcept(FE_ALL_EXCEPT);

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128d va = _mm_load_sd(&a);
			__m128d vb = _mm_load_sd(&b);
			__m128d vresult = _mm_mul_sd(va, vb);
			_mm_store_sd(&result, vresult);
		}
		else {
			result = a * b;
		}

		checkFloatingPointExceptions(status);
		status.applyToFPCR(fpcr);
		return result;
	}

	inline double divF64(double a, double b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		double result;

		// Explicit divide-by-zero check
		if (b == 0.0) {
			status.divByZero = true;
			result = std::copysign(INFINITY, a);
		}
		else {
			std::feclearexcept(FE_ALL_EXCEPT);

			if (Config::useSSE2 && !Config::forceFallback) {
				__m128d va = _mm_load_sd(&a);
				__m128d vb = _mm_load_sd(&b);
				__m128d vresult = _mm_div_sd(va, vb);
				_mm_store_sd(&result, vresult);
			}
			else {
				result = a / b;
			}

			checkFloatingPointExceptions(status);
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	inline double sqrtF64(double a, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		double result;

		if (a < 0.0) {
			status.invalid = true;
			result = std::numeric_limits<double>::quiet_NaN();
		}
		else {
			std::feclearexcept(FE_ALL_EXCEPT);

			if (Config::useSSE2 && !Config::forceFallback) {
				__m128d va = _mm_load_sd(&a);
				__m128d vresult = _mm_sqrt_sd(va, va);
				_mm_store_sd(&result, vresult);
			}
			else {
				result = std::sqrt(a);
			}

			checkFloatingPointExceptions(status);
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	// ============================================================================
	// Comparison Operations (set condition codes in FPCR)
	// ============================================================================

	inline bool cmpEqF64(double a, double b, quint64& fpcr) noexcept
	{
		if (std::isnan(a) || std::isnan(b)) {
			ArithmeticStatus status;
			status.invalid = true;
			status.applyToFPCR(fpcr);
			return false;
		}

		return a == b;
	}

	inline bool cmpLtF64(double a, double b, quint64& fpcr) noexcept
	{
		if (std::isnan(a) || std::isnan(b)) {
			ArithmeticStatus status;
			status.invalid = true;
			status.applyToFPCR(fpcr);
			return false;
		}

		return a < b;
	}

	inline bool cmpLeF64(double a, double b, quint64& fpcr) noexcept
	{
		if (std::isnan(a) || std::isnan(b)) {
			ArithmeticStatus status;
			status.invalid = true;
			status.applyToFPCR(fpcr);
			return false;
		}

		return a <= b;
	}

	// ============================================================================
	// Vectorized Operations with FPCR (batch processing)
	// ============================================================================

	struct Int64Pair { quint64 low, high; };

	inline Int64Pair add64x2(const Int64Pair& a, const Int64Pair& b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		Int64Pair result;

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128i va = _mm_set_epi64x(a.high, a.low);
			__m128i vb = _mm_set_epi64x(b.high, b.low);
			__m128i vresult = _mm_add_epi64(va, vb);

			result.low = _mm_cvtsi128_si64(vresult);
			result.high = _mm_cvtsi128_si64(_mm_srli_si128(vresult, 8));

			// Check both for overflow
			if (result.low < a.low)  status.intOverflow = true;
			if (result.high < a.high) status.intOverflow = true;
		}
		else {
			result.low = a.low + b.low;
			result.high = a.high + b.high;

			if (result.low < a.low)   status.intOverflow = true;
			if (result.high < a.high) status.intOverflow = true;
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	inline Int64Pair sub64x2(const Int64Pair& a, const Int64Pair& b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		Int64Pair result;

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128i va = _mm_set_epi64x(a.high, a.low);
			__m128i vb = _mm_set_epi64x(b.high, b.low);
			__m128i vresult = _mm_sub_epi64(va, vb);

			result.low = _mm_cvtsi128_si64(vresult);
			result.high = _mm_cvtsi128_si64(_mm_srli_si128(vresult, 8));

			// Check both for underflow
			if (a.low < b.low)   status.intOverflow = true;
			if (a.high < b.high) status.intOverflow = true;
		}
		else {
			result.low = a.low - b.low;
			result.high = a.high - b.high;

			if (a.low < b.low)   status.intOverflow = true;
			if (a.high < b.high) status.intOverflow = true;
		}

		status.applyToFPCR(fpcr);
		return result;
	}

	struct DoublePair { double low, high; };

	inline DoublePair addF64x2(const DoublePair& a, const DoublePair& b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		DoublePair result;

		std::feclearexcept(FE_ALL_EXCEPT);

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128d va = _mm_set_pd(a.high, a.low);
			__m128d vb = _mm_set_pd(b.high, b.low);
			__m128d vresult = _mm_add_pd(va, vb);

			_mm_store_sd(&result.low, vresult);
			_mm_storeh_pd(&result.high, vresult);
		}
		else {
			result.low = a.low + b.low;
			result.high = a.high + b.high;
		}

		checkFloatingPointExceptions(status);
		status.applyToFPCR(fpcr);
		return result;
	}

	inline DoublePair subF64x2(const DoublePair& a, const DoublePair& b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		DoublePair result;

		std::feclearexcept(FE_ALL_EXCEPT);

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128d va = _mm_set_pd(a.high, a.low);
			__m128d vb = _mm_set_pd(b.high, b.low);
			__m128d vresult = _mm_sub_pd(va, vb);

			_mm_store_sd(&result.low, vresult);
			_mm_storeh_pd(&result.high, vresult);
		}
		else {
			result.low = a.low - b.low;
			result.high = a.high - b.high;
		}

		checkFloatingPointExceptions(status);
		status.applyToFPCR(fpcr);
		return result;
	}

	inline DoublePair mulF64x2(const DoublePair& a, const DoublePair& b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		DoublePair result;

		std::feclearexcept(FE_ALL_EXCEPT);

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128d va = _mm_set_pd(a.high, a.low);
			__m128d vb = _mm_set_pd(b.high, b.low);
			__m128d vresult = _mm_mul_pd(va, vb);

			_mm_store_sd(&result.low, vresult);
			_mm_storeh_pd(&result.high, vresult);
		}
		else {
			result.low = a.low * b.low;
			result.high = a.high * b.high;
		}

		checkFloatingPointExceptions(status);
		status.applyToFPCR(fpcr);
		return result;
	}

	inline DoublePair divF64x2(const DoublePair& a, const DoublePair& b, quint64& fpcr) noexcept
	{
		ArithmeticStatus status;
		DoublePair result;

		// Check for divide-by-zero
		if (b.low == 0.0 || b.high == 0.0) {
			status.divByZero = true;
			result.low = (b.low == 0.0) ? std::copysign(INFINITY, a.low) : a.low / b.low;
			result.high = (b.high == 0.0) ? std::copysign(INFINITY, a.high) : a.high / b.high;
		}
		else {
			std::feclearexcept(FE_ALL_EXCEPT);

			if (Config::useSSE2 && !Config::forceFallback) {
				__m128d va = _mm_set_pd(a.high, a.low);
				__m128d vb = _mm_set_pd(b.high, b.low);
				__m128d vresult = _mm_div_pd(va, vb);

				_mm_store_sd(&result.low, vresult);
				_mm_storeh_pd(&result.high, vresult);
			}
			else {
				result.low = a.low / b.low;
				result.high = a.high / b.high;
			}

			checkFloatingPointExceptions(status);
		}

		status.applyToFPCR(fpcr);
		return result;
	}

} // namespace AlphaSSE
#endif

#endif
