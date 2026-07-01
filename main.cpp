// ============================================================================
// main.cpp -- Emulatr V4 entry point
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
// Phase 1 spine: parse argv -> construct Machine -> load firmware ->
// reset -> run -> dump state -> exit.  No Qt event loop.  TraceSink
// hook is a Phase 2 add; Phase 3 will surface PalVectorTable wiring.
//
// Exit codes:
//
//   0  CPU halted cleanly (CALL_PAL HALT)
//   1  any fault, max-cycles exceeded, or argv parse error
//   2  firmware load failure
//
// CHANGE 2026-06-08 (TEP / Claude): boot throughput baseline (task #10).
//   Wall-clock mach.run() with std::chrono::steady_clock and emit a
//   "PROFILE:" line (retires, cycles, wall, instr/s, cyc/s) to stdout so the
//   number survives 2>/dev/null.  totalRetires() is the retired-instruction
//   metric (matches the ~47M/s figure); cyc/s is reported alongside since V4
//   retires != cycles 1:1.  Used to profile cold-boot-to->>> (--no-autoload).
//
// ============================================================================

#include <atomic>        // 2026-06-25: signal-handler -> Machine stop flag
#include <chrono>        // 2026-06-08: boot throughput baseline (task #10)
#include <csignal>       // 2026-06-25: SIGINT/SIGTERM -> graceful flush
#include <cstdio>
#include <cstdlib>
#include <filesystem>    // 2026-06-08: std::filesystem::path firmware fallback (SSOT Slice B)
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>   // 2026-06-05: EMULATR_AUTOSNAP env compare
#include <utility>       // 2026-06-08: std::move(settings) into Machine (SSOT Slice A)

// 2026-05-29: QCoreApplication required by SRMConsoleDevice (the COM1
// Qt-based TCP backend now owned by Machine).  We construct it once,
// never call exec() on this thread -- the emulator runs synchronously
// in mach.run().  The SRMConsoleDevice runs on its own QThread which
// pumps its own event loop for QTcpServer/QTcpSocket signal delivery.
// QCoreApplication's purpose here is to satisfy Qt's global state
// requirements (metatype registration, QtNetwork module initialisation
// on Windows) without which QTcpServer.listen() can fail in obscure
// ways.  No event loop runs on the main thread.
#include <QCoreApplication>

#include "coreLib/LogSubsystem.h"
#include "memoryLib/GuestMemory.h"
#include "systemLib/AppOptions.h"
#include "systemLib/CpuStateDump.h"
#include "config/IniLoader.h"            // SSOT: load EmulatorSettings once (Slice A)
#include "systemLib/Machine.h"
#include "systemLib/Snapshot.h"
#include "systemLib/SrmLoader.h"
#include "systemLib/StopReason.h"
#include "traceLib/BreakpointSink.h"
#include "traceLib/DecListingSink.h"
#include "traceLib/PaDump.h"
#include "traceLib/RetireProfiler.h"   // 2026-06-08: totalRetires() for PROFILE line (task #10)
#if defined(EMULATR_FP_SOFTFLOAT)
#include "fpBoxLib/fp_host_guard.h"    // 2026-06-10: host-FP safety self-test (task #26 Phase A)
#endif


// ----------------------------------------------------------------------------
// Graceful-stop on SIGINT (Ctrl-C) / SIGTERM (kill) -- 2026-06-25.
// Without this, the default disposition terminates the process immediately, so
// ~Machine::forceFlush() never runs and an `update srm'/`set' heal is LOST
// (the "hard taskkill skips the destructor" hazard the flash code warns about).
// The handler is async-signal-safe: it ONLY sets a lock-free atomic via
// Machine::requestStop() (NO I/O -- file ops are not async-signal-safe).  The
// run loop polls that flag, returns cleanly, and main() falls through to the
// normal ~Machine flush.  A SECOND signal restores the default handler and
// re-raises, so a wedged shutdown can still be force-killed.
// ----------------------------------------------------------------------------
namespace {
    std::atomic<systemLib::Machine*> g_machineForSignal{ nullptr };

    extern "C" void emulatrSignalHandler(int sig) noexcept
    {
        systemLib::Machine* m = g_machineForSignal.load(std::memory_order_relaxed);
        if (m != nullptr) {
            m->requestStop();                 // async-signal-safe: atomic store only
            std::signal(sig, SIG_DFL);        // 2nd same signal -> default (force quit)
        } else {
            std::signal(sig, SIG_DFL);
            std::raise(sig);                  // not armed yet -> default behavior
        }
    }
}

int main(int argc, char* argv[])
{
    // ------------------------------------------------------------------
    // Qt global state.  Required by SRMConsoleDevice (the COM1 TCP
    // backend lives on its own QThread inside Machine).  QCoreApplication
    // is constructed but never exec()'d -- the main thread runs the
    // emulator synchronously via mach.run().  The console QThread's own
    // event loop dispatches QTcpServer / QTcpSocket signals independent
    // of the main thread.  Lifetime: declared before any Qt object that
    // depends on it, destroyed last.  2026-05-29.
    // ------------------------------------------------------------------
    QCoreApplication app(argc, argv);

    // ------------------------------------------------------------------
    // CLI parse.  --help and parse errors short-circuit before we
    // touch any heavy machinery.
    // ------------------------------------------------------------------
    systemLib::AppOptions opts = systemLib::AppOptions::parse(argc, argv);  // non-const: firmwarePath may fall back to ini (Slice B)

    if (opts.helpRequested) {
        std::fputs(systemLib::AppOptions::helpText(), stdout);
        return 0;
    }
    if (!opts.parseError.empty()) {
        std::fprintf(stderr, "Emulatr: %s\n\n", opts.parseError.c_str());
        std::fputs(systemLib::AppOptions::helpText(), stderr);
        return 1;
    }

    // ------------------------------------------------------------------
    // Program-identity banner.  This is EmulatR announcing itself in its
    // own voice -- it is NOT emulated firmware output (the SRM firmware's
    // console banner arrives separately on COM1 via the UART sink).
    // Printed once, after the --help / parse-error short-circuits.
    // ------------------------------------------------------------------
    std::fputs(
        "================================================================\n"
        "   ASA-EmulatR  V4.0-0  --  Alpha AXP (EV6 / 21264) Emulator\n"  // 2026-06-05: version tag
        "   (c) 2025, 2026 eNVy Systems, Inc.    All rights reserved.\n"
        "   Project Architect: Timothy Peer\n"
        "   Licensed under the eNVy Systems Non-Commercial License v1.1\n"
        "   https://envysys.com\n"
        "================================================================\n\n",
        stdout);

    // ------------------------------------------------------------------
    // Host-FP safety gate (task #26 Phase A).  Prove the host floating-point
    // path agrees with the SoftFloat oracle bit-for-bit BEFORE any guest FP
    // can execute; a divergent build (x87 double-rounding, FMA contraction,
    // or flush-to-zero) would make every guest FP result silently wrong, so
    // it must be unrunnable.  Hard-abort policy lives in the guard; the
    // detector itself is pure (the doctest exercises the same detector).
    // ------------------------------------------------------------------
#if defined(EMULATR_FP_SOFTFLOAT)
    fpBox::enforceHostFpSafeOrAbort();
#endif

    // ------------------------------------------------------------------
    // SSOT: load EmulatorSettings ONCE from EmulatrV4.ini -- the single source
    // of truth for tunables.  Precedence: struct defaults < ini < CLI.  Loaded
    // BEFORE firmware resolution so the firmware path can fall back to the ini.
    // ------------------------------------------------------------------
    emulatr::config::EmulatorSettings settings;
    {
        auto const cfgLoad = emulatr::config::IniLoader::loadDefault();
        if (!cfgLoad.foundFile) {
            std::fprintf(stderr, "config: no EmulatrV4.ini found; using built-in defaults\n");
        }
        for (std::string const& w : cfgLoad.warnings) {
            std::fprintf(stderr, "config: %s\n", w.c_str());
        }
        for (std::string const& issue : cfgLoad.settings.validate()) {
            std::fprintf(stderr, "config invalid: %s\n", issue.c_str());
        }
        settings = cfgLoad.settings;
    }

    // SSOT (Slice B): firmware path precedence CLI > ini.  If --firmware was not
    // given, fall back to [ROM] firmwareImage; require one of the two.
    if (opts.firmwarePath.empty() && !settings.rom.firmwareImage.empty()) {
        opts.firmwarePath = std::filesystem::path(settings.rom.firmwareImage);
        std::fprintf(stderr, "firmware: using [ROM] firmwareImage from ini: %s\n",
                     settings.rom.firmwareImage.c_str());
    }
    if (opts.firmwarePath.empty()) {
        std::fprintf(stderr,
                     "error: no firmware -- pass --firmware <path> or set "
                     "[ROM] firmwareImage in EmulatrV4.ini\n");
        return 2;
    }

    // ------------------------------------------------------------------
    // Resolve firmware format.  Auto picks SRM if the file extension
    // is .exe (case-insensitive) -- the vendor convention -- and Raw
    // otherwise.  --firmware-format raw|srm overrides regardless of
    // extension.
    // ------------------------------------------------------------------
    auto const ext = opts.firmwarePath.extension().string();
    bool const looksLikeExe =
        ext.size() == 4
        && (ext[0] == '.')
        && (ext[1] == 'e' || ext[1] == 'E')
        && (ext[2] == 'x' || ext[2] == 'X')
        && (ext[3] == 'e' || ext[3] == 'E');

    // A '.rom' extension (Auto format) selects the pre-decompressed
    // AXPBox console-image loader (loadDecompressedRom).
    bool const looksLikeRom =
        ext.size() == 4 && ext[0] == '.'
        && (ext[1] == 'r' || ext[1] == 'R')
        && (ext[2] == 'o' || ext[2] == 'O')
        && (ext[3] == 'm' || ext[3] == 'M');

    systemLib::FirmwareFormat const fmt =
        (opts.firmwareFormat == systemLib::FirmwareFormat::Auto)
            ? (looksLikeExe ? systemLib::FirmwareFormat::Srm
                            : systemLib::FirmwareFormat::Raw)
            : opts.firmwareFormat;

    // SSOT (2026-06-12): guest memory size precedence CLI > ini.  If --mem was
    // NOT given on the CLI, fall back to [System] memorySize from EmulatrV4.ini
    // (parsed into settings).  Without this the 64 MiB AppOptions default always
    // won, so show config / the SRM memory sizing reported 64 MB regardless of
    // the ini's 1 GiB.  The value flows memSize -> Machine -> TsunamiChipset ->
    // Cchip AARs (computeAAR), which is what the firmware sizes against.
    if (!opts.memSizeSet && settings.system.memorySizeBytes != 0) {
        opts.memSize = settings.system.memorySizeBytes;
        std::fprintf(stderr, "memory: using [System] memorySize from ini: %llu bytes\n",
                     static_cast<unsigned long long>(opts.memSize));
    }

    // SSOT (2026-06-23): record the RESOLVED firmware path (CLI > ini) back into
    // settings so the Machine ctor can derive the platform manifest from it
    // (<firmware-stem>_platform.json).  run_fw.sh passes --firmware on the CLI and
    // does NOT set [ROM] firmwareImage, so without this the ctor would see a
    // stale/empty ini value and load the wrong (or default) manifest.
    // generic_string() keeps forward slashes for the stem derivation.
    settings.rom.firmwareImage = opts.firmwarePath.generic_string();

    // ------------------------------------------------------------------
    // Construct the Machine and load firmware.
    // ------------------------------------------------------------------
    systemLib::Machine mach{opts.memSize, std::move(settings)};

    // Arm graceful-stop signals now that `mach` exists (it outlives the run and
    // its destructor flushes the flash).  SIGINT (Ctrl-C) + SIGTERM (kill) ->
    // requestStop() -> clean run-loop exit -> ~Machine forceFlush.  std::signal
    // is portable across Windows (CRT) and macOS/Linux; no #ifdef needed.
    g_machineForSignal.store(&mach, std::memory_order_relaxed);
    std::signal(SIGINT,  emulatrSignalHandler);
    std::signal(SIGTERM, emulatrSignalHandler);

    bool loadOk = false;
    bool isDecompressedRom = false;
    if (opts.firmwareFormat == systemLib::FirmwareFormat::Auto && looksLikeRom) {
        // Pre-decompressed AXPBox console image: load to PA 0, PC/PAL_BASE
        // from the 16-byte header.  Skips the decompressor entirely.
        loadOk = mach.loadDecompressedRom(opts.firmwarePath);
        isDecompressedRom = true;
    } else if (fmt == systemLib::FirmwareFormat::Srm) {
        // SRM uses the V1 default load PA (0x900000) unless overridden.
        uint64_t const srmLoadPa =
            (opts.loadPa != 0) ? opts.loadPa : systemLib::kDefaultLoadPa;
        loadOk = mach.loadSrmFirmware(opts.firmwarePath, srmLoadPa);
    } else {
        loadOk = mach.loadFirmware(opts.firmwarePath, opts.loadPa, opts.startPa);
    }
    if (!loadOk) {
        std::fprintf(stderr, "Emulatr: %s\n", mach.lastLoadError().c_str());
        return 2;
    }

    // ------------------------------------------------------------------
    // Optional: install the trace sink.  --trace dec.log,machine.log
    // populates opts.decTraceLog / opts.machineTraceLog and arms
    // traceMask = TRACE_ALL (placeholder set by the parser).  Either
    // log path may be empty -- DecListingSink disables that channel.
    // The sink lives on the stack alongside `mach`; Machine holds a
    // raw pointer per setTraceSink's contract.
    // ------------------------------------------------------------------
    std::unique_ptr<traceLib::DecListingSink> trace;
    if (!opts.decTraceLog.empty() || !opts.machineTraceLog.empty()) {
        trace = std::make_unique<traceLib::DecListingSink>(
                    opts.decTraceLog,
                    opts.machineTraceLog,
                    opts.traceMask);
    }
    else if (std::getenv("EMULATR_TRACE_WINDOW") != nullptr) {
        // 2026-06-13: console-armable retire window WITHOUT a continuous
        // RETIRE_COMPACT stream.  EMULATR_TRACE_WINDOW makes the DecListingSink
        // ctor open its _srm.trc; traceMask=0 means it emits ONLY while
        // DecListingSink::s_traceWindowCountdown > 0, which the SRM operator
        // arms from the prompt via the TIG trace-arm reg
        // (TsunamiChipset::kTigTraceArmReg):
        //     >>> e pmem:80130000FF8      -- trace ON until run end
        //     >>> d pmem:80130000FF8 N    -- trace next N instrs (0 = off)
        // This bounds capture to exactly the `b dqa0/1` command, skipping the
        // multi-billion-cycle cold boot.  dec.log / machine.log stay disabled.
        trace = std::make_unique<traceLib::DecListingSink>(
                    std::filesystem::path{}, std::filesystem::path{},
                    /*traceMask*/ 0u);
    }

    // ------------------------------------------------------------------
    // BreakpointSink wrap.
    // ------------------------------------------------------------------
    // BreakpointSink is always constructed.  Its gate atomics (s_gateOpenPc,
    // s_gateClosePc, s_revolutionsRemaining) carry compile-time defaults
    // targeting the 2026-05-28 MCHK return-path investigation (open=0xd841,
    // close=0xd955, one revolution -- SROM-resident PAL MCHK handler from
    // post-sys__cbox decision block through the HW_REI Rb=2 -> PC=0 jump).
    // When the run hits those PCs the
    // sink lazily opens X:\traces\YYYYMMDD-HHMMSS_break.trc and writes a
    // full-complement BRK record per retire bracketed by IPR_SNAP /
    // PT_SNAP / CBX_SNAP at gate transitions.
    //
    // The DecListingSink (when present) is wrapped as the downstream
    // chain so the existing _srm.trc / dec.log / machine.log channels
    // are unperturbed; every TraceSink call is forwarded to the
    // downstream before the BreakpointSink's own logic runs.
    //
    // For investigations targeting a different region, write the new
    // gate PCs into BreakpointSink::s_gateOpenPc / s_gateClosePc from
    // the VS Immediate window before invoking run(), or via the static
    // setGateOpenPc / setGateClosePc helpers.  When no run ever reaches
    // a configured gate, no break file is created.
    // ------------------------------------------------------------------
    auto breakSink = std::make_unique<traceLib::BreakpointSink>(
                          std::filesystem::path{},
                          trace.get());
    mach.setTraceSink(breakSink.get());

    // Pre-decompressed console: Step D never fires to flip the trace emit
    // gate, so enable the retire trace from cycle 0 for AXPBox comparison.
    if (isDecompressedRom && mach.traceSink() != nullptr) {
        mach.traceSink()->setEmitEnabled(true);
    }

    // ------------------------------------------------------------------
    // EMULATR_CHECKPOINTS -- multi-PC checkpoint ledger arming (2026-06-03)
    // ------------------------------------------------------------------
    // Orthogonal to the gate below.  Set the env var to a comma-separated
    // list of up to BreakpointSink::kMaxCheckpoints entries, each either
    // "0xPC" or "label:0xPC".  Every armed PC becomes a one-shot tripwire
    // that records its first-hit cycle + a full GPR snapshot; at run end
    // the sink prints which checkpoints were reached and which was LAST.
    // Collapses the H1/H2/H3 "where does the boot stop" question into a
    // single run.  Example (path-to-prompt diagnostic):
    //   set EMULATR_CHECKPOINTS=shcreate:0x...,shentry:0x...,isatty:0x...,rwp:0x...
    //
    // REMOVAL TRIGGER: inert unless the env var is set; delete only if the
    // checkpoint ledger feature is retired.
    if (char const* ckEnv = std::getenv("EMULATR_CHECKPOINTS")) {
        std::string spec(ckEnv);
        int    slot = 0;
        size_t pos  = 0;
        while (pos < spec.size() &&
               slot < traceLib::BreakpointSink::kMaxCheckpoints) {
            size_t const comma = spec.find(',', pos);
            std::string tok = spec.substr(pos, (comma == std::string::npos)
                                                   ? std::string::npos
                                                   : comma - pos);
            pos = (comma == std::string::npos) ? spec.size() : comma + 1;

            size_t const b = tok.find_first_not_of(" \t");
            size_t const e = tok.find_last_not_of(" \t");
            if (b == std::string::npos) {
                continue;
            }
            tok = tok.substr(b, e - b + 1);
            if (tok.empty()) {
                continue;
            }

            std::string label;
            std::string hexs  = tok;
            size_t const colon = tok.find(':');
            if (colon != std::string::npos) {
                label = tok.substr(0, colon);
                hexs  = tok.substr(colon + 1);
            }
            uint64_t const pc = std::strtoull(hexs.c_str(), nullptr, 0);
            if (pc != 0) {
                traceLib::BreakpointSink::setCheckpoint(
                    slot, pc, label.empty() ? nullptr : label.c_str());
                std::fprintf(stderr,
                    "EMULATR_CHECKPOINTS: armed slot %d label=%s pc=0x%llx\n",
                    slot, label.empty() ? "-" : label.c_str(),
                    static_cast<unsigned long long>(pc));
                ++slot;
            }
        }
        std::fflush(stderr);
    }

    // ------------------------------------------------------------------
    // Post-SCBB wait-loop diagnostic gate -- session 2026-05-29
    // ------------------------------------------------------------------
    // Re-arms the BreakpointSink at the firmware's post-SCBB wait point.
    // Across 21 interval-timer fires between cyc 189.5M and 209.7M, the
    // savedPc settled at 0x1c6788 in 17/21 cases (with outliers at
    // 0x1c6524, 0x1c699c, 0x1c641c, 0x59460, 0x77150).  This identifies
    // PC 0x1c6788 as the dominant user-mode wait point AFTER the
    // calibration loop, SCBB, and OS PAL takeover have all completed.
    //
    // Gate width: open at 0x1c6788, close at +8 (2 instructions later).
    // This captures ~2 retire records per revolution; 4 revolutions
    // yields ~8 records bracketed by IPR_SNAP / PT_SNAP / CBX_SNAP at
    // each gate transition.  Comparing register state across revolutions
    // identifies which register / IPR / PT slot is being polled, and
    // whether the polled value is advancing or stuck.
    //
    // REMOVAL TRIGGER: delete this block once the post-SCBB wait
    // diagnosis is closed and the firmware reaches banner emission
    // (first writeTHR fires on the Uart16550 stderr mirror).
    //
    // Prior gate (session 2026-05-28) was at 0x1c69e8/0x1c69f0 for the
    // LSR-returns-0xFF wedge investigation; that wedge was closed by
    // the AlphaPte kPfnWidth fix (task #42) and the UART now returns
    // proper 0x60 (THRE | TEMT).
    // ------------------------------------------------------------------
    // 2026-05-31 (timer self-test root-cause): capture one full clock-interrupt
    // handler.  The clock dispatches via SCB__CLOCK(0x600) -> R2 = *(SCBB+0x600)
    // = 0x1c6d4c; observation from the clean cold boot is that 0x1c6d4c acks the
    // timer (Cchip MISC ITINTR W1C) and returns to the console main loop WITHOUT
    // running the C timer_interrupt() ISR, so timebase[].tick_count never
    // advances and the SRM prints "*** no timer interrupts on CPU 0 ***".
    // Gate open at the clock-dispatch target, close at a main-loop PC the
    // handler returns through (0x1c6788).  Each revolution = one full handler
    // invocation; the BRK stream shows whether it ever does the tick_count
    // store (timer_interrupt body: getpcb / spinlock / pcb cputime+=1 /
    // quantum dec / STL tick_count) or just acks + hw_rei.  4 revolutions.
    traceLib::BreakpointSink::setGateOpenPc (0x000000000001c6d4cULL);
    traceLib::BreakpointSink::setGateClosePc(0x000000000001c6788ULL);
    traceLib::BreakpointSink::setRevolutionsRemaining(4);

    // ------------------------------------------------------------------
    // EMULATR_GATE -- runtime override of the BreakpointSink gate (2026-06-03)
    // ------------------------------------------------------------------
    // Mirrors the EMULATR_CHECKPOINTS parse above so windowed retire
    // traces never need a rebuild to re-aim the gate.  Format: a
    // comma-separated list of key:0xPC | key:int tokens, keys
    // open / close / rev (any subset; unspecified keys keep the
    // compile-time default).  Example (build_dsrdb fread SYSFAULT window,
    // journals/Fread_Sysfault_Investigation.txt):
    //   set EMULATR_GATE=open:0x7d048,close:0x1c62d8,rev:1
    //
    // REMOVAL TRIGGER: none -- this is permanent diagnostic infrastructure
    // (the gate is otherwise only changeable by editing the two
    // setGate*Pc calls above and rebuilding).
    if (char const* gateEnv = std::getenv("EMULATR_GATE")) {
        std::string spec(gateEnv);
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t const comma = spec.find(',', pos);
            std::string tok = spec.substr(pos, (comma == std::string::npos)
                                                   ? std::string::npos
                                                   : comma - pos);
            pos = (comma == std::string::npos) ? spec.size() : comma + 1;

            size_t const colon = tok.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string const key = tok.substr(0, colon);
            std::string const val = tok.substr(colon + 1);
            if (key == "open") {
                traceLib::BreakpointSink::setGateOpenPc(
                    std::strtoull(val.c_str(), nullptr, 0));
            } else if (key == "close") {
                traceLib::BreakpointSink::setGateClosePc(
                    std::strtoull(val.c_str(), nullptr, 0));
            } else if (key == "rev") {
                traceLib::BreakpointSink::setRevolutionsRemaining(
                    static_cast<int32_t>(std::strtol(val.c_str(), nullptr, 0)));
            }
        }
        std::fprintf(stderr, "EMULATR_GATE: gate overridden from env\n");
    }

    std::fprintf(stderr,
                 "BreakpointSink: armed open=0x%016llx close=0x%016llx "
                 "revolutions=%d (override via EMULATR_GATE or s_gateOpenPc / "
                 "s_gateClosePc / s_revolutionsRemaining)\n",
                 static_cast<unsigned long long>(
                     traceLib::BreakpointSink::s_gateOpenPc.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(
                     traceLib::BreakpointSink::s_gateClosePc.load(std::memory_order_relaxed)),
                 traceLib::BreakpointSink::s_revolutionsRemaining.load(std::memory_order_relaxed));
    std::fflush(stderr);

    // ------------------------------------------------------------------
    // Pre-diagnostic one-shot snapshot trigger (--snapshot-on-pc).
    // ------------------------------------------------------------------
    // Arms Machine's per-retire PC check.  The first retire at the armed
    // PC writes a predig_*.axpsnap file and disables auto-save, so the
    // file stays mtime-newest for the next cold start's autoloadLatest.
    // Recommended pairing for the sys__cbox investigation:
    //     --snapshot-on-pc 0x12040 --snapshot-name-tag sys_cbox_entry
    // The resulting file is `snapshots/predig_sys_cbox_entry_cyc<N>.axpsnap`
    // and survives every subsequent run's auto-prune.
    // ------------------------------------------------------------------
    // 2026-06-06: one PC = legacy single-shot (disables auto-save on fire,
    // keeps the predig mtime-newest for autoload).  Two+ PCs = multi-PC
    // mint run: each fires once, distinct predig per PC, auto-save left to
    // --autosnapshot off.  Lets one cold boot capture several entry points.
    if (opts.snapshotOnPcs.size() == 1) {
        mach.armSnapshotOnPc(opts.snapshotOnPcs[0], opts.snapshotNameTag);
        std::fprintf(stderr,
                     "Machine: predig snapshot armed at pc=0x%016llx "
                     "tag='%s' (one-shot, will disable auto-save on fire)\n",
                     static_cast<unsigned long long>(opts.snapshotOnPcs[0]),
                     opts.snapshotNameTag.c_str());
        std::fflush(stderr);
    }
    else if (opts.snapshotOnPcs.size() > 1) {
        for (uint64_t pc : opts.snapshotOnPcs) {
            char hexbuf[24];
            std::snprintf(hexbuf, sizeof hexbuf, "%llx",
                          static_cast<unsigned long long>(pc));
            std::string tag = opts.snapshotNameTag.empty()
                            ? std::string{hexbuf}
                            : opts.snapshotNameTag + "_" + hexbuf;
            mach.addSnapshotOnPc(pc, tag);
        }
        std::fprintf(stderr,
                     "Machine: %zu multi-PC predig snapshots armed "
                     "(each one-shot; use --autosnapshot off)\n",
                     opts.snapshotOnPcs.size());
        std::fflush(stderr);
    }

    // ------------------------------------------------------------------
    // Experimental: one-shot synthetic INTERRUPT-class trap injection.
    // ------------------------------------------------------------------
    // Fires once when cpu.cycleCount first reaches the armed value.
    // Diverts cpu.pc to palBase + 0x100 (the Alpha INTERRUPT entry) as
    // if a chipset had asserted IRQ.  Used to test whether the SRM
    // sys__cbox MCHK loop is a deliberate idle wait for an external
    // event source that V4's chipset model does not currently drive.
    //
    // Recommended invocation when paired with the predig snapshot for
    // the 2026-05-13 investigation:
    //     --inject-interrupt-at-cycle 0x1017f000
    // That fires ~3500 cycles after the predig autoload point (cycle
    // 270000161 = 0x1017e0e1), well inside the steady-state MCHK loop.
    // ------------------------------------------------------------------
    if (opts.injectInterruptCycle != 0) {
        mach.armInterruptInjection(opts.injectInterruptCycle);
        std::fprintf(stderr,
                     "Machine: INTERRUPT injection armed at cyc=%llu "
                     "(one-shot, target=palBase + 0x100)\n",
                     static_cast<unsigned long long>(opts.injectInterruptCycle));
        std::fflush(stderr);
    }

    // The loader captured startPc (PAL bit stripped) and palMode (PAL
    // bit value); resetToLoadedEntry seeds the CPU from those.  The
    // --pal-mode CLI flag forces palMode = true regardless of the
    // start-pa bit, matching V1 convenience semantics.
    mach.resetToLoadedEntry();
    if (opts.palMode) {
        mach.cpu().pc |= uint64_t{1};   // PALmode == PC<0> (force PAL via --pal-mode)
    }

    // ------------------------------------------------------------------
    // Snapshot autoload (newest *.axpsnap in ./snapshots/).
    // ------------------------------------------------------------------
    // If a snapshot is found, its CpuState + memory + chipset + SRM
    // staging fully replace the freshly-loaded firmware state above.
    // The cold-boot firmware load is still executed so that the
    // no-snapshot path works; on snapshot hit it just gets overwritten
    // (cheap relative to the boot loop we are skipping).  Pruning of
    // old auto-saves happens inside Machine::run when a periodic save
    // fires.
    // 2026-06-05: --no-autoload (or EMULATR_NO_AUTOLOAD) forces a genuine
    // cold boot by skipping the autoload-newest step -- required for the
    // cold-origin mint run (yyreset hit count must be from cold, not a
    // restore).  See Spec_overnight_coldboot_mint_run.
    bool const noAutoload =
        opts.noAutoload || (std::getenv("EMULATR_NO_AUTOLOAD") != nullptr);
    if (noAutoload) {
        std::fprintf(stderr, "DEBUG: autoload suppressed (--no-autoload) "
                             "-- genuine cold boot\n");
    } else {
        systemLib::SnapshotResult const ar =
            systemLib::autoloadLatest(mach, mach.snapshotDir());
        if (ar.success) {
            std::fprintf(stderr,
                         "DEBUG: snapshot autoloaded from '%s' (cycle=%llu)\n",
                         ar.path.c_str(),
                         static_cast<unsigned long long>(ar.cycleAtCapture));
        } else if (!ar.path.empty()) {
            std::fprintf(stderr,
                         "DEBUG: snapshot autoload failed for '%s': %s\n",
                         ar.path.c_str(), ar.errorMessage.c_str());
        }
    }

    // Auto-snapshot defaults to OFF on Machine so the test suite stays
    // silent.  Production binary opts in: write a halt save on any
    // fault and a periodic save every kAutoSavePeriodCycles cycles.
    // 2026-06-05: --autosnapshot off (or EMULATR_AUTOSNAP=off) disables
    // the periodic saves so a mint run's only output is the named
    // --snapshot-on-pc file (no disk cliff on a trillion-cycle cold boot).
    char const* const autosnapEnv = std::getenv("EMULATR_AUTOSNAP");
    bool const autoSnapshotEnabled =
        opts.autoSnapshot
        && !(autosnapEnv != nullptr && std::string_view{autosnapEnv} == "off");
    mach.setAutoSnapshotEnabled(autoSnapshotEnabled);
    if (!autoSnapshotEnabled) {
        std::fprintf(stderr, "DEBUG: periodic auto-snapshots OFF "
                             "(--autosnapshot off)\n");
    }

    // ------------------------------------------------------------------
    // Apply --log-* flags onto the LogSubsystem configuration table.
    // Errors are surfaced and treated as fatal: a typo in a subsystem
    // name should fail loud, not silently mis-configure logging.
    // ------------------------------------------------------------------
    {
        std::string errMsg;
        bool ok = true;
        if (!opts.logDisable.empty()) {
            ok = coreLib::applyLogFlagDisable(opts.logDisable, errMsg);
        }
        if (ok && !opts.logOnly.empty()) {
            ok = coreLib::applyLogFlagOnly(opts.logOnly, errMsg);
        }
        if (ok && !opts.logVerbose.empty()) {
            ok = coreLib::applyLogFlagVerbose(opts.logVerbose, errMsg);
        }
        if (ok && !opts.logFile.empty()) {
            ok = coreLib::applyLogFlagFile(opts.logFile, errMsg);
        }
        if (!ok) {
            std::fprintf(stderr, "Emulatr: %s\n", errMsg.c_str());
            return 1;
        }
    }

    // ------------------------------------------------------------------
    // Optional: one-shot --dump-disasm <pa>:<count>.  Fires here so the
    // dump sees post-load, post-snapshot-autoload memory contents -- the
    // bytes that the about-to-run CPU will fetch.  Macro expands to a
    // no-op when EMULATR_PA_DUMP is compiled off (Release).
    // ------------------------------------------------------------------
    if (opts.dumpDisasmCount != 0) {
        std::fprintf(stderr,
                     "DEBUG: --dump-disasm pa=0x%016llx count=%llu\n",
                     static_cast<unsigned long long>(opts.dumpDisasmPa),
                     static_cast<unsigned long long>(opts.dumpDisasmCount));
        DUMP_DISASM(mach.memory(), opts.dumpDisasmPa,
                    static_cast<std::size_t>(opts.dumpDisasmCount));
        std::fflush(stderr);
    }

    // DIAGNOSTIC: confirm palBase seeding before run().  Surface both
    // descriptor palBase concepts: initialPalBase (= loadPa, what
    // cpu.palBase is seeded with) and targetPalBase (= firmware-
    // embedded constant, the value the firmware will MTPR to during
    // decompression).  Their split was introduced 2026-05-19 from
    // AXPBox study; see SrmDescriptor in systemLib/SrmLoader.h.
    std::fprintf(stderr,
                 "DEBUG: pre-run palBase=0x%016llx  pc=0x%016llx  palMode=%d  "
                 "srmValid=%d  initialPalBase=0x%016llx  targetPalBase=0x%016llx\n",
                 static_cast<unsigned long long>(mach.cpu().palBase),
                 static_cast<unsigned long long>(mach.cpu().pc),
                 static_cast<int>(mach.cpu().inPalMode()),
                 static_cast<int>(mach.srmDescriptor().valid),
                 static_cast<unsigned long long>(mach.srmDescriptor().initialPalBase),
                 static_cast<unsigned long long>(mach.srmDescriptor().targetPalBase));

    // ------------------------------------------------------------------
    // Run.  CHANGE 2026-06-08 (TEP / Claude, task #10): wall-clock the run
    // and emit a PROFILE line so cold-boot-to->>> throughput is measurable.
    // ------------------------------------------------------------------
    [[maybe_unused]] auto const profT0 = std::chrono::steady_clock::now();
    // try/catch so an exception escaping the run cannot bypass the destructor
    // flush: `mach` is a main()-local, so falling through here (instead of
    // letting the exception propagate to std::terminate) guarantees
    // ~Machine::forceFlush() persists the flash NVRAM. 2026-06-25.
    systemLib::StopReason sr = systemLib::StopReason::HaltedClean;
    try {
        sr = mach.run(opts.maxCycles);
    } catch (std::exception const& e) {
        std::fprintf(stderr,
            "FATAL: exception escaped mach.run() (%s) -- returning cleanly so "
            "~Machine flushes the flash NVRAM\n", e.what());
    } catch (...) {
        std::fprintf(stderr,
            "FATAL: unknown exception escaped mach.run() -- returning cleanly so "
            "~Machine flushes the flash NVRAM\n");
    }
    [[maybe_unused]] auto const profT1 = std::chrono::steady_clock::now();
#if EMULATR_BRINGUP_PROBES
    double             const profSecs    = std::chrono::duration<double>(profT1 - profT0).count();
    unsigned long long const profCycles  = static_cast<unsigned long long>(mach.cpu().cycleCount);
    unsigned long long const profRetires = static_cast<unsigned long long>(traceLib::RetireProfiler::totalRetires());
    std::cout << "\nPROFILE: retires=" << profRetires
              << " cycles=" << profCycles
              << " wall=" << profSecs << "s"
              << "  instr/s=" << (profSecs > 0.0 ? static_cast<unsigned long long>(profRetires / profSecs) : 0ull)
              << "  cyc/s="   << (profSecs > 0.0 ? static_cast<unsigned long long>(profCycles  / profSecs) : 0ull)
              << '\n';
    // WARP accounting (2026-06-30).  cycleCount conflates executed + warped
    // cycles; warpCycles isolates the fast-forwarded (idle/delay) part.
    //   MHz_real = execCycles/wall  -- honest EmulatR throughput (build-relative)
    //   MHz_eff  = cycleCount/wall   -- warp-inflated effective machine speed
    //   K = MHz_eff/MHz_real         -- warp free-time factor (workload-invariant)
    // Guest-invisible; the guest clock stays warp-invariant (see WARP topic).
    unsigned long long const profWarp = static_cast<unsigned long long>(mach.cpu().warpCycles);
    unsigned long long const profExec = (profCycles >= profWarp) ? (profCycles - profWarp) : profCycles;
    double const mhzReal = (profSecs > 0.0) ? (static_cast<double>(profExec)   / profSecs / 1.0e6) : 0.0;
    double const mhzEff  = (profSecs > 0.0) ? (static_cast<double>(profCycles) / profSecs / 1.0e6) : 0.0;
    double const warpK   = (profExec > 0ull) ? (static_cast<double>(profCycles) / static_cast<double>(profExec)) : 1.0;
    std::cout << "WARP-ACCOUNTING: exec_cyc=" << profExec
              << " warp_cyc=" << profWarp
              << "  MHz_real=" << mhzReal
              << "  MHz_eff="  << mhzEff
              << "  K="        << warpK
              << '\n';
#endif

    // ------------------------------------------------------------------
    // Post-mortem dump to stdout.
    // ------------------------------------------------------------------
    systemLib::dumpCpuState(mach.cpu(), std::cout);
    std::cout << '\n';
    systemLib::dumpStopReason(sr, mach.cpu(), std::cout);

    // ------------------------------------------------------------------
    // DIAGNOSTIC: on fault stops, dump 10 instruction words from three
    // candidate addresses to test the "R30 holds palBase implicitly"
    // hypothesis.  If the bytes at (palBase | dispatch_offset) look
    // like real PALcode (typical patterns: BIS for register clears,
    // HW_LD for IPR-side reads, BR/JSR for control flow), the SRM
    // decompressor populated 0x600000+ correctly and the only fix
    // needed is to keep palBase IPR non-zero.  If those bytes are
    // also garbage, the decompressor isn't placing PALcode where we
    // think it is.  Skipped on clean halts -- only useful for fault
    // post-mortems.
    // ------------------------------------------------------------------
#if EMULATR_BRINGUP_PROBES
    if (sr != systemLib::StopReason::HaltedClean) {
        auto const& cpu = mach.cpu();
        auto& memory    = mach.memory();

        struct Probe {
            char const* label;
            uint64_t    pa;
        };

        // Reconstruct the three candidate addresses we want to inspect.
        uint64_t const r30          = cpu.intReg[30];
        uint64_t const faultPc      = cpu.pc;
        // V1 dispatch formula evaluated against R30 in place of palBase
        // IPR.  Hard-coded for the func-0x29 case; if cpu.pc points at
        // a CALL_PAL function we can recompute, but for now use a
        // simple OR with the low-15-bit displacement of the fault PC.
        uint64_t const r30Combined  = (r30 & ~uint64_t{0x7FFF})
                                    | (faultPc & uint64_t{0x7FFE});
        uint64_t const r30PlusFault = r30 + faultPc;

        Probe const probes[] = {
            { "fault_pc                       ", faultPc },
            { "fault_pc + R30  (Tim's idea)   ", r30PlusFault },
            { "(R30 & ~0x7FFF) | low15(faultPc)", r30Combined },
        };

        std::cout << "\nInstruction-stream probes (10 words each):\n";
        for (Probe const& p : probes) {
            std::cout << "  " << p.label << "= 0x" << std::hex
                      << std::setw(16) << std::setfill('0') << p.pa
                      << std::dec << std::setfill(' ') << "\n";
            for (uint64_t i = 0; i < 10; ++i) {
                uint64_t const pa = p.pa + (uint64_t{i} * 4);
                uint32_t word = 0;
                memoryLib::MemStatus const st = memory.read4(pa, word);
                std::cout << "    +" << std::setw(2) << std::setfill('0')
                          << (i * 4) << std::setfill(' ')
                          << "  pa=0x" << std::hex << std::setw(16)
                          << std::setfill('0') << pa
                          << std::setfill(' ');
                if (st == memoryLib::MemStatus::Ok) {
                    std::cout << "  word=0x" << std::setw(8)
                              << std::setfill('0') << word
                              << std::setfill(' ') << std::dec << "\n";
                } else {
                    std::cout << "  <out-of-range>\n" << std::dec;
                }
            }
            std::cout << '\n';
        }
    }
#endif

    return sr == systemLib::StopReason::HaltedClean ? 0 : 1;
}
