<!--
EmulatR V4 -- INSTRUCTION TO COWORK: clean distributable EmulatR.exe.
Establish output-stream hygiene so a release run is silent of diagnostic kruft:
guest console pristine, non-diagnostic init/lifecycle journaled to stdout
(redirectable to a log), diagnostics gated off in release.  Target: a weekend
distributable image/environment.  2026-07-10.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands: propose
prose + verify line numbers BEFORE any edit.  FAITHFUL implementation.
[LOCATE] = point-in-time, verify against the live tree.  [CONFIRM] = confirm
the fact before relying on it.
-->

# COWORK INSTRUCTION: clean distributable EmulatR.exe -- output hygiene (2026-07-10)

## Goal

A distributable EmulatR.exe that, run with an empty environment, produces:
  - a PRISTINE guest console (only the machine's own bytes: SRM banner -> P00>>>),
  - a clean OPERATIONAL JOURNAL on stdout (init/lifecycle only), redirectable to
    a log file for the distribution,
  - ZERO diagnostic kruft (no probe/trace/per-dispatch spew) anywhere.

The operator connects PuTTY/plink to the console and sees an Alpha console, not
EmulatR internals.  The journal log is for support/repro, separate from the
console.

## The three-stream model (the invariant to enforce)

Every host-facing write must belong to exactly ONE of these, and they must not
cross:

  1. GUEST CONSOLE -- the emulated UART (COM1 0x3F8 / COM2 0x2F8, via
     EMULATR_CONSOLE_PORT / the console sink).  Carries ONLY guest-emitted bytes.
     NEVER emulator text.  This is the operator-facing stream.
  2. OPERATIONAL JOURNAL -- stdout.  Emulator lifecycle/init only (see T3).
     One structured line per event.  Redirectable to a log file.  NEVER
     per-instruction / per-dispatch / per-fault / per-translation output.
  3. DIAGNOSTICS -- stderr and/or compile-guarded sinks.  Silent by default in a
     release build.  NEVER on the guest console, NEVER on the journal.

The bug pattern to eliminate: any unguarded emulator write that lands on the
guest console (corrupts the machine's stream) or on stdout as normal operation
(kruft the distributable should not emit).

## Tasks (discuss-before-code: propose + verify, THEN implement)

### T1 -- Audit every unguarded host-output write

Grep the tree (excluding test-only and V0/V1/V2 read-only trees) for:
  std::cout, std::cerr, std::clog, printf, fprintf, fputs, puts, putchar,
  fwrite(.., stdout), std::fwrite, and (Qt) qDebug, qInfo, qWarning,
  QTextStream(stdout), QTextStream(stderr).
Classify each hit:
  (C) guest-console output   -- should be on the console sink, verify it is.
  (J) operational journal    -- legitimate init/lifecycle, keep on stdout.
  (D) diagnostic             -- must be gated or on stderr/diag sink.
SPECIAL ATTENTION (the concern that started this): execCserve, the CALL_PAL 0x09
dispatch, and the dispatch table [LOCATE palBoxLib/grains/PalEntries.cpp
execCserve + grainFactoryLib/generated/DispatchTables.cpp].  Confirm whether
CSERVE 0x66 or its routing writes stdout as NORMAL operation (not behind a
guard).  Report the fd/stream each hit targets.

### T2 -- Enforce the separation

  (C) guest-console writes -> route ONLY to the console UART sink
      [LOCATE the THR-emit path / ConsoleUartCom2 / PlatCap console binding].
      Confirm no console byte path falls through to stdout/stderr.
  (D) diagnostics -> behind the appropriate compile guard (EMULATR_BRINGUP_PROBES
      etc.) OR routed to stderr / a dedicated diag sink.  Any unguarded stdout
      write that is per-instruction/per-dispatch/per-fault/per-translation is
      kruft: gate it or move it off stdout.
  (J) journal -> the single stdout journal stream, content per T3.
Use the existing file-sink precedent (coreLib::logFaultEvent -> logs/faults.log)
as the model for "diagnostics go to their own sink, not the console/journal."

### T3 -- Define the operational-journal content (what stays on stdout)

INCLUDE (INFO level, one structured line each, emitted once):
  build/version banner; ini loaded + path; model + memorySize resolved; chipset
  geometry / AAR tiling result; ROM image loaded + load base; PALcode base;
  console port bound (which port); CPU start; clean shutdown; fatal errors with
  cause.
EXCLUDE (never on the journal): anything per-instruction, per-dispatch,
per-fault, per-translation, per-CSR; probe output; trace spew; anything that
fires more than once per boot phase.

### T4 -- Release build cleanliness

  - Confirm the distributable is a Release CMake config with ALL diagnostic
    compile guards OFF: EMULATR_BRINGUP_PROBES=OFF, EMULATR_MEMDIAG=OFF,
    EMULATR_IRQDIAG=OFF, [CONFIRM the full guard list from CMake].
  - Confirm every runtime env knob (EMULATR_*_TRACE, EMULATR_*_WATCH,
    EMULATR_*_DIAG, etc.) DEFAULTS to off.  A distributable must be silent with
    an empty environment -- no env setup required for a clean run.
  - Verify guarded blocks use the #ifdef + ((void)0) form, not a runtime
    if(getenv(..)) compiled in unconditionally (the latter ships dormant-but-
    present and can still touch I/O).

### T5 -- The stdout->log mechanism (propose; Tim decides)

  Option A (app-owned, recommended for a distributable): EmulatR.exe opens a
    timestamped journal log beside the exe by default
    (EmulatR_YYYYMMDD_HHMMSS.log); the stdout journal is tee'd there.
    --log <path> overrides; --no-log gives pure stdout for interactive use.
    Self-contained; the operator just runs the exe.  [LOCATE AppOptions for the
    flag plumbing.]
  Option B (redirect, zero code change): keep stdout as the journal; ship a
    launcher (run_emulatr.cmd) that does
        EmulatR.exe %* > logs\EmulatR_%DATE%_%TIME%.log 2>&1
    The zero-risk weekend fallback.
Recommendation: A for operator-friendliness; B if the weekend clock is tight.
Either way, stderr can go to the same log or a sibling .err log -- Tim's call.

## Acceptance (the clean-run test -- must pass before distribution)

Release exe, EMPTY environment, PuTTY on the console port:
  1. Console stream: SRM banner -> P00>>> with ZERO emulator text interleaved.
  2. Journal (stdout/log): only the T3 INFO lines; grep the log for the T1
     diagnostic symbols/strings -> zero hits; no per-dispatch/per-fault spew.
  3. stderr: quiet (only genuine warnings/errors, if any).
  4. No env vars set; the run is clean without any.
Capture the console log and the journal log as acceptance artifacts.

## Do-no-harm gate

Output routing touches shared paths (console sink, dispatch).  Gate any commit:
full suite + DS10 + DS20 + ES40 boot-to-P00 green.  Additionally re-run DS10 and
DS20 and confirm their console banners are byte-identical before/after -- they
share the console-emit path and must not regress.

## Scope (weekend-sized -- do not over-build)

IN: T1 audit, T2 reroute/gate the kruft, T3 journal content, T4 release-guard +
env-default verification, T5 log mechanism, the acceptance run.
OUT (defer, do NOT start this weekend): a full leveled-logging framework; per-
subsystem log categories; structured/JSON logging.  If T1 finds scattered cout
with no central logger, the minimum viable fix is reroute+gate to satisfy the
acceptance test -- note the framework as a follow-up task, do not build it now.

## For ongoing bring-up (CSERVE 0x66 and beyond)

CSERVE 0x66 identification/instrumentation, and every future probe, MUST follow
this discipline so bring-up never recontaminates the distributable: compile-
guarded (EMULATR_BRINGUP_PROBES), output to the diag sink or stderr -- NEVER the
console, NEVER the stdout journal.  If a runtime toggle is wanted, put the
env-var activation INSIDE the compile guard, not standalone.  The 0x66 work is a
separate brief; this instruction only sets the rule it must obey.

## Packaging adjacent (flag, not scope)

For the image to run on a clean host: bundle Release Qt DLLs, the ROM/PAL
images, a default ini, and the console-port setup; confirm no absolute dev paths
(D:/EmulatR, build dirs) are baked into the exe or default ini.  Full packaging
is its own task -- flag it if the T-work surfaces hardcoded path assumptions.

## References

[LOCATE] (verify live): console UART sink / ConsoleUartCom2 / PlatCap console
binding + THR-emit path; palBoxLib/grains/PalEntries.cpp execCserve; CALL_PAL
0x09 path; grainFactoryLib/generated/DispatchTables.cpp dispatch; AppOptions
(--log/--no-log/--console-port); CMake diagnostic-guard option list.
Precedent: coreLib::logFaultEvent -> logs/faults.log (existing dedicated file
sink; model for diagnostics-off-the-console).
Related: 20260710_es40_memtest_acv_RESOLVED_aar_asiz_and_tiling.md (current
frontier); the CSERVE 0x66 brief (separate, forthcoming).
Memory: [[emulatr-es40-diag-knobs]], [[deliver-bash-as-scripts]],
[[verify-webchat-claims-vs-live-tree]].
