<!--
EmulatR V4 -- ES40 REACH-PROMPT: combott_txready Diagnostic Sequence (S1-D4) (2026-07-08)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: Cowork-executable diagnostic sequence to localize the post-SCB-fix ES40
reach-prompt blocker (console output orbits combott_txready at guest 0x1b7d34,
never reaches P00>>>).  This is a CAPTURE-BEFORE-FIX plan: it names no fix.  Its
output is the evidence that selects the fix in a later discuss-before-code turn.
Companion to journals/20260708_es40_reach_prompt_frontier.md.  ASCII(128) only.
Discuss-before-code stands: the D-step probes are env-gated diagnostic edits, not
behavioral changes; land no behavioral fix from this journal.
-->

# ES40 combott_txready Diagnostic Sequence (S1-D4) -- 2026-07-08

## Purpose and framing

The CSERVE-0x66 SCB-base fix cleared the interval-clock halt; ES40 now streams to
cyc 2.33B but orbits the SRM console-output path (combott_txready, guest
0x1b7d34/0x1b7d80) without reaching P00>>>.  This journal is the ordered probe
sequence to localize the cause.  Cheapest-decisive first; each D-step has an
explicit PASS/FAIL branch that gates the next.

Two facts constrain the whole sequence and must be kept in front:

- FACT-1 (impossibility of a classic tx-ready spin).  Uart16550::readLSR() always
  returns THRE|TEMT.  A loop that gates purely on THRE therefore exits its
  tx-ready test every pass and cannot spin on tx-ready.  The wedge is NOT "THRE
  never sets"; it is something else the loop reads.  The read census (MSR 49x,
  LSR 48x, IER 0x2f9 never) says the extra pressure is on MSR (modem status),
  not LSR (line status).

- FACT-2 (UART regs are MMIO, not DRAM).  The observed accesses are in Tsunami
  PCI I/O space at base 0x801FC000000 (COM2 0x2F8 -> 0x801FC0002F8..2FF; COM1
  0x3F8 -> 0x801FC0003F8..3FF).  These route through the chipset I/O hook, NOT
  through memoryLib::GuestMemory.  CONSEQUENCE: EMULATR_GMEM_WATCH /
  EMULATR_VECWATCH_VAL (GuestMemory store probes) will NOT observe IER or THR
  writes.  D1/D2 must instrument the UART write seam or a commit-stage store-EA
  filter, not GMEM.  Do not point GMEM_WATCH at 0x801FC00xxxx and conclude "no
  writes" -- that would be a false negative.

UART register map (16550, DLAB=0), for reference throughout:

    off +0  THR/RBR   COM2 0x801FC0002F8   COM1 0x801FC0003F8
    off +1  IER       COM2 0x801FC0002F9   COM1 0x801FC0003F9
    off +2  IIR/FCR   COM2 0x801FC0002FA   COM1 0x801FC0003FA
    off +3  LCR       COM2 0x801FC0002FB   COM1 0x801FC0003FB
    off +4  MCR       COM2 0x801FC0002FC   COM1 0x801FC0003FC
    off +5  LSR       COM2 0x801FC0002FD   COM1 0x801FC0003FD
    off +6  MSR       COM2 0x801FC0002FE   COM1 0x801FC0003FE
    off +7  SCR       COM2 0x801FC0002FF   COM1 0x801FC0003FF

IER bit of interest: ETBEI = bit 1 (0x02) = "enable THR-empty interrupt".
MSR bits of interest: CTS = bit 4 (0x10); DSR = bit 5 (0x20); DCD = bit 7 (0x80).

Build/run baseline for every step: relwithdebinfo; raise PuTTY per the
emulatr-launch skill; redirect stderr to a log; model matches firmware
(model=ES40 for a trustworthy ES40 run -- do not run ES40 firmware under
model=DS20).  D-steps that add or use BRINGUP probes build with
-DEMULATR_BRINGUP_PROBES=ON; release stays OFF.

---

## S1 -- Static: full combott_txready from SRM source (no run)

GOAL: recover the exact loop exit predicate and every status bit it depends on,
so the runtime read census (MSR 49x, LSR 48x, no IER) can be mapped to a source
branch and a truth value computed for the values V4 actually feeds
(LSR=THRE|TEMT, MSR=0x00 on the unwired port).

STEPS:

- S1.1  Locate the function.  In the apisrm/ref tree (authoritative SRM console
  source; treat read-only):
      grep -rn "combott_txready" apisrm/ref/
  Record the file and line.  Expected neighborhood: the console comm/tt driver
  under srmconsole (same tree as IE.C / KERNEL.C from the SCB work).  If the
  symbol is a macro or an inlined helper, follow it to the real body.

- S1.2  Extract the COMPLETE function body (not the 2026-07-06 partial excerpt).
  Capture verbatim into this journal under an "S1 evidence" appendix:
    (a) the console_mode[id] read, if any, and the INTERRUPT_MODE vs POLLED
        branch it drives;
    (b) every UART status read (LSR, MSR) and the exact mask/compare applied to
        each (e.g. "MSR & 0x10" for CTS, "LSR & 0x20" for THRE);
    (c) the cp->flow / cbip branches and any hardware-flow-control gate;
    (d) the exact predicate that makes the routine return "tx ready" vs loop.

- S1.3  Identify the character-source pointer.  Find what the loop dereferences
  to obtain the byte it is trying to transmit (buffer base + index, or a cp->
  field).  Note its provenance -- this feeds D3 (the R16 ACV may be this exact
  pointer).

- S1.4  Compute the truth table for V4's fed values.  With the source predicate
  from S1.2, evaluate:
      Given readLSR() == (THRE|TEMT) and readMSR() == 0x00:
        does the predicate EXIT (tx-ready) or LOOP (wait)?
  If it LOOPS on MSR==0x00, name the offending bit test (almost certainly a CTS
  or DSR gate under cp->flow).  This is the single most likely wedge and S1
  alone may confirm it.

- S1.5  Cross-check DS10/DS20 non-regression on paper.  From the same source,
  determine whether DS10/DS20 reach P00>>> because they (a) drive COM1 which is
  wired, (b) never set cp->flow, or (c) take the POLLED branch.  Record which,
  so any later fix can be shown do-no-harm before it is written.

S1 OUTPUT: the exit predicate, the branch map, the S1.4 truth value (EXIT or
LOOP under MSR=0x00), and the char-source pointer identity.  This may resolve the
blocker to "MSR CTS/DSR gate under cp->flow" without any run; the D-steps then
become confirmation rather than search.

---

## D1 -- Dynamic: IER write-watch on 0x2F9 / 0x3F9 (confirms or KILLS interrupt-mode)

GOAL: decide whether interrupt-mode transmit was ever ARMED.  Interrupt-mode tx
requires a write to IER with ETBEI (bit 1) set.  The combott_txready window never
READS IER, but the ARM would be a WRITE at console-init PC, outside the current
0x1b7d00-0x1b7dc0 window -- so a PC-window read census cannot see it.  This step
watches the WRITE directly.  D1 gates D4 entirely: if ETBEI is never armed, the
8259/DRIR/canAcceptInterrupt investigation is OUT OF SCOPE for this blocker.

METHOD (per FACT-2, instrument the UART write seam, NOT GMEM):

- D1.1  Add an env-gated write probe at the UART register-write dispatch --
  the write-side counterpart to readMSR/readLSR/readIIR in
  deviceLib/Tsunami/Uart16550.h.  CONFIRM the exact method name/seam in-tree
  (writeReg / writeIER / the reg-write switch).  Probe shape (surgical,
  BRINGUP-gated):
      // TODO(diag-ier): env-gated IER-write census for ES40 txready localize
      // Gated on EMULATR_BRINGUP_PROBES; logs port + offset + value on IER write.
  Emit one line per write to offset +1 (IER) on either COM port:
      "IERWATCH port=COM<n> pa=0x801FC000<off> val=0x%02x etbei=%d cyc=%llu pc=0x%llx"
  where etbei = (val & 0x02) != 0.  Env gate: a new EMULATR_IERWATCH=1 (or fold
  under the existing BRINGUP probe gate).  Cap the log (e.g. 64 lines) to honor
  bounded-trace discipline.
  ALTERNATIVE (no UART edit) if a commit-stage store-EA filter already exists in
  the pipeline (the store-side analogue of the DIAG-PC memAddr load column):
  key it on physical EA in {0x801FC0002F9, 0x801FC0003F9} and log value.  Prefer
  whichever seam is already present; do not build both.

- D1.2  Run a full boot to the retire cap with the probe active
  (-DEMULATR_BRINGUP_PROBES=ON, EMULATR_IERWATCH=1).  Bounded log only.

- D1.3  Read the census.

BRANCH:

- D1-FAIL (ETBEI never written on COM1 or COM2): interrupt-mode tx was never
  enabled.  This loop IS the polled path.  ==> D4 is OUT OF SCOPE; do not spend a
  session on 8259/DRIR/canAcceptInterrupt for this blocker.  Proceed to D2/D3;
  the fix will be in the polled predicate (S1.4) or the port wiring (D2).

- D1-PASS (a write to IER with ETBEI set is observed): interrupt-mode tx WAS
  armed; its non-delivery is a real candidate.  ==> D4 is IN SCOPE.  Record the
  arming PC and which port.  Still complete D2/D3 (they are independent).

---

## D2 -- Dynamic: THR byte capture on 0x2F8 / 0x3F8 (spin vs progress; dropped-port test)

GOAL: two answers at once -- (a) is the loop actually STUCK, or making normal
per-character progress; (b) is the console emitting to a port V4 discards.
Motivation: COM1 is wired (setBackend at Machine.cpp:417); COM2 is never wired
(TsunamiChipset.h:863) and returns MSR=0x00.  All observed reads are at COM2
(0x2Fx).  If the console is streaming the banner to COM2, PuTTY (on COM1) shows
nothing and the "wedge" is really a port-selection / backend-wiring problem.

METHOD (same seam as D1; THR is offset +0):

- D2.1  Extend the D1 write probe (or add a sibling) to log writes to offset +0
  (THR) on both COM ports, with the byte value and, where printable, the ASCII:
      "THRWATCH port=COM<n> pa=0x801FC000<off> val=0x%02x ch='%c' cyc=%llu"
  Same BRINGUP gate; same bounded cap (allow a larger cap here, e.g. 256, to
  catch a banner-length run, but keep it bounded -- no boot-scale dump).

- D2.2  Run to the retire cap.  Collect the THRWATCH lines for both ports.

BRANCH:

- D2-A (THR bytes to COM2 spell readable console text / the banner): the console
  is PROGRESSING to a port V4 drops.  ==> blocker is port selection or COM2
  backend wiring, NOT the UART status model.  This is consistent with FACT-1
  (no true tx-ready spin) and would explain why DS10/DS20 (COM1) reach >>>.
  Capture the exact port the console chose and why (console_mode / cp selection
  from S1).

- D2-B (no THR writes, or a single byte retried forever): the loop is genuinely
  WEDGED before it can hand a byte to THR -- it never clears its pre-transmit
  gate.  ==> blocker is the loop predicate (S1.4), most likely the MSR CTS/DSR
  gate.  The retried byte's source pointer ties to D3.

- D2-C (THR bytes to COM1, readable): the console IS driving the wired port and
  emitting -- then the "no P00>>>" is partly a visibility/timing artifact; widen
  the retire cap and re-check for forward progress before treating it as a wedge.

---

## D3 -- Dynamic: R16 provenance for the non-canonical ACV (is it the blocker?)

GOAL: decide whether the ACV (lastFault=7, excAddr=0x1b7d34, R16=0x80000d00_
00000000, Kernel) is the true blocker or a cap-time artifact -- and whether it
shares an origin with the earlier 0x3fc11fff garbage base.  R16 is non-canonical
(bit63 set, bits62:48 zero -> sign-extension check fails -> ACV).  0x1b7d34 is
inside the combott_txready window, so if R16 is the loop's char-source or a
descriptor pointer, the ACV IS the wedge and D1/D2 are downstream.

METHOD (hook already exists per the frontier journal):

- D3.1  Build -DEMULATR_BRINGUP_PROBES=ON.  Run with
      EMULATR_HOOKA_VA=0x80000d0000000000
  to capture the issuing PC and the full GPR set at the point R16 takes that
  value (the producer), plus the deref site.

- D3.2  Identify R16's producer chain: is 0x80000d00_00000000 loaded from memory
  (a stored pointer), computed (base+offset), or an IPR/superpage-tag artifact?
  Note the base and any offset -- compare the SHAPE to the SCB phys_base bug
  (a spurious high bits<47:32> component added to a low pointer).  The
  0x80000d00_ prefix is a high-half pattern; check whether it is a mis-formed
  kseg/superpage address (same defect family as the phys_base +0x1010000), not a
  UART issue.

- D3.3  Cross-reference the prior ACV analysis:
      journals/20260702_es40_acv_garbage_origin_traced.md
      journals/20260702_es40_acv_va_form_analysis_cowork.md
      journals/20260702_ev6translator_harvest_task.md
  If R16's producer matches the 0x3fc11fff origin (same routine, same
  base-pointer construction), this is the SAME translation/superpage defect
  resurfacing -- log that linkage explicitly.

BRANCH:

- D3-BLOCKER (R16 is dereferenced by the combott_txready loop, e.g. as the
  char-source from S1.3): the ACV is the wedge.  ==> fix path is the R16 producer
  (translation/pointer construction), NOT the UART.  Elevate this above D1/D2
  outcomes.

- D3-INCIDENTAL (R16 is untouched by the loop's exit path and only faults at the
  cap): note and set aside; the wedge is the loop predicate or port wiring.

---

## D4 -- CONDITIONAL (only if D1-PASS): interrupt path COM1(IRQ4)/COM2(IRQ3) -> DRIR<55>

GATE: run D4 ONLY if D1 showed ETBEI armed.  If D1-FAIL, SKIP -- do not
instrument the 8259/DRIR chain for this blocker.

GOAL: if interrupt-mode tx is armed, prove whether a UART THRE assertion actually
reaches DRIR<55> and is accepted, or is dropped somewhere in
UART -> 8259 -> DRIR -> canAcceptInterrupt.

METHOD (poll the chipset interrupt structures during the wedge; seams from the
frontier journal):

- D4.1  Confirm THRE-interrupt generation at the UART.  In
  deviceLib/Tsunami/Uart16550.h, verify m_threLatch sets and readIIR() reports
  THR-empty (IIR=0x02) when ETBEI is set and THR drains.  Add a BRINGUP-gated
  one-line log on uart_int_pending assertion for the COM port
  (port, iir, threLatch, cyc).

- D4.2  Trace the 8259 stage.  For COM1 the vector is IRQ4; for COM2, IRQ3.  Log
  the Cypress 8259 IMR / in-service / priority decision for that IRQ (is it
  masked; does it win priority) at the point uart_int_pending asserts.

- D4.3  Trace DRIR<55>.  At Machine.cpp:1565-1566 (8259 output mirrored into
  DRIR<55>), log whether bit 55 sets when the 8259 asserts.

- D4.4  Trace acceptance.  At Machine.cpp:720 canAcceptInterrupt and the device
  IRQ delivery at :1747-1760 (pendingIrq3 / canAcceptInterrupt(21)), log whether
  the pending device IRQ is ACCEPTED at the current IPL/mode or rejected.

- D4.5  Run to the cap with the D4 chain logged (bounded), and read where the
  assertion dies: UART (no THRE latch), 8259 (masked / loses priority), DRIR (bit
  never set), or acceptance (canAcceptInterrupt rejects).

BRANCH: the first stage that fails to propagate names the fix seam
(UART THRE-latch/readIIR, the 8259 mask/priority, the DRIR<55> mirror, or the
canAcceptInterrupt IPL/mode gate).  Do not write the fix here; record the failing
stage for a discuss-before-code turn.

---

## Decision tree (how the steps compose)

- S1.4 == LOOP-on-MSR=0x00  -> strong lead: MSR CTS/DSR gate under cp->flow.
  D2-B confirms (byte never reaches THR); fix is the MSR model or the flow gate.
- D2-A (banner to COM2)     -> port-selection / COM2-backend-wiring blocker;
  UART status model is a red herring.
- D3-BLOCKER               -> R16 producer (translation) is the blocker; overrides
  the UART leads; ties to 20260702 garbage-base family.
- D1-PASS + D4 failing stage -> interrupt-mode tx delivery gap at the named stage.
- D1-FAIL                   -> interrupt path OUT OF SCOPE; blocker is polled
  predicate (S1) or port wiring (D2).

Expected most-likely outcome (prediction, to confirm not assume): S1 shows a
CTS/flow gate; D1-FAIL (IER never armed, so polled); D2 shows either a retried
byte (B) or banner-to-COM2 (A).  Fix lands in the MSR model or COM2 wiring, not
the 8259 path.  D3 must still run -- if R16 is the char-source, it pre-empts all
of the above.

## Do-no-harm gate (before ANY fix from a later turn)

Whatever the selected fix: show from S1.5 + a DS10 and DS20 run that the change
cannot regress their path to >>> (they either never enter the MSR-gated branch,
never set cp->flow, or drive the wired COM1).  Full suite + DS10 + DS20 + ES40
boot gate before any core/chipset commit.  BRINGUP probes OFF for release; remove
each diag TODO(diag-*) in the same edit that lands or retires it.

## Reproduction quick-reference

- Profiler (always-on): traces/profile_*run_end.txt after any run.
- combott_txready read window:
    EMULATR_DIAG_PCLO=0x1b7d00 EMULATR_DIAG_PCHI=0x1b7dc0 EMULATR_DIAG_CAP=400
- D1/D2 UART write census (new probe): -DEMULATR_BRINGUP_PROBES=ON, gate on the
  new EMULATR_IERWATCH / THRWATCH env (or the existing BRINGUP gate); bounded cap.
- D3 R16 provenance: -DEMULATR_BRINGUP_PROBES=ON,
    EMULATR_HOOKA_VA=0x80000d0000000000
- Retire cap for full stream: --max-cycles 0x50000000 (raise to test spin vs
  progress in D2-C).
- Always raise PuTTY (emulatr-launch skill); redirect stderr to a log; model=ES40.

## Reference journals

- journals/20260708_es40_reach_prompt_frontier.md   -- the frontier this localizes.
- journals/20260708_es40_scb_base_mismatch_root.md  -- the SCB fix that got us here.
- journals/20260706_es40_com2_txready_spin_root.md  -- the two-root fork (Root A/B).
- journals/20260705_es40_next_frontier.md           -- the 0x629f0/0xa8xxx loop.
- journals/20260702_es40_acv_garbage_origin_traced.md
- journals/20260702_es40_acv_va_form_analysis_cowork.md
- journals/20260702_ev6translator_harvest_task.md
