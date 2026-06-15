// ============================================================================
// iBoxLib/grains/ControlFlow.cpp -- iBox branch and jump executors (v1)
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
// Hand-written leaf functions for the iBox dispatch arm.  First-wave
// scope is the ten control-flow leaves wired in the master TSV:
//
//   Bra-format direct branches (opcode 0x30..0x3F):
//     BR   (0x30)  unconditional with link
//     BSR  (0x34)  unconditional with link to subroutine
//     BEQ  (0x39)  branch if Ra == 0
//     BLT  (0x3A)  branch if Ra <  0  (signed)
//     BNE  (0x3D)  branch if Ra != 0
//     BGE  (0x3E)  branch if Ra >= 0  (signed)
//
//   Jmp-format indirect (opcode 0x1A, sub 0..3):
//     JMP            (sub 0)  indirect jump
//     JSR            (sub 1)  indirect call
//     RET            (sub 2)  return; PC popped from Rb
//     JSR_COROUTINE  (sub 3)  coroutine swap
//
// V4 v1 stance: every branch is treated as 100 % mispredicted.  No
// branch-target buffer, no return-address stack, no taken /
// not-taken predictor.  When a branch reaches EX and is taken (or
// the jump always diverts), the leaf packs divert = true and
// divertTarget into BoxResult; the pipeline driver squashes whatever
// was fetched after the branch and refetches from divertTarget.
// When a conditional branch is not taken, the leaf returns with
// divert = false and the sequentially-fetched next instruction is
// correct.  The predictor work that would optimise this is
// deliberately deferred (see project_v4_branch_prediction.md).
//
// Encoding shorthands:
//
//   Bra-format (BR / BSR / Bxx):
//     bits[31:26]  primary opcode
//     bits[25:21]  Ra  (write return PC for BR/BSR; read for Bxx)
//     bits[20:0]   21-bit signed longword displacement
//     target       grain.pc + 4 + (sext_21(disp) << 2)
//
//   Jmp-format (JMP / JSR / RET / JSR_COROUTINE):
//     bits[31:26]  0x1A primary opcode
//     bits[25:21]  Ra  (return PC dest; R31 means no-write)
//     bits[20:16]  Rb  (target source; opB at EX)
//     bits[15:14]  function code (0=JMP, 1=JSR, 2=RET, 3=JSR_COR)
//     bits[13:0]   prediction hint -- ignored under 100 % misprediction
//     target       c.opB & ~3   (low 2 bits architecturally zero)
//
// Return-PC convention:
//
//   BR, BSR, JMP, JSR, RET, JSR_COROUTINE all carry S_WritesRa.  The
//   leaf packs regWriteIdx = Ra and regWriteValue = grain.pc + 4.
//   When Ra is encoded as R31 the index equals kNoRegWrite and the
//   MEM drainer skips the regfile commit -- so "BR R31, target" and
//   "JMP R31, (Rb)" are unconditional branches with no link, exactly
//   per Alpha SRM convention.
//
// ============================================================================

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

#include <cstdint>

namespace iBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;


// ---------------------------------------------------------------------------
// Encoding helpers.
// ---------------------------------------------------------------------------

// 21-bit signed longword displacement from a Bra-format encoding,
// returned as a 64-bit byte offset (multiplied by 4).  Implemented
// via signed left-then-arithmetic-right shift to sign-extend bit 20
// without branching.  C++20 defines signed shift, which V4 targets.
[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr int64_t braDispSext(InstructionGrain const& g) noexcept
{
    const int32_t signedEnc = static_cast<int32_t>(g.encoded);
    const int32_t disp21    = (signedEnc << 11) >> 11;   // sext bits[20:0]
    return static_cast<int64_t>(disp21) * 4;             // longwords -> bytes
}

// Ra field at encoded[25:21].  Doubles as the return-PC destination
// for branches with link; an Ra of 31 trips the kNoRegWrite check at
// MEM and suppresses the commit.
[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint8_t raIndex(InstructionGrain const& g) noexcept
{
    return static_cast<uint8_t>((g.encoded >> 21) & 0x1Fu);
}


#pragma region Bra-format Unconditional

// ----------------------------------------------------------------------------
// BR Ra, disp -- always taken.  Ra <- pc + 4, PC <- target.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBr(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = (g.pc + 4) & ~uint64_t{3};   // clear PALmode/align bits from link (AXPBox: pc & ~3)
    r.divertTarget  = g.pc + 4 + static_cast<uint64_t>(braDispSext(g));
    r.divert        = true;
    return r;
}

// ----------------------------------------------------------------------------
// BSR Ra, disp -- branch to subroutine.  Identical packing to BR; the
// S_CallBased flag in semFlags differentiates the two for trace and
// future return-stack work.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBsr(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = (g.pc + 4) & ~uint64_t{3};   // clear PALmode/align bits from link (AXPBox: pc & ~3)
    r.divertTarget  = g.pc + 4 + static_cast<uint64_t>(braDispSext(g));
    r.divert        = true;
    return r;
}

#pragma endregion Bra-format Unconditional


#pragma region Bra-format Conditional

// ----------------------------------------------------------------------------
// BEQ Ra, disp -- branch if Ra == 0.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBeq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (c.opA == 0);

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BNE Ra, disp -- branch if Ra != 0.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBne(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (c.opA != 0);

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BLT Ra, disp -- branch if Ra < 0 (signed).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBlt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (static_cast<int64_t>(c.opA) < 0);

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BGE Ra, disp -- branch if Ra >= 0 (signed).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBge(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (static_cast<int64_t>(c.opA) >= 0);

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BLE Ra, disp -- branch if Ra <= 0 (signed).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBle(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (static_cast<int64_t>(c.opA) <= 0);

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BGT Ra, disp -- branch if Ra > 0 (signed).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBgt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (static_cast<int64_t>(c.opA) > 0);

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BLBC Ra, disp -- branch if low bit of Ra is clear.  Distinct from
// BEQ: tests bit 0 of Ra, not Ra == 0.  Used by SRM PALcode for
// boolean-flag-driven loops (CSERVE init loop sets R0 = (R3 == limit)
// and BLBCs back if not equal).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBlbc(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (c.opA & 1ULL) == 0;

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

// ----------------------------------------------------------------------------
// BLBS Ra, disp -- branch if low bit of Ra is set.  Symmetric pair
// to BLBC.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBlbs(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const bool taken = (c.opA & 1ULL) != 0;

    BoxResult r;
    r.semFlags     = g.semFlags;
    r.divert       = taken;
    r.divertTarget = taken
        ? (g.pc + 4 + static_cast<uint64_t>(braDispSext(g)))
        : 0;
    return r;
}

#pragma endregion Bra-format Conditional


#pragma region Jmp-format Indirect

// ----------------------------------------------------------------------------
// JMP Ra, (Rb), hint -- indirect unconditional jump.  Target from Rb,
// low 2 bits cleared.  Ra <- pc + 4 (R31 suppresses commit).  Hint
// bits in encoded[13:0] are ignored under 100 % misprediction.
//
// JMP / JSR / RET / JSR_COROUTINE share an identical packing; their
// flags differ in semFlags (S_CallBased on JSR / JSR_COROUTINE,
// S_RetBased on RET / JSR_COROUTINE) and the dispatch routing comes
// from the function-code sub-decode, not the leaf body.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execJmp(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = (g.pc + 4) & ~uint64_t{3};   // clear PALmode/align bits from link (AXPBox: pc & ~3)
    r.divertTarget  = (c.opB & ~0x3ULL) | (g.pc & 1ULL);  // carry PALmode (PC<0>) across the indirect jump
    r.divert        = true;
    return r;
}

// ----------------------------------------------------------------------------
// JSR Ra, (Rb), hint -- indirect call.  Same shape as JMP.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execJsr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = (g.pc + 4) & ~uint64_t{3};   // clear PALmode/align bits from link (AXPBox: pc & ~3)
    r.divertTarget  = (c.opB & ~0x3ULL) | (g.pc & 1ULL);  // carry PALmode (PC<0>) across the indirect jump
    r.divert        = true;
    return r;
}

// ----------------------------------------------------------------------------
// RET Ra, (Rb), hint -- return; PC popped from Rb.  Same shape; the
// S_RetBased flag will let a future return-address stack consumer
// distinguish RET from a generic JMP.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execRet(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = (g.pc + 4) & ~uint64_t{3};   // clear PALmode/align bits from link (AXPBox: pc & ~3)
    r.divertTarget  = (c.opB & ~0x3ULL) | (g.pc & 1ULL);  // carry PALmode (PC<0>) across the indirect jump
    r.divert        = true;
    return r;
}

// ----------------------------------------------------------------------------
// JSR_COROUTINE Ra, (Rb), hint -- coroutine swap.  Same shape; both
// S_CallBased and S_RetBased are set in semFlags.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execJsrCoroutine(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = (g.pc + 4) & ~uint64_t{3};   // clear PALmode/align bits from link (AXPBox: pc & ~3)
    r.divertTarget  = (c.opB & ~0x3ULL) | (g.pc & 1ULL);  // carry PALmode (PC<0>) across the indirect jump
    r.divert        = true;
    return r;
}

#pragma endregion Jmp-format Indirect

} // namespace iBox
