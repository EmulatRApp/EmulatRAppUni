<!--
EmulatR V5 -- Design Journal JRN-SCSI-001
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-001 -- PCI SCSI HBA + virtual disk: design + phased plan
#                 (DISCUSS-FIRST: no code changed; this is the proposal)

    Doc id   : JRN-SCSI-001
    Date     : 2026-07-24
    Status   : DESIGN FOR REVIEW.  Motivated by JRN-VMB-022: the boot
               media's APB has NO "IDE" protocol; its accepted set is
               {DVA_,RAID,SCSI,MSCP,FLOP,MOP_,BOOT}.  A SCSI boot disk
               (console "SCSI 0 ..." topology string) is the direct
               unblock that this APB can consume.  This REVERSES
               JRN-VMB-018/-019's "SCSI HBA not a prerequisite" (that
               rested on the now-refuted "IDE boot supported" premise).
    Relates  : JRN-VMB-022/-021/-020; tasks_20260612_boot_pci_deploy
               (#37-#42, esp. B1); Device_Enumeration_Scaffold_Spec;
               REFERENCE_INDEX "d06" designation.

--------------------------------------------------------------------------------
## 1. HBA selection: NCR 53C810 (PKE)  [decision + alternatives]

The DS20 v7.3-2 console's authoritative PCI bind table (apisrm
`ref/io_device_list.h`) offers two SCSI HBA families the firmware can
drive to a bootable dk device:

  { TYPE_PCI, 0x00011000, .. "n810", "NCR 53C810",     "pk", &pk_controllers, "PKE"     }
  { TYPE_PCI, 0x10201077, .. "isp1020", "QLogic ISP1020", "pk", &pk_controllers, "ISP1020" }

Both drivers are PRESENT in the RUNNING DS20 v7.3-2 console (verified in
the NOIOVEC halt snapshot: live symbols pks_owner/pks_rx_s @PA 0x1825a8
and pke_isr/pke_owner @PA 0x180b90; PCI name table @PA 0x198750 lists
NCR 53C810/825/875/895/895A/896 + QLogic ISP10x0).

RECOMMENDED: **NCR 53C810 (VID 0x1000, DID 0x0001, class 0x010000)**.
  - AXPBox implements it (`axpbox/src/Sym53C810.cpp`, 2,626 lines) and
    BOOTS VMS through it -- the mirror-AXPBox convention gives us a
    proven secondary reference for every register/SCRIPTS ambiguity.
  - The console-side contract is small and FULLY SOURCED: `pke_driver.c`
    (1,282 lines) + `pke_script.mar` (192 lines -- the EXACT SCRIPTS
    program the firmware downloads/runs) + `ncr810_def.h` (register
    authority, 511 lines) + `scsi.c` (class driver, shared with pks).
  - Era-correct for the old APB (A13-03): 53C810 = KZPAA, the
    VMS-native SYS$PKEDRIVER device of that vintage.
  - REFERENCE_INDEX already designates ncr810_def.h/sym_def.h as "the
    d06 SCSI controller -- Step-4 register-model authority for the
    SRM-boot-faithful SCSI HBA" -- this design executes that intent.
ALTERNATIVE (not recommended now): QLogic ISP1020 (pks) -- mailbox +
downloadable RISC firmware handshake; no AXPBox/QEMU-adjacent model;
more un-corroborated surface.  53C895 (AXPBox also has it) buys wide/
ultra SCSI nothing in the boot path needs; 810 is the smaller contract.

--------------------------------------------------------------------------------
## 2. What already exists (leverage; audited 2026-07-24)

  deviceLib/scsi/            -- SCSI TARGET layer already in-tree:
    IBlockMedia.h, FileBlockMedia.h, BlockMediaFactory.h (media seam),
    ScsiCommand.h, ScsiSenseData.h, ScsiTypes.h,
    VirtualScsiDevice.h (abstract LUN target: deviceType() +
    handleCommand(ScsiCommand&)), VirtualIsoDevice.h (CD target used by
    the ATAPI path).  => The HBA hands ScsiCommand to targets; we add a
    DISK target, not a new command layer.
  systemLib/ManifestPciDevice.h (config-space images + BAR size-probe,
    :59-81), deviceLib/Tsunami/PciConfigSpace.h.
  deviceLib/Tsunami/Dec21143Tulip.h:55-59,356-362 --
    setRangeCallbacks/programBar BAR-rebind hooks (pattern exists,
    nothing invokes them yet = exactly Track B item B1).
  chipsetLib/TsunamiPchip.h:1158-1186 type-0 config dispatch;
    :401-420 translateDmaToPa (direct-map window only);
    EMULATR_PCI_CFG_TRACE :1116-1127.
  chipsetLib/TsunamiChipset.h:430-444 INTx -> DRIR routing helpers.
  systemLib/Machine.cpp:517-543 manifest-driven PCI registration.
  Manifest schema (ds20_v7_3_platform.json): pci_devices[] with
    model/hose/bus/slot/func/vendor/device/class_code/bars[]/
    interrupt_pin + nested storage[] (Cypress IDE shows the pattern).
  vStorage: Alpha/dka0.vdisk (currently attached as IDE dqa0!).

--------------------------------------------------------------------------------
## 3. Proposed architecture (new code)

  A. deviceLib/scsi/VirtualDiskDevice.h  -- direct-access target
     (SPC/SBC peripheral type 0x00) over IBlockMedia.  Boot-path command
     set: TEST UNIT READY(0x00), REQUEST SENSE(0x03), INQUIRY(0x12),
     MODE SENSE(6)(0x1a) [+(10) 0x5a], READ CAPACITY(10)(0x25),
     READ(6)(0x08), READ(10)(0x28); write path WRITE(6/10) in P4.
     Mirrors VirtualIsoDevice's shape; sense/status via ScsiSenseData.

  B. deviceLib/Tsunami/Ncr53C810.h  -- the HBA (IPciDevice via the
     ManifestPciDevice seam):
     - Register file per ncr810_def.h: SCNTL0-3, SDID, SIEN, SCID,
       SXFER, SODL, SOCL, SFBR, SIDL, SBDL, SBCL, DSTAT, SSTAT0-2,
       DSA, ISTAT, CTEST0-8, DFIFO, DBC/DCMD, DNAD, DSP, DSPS,
       SCRATCH, DMODE, DIEN, DWT, DCNTL, ADDER.
     - SCRIPTS interpreter: the ops pke_script.mar actually uses
       (block-move in/out per phase, SELECT ATN, WAIT DISCONNECT/
       RESELECT, JUMP/CALL conditional on phase/data, INT, register
       move) -- implement the used subset faithfully, fault loudly
       (EMULATR_SCSI_TRACE) on anything outside it.
     - Bus/target dispatch: HBA owns scsi id map -> VirtualScsiDevice*
       (id 0 = VirtualDiskDevice on the manifest media; HBA itself
       id 7 per pke convention).
     - DMA through the chipset seam (translateDmaToPa direct-map;
       see Sec 4 DMA note).  INTx via TsunamiChipset helper -> DRIR.
  C. Manifest (ds20_v7_3_platform.json) new pci_devices entry:
       { "name": "pka_53c810", "model": "ncr53c810", hose 0, bus 0,
         "slot": <see Q2>, "func": 0, "vendor": "0x1000",
         "device": "0x0001", "class_code": "0x010000",
         "interrupt_pin": 1,
         "bars": [ io 0x100, mem 0x100 ],
         "storage": [ { "unit": 0, "type": "scsi_disk",
                        "media": <see Q3>, "media_kind": "image" } ] }
  D. Machine.cpp wiring: model string "ncr53c810" -> construct HBA,
     attach targets from storage[], register config space + BAR hooks.

--------------------------------------------------------------------------------
## 4. Prerequisites / interlocks (Track B)

  B1 BAR-write -> decode REBIND: REQUIRED FIRST.  The SRM assigns the
     810's IO+MEM BARs before pke init; without live re-routing the
     register file is unreachable.  The tulip hook pattern
     (setRangeCallbacks/programBar) generalizes: invoke from the config
     write path; Pchip register/unregister io-port + mem ranges.
     Acceptance = the JRN-VMB-019 B1 doctest (write BAR -> decode
     moves) + SRM's own assignment replays clean under
     EMULATR_PCI_CFG_TRACE.  This lands as its OWN commit before any
     HBA code (it also benefits tulip/PCI #41 independently).
  DMA: console pke SCRIPTS do bus-master DMA; SRM-era access goes
     through the Pchip direct-map window (translateDmaToPa handles).
     RISK FLAGGED: VMS runtime driver may program SG windows (B4);
     defer until the boot path demands it (JRN-018 authority check said
     boot-time dq path was PIO; pke path is DMA but console windows are
     direct-map -- verify live at P1 with EMULATR_PCI_CFG_TRACE).
  B2 IDSEL boundary: unchanged, cheap, fold into the B1 commit.

--------------------------------------------------------------------------------
## 5. Phases + acceptance gates (each gate = scripted-console run,
      VMB-020 Sec 3 runbook; every phase its own approval + commit)

  P0  Manifest + config space ONLY (no function): `show config` lists
      "NCR 53C810"; pke probe may fail gracefully; boot to >>> clean;
      DS10/ES40 unaffected (manifest is DS20's).
  P1  B1 rebind + register skeleton + INTx: pke init survives, no
      wedge; `show dev` shows pka0.7.0.<slot>.0 controller.
  P2  SCRIPTS interpreter + phase engine + VirtualDiskDevice:
      `show dev` enumerates dka0.0.0.<slot>.0 (INQUIRY/TUR/READ
      CAPACITY flow through SCRIPTS end-to-end).
  P3  `b dka0`: boot block via READ, VMB/APB load, and THE TEST:
      topology string becomes "SCSI 0 <105-analog> ..." -> the
      VMB-021/-022 resolver should now MATCH -> past %APB-F-NOIOVEC
      into bootdriver init (next frontier: multi-block reads #32,
      SYSBOOT gates, remaining PCI #41).
  P4  WRITE path + error/sense robustness + VMS-runtime needs (B4 SG
      DMA if demanded).  Regression: DS10/ES40 to >>>, dqa enumeration
      unchanged.

  Verification assets: doctests for each SCRIPTS op against
  pke_script.mar sequences; AXPBox Sym53C810 as corroboration when the
  HRM/def leaves ambiguity (EmulatR PRIMARY, AXPBox secondary); the A2
  replay oracle (VMB-020) for any suspect execution window.

--------------------------------------------------------------------------------
## 6. OPEN QUESTIONS (need Tim's call before code)

  Q1 HBA: NCR 53C810 as recommended?  (vs ISP1020 / 53C895)
  Q2 Slot layout: DS20E reference layout = SCSI at slot 7, DE500 at
     slot 9 (manifest comment; REFERENCE_INDEX).  Options:
       (a) FAITHFUL: move tulip 7 -> 9, SCSI at 7 (changes ewa
           badge/slot in show config; topology strings change),
       (b) MINIMAL: SCSI at free slot 8, tulip stays at 7.
     Recommend (a) for fidelity, accepting the churn.
  Q3 Media: move Alpha/dka0.vdisk from the IDE dqa0 attach to the SCSI
     target (its name finally becomes true, and it carries the very
     APB/VMS system we analyzed -- the direct NOIOVEC retest), leaving
     dqa0 empty or with a scratch image?  Or a fresh image for SCSI and
     keep dqa0 as-is?  Note the SAME image must not be attached to
     both simultaneously.
  Q4 Placement/naming: deviceLib/Tsunami/Ncr53C810.h alongside the
     other PCI devices, or start deviceLib/pci/?  (Recommend staying in
     Tsunami/ = current convention, refactor later if a second chipset
     needs it.)
  Q5 Sequencing: B1 (+B2) as a standalone approved commit FIRST, then
     P0..P4?  (Recommended.)

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
