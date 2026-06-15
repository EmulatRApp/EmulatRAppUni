// ============================================================================
// alpha_vector_fp_inl.h - High Performance Unified Cross-Platform SIMD Math
// ============================================================================
// project: asa-emulatr - alpha axp architecture emulator
// copyright (c) 2025-2026 envy systems, inc. all rights reserved.
// ============================================================================
#ifndef _emulatrappuni_corelib_alpha_vector_fp_inl_h
#define _emulatrappuni_corelib_alpha_vector_fp_inl_h

#include <qtglobal>
#include <cmath>
#include <bit>

// Platform Detection and Alignment Configuration
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h> // AVX2 / SSE Intrin
#define ALPHASIMD_ALIGN alignas(32)
#define HOST_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>  // ARM64 NEON Intrin
#define ALPHASIMD_ALIGN alignas(16)
#define HOST_ARM64 1
#else
#define ALPHASIMD_ALIGN alignas(16)
#define HOST_FALLBACK 1
#endif

namespace alphasse {

// Parallel Vector Packages for Batched Execution Engines
struct ALPHASIMD_ALIGN Float32Vector {
    union {
        float f32[8];
#if defined(HOST_X86_64)
        __m256 v256;
#elif defined(HOST_ARM64)
        float32x4_t v128[2];
#endif
    };
};

struct ALPHASIMD_ALIGN Float64Vector {
    union {
        double f64[4];
#if defined(HOST_X86_64)
        __m256d v256d;
#elif defined(HOST_ARM64)
        float64x2_t v128d[2];
#endif
    };
};

// ============================================================================
// Batched Vector Arithmetic (Outperforms Scalar when loops are unrolled)
// ============================================================================

/**
 * @brief Vectorized T-Float (Double Precision) Addition
 * Processes 4 elements simultaneously on x86_64, or 2x2 elements on ARM64.
 */
inline Float64Vector vector_add_tfloat(const Float64Vector& a, const Float64Vector& b) noexcept {
    Float64Vector result;
#if defined(HOST_X86_64)
    result.v256d = _mm256_add_pd(a.v256d, b.v256d);
#elif defined(HOST_ARM64)
    result.v128d[0] = vaddq_f64(a.v128d[0], b.v128d[0]);
    result.v128d[1] = vaddq_f64(a.v128d[1], b.v128d[1]);
#else
#pragma omp simd
    for (int i = 0; i < 4; ++i) {
        result.f64[i] = a.f64[i] + b.f64[i];
    }
#endif
    return result;
}

/**
 * @brief Vectorized S-Float (Single Precision) Addition
 * Processes 8 elements simultaneously on x86_64, or 2x4 elements on ARM64.
 */
inline Float32Vector vector_add_sfloat(const Float32Vector& a, const Float32Vector& b) noexcept {
    Float32Vector result;
#if defined(HOST_X86_64)
    result.v256 = _mm256_add_ps(a.v256, b.v256);
#elif defined(HOST_ARM64)
    result.v128[0] = vaddq_f32(a.v128[0], b.v128[0]);
    result.v128[1] = vaddq_f32(a.v128[1], b.v128[1]);
#else
#pragma omp simd
    for (int i = 0; i < 8; ++i) {
        result.f32[i] = a.f32[i] + b.f32[i];
    }
#endif
    return result;
}

// ============================================================================
// Zero-Overhead Delayed Sticky Flag Synchronization (Deferred Batching)
// ============================================================================

/**
 * @brief Merges hardware floating-point exceptions into the Alpha FPCR.
 * Invoke this ONLY at basic block boundaries to completely eliminate flag sync overhead.
 */
inline void flush_hardware_exceptions_to_fpcr(quint64& fpcr) noexcept {
#if defined(HOST_X86_64)
    unsigned int mxcsr = _mm_getcsr();
    if (mxcsr & 0x10) fpcr |= (1ULL << 52); // Underflow Sticky (UNF)
    if (mxcsr & 0x08) fpcr |= (1ULL << 53); // Overflow Sticky (OVF)
    if (mxcsr & 0x04) fpcr |= (1ULL << 54); // Division by Zero (DZE)
    if (mxcsr & 0x01) fpcr |= (1ULL << 55); // Invalid Operation (INV)
    _mm_setcsr(mxcsr & ~0x1D);              // Fast Clear Sticky Flags
#elif defined(HOST_ARM64)
    unsigned long long fpsr;
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    if (fpsr & 0x08) fpcr |= (1ULL << 52);  // Underflow Bit
    if (fpsr & 0x04) fpcr |= (1ULL << 53);  // Overflow Bit
    if (fpsr & 0x02) fpcr |= (1ULL << 54);  // DivByZero Bit
    if (fpsr & 0x01) fpcr |= (1ULL << 55);  // InvalidOp Bit
    __asm__ volatile("msr fpsr, %0" :: "r"(fpsr & ~0x0F)); // Fast Clear
#endif
}

} // namespace alphasse

#endif // _emulatrappuni_corelib_alpha_vector_fp_inl_h

