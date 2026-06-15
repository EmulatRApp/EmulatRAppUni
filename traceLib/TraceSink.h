// ============================================================================
// traceLib/TraceSink.h -- abstract sink interface for pipeline trace events
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
// TraceSink is the abstract interface PipelineDriver calls at three
// well-defined sites:
//
//   onCommit    : after retire at WB, every retired instruction
//   onPalEntry  : at WB when the just-retired grain carries S_PalEntry
//                 (CALL_PAL leaves -- the dispatcher entered PALcode)
//   onPalExit   : at WB when the just-retired grain carries S_PalExit
//                 (HW_REI -- the dispatcher exited PALcode)
//
// Pure-virtual contract; concrete sinks (DecListingSink and any future
// formatter) override only what they care about.  Default empty
// bodies for the two PAL hooks let simple sinks override only
// onCommit -- the common case for non-windowed trace.
//
// Lifetime: PipelineDriver receives a raw TraceSink* per run; the
// caller (Machine) owns the sink's storage and guarantees it
// outlives the run() call.  Sinks must be safe to call from any
// thread the pipeline runs on (V4 is single-threaded; multi-threading
// is a future concern, not today's).
//
// ============================================================================

#ifndef TRACELIB_TRACESINK_H
#define TRACELIB_TRACESINK_H

#include <cstdint>

#include "traceLib/CommitRecord.h"

namespace coreLib {
struct CpuState;
}

namespace traceLib {

class TraceSink
{
public:
    virtual ~TraceSink() = default;

    // Required: called after every retired instruction.  postCommitCpu
    // reflects the state *after* the regfile commit at MEM and the PC
    // advance at WB -- so cpu.intReg[N] holds the value the just-
    // retired instruction wrote, and cpu.pc points at the next
    // instruction to fetch.
    virtual void onCommit(CommitRecord const&        record,
                          coreLib::CpuState const&   postCommitCpu) = 0;

    // Optional: called when WB retires a grain whose semFlags include
    // S_PalEntry (CALL_PAL).  excAddr is the saved exception address
    // recorded by the trap delivery path; entryPc is the PAL vector
    // address the divert lands at.  Default no-op.
    virtual void onPalEntry(uint64_t cycle,
                            uint64_t entryPc,
                            uint64_t excAddr) { (void)cycle; (void)entryPc; (void)excAddr; }

    // Optional: called when WB retires a grain whose semFlags include
    // S_PalExit (HW_REI).  targetPc is the PC the divert resumes at
    // (cleared of the PAL bit).  Default no-op.
    virtual void onPalExit(uint64_t cycle,
                           uint64_t targetPc) { (void)cycle; (void)targetPc; }

    // Optional: called by the run driver when the run loop exits, for
    // any reason (clean halt, fault, max-cycles).  Concrete sinks use
    // this to dump their lookback ring so the operator can see the
    // last few retired instructions before the stop -- useful in
    // PAL_WINDOW mode where steady-state emission is suppressed.
    // Default no-op.
    virtual void onRunEnd(coreLib::CpuState const& finalCpu) { (void)finalCpu; }

    // ------------------------------------------------------------------
    // Phase C+: external emit-gate for boot-time trace volume control.
    // ------------------------------------------------------------------
    // Machine calls setEmitEnabled(false) at run-start when PAL has not
    // yet been relocated; setEmitEnabled(true) at the moment Step D PAL
    // relocation completes (and at run-start when the snapshot already
    // captures a post-relocation state).  Concrete sinks may short-
    // circuit their per-commit file I/O while disabled; the lookback
    // ring should keep updating so post-gate onPalEntry dumps have
    // valid context.
    //
    // The contract is intentionally minimal -- the base class no-op is
    // correct for sinks that have their own gating (BreakpointSink's
    // paired-PC gate, for example).  Only DecListingSink overrides
    // today; PipelineDriver and BreakpointSink ignore the call.
    //
    // Rationale: pre-relocation SRM init produces ~5x more trace I/O
    // than the interesting post-relocation Phase C+ window, and that
    // I/O is the dominant wall-clock cost on cold-boot runs.  Gating
    // through PAL relocation reduces cold-boot wall-clock by ~5-10x.
    // See Machine::canAcceptInterrupt for the architectural rationale
    // and Phase C+ post-mortem; same m_palImageRelocated signal drives
    // both interrupt-arbitration and trace-emit gating.
    virtual void setEmitEnabled(bool enabled) noexcept { (void)enabled; }
};

} // namespace traceLib

#endif // TRACELIB_TRACESINK_H
