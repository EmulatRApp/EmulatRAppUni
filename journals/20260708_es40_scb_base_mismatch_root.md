<!--
EmulatR V4 -- ES40 Post-First-Tick Halt: SCB Base-Mismatch ROOT CAUSE (2026-07-08)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: record the confirmed root cause of the ES40 interval-clock null-dispatch
halt (task #29), the full evidence chain, the hypotheses RULED OUT (do not
re-chase), the diagnostic instrumentation added, and the fix seam + next step.
ASCII(128) only.  Discuss-before-code stands.
-->

# ES40 Post-First-Tick Halt -- ROOT CAUSE: SCB Base Mismatch (2026-07-08)

## TL;DR

The SRM console builds its System Control Block (SCB) correctly, at physical
`0x28000`, IDENTICALLY on ES40 and DS20.  The bug is only in the base pointer the
PAL uses to find it.  On ES40 the PAL-visible SCB base (`impure+0x170`) is
`0x1038000` = spurious `phys_base 0x1010000` + `0x28000`; on DS20 it is `0x28000`
(phys_base `0`).  So the ES40 PAL dispatches the interval-clock interrupt through
`0x1038600`, which is `0x1010000` ABOVE the real (filled) SCB and reads back zero
-> `HW_REI` to PC 0 -> `kFaultHalt`.  DS20's base matches its fill and boots to
`>>>`.

Root cause = a spurious `+0x1010000` phys-base offset applied to the SCB base
pointer on ES40 but NOT to where the console actually lays the table down (low,
identity-mapped).  It is NOT an MMU / translation defect.

## The halt (unchanged from the 2026-07-07 briefing)

ES40 (es40_v7_3.exe, Typhoon, 4 GB) boots clean to CON COM2 "lowering IPL"
(cyc ~1.239B).  After the first interval-timer tick the console PAL return path
at runtime `0xda50` does:

    HW_LD R04, 0x170(R21)   R04 = 0x1038000     (SCB base, from impure)
    HW_LD R05, 0x158(R21)   R05 = 0x600         (SCB$V_INTV = interval clock)
    ADDQ  R04, R05, R04      R04 = 0x1038600
    HW_LD R02, 0x000(R04)    R02 = [0x1038600] = 0   <-- null clock vector
    BIC   R02, #3, R02       R02 = 0
    HW_REI (Rb=R02)          -> PC = 0 -> HALT (kFaultHalt, HaltedClean)

`SCB$V_INTV = 0x600` (interval clock) confirmed in
`Processor Support/.../srmconsole/ALPHASCB_DEF.SDL:129`.

## How the SCB is really built (SRM console source)

- `scb_init()` (`apisrm/.../srmconsole/IE.C:984`): `scb = dyn$_malloc(512*16, ...)`,
  then a loop fills all 512 hardware SCB entries with `common_isr` + `&scbv[i]`.
  THIS is the ~8 KB fill that must populate `SCB[0x600]`.
- `int_vector_set()` (`IE.C:690`) writes only the SOFTWARE table `scbv[]`
  (handler registry).  `initialize_hardware()` (`pc264_init.c:472`) and
  `krn$_timer()` call it -- they never touch the hardware SCB.  So a correct
  clock registration in `scbv[]` is useless if the hardware SCB entry at `0x600`
  does not hold `common_isr`.

## Decisive evidence (the DS20 differential)

Instrumented with the GMEM range watch (`memoryLib/GuestMemory.cpp`,
`EMULATR_GMEM_WATCH_LO/HI`) and the impure-slot watch (`EMULATR_GMEM_WATCH`):

- ES40  `[0x1038000,0x1039000)`  -> ZERO stores across the whole boot.
- ES40  impure `0x7170` (base cache) -> value `0x1038000`.
- ES40  `[0x28000,0x2a000)`      -> 12,288 sequential fill stores (`0x28000`,
                                     `0x28004`, ...).  scb_init DID run and fill,
                                     LOW.
- DS20 (clean, ini model=DS20) impure `0x7170` -> value `0x28000`.
- DS20  `[0x28000,0x2a000)`      -> 12,288 sequential fill stores (identical).
- DS20 boots to `>>>`.

Conclusion: both consoles build the SCB at PA `0x28000`.  ES40's PAL base is
`0x1038000` (= `0x1010000 + 0x28000`); DS20's is `0x28000` (= `0 + 0x28000`).
The `+0x1010000` is ES40-specific and is the entire defect.

## RULED OUT -- do NOT re-chase (each killed by evidence)

1. **SPE / kseg / superpage READ decode.**  The PAL reads the SCB via a PHYSICAL
   `HW_LD` at `0x1038600`; the earlier "web analysis" B1/B2 fixes and the
   VA<45:44> SPE[2] mask were already correct.  The read side is not the bug.
2. **"Q2a -- SCB vector never installed."**  FALSE.  scb_init runs and fills the
   SCB (12,288 stores) -- just at PA `0x28000`, not where the ES40 PAL looks.
3. **Console-client / PuTTY gating.**  The halt reproduces identically with PuTTY
   attached (`es40_putty.log`): same base cache, same empty `0x1038xxx`, same
   `IRQDIAG-DELIVER` at cyc 1239510365.  Not a headless artifact.
4. **VPTB=0 / page-table double-miss / EV6 walk.**  A red herring.  `va_ctl=2`
   (VPTB=0) and the near-null VPTE double-miss storm are SHARED by DS20, which
   BOOTS -- so they are survivable, not the halt cause.  The console never
   executes `CALL_PAL MTPR_VPTB` (env `EMULATR_VPTB_DIAG` -> zero hits on ES40 and
   DS20).  V4's software-managed-TLB model (PALcode walks, `HW_MTPR DTB_PTE`
   refill) is faithful; do NOT add a hardware page walker (EV6 has none).
5. **Missing PCI / device enumeration (DS10 precedent).**  `initialize_hardware()`
   is pure `int_vector_set()` registration -- no PCI probe, no device dependency.

## Root cause, precisely

The console lays its data (heap, SCB) in LOW, identity-mapped memory (data VAs
seen faulting/walking were `0x1b8xxx`, `0x207xxx`, `0x181xxx`; the SCB fill lands
at PA `0x28000`).  It computes the PAL-visible SCB base as `phys_base + 0x28000`.
On DS20 `phys_base = 0` (base matches the fill).  On ES40 `phys_base = 0x1010000`,
so the base pointer is `0x1010000` above the actual SCB.  The `0x1010000` is
computed by the GUEST console from platform data V4 supplies (it is NOT a V4
literal -- grep of coreLib/systemLib/deviceLib/palBoxLib/chipsetLib is clean).

## ROOT, named authoritatively (2026-07-08) -- CSERVE 0x66 R0-clobber

The `0x1010000` is NOT a HWRPB/config value.  It is a TOY-clock TIMESTAMP that V4
wrote into R0 from a MISLABELED CSERVE.  Traced in es40.dec: the ES40 console does
`R16 = 0x66; CSERVE (CALL_PAL 0x09); ADDQ 0x28000, R0 -> SCB base`.  V4's
`execCserve` case `0x66` (=102) returned `ToyRtc::timestampMMDDhhmm(cycleCount)` =
`0x01010000` (BCD month=01 day=01 hour=00 min=00 = Jan-1 00:00, the ToyRtc epoch),
so SCB base = `0x01010000 + 0x28000 = 0x1038000`.

The authoritative PC264 PAL `sys__cserve` dispatch
(`Processor Support/.../ref/ev6_vms_pc264_pal.mar:3911`) `hw_ret`s with R0
UNTOUCHED for every undefined function code, and 102 (0x66) IS undefined (last
defined = MP_WORK_REQUEST=101; `ev6_pc264_pal_defs.mar:60-75`).  `sys__get_timestamp`
in that PAL is an INTERNAL `bsr` routine (error-log timestamping), NOT a CSERVE.
So V4's `0x66=get_time` was an unsourced fabrication that clobbered R0; the console
expected R0 = its phys_base (0), giving SCB base `0x28000` (matching the fill and
DS20).  Oracle-discipline lesson: a plausible-but-unsourced value entered the
oracle (added to paper over a memory-test SYSFAULT) and corrupted an unrelated
path.

## Fix applied + CONFIRMED (2026-07-08)

`palBoxLib/grains/PalEntries.cpp` execCserve: **removed the `case 0x66`
(get_time)** so it falls to the default (R0 untouched, no fault) -- matching
`hw_ret (p23)`.  Also removed the now-orphaned `#include ".../ToyRtc.h"` (its only
user).  This makes the ES40 SCB base `= R0(0) + 0x28000 = 0x28000`, matching where
`scb_init` fills.  Core change: rides the full gate (Emulatr_tests + DS10/DS20/ES40
boot-to-prompt) before commit.  `EMULATR_BRINGUP_PROBES` must be flipped OFF for the
release build.

CONFIRMED by re-run (es40_fix_test.log): SCB base now `0x28000` (impure 0x7170
`v=0x28000`); CSERVE 0x66 shows `func=102 (reserved / no-op)`; the boot ran to cyc
`2,332,174,973` (`retires=1,342,177,241`, the `--max-cycles 0x50000000` retire cap)
and stopped on `MaxCyclesExceeded`, `halted=false` -- NOT the old `kFaultHalt`.  The
`1.2395B` SCB null-dispatch halt is GONE (~5x more retires than the pre-fix 249M).
The deferred memory-test SYSFAULT did NOT re-surface.

## NEXT FRONTIER (new, downstream) -- non-canonical R16 ACV

The SCB fix advanced ES40 into the (now-working) interrupt path, where it hits a
SEPARATE pre-existing wall and does not yet reach `P00>>>`.  At the cap: `lastFault
= 7 (kFaultAcv)`, `excAddr = 0x1b7d34` (clock-ISR context), PC `0xa88e4`, Kernel
mode, **`R16 = 0x80000d0000000000`** -- a NON-CANONICAL VA (bit 63 set, bits 62:48
zero = sign-ext violation) -> ACV on dereference.  This matches the project's
already-flagged "ES40 R16 backtrace / ACV loop root is a garbage pointer" and the
`ACVPROBE` Hook A/C (built for the console 1-1 ACV).  NOT a regression from dropping
get_time (that touched only R0/the SCB base; the ACV is downstream of the clock
dispatch we unblocked).  Own it in a fresh plan: first determine loop-vs-slow
(higher retire cap, or HookC on `0x80000d0000000000`), then trace R16's last
writer.

## DEFERRED TASK -- time handling / memory-test get_time

`get_time` was originally added to `0x66` to paper over an ES40 memory-test
SYSFAULT at guest `0x8c2d0` ("`return = input - get_time()`").  With `0x66` now a
faithful no-op, that path must obtain time another way -- most likely the internal
`sys__get_timestamp` bsr, NOT a CSERVE.  BEFORE trusting the no-op on the full cold
boot, confirm the memory-test's real time source and how the TOY/get_timestamp
mechanism is meant to work end to end.  Watch for the memory-test SYSFAULT
re-surfacing when ES40 is re-run past the (now-fixed) SCB halt.

## Gate -- PASSED (2026-07-08)

- `Emulatr_tests`: **474 cases / 6066 assertions, all green** (0 failed).
- ES40 (ini model=ES40): SCB halt cleared; ran to cyc 2.33B, MaxCyclesExceeded,
  no kFaultHalt / no SYSFAULT.  Does NOT reach `P00>>>` yet -- blocked by the new
  R16 ACV frontier above; the gate criterion for THIS change is "SCB halt cleared +
  no regression," not ES40-to-prompt.
- DS10 (ini model=DS10): boots to SRM `>>>`.
- DS20 (ini model=DS20): boots to the `AlphaServer DS20` console banner, interactive.

Do-no-harm across the model line CONFIRMED.  Clear to commit.

Commit contents: the one-case CSERVE `0x66` removal + the orphaned `ToyRtc.h`
include removal + this journal.  The session's diagnostic probes (fault-VA column,
`EMULATR_TRACE_ARM_ON_DTBM`, `EMULATR_VPTB_DIAG`, `EMULATR_VECWATCH_VAL`) are
additive/env-gated -- same or separate commit.  Build the shippable binary with
`-DEMULATR_BRINGUP_PROBES=OFF` (the ACVPROBE + CSERVE-default probes ballooned
es40_fix_test.log to 5.2 GB).

## Next frontier -> new plan

Start `journals/2026xxxx_es40_r16_acv_*.md` for the non-canonical-R16 ACV
(excAddr 0x1b7d34, R16=0x80000d0000000000).  Cross-reference the Ev6Translator
harvest task and the ACVPROBE hooks.

## Diagnostic instrumentation added this session (all env-gated)

- `coreLib/FaultEventLog.{h,cpp}` + `pipelineLib/PipelineDriver.h`: fault log now
  carries the faulting VA (`va=0x...` column in logs/faults.log and the loud
  stderr line).  Passed `cpu.va` at the log call.
- `pipelineLib/PipelineDriver.h`: `EMULATR_TRACE_ARM_ON_DTBM=<instrs>` -- one-shot
  decoded-retire window armed on the first `kFaultDtbMissDouble` (pairs with the
  sink's PAL-entry lookback).
- `palBoxLib/grains/PalEntries.cpp` (`execMtprVptb_vms`): `EMULATR_VPTB_DIAG` --
  logs each `MTPR_VPTB` write + current `va_ctl` (capped 64).  Gated on
  `EMULATR_BRINGUP_PROBES`.
- `memoryLib/GuestMemory.cpp`: `EMULATR_VECWATCH_VAL=<v>` value-keyed store probe
  (added earlier this task).

Reproduction (build relwithdebinfo; `-DEMULATR_BRINGUP_PROBES=ON` for the ACVPROBE
hooks):
- Base cache:  `EMULATR_GMEM_WATCH=0x7170`
- SCB fill:    `EMULATR_GMEM_WATCH_LO=0x28000 EMULATR_GMEM_WATCH_HI=0x2a000`
- Fault VA:    read logs/faults.log `va=` column
- Always raise PuTTY (see emulatr-launch skill); redirect stderr to a log.

## Config footgun (cost us a contaminated DS20 run)

`config/EmulatrV4.ini [System] model` pins the platform (currently `ES40`) as a
SEPARATE channel from the firmware + manifest.  Running DS20 firmware with
`model=ES40` is a HYBRID boot (DS20 device bus + ES40 capabilities); the guard at
`systemLib/Machine.cpp:516` logs "PLATFORM MISMATCH" (non-fatal).  For a trustworthy
cross-model run, set `model` to match the firmware.  The DS20 numbers above are
from a CLEAN run (`model=DS20`).

## Standing rules

Discuss-before-code; header + inline docs citing HRM/source + task id; ASCII(128)
only; surgical Edit; treat V0/V1/V2 + Processor Support read-only; verify writes
via bash; bounded trace tails only; full suite + DS10 + DS20 + ES40 boot gate
before any core/platform commit.
