<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-003
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-003 -- NCR 53C810 implementation: S1 seams + P0-P2 code LANDED,
#                 unit-green; live SRM gate (pka0/dka0 enumeration) PENDING
#                 (binary was in use by the architect's session at link time).

    Doc id   : JRN-SCSI-003
    Date     : 2026-07-25 (session of 2026-07-24)
    Status   : CODE COMPLETE for JRN-SCSI-001 P0-P2 scope; 487-case doctest
               suite green except the 3 KNOWN pre-existing drift failures
               (test_ide_wiring x1, test_mmio_csc_roundtrip x2 -- predate
               this work, separate repair item).  Live `show dev` gate owed.
    Relates  : JRN-SCSI-001 (design + Q1-Q5), JRN-SCSI-002 (seam map),
               TASK-PROBE-001 (completed TASK A, same session).

--------------------------------------------------------------------------------
## 1. What landed (files)

  S1 SEAMS (G-B unregister + G-A bulk DMA + tulip rebind):
    chipsetLib/TsunamiPchip.h      +unregisterIoPortRange/+unregisterPciMemRange
    chipsetLib/TsunamiChipset.h    tulip unregister lambda now REAL (stale-BAR
                                   shadowing fixed); +dmaReadBytes/+dmaWriteBytes
                                   (bulk bus-master capability, 4 KiB-chunked
                                   re-translation over translateDmaToPa)
    tests/chipsetLib/test_pci_bar_rebind.cpp   (5 cases; B1 acceptance incl.
                                   live-decode move through a real Pchip)

  SCSI STACK (P0-P2):
    deviceLib/scsi/VirtualDiskDevice.h   SBC direct-access target on
                                   IBlockMedia: TUR, REQUEST SENSE, INQUIRY,
                                   MODE SENSE(6/10), READ CAPACITY(10),
                                   READ(6/10), WRITE(6/10), VERIFY, START
                                   STOP, PREVENT/ALLOW; unknown op -> loud
                                   ILLEGAL REQUEST, sense retained.
    deviceLib/Tsunami/Ncr53C810.h  the HBA: config space (VID/DID 0x1000/
                                   0x0001 per io_device_list.h bind row),
                                   BAR0 io + BAR1 mem (0x100 each, rebind
                                   via S1), full 0x00-0x5F CSR file per
                                   n810_def.h, SCRIPTS engine (BM direct +
                                   table-indirect w1=DSA-offset per the DEC
                                   dma macro; IO sel/disc/set/clr; RW
                                   bic/bis; TC jmp/call/ret/int with phase+
                                   data compares vs SFBR), run-to-completion
                                   execution inside DSP/DCNTL<STD> writes,
                                   clear-on-read DSTAT/SIST0/SIST1, ISTAT
                                   DIP/SIP composition (polled + INTx),
                                   selection-timeout STO for empty ids,
                                   ScsiBus-lite target map, IDENTIFY->LUN.
                                   Deviations D1 (data-in pad, no MA residue)
                                   D2 (no disconnect/reselect) D3 (SSM
                                   untested) documented in-header.
    chipsetLib/TsunamiChipset.h    m_scsi + m_scsiDisk0 members; wireDevices
                                   wiring (range/DMA/INTx=INTB line 1);
                                   pciHandlerForModel "ncr53c810";
                                   setScsiDiskMedia(id, media) -- media
                                   attach places the target on the bus; no
                                   media -> id stays empty (STO).
    systemLib/PlatformConfig.h/.cpp  StorageType::ScsiDisk + "scsi_disk"
                                   parse + validation (unit=target id 0..6,
                                   channel 0, create_if_missing allowed).
    systemLib/Machine.cpp          storage consumption routes ScsiDisk ->
                                   setScsiDiskMedia.
    ds20_v7_3_platform.json        pka_53c810 @ slot 8 (Q2 minimal-churn;
                                   DS20E-faithful shuffle deferred), INTB,
                                   scsi_disk unit 0 media
                                   Alpha/dka0_scsi_scratch.img
                                   create_if_missing 512M (Q3: swap to the
                                   VMS system disk at P3, one attach only).
    tests/deviceLib/test_ncr53c810.cpp  (5 cases, 25 asserts): config
                                   identity; FULL SCRIPTS transaction
                                   SELECT->IDENTIFY->INQUIRY->DATA IN->
                                   STATUS->MSG IN against RAM media through
                                   the DMA seam; STO on empty id; READ(10)
                                   512-byte move; BAR rebind events.

--------------------------------------------------------------------------------
## 2. Verification state

  Unit: 487 doctest cases, 484 pass; the 3 failures are the PRE-EXISTING
  drift set found when the tests target was resurrected this session
  (resetDchipDrev case was FIXED to pin the live HRM value; test_ide_wiring
  + 2x mmio_csc remain -- they predate SCSI, repair separately).
  Live gate OWED (P1/P2 acceptance): boot DS20 (port 10024 scripted runbook,
  VMB-020 Sec 3), `show config` -> "NCR 53C810" at slot 8, `show dev` ->
  pka0.7.0.8.0 + dka0.0.0.8.0 via the console's own pke driver + SCRIPTS.
  BLOCKED at link time: the architect's interactive run held
  RelWithDebInfo/Emulatr.exe (LNK1168); relink + run when free.
  NOTE the run-dir manifest copy refreshes on the next Emulatr build
  (POST_BUILD), which also creates the scratch image on first run.

--------------------------------------------------------------------------------
## 3. Next

  1. Relink Emulatr when the binary frees; live gate above.  Expected first
     frictions: SRM BAR assignment ordering vs claim registration (watch
     EMULATR_PCI_CFG_TRACE + EMULATR_SCSI_TRACE), pke SCRIPTS ops outside
     the modeled subset (engine faults LOUD by design), scntl3/stest quirks.
  2. P3: `b dka0` with the VMS media -> the NOIOVEC retest ("SCSI 0 ..."
     topology string through the VMB-021 resolver).
  3. P4 debts: MA/residue path (D1), disconnect/reselect (D2), device-state
     snapshot participation (G-G), DS10/ES40 regression, pre-existing test
     drift repair, C4834 warning sweep.

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
