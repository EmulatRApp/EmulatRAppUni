<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-023
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-023 -- PT__VPTB writer watch: the cell is TOGGLED by the
#                 callback context swap (0x1333c clears / 0xe558
#                 restores), and the FINAL clear before the wall is
#                 UNPAIRED.  VA_FORM and the timer divert exonerated.

    Doc id   : JRN-SCSI-023
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-022 (halt-10 = DTBM_DOUBLE_3 crash1 on
               PT__VPTB mismatch), JRN-SCSI-021 (the wall).
    Input    : run_ds20_showdev_20260726_183745.log
               (EMULATR_PA_WATCH=0x7000 LEN=8, full boot to halt-10;
               48 stores captured), predig snapshot for disasm.

--------------------------------------------------------------------------------
## 1. The writers of PT__VPTB (PA 0x7000)

  pc=0x13b98  (cyc 182M)     v=0            early console zero-init
  pc=0x1333c  v=0            "ENTER CONSOLE CONTEXT": literally
      13330: HW_MFPR r3, IPR 0x1110
      13334: SLL r3,#0x22 ; 13338: SRL r3,#0x22   (keep bits [41:0])
      1333c: HW_ST r31, 0x0(r21)                  (PT__VPTB <- 0)
  pc=0xe558   v=0x2_0000_0000  "RESTORE OS CONTEXT" (ra=0x62f6c,
      console callback-exit path) -- the CORRECT VPTB value.

  From cyc 1.874e9 the two alternate in lockstep with the post-accept
  APB<->console callback traffic (the func-70 disk I/O conversation):
  every callback ENTRY clears PT__VPTB (0x1333c, ra=0x2004bxxx = APB
  CRB sites), every callback EXIT restores it (0xe558).  On real
  hardware this discipline is invisible -- the OS never double-misses
  while the console context is installed.

## 2. The fatal sequence

  Last three writes:
    cyc 1885622320  0xe558   v=2<<32   (restore -- callback exit)
    cyc 1885776941  0x1333c  v=0       (clear -- ra=0x24d9c, which is
                                        DATA (zeros), i.e. stale r26;
                                        caller unknown, NOT a normal
                                        APB callback entry)
    -- NO restore follows --
  The OS bootstrap at 0x29xxx then resumes, fetches 0x2a000 within
  ~50k cycles, ITB-miss -> VPTE double-miss -> DTBM_DOUBLE_3 crash1
  reads PT__VPTB = 0 vs formatted VA 0x2xxxxxxxx -> halt 0x0A
  (JRN-SCSI-022 chain, now with the corrupting write IDENTIFIED).

## 3. Exonerations

  - EmulatR's VA_FORM/IVA_FORM: the formatted PTE VA is CORRECT
    (JRN-SCSI-022 Sec re-confirmed; the mismatch is entirely
    PT__VPTB = 0).
  - The interval-timer divert: all 31 diverts fired during console
    init (cyc ~1.18e9); none within 700M cycles of the wall.
  - The B+ cpp-mode seed: not involved (this boot ran
    CSERVE_START_MODE=guest; the seed path never executed).

## 4. Open question (N3) + candidate shapes

  WHO invokes the 0x1333c clear at cyc 1885776941, and why does its
  exit path skip the 0xe558 restore?  Candidates:
    (a) The 0x29xxx OS-bootstrap code calls console services directly
        (its four out-of-window BSRs -> ~0x65xxx console dispatch);
        one such service enters console context and its exit path
        (different from the CRB callback exit) does not restore --
        faithful-console behavior that EmulatR breaks elsewhere, or
        an EmulatR divert/return defect on that path.
    (b) The clear is part of the ITB-miss handling itself (the IPR
        0x1110 read + 42-bit mask preceding it) -- a "fall back to
        1-to-1" branch mis-taken under EmulatR.
  N3a: identify IPR 0x1110 (EV6 defs) and disassemble the full
       routine around 0x13300 to name it.
  N3b: DIAG window over 0x13300-0x13360 + 0xe540-0xe580 with CYCLO
       late: capture entry paths (the excAddr/flow context of each
       clear/restore) -- one run names the unpaired caller.
  N3c: from the caller, decide fix altitude: EmulatR defect in a
       divert/return path vs a missing context-restore EmulatR must
       emulate.

--------------------------------------------------------------------------------
## 5. Files touched

  - this journal   NEW
  No emulator code changed.
