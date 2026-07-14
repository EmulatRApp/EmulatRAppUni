<!--
EmulatR V4 -- ES40 LFU "Initializing...." hang: RSCC / warp instrumentation SPEC.
Discuss-before-code artifact.  NO source edited.  ASCII(128) only; hex radix.
Purpose: instrument the silicon-mode LFU-reset delay-loop hang so a warp-on vs
warp-off A/B run reads the verdict directly (is a cycle-count warp inflating the
RSCC-based micro_delay target, or is the resumed native register context
corrupted across the interval-timer interrupt).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
-->

# ES40 LFU "Initializing...." hang -- RSCC / warp instrumentation (SPEC, 2026-07-13)

## 0. Status

Discuss-before-code. This is the design; NO source is edited. On approval the
edits in section 3 land as surgical Edits (all gated behind a new runtime knob
EMULATR_RSCC_DIAG so they are inert unless armed), then the two scripts in
section 4 capture the A/B.

## 1. What we are testing (the question)

PROVEN facts (from the 2026-07-12 silicon runs + source trace):

- The LFU `exit`/`UPD> exit` reset path is lfu_system_reset() (LFU.C:722) ->
  on PC264 the else branch -> "Initializing...." -> krn$_sleep(2000) ->
  console_restart() (PC264.C:371) -> krn$_micro_delay(10000) ->
  outtig(NULL,0xE00004,1).
- In silicon mode the console never reaches the outtig reset. It spins in a
  timed delay loop at console 0x6a4f8-0x6a520 (constant caller ra=0x6a50c) that
  reads the System Cycle Counter via the stub at 0x1b78e8 { CALL_PAL 0x9d ; STQ
  r0,0(r22) ; RET }, dispatched to the PAL RSCC handler at 0xb740-0xb770. At the
  hang, PC samples are 110/200 at 0x8680 + the rest 0xb740-0xb770.
- The delay is DELTA-based: the loop reads current RSCC into r4 = [r29+8],
  compares against the precomputed target r0 = [r29+16] (CMPLE at 0x6a514).
- ISP run (putty_...164111) returns to P00>>> after "Initializing...."; the
  silicon run (putty_...203607) hangs with no second boot banner.

OPEN question (Tim, 2026-07-13): the run warped during boot; warp advances
m_cpu.cycleCount, and RSCC == cycleCount (kCcMultiplier=1, CpuState.h:281), so
warp alters the guest-visible RSCC. Two candidate mechanisms:

  (H-warp)  A cycle-count warp STRADDLED the CPU-speed / cycle-time calibration
            window, inflating the calibrated cycles-per-microsecond, so
            micro_delay(N us) computes a target N that is astronomically large
            -> the loop never reaches it -> hang. (A one-time offset that ends
            before any delta window CANCELS in delta arithmetic; only a straddle
            or a calibration that measures RSCC against a warp-immune reference
            makes it a factor.)
  (H-corrupt) The delay loop's compare operands (r0 target, r3, r4 current)
            live in registers the interval-timer divert->HW_REI fails to
            preserve, so the exit condition is never satisfied.

The DIVERT-REI ledger cannot decide this: DS20 (boots to P00) emits 19,449
DIVERT-REI mismatches vs 8 clean; ES40-silicon emits 4,717. Raw mismatch count
is INVERSELY correlated with the failure, because the 2-slot FIFO keyed on
resumePc cross-pairs diverts at a repeatedly-interrupted spin PC (0x1b78b0
x2876, 0x1b7b20 x1813) and R23 (the SDE<1> CALL_PAL linkage/shadow register,
PalEntries.cpp:1234) dominates the "mismatches" -- an expected shadow-bank
difference, not corruption. So the ledger must be made EXACT before it can
convict or clear H-corrupt.

## 2. Design principle

Add three read-only captures, all gated behind ONE new env knob
EMULATR_RSCC_DIAG (like EMULATR_IRQDIAG: a getenv-once static). They emit to
stderr (the console-mirror log) and never alter execution. A separate knob is
NOT needed for the A/B: the A/B is driven purely by whether EMULATR_IDLEWARP is
set (section 4). The captures expose warpCycles at each RSCC-consuming site so a
warp-free "clean RSCC" = cycleCount - warpCycles is visible next to the raw
value the guest used.

Existing scaffolding to reuse (do NOT duplicate):
- m_cpu.warpCycles accumulator already tracks total injected cycles (Machine.cpp
  :1659 IDLEWARP; PipelineDriver.h:309/347 the quarantined warps).
- The 32-int-reg dump pattern (PipelineDriver.h:378-389 C970DUMP) is the model
  for the delay-loop and calibration dumps.
- EMULATR_DIAG_PCLO/PCHI + CYCLO/CYCHI retire-window trace (DIAG-PC line) exists
  for pinning the calibration PC before a dedicated dump is added.

## 3. The edits (surgical; all EMULATR_RSCC_DIAG-gated; for approval)

### EDIT 1 -- warp ledger (full discontinuity record)

FILE: systemLib/Machine.cpp, IDLEWARP site (currently 1657-1670).
SHAPE: after the existing `m_cpu.cycleCount = m_systemClock;` (1660), add an
EMULATR_RSCC_DIAG-gated, UN-throttled line (keep the throttled IDLETICKWARP):

    #if EMULATR_BRINGUP_PROBES
    if (s_rsccDiag) {   // static getenv("EMULATR_RSCC_DIAG") once
        std::fprintf(stderr,
            "WARPLEDGER site=idle from=%llu to=%llu delta=%llu warpTot=%llu "
            "pc=0x%llx\n",
            (unsigned long long)c0,
            (unsigned long long)m_cpu.cycleCount,
            (unsigned long long)(m_cpu.cycleCount - c0),
            (unsigned long long)m_cpu.warpCycles,
            (unsigned long long)idlePc);
        std::fflush(stderr);
    }
    #endif

Rationale: every idle warp jump is now a single greppable WARPLEDGER row with a
running warpTot. (The quarantined PipelineDriver.h warps already print
TICKWARP/RSCCWARP/SPINWARP rows; no change there -- they are off in this A/B.)
Header block + inline comment per ADR-0001.

### EDIT 2 -- delay-loop + clean-RSCC capture (the target vs current)

FILE: pipelineLib/PipelineDriver.h, beside the existing 0x7bef0/C970 dumps
(~368-391).
SHAPE: a new EMULATR_RSCC_DIAG-gated block that fires at the delay-loop compare
PC 0x6a514 (at entry: r4 = current RSCC just read, r0 = precomputed target),
throttled to the first N and then 1/1024:

    #if EMULATR_BRINGUP_PROBES
    if (s_rsccDiag && cpu.pcAddr() == 0x000000000006a514ull) {
        static uint64_t n = 0;
        if (n < 64 || (n & 0x3FFull) == 0) {
            uint64_t const clean = cpu.cycleCount - cpu.warpCycles;
            std::fprintf(stderr,
                "RSCCDIAG-DELAY pc=0x6a514 iter=%llu cur=r4=0x%llx "
                "target=r0=0x%llx r3=0x%llx cyc=%llu warpTot=%llu cleanRSCC=%llu\n",
                (unsigned long long)n,
                (unsigned long long)cpu.intReg[4],
                (unsigned long long)cpu.intReg[0],
                (unsigned long long)cpu.intReg[3],
                (unsigned long long)cpu.cycleCount,
                (unsigned long long)cpu.warpCycles,
                (unsigned long long)clean);
            std::fflush(stderr);
        }
        ++n;
    }
    #endif

Reads: target (r0) vs current (r4). If target is astronomically large and cur
climbs but never reaches it -> H-warp (inflated target). If target is sane and
cur JUMPS or resets across ticks -> H-corrupt. cleanRSCC vs cyc shows how much
warp is baked into the counter the guest is polling.

### EDIT 3 -- CPU-speed / cycle-time calibration capture

The calibration PC is not yet pinned in the guest image. Pin it FIRST with the
existing DIAG-PC window (no code): run once with
EMULATR_DIAG_PCLO/PCHI bracketing the kernel-init band around
krn$_timer_set_cycle_time (kernel.c:~1860) / timer_check, read the DIAG-PC rows,
and identify the PC where the calibrated cycles-per-microsecond is stored. THEN
add a dump mirroring EDIT 2 at that PC printing {cyc, warpCycles, cleanRSCC, the
computed cycles/us register}. Deferred to a second edit once the PC is known;
recorded here as TODO(rscc-cal-pc).

### EDIT 4 -- make the DIVERT-REI ledger EXACT (LIFO + full context)

FILE 4a: systemLib/Machine.cpp:1759-1771 (divert record).
FILE 4b: palBoxLib/grains/PalEntries.cpp:2262-2317 (HW_REI compare).
SHAPE:
  - Replace the 2-slot FIFO with a small LIFO STACK (depth 8) of pending
    diverts; each entry gets a monotonic id, savedPc, cyc, palMode, SDE, AND a
    full 32-int-reg snapshot (not just the 10).
  - At HW_REI to native (resumeInPal==false), pop the NEWEST unmatched pending
    whose savedPc == resumePc (interrupts nest LIFO, so newest-first pairing is
    the correct match and kills the FIFO cross-pairing that made DS20 show
    19,449 false mismatches).
  - On a matched pair, diff all 32 registers; emit one throttled
    DIVERT-REI-EXACT row per differing register with {id, rn, was, now,
    savedPc, divertCyc, reiCyc, sde_at_divert, sde_at_rei}.
Rationale: separates real corruption from pairing artifact, and captures the SDE
state on both ends so an unpaired shadow-swap (H-corrupt via SDE asymmetry) is
directly visible. Keep it EMULATR_RSCC_DIAG-gated so it is off by default.

### Knob wiring

Add, once, near the other diag statics (Machine.cpp + PalEntries.cpp +
PipelineDriver.h each get a file-local `static bool const s_rsccDiag =
std::getenv("EMULATR_RSCC_DIAG") != nullptr;`). All four edits compile only under
EMULATR_BRINGUP_PROBES (already the diagnostic build's define) and run only when
s_rsccDiag is true. Zero effect on release/normal runs.

## 4. The A/B run scripts (section-4 deliverables)

Two self-locating scripts in tools/. Both cold-boot ES40 in SILICON mode with
EMULATR_RSCC_DIAG=1 and tee a case-labeled log + auto-extract a .summary of the
WARPLEDGER / RSCCDIAG-DELAY / DIVERT-REI-EXACT rows for a one-glance A/B diff.
They wrap tools/run_es40_showdev.sh (reuse its ES40 silicon setup, disk attach,
PuTTY, restore-on-exit) so no setup logic is duplicated.

  tools/run_es40_rscc_ab_warp.sh    -- CASE A: EMULATR_IDLEWARP=1 (reproduces the
                                       failing run; warp baked into RSCC).
  tools/run_es40_rscc_ab_nowarp.sh  -- CASE B: no EMULATR_IDLEWARP (faithful
                                       timing; RSCC == retired cycles). Larger
                                       MAXCYC since nothing is skipped.

Operator note: the boot-phase signals (calibration constant, generic micro_delay
targets, warp ledger) are captured WITHOUT entering LFU -- they prove or refute
H-warp on their own. To reproduce the exact "Initializing...." hang, reach P00
then type `lfu` and `exit` while the diag is armed; the RSCCDIAG-DELAY rows at
0x6a514 then show the console_restart delay's target vs current.

## 5. Reading the verdict

- H-warp CONFIRMED if: CASE A shows an inflated RSCCDIAG-DELAY target (and/or an
  inflated calibration cycles/us) that CASE B does not, and CASE B's
  console_restart delay completes (reaches the outtig reset / returns to P00).
- H-corrupt CONFIRMED if: the target is identical and sane in both cases but the
  DIVERT-REI-EXACT diff shows the delay loop's live registers (r0/r3/r4) change
  across a matched divert->REI pair -- i.e. real, not the old FIFO artifact.
- Both can be true; the two captures are independent and run in one build.

## 6. Do-no-harm

All edits are EMULATR_BRINGUP_PROBES-compiled and s_rsccDiag-gated -> inert in
normal/release runs and in any run that does not export EMULATR_RSCC_DIAG. No
control-flow change; reads only. DS10/DS20 unaffected. ASCII(128), include
guards, hex radix honored.
