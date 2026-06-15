// ============================================================================
// coreLib/alpha_int_helpers.h -- integer ALU primitives with overflow tracking
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

// EmulatR V3 carryover, V4-tightened -- source: coreLib/alpha_int_helpers_inl.h (V3)

//
// Inline integer ALU primitives used by the eBox leaves.  Each
// arithmetic helper that can overflow takes an IntStatus& output
// parameter so the caller (typically a leaf for an _V trapping
// variant) can detect and propagate the trap.  Non-trapping leaves
// pass an IntStatus and ignore it -- the helper is unconditional
// math and the cost of the unset overflow flag is one branch the
// compiler should fold away.
//
// Naming and shape:
//   addL/subL/mulL  -- 32-bit signed; result wraps modulo 2^32.
//   addQ/subQ/mulQ  -- 64-bit signed; result wraps modulo 2^64.
//   addQU/subQU/mulQU -- 64-bit unsigned; carry/borrow on overflow.
//   andQ/orQ/xorQ/notQ/bicQ/ornotQ/eqvQ -- bitwise (no overflow).
//   cmpEQ/LT/LE/ULT/ULE -- 1 or 0.
//   cmovEQ/NE/LT/LE/GT/GE/LBS/LBC -- functional select.
//   sllQ/srlQ/sraQ -- 64-bit shifts with shift-count >= 64 saturated.
//   umulh -- upper 64 bits of unsigned 128-bit product.
//   isTrappingVariant -- function-field bit 6 marks _V trap path.
//
// Leaves typically use these helpers like:
//
//   IntStatus st;
//   const int64_t result = alpha_int::addQ(static_cast<int64_t>(c.opA),
//                                          static_cast<int64_t>(c.opB), st);
//   BoxResult r;
//   r.regWriteValue = static_cast<uint64_t>(result);
//   if (st.overflow && wantsTrap) r.faultCode = kFaultIntOverflow;
//   ...
//
// ============================================================================

#ifndef CORELIB_ALPHA_INT_HELPERS_H
#define CORELIB_ALPHA_INT_HELPERS_H

#include <cstdint>
#include <limits>

#if defined(_MSC_VER)
#include <intrin.h>          // _umul128
#endif

#include "coreLib/axp_attributes_core.h"


namespace alpha_int {

// ----------------------------------------------------------------------------
// IntStatus -- output flags for arithmetic operations
// ----------------------------------------------------------------------------
//
// Only the overflow flag is set by helpers in this file.  The other
// bits are reserved for future helpers (divide, alignment-checking
// memory ops) and stay clear here.
struct IntStatus
{
    bool overflow              = false;
    bool divideByZero          = false;
    bool unalignedAccess       = false;
    bool reservedOperand       = false;
    bool floatingPointException = false;

    constexpr auto hasOverflow()              const noexcept -> bool { return overflow; }
    constexpr auto hasDivideByZero()          const noexcept -> bool { return divideByZero; }
    constexpr auto hasUnalignedAccess()       const noexcept -> bool { return unalignedAccess; }
    constexpr auto hasReservedOperand()       const noexcept -> bool { return reservedOperand; }
    constexpr auto hasFloatingPointException() const noexcept -> bool { return floatingPointException; }

    constexpr auto hasError() const noexcept -> bool
    {
        return overflow || divideByZero || unalignedAccess
            || reservedOperand || floatingPointException;
    }

    auto clear() noexcept -> void
    {
        overflow              = false;
        divideByZero          = false;
        unalignedAccess       = false;
        reservedOperand       = false;
        floatingPointException = false;
    }
};


// ----------------------------------------------------------------------------
// 32-bit signed arithmetic (ADDL, SUBL, MULL)
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto addL(int32_t a, int32_t b, IntStatus& status) noexcept -> int32_t
{
    const int32_t result = a + b;
    if ((a > 0 && b > 0 && result < 0) ||
        (a < 0 && b < 0 && result > 0)) {
        status.overflow = true;
    }
    return result;
}

AXP_HOT AXP_ALWAYS_INLINE
auto subL(int32_t a, int32_t b, IntStatus& status) noexcept -> int32_t
{
    const int32_t result = a - b;
    if ((a > 0 && b < 0 && result < 0) ||
        (a < 0 && b > 0 && result > 0)) {
        status.overflow = true;
    }
    return result;
}

AXP_HOT AXP_ALWAYS_INLINE
auto mulL(int32_t a, int32_t b, IntStatus& status) noexcept -> int32_t
{
    const int64_t wide   = static_cast<int64_t>(a) * static_cast<int64_t>(b);
    const int32_t result = static_cast<int32_t>(wide);
    if (wide != static_cast<int64_t>(result)) {
        status.overflow = true;
    }
    return result;
}


// ----------------------------------------------------------------------------
// 64-bit signed arithmetic (ADDQ, SUBQ, MULQ)
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto addQ(int64_t a, int64_t b, IntStatus& status) noexcept -> int64_t
{
    const int64_t result = a + b;
    if ((a > 0 && b > 0 && result < 0) ||
        (a < 0 && b < 0 && result > 0)) {
        status.overflow = true;
    }
    return result;
}

AXP_HOT AXP_ALWAYS_INLINE
auto subQ(int64_t a, int64_t b, IntStatus& status) noexcept -> int64_t
{
    const int64_t result = a - b;
    if ((a > 0 && b < 0 && result < 0) ||
        (a < 0 && b > 0 && result > 0)) {
        status.overflow = true;
    }
    return result;
}

AXP_HOT AXP_ALWAYS_INLINE
auto mulQ(int64_t a, int64_t b, IntStatus& status) noexcept -> int64_t
{
    const int64_t result = a * b;
    if (a != 0 && b != 0) {
        if ((a == -1 && b == std::numeric_limits<int64_t>::min()) ||
            (b == -1 && a == std::numeric_limits<int64_t>::min())) {
            status.overflow = true;
        } else if (result / a != b) {
            status.overflow = true;
        }
    }
    return result;
}


// ----------------------------------------------------------------------------
// 64-bit unsigned arithmetic
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto addQU(uint64_t a, uint64_t b, IntStatus& status) noexcept -> uint64_t
{
    const uint64_t result = a + b;
    if (result < a) status.overflow = true;
    return result;
}

AXP_HOT AXP_ALWAYS_INLINE
auto subQU(uint64_t a, uint64_t b, IntStatus& status) noexcept -> uint64_t
{
    if (a < b) status.overflow = true;
    return a - b;
}

AXP_HOT AXP_ALWAYS_INLINE
auto mulQU(uint64_t a, uint64_t b, IntStatus& status) noexcept -> uint64_t
{
    const uint64_t result = a * b;
    if (b != 0 && result / b != a) status.overflow = true;
    return result;
}


// ----------------------------------------------------------------------------
// Bitwise logical operations (no overflow)
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto andQ(uint64_t a, uint64_t b) noexcept -> uint64_t { return a & b; }

AXP_HOT AXP_ALWAYS_INLINE
auto orQ(uint64_t a, uint64_t b)  noexcept -> uint64_t { return a | b; }

AXP_HOT AXP_ALWAYS_INLINE
auto xorQ(uint64_t a, uint64_t b) noexcept -> uint64_t { return a ^ b; }

AXP_HOT AXP_ALWAYS_INLINE
auto notQ(uint64_t a)             noexcept -> uint64_t { return ~a; }

AXP_HOT AXP_ALWAYS_INLINE
auto bicQ(uint64_t a, uint64_t b)   noexcept -> uint64_t { return a & ~b; }

AXP_HOT AXP_ALWAYS_INLINE
auto ornotQ(uint64_t a, uint64_t b) noexcept -> uint64_t { return a | ~b; }

AXP_HOT AXP_ALWAYS_INLINE
auto eqvQ(uint64_t a, uint64_t b)   noexcept -> uint64_t { return ~(a ^ b); }


// ----------------------------------------------------------------------------
// Comparison primitives (return 0 or 1, ready for regfile commit)
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto cmpEQ(uint64_t a, uint64_t b) noexcept -> uint64_t { return (a == b) ? 1ULL : 0ULL; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmpLT(int64_t a, int64_t b)   noexcept -> uint64_t { return (a <  b) ? 1ULL : 0ULL; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmpLE(int64_t a, int64_t b)   noexcept -> uint64_t { return (a <= b) ? 1ULL : 0ULL; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmpULT(uint64_t a, uint64_t b) noexcept -> uint64_t { return (a <  b) ? 1ULL : 0ULL; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmpULE(uint64_t a, uint64_t b) noexcept -> uint64_t { return (a <= b) ? 1ULL : 0ULL; }


// ----------------------------------------------------------------------------
// Conditional move primitives -- functional form
// ----------------------------------------------------------------------------
//
// Returns src when the predicate is met, dst otherwise.  Leaves can
// use these to compute the value, then translate to the V4 leaf form
// that suppresses the regfile commit when not met (regWriteIdx =
// kNoRegWrite).

AXP_HOT AXP_ALWAYS_INLINE
auto cmovEQ(uint64_t src, uint64_t dst, uint64_t test) noexcept -> uint64_t
{ return (test == 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovNE(uint64_t src, uint64_t dst, uint64_t test) noexcept -> uint64_t
{ return (test != 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovLT(uint64_t src, uint64_t dst, int64_t test) noexcept -> uint64_t
{ return (test < 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovLE(uint64_t src, uint64_t dst, int64_t test) noexcept -> uint64_t
{ return (test <= 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovGT(uint64_t src, uint64_t dst, int64_t test) noexcept -> uint64_t
{ return (test > 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovGE(uint64_t src, uint64_t dst, int64_t test) noexcept -> uint64_t
{ return (test >= 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovLBC(uint64_t src, uint64_t dst, uint64_t test) noexcept -> uint64_t
{ return ((test & 1ULL) == 0) ? src : dst; }

AXP_HOT AXP_ALWAYS_INLINE
auto cmovLBS(uint64_t src, uint64_t dst, uint64_t test) noexcept -> uint64_t
{ return ((test & 1ULL) != 0) ? src : dst; }


// ----------------------------------------------------------------------------
// 64-bit shifts -- saturate at width, treat negative shift as zero shift
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto sllQ(uint64_t v, int s) noexcept -> uint64_t
{
    if (s >= 64) return 0;
    if (s < 0)   return v;
    return v << s;
}

AXP_HOT AXP_ALWAYS_INLINE
auto srlQ(uint64_t v, int s) noexcept -> uint64_t
{
    if (s >= 64) return 0;
    if (s < 0)   return v;
    return v >> s;
}

AXP_HOT AXP_ALWAYS_INLINE
auto sraQ(int64_t v, int s) noexcept -> int64_t
{
    if (s >= 64) return (v < 0) ? -1 : 0;
    if (s < 0)   return v;
    return v >> s;
}


// ----------------------------------------------------------------------------
// UMULH -- upper 64 bits of unsigned 128-bit product
// ----------------------------------------------------------------------------
//
// Three platform paths.  GCC/Clang use native __uint128_t.  MSVC uses
// _umul128 from <intrin.h>.  Anything else falls back to the
// 32-by-32 decomposition.  status is unused -- UMULH does not trap
// on Alpha -- but is taken to keep the signature uniform with the
// other arithmetic helpers in this file.
//
AXP_HOT AXP_ALWAYS_INLINE
auto umulh(uint64_t a, uint64_t b, [[maybe_unused]] IntStatus& status) noexcept -> uint64_t
{
#if defined(__SIZEOF_INT128__)
    const __uint128_t wide =
        static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
    return static_cast<uint64_t>(wide >> 64);
#elif defined(_MSC_VER)
    unsigned long long high = 0;
    (void)_umul128(a, b, &high);
    return high;
#else
    const uint64_t a_lo = a & 0xFFFFFFFFULL;
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = b & 0xFFFFFFFFULL;
    const uint64_t b_hi = b >> 32;

    const uint64_t p0 = a_lo * b_lo;
    const uint64_t p1 = a_lo * b_hi;
    const uint64_t p2 = a_hi * b_lo;
    const uint64_t p3 = a_hi * b_hi;

    const uint64_t mid = p1 + p2 + (p0 >> 32);
    return p3 + (mid >> 32);
#endif
}


// ----------------------------------------------------------------------------
// _V trap-variant predicate
// ----------------------------------------------------------------------------
//
// Function-field bit 6 (mask 0x40) marks the trapping variant of an
// opcode-0x10 (INTA) instruction.  ADDL_V, SUBL_V, MULL_V, ADDQ_V,
// SUBQ_V, MULQ_V all carry this bit.  Returns false for any opcode
// other than 0x10.
AXP_HOT AXP_ALWAYS_INLINE
auto isTrappingVariant(uint8_t opcode, uint16_t subDecode) noexcept -> bool
{
    if (opcode != 0x10) return false;
    constexpr uint16_t kTrapBit = 0x40;
    return (subDecode & kTrapBit) != 0;
}

} // namespace alpha_int

#endif // CORELIB_ALPHA_INT_HELPERS_H
