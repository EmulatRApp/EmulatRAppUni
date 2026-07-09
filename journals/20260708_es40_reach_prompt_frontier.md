<!--
EmulatR V4 -- ES40 REACH-PROMPT Frontier (post-SCB-fix): COM2 spin + R16 ACV (2026-07-08)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: hand-off for the NEXT ES40 blocker after the CSERVE-0x66 SCB-base fix
cleared the interval-clock halt.  ES40 now runs 1.24B->2.33B cyc but does NOT reach
P00>>>.  Captures what is DONE, what is RULED OUT, the open questions, and the
leading hypothesis (interrupt-mode UART path via canAcceptInterrupt).  ASCII(128).
Discuss-before-code stands.
-->

# ES40 REACH-PROMPT Frontier -- post-SCB-fix (2026-07-08)

## TL;DR

The 2026-07-08 CSERVE-0x66 fix cleared the interval-clock SCB halt
(`journals/20260708_es40_scb_base_mismatch_root.md`).  ES40 now streams to cyc
`2.33B` (`--max-cycles 0x50000000` retire cap) with no halt/fault, but still does
NOT reach `P00>>>`.  The remaining blocker is in the SRM console-output path
(`0x629f0`/`0xa8xxx` -> `combott_txready`, guest `0x1b7d34`/`0x1b7d80`), NOT the
memory scan (which completes).  The simple UART-model fix is ALREADY in the tree,
so the real gate is deeper.  Leading lead (T. Peer): the console runs
INTERRUPT_MODE after "lowering IPL", so COM I/O rides the UART THRE/RX interrupt
through 8259 -> DRIR -> `canAcceptInterrupt`, not just LSR polling -- verify that
chain latches/accepts for COM1/COM2.

## DONE this session (do not redo)

- **CSERVE 0x66 get_time removed** -> ES40 SCB base `0x28000` (was `0x1038000`);
  interval-clock halt GONE.  Gate GREEN: `Emulatr_tests` 474/6066; DS10 `>>>`;
  DS20 banner.  Full chain: `journals/20260708_es40_scb_base_mismatch_root.md`.

## Where it is now (profiler + diag evidence)

RetireProfiler (`traces/profile_*run_end.txt`, 1.34B retires):
- `0x84800` 22.4% (301M), `0x7fc00` 12.3%, `0x81000` 9.5%, `0x84400` 8.6% --
  ~53% of retires in the console window `~0x7fc00-0x85000`, cyc `189M-1.1B`.  This
  is the **memory scan/test** (spin-skip refuses it as a live memory loop); it
  COMPLETES by ~1.1B (last_cyc), so it is legit long work, NOT the blocker.  (2005
  memo: confirm memory-size dependence; re-test at 32 GB.)
- After the scan, execution orbits `0x629f0`/`0xa8xxx` (2026-07-05 frontier) with
  no COM1 init, so PuTTY stays blank.  At the retire cap: `lastFault=7 (kFaultAcv)`,
  `excAddr=0x1b7d34`, `R16=0x80000d0000000000` (non-canonical: bit63 set,
  bits62:48 zero -> sign-ext ACV), Kernel mode.

DIAG-PC window `0x1b7d00-0x1b7dc0` (= `combott_txready`): the loop reads MSR
`0x801fc0002fe` (49x) + LSR `0x801fc0002fd` (48x); it NEVER reads IER `0x2f9`.

## RULED OUT -- do NOT re-chase

- **Root B (console-base / HWRPB) of the COM2 spin.**  The distinguishing test
  (2026-07-06 journal) says IER `0x2f9` reads => Root B; the loop reads only
  MSR/LSR, never IER.  So it is NOT the `get_console_base_pa()==0` unconditional
  return.
- **The simple Root-A (UART-model) fix -- ALREADY PRESENT.**  `Uart16550.h`:
  `readMSR()` returns `0x00` for an unwired port (`m_backend==nullptr`), and COM2
  IS unwired (COM1 gets `setBackend` at `Machine.cpp:417`; COM2 never does,
  `TsunamiChipset.h:863`).  `readLSR()` always returns `THRE|TEMT`.  So feeding
  `MSR=0x00`, `LSR=THRE` into `combott_txready` (per the 2026-07-06 excerpt)
  yields tx-ready=1, i.e. it should NOT spin on tx-ready.  The obvious fix is done;
  the remaining cause is deeper.

## Open questions (start here)

1. **Full `combott_txready` return path from SRM source** (not the partial 2026-07-06
   excerpt): the `cbip`/`cp->flow` branches and any UPSTREAM device-detect that
   gates on MSR/LSR differently.  Confirm the real exit condition the loop never
   sees.
2. **Is it a spin or slow progress?**  Re-run with a higher retire cap (or
   forward-progress watch) to prove tight-loop vs advancing.  The 49/48 MSR/LSR
   reads in a bounded window could be NORMAL per-character output.
3. **The non-canonical R16 ACV** (`excAddr=0x1b7d34`, `R16=0x80000d0000000000`):
   is it the true blocker (a garbage pointer the loop re-derives) or incidental at
   the cap?  Cross-ref the 2026-07-02 ACV journals + the Ev6Translator harvest task.

## LEADING HYPOTHESIS (T. Peer, 2026-07-08) -- interrupt-mode UART path

After "lowering IPL" the console sets `console_mode[id] = INTERRUPT_MODE`
(`srmconsole/KERNEL.C`), so from there it drives COM output/input off UART
INTERRUPTS, not LSR polling.  The chain exists in V4 (`systemLib/Machine.cpp`):
UART `uart_int_pending` -> Cypress 8259 **IRQ4 (COM1) / IRQ3 (COM2)** ->
[IMR/in-service/priority] -> **DRIR<55>** (Machine.cpp:1565-1566), gated by
`Machine::canAcceptInterrupt(irqLevel)` (Machine.cpp:720; device IRQs delivered at
:1747-1760 via `pendingIrq3` / `canAcceptInterrupt(21)`; `evalDeviceIrqs` mirrors
the 8259 output into DRIR<55>).

Hypothesis: the interrupt-mode console output stalls because the UART **THRE
(tx-buffer-empty) interrupt** for COM1/COM2 does not propagate/accept through
8259 -> DRIR<55> -> `canAcceptInterrupt`, so the console's tx-interrupt handler
never fires and it falls back to (or wedges around) the `combott_txready` poll.
NEXT: use `canAcceptInterrupt()` + the DRIR/8259 latch state to POLL the chipset
interrupt structures tied to COM1 (IRQ4) and COM2 (IRQ3) during the wedge -- verify
whether a UART THRE assertion actually reaches DRIR<55> and is accepted.  If the
THRE-interrupt path is the gap, the fix is in the UART->8259->DRIR wiring or the
`readIIR`/THRE-latch (`Uart16550::readIIR`, m_threLatch/ETBEI at
`deviceLib/Tsunami/Uart16550.h`), NOT in `readMSR`/`readLSR`.

## Reproduction (relwithdebinfo; PuTTY per emulatr-launch skill)

- Profiler (always-on): `traces/profile_*run_end.txt` after any run.
- combott_txready window:
  `EMULATR_DIAG_PCLO=0x1b7d00 EMULATR_DIAG_PCHI=0x1b7dc0 EMULATR_DIAG_CAP=400`
- Fault VA + R16 at the wedge: register dump at run-end / `logs/faults.log`.
- HookC on the bad pointer (`-DEMULATR_BRINGUP_PROBES=ON`):
  `EMULATR_HOOKA_VA=0x80000d0000000000` -> issuing PC + GPRs (R16 provenance).

## Reference journals

- `20260708_es40_scb_base_mismatch_root.md` -- the SCB fix (this session).
- `20260706_es40_com2_txready_spin_root.md` -- the COM2 spin two-root fork.
- `20260705_es40_next_frontier.md` -- the `0x629f0`/`0xa8xxx` loop.
- `20260702_es40_acv_garbage_origin_traced.md`, `..._acv_va_form_analysis_cowork.md`,
  `20260702_ev6translator_harvest_task.md` -- prior R16/ACV analysis.

## Standing rules

Discuss-before-code; header + inline docs citing HRM/source + task id; ASCII(128);
surgical Edit; V0/V1/V2 + Processor Support read-only; bounded trace tails only
(NO full boot-scale trace-to-disk -- the profiler + bounded PC windows localize);
full suite + DS10 + DS20 + ES40 boot gate before any core/chipset commit;
`EMULATR_BRINGUP_PROBES=OFF` for release.
