<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-008
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-008 -- ds20_v7_3_platform.json vs live device discovery.  The slot
#                 naming is CORRECT (100*func+slot, proven from the DS10
#                 golden).  Four real deltas; one fixed here.  Plus a
#                 recommendation on dqb / dkb, and why NOT to land it yet.

    Doc id   : JRN-SCSI-008
    Date     : 2026-07-25
    Status   : ANALYSIS RECORD + one manifest fix.  No emulator code changed.
    Relates  : JRN-SCSI-004 Sec 1 (the live gate that produced this device
               list), JRN-SCSI-007 (env audit), JRN-SCSI-005 Sec 1 (AXPBox
               presenting the same media as RZ58).
    Evidence : console_env_baseline_ds20_scsi_20260725_222935.log (operator
               capture, P00>>> -- SH DEV D + SHOW boot* + a dka100 boot),
               tools/srm_conformance/golden/ds10_v7_3.golden.log (real DS10),
               RelWithDebInfo/logs/run_ds20_p3retest_20260725_004639.log.

--------------------------------------------------------------------------------
## 0. NOT a delta: the "105" in dqa1.1.0.105.0

  The SRM device-name slot field is 100 * func + slot.  Proven on REAL
  hardware, from our own golden log:

      bus 0, slot 16, function 0 -- pga -- FCA-2684
      bus 0, slot 16, function 1 -- pgb -- FCA-2684
      open fibre pga0.0.0.16.0
      open fibre pgb0.0.0.116.0        <- func 1 at slot 16 -> 116

  Cypress IDE is func 1 at slot 5, so 100 + 5 = 105 is REQUIRED, and it is the
  same 105 that appears in the "IDE 0 105 0 0 0 0 0" topology string the whole
  NOIOVEC track has been reading (JRN-VMB-022, JRN-SCSI-004).  Likewise
  dka<id*100+lun> naming and the slot-8 pka placement match the manifest and
  match real-hardware form (real DS10: dka0.0.0.14.0 for pka at slot 14).

  Record this so it is not re-litigated: nothing in the slot/name layer is
  mismatched.

--------------------------------------------------------------------------------
## 1. DELTA (FIXED HERE) -- dqa0 declared but never enumerated

  cypress_ide.storage[0] was { channel 0, unit 0, ata_disk, media "" }.  The
  pka comment already states the governing rule -- "a disk target EXISTS on the
  bus only for a storage row below" -- and a row with EMPTY media does not
  materialise a device.  So the file described a dqa0 that SH DEV D does not
  show.

  FIX APPLIED: the empty row is REMOVED from ds20_v7_3_platform.json and its
  intent folded into the cypress_ide device comment (how to repopulate dqa0:
  add a channel 0 / unit 0 / ata_disk row WITH media).  cypress_ide now
  declares exactly one storage row, the ATAPI CD at unit 1 -> dqa1.  The seven
  pka rows are untouched.  CRLF line endings and the hand-formatted layout are
  preserved (this file is format-preserving SSOT for PlatformEditor).

  DELIBERATELY NOT PROPAGATED: only the SOURCE manifest is edited.  The run-dir
  copies (RelWithDebInfo/, out/build/relwithdebinfo/) are left as they were so
  the PENDING capture (JRN-SCSI-007 Sec 3: `b dka0.0.0.8.0 -flags 0` then
  SHOW boot*) still runs against the exact configuration every earlier
  footprint was taken on.  They refresh on the next build -- which should
  happen AFTER that capture, not before.

--------------------------------------------------------------------------------
## 2. DELTA (open) -- dva0 discovered but not declarable

  SH DEV D reports dva0.0.0.0.0.  The manifest has only iic_devices and
  pci_devices; there is no ISA/legacy section, so the floppy, keyboard and COM
  ports are implicit in code and CANNOT be expressed.  Consequence: live
  discovery can never be derived from the manifest alone, and any
  manifest-vs-discovery check will always show unexplained devices.
  This is a SCHEMA gap, not a wrong value.  Candidate fix: an
  "isa_devices" / "legacy_devices" array, even if initially descriptive only.

--------------------------------------------------------------------------------
## 3. DELTA (open) -- one IDE channel where real hardware has two

  Real DS10 golden reports BOTH channels of its IDE bridge:
      bus 0, slot 13 -- dqa -- Acer Labs M1543C IDE
      bus 0, slot 13 -- dqb -- Acer Labs M1543C IDE
  EmulatR DS20 reports dqa only.  The manifest declares no channel-1 storage,
  so file and behaviour AGREE -- but neither matches a real 82C693, which is a
  two-channel part.

--------------------------------------------------------------------------------
## 4. DELTA (open, and the one most likely to matter) -- synthetic INQUIRY

  deviceLib/scsi/VirtualDiskDevice.h:160 hardcodes the INQUIRY revision:
      std::memcpy(&buf[32], "0001", 4);
  and the PlatformEditor schema exposes only storage[*].model -- there are NO
  vendor / product / revision fields for a storage row.  So every disk,
  including the bootable system disk, presents as "EMULATR VIRTUAL DISK 0001".

  AXPBox presented THE SAME dka0.vdisk as RZ58 (JRN-SCSI-005 Sec 1) and booted
  OpenVMS V8.3 from it.  That makes disk IDENTITY a live variable in the A4
  comparison, not a cosmetic one: it is data the guest can read and branch on.
  Two things follow:
    (a) schema/model work: let a storage row carry vendor / product / revision
        so a row can present as e.g. DEC / RZ58 / 0307.
    (b) an experiment cheaper than (a): once the JRN-SCSI-007 Sec 3 capture is
        in, try dka0 presenting as a DEC RZ-series product and see whether the
        boot behaviour moves.  Do it as a DELIBERATE VARIATION after the
        baseline, never mixed into it.

--------------------------------------------------------------------------------
## 5. RECOMMENDATION on dqb and dkb (asked 2026-07-25)

  5.1 dqb (second IDE CHANNEL) -- YES, but not yet.
      Cheap: AtaTaskfileEngine already decodes both port ranges
      (kPriCmdBase 0x1F0, kSecCmdBase 0x170) and the attach API is already
      channel-parameterised (Cy82C693Ide.h attachDevice/attachMedia/attachDisk
      all take `channel`).  So this is manifest + wiring, not new hardware
      modelling, and it closes a real fidelity gap (Sec 3).

  5.2 dkb (a SECOND SCSI HBA -> dkb disks) -- NO, not now.
      Real DS10 has two HBAs (pka QLogic slot 14, pkb NCR 53C895 slot 15), so
      it is defensible eventually.  But it buys NOTHING for NOIOVEC and costs a
      second HBA instance, IRQ routing, and manifest surface.

  5.3 THE SEQUENCING ARGUMENT (applies to both, and is the real point)
      Every conclusion in the NOIOVEC track rests on comparisons against a
      FIXED bus topology -- most of all the JRN-SCSI-004 Sec 4 result that the
      IDE and SCSI DIAG-PC footprints are byte-identical (752 unique PCs, empty
      PC-set diff).  Adding dqb or dkb changes what the console enumerates,
      which changes the device topology strings, which changes the very GETENV
      data being compared.  It would invalidate days of established baseline
      for no diagnostic gain.

      ORDER: (1) the Sec 3 capture from JRN-SCSI-007 + the widened DIAG-PC
      window (JRN-SCSI-006 Sec 8.4) on the CURRENT topology; (2) the INQUIRY
      identity variation (Sec 4b); (3) dqb; (4) dkb only if a platform-fidelity
      goal calls for it.

--------------------------------------------------------------------------------
## 6. Env findings from the same capture (detail in JRN-SCSI-007)

  The operator capture is the PRE-boot baseline, so empty booted_* is CORRECT
  (real DS10 golden shows the same).  But it is a P00>>> transcript -- the
  FIRMWARE path -- and three boot-path EVs differ from real hardware:

      boot_dev / bootdef_dev   real 'dka0.0.0.14.0 dkb0.0.0.15.0' (27 B)
                               ours EMPTY
      auto_action              real 'HALT'                  ours ABSENT
      boot_osflags             real '0,0' (3 B)             ours '0' (1 B)

  Note the last one is the D15 delta appearing on the FIRMWARE path, not just
  the synthetic console.  JRN-SCSI-007 Sec 1 withdrew the D15 re-rating because
  the srm_conformance sample was a synthetic-console (EmulatR>>>) transcript;
  this capture shows the same '0' vs '0,0' shape at P00>>>, so the QUESTION is
  live again for the right subsystem.  It is still not evidence about the gate
  until the post-boot reading exists.
