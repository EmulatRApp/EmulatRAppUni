// ============================================================================
// coreLib/PipelineSlot.h -- per-cycle pipeline carrier for EmulatR V4
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
// PipelineSlot is the per-cycle carrier that holds an in-flight
// instruction as it traverses pipeline stages.  A slot is composed of
// the decoded grain (immutable from DE onward), the BoxResult bag
// (populated at EX, drained at MEM and WB), stage tracking, and a
// small amount of trace metadata.
//
// Design contract:
//
//   The slot is the pipeline's unit of state.  IF mints a slot from
//   a fetched instruction word; DE populates grain via the dispatch
//   tables; GR resolves operands into ExecCtx (off-slot, repopulated
//   per call); EX invokes grain.execFn and stores the returned
//   BoxResult on the slot; MEM applies memEffect AND commits the
//   regfile write (V3 profiled this drain placement to save cycles
//   versus a WB-stage commit, and it matches 21264 stage 6 silicon);
//   WB retires the slot, advances the architectural PC, and delivers
//   any faults raised at MEM.
//
//   The slot does NOT carry operand values.  Pre-resolved operands
//   live on ExecCtx, which the pipeline rebuilds per EX call.  This
//   keeps the slot tightly sized and avoids stale-operand questions
//   when bypass resolution changes between cycles.
//
//   The slot does NOT carry the architectural PC of the CPU.  It
//   carries grain.pc (the address this instruction was fetched from);
//   the CPU's architectural PC is a register, mutated only at WB.
//
//   Stage transitions are managed by the pipeline driver, not by
//   methods on the slot.  The Stage field is informational -- useful
//   for trace formatting and assertion checks -- but the source of
//   truth for "which stage owns this slot" is the slot's position in
//   the pipeline driver's stage array.
//
// Sizing:
//   Slot composes grain (32B) + result (~48B) + stage tracking (~16B)
//   for a working set in the neighbourhood of 96-100 bytes.  Larger
//   than one cache line; that is acceptable given that a slot is
//   touched by the same stage code repeatedly across multiple cycles
//   of pipeline traversal.
//
// ============================================================================

#ifndef CORELIB_PIPELINESLOT_H
#define CORELIB_PIPELINESLOT_H

#include <cstdint>

#include "InstructionGrain.h"
#include "coreLib/BoxResult.h"
#include "coreLib/InstructionGrain.h"

namespace coreLib {

// Pipeline stage identifier.  Empty / Retired / Squashed are
// non-stage states that mark a slot's lifecycle outside the active
// pipeline; the six middle values are the in-order stage sequence.
enum class Stage : uint8_t {
    Empty,      // unused slot; no instruction in flight
    IF,         // instruction fetch
    DE,         // decode and dispatch resolution
    GR,         // grain ready; operand read / bypass forwarding
    EX,         // execute; leaf invoked, BoxResult produced
    MEM,        // memory effect application
    WB,         // writeback; regfile commit, PC advance, fault delivery
    Retired,    // post-WB; slot is reclaimed at the next IF mint
    Squashed,   // killed by a divert; will not retire
};


// Returns the canonical name of a Stage value (for trace formatting).
constexpr char const* stageName(Stage s)
{
    switch (s) {
        case Stage::Empty:    return "Empty";
        case Stage::IF:       return "IF";
        case Stage::DE:       return "DE";
        case Stage::GR:       return "GR";
        case Stage::EX:       return "EX";
        case Stage::MEM:      return "MEM";
        case Stage::WB:       return "WB";
        case Stage::Retired:  return "Retired";
        case Stage::Squashed: return "Squashed";
    }
    return "<invalid>";
}


struct PipelineSlot
{
    // ------------------------------------------------------------------
    // Decoded instruction (grain) and per-execution effect bag (result)
    // ------------------------------------------------------------------
    // grain is set at DE and immutable thereafter through retirement.
    // result is populated by the leaf at EX; MEM and WB drain it.
    InstructionGrain grain;
    BoxResult        result;

    // ------------------------------------------------------------------
    // Stage tracking and trace metadata
    // ------------------------------------------------------------------

    // Which stage currently holds this slot.  Informational; the
    // pipeline driver's stage array is the source of truth.
    Stage stage = Stage::Empty;

    // Cycle the slot was minted at IF.  Used by trace formatters to
    // correlate slot-level events across the cycle log; also useful
    // for diagnosing pipeline stalls (a slot stuck in one stage for
    // many cycles tells us where the bubble is).
    uint64_t cycleIssued = 0;

    // Monotonic slot index assigned at IF.  Distinct from cycleIssued
    // so that trace tooling can refer to a specific slot independent
    // of the cycle on which it was issued (multiple slots may be
    // minted in the same cycle once we move past single-issue v1).
    uint32_t slotIdx = 0;

    // True if the slot holds a valid in-flight instruction.  False
    // for empty pipeline positions and for squashed slots that have
    // been killed by a divert and are awaiting reclamation.
    bool valid = false;
};

} // namespace coreLib

#endif // CORELIB_PIPELINESLOT_H
