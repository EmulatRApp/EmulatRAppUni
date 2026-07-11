<!--
EmulatR V4 -- Output-hygiene brief, T1 audit RESULTS.  Read-only inventory of
every host-facing write in the buildable V4 app tree, classified against the
three-stream model (guest console / operational journal / diagnostics), with the
target fd of each and the guard posture.  Feeds T2-T5.  2026-07-10.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
[LOCATE] = point-in-time; verify against the live tree before editing.
Brief: 20260710_cowork_clean_distributable_output_hygiene.md.
-->

# Output hygiene -- T1 AUDIT RESULTS (2026-07-10)

## Scope + method

Read-only grep of the buildable V4 app tree (EmulatRAppUniV4/Emulatr), EXCLUDING
tests/, generated/, build/out/RelWithDebInfo/Debug, and the V0/V1/V2 +
"Processor Support" read-only trees.  Symbols swept: std::cout/cerr/clog,
printf/fprintf/fputs/puts/putchar, std::fwrite, qDebug/qInfo/qWarning,
QTextStream.  Each hit classified (C) guest-console / (J) operational-journal /
(D) diagnostic, with its target fd.

## Inventory (counts, app source only)

    symbol            hits   target
    std::cout          12    stdout   (all in main.cpp)
    std::cerr           2    stderr   (traceLib/PaDump.h dump macros)
    std::clog           0    --
    printf              4    stdout   (main.cpp)
    fprintf           244    stderr 232 / stdout 0 / FILE* 7 / no-op-macro 1
    fputs               3    stdout 2 (help) / stderr 1 (help-on-error)
    puts / putchar    1/4    stdout   (main.cpp)
    std::fwrite         2    FlashRom->file (NVRAM) ; StdoutConsoleBackend->stdout
    qDebug              5    Qt handler (stderr default)
    qWarning            4    Qt handler (stderr default)
    qInfo / QTextStream 0    --

HEADLINE: there are ZERO fprintf(stdout).  The stdout surface is essentially
main.cpp alone, plus one guest-console backend (below).  This is a small,
tractable reroute+gate job, not a refactor -- matches the "do not build a logging
framework" scope.

## Findings

F1 -- SPLIT GUEST CONSOLE (the one real cross-stream violation).
  deviceLib/StdoutConsoleBackend.cpp:29  std::fwrite(data,1,len,stdout).
  global_ConsoleManager.cpp:33 lazily registers StdoutConsoleBackend as "OPA0"
  (an IConsoleDevice "for V4 SRM bring-up") so CSERVE terminal I/O has a live
  device.  Meanwhile the emulated UART (COM1 0x3F8 / COM2 0x2F8) is routed to the
  TCP console port (EMULATR_CONSOLE_PORT / PuTTY).  Net: GUEST console bytes can
  reach the operator two ways -- UART->TCP (clean) and CSERVE/OPA0->STDOUT (mixes
  guest bytes into the journal fd).  This is exactly stream (C) landing on stream
  (J)'s fd.  T2 must route OPA0/StdoutConsoleBackend to the SAME console sink as
  the UART (or back the CSERVE console I/O with the UART device), never stdout.
  [CONFIRM whether the ES40/DS boot actually exercises OPA0 CSERVE console I/O or
  only the UART; if OPA0 is dead on these paths it is latent, not active -- but it
  ships present and must not default to stdout.]

F2 -- UNCONDITIONAL end-of-run diagnostic dumps on the journal fd (main.cpp).
  All to std::cout, NOT gated:
    - 705-735 PROFILE + WARP-ACCOUNTING (deliberately moved OUT of
      EMULATR_BRINGUP_PROBES on 2026-06-30 as an always-on perf gauge).
    - 738-742 post-mortem dumpCpuState + dumpStopReason.
    - 783-805 "Instruction-stream probes (10 words each)" diagnostic dump.
  Classification (D), currently on (J)'s stdout.  For a clean distributable these
  are kruft.  T2/T3 call: PROFILE is one exit-time line (Tim may KEEP it as a
  release perf gauge, or gate behind --profile); the cpu-state / stop-reason /
  instruction-probe dumps are per-stop diagnostics and belong on stderr or behind
  a flag, off the journal.

F3 -- 232 fprintf(stderr) diagnostics (D).  Correct stream, but T4 posture
  matters.  Compile-guard sites present: EMULATR_BRINGUP_PROBES x55,
  EMULATR_MEMDIAG x15, EMULATR_DIAG x15, EMULATR_IRQDIAG x4, EMULATR_TRACE x2.
  BUT 23 files emit fprintf(stderr) from inside runtime if(getenv(...)) blocks --
  the "dormant-but-present in release" pattern the brief flags (compiled in, can
  still touch I/O when an env var is set).  Concentrations (top files):
  Machine.cpp 29, main.cpp 23, MemDrainer.h 21, PalEntries.cpp 19,
  PipelineDriver.h 18, TsunamiPchip.h 18, BreakpointSink.cpp 15,
  DecListingSink.cpp 14, FlashRom.cpp 10, Uart16550.h 8, Pic8259Pair.h 7.
  T4: confirm these are silent by default (env off) and consider moving the
  getenv-gated probes inside the compile guard so release ships them out entirely.

F4 -- Qt logging (D).  deviceLib/SRMEnvStore.cpp qDebug x5 fires on EVERY SRM env
  get/set/save/load (per-operation spew); PlatformConfig.cpp:229 qWarning is a
  once-per-boot fallback ("using built-in default DS10 manifest").  Qt routes
  these to stderr / the message handler.  T4: build release with
  QT_NO_DEBUG_OUTPUT (drops qDebug) and/or install a qInstallMessageHandler that
  sends Qt output to the diag sink, not the console/journal.

F5 -- Dedicated diagnostic FILE* sinks (D), already off-console (good precedent).
  eBoxLib/IntArith.cpp:1559 (s_rpccLogFp), pipelineLib/PipelineDriver.h:790-809
  (lexLog), traceLib/RetireProfiler.h:139/155 (f).  These follow the
  coreLib::logFaultEvent -> logs/faults.log model.  Keep the model; ensure they
  are gated/opt-in in release.  (tools/host_decompressor/src/decomp.h:69
  `#define fprintf(x,y)` is a no-op macro in a host tool -- not a write.)

## Operational journal already exists: spdlog

config/LoggingInit.cpp + config/EmulatorSettings.h: the "[ts] [info] ..." lines
(e.g. "platform latched: model=ES40 ...") are spdlog, with per-component loggers
(spdlog::get(name)) and levels from EmulatorSettings.  So T3 is mostly
CONFIGURATION, not new plumbing: point spdlog's default sink at stdout, default
level=info, and make sure the T3 INFO content (build/version, ini path, model +
memSize, AAR tiling result, ROM/PAL base, console port bound, CPU start, clean
shutdown, fatal cause) is logged at info and nothing per-instruction is.
[LOCATE the spdlog sink construction + default level to confirm stdout vs stderr.]

## Special attention (the concern that started this): execCserve / CALL_PAL 0x09

palBoxLib/grains/PalEntries.cpp: all execCserve prints are fprintf(STDERR) AND
under `#if EMULATR_BRINGUP_PROBES` (e.g. the "CSERVE Defaulted - UnImplemented"
line at :617-619).  So the CSERVE 0x66 spew we have been watching is
compile-guarded and STDERR -- silent in a release build (probes off), never on
the console or journal.  grainFactoryLib/generated/DispatchTables.cpp (the
CALL_PAL 0x09 dispatch) contains no host-output writes.  VERDICT: execCserve is
already compliant; the 0x66 bring-up work only needs to KEEP using the guarded
stderr/diag path (per the brief's "ongoing bring-up" rule).

## Non-issues (do not touch)

- chipsetLib/FlashRom.cpp:292 std::fwrite -> the flash/NVRAM backing FILE (disk
  persistence), not a console/journal stream.
- main.cpp fputs help text (125 stdout / 130 stderr) -- user-invoked --help only.

## Net assessment + what T2-T5 inherit

The distributable is much closer than feared: no fprintf(stdout), the journal is
already spdlog, and the CSERVE dispatch is already guarded.  The concrete work:
  T2: (a) reroute OPA0/StdoutConsoleBackend off stdout onto the console sink
      (F1); (b) gate/move the main.cpp end-of-run cpu-state/stop/probe dumps off
      stdout (F2); (c) decide PROFILE's fate (keep as release gauge vs --profile).
  T3: configure spdlog (sink=stdout, level=info) + confirm the INFO line set.
  T4: verify env knobs default off; QT_NO_DEBUG_OUTPUT in release; consider
      folding the 23 getenv-gated stderr probes inside compile guards.
  T5: log mechanism (Option A app-owned --log vs Option B launcher redirect).
Acceptance run validates on DS10/DS20 now (console banner byte-identical
before/after); the full ES40 banner->P00 clean run is gated on the 0x66 fix
(task #12), independent of this routing work.

## Status

T1 (audit): COMPLETE (this journal).  T2-T5: pending, ready to start.  No source
touched.
