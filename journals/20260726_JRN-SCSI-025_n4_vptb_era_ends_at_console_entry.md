<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-025
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-025 -- N4 DONE: the restore contract WORKS (VPTB is really
#                 installed into VA_CTL and I_CTL) -- but the VPTB ERA
#                 ENDS at the last sys__enter_console, ~313M cycles
#                 BEFORE the wall.  The OS runs its whole life with
#                 VPTB=0 in both control registers.

    Doc id   : JRN-SCSI-025
    Date     : 2026-07-26
    Status   : PROBE RECORD.  Diagnostic-only code change (cap knob).
    Relates  : JRN-SCSI-024 (VA_FORM VPTB=0), -022 (crash1 chain).
    Input    : run_ds20_showdev_20260726_195956.log
               (EMULATR_VACTL_DIAG=1 EMULATR_VACTL_DIAG_N=200000;
               54,039 VA_CTL/I_CTL writes across a full boot to the
               halt-10 wall).

--------------------------------------------------------------------------------
## 0. Instrumentation fix (why this run saw what earlier ones could not)

  The VACTL probe's cap was hardcoded 128 and filled during console init
  (~cyc 1.18e9) -- a BILLION cycles before the wall, so every earlier
  look at this register pair was blind to the OS era (the same
  starved-probe failure JRN-VMB-008 R1 notes for the shared MMU probe).
  PalEntries.cpp now reads EMULATR_VACTL_DIAG_N (default 128, unchanged
  when unset).  Diagnostic-only; inert without the env var.

## 1. The restore contract DOES work (suspects (i) and (ii) both dead)

  228 writes carry the CORRECT VPTB 0x2_0000_0000 -- to BOTH registers:
    cyc 1824560965  pc 0xe5fd  VA_CTL <- 0x2_0000_0000   (restore tail)
    cyc 1824561078  pc 0xe7c1  I_CTL  <- 0x2_0034_0007    (VPTB present)
  So: EmulatR's HW_MTPR dispatch handles the PAL's annotated index forms
  (confirmed statically in the N3 tick: iprSelector uses scbd bits[15:8]
  and ignores the ^x20 extension), the saved CNS__VA_CTL value is good
  (N4's r0 probe), and pal__restore_state genuinely re-installs VPTB on
  BOTH sides.  JRN-SCSI-024 Sec 4's suspects (i)/(ii) are CLOSED.

## 2. The real shape: a VPTB ERA that ends before the OS needs it

    cyc 1.8246e9 .. 1.8522e9   VPTB present (228 writes, both regs)
    cyc 1.8522e9               LAST VPTB-bearing write
    cyc 1.8522e9 + 259         pc 0x13351/0x13381: I_CTL <- vptb 0,
                               VA_CTL <- 0x2 (va48=1)  == sys__enter_console
                               (JRN-SCSI-024 Sec 1: strip VPTB, set 48-bit
                                for the 1-to-1 console)
    cyc 1.8523e9 .. 2.1658e9   ~313M cycles, thousands of writes,
                               EVERY ONE vptb=0 -- including the entire
                               post-boot APB/OS era and the fetch at
                               0x2a000 that walls.
  So the console entry that ends the VPTB era is never followed by a
  restore.  The OS-side code then executes with VPTB=0 in both control
  registers -> IVA_FORM/VA_FORM produce the bare (va>>10) offsets logged
  in JRN-SCSI-024 Sec 2 -> the DTBM_DOUBLE_3 self-check compares 0
  against PT__VPTB and halts 0x0A.

## 3. The four 0xfffffefc writes -- ATTRIBUTED, not garbage

    cyc 1852246415/427/460/463  pc 0xdfd1/0xe001/0xe085/0xe091
    I_CTL <- 0x0000fefc00340007  => vptb field = 0xfffffefc_00000000

  ATTRIBUTION (PC histogram over the whole run -- free, no run needed):
  those four PCs each wrote the CORRECT VPTB 0x2_0000_0000 **22 times**
  and the 0xfefc value ONCE -- i.e. 23 rounds of the SAME routine, the
  last one carrying a different value.  This is not anomalous code; it
  is ordinary code with a changed input.  Disassembly (0xdfc0-0xe017):
  `HW_MTPR r0, I_CTL` serializing instructions interleaved with
  `HW_ST r9..r13, 0x58..0x78(r1)` -- the save-state register spill.
  The low bits are the normal I_CTL control image (0x00340007/0x00340087);
  ONLY the VPTB field differs.  And 0xfffffefc_00000000 is a plausible
  OpenVMS S0-space page-table base, not a corrupted bit pattern.
  READING (b) IS LIVE: something armed a NEW (OS-era) VPTB immediately
  before the last console entry.  Note JRN-VMB-010's "MTPR_VPTB is dead
  code" verdict was scoped to the CONSOLE boot; the OS era would be its
  first live customer.

## 3b. Snapshot cross-check: the saved image is INTACT

  Post-boundary snapshot (predig_oemsnap_cyc1899059989, taken at cyc
  1.899e9 -- after the era ends, before the wall):
      PT__IMPURE      = 0x4200
      PT__VPTB (live) = 0x0000000000000000     <- stripped
      CNS__VA_CTL     = 0x0000000200000000     <- VPTB STILL PRESENT
      CNS__I_CTL      = 0x0000002060000000
      CNS__PTBR       = 0x000000003ff04000
  The SAVED image still carries VPTB while the live PAL temp does not.
  So the save side is healthy and nothing clobbered CNS post-save: the
  restore for the final console entry PROVABLY NEVER EXECUTED.
  => "CNS clobbered post-save" is ELIMINATED.  The remaining split is
  architectural-skip vs tail-cut (N5 rows 1/2).

## 3c. Loose end CLOSED: halt code 10 has a mechanism-name

  The apisrm/.mar halt-code enumeration search (codes 1..7 named,
  10 absent) is now ANSWERED-BY-MECHANISM and needs no further
  grepping: 10 = ^x0A = the VMS PAL DTBM_DOUBLE_3 rev-1.60 self-check
  crash1 (JRN-SCSI-022 Sec 3).  It is a PAL-internal consistency halt,
  not an OS/console reason code -- which is exactly why it never
  appears in kernel.c's hlt_table or dp264_info.c's hlttxt[].

## 4. Next (N5) -- decide at the boundary

  N5a  DIAG window over 0xdf00-0xe100 + 0x13300-0x13400 gated to
       CYCLO ~1852246000 CYCHI ~1852247200 (a ~1200-cycle capture):
       shows the exact instruction flow from the garbage writes through
       the final enter_console, and WHO calls it (return address /
       preceding frame) -- i.e. is this the OS asking for the console,
       or an EmulatR divert that never comes back?
  N5b  Correlate with the CSERVE/callback log lines in the same window
       (the run log already carries CSERVE-ROUTE / divert traces).
  N5c  From N5a: if the final enter_console is an EmulatR-side divert
       that skips the paired restore, the fix is in that divert path;
       if it is a faithful guest console entry, then the missing piece
       is the OS's own post-console VPTB re-establishment -- which on
       real hardware comes from CALL_PAL MTPR_VPTB, a path EmulatR
       intercepts (PalEntries.cpp execMtprVptb_vms) and whose
       interception may be bypassed by the divert-to-guest-PAL stack.

--------------------------------------------------------------------------------
## 5. Files touched

  - palBoxLib/grains/PalEntries.cpp   diagnostic cap knob
                                      (EMULATR_VACTL_DIAG_N)
  - this journal                      NEW
  No functional emulator code changed.
