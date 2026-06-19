// ============================================================================
// alpha_SSE_int_inl.h - ============================================================================
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

#pragma once
#include "alpha_SSE_core.h"
#include <emmintrin.h>  // SSE2
#include <pmmintrin.h>  // SSE3
#include <QtGlobal>
#include <cfenv>
#include <cmath>
#include "alpha_fpcr_core.h"

// ============================================================================
// 64-bit Integer Operations WITHOUT FPCR side effects
// For Alpha integer instructions (ADDL, ADDL/V, SUBL, SUBL/V, etc.)
// ============================================================================

namespace AlphaSSE {

	// Signed add with overflow detection (NO FPCR USE)
	inline qint64 addS64_int(qint64 a, qint64 b, bool& overflow) noexcept
	{
		overflow = false;
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

		// Detect signed overflow
		if ((a > 0 && b > 0 && result < 0) ||
			(a < 0 && b < 0 && result > 0)) {
			overflow = true;
		}

		return result;
	}

	// Signed subtract with overflow detection (NO FPCR USE)
	inline qint64 subS64_int(qint64 a, qint64 b, bool& overflow) noexcept
	{
		overflow = false;
		qint64 result;

		if (Config::useSSE2 && !Config::forceFallback) {
			__m128i va = _mm_cvtsi64_si128(a);
			__m128i vb = _mm_cvtsi64_si128(b);
			__m128i vresult = _mm_sub_epi64(va, vb);
			result = _mm_cvtsi128_si64(vresult);
		}
		else {
			result = a - b;
		}

		// Signed overflow detection pattern:
		// a - b = a + (-b)
		qint64 negb = -b;
		if ((a > 0 && negb > 0 && result < 0) ||
			(a < 0 && negb < 0 && result > 0)) {
			overflow = true;
		}

		return result;
	}

	// Signed multiply with overflow detection (NO FPCR USE)
	inline qint64 mulS64_int(qint64 a, qint64 b, bool& overflow) noexcept
	{
		overflow = false;
		__int128 wide = static_cast<__int128>(a) * static_cast<__int128>(b);
		qint64 result = static_cast<qint64>(wide);

		// Overflow if truncation occurs
		if (wide != static_cast<__int128>(result)) {
			overflow = true;
		}

		return result;
	}
}

