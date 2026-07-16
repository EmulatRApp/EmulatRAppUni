<!--
EmulatR V4 -- ES40 REAL_HW (Silicon) "Initializing...." hang: ANALYSIS HANDOFF.
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree.
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Purpose: single seedable hand-off for next-step analysis of the ES40 silicon-mode
LFU-reset "Initializing...." hang.  Web variant produces the design/analysis;
Cowork implements against the live tree.  ASCII(128) only.
-->

# ES40 REAL_HW (Silicon) "Initializing...." Hang -- Analysis Handoff (2026-07-15)

## What this document is for

This is a hand-off, not a change. The deliverable that seeds into it is a
DESIGN / ROOT-CAUSE ANALYSIS of why the ES40, in silicon (REAL_HW) mode, hangs
at "Initializing...." during an LFU reset. Web-variant produces the analysis and
the seam-by-seam edit shape; Cowork (live file access) turns it into diffs,
verifies line numbers, and runs the A/B. Treat every file excerpt and line
number here as a point-in-time snapshot; Cowork is the source of truth for
current file state.

Seed your findings into section 7 (SEED SLOTS) as you iterate.

## 1. The symptom

- Enter the LFU (`UPD> exit` / firmware update reset) to re-initialize the
  in-memory SRM. The console prints "Initializing...." and then HANGS in
  silicon mode. No second boot banner appears.
- The SAME path in ISP mode returns cleanly to `P00>>>` after "Initializing....".
  (ISP run: putty_...164111 returns; silicon run: putty_...203607 hangs.)
- This is a REAL_HW-gated timing path -- it is invisible in the default ISP
  mode, which is why the shipped beta (ISP default) does not hit it.

## 2. Verified current state (from 20260713 LFU spec)

Firmware path:

- `lfu_system_reset()` (LFU.C:722) -> on PC264 the else branch prints
  "Initializing...." -> `krn$_sleep(2000)` -> `outtig(NULL, 0xE00004, 1)`
  (the real reset). In silicon mode the console NEVER reaches the `outtig`
  reset.

Where it spins:

- A timed delay loop at console PC `0x6a4f8 - 0x6a520` (constant caller
  `ra = 0x6a50c`). The loop reads the System Cycle Counter (RSCC) via the stub
  at `0x1b78e8` { `CALL_PAL 0x9d` ; `STQ r0,0(r22)` ; `RET` }, dispatched to the
  PAL RSCC handler at `0xb740 - 0xb770`.
- At the hang, PC samples are ~110/200 at `0x8680`, the rest at `0xb740-0xb770`.
- The loop compares the freshly-read RSCC against a precomputed target
  `r0 = [r29+16]` with `CMPLE` at `0x6a514`.
- Divert cross-pairs (interrupted spin) cluster at `0x1b78b0` (x2876) and
  `0x1b7b20` (x1813), on R23 (the SDE<1> CALL_PAL linkage / shadow register).

The counter model:

- RSCC == `m_cpu.cycleCount` (kCcMultiplier = 1, CpuState.h:281). So the guest's
  "wait until RSCC reaches target" is a wait on the emulator cycle counter.

## 3. Leading hypothesis (to confirm or refute)

The delay loop precomputes a target from the cycle counter, then waits for the
counter to reach it. A cycle-count DISCONTINUITY between the precompute and the
wait (an idle/RSCC warp injecting cycles out of band) makes the target
unreachable in the loop's own frame -> the `CMPLE` never satisfies -> infinite
spin. In short: silicon-mode timing + warp interact so the LFU sleep never
completes.

Note the two suspicions converge: the beta cover letter names the "Typhoon/Titan
chipset," and the RSCC/interval-timer timing model IS part of the Tsunami/Typhoon
(21272) chipset that is still PARTIAL (Cchip interval timer `fireIntervalTimer`
TODO, TsunamiCchip.h; see 20260711 ledger). Titan (21274) is newer still and not
the ES40's chipset -- ES40 is Tsunami/Typhoon -- so Titan is likely a red herring
for THIS hang and should be de-prioritized versus the RSCC/warp timing path.

## 4. The question to answer first

Is the hang caused by (A) the warp/cycle-count discontinuity making the
precomputed delay target unreachable, or (B) an incomplete Tsunami/Typhoon timing
model (interval timer / RSCC) independent of warp? The A/B below separates them.

## 5. Ready next step (already specced -- do not redesign)

`20260713_es40_lfu_rscc_warp_instrumentation_spec.md` already defines the
instrumentation, all `EMULATR_RSCC_DIAG`-gated, for an A/B (warp-on vs warp-off):

- EDIT 1 -- warp ledger: full cycle-discontinuity record at the IDLEWARP site
  (Machine.cpp ~1657-1670; warpCycles accumulator).
- EDIT 2 -- delay-loop capture at PC `0x6a514`: dump {iter, cur=r4=RSCC,
  target=r0, cyc, warpCycles} so target-vs-current is visible per iteration.
- EDIT 3 -- CPU-speed / cycles-per-us calibration capture (pin the calibration
  PC first via the existing DIAG-PC window, then dump).
- EDIT 4 -- make the DIVERT-REI ledger exact (LIFO + full context;
  Machine.cpp:1759-1771).

Run design (section 4 of that spec): capture the delay-loop target, current
RSCC, and warp ledger WITH and WITHOUT warp, at the hang. If warp-off completes
and warp-on hangs with a target the counter jumped past -> hypothesis A
confirmed.

## 6. Out of scope (do NOT chase here)

- Titan (21274) implementation for ES45/DS15/DS25. ES40 is Tsunami/Typhoon;
  Titan is a separate, newer, deliberately-incomplete track.
- The ISP-mode path (returns cleanly; nothing to fix there).
- General decompressor / cold-boot performance (separate lever,
  EMULATR_FAST_DECOMPRESS).

## 7. SEED SLOTS (Tim / web variant fill these as analysis proceeds)

### 7.1 Code path you identified
(Paste the specific silicon-mode code path / seam you traced here.)

### 7.2 Instrumentation run results
(A/B outputs: delay-loop target vs current RSCC, warp ledger discontinuity, at
the hang. Paste the RSCCDIAG-DELAY / warp-ledger rows.)

### 7.3 Root cause (confirmed)
(A vs B from section 4, with evidence.)

### 7.4 Fix design (seam-by-seam)
(The proposed correction: e.g., warp accounting that keeps RSCC monotonic and
consistent with any precomputed delay target, or the Cchip timing-model gap.)

### 7.5 Test / assertion plan
(How a passing run is proven: LFU reset completes, reaches outtig(0xE00004),
returns to P00>>> in silicon mode.)

## 8. Reference journals (live tree)

- 20260713_es40_lfu_rscc_warp_instrumentation_spec.md  (the instrumentation, A/B)
- 20260712_HMDoc-platform-execution-mode-isp-vs-silicon.md  (ISP vs REAL_HW gate)
- 20260711_tsunami_typhoon_reaudit_current_state_ledger.md  (chipset state)
- 20260616_titan_21274_interface.md  (Titan, for context / de-scope)

## 9. Standing rules (apply to all implementation work)

- Discuss before code: non-trivial changes proposed first as prose with file
  paths, line numbers, and the concrete edit shape; wait for approval.
- Documentation at header and source line; no anonymous changes.
- All diagnostic instrumentation behind compile/env gates (here:
  EMULATR_RSCC_DIAG); zero cost when off; never in a release build.
- ASCII(128) only in all file content.
- Best-effort deterministic architecture; name any determinism trade-off.
