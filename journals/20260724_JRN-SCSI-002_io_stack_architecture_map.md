<!--
EmulatR V5 -- Architecture Journal JRN-SCSI-002
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-002 -- The CPU-to-SCSI-disk I/O stack: faithful layer map,
#                 what EmulatR has at each layer, and the architecture
#                 gaps (seams + levers) the ORACLE still needs.

    Doc id   : JRN-SCSI-002
    Date     : 2026-07-24
    Status   : ARCHITECTURE MAP FOR REVIEW.  No code changed.
    Purpose  : Answer "provide the I/O stack from the CPU to a SCSI disk,
               how it relates to what we have, and the gaps" -- the
               comprehensive-Oracle framing that JRN-SCSI-001's phased
               plan must serve, not shortcut.
    Relates  : JRN-SCSI-001 (HBA proposal), JRN-VMB-022 (why SCSI),
               V4_IO_Machinery_Map.txt (V4 gap ledger G1-G4),
               2026-05-22_SystemBus_Contract_and_Refactor_Plan.md,
               tasks_20260612_boot_pci_deploy.md (#37-#42 / B1-B6).

--------------------------------------------------------------------------------
## 1. The FAITHFUL stack (real DS20 hardware), both directions

An I/O stack is TWO stacks.  Outbound (CPU master) and inbound (device
master + interrupts).  A comprehensive Oracle needs seams on BOTH.

### 1.1 OUTBOUND: CPU -> HBA register (programmed I/O)

  L1  EV6 core       LDx/STx to VA (I/O accesses are non-cacheable
                     space; MB/WMB ordering around them matters to
                     drivers).
  L2  MMU            DTB / superpage (kseg) -> 43-bit PA.
  L3  System fabric  EV6 cmd/addr -> Cchip (decode + arbitration),
                     data through Dchip slices.
  L4  Pchip (hose)   PA-window decode -> a PCI transaction:
                       dense  PCI memory   (PA 0x800.0000.0000+)
                       sparse PCI memory   (byte-lane folding, HRM
                                            10.1.3.1/2)
                       dense/sparse PCI IO (UART at 0x801.FC00.0000+)
                       config space        (type-0 on bus 0 via IDSEL
                                            one-hot AD[31:11], Table
                                            10-3; type-1 forwarded to
                                            sub-buses, Sec 8.5)
  L5  PCI bus        Address/data phases, byte enables, DEVSEL (no
                     claimant -> master-abort all-1s, HRM); PCI-PCI
                     bridges (21052/21152) spawn sub-buses; hose 1 =
                     second Pchip.
  L6  53C810 target  Config header (VID/DID 0x1000/0x0001, class
                     0x0100xx, INT pin) + BAR0 (IO, 0x100) + BAR1
                     (MMIO, 0x100) -> the 0x00-0x5F register file
                     (SCNTL*, SDID, SFBR, SBCL, DSTAT/SSTAT*, DSA,
                     ISTAT, CTEST*, DBC/DCMD, DNAD, DSP, DSPS,
                     SCRATCH, DMODE, DIEN, DCNTL...).

### 1.2 INBOUND A: HBA -> memory (bus-master DMA -- the defining flow)

  The 53C810 is not a passive register file.  Its SCRIPTS processor
  FETCHES ITS OWN PROGRAM FROM HOST MEMORY and moves all data by DMA:

  D1  SRM/OS writes a SCRIPTS program into guest RAM (pke_script.mar
      assembled), points DSP at it.
  D2  SCRIPTS engine (in the chip) issues PCI MEMORY READ bursts to
      fetch instructions, and MEMORY READ/WRITE for table-indirect
      operands and data block-moves.
  D3  Pchip INBOUND windows translate PCI address -> PA:
      WSBAx/WSMx/TBAx per window; direct-mapped OR scatter-gather
      (TBA -> PTE fetch -> per-8KB-page mapping, with the Pchip's own
      SG TLB).  (HRM Ch.10; EmulatR: translateDmaToPa.)
  D4  Dchip -> memory controller -> DRAM.

### 1.3 INBOUND B: completion (interrupt)

  I1  Chip raises INTA# (config Interrupt Pin) on SCRIPTS INT /
      phase-mismatch / SCSI events per SIEN/DIEN masks.
  I2  Board routing: slot INTx -> Cchip IRQ input (DRIR bit per line;
      DIMx masks -> DIRx -> CPU b_irq pins).
  I3  EV6 PALcode interrupt entry -> SCB vector -> driver ISR (pke_isr
      reads ISTAT/DSTAT/SIST to dismiss).

### 1.4 The SCSI side of the HBA

  S1  SCSI bus: 8 IDs (initiator 7), arbitration -> selection (with
      ATN) -> MESSAGE OUT (IDENTIFY: LUN) -> COMMAND (CDB) -> DATA
      IN/OUT -> STATUS -> MESSAGE IN (COMMAND COMPLETE); disconnect/
      reselect for long ops.  The 810 drives these phases UNDER SCRIPTS
      CONTROL (each SCRIPTS block-move names an expected phase).
  S2  Target LUN executes SBC/SPC commands: INQUIRY, TEST UNIT READY,
      REQUEST SENSE, MODE SENSE, READ CAPACITY(10), READ(6)/(10),
      WRITE(6)/(10)...
  S3  Media: LBA-addressed 512-byte blocks.

### 1.5 The guest SOFTWARE stack that will run on it (unmodified)

  G1  SRM probe: config walk -> io_device_list bind (0x00011000 ->
      "n810"/pk/PKE) -> BAR sizing/assignment -> pke_driver init ->
      SCRIPTS download -> scsi.c class driver probe (INQUIRY per ID) ->
      `show dev` pka0/dka0.
  G2  `b dka0`: console driver reads boot blocks -> VMB -> APB;
      BOOT_DEV topology string protocol = "SCSI" -> the VMB-021/-022
      resolver MATCHES (the whole point).
  G3  APB bootdriver (SYS$PKEDRIVER lineage) drives the SAME chip
      directly; then SYSBOOT, then the full VMS runtime driver.
      Each layer re-initializes the chip its own way -- faithfulness,
      not driver-specific hacks, is what survives all three.

--------------------------------------------------------------------------------
## 2. Layer-by-layer: what EmulatR has TODAY (anchors audited 2026-07-24)

  L1  CPU            AlphaCpuAgent + dispatcher (schedLib) -- FAITHFUL;
                     determinism_equivalence is the acceptance gate.
  L2  MMU            Ev6Translator soft TLB -- FAITHFUL for this path
                     (harvest debts recorded, memory.md 2.3).
  L3  Cchip/Dchip    TsunamiChipset -- CSR/DRIR/IPI modeled; interval
                     timing PARTIAL (memory.md 2.4).  ADEQUATE here.
  L4  Pchip outbound
        dense mem    linear path + PciMemRange claims -- PARTIAL:
                     PciMemRange REUSES IIoPortHandler with a 16-BIT
                     REBASED OFFSET (IDeviceHandlers.h:101-128) -- fine
                     for a 2-reg IIC chip, TOO NARROW as the general
                     MMIO-BAR seam (works for the 810's 0x100 regs,
                     breaks for any larger device; and width/ordering
                     semantics are squeezed through a port signature).
        io ports     registerIoPortRange (TsunamiPchip.h:345) -- OK.
        config       type-0 decode + dispatch (TsunamiPchip.h:1158-86)
                     -- OK on bus 0.  Type-1/PPB -- ABSENT.
        sparse       ABSENT (B3).  NOTE: verify which space pke uses
                     (SRM inport/outport family is dense-IO on PC264;
                     confirm at P1 with EMULATR_PCI_CFG_TRACE).
  L5  PCI bus        IMPLICIT (maps inside Pchip).  No first-class bus
                     object, no PPB, hose 1 stub (B5), IDSEL boundary
                     unverified (B2).
  L6  PCI target     IPciDeviceHandler = CONFIG ONLY (IDeviceHandlers.h
                     :44) + IIoPortHandler; ManifestPciDevice
                     (systemLib) = config image + BAR size-probe.
                     BAR-write -> decode REBIND: MISSING (B1); tulip
                     carries the hook pattern unused
                     (Dec21143Tulip.h:55,356).
  D2/D3 BUS-MASTER   *** NO SEAM EXISTS ***.  No interface lets a
                     device read/write guest memory through its hose.
                     translateDmaToPa (TsunamiPchip.h:401-420) exists
                     but is direct-map-only and has no device-facing
                     contract wrapping it.  THIS IS THE DEEPEST GAP:
                     SCRIPTS cannot even FETCH ITS PROGRAM without it.
  I1/I2 interrupts   Chipset-side INTx->DRIR helpers exist
                     (TsunamiChipset.h:430-444); device-side there is
                     no formal line contract (existing devices wire
                     bespoke).  SMALL formalization gap.
  S1  SCSI bus       ABSENT.  The ATAPI path BYPASSES it (IDE hands a
                     ScsiCommand straight to the target) -- correct for
                     ATAPI (it IS packet-over-IDE), not a substitute
                     for a bus with IDs/LUNs/selection/disconnect.
  S2  SCSI target    VirtualScsiDevice (abstract, deviceLib/scsi) +
                     VirtualIsoDevice (CD) -- GOOD SEAM, present.
                     Direct-access DISK target: MISSING.
  S3  Media          IBlockMedia / FileBlockMedia / BlockMediaFactory
                     -- present, proven (vStorage).
  X   Snapshot       Level-1 snapshot serializes CPU + memory + chipset
                     CSRs; per-DEVICE model state (IDE FSMs etc.) is
                     not a formal participant -- any new stateful HBA
                     needs a save/restore seam or documented reset-
                     equivalence.  (Gap applies to existing devices
                     too; SCSI makes it acute: SCRIPTS mid-flight.)

--------------------------------------------------------------------------------
## 3. THE GAPS, as architecture (ranked; each is a SEAM, not a hack)

  G-A  BUS-MASTER / DMA AGENT SEAM (new; unlocks every future HBA/NIC)
       Contract sketch: the hose hands each device a narrow capability
       object --
         struct IPciBusMaster {            // implemented by the PCHIP
           uint64_t dmaRead (uint64_t pciAddr, void* dst, size_t n);
           uint64_t dmaWrite(uint64_t pciAddr, void const* src, size_t n);
         };
       routed through window translation (direct-map today, SG/TBA
       later -- B4 slots INSIDE this seam invisibly to devices).
       Ordering/atomicity documented at the seam.  Consumers: 53C810
       SCRIPTS fetch + block-moves, tulip descriptor rings (currently
       stalled), any future HBA.
  G-B  DYNAMIC DECODE / BAR REBIND (B1) + a REAL MMIO region seam
       Replace the 16-bit PciMemRange squeeze with a first-class
       region contract (64-bit offset, width-aware):
         struct IMmioRegion { read(off,w); write(off,w,v); };
       BAR writes re-route ranges live (tulip hooks generalized into
       ManifestPciDevice/PciConfigSpace so EVERY device gets it).
       Acceptance: JRN-VMB-019 B1 doctest + SRM assignment replay.
  G-C  SCSI BUS OBJECT + DISK TARGET (new, deviceLib/scsi)
       ScsiBus: ID/LUN registry, selection, one-command-at-a-time
       arbitration model (disconnect/reselect modeled as a seam,
       simplified first, deepenable without breaking callers);
       VirtualDiskDevice: SBC direct-access on IBlockMedia.
       The HBA talks ONLY to ScsiBus; targets stay HBA-agnostic
       (the same disk target must later serve an ISP1020 or a
       Titan-hose HBA unchanged).
  G-D  SPARSE SPACES (B3)  -- per HRM folding; needed the moment any
       driver uses sparse (verify pke's choice at P1; implement
       regardless for Oracle completeness, priority after G-A/G-B).
  G-E  PCI BUS AS FIRST-CLASS + PPB/type-1 + HOSE 1 (B5)
       Organizational: lift the implicit maps into a PciBus object the
       Pchip OWNS (bus 0 today), so 21052 bridges and hose 1 become
       additive, not surgical.  Do NOT block SCSI on this; DO shape
       G-A/G-B signatures so they don't preclude it.
  G-F  INTERRUPT LINE FORMALIZATION
       struct IIntxLine { assert(); deassert(); } handed to the device
       at wire-up (bound to slot/pin -> DRIR bit).  Small; fold into
       the HBA wiring commit.
  G-G  DEVICE-STATE SNAPSHOT PARTICIPATION
       Extend the Level-1 snapshot contract: each registered device may
       contribute a versioned state blob (save/load), or declares
       reset-equivalence.  Without it, auto_halt snapshots of a
       mid-SCRIPTS machine restore incoherently.

  Ticket cross-map: B1 -> G-B, B2 -> (fold into G-B commit),
  B3 -> G-D, B4 -> inside G-A, B5 -> G-E, B6 (PERROR) -> error path of
  G-A/G-B when real error sources exist.

--------------------------------------------------------------------------------
## 4. How JRN-SCSI-001's phases sit on this map (revised sequencing)

  P0 (config-only)     exercises L6-config only         -- no new seams
  S1 SEAM COMMIT       G-B (IMmioRegion + BAR rebind) + G-F (IIntxLine)
  S2 SEAM COMMIT       G-A (IPciBusMaster over translateDmaToPa)
  P1 (register file)   53C810 on G-B regions + G-F line
  P2 (SCRIPTS+disk)    SCRIPTS on G-A; ScsiBus + VirtualDiskDevice (G-C)
  P3 (b dka0)          the NOIOVEC retest
  P4 (writes/robust)   + G-G snapshot participation; G-D as verified;
                       B4 SG inside G-A when a consumer demands it.
  G-E stays deferred but every S1/S2 signature is reviewed against it.

The Oracle framing in one line: the HBA is a CONSUMER of four seams
(config, MMIO region, bus-master, interrupt line) and a DRIVER of two
(SCSI bus, block media) -- after this work, ANY future storage/network
controller is a filling-in of the same six, and the pattern-VM class of
mystery (VMB-016..022) meets instrumented seams at every layer instead
of bespoke plumbing.

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
