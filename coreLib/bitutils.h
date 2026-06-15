// ============================================================================
// coreLib/bitutils.h -- bit manipulation utilities
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

// EmulatR V3 carryover, V4-tightened -- source: coreLib/BitUtils.h (V3)

//
// High-performance bit manipulation utilities using compiler intrinsics
// where available, with portable fallbacks otherwise:
//
//   GCC / Clang : __builtin_clz / __builtin_ctz / __builtin_popcount
//   MSVC        : _BitScanReverse / _BitScanForward / __popcnt
//   Other       : portable loop fallback
//
// Functions:
//
//   popcount(v)        -- number of 1 bits in v
//   highestSetBit(v)   -- bit index (0-63) of MSB; returns 0 when v == 0
//   lowestSetBit(v)    -- bit index (0-63) of LSB; returns 0 when v == 0
//   isPowerOfTwo(v)    -- true when exactly one bit is set
//   nextPowerOfTwo(v)  -- smallest power-of-two >= v (zero -> 1)
//   extractBits(v,s,n) -- right-aligned n-bit field starting at bit s
//
// Zero handling differs from C++20 <bit>:
//   BitUtils::highestSetBit(0) == 0      (this header)
//   std::countl_zero(uint64_t{0}) == 64  (standard library)
//
// Alpha CTLZ / CTTZ specify zero -> 64.  Leaves that use the
// BitUtils helpers must guard against zero explicitly:
//   value ? (63 - BitUtils::highestSetBit(value)) : 64ULL  // CTLZ
//   value ? BitUtils::lowestSetBit(value)         : 64ULL  // CTTZ
// or use std::countl_zero / std::countr_zero from <bit> directly,
// which match Alpha semantics one-for-one.
//
// ============================================================================

#ifndef CORELIB_BITUTILS_H
#define CORELIB_BITUTILS_H

#include <cstdint>
#include <type_traits>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "coreLib/axp_attributes_core.h"


namespace BitUtils {

// ----------------------------------------------------------------------------
// highestSetBit -- bit index (0-based) of the most-significant set bit.
//
// Returns 0 for zero input.  Performance: 1 cycle on x86-64 (BSR).
// ----------------------------------------------------------------------------
template<typename T>
AXP_HOT AXP_ALWAYS_INLINE
auto highestSetBit(T value) noexcept -> uint8_t
{
    static_assert(std::is_unsigned<T>::value,
                  "highestSetBit requires unsigned integer type");

    if (value == 0) return 0;

    if constexpr (sizeof(T) <= sizeof(uint32_t)) {
        const uint32_t mask = static_cast<uint32_t>(value);

#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(31 - __builtin_clz(mask));
#elif defined(_MSC_VER)
        unsigned long index;
        _BitScanReverse(&index, mask);
        return static_cast<uint8_t>(index);
#else
        uint8_t bit = 31;
        while (bit > 0 && !(mask & (1u << bit))) {
            --bit;
        }
        return bit;
#endif
    } else {
        const uint64_t mask = static_cast<uint64_t>(value);

#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(63 - __builtin_clzll(mask));
#elif defined(_MSC_VER) && defined(_WIN64)
        unsigned long index;
        _BitScanReverse64(&index, mask);
        return static_cast<uint8_t>(index);
#else
        uint8_t bit = 63;
        while (bit > 0 && !(mask & (1ULL << bit))) {
            --bit;
        }
        return bit;
#endif
    }
}


// ----------------------------------------------------------------------------
// lowestSetBit -- bit index (0-based) of the least-significant set bit.
//
// Returns 0 for zero input.  Performance: 1 cycle on x86-64 (BSF).
// ----------------------------------------------------------------------------
template<typename T>
AXP_HOT AXP_ALWAYS_INLINE
auto lowestSetBit(T value) noexcept -> uint8_t
{
    static_assert(std::is_unsigned<T>::value,
                  "lowestSetBit requires unsigned integer type");

    if (value == 0) return 0;

    if constexpr (sizeof(T) <= sizeof(uint32_t)) {
        const uint32_t mask = static_cast<uint32_t>(value);

#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(__builtin_ctz(mask));
#elif defined(_MSC_VER)
        unsigned long index;
        _BitScanForward(&index, mask);
        return static_cast<uint8_t>(index);
#else
        uint8_t bit = 0;
        while (!(mask & (1u << bit))) {
            ++bit;
        }
        return bit;
#endif
    } else {
        const uint64_t mask = static_cast<uint64_t>(value);

#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(__builtin_ctzll(mask));
#elif defined(_MSC_VER) && defined(_WIN64)
        unsigned long index;
        _BitScanForward64(&index, mask);
        return static_cast<uint8_t>(index);
#else
        uint8_t bit = 0;
        while (!(mask & (1ULL << bit))) {
            ++bit;
        }
        return bit;
#endif
    }
}


// ----------------------------------------------------------------------------
// popcount -- number of 1 bits in value.
//
// Performance: 1 cycle on x86-64 with POPCNT extension.
// ----------------------------------------------------------------------------
template<typename T>
AXP_HOT AXP_ALWAYS_INLINE
auto popcount(T value) noexcept -> uint8_t
{
    static_assert(std::is_unsigned<T>::value,
                  "popcount requires unsigned integer type");

    if constexpr (sizeof(T) <= sizeof(uint32_t)) {
        const uint32_t mask = static_cast<uint32_t>(value);

#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(__builtin_popcount(mask));
#elif defined(_MSC_VER)
        return static_cast<uint8_t>(__popcnt(mask));
#else
        uint8_t count = 0;
        uint32_t m = mask;
        while (m) {
            count += static_cast<uint8_t>(m & 1u);
            m >>= 1;
        }
        return count;
#endif
    } else {
        const uint64_t mask = static_cast<uint64_t>(value);

#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(__builtin_popcountll(mask));
#elif defined(_MSC_VER) && defined(_WIN64)
        return static_cast<uint8_t>(__popcnt64(mask));
#else
        uint8_t count = 0;
        uint64_t m = mask;
        while (m) {
            count += static_cast<uint8_t>(m & 1ULL);
            m >>= 1;
        }
        return count;
#endif
    }
}


// ----------------------------------------------------------------------------
// isPowerOfTwo -- true when value has exactly one bit set.
// ----------------------------------------------------------------------------
template<typename T>
AXP_ALWAYS_INLINE
auto isPowerOfTwo(T value) noexcept -> bool
{
    static_assert(std::is_unsigned<T>::value,
                  "isPowerOfTwo requires unsigned integer type");

    return (value != 0) && ((value & (value - 1)) == 0);
}


// ----------------------------------------------------------------------------
// nextPowerOfTwo -- smallest power of two >= value.  Zero rounds up to 1.
// ----------------------------------------------------------------------------
template<typename T>
AXP_ALWAYS_INLINE
auto nextPowerOfTwo(T value) noexcept -> T
{
    static_assert(std::is_unsigned<T>::value,
                  "nextPowerOfTwo requires unsigned integer type");

    if (value == 0)        return T{1};
    if (isPowerOfTwo(value)) return value;

    return T{1} << (highestSetBit(value) + 1);
}


// ----------------------------------------------------------------------------
// extractBits -- extract a right-aligned n-bit field starting at bit s.
//
// Example: extractBits(0xABCD, 4, 8) == 0xBC
// ----------------------------------------------------------------------------
template<typename T>
AXP_ALWAYS_INLINE
auto extractBits(T value, uint8_t startBit, uint8_t bitCount) noexcept -> T
{
    static_assert(std::is_unsigned<T>::value,
                  "extractBits requires unsigned integer type");

    const T mask = (T{1} << bitCount) - T{1};
    return (value >> startBit) & mask;
}

} // namespace BitUtils

#endif // CORELIB_BITUTILS_H
