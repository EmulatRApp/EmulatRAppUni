<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-022
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-022 -- N1 DONE: halt-10 @ 0x2a000 decoded end to end.  The
#                 guest is EXONERATED (PTE valid, code present); the
#                 halt is the VMS PAL's DTBM_DOUBLE_3 1.60 self-check
#                 (PTE-VA<63:33> != VPTB); prime suspect = EmulatR's
#                 IVA_FORM/VA_FORM formatting (43- vs 48-bit mode).

    Doc id   : JRN-SCSI-022
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-021 (the new wall), memory.md Sec 5
               "Ev6Translator harvest" (VA-form-aware decode = the
               pre-named gap), JRN-VMB-016 (PT__VPTB seeding history).
    Inputs   : run_ds20_showdev_20260726_171642.log (DIAG window
               0x28000-0x2c000, 451 records) + NEW post-halt snapshot
               snapshots/predig_oemsnap_cyc2546097000.axpsnap (captured
               via the EMULATR_CONSOLE_SNAPSHOT marker at the post-halt
               P00>>>; scratch driver).

--------------------------------------------------------------------------------
## 1. The code at 0x29xxx (probe 2, trace decode)

  C-compiled OS-bootstrap code (SYSBOOT territory): GP-cell region
  0x1028-0x14e0 -> data 0xf5xx-0x168xx; reads HWRPB+0x50/+0x58; runs a
  quadword twos-checksum loop (0x29df0-0x29e14) and STOREs the result
  to HWRPB+0x120 = hwrpb$Q_CHKSUM (hwrpb$K_CHKSUM = 288, apisrm
  apu_hwrpb_def.h:329) -- the canonical "update HWRPB fields then
  re-checksum" sequence of an OS bootstrap taking HWRPB ownership.
  Its epilogue is CUT at the page boundary: r2..r6 restored, then the
  fetch at 0x2a000 takes kFaultItbMiss and never completes.

## 2. The guest is exonerated (probe 1, snapshot ptwalk)

  snap_ptwalk (PTBR 0x3ff04000) on the post-halt snapshot:
    VA 0x29000/0x29a70 -> L3[0x14] PTE PFN 0x36c [V,ASM,KRE,KWE]
                          -> PA 0x6d9xxx (the traced code, verified)
    VA 0x2a000         -> L3[0x15] PTE PFN 0x36d [V,ASM,KRE,KWE]
                          -> PA 0x6da000, first QW = LDQ r7,0x38(r29) /
                          LDQ r8,0x40(r29) -- THE REST OF THE EPILOGUE.
  The image is fully loaded and fully mapped, no FOE, KRE set.  The
  short-load and short-map hypotheses are DEAD.

## 3. Halt code 10 named (VMS PAL source)

  EV6_VMS_PAL.MAR ~1115-1131, inside START_HW_VECTOR <DTBM_DOUBLE_3>
  under `.if ne check_ebox_iprs` (rev 1.60 self-checks):

      hw_ldq/p r25, PT__VPTB(p_temp)
      srl  p4, #33, r26      ; p4 = va of level 3 PTE
      sll  r26, #33, r26     ; keep <63:33>
      xor  r25, r26, r25
      bne  r25, trap__dbm_double3_crash1
      ...
    trap__dbm_double3_crash1:
      lda  p20, ^x0A(r31)    ; crash code = 0x0A = 10
      hw_stq/p p20, PT__HALT_CODE(p_temp)
      br   trap__halt_after_fix

  (Sibling checks: 0x0B = byte-op sanity, 0x0C = rpcc sanity --
  matching the run's decimal "halt code = 10" with no text line, since
  kernel.c names only 1..7.)  Consistent with the fault log: the wall
  produces NO non-routine fault -- the PAL halts "cleanly".

## 4. The fatal chain + the suspect

    fetch VA 0x2a000
      -> ITB miss (kFaultItbMiss) -> guest ITB_MISS handler
      -> hw_ld /v of the L3 PTE at the IVA_FORM-formatted VA
      -> that load double-misses -> DTBM_DOUBLE_3
      -> 1.60 check: formatted-PTE-VA<63:33> != PT__VPTB -> HALT 0x0A

  With the guest tables valid, the corrupt item is the FORMATTED PTE
  VA delivered by EmulatR's IVA_FORM/VA_FORM IPR emulation.  The
  arithmetic fits a MODE error exactly: VPTB here is the L1[1]
  self-map = 0x2_0000_0000 (bit 33).  43-bit VA_FORM keeps
  VPTB<63:33> (check passes); a 48-bit-mode format keeps only
  VPTB<63:38> = 0 -> formatted VA top bits = 0 != VPTB -> crash1.
  memory.md Sec 5 ALREADY flags "VA-form-aware (43/48-bit) segment
  decode" as a gap in mmuLib/Ev6Translator.h (Ev6Translator-harvest
  note; kseg SPE 48-bit-hardcoded).  Secondary suspect: PT__VPTB
  memory-copy divergence from the VPTB IPR (JRN-VMB-016 seeding
  history) -- distinguishable because the mode error mangles p4 while
  seeding errors mangle r25.

  Why 0x29xxx worked and 0x2a000 died: both PTEs sit in the SAME PTE
  page; the earlier region-entry ITB miss resolved WITHOUT a double
  miss (PTE page still in DTB).  Only the double-miss PATH runs the
  1.60 check -- the failure needs an ITB miss whose PTE load also
  DTB-misses, which is why the wall appears "sometimes, later".

## 5. Next (N2')

  N2a  Static: read EmulatR's IVA_FORM/VA_FORM implementation
       (iBox/mmuLib) against the EV6 HRM formatting for VA_CTL
       VA_FORM_32/VA_48 modes, and against what the VMS PAL programs
       into VA_CTL at PAL init.  If the mode term is wrong or
       hardcoded, that is the fix site (HRM-verify per memory.md
       Sec 4 rules before changing any decode).
  N2b  Dynamic confirm: DIAG window over the DTBM_DOUBLE_3 vector
       region (palBase+0x300..+0x400) + DIAG_WREG on r26 (the
       srl/sll-cleaned p4 bits) -- one boot shows the mismatching
       values verbatim.
  N2c  After any fix: re-run V2; expected next stop is deeper SYSBOOT
       (or the real SYSBOOT> prompt, the session goal).

--------------------------------------------------------------------------------
## 6. Files touched

  - this journal   NEW
  - scratchpad halt10_snapshot_driver.py (session-temp, not committed)
  No emulator code changed.
