<!--
EmulatR V4 -- ES40 Console-PAL Interrupt-Return Null-Dispatch Analysis Briefing
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Commercial use prohibited without separate license.
Contact: peert@envysys.com | https://envysys.com
Purpose: hand-off briefing for the claude.ai web variant to produce the ROOT-CAUSE
ANALYSIS + FIX DESIGN PLAN for the ES40 post-first-tick HALT.  Implementation happens
in Cowork against the live tree.  ASCII(128) only.
-->

# ES40 Interrupt-Return Null-Dispatch -- Analysis Briefing (2026-07-07)

## 0. What this document is for

The web variant's deliverable is an ANALYSIS + FIX DESIGN PLAN, not code.  Determine
WHY the ES40 console PAL returns from the interval-timer interrupt through a null
dispatch pointer, and specify the seam-by-seam fix.  Cowork (the agent with live file
access + the ability to run the emulator) turns the plan into diffs and re-runs the boot.

Treat all excerpts here as a point-in-time snapshot; Cowork is the source of truth for
current file/trace state.  Companion running log: journals/20260707_es40_printf_deadlock_root.md
sections 12-15 (this briefing is the distilled, actionable form of section 15).

## 1. Where the boot now reaches (the win to protect)

ES40 (firmware es40_v7_3.exe, Typhoon, 4 GB) now boots clean through decompression,
hardware init, and the console executive to the CON COM2 message "lowering IPL" -- the
threshold of the P00>>> prompt -- at cycle ~1,239,510,365.  This blows past every prior
wall (the old 0x60222c decompressor panic was at cyc ~12.5M).  Two fixes carried it here
this session: CSERVE 0x66 = get_time (execCserve), and confirmation that the whole
interval-timer interrupt path was already faithful.

## 2. Confirmed FAITHFUL -- do NOT redesign these

Proven correct by the EMULATR_IRQDIAG probe + the DIAG-PC/excAddr capture:

- Interval-timer FIRE: chipsetLib::intervalTimerShouldFire (pure cycle-mask) latches
  MISC<ITINTR> + b_irq<2> every 2^18 cycles.  Works.
- DELIVER gate: Machine::canAcceptInterrupt(22) opens exactly when the SRM enables
  HW_IER<EIEN2> (bit 35).  Proven: one IER write of 0x7effffe000 (ei2=1) at pc=0xec80
  immediately produced an IRQDIAG-DELIVER.
- HW_IER seam: execHwMtpr HW_IER / HW_IER_CM (selector 0x010A/0x010B) -> cpu.ier via
  ierCmIerPortion.  Exercised and correct.
- Divert cause: stageInterruptDivert(cpu, 1<<35) = EI[2] = ISUM bit 35 = IRQ_CLK
  (systemLib/Machine.cpp).  The console correctly dispatched to sys__int_clk and counted
  the tick.
- MTPR_IPL: the PAL handler programs HW_IER itself; V4 honors it.  (Separately, the
  PAL_MTPR::MTPR_IPL enum was corrected 0x0E->0x0F this session -- doc/forward-compat
  only, not on this path.)

Net: interrupt DELIVERY is closed.  The failure is entirely in the interrupt RETURN.

## 3. The failure, fully traced (evidence)

Capture: es40_excaddr.log (EMULATR_DIAG_PCLO=0 PCHI=~0 CYCLO=1239510350 CYCHI=1239510585,
excAddr column added to DIAG-PC).  excAddr held the correct interrupted PC (0x1b7d34) the
entire ISR, until:

    cyc 1239510540  0xda6c  6c441000  HW_LD R02,0(R04)  memAddr=0x1038600  R02 = [0x1038600] = 0
    cyc 1239510547  0xda88  44407102  BIC   R02,#3,R02                      R02 = 0
    cyc 1239510550  0xda94  7be2a000  HW_REI (REGISTER form, Rb=R02)        -> PC = R02 = 0
    cyc 1239510551  0x0     00000000  fault=6 (I-fetch miss at PC 0)        excAddr still 0x1b7d34
    cyc 1239510552  0x8580  ...       trap delivery sets excAddr = faulting PC = 0
    ... second handler recomputes, HW_REI (Rb=R23=0) at 0xd5f0 -> PC 0 again ...
    cyc 1239510579  0x0     00000000  HALT (kFaultHalt, HaltedClean)

Key point: excAddr=0 is a SYMPTOM (the trap sets excAddr=faulting PC after the REI already
jumped to 0).  The PRIMARY fault is the register-form HW_REI at 0xda94 returning through
R02 = 0.

R02 provenance -- a DOUBLY-INDIRECT load:

    R04 = [R21+0x170] = 0x1038000     (a table / dispatch BASE pointer)
    R05 = [R21+0x158] = 0x600         (a vector OFFSET; 0x600 = SCB HW-interrupt region)
    R04 = R04 + R05    = 0x1038600
    R02 = [R04]        = [0x1038600]  = 0     <-- the null the ISR returns through

Ghidra cross-read (Tim): 0xda94 is the console PAL register-form return ("BR r3"); a failed
return-consistency check routes to a deliberate halt stub at 0xb475.  Real PAL substitutes
0xb475; V4 lands at 0x0 only because the register is literally 0.  SAME event: the console
PAL detects a bad return target and halts.

## 4. Memory evidence (decisive)

EMULATR_GMEM_WATCH (GuestMemory.cpp, logs every store overlapping a quadword):
- EMULATR_GMEM_WATCH=0x1038600 -> ZERO stores across the entire boot.
- EMULATR_GMEM_WATCH=0x1038000 -> ZERO stores across the entire boot.

So the dispatch table at 0x1038000 is never written at that PHYSICAL address.  Either it is
never built, or it is built at a DIFFERENT PA than the console PAL's physical HW_LD reads.

Two fill patterns seen, implying two memory regions: [R21+0xf0] read 0xcafebeef (this is the
FORCED ISP-model sentinel V4 returns for reads of 0xBFFC per MemDrainer.h -- flag, likely
incidental), whereas [0x1038600] read 0 (zero-filled DRAM).

## 5. Routine identity notes (partial)

- Runtime 0xda50 = console PAL image offset 0x5a50 (PAL loads at palBase 0x8000; PAL_BASE
  IPR read back 0x8000 at runtime).
- sys__int_post (ev6_osf_pc264_pal.mar:1927) is a DIFFERENT routine -- it is p_temp/PT__
  based and computes the IER mask from a PAL_BASE-relative table.  The V4 trace DOES execute
  that IER-mask code later (runtime 0x11490, HW_MFPR PAL_BASE then shifts), but AFTER the
  fatal REI-to-0.  The fatal routine at 0xda50 is the R21-based path.
- The impure offsets 0x150/0x158/0x170 map by VALUE to cns$f8/f9/f12 in
  ev6_pc264_pal_impure.mar, BUT those symbolic names are NOT referenced in the PC264 PAL
  source, so R21 is NOT confirmed to be the cns$ save-area base.  R21's true target is the
  first unknown to resolve.

## 6. Open questions the analysis must answer

Q1.  What is R21, and what structure does [R21+0x170]=0x1038000 point at -- a System Control
     Block (SCB), a per-CPU PCB, or a console dispatch table?  (0x600 = SCB HW-interrupt
     vector region strongly suggests an SCB / vector table.)
Q2.  Which console/PAL init or registration step is supposed to write [0x1038000+0x600] with
     the interval-timer handler / resume PC, and either
       (a) did V4 diverge/skip before that step ran (a setup or ORDERING gap -- the clock
           tick arriving before the vector is installed), or
       (b) did the console STORE the vector via a VIRTUAL address that V4 maps to a PA other
           than 0x1038000 (a kseg / superpage / SCBB translation mismatch), so the physical
           HW_LD at 0x1038600 reads never-written zero DRAM?
Q3.  Given the answer, what is the minimal, faithful V4 fix -- and does it risk DS10/DS20
     (both currently reach P00>>>)?  Name the seam and the do-no-harm argument.

## 7. Proposed analysis plan (step by step)

STEP A -- Name R21 and the 0x1038000 structure.
  A1. In es40.dec, find the last writer of R21 before cyc 1239510533 (entry to 0xda50).
      Command (Cowork can run): grep the retire stream for "R21 = 0x" in the window
      [1239510365 .. 1239510533]; the producing instruction + source (impure offset, IPR,
      or CALL_PAL arg) names R21.
  A2. In Ghidra, label runtime 0x8000-region offsets: 0xda50 (=img 0x5a50), 0x8580, 0xd5c0,
      0x11490.  Map 0xda50 to its source routine in ev6_vms_pc264_pal.mar or
      ev6_osf_pc264_pal.mar (ES40 personality is OpenVMS -- prefer the VMS file; confirm by
      matching the instruction stream 0xda50-0xda94 given in section 3).
  A3. Resolve [R21+0x170] and [R21+0x158] to their symbolic field names in whatever struct
      A1/A2 identifies (SCB base + vector, PCB + offset, or dispatch-table base + index).

STEP B -- Determine who should populate [0x1038000 + 0x600].
  B1. Search the SRM console C source (PalcodeBitsavers/srmconsole/5.8 and apisrm/ref) for
      the interval-timer / clock interrupt handler REGISTRATION: where the console installs
      its SCB or vector-table entry for the clock (grep: scb, int_clk, ent_int, vector,
      0x600 / 600).
  B2. Establish the expected ORDER: is the vector installed BEFORE "lowering IPL" (so the
      first tick must find it populated), or is the console designed to take an early tick
      with the vector still null (unlikely)?  This decides gap-vs-ordering.

STEP C -- VA/PA reconciliation test (rules Q2a vs Q2b).
  C1. Read the console's SCBB IPR value at runtime (or the register that feeds R21+0x170) and
      compute the VA the console would STORE the vector to.  Compare V4's VA->PA translation
      of that VA against 0x1038000.
  C2. Instrument: add a VIRTUAL-address store watch (or widen GMEM watch to a range around
      the console data segment) to catch the console writing the clock vector at ANY PA.  If
      a store of a valid PC to some PA X occurs and X != 0x1038000, it is a translation
      mismatch (Q2b).  If NO such store occurs anywhere, it is a setup/ordering gap (Q2a).
      Cowork can add a ranged GMEM watch or a "store value == plausible PC && PA in console
      segment" probe.

STEP D -- Classify and design the fix.
  - If Q2a (never built / ordering): the fix is on the DELIVERY-timing or console-init side --
    e.g., confirm the console truly enabled the clock (it did: ei2 set) and find why the
    registration step did not run; the divergence is upstream, not in interrupt handling.
  - If Q2b (VA/PA mismatch): the fix is in V4's address translation for the console data
    segment / SCBB region (kseg or superpage decode) -- name the exact translation seam and
    the HRM-cited correct mapping.
  - Either way: state the do-no-harm argument for DS10/DS20 (why they are unaffected --
    likely because they reach the prompt via a path that does not exercise this exact
    return, or because their console data segment maps identically).

## 8. Reproduction + instrumentation recipe (for Cowork)

Build (relwithdebinfo arms EMULATR_TRACE_HOOKS + the diagnostic layer):
    cd /d/EmulatR/EmulatRAppUniV4/Emulatr && ./tools/build_emulatr.sh relwithdebinfo

Fatal-window instruction+excAddr trace (fast, no --trace):
    cd out/build/relwithdebinfo
    EMULATR_2D_NOOP=1 EMULATR_SPINSKIP=1 EMULATR_NO_PUTTY=1 EMULATR_CONSOLE_MIRROR=1 \
    EMULATR_DIAG_PCLO=0x0 EMULATR_DIAG_PCHI=0xffffffffffffffff \
    EMULATR_DIAG_CYCLO=1239510350 EMULATR_DIAG_CYCHI=1239510585 EMULATR_DIAG_CAP=1000 \
    ./Emulatr.exe --firmware firmware/es40_v7_3.exe --mem 4294967296 \
      --no-autoload --max-cycles 0x50000000 > es40_excaddr.log 2>&1

Memory-store watch on any quadword (no rebuild; env only):
    EMULATR_GMEM_WATCH=0x1038600   (or 0x1038000, or the SCBB-derived PA from Step C)

Deep decoded lookback at the halt (60 retires, via --trace; slower ~30 min):
    add:  --trace es40.dec,es40.machine   then read the "=== RUN END" block at the tail.

Register trace for a specific GPR in a window (names a value's producer):
    EMULATR_DIAG_WREG=21 EMULATR_DIAG_WMIN=1   (logs writers of R21; pair with the CYCLO window)

## 9. Deliverables expected from the analysis

1. R21 + the 0x1038000 structure identity, with the source citation (struct + field names).
2. The name of the console/PAL step that should populate [0x1038000+0x600], and the
   gap-vs-ordering-vs-translation classification (Q2a / Q2b), backed by the Step C evidence.
3. The exact V4 fix seam (file + function + the concrete edit shape) with an HRM / apisrm
   citation, and the DS10/DS20 do-no-harm argument.
4. A test/assertion that proves the fix (ideally: ES40 advances past cyc 1239510580 and the
   console emits the next CON COM2 line after "lowering IPL").

## 10. Standing rules (apply to any implementation that follows)

Discuss before code; header + inline documentation on every change; ASCII(128) only;
include guards not pragma once; doctest CHECK only; hex switch/case labels; surgical Edit
over rewrites; treat V0/V1/V2 and Processor Support as read-only; verify every file write
via bash; bounded trace tails only (never whole-file grep on multi-GB logs).  Full text in
the project conventions / CLAUDE.md.
