// ============================================================================
// coreLib/alpha_int_byteops.h -- byte/word/long/quad pack-extract-mask helpers
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

// EmulatR V3 carryover, V4-tightened -- source: coreLib/alpha_int_byteops_inl.h (V3)

//
// Inline helpers for the Alpha byte-manipulation instruction family
// (EXT*, INS*, MSK*, ZAP, ZAPNOT) plus a few pack/unpack utilities used
// by the Mbox (unaligned store merge) path.
//
// The offset operand always indexes a byte position 0..7; the helpers
// mask the offset to its low 3 bits before use, matching Alpha SRM.
// All operations work entirely in the 64-bit value domain -- no SSE,
// no platform-specific intrinsics in v1.  If SIMD turns out to be a
// real win on hot paths we can re-introduce SSSE3 paths behind a
// build-time flag.
//
// ============================================================================

#ifndef CORELIB_ALPHA_INT_BYTEOPS_H
#define CORELIB_ALPHA_INT_BYTEOPS_H

#include <cstdint>

#include "coreLib/axp_attributes_core.h"


namespace alpha_byteops {

// ----------------------------------------------------------------------------
// Sign extension helpers
// ----------------------------------------------------------------------------

// Sign-extend a 16-bit value to 64 bits.
AXP_HOT AXP_ALWAYS_INLINE
auto sext16To64(uint16_t v) noexcept -> int64_t
{
    return static_cast<int64_t>(static_cast<int16_t>(v));
}

// Constexpr sign-extension to 64 bits from a Bits-wide low field.
template<int Bits>
AXP_ALWAYS_INLINE
constexpr auto signExtend(uint64_t value) noexcept -> int64_t
{
    static_assert(Bits > 0 && Bits < 64, "Invalid sign extension width");
    const int shift = 64 - Bits;
    return static_cast<int64_t>(static_cast<int64_t>(value << shift) >> shift);
}


// ----------------------------------------------------------------------------
// Address utilities
// ----------------------------------------------------------------------------

// Round v down to a multiple of align (align must be a power of two).
AXP_ALWAYS_INLINE
auto alignDown(uint64_t v, uint64_t align) noexcept -> uint64_t
{
    return v & ~(align - 1ULL);
}


// ----------------------------------------------------------------------------
// ZAP / ZAPNOT -- byte-mask-driven zeroing
// ----------------------------------------------------------------------------
//
// ZAP zeros byte i of value when bit i of mask is SET.
// ZAPNOT keeps  byte i of value when bit i of mask is SET.
//
// Each acts on bits[7:0] of mask; bits[63:8] are ignored.
//
// Example: zap(0x0123456789ABCDEF, 0x3C) == 0x01234500000000EF
// Example: zapnot(0x0123456789ABCDEF, 0x3C) == 0x000089ABCDEF0000

AXP_HOT AXP_ALWAYS_INLINE
auto zap(uint64_t value, uint64_t mask) noexcept -> uint64_t
{
    uint64_t result = value;
    for (int i = 0; i < 8; ++i) {
        if (mask & (1ULL << i)) {
            result &= ~(0xFFULL << (i * 8));
        }
    }
    return result;
}

AXP_HOT AXP_ALWAYS_INLINE
auto zapnot(uint64_t value, uint64_t mask) noexcept -> uint64_t
{
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        if (mask & (1ULL << i)) {
            result |= value & (0xFFULL << (i * 8));
        }
    }
    return result;
}


// ----------------------------------------------------------------------------
// MSKxL / MSKxH -- mask out a byte/word/long/quad starting at offset
// ----------------------------------------------------------------------------
//
// Low variants (MSKBL, MSKWL, MSKLL, MSKQL) zero the bytes that would
// fall within the low half of an unaligned access at offset (offset & 7).
//
// High variants (MSKWH, MSKLH, MSKQH) zero the bytes that would spill
// into the high half of the same unaligned access.

AXP_ALWAYS_INLINE
auto mskbl(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return value & ~(0xFFULL << (bytePos * 8));
}

AXP_ALWAYS_INLINE
auto mskwl(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return value & ~(0xFFFFULL << (bytePos * 8));
}

AXP_ALWAYS_INLINE
auto mskll(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return value & ~(0xFFFFFFFFULL << (bytePos * 8));
}

AXP_ALWAYS_INLINE
auto mskql(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const uint64_t mask = (1ULL << (bytePos * 8)) - 1ULL;
    return value & mask;
}

AXP_ALWAYS_INLINE
auto mskwh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int spillBytes = static_cast<int>(offset & 0x7) + 2 - 8;
    if (spillBytes <= 0) return value;
    return value & ~((1ULL << (spillBytes * 8)) - 1ULL);
}

AXP_ALWAYS_INLINE
auto msklh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int spillBytes = static_cast<int>(offset & 0x7) + 4 - 8;
    if (spillBytes <= 0) return value;
    return value & ~((1ULL << (spillBytes * 8)) - 1ULL);
}

AXP_ALWAYS_INLINE
auto mskqh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int spillBytes = static_cast<int>(offset & 0x7);
    if (spillBytes == 0) return value;
    return value & ~((1ULL << (spillBytes * 8)) - 1ULL);
}


// ----------------------------------------------------------------------------
// EXTxL / EXTxH -- extract a byte/word/long/quad starting at offset
// ----------------------------------------------------------------------------

AXP_ALWAYS_INLINE
auto extbl(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return (value >> (bytePos * 8)) & 0xFFULL;
}

AXP_ALWAYS_INLINE
auto extwl(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return (value >> (bytePos * 8)) & 0xFFFFULL;
}

AXP_ALWAYS_INLINE
auto extll(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return (value >> (bytePos * 8)) & 0xFFFFFFFFULL;
}

AXP_ALWAYS_INLINE
auto extql(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return value >> (bytePos * 8);
}

AXP_ALWAYS_INLINE
auto extwh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const int shift = (8 - bytePos) * 8;
    if (shift >= 64) return 0;
    return (value << shift) & 0xFFFFULL;
}

AXP_ALWAYS_INLINE
auto extlh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const int shift = (8 - bytePos) * 8;
    if (shift >= 64) return 0;
    return (value << shift) & 0xFFFFFFFFULL;
}

AXP_ALWAYS_INLINE
auto extqh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const int shift = (8 - bytePos) * 8;
    if (shift >= 64) return 0;
    return value << shift;
}


// ----------------------------------------------------------------------------
// INSxL / INSxH -- insert a byte/word/long/quad of value at offset position
// ----------------------------------------------------------------------------

AXP_ALWAYS_INLINE
auto insbl(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return (value & 0xFFULL) << (bytePos * 8);
}

AXP_ALWAYS_INLINE
auto inswl(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return (value & 0xFFFFULL) << (bytePos * 8);
}

AXP_ALWAYS_INLINE
auto insll(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return (value & 0xFFFFFFFFULL) << (bytePos * 8);
}

AXP_ALWAYS_INLINE
auto insql(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    return value << (bytePos * 8);
}

AXP_ALWAYS_INLINE
auto inswh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const int shift = (8 - bytePos) * 8;
    if (shift >= 64) return 0;
    return (value & 0xFFFFULL) >> shift;
}

AXP_ALWAYS_INLINE
auto inslh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const int shift = (8 - bytePos) * 8;
    if (shift >= 64) return 0;
    return (value & 0xFFFFFFFFULL) >> shift;
}

AXP_ALWAYS_INLINE
auto insqh(uint64_t value, uint64_t offset) noexcept -> uint64_t
{
    const int bytePos = static_cast<int>(offset & 0x7);
    if (bytePos == 0) return 0;
    const int shift = (8 - bytePos) * 8;
    if (shift >= 64) return 0;
    return value >> shift;
}


// ----------------------------------------------------------------------------
// Pack / unpack helpers (little-endian) used by the Mbox unaligned-store path
// ----------------------------------------------------------------------------

AXP_HOT AXP_ALWAYS_INLINE
auto unpackLE64(uint64_t value, uint8_t out[8]) noexcept -> void
{
    out[0] = static_cast<uint8_t>(value >>  0);
    out[1] = static_cast<uint8_t>(value >>  8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
    out[4] = static_cast<uint8_t>(value >> 32);
    out[5] = static_cast<uint8_t>(value >> 40);
    out[6] = static_cast<uint8_t>(value >> 48);
    out[7] = static_cast<uint8_t>(value >> 56);
}

AXP_HOT AXP_ALWAYS_INLINE
auto packLE64(uint8_t const in[8]) noexcept -> uint64_t
{
    return (static_cast<uint64_t>(in[0]) <<  0)
         | (static_cast<uint64_t>(in[1]) <<  8)
         | (static_cast<uint64_t>(in[2]) << 16)
         | (static_cast<uint64_t>(in[3]) << 24)
         | (static_cast<uint64_t>(in[4]) << 32)
         | (static_cast<uint64_t>(in[5]) << 40)
         | (static_cast<uint64_t>(in[6]) << 48)
         | (static_cast<uint64_t>(in[7]) << 56);
}


// ----------------------------------------------------------------------------
// STQ_U merge -- two-quadword RMW for unaligned store at byte offset 0..7
// ----------------------------------------------------------------------------
//
// q0 is the aligned quadword at  alignDown(PA, 8).
// q1 is the aligned quadword at  alignDown(PA, 8) + 8.
// src is the 8-byte payload being stored.
//
// touchesQ1 is set true when the store crosses the alignment boundary
// (byteOffset != 0) and the caller therefore needs to write back q1
// in addition to q0.  The Mbox uses this to decide whether a second
// translation/write is needed; if the access crosses a page boundary
// the caller must re-translate q1 separately.

AXP_HOT AXP_ALWAYS_INLINE
auto stqUMergeLane(uint64_t  q0In,
                   uint64_t  q1In,
                   uint64_t  src,
                   uint8_t   byteOffset,
                   uint64_t& q0Out,
                   uint64_t& q1Out,
                   bool&     touchesQ1) noexcept -> void
{
    const uint8_t k = byteOffset & 0x7U;

    if (k == 0) {
        q0Out      = src;
        q1Out      = q1In;
        touchesQ1  = false;
        return;
    }

    const unsigned sh        = static_cast<unsigned>(k) * 8U;
    const uint64_t lowMask   = (1ULL << sh) - 1ULL;

    q0Out = (q0In & lowMask) | (src << sh);

    const unsigned shiftRight = (8U - static_cast<unsigned>(k)) * 8U;
    const uint64_t insertLow  = (src >> shiftRight) & lowMask;

    q1Out      = (q1In & ~lowMask) | insertLow;
    touchesQ1  = true;
}

} // namespace alpha_byteops

#endif // CORELIB_ALPHA_INT_BYTEOPS_H
