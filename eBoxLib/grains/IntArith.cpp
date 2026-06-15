// ============================================================================
// eBoxLib/grains/IntArith.cpp -- INTA leaf executors (opcode 0x10)
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
// Hand-written leaf functions for the INTA dispatch arm (opcode 0x10:
// integer arithmetic).  Each leaf reads pre-resolved operands from
// ExecCtx (opA = Ra, opB = Rb or 8-bit literal -- the GR stage
// chooses based on grain.semFlags & S_HasLit), computes the result,
// and packs a regfile write into BoxResult.  WB does the actual
// commit at MEM stage drain time.
//
// ============================================================================

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"
#include "coreLib/bitutils.h"
#include "coreLib/alpha_int_byteops.h"
#include "coreLib/alpha_int_helpers.h"
#include "coreLib/ProcessorVariant.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "fBoxLib/grains/FpFormat.h"   // convertS_/convertF_FloatingToRegister (ITOFx)

#include <cstdint>
#include <cstdio>   // TEMP (spec v2 sec.0 RPCC probe) 2026-06-01 -- revert with probe
#include <cstdlib>  // TEMP (spec v2 sec.0 RPCC probe) 2026-06-01 -- revert with probe

namespace eBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;


#pragma region Miscellaneous Instructions
AXP_HOT AXP_FLATTEN
auto execAmask(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // AMASK Rb, Rc (Ra encoded as R31): Rc <- Rb & ~supported_features
    //
    // Bits cleared in the result mark features the implementation
    // supports.  21264 / EV6 supports:
    //   bit 0 (0x001)  BWX  byte/word extension
    //   bit 1 (0x002)  FIX  square root + fp/int convert
    //   bit 2 (0x004)  CIX  count extension (CTPOP/CTLZ/CTTZ)
    //   bit 8 (0x100)  MVI  multimedia extension (MIN/MAX/PERR/PK/UNPK)
    //
    // PAT (bit 9, EV67+) and PMI (bit 10, EV68+) are not 21264 base.
    // When CpuState is wired through ExecCtx.cpu the constant
    // becomes c.cpu->amask().
    constexpr uint64_t kAmask21264 =
        (1ULL << 0)   // BWX
        | (1ULL << 1)   // FIX
        | (1ULL << 2)   // CIX
        | (1ULL << 8);  // MVI

    const uint64_t result = c.opB & ~kAmask21264;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

#pragma endregion Miscellaneous Instructions

#pragma  region Integer Arithmetic Instructions
// ----------------------------------------------------------------------------
// ADDL Ra, Rb, Rc -- 32-bit integer add, result sign-extended to 64
// ----------------------------------------------------------------------------
//
// Encoding (Op format, opcode 0x10, func 0x00):
//   bits[31:26] = 0x10   primary opcode (INTA)
//   bits[25:21] = Ra     first source register
//   bits[20:16] = Rb     second source register
//   bits[15:13] = unused
//   bit[12]     = IMM    set when Rb field holds an 8-bit literal
//   bits[11:5]  = func   0x00 for ADDL
//   bits[4:0]   = Rc     destination register
//
// Operation:
//   Rc <- sign_extend_32_to_64(Ra<31:0> + Rb<31:0>)
//
// The IMM bit selection has already been resolved by GR -- if IMM is
// set, ctx.opB carries the literal; otherwise it carries the Rb regfile
// value.  The leaf is agnostic to that distinction.
//
AXP_HOT AXP_FLATTEN
BoxResult execAddl(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    int32_t a = static_cast<int32_t>(c.opA);
    int32_t b = static_cast<int32_t>(c.opB);
    int32_t sum32 = a + b;          // 32-bit add; overflow wraps per Alpha SRM
    int64_t result = sum32;         // implicit sign-extension to 64 bits

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);   // Rc
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(result);
    return r;
}
    // ----------------------------------------------------------------------------
// SUBL Ra, Rb, Rc -- 32-bit integer subtract, result sign-extended to 64
// ----------------------------------------------------------------------------
//
// Operation: Rc <- sign_extend_32_to_64(Ra<31:0> - Rb<31:0>)
//
// Underflow wraps modulo 2^32 per Alpha SRM (no trap; the _V variants
// trap, but those are separate rows and are not implemented in v1).
//
AXP_HOT AXP_FLATTEN
BoxResult execSubl(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    int32_t a = static_cast<int32_t>(c.opA);
    int32_t b = static_cast<int32_t>(c.opB);
    int32_t diff32 = a - b;
    int64_t result = diff32;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(result);
    return r;
}
// ----------------------------------------------------------------------------
// ADDQ Ra, Rb, Rc -- 64-bit integer add (no sign-extension; full width)
// ----------------------------------------------------------------------------
//
// Operation: Rc <- Ra + Rb  (modulo 2^64)
//
// Overflow wraps; the _V variant traps but is a separate row.
//
AXP_HOT AXP_FLATTEN
BoxResult execAddq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint64_t result = c.opA + c.opB;        // unsigned add wraps mod 2^64

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}
// ----------------------------------------------------------------------------
// SUBQ Ra, Rb, Rc -- 64-bit integer subtract
// ----------------------------------------------------------------------------
//
// Operation: Rc <- Ra - Rb  (modulo 2^64)
//
AXP_HOT AXP_FLATTEN
BoxResult execSubq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint64_t result = c.opA - c.opB;        // unsigned subtract wraps mod 2^64

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// Scaled add / sub family.  Common Alpha pattern for array indexing
// where each element is a longword (S4*) or quadword (S8*).  All eight
// variants are pure ALU ops with no traps; the longword variants
// sign-extend the 32-bit result, the quadword variants leave the 64
// bits as-is.  PALcode uses these heavily for stepping through PAL
// data tables (procedure values at +4 strides, queue links at +8
// strides, dispatch tables, etc.).
//
//   S4ADDL: Rc = sext((Ra*4 + Rb)<31:0>)
//   S4SUBL: Rc = sext((Ra*4 - Rb)<31:0>)
//   S8ADDL: Rc = sext((Ra*8 + Rb)<31:0>)
//   S8SUBL: Rc = sext((Ra*8 - Rb)<31:0>)
//   S4ADDQ: Rc = Ra*4 + Rb
//   S4SUBQ: Rc = Ra*4 - Rb
//   S8ADDQ: Rc = Ra*8 + Rb
//   S8SUBQ: Rc = Ra*8 - Rb
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
BoxResult execS4addl(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint32_t const lo = static_cast<uint32_t>((c.opA << 2) + c.opB);
    int64_t  const sx = static_cast<int64_t>(static_cast<int32_t>(lo));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(sx);
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS4subl(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint32_t const lo = static_cast<uint32_t>((c.opA << 2) - c.opB);
    int64_t  const sx = static_cast<int64_t>(static_cast<int32_t>(lo));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(sx);
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS8addl(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint32_t const lo = static_cast<uint32_t>((c.opA << 3) + c.opB);
    int64_t  const sx = static_cast<int64_t>(static_cast<int32_t>(lo));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(sx);
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS8subl(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint32_t const lo = static_cast<uint32_t>((c.opA << 3) - c.opB);
    int64_t  const sx = static_cast<int64_t>(static_cast<int32_t>(lo));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(sx);
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS4addq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = (c.opA << 2) + c.opB;
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS4subq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = (c.opA << 2) - c.opB;
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS8addq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = (c.opA << 3) + c.opB;
    return r;
}

AXP_HOT AXP_FLATTEN
BoxResult execS8subq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = (c.opA << 3) - c.opB;
    return r;
}


#pragma  endregion Integer Arithmetic Instructions


#pragma  region Comparison Arithmetic Instructions
// ----------------------------------------------------------------------------
// CMPEQ Ra, Rb, Rc -- 64-bit compare equal
// ----------------------------------------------------------------------------
//
// Operation: Rc <- (Ra == Rb) ? 1 : 0
//
// Compares the full 64-bit operands.  Sets Rc to 1 on equality,
// 0 otherwise.  Used by branch sequences and conditional moves.
//
AXP_HOT AXP_FLATTEN
BoxResult execCmpeq(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    uint64_t result = (c.opA == c.opB) ? 1u : 0u;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmpult(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMPULT Ra, Rb, Rc: Rc <- (Ra < Rb) ? 1 : 0  (unsigned)
    const uint64_t result = (c.opA < c.opB) ? 1ULL : 0ULL;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmpule(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMPULE Ra, Rb, Rc: Rc <- (Ra <= Rb) ? 1 : 0  (unsigned)
    const uint64_t result = (c.opA <= c.opB) ? 1ULL : 0ULL;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmplt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMPLT Ra, Rb, Rc: Rc <- (Ra < Rb) ? 1 : 0  (signed)
    const int64_t a = static_cast<int64_t>(c.opA);
    const int64_t b = static_cast<int64_t>(c.opB);
    const uint64_t result = (a < b) ? 1ULL : 0ULL;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmple(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMPLE Ra, Rb, Rc: Rc <- (Ra <= Rb) ? 1 : 0  (signed)
    const int64_t a = static_cast<int64_t>(c.opA);
    const int64_t b = static_cast<int64_t>(c.opB);
    const uint64_t result = (a <= b) ? 1ULL : 0ULL;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}



AXP_HOT AXP_FLATTEN
auto execUmulh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // UMULH Ra, Rb, Rc: Rc <- (Ra * Rb) >> 64; upper 64 bits of the
    // unsigned 128-bit product.  All platform-specific math now lives
    // in alpha_int::umulh.  UMULH never traps, so the status output
    // is unused; the helper takes it to keep the signature uniform
    // with the trapping arithmetic helpers.
    alpha_int::IntStatus st;
    const uint64_t result = alpha_int::umulh(c.opA, c.opB, st);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// MULQ Ra, Rb, Rc: Rc <- (Ra * Rb)<63:0>.  Low 64 bits of the
// signed/unsigned product (low half is identical for both
// signednesses).  C++ unsigned multiplication on 64-bit values
// produces the correct low-half result; signed overflow is a
// non-trap variant in this row (the trap variant would carry S_FpTrap
// or an integer-overflow indicator -- not present in the v1 cut).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMulq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = c.opA * c.opB;     // unsigned multiply truncated to 64 bits
    return r;
}


// ----------------------------------------------------------------------------
// MULL Ra, Rb, Rc: Rc <- sext(Ra<31:0> * Rb<31:0>)<31:0>.  32-bit
// multiply with 64-bit sign-extension of the 32-bit result.  Per
// Alpha SRM, the operands are taken as their low 32 bits (the upper
// 32 bits are ignored) and the 32-bit signed product is sign-extended
// into Rc.  C++: cast to uint32_t, multiply (wraps modulo 2^32), then
// sign-extend via int32_t -> int64_t.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMull(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint32_t const lo32 =
        static_cast<uint32_t>(c.opA) * static_cast<uint32_t>(c.opB);
    int64_t  const sext =
        static_cast<int64_t>(static_cast<int32_t>(lo32));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(sext);
    return r;
}

#pragma  endregion Comparison Arithmetic Instructions
#pragma  region Bit Count Family Instructions



AXP_HOT AXP_FLATTEN
auto execCtpop(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CTPOP Rb, Rc: Rc <- popcount(Rb).  FpTiExt rows read only Rb.
    const uint64_t result = static_cast<uint64_t>(BitUtils::popcount(c.opB));

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

// CTLZ -- BitUtils form
AXP_HOT AXP_FLATTEN
auto execCtlz(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = c.opB
        ? (63ULL - BitUtils::highestSetBit(c.opB))
        : 64ULL;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

// CTTZ -- BitUtils form
AXP_HOT AXP_FLATTEN
auto execCttz(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = c.opB
        ? static_cast<uint64_t>(BitUtils::lowestSetBit(c.opB))
        : 64ULL;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

#pragma  endregion Bit Count Family Instructions

#pragma  region Logical Operation Instructions
// ====================================================================
// Logical Operations -- 
// The IMM bit check is a GR-stage concern, not a leaf concern. 
// By the time the leaf runs at EX, ctx.opB already holds the right value 
// -- either the 8-bit literal zero-extended to 64 bits 
// (when the IMM bit was set) or the Rb register's value (when it was not). 
// The leaf just reads c.opB without knowing or caring where it came from.
// ====================================================================
AXP_HOT AXP_FLATTEN
auto execAnd(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = c.opA & c.opB;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execBic(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // BIC = Bit Clear (AND NOT): Rc <- Ra & ~Rb
    const uint64_t result = c.opA & ~c.opB;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}
AXP_HOT AXP_FLATTEN
auto execBis(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // BIS = bitwise OR; canonical Alpha pseudo-MOV is `bis Rs, R31, Rd`.
    const uint64_t result = c.opA | c.opB;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}
AXP_HOT AXP_FLATTEN
auto execEqv(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // EQV = XNOR (Equivalence): result has 1 in every bit position
    // where Ra and Rb agree, 0 where they differ.
    const uint64_t result = ~(c.opA ^ c.opB);

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}
AXP_HOT AXP_FLATTEN
auto execOrnot(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // ORNOT: Rc <- Ra | ~Rb
    const uint64_t result = c.opA | ~c.opB;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execXor(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = c.opA ^ c.opB;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

#pragma  endregion Logical Operation Instructions
// ====================================================================
// Conditional Move Operations
// ====================================================================
AXP_HOT AXP_FLATTEN
auto execCmoveq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVEQ: move if Ra == 0; else Rc unchanged.
    const bool met = (c.opA == 0);

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovne(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVNE: move if Ra != 0; else Rc unchanged.
    const bool met = (c.opA != 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovlt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVLT: move if Ra < 0 (signed); else Rc unchanged.
    const bool met = (static_cast<int64_t>(c.opA) < 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovge(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVGE: move if Ra >= 0 (signed); else Rc unchanged.
    const bool met = (static_cast<int64_t>(c.opA) >= 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovle(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVLE: move if Ra <= 0 (signed); else Rc unchanged.
    const bool met = (static_cast<int64_t>(c.opA) <= 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovgt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVGT: move if Ra > 0 (signed); else Rc unchanged.
    const bool met = (static_cast<int64_t>(c.opA) > 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovlbs(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVLBS: move if low bit of Ra is set; else Rc unchanged.
    const bool met = ((c.opA & 1ULL) != 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execCmovlbc(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // CMOVLBC: move if low bit of Ra is clear; else Rc unchanged.
    const bool met = ((c.opA & 1ULL) == 0);
    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = met ? static_cast<uint8_t>(g.encoded & 0x1F)
        : coreLib::kNoRegWrite;
    r.regWriteIsFp = false;
    r.regWriteValue = met ? c.opB : 0;
    return r;
}
#pragma region Logical and Shift Instructions

// ====================================================================
// Shift Operations
// ====================================================================
AXP_HOT AXP_FLATTEN
auto execSll(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // SLL Ra, Rb, Rc: Rc <- Ra << (Rb<5:0>); zero-fill from right.
    // Alpha SRM uses only the low 6 bits of opB as shift count.
    const unsigned shiftAmount = static_cast<unsigned>(c.opB & 0x3F);
    const uint64_t result = c.opA << shiftAmount;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execSrl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // SRL Ra, Rb, Rc: Rc <- Ra >> (Rb<5:0>); zero-fill from left (logical).
    // Logical because c.opA is uint64_t -- C++ guarantees logical shift
    // for unsigned right shift.
    const unsigned shiftAmount = static_cast<unsigned>(c.opB & 0x3F);
    const uint64_t result = c.opA >> shiftAmount;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execSra(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // SRA Ra, Rb, Rc: Rc <- Ra >> (Rb<5:0>); sign-extend from left (arithmetic).
    // C++20 guarantees signed right shift is arithmetic (preserves sign);
    // the project is on C++20 so this is portable.
    const unsigned shiftAmount = static_cast<unsigned>(c.opB & 0x3F);
    const int64_t  result = static_cast<int64_t>(c.opA) >> shiftAmount;

    BoxResult r;
    r.semFlags = g.semFlags;
    r.regWriteIdx = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp = false;
    r.regWriteValue = static_cast<uint64_t>(result);
    return r;
}
#pragma endregion Logical and Shift Instructions

#pragma region Byte Manipulation Instructions

// ----------------------------------------------------------------------------
// CMPBGE Ra, Rb, Rc -- byte-wise compare bytes >= (unsigned)
// ----------------------------------------------------------------------------
//
// For each byte position i in 0..7, set bit i of Rc to 1 when byte i of
// Ra is >= byte i of Rb (unsigned compare); else 0.  Rc<63:8> = 0.
//
AXP_HOT AXP_FLATTEN
auto execCmpbge(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        const uint8_t byteA = static_cast<uint8_t>(c.opA >> (i * 8));
        const uint8_t byteB = static_cast<uint8_t>(c.opB >> (i * 8));
        if (byteA >= byteB) {
            result |= (1ULL << i);
        }
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// EXTxL / EXTxH -- extract a byte/word/long/quad from Ra at offset Rb<2:0>
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execExtbl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extbl(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execExtwl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extwl(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execExtll(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extll(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execExtql(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extql(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execExtwh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extwh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execExtlh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extlh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execExtqh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::extqh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// INSxL / INSxH -- insert a byte/word/long/quad into a 64-bit value at offset
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execInsbl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::insbl(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execInswl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::inswl(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execInsll(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::insll(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execInsql(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::insql(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execInswh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::inswh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execInslh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::inslh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execInsqh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::insqh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// MSKxL / MSKxH -- mask out a byte/word/long/quad of Ra at offset Rb<2:0>
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execMskbl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::mskbl(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMskwl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::mskwl(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMskll(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::mskll(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMskql(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::mskql(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMskwh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::mskwh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMsklh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::msklh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMskqh(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const uint64_t result = alpha_byteops::mskqh(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// SEXTB / SEXTW -- sign-extend low 8 / 16 bits of opB to 64 bits
// ----------------------------------------------------------------------------
//
// Alpha SRM specifies Ra is encoded as R31 (zero) for SEXT*; assemblers
// enforce this.  v1 does not raise IllegalInstruction if Ra != R31.
//
AXP_HOT AXP_FLATTEN
auto execSextb(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const int64_t result = static_cast<int64_t>(static_cast<int8_t>(c.opB));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(result);
    return r;
}

AXP_HOT AXP_FLATTEN
auto execSextw(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const int64_t result = static_cast<int64_t>(static_cast<int16_t>(c.opB));

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = static_cast<uint64_t>(result);
    return r;
}


// ----------------------------------------------------------------------------
// ZAP / ZAPNOT -- mask-driven byte zeroing
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execZap(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // ZAP: zero byte i of Ra when bit i of opB is set.
    const uint64_t result = alpha_byteops::zap(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execZapnot(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // ZAPNOT: keep byte i of Ra when bit i of opB is set; zero otherwise.
    const uint64_t result = alpha_byteops::zapnot(c.opA, c.opB);

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

#pragma endregion Byte Manipulation Instructions

#pragma region MVI / Pixel Operations

// ----------------------------------------------------------------------------
// MAXUB8 / MAXSB8 / MAXUW4 / MAXSW4 -- per-lane max
// MINUB8 / MINSB8 / MINUW4 / MINSW4 -- per-lane min
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execMaxub8(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MAXUB8: Rc[i] = unsigned max(Ra[i], Rb[i]) for i in 0..7
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        const uint8_t a = static_cast<uint8_t>(c.opA >> (i * 8));
        const uint8_t b = static_cast<uint8_t>(c.opB >> (i * 8));
        result |= static_cast<uint64_t>((a > b) ? a : b) << (i * 8);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMaxsb8(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MAXSB8: Rc[i] = signed max(Ra[i], Rb[i]) for i in 0..7
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        const int8_t a = static_cast<int8_t>(c.opA >> (i * 8));
        const int8_t b = static_cast<int8_t>(c.opB >> (i * 8));
        const uint8_t v = static_cast<uint8_t>((a > b) ? a : b);
        result |= static_cast<uint64_t>(v) << (i * 8);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMaxuw4(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MAXUW4: Rc[i] = unsigned max(Ra[i], Rb[i]) for i in 0..3 (16-bit lanes)
    uint64_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const uint16_t a = static_cast<uint16_t>(c.opA >> (i * 16));
        const uint16_t b = static_cast<uint16_t>(c.opB >> (i * 16));
        result |= static_cast<uint64_t>((a > b) ? a : b) << (i * 16);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMaxsw4(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MAXSW4: Rc[i] = signed max(Ra[i], Rb[i]) for i in 0..3 (16-bit lanes)
    uint64_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const int16_t a = static_cast<int16_t>(c.opA >> (i * 16));
        const int16_t b = static_cast<int16_t>(c.opB >> (i * 16));
        const uint16_t v = static_cast<uint16_t>((a > b) ? a : b);
        result |= static_cast<uint64_t>(v) << (i * 16);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMinub8(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MINUB8: Rc[i] = unsigned min(Ra[i], Rb[i]) for i in 0..7
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        const uint8_t a = static_cast<uint8_t>(c.opA >> (i * 8));
        const uint8_t b = static_cast<uint8_t>(c.opB >> (i * 8));
        result |= static_cast<uint64_t>((a < b) ? a : b) << (i * 8);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMinsb8(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MINSB8: Rc[i] = signed min(Ra[i], Rb[i]) for i in 0..7
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        const int8_t a = static_cast<int8_t>(c.opA >> (i * 8));
        const int8_t b = static_cast<int8_t>(c.opB >> (i * 8));
        const uint8_t v = static_cast<uint8_t>((a < b) ? a : b);
        result |= static_cast<uint64_t>(v) << (i * 8);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMinuw4(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MINUW4: Rc[i] = unsigned min(Ra[i], Rb[i]) for i in 0..3 (16-bit lanes)
    uint64_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const uint16_t a = static_cast<uint16_t>(c.opA >> (i * 16));
        const uint16_t b = static_cast<uint16_t>(c.opB >> (i * 16));
        result |= static_cast<uint64_t>((a < b) ? a : b) << (i * 16);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execMinsw4(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // MINSW4: Rc[i] = signed min(Ra[i], Rb[i]) for i in 0..3 (16-bit lanes)
    uint64_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const int16_t a = static_cast<int16_t>(c.opA >> (i * 16));
        const int16_t b = static_cast<int16_t>(c.opB >> (i * 16));
        const uint16_t v = static_cast<uint16_t>((a < b) ? a : b);
        result |= static_cast<uint64_t>(v) << (i * 16);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// PERR -- pixel error (sum of absolute byte differences across 8 lanes)
// ----------------------------------------------------------------------------
//
// Rc <- sum(i in 0..7) abs(Ra[i] - Rb[i])  (per-byte unsigned difference)
//
AXP_HOT AXP_FLATTEN
auto execPerr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        const uint8_t a = static_cast<uint8_t>(c.opA >> (i * 8));
        const uint8_t b = static_cast<uint8_t>(c.opB >> (i * 8));
        result += (a >= b) ? static_cast<uint64_t>(a - b)
                           : static_cast<uint64_t>(b - a);
    }

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// UNPKBW / UNPKBL -- unpack bytes into wider lanes (zero-extended)
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execUnpkbw(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // UNPKBW: low 4 bytes of Rb spread into 4 16-bit lanes of Rc, zero-extended.
    const uint64_t result =
          ((c.opB >>  0) & 0xFFULL) <<  0
        | ((c.opB >>  8) & 0xFFULL) << 16
        | ((c.opB >> 16) & 0xFFULL) << 32
        | ((c.opB >> 24) & 0xFFULL) << 48;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execUnpkbl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // UNPKBL: low 2 bytes of Rb spread into 2 32-bit lanes of Rc, zero-extended.
    const uint64_t result =
          ((c.opB >> 0) & 0xFFULL) <<  0
        | ((c.opB >> 8) & 0xFFULL) << 32;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}


// ----------------------------------------------------------------------------
// PKWB / PKLB -- pack low byte of each lane into a contiguous low halfword
// ----------------------------------------------------------------------------

AXP_HOT AXP_FLATTEN
auto execPkwb(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // PKWB: low byte of each of Rb's 4 16-bit lanes -> 4-byte low halfword.
    const uint64_t result =
          ((c.opB >>  0) & 0xFFULL) <<  0
        | ((c.opB >> 16) & 0xFFULL) <<  8
        | ((c.opB >> 32) & 0xFFULL) << 16
        | ((c.opB >> 48) & 0xFFULL) << 24;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

AXP_HOT AXP_FLATTEN
auto execPklb(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // PKLB: low byte of each of Rb's 2 32-bit lanes -> 2-byte low halfword.
    const uint64_t result =
          ((c.opB >>  0) & 0xFFULL) << 0
        | ((c.opB >> 32) & 0xFFULL) << 8;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = result;
    return r;
}

#pragma endregion MVI / Pixel Operations


#pragma region Misc IPR-Touching Instructions

// ----------------------------------------------------------------------------
// RC / RS -- read-and-modify the per-CPU intrFlag IPR
// ----------------------------------------------------------------------------
//
// RC: Ra <- intrFlag; then intrFlag <- 1.
// RS: Ra <- intrFlag; then intrFlag <- 0.
//
// Both encode the destination in the Ra field (memory format), not Rc.
//
// V4 v1 placeholder: the per-CPU intrFlag IPR is owned by CpuState
// which is not yet wired through ExecCtx.cpu.  Until that arrives,
// these leaves return kFaultUnimplemented.  When CpuState lands the
// body becomes:
//
//   const uint64_t result = c.cpu->iprIntrFlag() ? 1ULL : 0ULL;
//   c.cpu->setIprIntrFlag(true);   // RC: set;   RS: clear
//   r.regWriteValue = result;
//   r.faultCode     = kNoFault;
//
// ----------------------------------------------------------------------------
// RC Ra -- Read and Clear interrupt flag.  Misc-format opcode 0x18,
// function 0xE000.  Atomic primitive: Ra <- cpu.intrFlag; cpu.intrFlag <- 0.
// PALcode synchronization primitive.  Mutates CpuState directly --
// documented exception to the "leaves do not touch CpuState" contract,
// same as HW_MTPR's IPR writes.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execRc(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t const prior = c.cpu->intrFlag;
    c.cpu->intrFlag = 0;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>((g.encoded >> 21) & 0x1F);   // Ra
    r.regWriteIsFp  = false;
    r.regWriteValue = prior;
    return r;
}

// ----------------------------------------------------------------------------
// RS Ra -- Read and Set interrupt flag.  Misc-format opcode 0x18,
// function 0xF000.  Atomic primitive: Ra <- cpu.intrFlag; cpu.intrFlag <- 1.
// Symmetric to RC.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execRs(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t const prior = c.cpu->intrFlag;
    c.cpu->intrFlag = 1;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>((g.encoded >> 21) & 0x1F);   // Ra
    r.regWriteIsFp  = false;
    r.regWriteValue = prior;
    return r;
}

// ----------------------------------------------------------------------------
// RPCC Ra -- Read Processor Cycle Counter.  Misc-format opcode 0x18,
// function 0xC000.  Ra <- the architectural cycle counter.  Pure read:
// unlike RC/RS it mutates no CpuState.
//
// The value MUST match what HW_MFPR HW_CC returns -- RPCC and HW_CC are
// two architectural views of one counter, and the guest sees incoherent
// time if they diverge.  execHwMfpr's HW_CC case yields cpu.cycleCount +
// cpu.ccOffset (the free-running pipeline tick plus the offset that
// HW_MTPR HW_CC last wrote); this leaf mirrors that expression exactly.
// If the HW_CC representation is ever revised, both sites change together.
//
// Destination is the Ra field (bits 25:21), per S_WritesRa in the TSV --
// the same field RC/RS write.  This leaf replaces a kFaultUnimplemented
// codegen stub: firmware first reached a live RPCC (PC 0xdda8) after the
// 2026-05-13 INTERRUPT injection, and the stub's fault vectored it into
// an endless MCHK loop.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execRpcc(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>((g.encoded >> 21) & 0x1F);   // Ra
    r.regWriteIsFp  = false;
    // 2026-05-29: scale by CpuState::kCcMultiplier so the firmware's
    // RSCC handler (which divides RPCC by ~5594 on DS10 SRM) sees a
    // value that lets its delay loops terminate in tractable pipeline
    // cycles.  See CpuState.h kCcMultiplier comment for full rationale.
    r.regWriteValue = (c.cpu->cycleCount + c.cpu->ccOffset)
                    * coreLib::CpuState::kCcMultiplier;

    // ---- TEMP RPCC PROBE (timer spec v2 sec.0 gating measurement) 2026-06-01 ----
    // Every firmware rscc() funnels its internal `rpcc` through this leaf, so
    // logging here captures the timer_check delay loop (apisrm timer.c:1377-1395)
    // one read per iteration.  Goal: measure the loop SPAN in raw cycleCount.
    // kCcMultiplier STAYS IN for this run -- this measures the collapse, it does
    // NOT fix it.  Decision rule: the timer_check loop shows as the TIGHTEST
    // cluster of pal=1 reads (tiny cyc deltas between consecutive lines) ending
    // just before the no_timer print; its first->last cyc delta is the span.
    // span ~10^2-10^3 cycleCount => collapse confirmed, proceed to spec sec.5.1;
    // span ~10^6 => firmware divide was real, STOP.
    //
    // Self-opens its own file so capture does NOT depend on shell `2>` redirection
    // (the prior run's redirect did not take; UART mirror also owns stderr).
    // Threshold: EMULATR_RPCC_LOG_AFTER=<cycleCount> if set, ELSE defaults to
    //            185,000,000 at compile time -- the probe is ON by default in
    //            this build, NO shell env needed (env inheritance kept eating it).
    //            Set EMULATR_RPCC_LOG_AFTER to a huge value to effectively disable.
    // File:    EMULATR_RPCC_LOG_FILE=<path>          (default below, in V4 tree)
    // Bound:   EMULATR_RPCC_LOG_MAX=<lines>          (default 200000)
    // REVERT this whole block + the two TEMP includes after the gating run.
    static const uint64_t s_rpccLogAfter = []() -> uint64_t {
        const char* e = std::getenv("EMULATR_RPCC_LOG_AFTER");
        return e ? std::strtoull(e, nullptr, 0) : 185000000ull;
    }();
    if (s_rpccLogAfter && (c.cpu->cycleCount >= s_rpccLogAfter)) {
        static const uint64_t s_rpccLogMax = []() -> uint64_t {
            const char* e = std::getenv("EMULATR_RPCC_LOG_MAX");
            return e ? std::strtoull(e, nullptr, 0) : 200000ull;
        }();
        static std::FILE* s_rpccLogFp = []() -> std::FILE* {
            const char* p = std::getenv("EMULATR_RPCC_LOG_FILE");
            const char* path = p ? p
                : "D:\\EmulatR\\EmulatRAppUniV4\\rpcc_probe.txt";
            std::FILE* fp = std::fopen(path, "w");
            std::fprintf(stderr, "[RPCC probe] armed -> %s (fp=%p)\n",
                         path, (void*)fp);
            return fp;
        }();
        static uint64_t s_rpccLogCount = 0;
        if (s_rpccLogFp && s_rpccLogCount < s_rpccLogMax) {
            ++s_rpccLogCount;
            std::fprintf(s_rpccLogFp,
                "[RPCC] cyc=%llu off=%llu scaled=%llu pc=0x%llx pal=%d\n",
                (unsigned long long)c.cpu->cycleCount,
                (unsigned long long)c.cpu->ccOffset,
                (unsigned long long)r.regWriteValue,
                (unsigned long long)c.cpu->pcAddr(),
                (int)c.cpu->inPalMode());
            std::fflush(s_rpccLogFp);  // survive an abrupt exit / halt
        }
    }
    // ---- END TEMP RPCC PROBE 2026-06-01 ----

    return r;
}

#pragma endregion Misc IPR-Touching Instructions


#pragma region Implementation Version

// ----------------------------------------------------------------------------
// IMPLVER -- report processor implementation version
// ----------------------------------------------------------------------------
//
// Alpha SRM constants:
//   0 = EV4 / EV5 (21064, 21164)
//   1 = EV56      (21164PC)
//   2 = EV6       (21264, 21264A, 21264B, 21264C, 21264D)
//   3 = EV7       (21364)
//
// V4 targets 21264 -> constant 2.  When multi-version support arrives
// the source becomes c.cpu->implver instead of the constant.
//
AXP_HOT AXP_FLATTEN
auto execImplver(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    constexpr uint64_t kImplVer21264 = 2ULL;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);
    r.regWriteIsFp  = false;
    r.regWriteValue = kImplVer21264;
    return r;
}

#pragma endregion Implementation Version


// ----------------------------------------------------------------------------
// FTOIT Fa, Rc -- FP-to-integer register move, T-format (opcode 0x1C func 0x70).
// ----------------------------------------------------------------------------
// Moves the raw 64-bit contents of FP register Fa verbatim into integer
// register Rc.  The GR stage loaded opA from fpReg[Ra] (S_ReadsFp); FTOIT is a
// straight bit copy (T-format = no reformatting, unlike the S-format FTOIS).
// One half of the EV6 FIX extension; ITOFx (opcode 0x14) is the reverse.
AXP_HOT AXP_FLATTEN
auto execFtoit(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);   // Rc (integer dest)
    r.regWriteIsFp  = false;
    r.regWriteValue = c.opA;                                    // fpReg[Ra], raw bits
    return r;
}


// ----------------------------------------------------------------------------
// ITOFx Ra, Fc -- integer-to-FP register move (opcode 0x14).  Reverse of FTOIx:
// the GR stage loaded opA from intReg[Ra] (S_ReadsInt); reformat into the FP
// register image and commit to fpReg[Fc] (regWriteIsFp).
//   ITOFT 0x024 -- T-format: raw 64-bit copy (no reformat).
//   ITOFS 0x004 -- low 32 bits as IEEE S_floating -> register (same map as LDS).
//   ITOFF 0x014 -- low 32 bits as VAX  F_floating -> register (same map as LDF).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execItoft(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);   // Fc
    r.regWriteIsFp  = true;
    r.regWriteValue = c.opA;                                    // intReg[Ra], raw bits
    return r;
}

AXP_HOT AXP_FLATTEN
auto execItofs(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);   // Fc
    r.regWriteIsFp  = true;
    r.regWriteValue = fBox::convertS_FloatingToRegister(static_cast<uint32_t>(c.opA));
    return r;
}

AXP_HOT AXP_FLATTEN
auto execItoff(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1F);   // Fc
    r.regWriteIsFp  = true;
    r.regWriteValue = fBox::convertF_FloatingToRegister(static_cast<uint32_t>(c.opA));
    return r;
}

} // namespace eBox
