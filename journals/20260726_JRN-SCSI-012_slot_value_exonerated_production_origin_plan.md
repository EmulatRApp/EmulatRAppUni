<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-012
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-012 -- SLOT VALUE EXONERATED (slot-6 A/B); the L1 question
#                 reduces to: WHERE DO THE WALK'S PRODUCTIONS COME FROM?
#                 Static attack plan for the production origin.

    Doc id   : JRN-SCSI-012
    Date     : 2026-07-26
    Status   : PROBE RECORD + attack plan.  No emulator code changed.
    Relates  : JRN-SCSI-011 (conversation = 4 answers, no open), JRN-VMB-021
               (walk dies at field 2; productions "registered"), JRN-VMB-019
               (pattern-VM 0x158284), JRN-SCSI-005 Sec 3 (0xf3-tail gate),
               JRN-SCSI-004 (footprint byte-identical IDE vs SCSI).

--------------------------------------------------------------------------------
## 1. The slot-6 A/B (2026-07-25 ~21:33) -- NEGATIVE, and clean

  Hypothesis: resolver gates on topology field 2 (the PCI slot; 8 =
  admitted "minimal-churn" placement; real DS20 embedded SCSI = slot 6
  per the console's own pci_irq_table; AXPBox-ES40 = slot 3).
  Method: run-dir manifest variant ds20_v7_3_platform.SLOT6.json
  (pka_53c810 slot 8 -> 6, pin 2 kept), EMULATR_PLATFORM_CONFIG=...,
  cold boot, operator console: `b dka0.0.0.6.0 -flags 0`.
  Result: console enumerated and booted dka0.0.0.6.0 (boot block valid,
  1226 blocks read -- slot-6 wiring + IRQ routing fine) ->
  %APB-F-NOIOVEC, same shape.  Log: logs/slot6_probe_20260725_213253.log.
  CONCLUSION: the slot VALUE (8 vs 6) is NOT the gate.  Together with
  JRN-SCSI-004 (footprint byte-identical between "IDE 0 105 ..." and
  "SCSI 0 8 ...") the walk's abandonment is CONTENT-INDEPENDENT over the
  strings we can produce: ident differs, field-2 differs, path identical.

--------------------------------------------------------------------------------
## 2. What the accumulated facts now force

  - APB consumed EXACTLY four console answers (JRN-SCSI-011): tty_dev
    "0", failed cbfunc 0x07, booted_osflags "0", booted_dev/boot_dev
    topology string.  No open/read/ioctl.  So any "console-side data APB
    consumed before the window" (JRN-VMB-021's alternative 1) is now
    limited to what APB reads DIRECTLY FROM MEMORY, not via callbacks:
    HWRPB fields (SYSTYPE 0x22, SYSVAR, DSRDB...) and the GCT/config
    tree the console builds at PA 0x3ff32000.
  - The walk production for the boot string is REGISTERED in the pattern
    stream (VMB-021: "IDE 0 105 0 0 0 0 0" present with 5+2 fields) --
    including the LITERAL field values.  A database compiled into
    APB.EXE years before this machine existed cannot contain "105" for
    OUR Cypress func-1 IDE: THE PRODUCTIONS ARE BUILT AT RUNTIME from
    data derived from the console's OWN device view.  The walk therefore
    compares boot_dev against a runtime-built table, and on EmulatR the
    comparison aborts at field 2 IDENTICALLY for every topology we
    present, while the SAME code accepts AXPBox's.  The delta must live
    in whatever SOURCE feeds the production builder -- prime suspect:
    the GCT/config tree (ES40-firmware-built on AXPBox vs DS20-firmware-
    built here) or an HWRPB/DSRDB field the builder keys on.

--------------------------------------------------------------------------------
## 3. Attack plan (static-first; all inputs already on disk)

  S1  Pattern-pointer dereference: the walk transcript's per-field ptrs
      (0x99232 -> fffca350, 0x9923e -> fffca324, ...) -- resolve their
      base (candidates: ctx a3=0x20063820-relative, key record a0-
      relative, sign-extended VA) by READING the failing-era snapshot at
      each candidate; the cell that contains "105"/"IDE"/digit text
      names the addressing and proves literal-value productions.
  S2  Production-builder locate: the stream cursor for the boot walk was
      a1=0x20099216 (inside APB image, WRITABLE region?).  Check whether
      0x99216 pages are in the image file or bss: bss/heap -> runtime-
      built (Sec 2 argument confirmed); then find the WRITER of 0x99216
      bytes (snapshot diff pre/post boot at that PA, or DIAG window over
      the builder once located via --find-bsr on the stream base).
  S3  Caller context: disassemble 0x2000e5xx/0x2000e974 (JRN-SCSI-005
      Sec 3 next-static-step, still owed): where a3 (ctx 0x20063820) is
      built, and which memory it was filled from (GCT walk? HWRPB?).
  S4  Only if S1-S3 stall: AXPBox comparative via the JRN-SCSI-005 Sec 5
      harness (May-20 axpbox.exe + decompressed.rom, ~3 min to >>>).
      NOTE `show *` shows DISPLAY forms only; the callback/topology form
      requires memory inspection -- hence static-first on our side where
      we have snapshots.
  Tools: snap_va_disasm.py (VA delta APB), snap_extract (offset skew
  note in JRN-SCSI-005 Sec 3), crb_conversation_decode.py, the hold/
  snapshot pair, tonight's slot6 CRB capture.

--------------------------------------------------------------------------------
## 5. SAME-NIGHT ADDENDUM -- S1/S2 executed from the snapshot; Sec 2's
##    "runtime-built" inference is CORRECTED; the wall has a door

  5.1 STREAM IS STATIC.  The pattern stream at VA 0x20099216 (records
      85f1/85f3 + ptr fffcaXXX, byte-exact vs the JRN-VMB-021 transcript)
      is PRESENT in the failing-era snapshot -- a state in which APB
      never executed one instruction.  It ships in APB.EXE.  Sec 2's
      runtime-built argument is WITHDRAWN for the stream itself.
  5.2 PRODUCTIONS ARE GENERIC.  No literal "IDE 0 105" / "SCSI 0 8"
      text exists in the image+heap range; 85f3 is a generic NUMBER
      token -- VMB-021's per-field values were the INPUT's, not the
      pattern's.  The probe pass fails BY DESIGN (bit-10); the accept
      pass is a SECOND INVOCATION that never occurs on EmulatR.
  5.3 POINTER BASE SOLVED: record-relative (VA_of_record + signed32).
      Targets land in static image data 0x200635xx -- regular
      descriptor-shaped operand records.
  5.4 THE CTX IS A STATIC DISPATCH TABLE.  VA 0x20063820 (walk arg a3)
      holds code pointers: +0x8 = 0x2000e9d0 (adjacent to the walk
      entry), later 0x20071270, 0x20007470...  This is the indirect-call
      mechanism -- and why --find-bsr 0x2000e974 finds ZERO static call
      sites.  Static call-site hunting is a dead end; a RETIRE TRACE is
      the correct instrument (architect's proposal, adopted).
  5.5 PLAN REVISION -> T-series (supersedes Sec 3 S1-S4):
      T1  full-APB retire trace, cold boot to NOIOVEC:
            EMULATR_NO_AUTOLOAD=1 EMULATR_2D_NOOP=1
            EMULATR_DIAG_PCLO=0x20000000 EMULATR_DIAG_PCHI=0x20099400
            EMULATR_DIAG_CAP=5000000
          then offline ASM reconstruction (alpha_disasm/analyze_retire_
          trace): count walk invocations, and name THE BRANCH after the
          failed probe that chooses message-emit over the accept
          invocation.  THE deliverable.
      T2  decode the operand records at 0x200635xx (record-relative
          base per 5.3) -> full production grammar.
      T3  enumerate the 0x20063820 dispatch table statically; find the
          accept-pass handler (expect ties to the 0x20097200+ region).
      T4  fold T1-T3: the deciding compare's data source (ctx field /
          GCT / HWRPB) -> EmulatR fix or the exact AXPBox question.
      T5  AXPBox comparative only if T1-T4 stall (JRN-SCSI-005 Sec 5
          harness).
      Hygiene every run: NO_AUTOLOAD (auto_halt_* now in run-dir
      snapshots/), 2D_NOOP, ONE console client, slot-8 baseline
      manifest.

--------------------------------------------------------------------------------
## 4. Files touched

  - out/build/relwithdebinfo/ds20_v7_3_platform.SLOT6.json  NEW (run-dir
    experiment variant; run-dir only, NOT the source manifest)
  - this journal                                            NEW
  No emulator code changed.
