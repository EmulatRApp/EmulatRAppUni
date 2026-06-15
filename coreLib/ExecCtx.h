// ============================================================================
// coreLib/ExecCtx.h -- per-EX-call execution context for leaf functions
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
// ExecCtx is the per-EX-call context handed to every leaf function.
// It carries the inputs the leaf needs that are NOT on the grain
// itself: pre-resolved operand values (after bypass-or-regfile read),
// cycle counter, current PAL mode and personality, and a hook back
// to the CPU for the rare side-state accesses (IPRs).
//
// Design contract:
//
//   ExecCtx is read-only from the leaf's perspective for everything
//   that participates in the architectural state machine.  Register
//   writes go through BoxResult.regWriteValue / regWriteIdx; memory
//   effects go through BoxResult.memAddr / memData / memSize;
//   architectural PC redirection goes through BoxResult.divertTarget.
//   The leaf NEVER mutates the regfile or memory directly.
//
//   The one exception is internal processor register access (HW_MFPR
//   and HW_MTPR), which is PAL-mode side state and not part of the
//   architectural commit path.  IPR access goes through the CpuState
//   pointer; the API will land with the CPU class.  For first-wave
//   leaves none of this is exercised.
//
//   opA and opB are populated by the pipeline (GR stage) immediately
//   before the leaf is invoked at EX.  The pipeline consults
//   grain.semFlags to decide what to put there:
//
//     S_ReadsRa  -> opA = readInt/readFp((encoded >> 21) & 0x1F)
//     S_HasLit   -> opB = (encoded >> 13) & 0xFF
//     S_ReadsRb  -> opB = readInt/readFp((encoded >> 16) & 0x1F)
//
//   The leaf reads opA / opB without thinking about whether the value
//   came from the regfile or a bypass forwarding path.
//
// ============================================================================

#ifndef CORELIB_EXECCTX_H
#define CORELIB_EXECCTX_H

#include <cstdint>

// Forward declaration -- ExecCtx carries a non-owning GuestMemory pointer
// for CSERVE-class intrinsics that transfer guest buffers during EX.
// (Full type lives in memoryLib/GuestMemory.h; only consumers that touch
// ctx.memory include it.)
namespace memoryLib { class GuestMemory; }

namespace coreLib {

// Forward declaration; the CPU class will define CpuState in
// coreLib/CpuState.h when it lands.  ExecCtx carries a non-owning
// pointer for the leaves that need IPR access; it is null for
// leaves that do not (the common case).
struct CpuState;


struct ExecCtx
{
    // ------------------------------------------------------------------
    // Pre-resolved operand values.
    // ------------------------------------------------------------------
    // Populated by the pipeline before the leaf is invoked.  The leaf
    // reads these directly; bypass-vs-regfile resolution is the
    // pipeline's concern, not the leaf's.  For instructions reading
    // the FP regfile the bit pattern is delivered as uint64_t; the
    // leaf type-puns it back to double / float as needed.
    uint64_t opA = 0;
    uint64_t opB = 0;

    // ------------------------------------------------------------------
    // Cycle counter.
    // ------------------------------------------------------------------
    // Snapshot of the CPU's cycle counter at the moment of EX call.
    // Read by RPCC and by trace formatters; write by leaves is not
    // permitted (the counter is owned by the CPU).
    uint64_t cycleCount = 0;

    // ------------------------------------------------------------------
    // PAL mode and personality.
    // ------------------------------------------------------------------
    // palMode is true while executing PALcode; false otherwise.  Set
    // by the pipeline based on the CPU's PS register before each EX
    // call.  Used by leaves for personality-conditional behaviour
    // (rare; most leaves are personality-agnostic).
    //
    // palPersonality is 0 for Tru64/OSF, 1 for OpenVMS.  This value
    // is configured at boot from the firmware/PAL image and rarely
    // changes thereafter; the codegen-emitted PAL dispatch tables
    // already key off this value during decode, so leaves typically
    // do not need to consult it.
    bool    palMode        = false;
    uint8_t palPersonality = 0;

    // ------------------------------------------------------------------
    // CPU back-pointer for side-state access.
    // ------------------------------------------------------------------
    // Non-owning pointer; null for leaves that do not need it.  The
    // sole expected consumers are HW_MFPR (IPR read), HW_MTPR (IPR
    // write), and any future leaves that have to reach into PAL-only
    // CPU state.  All architectural register and memory effects go
    // through BoxResult; this pointer is the documented escape hatch
    // for PAL-side state that is not part of the commit path.
    CpuState* cpu = nullptr;

    // ------------------------------------------------------------------
    // Guest memory accessor for CSERVE-class intrinsics.
    // ------------------------------------------------------------------
    // Non-owning; null for the common case.  Set by the pipeline
    // (PipelineDriver::step) for every EX call.  The expected consumers
    // are CSERVE intrinsics (PUTS / GET_ENV / SET_ENV / GETS) that must
    // read or write a guest buffer during EX -- the documented escape
    // hatch alongside the CpuState pointer, mirroring V1's
    // ConsoleManager::readVirtualByteFromPalHandler.  Ordinary
    // architectural memory effects still go through BoxResult.memAddr.
    memoryLib::GuestMemory* memory = nullptr;
};

} // namespace coreLib

#endif // CORELIB_EXECCTX_H
