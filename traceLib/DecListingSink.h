// ============================================================================
// traceLib/DecListingSink.h -- DEC-style listing trace sink for V4
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
// Concrete TraceSink that emits two parallel channels per retired
// instruction:
//
//   1. DEC listing channel  (human-readable, diff-against-reference):
//      "cNN cycle pc encoded mnem  operands  result"   (cNN = SMP cpu slot)
//      e.g. "c00 00000007 0000000000000008 47e30403 ADDQ  R01, R02, R03  R03 = 0x0000000000000052"
//
//   2. Machine-parsable channel (script-friendly, key=value):
//      "INS cpu=N rpcc=N pc=H instr=H mnem=X ops=\"...\" result=\"...\""
//
// When TRACE_REGFILE is set, every INS is followed by a REG line
// containing all 32 integer registers; TRACE_FPRFILE adds an FRG line
// for FP.  Lines emit in the order INS / REG / FRG.
//
// PAL window auto-on:  trace mask TRACE_PAL_WINDOW disables the
// per-commit emit but enables a 16-deep silent lookback ring.  When
// onPalEntry fires, the sink dumps the last 10 entries from the ring
// into the trace channels and goes loud.  When onPalExit fires, the
// sink keeps emitting for an additional POST_PAL_TAIL (10) commits
// before returning silent.  This matches V1's cpuTrace shape and is
// the diagnostic wedge that makes early-boot debugging tractable.
//
// Synchronous emission: every line ends with std::ofstream::flush() so
// crashes do not lose recent context.  No third-party logger; v1
// trades raw throughput for "the trace file is always current".
//
// Retire-compact diagnostic stream (TRACE_RETIRE_COMPACT, bit 0x80):
//   One-line-per-retire stream for hang/loop diagnosis.  Goes to a
//   third ofstream separate from the dec/machine logs, opened by
//   the ctor when the bit is set in the mask.  Default location is
//   X:\traces (2 TB reserve drive).  Filename is YYYYMMDD-HHMMSS_srm.trc,
//   captured at sink-open time so concurrent or repeat runs do not
//   collide.
//
//   Line format:
//     RET cpu=<n> rpcc=<n> pc=<hex16> <mnem> pal=<0|1> exc=<hex16>[ R<dd>=<hex16>]*
//          [ sde=<1-3>][ H<dd>=<hex16>]*
//
//   <mnem> is the codegen-emitted instruction mnemonic literal,
//   left-padded to 8 characters for column alignment.  "?" if the
//   leaf omitted it.
//
//   Elision rule (the consumer must know this):
//     - Registers whose value is 0 are OMITTED from the line.
//     - "Missing on this line" therefore means "zero on this cycle".
//     - R31 is omitted always (architecturally hardwired to zero).
//     - F-registers are omitted entirely (boot/SRM path is FP-free).
//     - PAL shadow bank: H<dd> is the shadow register for arch reg <dd>
//       (H04..H07 shadow R4-R7, H20..H23 shadow R20-R23 = the EV6 SDE<1>
//       set, intShadow[0..7]).  Omitted when zero, like the GPRs.  `sde`
//       is I_CTL<7:6> (SDE<1:0>), emitted only when non-zero so its first
//       appearance marks the cycle PAL shadowing was enabled.  Together
//       they let a reader confirm the PAL-mode shadow swap fires on entry
//       and exit (H<dd> and R<dd> exchange across the pal boundary).
//
//   IPL is intentionally not in this format.  V4's CpuState does not
//   yet carry an architectural IPL field (V2-POC-shallow scope); when
//   IPL lands the line gains an `ipl=<hex2>` field after `pal=`.
//
//   Each line is self-describing -- no replay required to read mid-file.
//   Sync ofstream, retirement order preserved.
//
//   Debugger-pokeable peek: setTraceWindowCountdown(N), or deposit
//   directly into DecListingSink::s_traceWindowCountdown via the VS
//   Immediate window, emits the next N retire lines regardless of mask.
//   Counter decrements per emitted line and self-disables at zero.
//   Use this for mid-run trace peek without rebuilding.  This is the
//   debugger-pokeable counter the project journal flagged as a TODO.
//
// ============================================================================

#ifndef TRACELIB_DECLISTINGSINK_H
#define TRACELIB_DECLISTINGSINK_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "traceLib/TraceSink.h"

namespace traceLib {

// Trace mask bits (mirrors V1's cpuTrace constants).
constexpr uint32_t TRACE_INSTR          = 0x00000001;
constexpr uint32_t TRACE_REGFILE        = 0x00000010;  // full int regfile per insn
constexpr uint32_t TRACE_FPRFILE        = 0x00000020;  // full FP regfile per insn
constexpr uint32_t TRACE_PAL_WINDOW     = 0x00000040;  // silent except in PAL windows
constexpr uint32_t TRACE_RETIRE_COMPACT = 0x00000080;  // one-line retire stream, non-zero regs only
constexpr uint32_t TRACE_ALL            = 0xFFFFFFFFu;
constexpr uint32_t TRACE_NONE           = 0x00000000;


// Lookback ring size and pre/post-PAL window depths.
constexpr uint32_t LOOKBACK_SIZE  = 16;
constexpr uint32_t LOOKBACK_DUMP  = 10;
constexpr uint32_t POST_PAL_TAIL  = 10;

// Heartbeat: emit a "still alive" marker every N retired instructions,
// independent of the trace mask.  Each heartbeat now also dumps the last
// LOOKBACK_DUMP (10) retired instructions from the lookback ring -- the
// same machinery onPalEntry / onRunEnd use.  This trades single-line
// snapshots for richer context per heartbeat: ~11 lines per fire (1
// summary + 10 lookback), so total log volume per million cycles is
// roughly unchanged from when this was a single-line print at 100K.
//
// Recommended HEARTBEAT_INTERVAL values:
//    100000  -- tight debug, suspected wedge or hang
//   1000000  -- normal interactive debug (current default)
//   5000000  -- background / overnight runs
//
// At ~20K cps the 1M-cycle interval surfaces progress every ~50s.
constexpr uint64_t HEARTBEAT_INTERVAL = 1000000;


// One frozen commit record kept in the lookback ring.  Strings stored
// by value rather than by pointer so the ring outlives its source
// CommitRecord.
struct LookbackEntry
{
    uint64_t    cycle      = 0;
    uint64_t    pc         = 0;
    uint32_t    encoded    = 0;
    uint32_t    cpuId      = 0;   // SMP slot (CpuState.cpuSlot) of the retiring
                                  // CPU, captured at freeze.  Default 0 keeps
                                  // default-constructed ring slots benign --
                                  // they are .valid-gated and never emitted.
    std::string mnemonic;       // copy of the codegen literal
    std::string operands;       // pre-rendered Disassembler output
    std::string result;         // pre-rendered formatResult output
    bool        valid      = false;
};


class DecListingSink final : public TraceSink
{
public:
    // Construct from two filesystem paths (DEC channel + machine channel)
    // and a trace mask.  Either path may be empty -- that channel is
    // disabled.  Trace mask zero is allowed (no per-commit emission)
    // but is only useful with TRACE_PAL_WINDOW set.
    DecListingSink(std::filesystem::path const& decLogPath,
                   std::filesystem::path const& machineLogPath,
                   uint32_t                     traceMask);

    // Drains any buffered output via flush() -- belt-and-braces in
    // case the destructor runs after a fault.
    ~DecListingSink() override;

    // Hot path: every retired instruction.
    void onCommit(CommitRecord const&        record,
                  coreLib::CpuState const&   postCommitCpu) override;

    // PAL transition hooks: feed the window machinery.
    void onPalEntry(uint64_t cycle,
                    uint64_t entryPc,
                    uint64_t excAddr) override;
    void onPalExit(uint64_t cycle,
                   uint64_t targetPc) override;

    // Run-end hook: dumps the last LOOKBACK_DUMP entries from the
    // ring with a "RUN END" marker so the post-mortem trace shows
    // what was retired immediately before the stop.  Critical when
    // PAL_WINDOW mode is gating emission and the run halts on a
    // non-PAL fault (kFaultUnimplemented at a leaf, OPCDEC, etc).
    void onRunEnd(coreLib::CpuState const& finalCpu) override;

    // ------------------------------------------------------------------
    // Phase C+: external emit-gate.  See TraceSink::setEmitEnabled for
    // rationale.  When disabled, onCommit() still freezes the record
    // and updates the lookback ring (in-memory, cheap) but skips the
    // three I/O paths (emitCommit / emitRetireCompact / Heartbeat).
    // PAL transition hooks (onPalEntry / onPalExit / onRunEnd) are NOT
    // gated -- they emit small one-time markers important even during
    // the pre-relocation window.
    // ------------------------------------------------------------------
    void setEmitEnabled(bool enabled) noexcept override;

    // Force buffered output to disk; sink callers may invoke at run-end
    // or on a signal handler before terminating.
    void flush();

    // ------------------------------------------------------------------
    // Debugger-pokeable mid-run trace peek.
    // ------------------------------------------------------------------
    // Set this to N from the VS Immediate window (or call
    // setTraceWindowCountdown(N)) to emit the next N retire-compact
    // lines regardless of mask.  Decrements toward zero and self-
    // disables at zero.  Atomic so concurrent peek requests do not
    // race on a multi-threaded driver (V4 is single-threaded today
    // but the contract is forward-compatible).
    static std::atomic<int64_t> s_traceWindowCountdown;
    static void setTraceWindowCountdown(int64_t n) noexcept
    {
        s_traceWindowCountdown.store(n, std::memory_order_release);
    }
    static bool traceWindowActive() noexcept
    {
        return s_traceWindowCountdown.load(std::memory_order_relaxed) > 0;
    }


private:
    // Configuration captured at construction.
    uint32_t      m_traceMask;
    std::ofstream m_decLog;          // DEC listing channel (may be closed)
    std::ofstream m_machineLog;      // machine-parsable channel
    std::ofstream m_retireLog;       // retire-compact channel (X:\traces, may be closed)
    bool          m_decOpen     = false;
    bool          m_machineOpen = false;
    bool          m_retireOpen  = false;

    // Lookback ring -- fixed size, head is monotonically incrementing.
    std::array<LookbackEntry, LOOKBACK_SIZE> m_lookback;
    uint64_t                                 m_lookbackHead = 0;

    // PAL window state.
    bool      m_inPalWindow      = false;
    uint32_t  m_postExitCountdown = 0;

    // Phase C+: external emit-gate.  Default false (gated/silent) so
    // a cold-boot start emits nothing until Machine flips it true at
    // PAL relocation completion.  Machine::run also syncs this at
    // run-start so snapshot-resumed runs (which already have PAL
    // resident) emit from cycle 1 of the resumed run.
    //
    // 2026-05-18 evening: temporarily defaulted to TRUE for a forensic
    // cold-boot capture of the cyc 0..4.19M window before the SRM
    // firmware JSRs to PC=0x6005c0.  Revert to `false` once we have
    // diagnosed the Step D source-starvation issue.  See
    // project_srmloader_axpbox_model.md "Step D source-starvation
    // issue exposed" section.
    bool      m_emitEnabled      = true;   // FORENSIC: trace from cyc 0

    // Wall-clock anchor for the heartbeat elapsed-time field.
    // Captured at construction; every heartbeat reports the delta
    // from this point in milliseconds plus a cycles-per-second
    // derived rate.  Lets long quiet runs show throughput in the
    // log, useful for distinguishing a stalled emulator from a
    // slow but progressing one.
    std::chrono::steady_clock::time_point m_startTime;

    // Emit one CommitRecord (already-rendered into LookbackEntry).
    // Always writes both channels (DEC + machine) when their streams
    // are open.  Optionally appends REG / FRG lines per trace mask.
    void emitCommit(LookbackEntry const&     entry,
                    coreLib::CpuState const* postCommitCpu);

    // Write the REG line (32 integer regs) and / or the FRG line (32
    // FP regs) into the machine channel.
    void emitRegisters(uint64_t                cycle,
                       coreLib::CpuState const& postCommitCpu);

    // True when the sink should emit a per-commit listing right now.
    // Honours TRACE_INSTR plus PAL-window state.
    bool shouldEmitNow() const noexcept;

    // Emit one retire-compact line into m_retireLog.  Reads the integer
    // regfile via postCommitCpu, omitting any register currently zero
    // and skipping R31 always.  Charges the window counter when the
    // window is the trigger so it self-disables at zero.
    void emitRetireCompact(CommitRecord const&        record,
                           coreLib::CpuState const&   postCommitCpu);
};

} // namespace traceLib

#endif // TRACELIB_DECLISTINGSINK_H
