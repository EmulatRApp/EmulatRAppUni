<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-026
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-026 -- HALT-10 CLOSED END TO END.  MTPR_VPTB shadowed the
#                 PAL routine INCOMPLETELY (2 of 3 side effects); the
#                 missing PT__VPTB store desynced the guest's page-table
#                 reference from its own IPRs.  Fixed, boot-proven:
#                 the DS20 now reaches SYSBOOT.

    Doc id   : JRN-SCSI-026
    Date     : 2026-07-26
    Status   : FIX APPLIED + VERIFIED (architect-approved, gates A1-A4).
    Closes   : the halt-10 arc JRN-SCSI-021..025.
    Relates  : JRN-SCSI-020/021 (the EXTxH fix that got us here),
               JRN-VMB-010 (whose "MTPR_VPTB is dead code" verdict is
               reconciled in Sec 6).

--------------------------------------------------------------------------------
## 1. The defect

  EV6_VMS_CALLPAL.MAR :1524-1545 -- CALL_PAL MTPR_VPTB has THREE side
  effects:
      (a) hw_stq/p r16, PT__VPTB(p_temp)      ; the MEMORY copy
      (b) bis p7,r16 -> EV6_MTPR VA_CTL       ; D-side IPR
      (c) merge      -> hw_mtpr <I_CTL ! ^x20>; I-side IPR
  EmulatR's execMtprVptb_vms implemented (b) and (c) -- added 2026-07-19
  when VPTB was found stranded in cpu.vptb -- and NOT (a).

  Why (a) is the load-bearing one: the guest's OWN miss handlers read
  the CELL, not the IPRs.  DTBM_DOUBLE_3's rev-1.60 self-check
  (EV6_VMS_PAL.MAR ~1115) formats a PTE address FROM THE IPRs and
  compares its <63:33> against PT__VPTB FROM MEMORY.  Update one side
  only and the two disagree by construction -- from the first OS
  MTPR_VPTB onward.

## 2. The causal chain (each link independently evidenced)

  1. EXTxH fix (JRN-SCSI-020/021) -> APB parses the topology string,
     accepts the boot device, transfers to the OS bootstrap.
  2. OS bootstrap (0x29a70-0x29ffc) arms its own page tables:
     CALL_PAL MTPR_VPTB at pc 0x29dc4 with R16 = 0xFFFFFEFC_00000000
     (the OpenVMS S0 base).                       [A2 sensor capture]
  3. EmulatR updates va_ctl + i_ctl; PT__VPTB keeps the CONSOLE-era
     0x2_0000_0000.  Sensor line shows the split verbatim:
       VPTB-DIAG[mtpr] ... R16=0xfffffefc00000000
         va_ctl=0xfffffefc00000000 i_ctl=0x0000fefc00340087
         VA_FORM=0xfffffefc000000a0
  4. 322 cycles later a walk of the OS's own PTE VA computes
     VA_FORM=0x00000003bf000000 -- a bare offset -- because that path
     reads the stale base.                        [same log]
  5. sys__enter_console strips I_CTL VPTB and clears PT__VPTB; no
     restore follows (JRN-SCSI-025) -- because the OS's arming was
     never coherent to begin with.
  6. Fetch at 0x2a000 -> ITB miss -> VPTE load double-misses ->
     DTBM_DOUBLE_3 crash1 -> "halt code = 10, PC = 2a000".

## 3. The fix

  palBoxLib/grains/PalEntries.cpp execMtprVptb_vms: add side effect (a)
  as a deferred memEffect -- raw R16, 8 bytes, to p_temp+0x0
  (PT__VPTB = ^x0, EV6_PAL_TEMPS.MAR:33; p_temp = PAL-bank r21 =
  intShadow[5], the :901 seed-path addressing).

  TWO-CUT NOTE (kept because it is the lesson): the first cut set the
  memEffect fields WITHOUT S_PhysAddr.  The .mar uses hw_stq/p -- a
  PHYSICAL store -- so the Mbox translated p_temp as a VIRTUAL address,
  the store DTB-missed, the VPTE walk double-missed, and DTBM_DOUBLE_3
  halted 0x0A *at the MTPR instruction*: the wall MOVED from PC 0x2a000
  to PC 0x29dc4.  Corrected by ORing S_PhysAddr | S_Store as
  execStqp:1466 does.  Both flags are now pinned by doctest.

  GUARD (architect ruling, hard-stop over silent degradation): an
  out-of-range p_temp emits a loud line and FAULTS (kFaultUnimplemented)
  rather than skipping the store -- a skipped store recreates this exact
  desync under a different precondition.

## 4. Verification (do-no-harm gate)

  V1  VPTB/byteops doctest cluster: 17 cases / 2813 assertions PASS.
      New cases pin ALL THREE side effects by name, the S_PhysAddr /
      S_Store flags, the fault-not-skip guard, and the VMS-shaped value
      0xFFFFFEFC_00000000 across the I_CTL 47:30 store + SEXT-from-47
      read (HRM Fig 5-22) and all three VA_FORM compositions (Figs
      5-5/5-6/5-7).
      NOTE the pre-existing MTPR_VPTB test FAILED first -- it encoded
      the old 2-of-3 contract and tripped the new guard.  Rewritten.
  V3  Full suite: 500 cases, 497 pass -- exactly the 3 pre-existing
      drift failures (ide_wiring + 2x mmio_csc, JRN-SCSI-003).
  V2  Cold DS20 boot, `b dka0.0.0.8.0 -flags 0`:
        halt code = 10  ->  GONE.
        %SYSBOOT-F-LDFAIL, unable to load SYS$PUBLIC_VECTORS.EXE,
        status = 0013809A
      The message is now OpenVMS's OWN loader reporting a device-level
      failure -- a LAYER TRANSITION, not a PAL consistency halt.
      Reproduced twice (12.51 s / 12.79 s console time): deterministic.
  No snapshot version bump (no state-layout change).

## 5. A4 -- the PT__ scratch-desync audit (this is a defect CLASS)

  Method (row procedure): enumerate the .mar writers/readers of the
  cell; find every EmulatR leaf that shadows those CALL_PALs; diff the
  side-effect sets; confirm live with a sensor before any fix.

  | Cell / field | .mar contract | EmulatR leaf | Verdict |
  |---|---|---|---|
  | `PT__VPTB` (+0x0) | MTPR_VPTB stores raw R16 (CALLPAL:1534); TB-miss handlers + DTBM_DOUBLE_3 self-check READ it | `execMtprVptb_vms` | **WAS DESYNCED -> FIXED** (Sec 3), boot-proven |
  | `PT__PTBR` (+0x8) | SWPCTX stores PFN<<13 (CALLPAL:430); TB-miss handlers read it as the WALK ROOT (VMS_PAL:1107/1166/1505/1778) | `execSwpctx` writes HWPCB + cpu.ptbr, no scratch write | **CLEAN** -- sensor (EMULATR_PTBR_DIAG) fired ZERO times across a full boot to SYSBOOT: the leaf is not on this path (SWPCTX diverts to the guest PAL, which does its own store) |
  | `cpu.ptbr` (field) | n/a -- EmulatR abstraction | maintained only by `execSwpctx`; consumers are DIAG PRINTFS ONLY | **LATENT TRAP** -- reads 0x0 in the OS era; harmless today, authoritative-looking tomorrow.  Any future consumer (Ev6Translator HW-walk harvest, TB translation-root cache) inherits stale state.  Mark non-authoritative or maintain on every path. |
  | memEffect addressing | `hw_stq/p` = PHYSICAL | leaf must OR `S_PhysAddr` (absence silently means VIRTUAL) | **CLASS RULE** -- cost one wall-migration this session (Sec 3) |
  | `PT__VA_CTL` (source) | MTPR_VPTB ORs r16 into the control part read FROM MEMORY | leaf merges into the LIVE cpu.va_ctl | **RESIDUAL DIVERGENCE** -- equivalent while the two agree; closing it needs a 2nd memory access (breaks the one-access-per-leaf budget).  Documented, not changed. |
  | I_CTL VPTB width | .mar ORs ALL of r16 bits >=30 | leaf masks to 47:30 (HRM field width) | **BENIGN, PROVEN** -- 47:30 store + SEXT-from-47 read round-trips 0xFFFFFEFC_00000000 losslessly for canonical bases; live log shows the reconstruction |

  Remaining cells to sweep (writer census from the VMS .mar, by
  frequency): PT__TRAP(95), PT__FAULT_PC(50), PT__FAULT_SCB(42),
  PT__FAULT_R4(31), PT__CALL_PAL_PC(28), PT__R3(25), PT__HALT_CODE(19),
  PT__NEW_PS(8), PT__PCBB(3), PT__SCBB(2), PT__PRBR(2), PT__KSP(3),
  PT__M_CTL(3), PT__DTB_ALT_MODE(3), PT__WHAMI(1), PT__IMPURE(1)...
  Priority = cells an INTERCEPTED CALL_PAL writes (the MFPR/MTPR_* and
  SWPCTX/CHMx families) -- the rest are PAL-internal and unreachable
  by a leaf.

## 6. Reconciling JRN-VMB-010

  That journal ruled execMtprVptb_vms "dead code -- never fires on
  DS20".  CORRECT IN ITS SCOPE: the CONSOLE boot never calls it.  The
  OS era is its first live caller (pc 0x29dc4), which is exactly where
  an incompletely-shadowed side effect first bites -- and why the
  defect survived every console-era test.  Scope-limited "dead code"
  verdicts should carry their scope in the verdict.

## 7. Next

  - SCSI device model, NOT another PAL desync: the SYSBOOT window shows
    185 console callbacks and ~zero disk reads, with the only disk
    command being `VirtualDiskDevice: UNSUPPORTED opcode 0x15 (len 6)
    -> ILLEGAL REQUEST` = MODE SELECT(6), plus 9x `Ncr53C810: data-in
    move 255 > available 36 -- padded`.  SYSBOOT fails to CONFIGURE the
    drive and never reaches the file read.  Status 0x0013809A decodes
    severity=2 (ERROR), facility=0x13, msg=4115 -- device/driver class.
    Work item: implement MODE SELECT(6)/MODE SENSE + the mode pages,
    and fix data-in length negotiation.
  - Deliverable (2), semantics integrity (architect-endorsed): memory-
    effect invariant asserts (loud-halt in debug / EMULATR_DIAG_*
    builds, `((void)0)` in release -- leaves are noexcept); .mar
    citations accreted per audited row; and a TB-eligibility rule in
    `jit_qualifying_ruleset.md` -- a grain whose leaf lacks a
    citation-verified semantics entry is TB-INELIGIBLE.  Rationale to
    state there: an interpreter with wrong semFlags yields a wrong
    result at a traceable PC; a TB tier compiles that lie into every
    execution of the block with no retire record pointing at it.  An
    uncited leaf defaulting to interpreter-only is the safe failure
    mode -- the class stays closed by construction, not by vigilance.

## 8. Files touched

  - palBoxLib/grains/PalEntries.cpp   FIX (PT__VPTB store + guard);
                                      EMULATR_VACTL_DIAG_N cap knob;
                                      NEW EMULATR_PTBR_DIAG sensor
  - tests/palBoxLib/test_palentries.cpp  MTPR_VPTB 3-effect contract
  - tests/coreLib/test_byteops.cpp       VPTB/VA_FORM cluster
  - tools/srm_console_driver.py          3rd LFU prompt (option-firmware
                                         device) -- a boot stalled ~40
                                         min on it this session
  - this journal                         NEW
