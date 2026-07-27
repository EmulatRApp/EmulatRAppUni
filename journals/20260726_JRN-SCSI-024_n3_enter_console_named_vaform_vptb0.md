<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-024
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-024 -- N3 DONE: the "unpaired clear" is the crash's OWN
#                 halt path (sys__enter_console) -- INNOCENT.  The real
#                 defect side is p4: EVERY formatted PTE VA in the run
#                 carries VPTB=0 while PT__VPTB (memory) is correct.
#                 -023's VA_FORM exoneration is WITHDRAWN.

    Doc id   : JRN-SCSI-024
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : corrects JRN-SCSI-023 Sec 2/3; JRN-SCSI-022 (crash1).
    Inputs   : run_ds20_showdev_20260726_191215.log (DIAG window
               0x8300-0x8310 + DIAG_WREG=4; 17818 R4 writes), PAL
               sources EV6_VMS_PC264_PAL.MAR / EV6_VMS_PAL.MAR,
               post-halt snapshots (PAL disasm).

--------------------------------------------------------------------------------
## 1. N3: the 0x1333c caller is NAMED -- and innocent

  The clear block matches EV6_VMS_PC264_PAL.MAR:4699-4703 byte-exactly
  (IPR 0x1110 = EV6__I_CTL; SLL/SRL #0x22 = 64 - I_CTL__VPTB__S with
  VPTB__S=30; hw_stq/p r31, PT__VPTB): it is **sys__enter_console**
  (label at :4638).  Its callers: the fault-reset path, the pc264
  reset path, and **trap__update_pcb_and_halt (EV6_VMS_PAL.MAR:6197)**
  -- the halt path that crash1 itself takes.  The "unpaired" final
  clear at cyc 1885776941 (JRN-SCSI-023 Sec 2) is therefore the
  CRASH'S OWN console entry, executed AFTER the check failed.
  Consequences:
    - The snapshot's PT__VPTB = 0 (JRN-SCSI-022 Sec 5 input) is a
      POST-MORTEM artifact of the halt path, not the crash-time value.
    - JRN-SCSI-023's "VA_FORM exonerated" is WITHDRAWN.
  Note sys__enter_console also STRIPS the VPTB field from I_CTL and
  sets VA_CTL VA_48 (48-bit) for the 1-to-1 console -- so the console
  state is legitimately VPTB-less; the OS state must be re-installed
  on every exit.

## 2. The r4 probe: formatted PTE VAs have NO VPTB -- the real defect

  DTBM_DOUBLE_3's vector head loads r4 = VA_FORM (the formatted L3-PTE
  VA the crash check tests).  DIAG_WREG=4 over 0x8300-0x8310 captured
  17818 double-miss entries across the boot: EVERY r4 value is a bare
  (va>>10)-style offset (0x560, 0xffcb8, 0x6e0, 0xffd18, ...) --
  **the VPTB slice is ZERO in all of them.**  Most entries are console
  1-to-1 mode (p_misc<63> branch bypasses the check, VPTB irrelevant);
  the fatal VM-mode entry compares p4<63:33> = 0 against PT__VPTB =
  0x2_0000_0000 (correct in memory at crash time, per the -023 toggle
  trace) -> mismatch -> crash1 -> halt 10.

  So the defect side is EmulatR's cpu.va_ctl / cpu.i_ctl VPTB FIELDS
  being zero when VM-mode code runs -- computeVaForm itself is correct
  (JRN-SCSI-022) but its VPTB input is empty.

## 3. How the real PAL keeps VPTB in the IPRs (source-mapped)

  - CALL_PAL MTPR_VPTB (EV6_VMS_CALLPAL.MAR:1524): merges VPTB into
    I_CTL/VA_CTL IPRs + stores PT__VPTB(p_temp).  EmulatR intercepts
    this as an intrinsic and DOES propagate (PalEntries.cpp:1526 fix).
  - sys__enter_console: STRIPS I_CTL's VPTB, sets VA_48 (Sec 1).
  - pal__save_state (EV6_VMS_PAL.MAR:6388-6391): saves CNS__VA_CTL =
    PT__VA_CTL | PT__VPTB (merged); I_CTL saved raw (pre-strip).
  - pal__restore_state (running PAL ~0xe510-0xe5b0 + tail): reloads
    the PAL temps (0xe558 = PT__VPTB <- saved, the -023 "restore"
    writer) and MTPRs the saved IPRs back -- including VA_CTL WITH the
    merged VPTB.  On real hardware every callback exit therefore
    re-installs a VPTB-bearing VA_CTL.

  Given Sec 2, ONE of these fails under EmulatR:
    (i)   the restore-path HW_MTPR VA_CTL/I_CTL never reaches
          cpu.va_ctl/cpu.i_ctl (IPR-index dispatch gap for the
          scoreboard-annotated index forms the PAL uses, e.g.
          <EV6__I_CTL ! ^x20>), or
    (ii)  the restored VALUE lacks VPTB (CNS__VA_CTL saved before
          VPTB ever existed and never refreshed -- e.g. the OS set
          VPTB via a path EmulatR's intercepts kept out of the
          save/restore loop), or
    (iii) VM-mode execution after the LAST restore ran with a
          different (stale) va_ctl for another reason.

## 4. Next (N4) -- one run decides among (i)/(ii)/(iii)

  The restore block's IPR reloads pass through r0 (HW_LD r0,
  CNS__*(r1) ... hw_mtpr r0, <IPR>).  DIAG window over the restore
  tail (0xe55c-0xe700) + DIAG_WREG=0 logs every restored IPR value:
    - r0 for the CNS__VA_CTL reload SHOWS VPTB -> value good ->
      suspect (i): EmulatR's HW_MTPR index decode for the annotated
      VA_CTL/I_CTL forms (verify against PalEntries.cpp switch and
      the EV6 index encoding; the MFPR side demonstrably works).
    - r0 LACKS VPTB -> suspect (ii): trace where CNS__VA_CTL got its
      value (PA-WATCH on the impure CNS__VA_CTL cell).
  Fix altitude follows directly; HRM-verify any IPR-decode change
  (memory.md Sec 4 rule).

--------------------------------------------------------------------------------
## 5. Files touched

  - this journal   NEW
  No emulator code changed.
