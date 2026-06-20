// ============================================================================
// traceLib/BreakpointSink.h -- gated full-complement retire trace sink
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
// BreakpointSink is a TraceSink that captures one (or N) full revolutions
// of a target PALcode region at maximum fidelity, bracketed by a pair of
// debugger-pokeable PC gates.  Built to answer "what happens inside one
// pass of the sys__cbox outer loop" without slowing the rest of the run
// or perturbing the existing retire-compact stream.
//
// Trigger model
// -------------
// Two gates, each an armed PC:
//
//   s_gateOpenPc  -- when a retire's PC equals this, the capture window
//                    opens.  PAL_TEMP and full IPR state are snapshotted
//                    once at this transition.
//
//   s_gateClosePc -- after the window is open, the next retire whose PC
//                    equals this closes the window.  PAL_TEMP and IPR
//                    state are snapshotted again at this transition.
//
// One open-then-close pair is "one revolution".  s_revolutionsRemaining
// is the configured count of revolutions to capture; the sink stays armed
// across open/close cycles until that counter reaches zero, then disarms
// itself.  Default initial value is 1 (capture exactly one revolution).
//
// All three control atomics are static and debugger-pokeable in the same
// posture as DecListingSink::s_traceWindowCountdown -- write to them from
// the VS Immediate window (or from a launching wrapper) to arm and to
// configure the revolution count before run() is invoked.  Reading them
// post-run reports any leftover state.
//
// For the 2026-05-12 SRM trace investigation the configured pair is:
//   s_gateOpenPc  = 0x00000000000126f0  (sys__cbox_over8 BEQ -- the
//                                        instruction that falls through into
//                                        the cbox_done epilogue.  Capturing
//                                        from here makes the retire at 0x12700
//                                        visible as a BRK record so the
//                                        encoded word and the produced R4
//                                        value are both in the trace.  Earlier
//                                        passes used 0x12700 as the gate but
//                                        that hid the loader's effect on R4
//                                        inside the boundary snapshot.)
//   s_gateClosePc = 0x0000000000012040  (re-issue of BSR, one revolution later)
//   s_revolutionsRemaining = 1
//
// Output file
// -----------
// Sibling to the retire-compact stream, written to the same directory
// (default X:\traces) with name YYYYMMDD-HHMMSS_break.trc.  Stamp is
// captured at sink construction so concurrent or repeat runs do not
// collide.  The stream is opened lazily on the first gate-open event;
// if no gate ever fires the file is never created.  Closed in onRunEnd
// (and the destructor as belt-and-braces).
//
// Record format (each line is self-describing)
// --------------------------------------------
//   #-prefixed header records at file open describing format and gates.
//
//   BP_OPEN  rpcc=<n> rev=<n> pc=<hex16>
//     Gate-open transition.  Immediately followed by IPR_SNAP and PT_SNAP.
//
//   BP_CLOSE rpcc=<n> rev=<n> pc=<hex16>
//     Gate-close transition.  Immediately followed by IPR_SNAP and PT_SNAP.
//
//   IPR_SNAP rpcc=<n> kind=<open|close>
//     palBase=H ptbr=H asn=H va_ctl=H i_ctl=H m_ctl=H i_spe=H m_spe=H
//     mode=N palMode=N cycleCount=H ccOffset=H
//     intrFlag=H mm_stat=H excAddr=H vptb=H scbb=H pcbb=H
//     ksp=H esp=H ssp=H usp=H fen=H asten_sr=H
//     (one record per gate transition; gives the IPR file in one record)
//
//   PT_SNAP  rpcc=<n> kind=<open|close>
//     PT00=H PT01=H ... PT31=H
//     (one record per gate transition; PAL_TEMP[0..31] in hex)
//
//   CBX_SNAP rpcc=<n> kind=<open|close>
//     <CBoxState fields as key=value, one record per gate transition>
//     (shape determined from coreLib/CBoxState.h at .cpp time; placeholder
//     for now -- the open/close snapshot pair carries the Cbox shift-chain
//     state so the diff reveals what the SRM read out of the chain.)
//
//   BRK rpcc=<n> pc=<hex16> encoded=<hex8> <mnem> pal=<0|1> exc=<hex16>
//     R00=H R01=H ... R30=H
//     [ F00=H F01=H ... F30=H ]
//     [ ea=H val=H ]              -- when HW_LD or HW_ST retired
//     [ iprid=H iprval=H ]        -- when HW_MFPR or HW_MTPR retired
//
//   The encoded= field is the raw 32-bit instruction word from
//   CommitRecord.encoded; carrying it inline lets a post-mortem reader
//   decode the exact sub-function of HW_xx instructions (which share
//   opcode 0x1E and disambiguate on bits 15:12) without consulting an
//   external assembler dump.
//
//     Per-retire record while the window is open.  Full GPR complement is
//     always emitted (R31 omitted -- architecturally zero).  FPR block is
//     omitted entirely when every F-reg is zero (boot/SRM path is FP-free
//     by design -- keeps the typical record at ~640 bytes instead of
//     ~1280).  ea/val suffix appears when the just-retired grain produced
//     a memory effect; iprid/iprval suffix appears when the mnemonic
//     matches HW_MFPR or HW_MTPR.
//
// Per-retire records are emitted only while the window is open.  Between
// revolutions (when the gate has closed but more revolutions are armed)
// the sink is silent.
//
// Downstream chain
// ----------------
// Optional downstream TraceSink* passed to the ctor.  If non-null, every
// onCommit / onPalEntry / onPalExit / onRunEnd call is forwarded to the
// downstream BEFORE the BreakpointSink's own logic runs.  This lets a
// caller chain DecListingSink + BreakpointSink without modifying the
// PipelineDriver's single-sink interface.  When the chain is nullptr the
// sink runs standalone.
//
// Volume
// ------
// One revolution of sys__cbox at the 2026-05-12 PCs is roughly 220 retires
// (prologue + 11 shift iterations + epilogue).  At ~640 bytes per BRK
// line that is ~140 KB per revolution plus four ~1-KB snapshot records.
// Capturing N=10 revolutions produces ~1.4 MB total -- trivially small.
//
// Threading
// ---------
// V4 v1 is single-threaded; the sink does no internal locking.  The static
// atomics are atomic only so that a debugger writing to them via the
// process's address space sees consistent values, not for thread safety
// against a multi-issue retire engine.
//
// ============================================================================

#ifndef TRACELIB_BREAKPOINTSINK_H
#define TRACELIB_BREAKPOINTSINK_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "traceLib/TraceSink.h"

namespace coreLib {
struct CpuState;
struct BoxResult;
}

namespace traceLib {

// ---------------------------------------------------------------------------
// Defaults for the 2026-05-28 MCHK return-path investigation.
// ---------------------------------------------------------------------------
// Captures the SROM-resident PAL MCHK handler from the post-sys__cbox
// decision block through the HW_REI that lands PC at 0.  Open at the
// first MTPR I_CTL retired after sys__cbox returns (0xd841, PAL mode);
// close at the HW_REI Rb=2 instruction (0xd955).  Single revolution =
// one MCHK delivery, captured at full retire fidelity so the R2-source
// decision logic is readable from the BRK records.
//
// Previous defaults (May-12 sys__cbox investigation, both retired):
//   s_gateOpenPc  = 0x00000000000126f0
//   s_gateClosePc = 0x0000000000012040
// Override before run() via the static setters or direct atomic writes
// from the debugger.
constexpr uint64_t BP_DEFAULT_GATE_OPEN_PC  = 0x000000000000d841ULL;
constexpr uint64_t BP_DEFAULT_GATE_CLOSE_PC = 0x000000000000d955ULL;
constexpr uint32_t BP_DEFAULT_REVOLUTIONS   = 1;


class BreakpointSink final : public TraceSink
{
public:
    // Construct against an output directory and an optional downstream
    // sink.  The output directory is the directory the break file is
    // created in (file naming and lazy-open semantics described in the
    // file-level comment).  Empty path falls back to env EMULATR_TRACE_DIR
    // then to X:\traces, matching DecListingSink's resolution order.
    //
    // downstream is forwarded every TraceSink call so callers can chain
    // DecListingSink + BreakpointSink without modifying PipelineDriver.
    // Pass nullptr to run the sink standalone.  Ownership is not taken;
    // downstream must outlive this sink (which Machine already arranges).
    explicit BreakpointSink(std::filesystem::path const& outputDir = {},
                            TraceSink*                   downstream = nullptr);

    // Flush and close the break file if it was opened.  Forwards to
    // downstream first if present.
    ~BreakpointSink() override;

    // Hot path: forwards to downstream, then runs gate logic.  Emits a
    // BRK record only while the gate is open.  Emits the open/close
    // snapshot triplet (BP_OPEN+IPR_SNAP+PT_SNAP+CBX_SNAP, or the close
    // equivalents) on transitions, lazily opening the file on the first
    // BP_OPEN.
    void onCommit(CommitRecord const&        record,
                  coreLib::CpuState const&   postCommitCpu) override;

    // PAL transition forwarding.  The sink itself takes no action on
    // these -- the gate logic operates on per-retire PC, not on PAL
    // transitions -- but the downstream may care.
    void onPalEntry(uint64_t cycle,
                    uint64_t entryPc,
                    uint64_t excAddr) override;
    void onPalExit(uint64_t cycle,
                   uint64_t targetPc) override;

    // Run-end forwarding plus flush/close of the break file.  Emits a
    // RUN_END marker into the break file when it is open so the
    // post-mortem reader knows the capture terminated cleanly (vs being
    // truncated mid-revolution by a crash).
    void onRunEnd(coreLib::CpuState const& finalCpu) override;

    // ------------------------------------------------------------------
    // Phase C+: external emit-gate forwarding.
    // ------------------------------------------------------------------
    // BreakpointSink is a chain wrap of a downstream TraceSink (usually
    // DecListingSink).  When Machine calls setEmitEnabled on the outer
    // sink, we forward the call through to m_downstream so the inner
    // sink can gate its per-commit I/O paths.  BreakpointSink itself
    // does NOT honour the gate -- its emission is already controlled by
    // the paired-PC gate (gateOpenPc / gateClosePc), and its capture
    // window is always post-relocation by construction.  Forwarding-
    // only override mirrors the pattern used by onPalEntry / onPalExit
    // which BreakpointSink also passes through without acting on.
    void setEmitEnabled(bool enabled) noexcept override;

    // ------------------------------------------------------------------
    // Debugger-pokeable control surface.
    // ------------------------------------------------------------------
    // Same posture as DecListingSink::s_traceWindowCountdown -- write to
    // these atomics from the VS Immediate window (or from a tooling
    // wrapper) to configure the gate pair and the revolution count
    // before invoking PipelineDriver::run().  Reading them after the run
    // reports whether all configured revolutions completed.
    //
    // s_gateOpenPc / s_gateClosePc each carry one armed PC.  Set to zero
    // to disarm that gate (a zero PC never matches because PC 0 is the
    // VBR / reset vector and is never retired in steady state).
    //
    // s_revolutionsRemaining is decremented each time a gate-close
    // transition completes; the sink ceases to react when it reaches
    // zero.  s_revolutionsCaptured is the running tally for inspection.
    //
    // s_gateOpen reflects whether the window is currently open.  Read-
    // only from outside the sink; the sink owns the writes.
    static std::atomic<uint64_t> s_gateOpenPc;
    static std::atomic<uint64_t> s_gateClosePc;
    static std::atomic<int32_t>  s_revolutionsRemaining;
    static std::atomic<uint32_t> s_revolutionsCaptured;
    static std::atomic<bool>     s_gateOpen;

    // ------------------------------------------------------------------
    // Optional debugger-break triggers at gate transitions.
    // ------------------------------------------------------------------
    // When s_breakOnGateOpen is true, the gate-open transition calls
    // __debugbreak() AFTER the snapshot triplet has been emitted to
    // _break.trc.  An attached debugger pops to the current frame inside
    // BreakpointSink::processCommit; walking one frame up into
    // PipelineDriver::step exposes `slot`, `cpu`, `record` for live
    // inspection.  The break record is already on disk by the time the
    // break fires, so killing the process at the prompt does not lose
    // capture data.
    //
    // s_breakOnGateClose is the symmetric trigger at the close transition.
    //
    // Both default false.  Set true ONLY when running under an attached
    // debugger (VS F5 or windbg).  Calling __debugbreak() without a
    // debugger pops the OS JIT-debugger prompt; on a headless build that
    // is a process-stopping event.  Standard guidance: set these in the
    // VS Immediate window before the run starts, never bake true into
    // a shipping binary.
    //
    // Implementation detail: __debugbreak() is MSVC-specific.  On other
    // toolchains the .cpp falls back to a stderr message and continues
    // without breaking (the file capture still happens normally).
    static std::atomic<bool>     s_breakOnGateOpen;
    static std::atomic<bool>     s_breakOnGateClose;

    // Convenience setters -- equivalent to direct atomic stores but
    // shorter to type at a debugger prompt.
    static void setGateOpenPc(uint64_t pc) noexcept
    {
        s_gateOpenPc.store(pc, std::memory_order_release);
    }
    static void setGateClosePc(uint64_t pc) noexcept
    {
        s_gateClosePc.store(pc, std::memory_order_release);
    }
    static void setRevolutionsRemaining(int32_t n) noexcept
    {
        s_revolutionsRemaining.store(n, std::memory_order_release);
    }
    static void setBreakOnGateOpen(bool on) noexcept
    {
        s_breakOnGateOpen.store(on, std::memory_order_release);
    }
    static void setBreakOnGateClose(bool on) noexcept
    {
        s_breakOnGateClose.store(on, std::memory_order_release);
    }

    // ------------------------------------------------------------------
    // Independent multi-PC checkpoint ledger (2026-06-03).
    // ------------------------------------------------------------------
    // Orthogonal to the open/close gate above -- arms up to
    // kMaxCheckpoints standalone "tripwire" PCs.  Every retire whose PC
    // matches an armed checkpoint is recorded once (first-hit cycle plus
    // a one-shot full GPR snapshot) and tallied (hit count + last-hit
    // cycle).  At onRunEnd the sink reports, to stderr AND to the break
    // file, which checkpoints were reached and which was reached LAST
    // (largest first-hit cycle).
    //
    // Purpose: collapse a multi-hypothesis "which PC is the last one the
    // run reaches" question into a SINGLE run instead of one run per PC.
    // The first-hit GPR snapshot captures, e.g., R0 at an isatty() call
    // site for free.  A checkpoint PC of 0 is disarmed.  Independent of
    // the revolution budget, so checkpoints keep firing after the gate
    // window's revolutions are exhausted.
    //
    // Same debugger-pokeable posture as the gate atomics; also settable
    // from a launcher via setCheckpoint(), or the EMULATR_CHECKPOINTS
    // env parse in main.cpp.
    static constexpr int kMaxCheckpoints = 8;
    static std::array<std::atomic<uint64_t>, kMaxCheckpoints> s_checkpointPc;
    static char s_checkpointLabel[kMaxCheckpoints][24];

    // Arm checkpoint slot idx [0..kMaxCheckpoints) at PC pc with an
    // optional short label (copied; truncated to 23 chars).  pc==0
    // disarms the slot.  Out-of-range idx is ignored.
    static void setCheckpoint(int idx, uint64_t pc, char const* label) noexcept;

    // Read-only inspection helper.  Returns true when at least one gate
    // is armed AND the revolution counter has not exhausted.
    static bool armed() noexcept;


private:
    // Configuration captured at construction.
    std::filesystem::path m_outputDir;
    TraceSink*            m_downstream;       // not owned

    // Lazy file state -- the break file is created on the first
    // gate-open event, not at sink construction, so a run that never
    // hits a gate produces no file.
    std::ofstream         m_breakLog;
    bool                  m_breakOpen   = false;
    std::filesystem::path m_breakPath;        // resolved at first open
    std::string           m_stamp;            // YYYYMMDD-HHMMSS

    // Wall-clock anchor at construction.  Carried into the break file
    // header so the file is self-describing about which run produced it.
    std::chrono::steady_clock::time_point m_startTime;

    // Forward to downstream first, then run gate logic.  Called from
    // onCommit.  Returns true when a BRK record was emitted on this
    // retire (currently informational; not consumed by callers).
    bool processCommit(CommitRecord const&        record,
                       coreLib::CpuState const&   postCommitCpu);

    // Lazy open of the break file.  Resolves the directory (m_outputDir,
    // env EMULATR_TRACE_DIR, X:\traces), composes the filename from
    // m_stamp, opens, writes the descriptive header records, sets
    // m_breakOpen.  Idempotent -- safe to call after the file is open.
    void ensureBreakOpen();

    // Emit one BRK record for the just-retired instruction.  Full GPR
    // complement always; FPR block when any F-reg is non-zero; ea/val
    // suffix when the BoxResult carries a memory effect; iprid/iprval
    // suffix when the mnemonic is HW_MFPR or HW_MTPR.
    void emitBrkRecord(CommitRecord const&        record,
                       coreLib::CpuState const&   postCommitCpu);

    // Emit the open/close snapshot triplet (BP_*, IPR_SNAP, PT_SNAP,
    // CBX_SNAP).  kind is "open" or "close"; both shapes share this
    // helper because the record content is identical -- only the leading
    // BP_OPEN / BP_CLOSE marker differs.
    void emitGateSnapshot(uint64_t                 cycle,
                          uint64_t                 pc,
                          uint32_t                 revolution,
                          char const*              kind,
                          coreLib::CpuState const& postCommitCpu);

    // Compose the timestamp string used in the filename and in the file
    // header.  Captured at construction; deterministic per sink instance.
    static std::string composeStamp();

    // ------------------------------------------------------------------
    // Checkpoint ledger storage (one entry per checkpoint slot).
    // Sink-owned and non-atomic: V4 v1 is single-threaded, so the retire
    // path is the sole writer.  The static s_checkpointPc array carries
    // the armed PCs (debugger-pokeable); this per-instance ledger carries
    // the observed results.
    // ------------------------------------------------------------------
    struct CheckpointLedger {
        bool     hit        = false;
        uint64_t firstCycle = 0;
        uint64_t lastCycle  = 0;
        uint64_t hitCount   = 0;
        uint64_t snapInt[31] = {};   // R0..R30 at first hit (R31 == 0)
        uint64_t snapCc      = 0;    // cycleCount at first hit
    };
    CheckpointLedger m_ckpt[kMaxCheckpoints];

    // Per-retire checkpoint test.  Called from onCommit independently of
    // the gate revolution budget.  On a first-hit match, snapshots the
    // GPR file and emits a CKPT record (lazily opening the break file).
    void recordCheckpoint(CommitRecord const&        record,
                          coreLib::CpuState const&   postCommitCpu);

    // Emit the CKPT_SUMMARY block (file + stderr) at run end: per-slot
    // hit/first/last/count + R0, and the decisive "LAST checkpoint
    // reached" line.  No-op when no checkpoint slot is armed.
    void emitCheckpointSummary(coreLib::CpuState const& finalCpu);
};

} // namespace traceLib

#endif // TRACELIB_BREAKPOINTSINK_H
