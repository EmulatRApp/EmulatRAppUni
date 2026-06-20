// ============================================================================
// traceLib/DecListingSink.cpp -- DEC-style listing sink implementation
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "traceLib/DecListingSink.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/DispatchEntry.h"
#include "grainFactoryLib/DispatchAccess.h"
#include "grainFactoryLib/generated/DispatchKinds.h"

#include "traceLib/Disassembler.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>


namespace traceLib {

// Definition of the debugger-pokeable peek counter.  Lives in the .cpp
// so the symbol exists exactly once across translation units.
std::atomic<int64_t> DecListingSink::s_traceWindowCountdown{ 0 };

namespace {

// Render a CommitRecord into a LookbackEntry.  The lookback ring
// stores frozen strings so the entries remain valid after the source
// CommitRecord goes out of scope -- this is what makes PAL-window
// auto-on possible (we hand-replay 10 entries on entry).
LookbackEntry freezeRecord(CommitRecord const& record, uint32_t cpuSlot)
{
    LookbackEntry e;
    e.cpuId    = cpuSlot;   // SOLE population path: the signature forces the one
                            // caller (onCommit) to supply the retiring CPU's slot,
                            // so a frozen entry can never carry a default/stray id.
    e.cycle    = record.cycle;
    e.pc       = record.pc;
    e.encoded  = record.encoded;
    e.mnemonic = record.mnemonic ? record.mnemonic : "?";

    // Look up the dispatch kind for the operand-string formatter.
    uint8_t const primaryOp = static_cast<uint8_t>((record.encoded >> 26) & 0x3Fu);
    auto const& entry       = grainFactory::primaryEntry(primaryOp);
    e.operands              = disassembleOperands(record.encoded, primaryOp, entry.kind);

    if (record.result) {
        e.result = formatResult(record.result->regWriteIdx,
                                record.result->regWriteValue,
                                record.result->regWriteIsFp);
    }
    e.valid = true;
    return e;
}


// Render the current wall-clock time as an ISO-8601 string with
// local-time offset, e.g. "2026-05-07T14:23:45-07:00".  Stamped into
// both trace channel headers so multi-run output directories stay
// distinguishable -- a same-named log from yesterday vs today is
// disambiguated at a glance.  Falls back to "<timestamp-error>" if
// the platform's localtime conversion fails (gmtime fallback would
// also work but local time is more useful at the operator's desk).
std::string isoTimestampNow() noexcept
{
    auto const now    = std::chrono::system_clock::now();
    auto const now_tt = std::chrono::system_clock::to_time_t(now);

    std::tm tmLocal{};
#if defined(_WIN32)
    // MSVC: localtime_s(struct tm*, time_t const*).  Returns 0 on success.
    if (localtime_s(&tmLocal, &now_tt) != 0) {
        return std::string{"<timestamp-error>"};
    }
#else
    if (localtime_r(&now_tt, &tmLocal) == nullptr) {
        return std::string{"<timestamp-error>"};
    }
#endif

    char buf[40];
    // %z gives "+HHMM" or "-HHMM"; reformat to "+HH:MM" by hand for
    // strict ISO-8601 compliance.  std::strftime returns 0 on buffer
    // overflow; 32 chars is generous for "YYYY-MM-DDTHH:MM:SS+HHMM".
    if (std::strftime(buf, sizeof(buf),
                      "%Y-%m-%dT%H:%M:%S%z", &tmLocal) == 0) {
        return std::string{"<timestamp-error>"};
    }

    std::string s{buf};
    // Insert the colon in the timezone offset: "+0700" -> "+07:00".
    if (s.size() >= 5) {
        char const sign = s[s.size() - 5];
        if (sign == '+' || sign == '-') {
            s.insert(s.size() - 2, 1, ':');
        }
    }
    return s;
}


// printf-into-string helper -- same shape as Disassembler.cpp's vfmt
// but defined locally to avoid coupling the two translation units.
std::string vfmt(char const* fmt, ...) noexcept
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int const n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return std::string{};
    return std::string{buf,
                       static_cast<size_t>(n < int(sizeof(buf))
                                              ? n
                                              : int(sizeof(buf)) - 1)};
}

} // anonymous namespace


DecListingSink::DecListingSink(std::filesystem::path const& decLogPath,
                               std::filesystem::path const& machineLogPath,
                               uint32_t                     traceMask)
    : m_traceMask(traceMask)
    , m_startTime(std::chrono::steady_clock::now())
{
    std::string const stamp = isoTimestampNow();

    if (!decLogPath.empty()) {
        m_decLog.open(decLogPath, std::ios::out | std::ios::trunc);
        m_decOpen = m_decLog.is_open();
        if (m_decOpen) {
            m_decLog << "CPU TRACE LOG -- DEC Style Listing\n";
            m_decLog << "Run started: " << stamp << "\n";
            m_decLog << "Trace mask:  0x" << std::hex << std::setw(8)
                     << std::setfill('0') << traceMask
                     << std::dec << std::setfill(' ') << "\n";
            m_decLog << "----------------------------------\n";
            m_decLog.flush();
        }
    }
    if (!machineLogPath.empty()) {
        m_machineLog.open(machineLogPath, std::ios::out | std::ios::trunc);
        m_machineOpen = m_machineLog.is_open();
        if (m_machineOpen) {
            m_machineLog << "# CPU TRACE LOG -- Machine Parsable\n";
            m_machineLog << "# run_started=" << stamp << "\n";
            m_machineLog << "# trace_mask=0x" << std::hex << std::setw(8)
                         << std::setfill('0') << traceMask
                         << std::dec << std::setfill(' ') << "\n";
            m_machineLog.flush();
        }
    }

    // ------------------------------------------------------------------
    // Retire-compact channel.  Default lands in D:\EmulatR\traces (the
    // project trace folder).  Only opened when the mask bit is set;
    // otherwise the file system is not touched at all.  On any open
    // failure we leave m_retireOpen false and emit a loud diagnostic to
    // stderr -- the retire path self-disables and the rest of the run
    // continues, so an unwritable trace dir does not abort the sim.
    // ------------------------------------------------------------------
    // 2026-06-01: also open the retire .trc when EMULATR_TRACE_WINDOW is set, so a
    // code-armed trace window (setTraceWindowCountdown) can capture a bounded burst
    // WITHOUT the always-on TRACE_RETIRE_COMPACT firehose mask (avoids the 560 GB
    // blowup at billions of cycles). File opens; onCommit still emits only while the
    // window countdown is active (mask bit stays clear).
    if ((traceMask & TRACE_RETIRE_COMPACT) || std::getenv("EMULATR_TRACE_WINDOW")) {
        std::fprintf(stderr,
                     "DecListingSink: TRACE_RETIRE_COMPACT requested\n");
        std::fflush(stderr);

        // Path resolution.  Operator can override via the
        // EMULATR_RETIRE_TRACE_DIR environment variable; default is
        // D:\EmulatR\traces -- the project trace folder, so traces are
        // readable in place with no copy step.  (Previously X:\traces,
        // a 2 TB iSCSI reserve, but that drive filled mid-run and is
        // not always mounted.)  The envvar still retargets without a
        // rebuild -- set it to e.g. X:\traces or any spare volume.
        char const* const envDir = std::getenv("EMULATR_RETIRE_TRACE_DIR");
        std::string const dirStr = (envDir != nullptr && envDir[0] != '\0')
                                       ? std::string{envDir}
                                       : std::string{"D:\\EmulatR\\traces"};
        std::fprintf(stderr,
                     "DecListingSink: retire dir resolved to '%s'%s\n",
                     dirStr.c_str(),
                     envDir ? " (from EMULATR_RETIRE_TRACE_DIR)" : " (default)");
        std::fflush(stderr);

        // Build timestamp in the format YYYYMMDD-HHMMSS (no separators
        // except the dash) so the filename works on every filesystem.
        char tsBuf[32] = "00000000-000000";
        {
            auto const now    = std::chrono::system_clock::now();
            auto const now_tt = std::chrono::system_clock::to_time_t(now);
            std::tm tmLocal{};
#if defined(_WIN32)
            bool const ok = (localtime_s(&tmLocal, &now_tt) == 0);
#else
            bool const ok = (localtime_r(&now_tt, &tmLocal) != nullptr);
#endif
            if (ok) {
                std::strftime(tsBuf, sizeof(tsBuf),
                              "%Y%m%d-%H%M%S", &tmLocal);
            }
        }
        std::fprintf(stderr,
                     "DecListingSink: timestamp '%s'\n", tsBuf);
        std::fflush(stderr);

        // Probe the parent's existence cheaply BEFORE create_directories.
        // exists() with error_code is a stat() call; it's the same kernel
        // path that would block on a wedged iSCSI mount, but it returns
        // an error code rather than throwing on failure.  When the
        // probe says the drive is missing or unreachable, we skip the
        // create_directories call entirely -- the directory cannot be
        // created on a drive that isn't there, so attempting it is a
        // guaranteed second wait.
        std::error_code probeEc;
        std::filesystem::path const retireDir{dirStr};
        std::filesystem::path const rootProbe = retireDir.root_path();
        std::fprintf(stderr,
                     "DecListingSink: probing root '%s'\n",
                     rootProbe.string().c_str());
        std::fflush(stderr);

        bool const rootExists = std::filesystem::exists(rootProbe, probeEc);
        std::fprintf(stderr,
                     "DecListingSink: root exists=%d (ec='%s')\n",
                     rootExists ? 1 : 0,
                     probeEc ? probeEc.message().c_str() : "ok");
        std::fflush(stderr);

        if (!rootExists) {
            std::fprintf(stderr,
                         "DecListingSink: root '%s' not reachable; retire sink "
                         "DISABLED.  Set EMULATR_RETIRE_TRACE_DIR to override.\n",
                         rootProbe.string().c_str());
            std::fflush(stderr);
            return;  // ctor done; sink stays disabled
        }

        std::error_code ec;
        std::filesystem::create_directories(retireDir, ec);
        std::fprintf(stderr,
                     "DecListingSink: create_directories returned ec='%s'\n",
                     ec ? ec.message().c_str() : "ok");
        std::fflush(stderr);

        if (ec) {
            std::fprintf(stderr,
                         "DecListingSink: create_directories('%s') FAILED: %s; "
                         "retire sink DISABLED\n",
                         retireDir.string().c_str(),
                         ec.message().c_str());
            std::fflush(stderr);
            return;
        }

        std::filesystem::path const retirePath =
            retireDir / (std::string{tsBuf} + std::string{"_srm.trc"});
        std::fprintf(stderr,
                     "DecListingSink: opening '%s'\n",
                     retirePath.string().c_str());
        std::fflush(stderr);

        m_retireLog.open(retirePath, std::ios::out | std::ios::trunc);
        m_retireOpen = m_retireLog.is_open();
        std::fprintf(stderr,
                     "DecListingSink: open returned is_open=%d\n",
                     m_retireOpen ? 1 : 0);
        std::fflush(stderr);

        if (m_retireOpen) {
            // Self-describing header: any consumer that opens the
            // file knows the format and elision rule without
            // needing external docs.
            m_retireLog << "# EmulatR V4 retire trace -- compact format\n";
            m_retireLog << "# run_started=" << stamp << "\n";
            m_retireLog << "# trace_mask=0x" << std::hex << std::setw(8)
                        << std::setfill('0') << traceMask
                        << std::dec << std::setfill(' ') << "\n";
            m_retireLog << "# format: RET cpu=<n> rpcc=<n> pc=<hex16> <mnem> "
                           "pal=<0|1> exc=<hex16>"
                           " [=>R|F<dd>=<hex16>]"
                           " [<ld|st><sz> va=<hex16> pa=<hex16> v=<hex16>]"
                           " [sde=<n>] [H<dd>=<hex16>]*\n";
            m_retireLog << "# fields: =>Rdd/=>Fdd = destination reg write this "
                           "retire (omitted if none); ld/st = data access of "
                           "<sz> bytes; va = effective/virtual addr the leaf "
                           "computed; pa = translated physical addr actually "
                           "accessed (pa==va for physical HW_LD/HW_ST/LDQP/STQP "
                           "-- translation bypassed); v = value loaded/stored.\n";
            m_retireLog << "# H<dd> = PAL shadow bank (SDE), omitted when zero.\n";
            m_retireLog << "# order: emitted at retire (WB) in retirement "
                           "order; sync ofstream, no reordering.\n";
            m_retireLog.flush();

            std::fprintf(stderr,
                         "DecListingSink: retire-compact sink OPENED at %s\n",
                         retirePath.string().c_str());
            std::fflush(stderr);
        } else {
            std::fprintf(stderr,
                         "DecListingSink: open('%s') FAILED; "
                         "retire sink DISABLED\n",
                         retirePath.string().c_str());
            std::fflush(stderr);
        }
    }
}


DecListingSink::~DecListingSink()
{
    flush();
}


void DecListingSink::flush()
{
    if (m_decOpen)     m_decLog.flush();
    if (m_machineOpen) m_machineLog.flush();
    if (m_retireOpen)  m_retireLog.flush();
}


// ============================================================================
// setEmitEnabled -- Phase C+ external trace-emit gate.
// ============================================================================
//
// Flips the per-commit I/O paths on or off.  When flipping ON after a
// gated window, drains buffered state via flush() so the first emitted
// line is contiguous with the next I/O write rather than reordered
// behind any header content the streams may still hold.  When flipping
// OFF, also flush so any final pre-gate emit is visible if the run
// terminates abnormally during the silent window.
//
// One-line state mutation today; lives in the .cpp so future expansion
// (e.g., emitting a "Trace gate opened at rpcc=N" marker into each
// stream) does not require touching the header.
// ============================================================================
void DecListingSink::setEmitEnabled(bool enabled) noexcept
{
    if (m_emitEnabled == enabled) {
        return;   // idempotent; Machine syncs at run-start and flips on
                  // again at PAL relocation -- second call is a no-op
                  // when both events agree, common for snapshot-resume.
    }
    flush();      // drain anything buffered before transitioning
    m_emitEnabled = enabled;
}


// ============================================================================
// emitRetireCompact -- one elided retire line per call
// ============================================================================
//
// Hot path.  Caller (onCommit) has already gated on either the mask bit
// or the window counter, so we are committed to writing a line.  When
// the trigger was the window counter, charge it once here so the peek
// self-disables at zero.  The integer regfile is read directly from
// postCommitCpu; no copy.
// ============================================================================

void DecListingSink::emitRetireCompact(CommitRecord const&        record,
                                       coreLib::CpuState const&   postCommitCpu)
{
    if (!m_retireOpen) {
        // No file to write to -- treat the call as a silent no-op.
        // The window counter is still charged so a peek requested
        // before the file existed does not stay armed forever.
        if (!(m_traceMask & TRACE_RETIRE_COMPACT)) {
            int64_t const w = s_traceWindowCountdown.load(std::memory_order_relaxed);
            if (w > 0) s_traceWindowCountdown.fetch_sub(1, std::memory_order_relaxed);
        }
        return;
    }

    // If the window peek is the only thing that brought us here,
    // charge it once for this line.  Mask-driven trace is free.
    if (!(m_traceMask & TRACE_RETIRE_COMPACT)) {
        int64_t const w = s_traceWindowCountdown.load(std::memory_order_relaxed);
        if (w > 0) s_traceWindowCountdown.fetch_sub(1, std::memory_order_relaxed);
    }

    // Header: cycle, PC, mnemonic, palMode, excAddr.  Mnemonic is the
    // codegen-emitted literal carried on CommitRecord; "?" is the
    // documented fallback used elsewhere in the sink when the leaf
    // omitted it.  Padded to 8 chars for column alignment -- the
    // longest natural Alpha mnemonics ("FCMOVxx", "MFPR_xxx") fit, and
    // overlength tokens just push the remainder right rather than
    // crashing the format.
    char const* const mnem = record.mnemonic ? record.mnemonic : "?";
    m_retireLog << vfmt("RET cpu=%u rpcc=%llu pc=%016llx %-8s pal=%d exc=%016llx",
                        static_cast<unsigned>(postCommitCpu.cpuSlot),
                        static_cast<unsigned long long>(record.cycle),
                        static_cast<unsigned long long>(record.pc),
                        mnem,
                        postCommitCpu.inPalMode() ? 1 : 0,
                        static_cast<unsigned long long>(postCommitCpu.excAddr));

    // Per-instruction effect: the destination register write this retire
    // produced, and any memory access (virtual/effective address + the
    // translated physical address + the value loaded or stored).  This
    // replaces the former full nonzero-regfile dump, whose persistent
    // working-set was diagnostically misleading -- stale registers read as
    // if freshly set (the 2026-05 "WimC" misread).  Full register state at
    // a region of interest comes from the BreakpointSink full-complement
    // gate; this channel now carries the per-instruction narrative.
    //   =>Rdd / =>Fdd : destination write (idx 31 == no write, suppressed)
    //   ld/st <sz>    : memory access of <sz> bytes
    //   va=           : effective/virtual address the leaf computed
    //   pa=           : physical address actually accessed (== va for
    //                   S_PhysAddr HW_LD/HW_ST/LDQP/STQP -- bypasses xlate)
    //   v=            : value loaded (into the dest reg) or stored
    if (coreLib::BoxResult const* const br = record.result) {
        if (br->regWriteIdx != coreLib::kNoRegWrite) {
            m_retireLog << vfmt(br->regWriteIsFp ? " =>F%02d=%016llx"
                                                 : " =>R%02d=%016llx",
                                static_cast<int>(br->regWriteIdx),
                                static_cast<unsigned long long>(br->regWriteValue));
        }
        if (br->memSize != coreLib::kNoMemEffect) {
            uint64_t const val = br->memIsStore ? br->memData
                                                : br->regWriteValue;
            m_retireLog << vfmt(" %s%u va=%016llx pa=%016llx v=%016llx",
                                br->memIsStore ? "st" : "ld",
                                static_cast<unsigned>(br->memSize),
                                static_cast<unsigned long long>(br->memAddr),
                                static_cast<unsigned long long>(br->memPhysAddr),
                                static_cast<unsigned long long>(val));
        }
    }

    // PAL shadow bank (EV6 SDE diagnostic, added 2026-05-22).
    // Dump the SDE gate and the 8 PAL shadow registers so the trace
    // shows the shadow bank alongside the visible regfile, making the
    // PAL-mode shadow swap (R4-R7 + R20-R23 <-> intShadow) observable at
    // every retire -- a reader can confirm the swap fires on PAL entry
    // and exit by watching H<dd> and R<dd> exchange across the boundary.
    // Slots are labelled by the architectural register each one shadows:
    // H04..H07 = intShadow[0..3] (shadow R4-R7), H20..H23 = intShadow[4..7]
    // (shadow R20-R23).  Same omit-if-zero elision as the GPR block, so
    // the group is invisible until the firmware arms the shadow bank.
    // `sde` is I_CTL<7:6> (SDE<1:0>); emitted only when non-zero so its
    // first appearance marks the cycle shadowing was enabled.
    {
        uint64_t const sde = (postCommitCpu.i_ctl >> 6) & uint64_t{0x3};
        if (sde != 0) {
            m_retireLog << vfmt(" sde=%llu",
                                static_cast<unsigned long long>(sde));
        }
        for (int i = 0; i < 4; ++i) {            // intShadow[0..3] shadow R4..R7
            uint64_t const v = postCommitCpu.intShadow[i];
            if (v == 0) continue;
            m_retireLog << vfmt(" H%02d=%016llx",
                                4 + i,
                                static_cast<unsigned long long>(v));
        }
        for (int i = 0; i < 4; ++i) {            // intShadow[4..7] shadow R20..R23
            uint64_t const v = postCommitCpu.intShadow[4 + i];
            if (v == 0) continue;
            m_retireLog << vfmt(" H%02d=%016llx",
                                20 + i,
                                static_cast<unsigned long long>(v));
        }
    }

    m_retireLog << '\n';
    m_retireLog.flush();

}


bool DecListingSink::shouldEmitNow() const noexcept
{
    // TRACE_INSTR is the unconditional per-commit emission switch.
    // When the user passes --trace and we set TRACE_ALL, this bit is
    // on and we emit on every onCommit.
    if (m_traceMask & TRACE_INSTR) {
        return true;
    }
    // TRACE_PAL_WINDOW is additive: with TRACE_INSTR off, emit only
    // during a PAL window or its post-exit tail (V1's quiet-mode
    // diagnostic for early-boot debugging).
    if ((m_traceMask & TRACE_PAL_WINDOW)
        && (m_inPalWindow || m_postExitCountdown > 0)) {
        return true;
    }
    return false;
}


void DecListingSink::emitCommit(LookbackEntry const&     entry,
                                coreLib::CpuState const* postCommitCpu)
{
    if (m_decOpen) {
        m_decLog << vfmt("c%02u %08llu %016llx %08x %-8s %-22s %s\n",
                         static_cast<unsigned>(entry.cpuId),
                         static_cast<unsigned long long>(entry.cycle),
                         static_cast<unsigned long long>(entry.pc),
                         entry.encoded,
                         entry.mnemonic.c_str(),
                         entry.operands.c_str(),
                         entry.result.c_str());
        m_decLog.flush();
    }
    if (m_machineOpen) {
        m_machineLog << vfmt(
            "INS cpu=%u rpcc=%llu pc=%016llx instr=%08x mnem=%s ops=\"%s\" result=\"%s\"\n",
            static_cast<unsigned>(entry.cpuId),
            static_cast<unsigned long long>(entry.cycle),
            static_cast<unsigned long long>(entry.pc),
            entry.encoded,
            entry.mnemonic.c_str(),
            entry.operands.c_str(),
            entry.result.c_str());
        m_machineLog.flush();
    }

    // Per-commit register dumps go into the machine channel only --
    // they would clutter the listing channel and are intended for
    // automated divergence diffing.
    if (postCommitCpu && m_machineOpen) {
        if (m_traceMask & (TRACE_REGFILE | TRACE_FPRFILE)) {
            emitRegisters(entry.cycle, *postCommitCpu);
        }
    }
}


void DecListingSink::emitRegisters(uint64_t                cycle,
                                   coreLib::CpuState const& postCommitCpu)
{
    if (m_traceMask & TRACE_REGFILE) {
        std::string line = vfmt("REG cpu=%u rpcc=%llu",
                                static_cast<unsigned>(postCommitCpu.cpuSlot),
                                static_cast<unsigned long long>(cycle));
        for (int i = 0; i < 32; ++i) {
            line += vfmt(" R%02d=%016llx",
                         i,
                         static_cast<unsigned long long>(postCommitCpu.intReg[i]));
        }
        m_machineLog << line << '\n';
    }
    if (m_traceMask & TRACE_FPRFILE) {
        std::string line = vfmt("FRG cpu=%u rpcc=%llu",
                                static_cast<unsigned>(postCommitCpu.cpuSlot),
                                static_cast<unsigned long long>(cycle));
        for (int i = 0; i < 32; ++i) {
            line += vfmt(" F%02d=%016llx",
                         i,
                         static_cast<unsigned long long>(postCommitCpu.fpReg[i]));
        }
        m_machineLog << line << '\n';
    }
    m_machineLog.flush();
}


void DecListingSink::onCommit(CommitRecord const&        record,
                              coreLib::CpuState const&   postCommitCpu)
{
    LookbackEntry const frozen = freezeRecord(record, postCommitCpu.cpuSlot);

    // Always update the lookback ring -- cheap, no I/O.  Keeping the
    // ring fresh during the Phase C+ pre-relocation gated window lets
    // the first post-gate onPalEntry dump emit valid recent context.
    m_lookback[m_lookbackHead & (LOOKBACK_SIZE - 1)] = frozen;
    ++m_lookbackHead;

    // Phase C+: external emit gate.  When Machine has not yet flipped
    // m_emitEnabled true (cold boot pre-PAL-relocation, cyc < 4.2M on
    // ES45), skip the three per-commit I/O paths below.  The lookback
    // ring update above continues so PAL transition hooks see valid
    // history.  PAL transition emits (onPalEntry/onPalExit) and
    // onRunEnd are NOT gated -- they emit one-time markers important
    // even during the silent window.
    if (!m_emitEnabled) {
        return;
    }

    if (shouldEmitNow()) {
        emitCommit(frozen, &postCommitCpu);

        // PAL window post-exit countdown: each commit while the
        // countdown is positive (and we are no longer inside the PAL
        // window) ticks it down.  When it reaches zero we go silent.
        if (!m_inPalWindow && m_postExitCountdown > 0) {
            --m_postExitCountdown;
        }
    }

    // Retire-compact diagnostic stream.  Independent of the PAL-window
    // gating above; fires whenever the mask bit is set OR the debugger-
    // pokeable peek counter is positive.  See header comment for line
    // format and elision rule.
    if ((m_traceMask & TRACE_RETIRE_COMPACT) || traceWindowActive()) {
        emitRetireCompact(record, postCommitCpu);
    }

    // Heartbeat: independent of trace mask, fires every
    // HEARTBEAT_INTERVAL retired instructions so a long quiet run
    // shows visible progress in the log file.  Includes a small
    // register snapshot (R00-R04) so the line carries forward-
    // progress information -- in steady-state loops the PC aliases
    // to the same offset within the loop body across heartbeats,
    // but the registers move.  R00-R04 are the canonical PALcode
    // scratch registers and the SRM decompressor's copy-loop state
    // (destination, count, source, scratch, segment counter) lives
    // there.
    if ((m_lookbackHead % HEARTBEAT_INTERVAL) == 0) {
        // Wall-clock elapsed since construction, in milliseconds.
        // Derived cycles-per-second is computed from cumulative
        // commits / elapsed seconds; protects against the first
        // heartbeat firing in <1 ms by clamping the divisor at 1.
        auto const now     = std::chrono::steady_clock::now();
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - m_startTime).count();
        uint64_t const cps =
            (elapsed > 0)
                ? static_cast<uint64_t>(
                      (static_cast<int64_t>(m_lookbackHead) * 1000) / elapsed)
                : m_lookbackHead;

        char regs[160];
        std::snprintf(regs, sizeof(regs),
                      "R00=%016llx R01=%016llx R02=%016llx R03=%016llx R04=%016llx",
                      static_cast<unsigned long long>(postCommitCpu.intReg[0]),
                      static_cast<unsigned long long>(postCommitCpu.intReg[1]),
                      static_cast<unsigned long long>(postCommitCpu.intReg[2]),
                      static_cast<unsigned long long>(postCommitCpu.intReg[3]),
                      static_cast<unsigned long long>(postCommitCpu.intReg[4]));

        // Compute the number of valid lookback entries to dump.  Same
        // arithmetic as onPalEntry / onRunEnd: clamp to LOOKBACK_DUMP
        // and walk forward in chronological order.
        uint32_t const have =
            (m_lookbackHead < LOOKBACK_DUMP) ? static_cast<uint32_t>(m_lookbackHead)
                                             : LOOKBACK_DUMP;

        if (m_decOpen) {
            m_decLog << vfmt(">>> HEARTBEAT cpu=%u rpcc=%llu pc=0x%016llx commits=%llu "
                             "elapsed_ms=%lld cps=%llu %s\n",
                             static_cast<unsigned>(postCommitCpu.cpuSlot),
                             static_cast<unsigned long long>(record.cycle),
                             static_cast<unsigned long long>(record.pc),
                             static_cast<unsigned long long>(m_lookbackHead),
                             static_cast<long long>(elapsed),
                             static_cast<unsigned long long>(cps),
                             regs);
            m_decLog << "    Lookback (last " << have << " instructions):\n";
            m_decLog.flush();
        }
        if (m_machineOpen) {
            m_machineLog << vfmt("HEARTBEAT cpu=%u rpcc=%llu pc=%016llx commits=%llu "
                                 "elapsed_ms=%lld cps=%llu %s\n",
                                 static_cast<unsigned>(postCommitCpu.cpuSlot),
                                 static_cast<unsigned long long>(record.cycle),
                                 static_cast<unsigned long long>(record.pc),
                                 static_cast<unsigned long long>(m_lookbackHead),
                                 static_cast<long long>(elapsed),
                                 static_cast<unsigned long long>(cps),
                                 regs);
            m_machineLog.flush();
        }

        // Dump the lookback ring tail.  Walks from oldest valid entry
        // up to head (inclusive of the just-committed instruction) so
        // the heartbeat shows the 10 most recent instructions in
        // chronological order.  Same loop pattern as onPalEntry.
        for (uint32_t i = 0; i < have; ++i) {
            uint64_t const idx =
                (m_lookbackHead - have + i) & (LOOKBACK_SIZE - 1);
            LookbackEntry const& e = m_lookback[idx];
            if (e.valid) emitCommit(e, /*postCommitCpu*/ nullptr);
        }
    }
}


void DecListingSink::onPalEntry(uint64_t cycle,
                                uint64_t entryPc,
                                uint64_t excAddr)
{
    // Activate the window -- if PAL_WINDOW is the gating mode this
    // turns on per-commit emission.  For non-windowed sinks this still
    // emits the marker into both channels for context.
    m_inPalWindow = true;

    // SMP marker tag: stamp the slot of the most recent retired entry
    // (single agent today => 0).  When CPU1 lands, thread the real slot
    // from the caller instead of inferring it from the ring.
    unsigned const cpu = static_cast<unsigned>(
        m_lookbackHead ? m_lookback[(m_lookbackHead - 1) & (LOOKBACK_SIZE - 1)].cpuId
                       : 0u);

    // Dump the last LOOKBACK_DUMP lookback entries into the trace
    // channels.  Walks backward from head to find oldest valid entry,
    // then forward to emit them in chronological order.
    uint32_t const have =
        (m_lookbackHead < LOOKBACK_DUMP) ? static_cast<uint32_t>(m_lookbackHead)
                                         : LOOKBACK_DUMP;

    if (m_decOpen) {
        m_decLog << vfmt(">>> PAL ENTRY cpu=%u rpcc=%llu entryPC=0x%016llx excAddr=0x%016llx\n",
                         cpu,
                         static_cast<unsigned long long>(cycle),
                         static_cast<unsigned long long>(entryPc),
                         static_cast<unsigned long long>(excAddr));
        m_decLog << "    Lookback (last " << have << " instructions):\n";
        m_decLog.flush();
    }
    if (m_machineOpen) {
        m_machineLog << vfmt("PAL_ENTRY cpu=%u rpcc=%llu entryPC=%016llx excAddr=%016llx\n",
                             cpu,
                             static_cast<unsigned long long>(cycle),
                             static_cast<unsigned long long>(entryPc),
                             static_cast<unsigned long long>(excAddr));
        m_machineLog.flush();
    }

    for (uint32_t i = 0; i < have; ++i) {
        uint64_t const idx =
            (m_lookbackHead - have + i) & (LOOKBACK_SIZE - 1);
        LookbackEntry const& e = m_lookback[idx];
        if (e.valid) emitCommit(e, /*postCommitCpu*/ nullptr);
    }
}


void DecListingSink::onPalExit(uint64_t cycle,
                               uint64_t targetPc)
{
    m_inPalWindow       = false;
    m_postExitCountdown = POST_PAL_TAIL;

    // SMP marker tag (see onPalEntry): slot of the most recent retired entry.
    unsigned const cpu = static_cast<unsigned>(
        m_lookbackHead ? m_lookback[(m_lookbackHead - 1) & (LOOKBACK_SIZE - 1)].cpuId
                       : 0u);

    if (m_decOpen) {
        m_decLog << vfmt("<<< PAL EXIT  cpu=%u rpcc=%llu targetPC=0x%016llx (tail=%u)\n",
                         cpu,
                         static_cast<unsigned long long>(cycle),
                         static_cast<unsigned long long>(targetPc),
                         POST_PAL_TAIL);
        m_decLog.flush();
    }
    if (m_machineOpen) {
        m_machineLog << vfmt("PAL_EXIT cpu=%u rpcc=%llu targetPC=%016llx tail=%u\n",
                             cpu,
                             static_cast<unsigned long long>(cycle),
                             static_cast<unsigned long long>(targetPc),
                             POST_PAL_TAIL);
        m_machineLog.flush();
    }
}


void DecListingSink::onRunEnd(coreLib::CpuState const& finalCpu)
{
    // Dump the marker plus the most recent LOOKBACK_DUMP retired
    // instructions from the ring.  Same machinery as onPalEntry's
    // pre-PAL replay -- the operator gets the last 10 cycles of
    // context regardless of whether PAL_WINDOW gating was active.
    uint32_t const have =
        (m_lookbackHead < LOOKBACK_DUMP) ? static_cast<uint32_t>(m_lookbackHead)
                                         : LOOKBACK_DUMP;

    if (m_decOpen) {
        m_decLog << vfmt("=== RUN END  cpu=%u pc=0x%016llx halted=%d lastFault=%u  "
                         "(replaying last %u retired instructions)\n",
                         static_cast<unsigned>(finalCpu.cpuSlot),
                         static_cast<unsigned long long>(finalCpu.pc),
                         finalCpu.halted ? 1 : 0,
                         static_cast<unsigned>(finalCpu.lastFaultCode),
                         have);
        m_decLog.flush();
    }
    if (m_machineOpen) {
        m_machineLog << vfmt("RUN_END cpu=%u pc=%016llx halted=%d lastFault=%u replay=%u\n",
                             static_cast<unsigned>(finalCpu.cpuSlot),
                             static_cast<unsigned long long>(finalCpu.pc),
                             finalCpu.halted ? 1 : 0,
                             static_cast<unsigned>(finalCpu.lastFaultCode),
                             have);
        m_machineLog.flush();
    }

    for (uint32_t i = 0; i < have; ++i) {
        uint64_t const idx =
            (m_lookbackHead - have + i) & (LOOKBACK_SIZE - 1);
        LookbackEntry const& e = m_lookback[idx];
        if (e.valid) emitCommit(e, /*postCommitCpu*/ nullptr);
    }
}

} // namespace traceLib
