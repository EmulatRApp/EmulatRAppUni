// ============================================================================
// mBoxLib/grains/LoadStore.cpp -- mBox load/store leaf executors (v1 wave)
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
// Hand-written leaf functions for the mBox dispatch arm.  First-wave
// scope is the seventeen wired leaves: LDA, LDAH (Mem-format compute-
// only), LDBU, LDWU, LDL, LDQ, LDQ_U, LDL_L, LDQ_L (Mem-format loads),
// STB, STW, STL, STQ, STQ_U, STL_C, STQ_C (Mem-format stores), and
// FETCH (Misc-format prefetch hint).  HW_LD / HW_ST (PALmode Hw-format)
// remain stubbed for a follow-up wave; FP load/store rows are deferred
// to the FP regfile pathing.
//
// Each leaf reads pre-resolved operands from ExecCtx (opA = Ra value
// when S_ReadsRa is set, opB = Rb value when S_ReadsRb is set), pulls
// the 16-bit signed displacement from encoded[15:0], computes
// EA = opB + sext_16(disp), and packs the access into BoxResult.  The
// leaf does NO translation, NO memory access, and NO fault delivery;
// those are concerns of the MEM-stage drainer reading the BoxResult.
//
// Encoding shorthands:
//
//     Loads          Stores         Compute-only        Hint
//     -----          ------         ------------        ----
//     memAddr  EA    memAddr  EA    regWriteValue       no effect
//     memSize  N     memSize  N     regWriteIdx Ra
//     memIsStore=F   memIsStore=T
//     regWriteIdx=Ra memData  opA
//
// LDQ_U / STQ_U force EA alignment by clearing the low 3 bits of the
// computed address; the MEM drainer treats the result like any other
// 8-byte access.  LDL_L / LDQ_L tell the drainer (via S_Locked in
// semFlags) to set the per-CPU reservation after the fill lands.
// STL_C / STQ_C tell the drainer (also via S_Locked) to check-and-
// clear the reservation before the store and to write Ra = 1 / 0 per
// the success indicator -- the leaf packs both memEffect AND a
// regWriteIdx for those two opcodes.
//
// Sign extension on loads is the MEM drainer's job, not the leaf's.
// See the BoxResult.h drain-map block for the memSize / regWriteIsFp
// rule.
//
// ============================================================================

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

#include <cstdint>

namespace mBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;
using coreLib::kNoRegWrite;


// ---------------------------------------------------------------------------
// Encoding helpers.
// ---------------------------------------------------------------------------
// Mem-format Alpha encoding:
//   bits[31:26]  primary opcode
//   bits[25:21]  Ra (target on loads, source on stores)
//   bits[20:16]  Rb (EA base)
//   bits[15:0]   16-bit signed displacement
//
// Both helpers are inline-only; they exist to keep the leaf bodies
// from repeating the same casts inline at every call site.

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr int64_t memDispSext(InstructionGrain const& g) noexcept
{
    return static_cast<int64_t>(
        static_cast<int16_t>(g.encoded & 0xFFFFu));
}

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint8_t raIndex(InstructionGrain const& g) noexcept
{
    return static_cast<uint8_t>((g.encoded >> 21) & 0x1Fu);
}


#pragma region Compute-only Mem-format

// ----------------------------------------------------------------------------
// LDA Ra, disp(Rb) -- Ra <- Rb + sext_16(disp).  No memory access.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLda(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = c.opB + static_cast<uint64_t>(memDispSext(g));
    return r;
}

// ----------------------------------------------------------------------------
// LDAH Ra, disp(Rb) -- Ra <- Rb + (sext_16(disp) << 16).  No memory access.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdah(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    const int64_t disp = memDispSext(g) << 16;

    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = c.opB + static_cast<uint64_t>(disp);
    return r;
}

#pragma endregion Compute-only Mem-format


#pragma region Mem-format Loads

// ----------------------------------------------------------------------------
// LDBU Ra, disp(Rb) -- Ra <- zero_extend(Mem[EA]<7:0>); EA = Rb + sext(disp).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdbu(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 1;
    r.memIsStore   = false;
    return r;
}

// ----------------------------------------------------------------------------
// LDWU Ra, disp(Rb) -- Ra <- zero_extend(Mem[EA]<15:0>).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdwu(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 2;
    r.memIsStore   = false;
    return r;
}

// ----------------------------------------------------------------------------
// LDL Ra, disp(Rb) -- Ra <- sign_extend_32(Mem[EA]<31:0>).
// MEM-stage drainer applies the sign extension based on memSize == 4 and
// regWriteIsFp == false (see BoxResult.h drain-map).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 4;
    r.memIsStore   = false;
    return r;
}

// ----------------------------------------------------------------------------
// LDQ Ra, disp(Rb) -- Ra <- Mem[EA]<63:0>.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 8;
    r.memIsStore   = false;
    return r;
}

// ----------------------------------------------------------------------------
// LDQ_U Ra, disp(Rb) -- Ra <- Mem[(Rb + sext(disp)) & ~7]<63:0>.
// EA is force-aligned to a quadword boundary in the leaf; the drainer
// treats the result like any other 8-byte access.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdqU(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = (c.opB + static_cast<uint64_t>(memDispSext(g))) & ~0x7ULL;
    r.memSize      = 8;
    r.memIsStore   = false;
    return r;
}

// ----------------------------------------------------------------------------
// LDL_L Ra, disp(Rb) -- load longword locked.  Drainer sets the
// per-CPU reservation after a successful fill; S_Locked in semFlags
// is the signal.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdlL(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 4;
    r.memIsStore   = false;
    return r;
}

// ----------------------------------------------------------------------------
// LDQ_L Ra, disp(Rb) -- load quadword locked.  Same drainer behaviour
// as LDL_L but 8-byte access; no sign extension.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdqL(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 8;
    r.memIsStore   = false;
    return r;
}

#pragma endregion Mem-format Loads


#pragma region Mem-format Stores

// ----------------------------------------------------------------------------
// STB Ra, disp(Rb) -- Mem[EA]<7:0> <- Ra<7:0>.  No register effect.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStb(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = c.opA;
    r.memSize     = 1;
    r.memIsStore  = true;
    return r;
}

// ----------------------------------------------------------------------------
// STW Ra, disp(Rb) -- Mem[EA]<15:0> <- Ra<15:0>.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStw(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = c.opA;
    r.memSize     = 2;
    r.memIsStore  = true;
    return r;
}

// ----------------------------------------------------------------------------
// STL Ra, disp(Rb) -- Mem[EA]<31:0> <- Ra<31:0>.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStl(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = c.opA;
    r.memSize     = 4;
    r.memIsStore  = true;
    return r;
}

// ----------------------------------------------------------------------------
// STQ Ra, disp(Rb) -- Mem[EA]<63:0> <- Ra.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = c.opA;
    r.memSize     = 8;
    r.memIsStore  = true;
    return r;
}

// ----------------------------------------------------------------------------
// STQ_U Ra, disp(Rb) -- Mem[(Rb + sext(disp)) & ~7] <- Ra.  Like LDQ_U
// the leaf force-aligns EA; drainer behaves like a normal 8-byte store.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStqU(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = (c.opB + static_cast<uint64_t>(memDispSext(g))) & ~0x7ULL;
    r.memData     = c.opA;
    r.memSize     = 8;
    r.memIsStore  = true;
    return r;
}

// ----------------------------------------------------------------------------
// STL_C Ra, disp(Rb) -- store longword conditional.  Two effects:
//   1. memEffect: 4-byte store of Ra at EA, gated by reservation.
//   2. regEffect: Ra <- 1 on success, 0 on lock loss.
//
// The leaf packs BOTH into BoxResult.  S_Locked in semFlags tells the
// drainer to check-and-clear the per-CPU reservation before the
// publish and to overwrite regWriteValue with the success indicator.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStlC(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = 0;   // placeholder; drainer overwrites with 1 or 0
    r.memAddr       = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData       = c.opA;
    r.memSize       = 4;
    r.memIsStore    = true;
    return r;
}

// ----------------------------------------------------------------------------
// STQ_C Ra, disp(Rb) -- store quadword conditional.  Same shape as
// STL_C but 8-byte access.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStqC(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = raIndex(g);
    r.regWriteIsFp  = false;
    r.regWriteValue = 0;
    r.memAddr       = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData       = c.opA;
    r.memSize       = 8;
    r.memIsStore    = true;
    return r;
}

#pragma endregion Mem-format Stores


#pragma region HW-format physical / virtual access (PALmode)

// ----------------------------------------------------------------------------
// HW_LD / HW_ST encoding helpers.
//
// The HW_LD (opcode 0x1B) and HW_ST (opcode 0x1F) instructions use a
// 12-bit signed displacement at encoded[11:0] -- distinct from the
// Mem-format 16-bit disp at encoded[15:0].  They run only in PAL mode
// (S_Privileged) and bypass alignment checks.  Bit[12] selects access
// width: 0 = 4-byte longword, 1 = 8-byte quadword.  Bits[15:13] carry
// type-mode hints (PHYS / ALT / WRT_CHK / VPTE) which we ignore for
// the V4 v1 cut -- in PAL mode the translator already returns PA = VA
// for any kseg-style access, which covers every PHYS path the SRM
// decompressor exercises.  ALT and WRT_CHK semantics layer in later.
// ----------------------------------------------------------------------------
[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr int64_t hwDispSext(InstructionGrain const& g) noexcept
{
    int32_t const signedEnc = static_cast<int32_t>(g.encoded);
    int32_t const disp12    = (signedEnc << 20) >> 20;   // sign-extend 12 bits
    return static_cast<int64_t>(disp12);
}

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint8_t hwQuadSize(InstructionGrain const& g) noexcept
{
    // bit[12] = 1 -> 8-byte quadword, 0 -> 4-byte longword
    return ((g.encoded >> 12) & 0x1u) ? uint8_t{8} : uint8_t{4};
}


// ----------------------------------------------------------------------------
// HW_LD Ra, disp12(Rb) -- PAL hardware load.
// EA = Rb + sext(disp12); memSize per encoded[12]; sign-extension of
// 4-byte loads follows the standard mBox convention applied at MEM
// drain.  No reservation, no alignment trap (HW_LD is permitted to
// take whatever the translator gives it).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execHwLd(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = false;
    r.memAddr      = c.opB + static_cast<uint64_t>(hwDispSext(g));
    r.memSize      = hwQuadSize(g);
    r.memIsStore   = false;
    return r;
}


// ----------------------------------------------------------------------------
// HW_ST Ra, disp12(Rb) -- PAL hardware store.
// EA = Rb + sext(disp12); memSize per encoded[12]; data = Ra.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execHwSt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(hwDispSext(g));
    r.memData     = c.opA;
    r.memSize     = hwQuadSize(g);
    r.memIsStore  = true;
    return r;
}

#pragma endregion HW-format physical / virtual access (PALmode)


#pragma region Misc-format Hint

// ----------------------------------------------------------------------------
// FETCH -- prefetch hint.  No architectural register or memory effect;
// S_PrefetchOnly in semFlags tells the MEM-stage drainer to skip both
// the regfile commit and the GuestMemory access.  Misc-format encoding,
// no displacement, no Ra read.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execFetch(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    return r;
}

#pragma endregion Misc-format Hint

// ============================================================================
// TODO -- additional Misc-format cache hints not yet wired
// ============================================================================
//
// The following four no-op cache hints exist in the Alpha SRM but do
// not yet have rows in GrainMasterV4.tsv, so they are not dispatched
// and not implemented here.  Each is a Misc-format opcode 0x18 sub-
// function; once master TSV rows land they slot in alongside execFetch
// with identical bodies (semFlags propagation, no register effect, no
// memory effect):
//
//   FETCH_M (0x18 / 0xA000)  Mbox  prefetch with modify intent
//   WH64    (0x18 / 0xF800)  Mbox  write hint, 64 bytes
//   WH64EN  (0x18 / 0xFC00)  Mbox  write hint, 64 bytes, evict next
//
// ECB lives in the Cbox dispatch arm (master TSV line 345 already
// classifies it Cbox) and belongs in cBoxLib/grains/ when that
// directory exists; do not implement it here even though it is a
// trivial no-op.
//
// ============================================================================

} // namespace mBox
