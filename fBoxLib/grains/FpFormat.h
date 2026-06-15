// ============================================================================
// fBoxLib/grains/FpFormat.h -- IEEE / VAX FP <-> 64-bit register conversion
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
// Conversion helpers between the four Alpha floating-point memory formats
// and the uniform 64-bit register format Alpha 21264 uses for FP regs.
//
//   IEEE S_floating  (LDS / STS, opcodes 0x22 / 0x26)  -- 32-bit IEEE single
//   IEEE T_floating  (LDT / STT, opcodes 0x23 / 0x27)  -- 64-bit IEEE double
//   VAX  F_floating  (LDF / STF, opcodes 0x20 / 0x24)  -- 32-bit VAX F
//   VAX  G_floating  (LDG / STG, opcodes 0x21 / 0x25)  -- 64-bit VAX G
//
// Reference: Alpha Architecture Reference Manual sections 4.7.5..4.7.8.
// Implementation ported verbatim from V1 (`FBoxLib/FBoxBase.h`)
// `convertS_FloatingToRegister`, `convertS_FloatingToMemory`,
// `convertG_FloatingToRegister`, `convertG_FloatingToMemory`.
//
// V1 origin file is read-only reference per project rules; ports retain
// the original control flow and constants.
// ============================================================================

#ifndef FBOXLIB_FP_FORMAT_H
#define FBOXLIB_FP_FORMAT_H

#include <cstdint>

namespace fBox {

// ---------------------------------------------------------------------------
// IEEE S_floating (32-bit single) -- memory <-> register conversion.
// ---------------------------------------------------------------------------
//
// Register form (64-bit):  sign[63]  exp11[62:52]  frac[51:29]  zero[28:0]
// Memory form (32-bit):    sign[31]  exp8[30:23]   frac[22:0]
//
// Exponent expansion (8 -> 11):
//   exp8 == 0    -> reg_exp = 0     (true zero / denorm)
//   exp8 == 0xFF -> reg_exp = 0x7FF (INF / NaN)
//   otherwise    -> reg_exp = (exp8[7] << 10) | ((~exp8[7] ? 0x07 : 0) << 7)
//                            | (exp8[6:0])
//
// Symmetric collapse on store.  Zero-pad of frac[28:0] preserved on load.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr uint64_t convertS_FloatingToRegister(uint32_t mem) noexcept
{
    uint64_t const sign = (mem >> 31) & 1u;
    uint32_t const exp8 = (mem >> 23) & 0xFFu;
    uint32_t const frac = mem & 0x7FFFFFu;

    if (exp8 == 0) {
        return sign << 63;       // true zero (sign-preserved)
    }

    uint32_t exp11;
    if (exp8 == 0xFF) {
        exp11 = 0x7FF;           // IEEE INF / NaN -> all ones
    } else {
        uint32_t const msb  = (exp8 >> 7) & 1u;
        uint32_t const fill = msb ? 0u : 7u;
        exp11 = (msb << 10) | (fill << 7) | (exp8 & 0x7Fu);
    }

    return (sign << 63)
         | (static_cast<uint64_t>(exp11) << 52)
         | (static_cast<uint64_t>(frac) << 29);
}


[[nodiscard]] constexpr uint32_t convertS_FloatingToMemory(uint64_t reg) noexcept
{
    uint32_t const sign  = static_cast<uint32_t>((reg >> 63) & 1u);
    uint32_t const exp11 = static_cast<uint32_t>((reg >> 52) & 0x7FFu);
    uint32_t const frac  = static_cast<uint32_t>((reg >> 29) & 0x7FFFFFu);

    if (exp11 == 0) {
        return sign << 31;
    }

    uint32_t exp8;
    if (exp11 == 0x7FF) {
        exp8 = 0xFF;
    } else {
        // Collapse: take exp11[10] (-> bit 7 of exp8) and exp11[6:0].
        exp8 = ((exp11 >> 3) & 0x80u) | (exp11 & 0x7Fu);
    }

    return (sign << 31)
         | (exp8 << 23)
         | frac;
}


// ---------------------------------------------------------------------------
// IEEE T_floating (64-bit double) -- identity in both directions.
// ---------------------------------------------------------------------------
//
// The Alpha 21264 stores T_floating in registers byte-for-byte identical
// to its little-endian IEEE 754 64-bit memory representation.  Provided
// as named helpers to keep call-site symmetry with the other formats.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr uint64_t convertT_FloatingToRegister(uint64_t mem) noexcept
{
    return mem;
}

[[nodiscard]] constexpr uint64_t convertT_FloatingToMemory(uint64_t reg) noexcept
{
    return reg;
}


// ---------------------------------------------------------------------------
// VAX F_floating (32-bit) -- memory <-> register conversion.
// ---------------------------------------------------------------------------
//
// VAX F format in memory is a 32-bit value with the low and high 16-bit
// words swapped relative to host byte order.  The register form is a
// 64-bit value with sign+exponent in bits[63:52], fraction extended.
//
// VAX F memory layout (longword): [frac_lo<15:0> | sign<15> | exp<14:7> | frac_hi<6:0>]
// where the longword has been word-swapped on load (Alpha LDF behavior).
//
// Conversion derives the standard register form: sign + 11-bit exponent
// (extracted from 8-bit VAX exp via the same rule as S_floating), frac
// padded to 23 bits.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr uint64_t convertF_FloatingToRegister(uint32_t mem) noexcept
{
    // After Alpha's word swap on load, the memory longword has the form:
    //   [31:16] = original mem<15:0>     (high half of VAX F)
    //   [15:0]  = original mem<31:16>    (low half of VAX F)
    //
    // For LDF we get the longword AS STORED (after Alpha's automatic
    // word swap), so the bit layout in our 32-bit variable is:
    //   bit 31  = original mem<15>      = VAX sign
    //   bits 30:23 = original mem<14:7> = VAX exp (8 bits)
    //   bits 22:16 = original mem<6:0>  = VAX frac high (7 bits)
    //   bits 15:0  = original mem<31:16>= VAX frac low (16 bits)
    //
    // Combine into register form: same expansion as S_floating.
    uint64_t const sign     = (mem >> 31) & 1u;
    uint32_t const exp8     = (mem >> 23) & 0xFFu;
    uint32_t const frac_hi  = (mem >> 16) & 0x7Fu;
    uint32_t const frac_lo  =  mem        & 0xFFFFu;

    if (exp8 == 0) {
        return sign << 63;
    }

    uint32_t const msb  = (exp8 >> 7) & 1u;
    uint32_t const fill = msb ? 0u : 7u;
    uint32_t const exp11 = (msb << 10) | (fill << 7) | (exp8 & 0x7Fu);

    // 23-bit fraction in register, padded with zeros at [28:0].
    uint32_t const frac23 = (frac_hi << 16) | frac_lo;

    return (sign << 63)
         | (static_cast<uint64_t>(exp11) << 52)
         | (static_cast<uint64_t>(frac23) << 29);
}


[[nodiscard]] constexpr uint32_t convertF_FloatingToMemory(uint64_t reg) noexcept
{
    uint32_t const sign  = static_cast<uint32_t>((reg >> 63) & 1u);
    uint32_t const exp11 = static_cast<uint32_t>((reg >> 52) & 0x7FFu);
    uint32_t const frac  = static_cast<uint32_t>((reg >> 29) & 0x7FFFFFu);

    if (exp11 == 0) {
        return sign << 31;
    }

    uint32_t const exp8 = ((exp11 >> 3) & 0x80u) | (exp11 & 0x7Fu);

    // Frac is 23 bits.  Split high 7 / low 16 for word-swapped layout.
    uint32_t const frac_hi = (frac >> 16) & 0x7Fu;
    uint32_t const frac_lo =  frac        & 0xFFFFu;

    return (sign  << 31)
         | (exp8  << 23)
         | (frac_hi << 16)
         | frac_lo;
}


// ---------------------------------------------------------------------------
// VAX G_floating (64-bit) -- memory <-> register conversion.
// ---------------------------------------------------------------------------
//
// VAX G format is a 64-bit value with four 16-bit words.  Alpha LDG
// performs a word swap on load: word0 in memory ends up in bits[63:48]
// of the register; word1 -> [47:32]; word2 -> [31:16]; word3 -> [15:0].
//
// The register form holds the same content (no exponent expansion --
// VAX G already uses 11-bit exponent); only the word order changes.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr uint64_t convertG_FloatingToRegister(uint64_t mem) noexcept
{
    uint64_t const w0 = (mem >>  0) & 0xFFFFu;
    uint64_t const w1 = (mem >> 16) & 0xFFFFu;
    uint64_t const w2 = (mem >> 32) & 0xFFFFu;
    uint64_t const w3 = (mem >> 48) & 0xFFFFu;
    return (w0 << 48) | (w1 << 32) | (w2 << 16) | w3;
}

[[nodiscard]] constexpr uint64_t convertG_FloatingToMemory(uint64_t reg) noexcept
{
    // Inverse of the above -- same swap.
    uint64_t const w0 = (reg >>  0) & 0xFFFFu;
    uint64_t const w1 = (reg >> 16) & 0xFFFFu;
    uint64_t const w2 = (reg >> 32) & 0xFFFFu;
    uint64_t const w3 = (reg >> 48) & 0xFFFFu;
    return (w0 << 48) | (w1 << 32) | (w2 << 16) | w3;
}

} // namespace fBox

#endif // FBOXLIB_FP_FORMAT_H
