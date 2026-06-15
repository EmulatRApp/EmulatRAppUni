// ============================================================================
// cBoxLib/grains/CacheOps.cpp -- cBox barrier and cache-hint executors (v1)
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
// Hand-written leaf functions for the cBox dispatch arm.  First-wave
// scope is the five Misc-format opcode 0x18 entries that classify as
// Cbox per GrainMasterV4.tsv:
//
//   TRAPB  (func 0x0000)  serializes precise traps      S_TrapBarrier
//   EXCB   (func 0x0400)  serializes exceptions         S_ExcBarrier
//   MB     (func 0x4000)  full memory barrier           S_Mb
//   WMB    (func 0x4400)  write memory barrier          S_Wmb
//   ECB    (func 0xE800)  evict cache block (hint)      S_PrefetchOnly
//
// Why every body here is a one-line semFlags propagation:
//
//   V4 v1 runs single-core, single-issue, with synchronous memory
//   accesses and no modeled D-cache or I-cache.  Under those
//   conditions the architectural side effect of each barrier or
//   evict-hint is trivially satisfied by the time the leaf runs:
//
//     * TRAPB / EXCB     no out-of-order traps to drain; precise by
//                        construction.
//     * MB / WMB         no reordered memory accesses to fence; loads
//                        and stores already happen in program order
//                        at the MEM-stage drain.
//     * ECB              no cache to evict; the hint has nothing to
//                        target.
//
//   The semantic flag set carries the meaningful information forward
//   to the MEM-stage drainer (and to a future cache subsystem).  Once
//   a cache model lands, MB / WMB read S_Mb / S_Wmb to flush write
//   buffers, ECB reads S_PrefetchOnly to invalidate a line; the leaf
//   bodies here do not change.  TRAPB and EXCB likewise stay this
//   shape -- their role is to serialize a queue that v1 does not
//   maintain.
//
//   ECB has S_ReadsRb in its master TSV row so the pipeline populates
//   c.opB with the EA base; the leaf accepts and ignores it.  When a
//   cache subsystem starts honouring ECB, the EA computation will move
//   here (EA = c.opB; no displacement on Misc-format) and pack memAddr
//   plus S_PrefetchOnly into BoxResult.  Until then opB is unused.
//
// ============================================================================

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

#include <cstdint>

namespace cBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;


#pragma region Trap and Exception Barriers

// ----------------------------------------------------------------------------
// TRAPB -- trap barrier; serialize precise traps from prior in-flight
// instructions.  No-op in v1 (no out-of-order trap queue exists).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execTrapb(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    return r;
}

// ----------------------------------------------------------------------------
// EXCB -- exception barrier; serialize exceptions.  No-op in v1.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execExcb(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    return r;
}

#pragma endregion Trap and Exception Barriers


#pragma region Memory Barriers

// ----------------------------------------------------------------------------
// MB -- full memory barrier; orders all prior loads / stores against
// all subsequent loads / stores.  No-op in v1 (memory accesses are
// already program-ordered at MEM-stage drain).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMb(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    return r;
}

// ----------------------------------------------------------------------------
// WMB -- write memory barrier; orders prior stores against subsequent
// stores.  No-op in v1.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execWmb(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    return r;
}

#pragma endregion Memory Barriers


#pragma region Cache Hints

// ----------------------------------------------------------------------------
// ECB -- evict cache block; hint to evict the cache line containing
// EA = Rb.  Misc-format reads Rb (c.opB) but in v1 no cache is
// modeled, so the hint has nothing to target.  S_PrefetchOnly in
// semFlags will tell a future cache subsystem to invalidate the line
// without taking any architectural register or memory effect.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execEcb(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    return r;
}

#pragma endregion Cache Hints

} // namespace cBox
