# Cypress 82C693 IDE + ATAPI CD Scaffold -- Specification and Architectural Design
# EmulatR V4 -- 2026-06-07
# Author: Claude (Anthropic), with T. Peer (project architect)
# Status: DESIGN. No code rides with this doc (house rule: discuss before code).
# Locked scope (architect, 2026-06-07): ATAPI CD ONLY, NO MEDIA; storage device
#   declared in the platform manifest; reuse V1 scsiCoreLib for the command layer.

--------------------------------------------------------------------------------
## 0. Summary

Model PCI Function 1 of the Cypress CY82C693 -- its integrated IDE controller --
plus one ATAPI CDROM with no media, so the firmware's IDE driver (dq_driver.c)
discovers a CD and puts it in the unit-block list ubs[].  That breaks the LFU
option-firmware retry loop (15 x krn$_sleep(5000) = ~75 modeled seconds, the
current ~45-60 min wall stall) on its first iteration.  The IDE taskfile +
ATAPI packet transport is new V4 code; the SCSI/ATAPI COMMAND layer (INQUIRY,
TEST UNIT READY, REQUEST SENSE) is REUSED from V1's scsiCoreLib (ScsiCommand /
VirtualScsiDevice / VirtualIsoDevice), which already returns CdDvdDevice (0x05).
This is also the foundation for the cheaper-than-SCSI first OS boot (dqa disk
path, a later phase).

--------------------------------------------------------------------------------
## 1. Why this is the lever (confirmed)

lfu.c (~769): the option-firmware search loops up to 15 times looking for a CD
in ubs[] (ub->inq[0] & 0x1f == 5, the INQUIRY peripheral device type for CDROM),
sleeping krn$_sleep(5000) = 5 modeled seconds between misses.  No IDE controller
is modeled, so no CD ever enters ubs[], so all 15 iterations sleep = ~75 modeled
seconds = the wall stall.  The loop keys on device TYPE, not media, so an ATAPI
CD with NO media still breaks it on iteration 0 -- the model does not need a real
disc.  (dva0 = diskette, a separate device; the CD path is dqa via IDE.)

--------------------------------------------------------------------------------
## 2. What dq_driver.c probes (the contract the model must satisfy)

Sources: dq_driver.c, cy82c693_def.h, ide_pb_def.h (Processor Support apisrm ref).
Constants: ATAPI_MAGIC_1=0x14, ATAPI_MAGIC_2=0xEB, COMMAND_IDENTIFY=0xEC,
COMMAND_PIDENTIFY=0xA1, STATUS_BSY=0x80.

Detection sequence per channel/unit:
  1. PCI enum finds the IDE function; driver programs I/O base via PCI config
     (outcfgl(pb, 0x10, 0x1F0) -> BAR0 = 0x1F0 primary; secondary = 0x170).
  2. Select drive (Drive/Head register, 0x1F6), wait STATUS_BSY (0x1F7 bit7) clear.
  3. Issue IDENTIFY DEVICE (0xEC).  An ATAPI device does NOT respond as ATA;
     the driver then reads the cylinder-low/high registers (0x1F4/0x1F5) and
     finds the ATAPI signature 0x14 / 0xEB -> branches to ATAPI.
  4. Issue PACKET IDENTIFY (0xA1); PIO-read the 256-word identify block
     (model string, type) via the data register (0x1F0).
  5. Issue ATAPI PACKET (0xA0) carrying a 12-byte SCSI CDB (INQUIRY etc.);
     PIO transfer the response.  INQUIRY peripheral device type 0x05 = CDROM
     is what lands the unit in ubs[] as a CD.

The model must answer steps 2-5 deterministically and FAST (no real timing), and
return "not ready / medium not present" sense for TEST UNIT READY and READ so the
LFU file probes fail immediately instead of hanging.

--------------------------------------------------------------------------------
## 3. Architecture

    firmware dq_driver.c
        | legacy taskfile I/O 0x1F0-0x1F7 / 0x170-0x177 + ctrl 0x3F6/0x376
        | PCI config (Function 1 identity + BAR0)
        v
    Cy82C693Ide  (NEW, deviceLib/Tsunami)            : IIoPortHandler + IPciDeviceHandler
        - per-channel ATA taskfile register file (8 cmd-block + control)
        - ATA detection: BSY/DRDY/DRQ status model, ATAPI signature 0x14/0xEB
        - ATAPI packet state machine: PACKET(0xA0) -> accept 12-byte CDB ->
          build ScsiCommand -> target->handleCommand() -> PIO-return data/sense
        | ScsiCommand contract (cdb, dataIn/out, status, senseData)
        v
    VirtualIsoDevice (REUSE, ported from V1 scsiCoreLib) : VirtualScsiDevice
        - deviceType() = CdDvdDevice (0x05)            -> CD lands in ubs[]
        - buildInquiryData() 36-byte INQUIRY            -> INQUIRY answered
        - NULL backend (no media) -> TEST UNIT READY / READ return CheckCondition
          with sense "NOT READY, MEDIUM NOT PRESENT"    -> LFU probes fail fast

Layering: Cy82C693Ide lives in deviceLib/Tsunami next to IicPcf8584 and the ISA
bridge.  scsiCoreLib is ported into a V4 lib (deviceLib/scsi or a new scsiCoreLib)
as a self-contained command layer; Cy82C693Ide depends on it downward (deviceLib
-> deviceLib), no upward dependency.

--------------------------------------------------------------------------------
## 4. scsiCoreLib port scope (minimal, for the no-media CD)

From V1 D:\EmulatR\EmulatRAppUni\scsiCoreLib, port the self-contained command
layer the ATAPI CD exercises -- NOT the disk/tape/cache machinery yet:
  ScsiTypes.h       ScsiStatus, ScsiLun, ScsiPeripheralDeviceType, directions
  ScsiOpcodes.h     INQUIRY(0x12), TEST_UNIT_READY(0x00), REQUEST_SENSE(0x03),
                    READ(10)(0x28), READ_CAPACITY, START_STOP, MODE_SENSE...
  ScsiCbd.h         CDB opcode/LBA/length decoders
  ScsiSenseData.h   fixed sense (NOT READY / MEDIUM NOT PRESENT key/asc/ascq)
  ScsiCommand.h     the controller<->target contract struct
  VirtualScsiDevice.h   base: handleCommand(), deviceType(), buildInquiryData()
  VirtualIsoDevice.h    read-only CD/DVD LU (the no-media path with null backend)
Defer: VirtualScsiDisk, VirtualTapeDevice, VirtualScsiCacheLayer, the backend/
QIODevice disk plumbing (that is the dqa-disk phase, Section 7).
House-rule adaptation on port: doctest CHECK only, include guards (not pragma
once), ASCII(128); the V1 sources use quint8/QByteArray (Qt) -- keep Qt or
swap to std per the V4 file's neighbours (IicPcf8584 is std; chipset uses Qt for
snapshot).  Recommend std-izing the hot path, keeping QByteArray only where the
V1 code already uses it for INQUIRY buffers, to minimize port churn.

--------------------------------------------------------------------------------
## 5. PCI Function 1 + manifest

Function 0 (ISA bridge, Cy82C693IsaBridge) already sets header-type 0x80 so the
firmware probes Function 1.  Function 1 (IDE) needs:
  - a PCI config header: vendor 0x1080, device 0xC693, class_code 0x0101xx (IDE),
    BAR0/BAR1 for the legacy taskfile windows.  _PROVISIONAL until a real dump
    confirms (spec 7.5).
  - registration at bus0/slot5/func1 (mirrors the bridge's func0 registration:
    m_pchip.registerPciDevice(0, 5, 1, &m_ide)).
  - legacy I/O port claims 0x1F0-0x1F7, 0x170-0x177, 0x3F6, 0x376 via the ISA
    I/O handler seam (setIoPortHandler / registerIoPortRange).

Manifest (ds10_platform.json): EXTEND the schema with a storage section so the
CD (and later disks) are declarative.  Proposed shape:
    "storage": [
      { "name":"dqa0", "bus":"ide", "channel":0, "unit":0, "type":"atapi_cd",
        "media":"none", "image":"" }
    ]
The IDE func1 PCI entry stays in pci_devices (model "cypress_ide").  Machine
synthesizes the storage list and hands it to the IDE model the same way it hands
the IIC list to IicPcf8584 (Machine-synthesizes-pushes-down, the P3 layering).
A media path later sets "image":"foo.iso" to mount a real disc.

--------------------------------------------------------------------------------
## 6. Phasing

S1  Port scsiCoreLib subset (Section 4) into V4 as a deviceLib-level lib; build +
    a doctest that an INQUIRY CDB to a null-backed VirtualIsoDevice returns type
    0x05 and TEST UNIT READY returns NOT READY/MEDIUM NOT PRESENT.  Pure host-side.
S2  Cy82C693Ide model: taskfile register file + ATA status/detection + ATAPI
    signature; doctest the detection handshake (BSY clears, 0x14/0xEB signature).
S3  ATAPI packet transport: PACKET(0xA0) accepts a 12-byte CDB, routes to the
    VirtualIsoDevice, PIO-returns INQUIRY/sense; doctest a full INQUIRY round-trip
    through the taskfile.
S4  Wire Function 1: PCI config registration + legacy I/O port claims in
    TsunamiChipset; manifest "storage" section + Machine push-down.
S5  Cold-boot validate: dq_driver discovers the CD; `show device` shows dqa0; the
    LFU loop breaks on iteration 0 (the ~75 s stall gone); boot reaches >>> fast.
    Re-mint snapshots.
S1-S3 are host-side doctest work (no boot).  Only S5 pays boot cost.

DEPENDENCY NOTE: full P4 (manifest-driven PCI consumer) is NOT a hard prerequisite
-- Function 1 can be registered directly like the existing func0 bridge.  Keep the
PCI identity in the manifest for consistency, but wire func1 directly now; fold
into the P4 generic ManifestPciDevice when P4 lands.

--------------------------------------------------------------------------------
## 7. Deferred (explicitly out of this scaffold)

  - Media: a real ISO image mount (VirtualIsoDevice with a file backend) -- the
    INQUIRY/READ-with-media path; trivial once S1-S4 land (set image path).
  - dqa DISK (hard drive): VirtualScsiDisk + flat-image backend + ATA IDENTIFY
    (0xEC) PIO read/write -- the first-OS-boot path; the larger next phase.
  - DMA / bus-master IDE (BMIDE): PIO only for now (dq_driver boot probe is PIO).
  - Secondary channel population, master+slave fan-out beyond the single CD.
  - USB (Function 2 of the 82C693).

--------------------------------------------------------------------------------
## 8. Verification matrix

    check                                method                               pass
    ----------------------------------   ----------------------------------   --------
    INQUIRY -> type 0x05 (null backend)  doctest VirtualIsoDevice             0x05
    TEST UNIT READY -> not ready         doctest sense key/asc/ascq           02/3A/00
    ATA detection signature              doctest taskfile reads 0x14/0xEB     match
    INQUIRY through taskfile (PIO)       doctest packet round-trip            36 bytes
    CD enters ubs[]                      cold boot: show device shows dqa0    present
    LFU loop breaks iteration 0          cold boot: no 75 s krn$_sleep stall  fast
    reach >>>                            cold boot wall time                  minutes
    doctest suite                        ctest                                all green

--------------------------------------------------------------------------------
## 9. Risk register

R1  ATAPI detection subtlety (signature timing / DRQ handshake) wrong -> driver
    mis-detects or retries.  Mitigation: doctest the exact dq_driver sequence;
    EMULATR trace on the taskfile if a cold boot misbehaves.
R2  scsiCoreLib port drags in more than the CD needs (disk/cache/backend).
    Mitigation: port only the Section-4 subset; stub the backend as null.
R3  PCI func1 identity _PROVISIONAL wrong -> firmware mis-IDs the controller.
    Mitigation: mark _PROVISIONAL; confirm vs a real dump; non-fatal to the loop
    break (legacy ports answer regardless of exact IDs).
R4  Modeling a CD changes the boot path (firmware tries to boot/inspect it).
    Mitigation: no-media returns not-ready everywhere except INQUIRY; the firmware
    skips an empty CD.  Watch the first cold boot for unexpected divergence.
R5  Qt-vs-std impedance on the port.  Mitigation: keep QByteArray only where V1
    already uses it; std-ize the taskfile hot path.

--------------------------------------------------------------------------------
## 10. Bottom line

One PCI function (the 82C693 IDE) plus a no-media ATAPI CD -- most of it reused
from V1's scsiCoreLib (VirtualIsoDevice already is a 0x05 CD) -- gets a CD into
ubs[], breaks the 75-second LFU loop, and cuts the ~45-60 min dva-scan wall to
minutes, while laying the IDE taskfile foundation for the later dqa disk / OS-boot
path.  S1-S3 are pure host-side doctest work; the cold-boot bill is paid once at S5.

--------------------------------------------------------------------------------
## 11. S0 GATE RESULTS + review refinements (2026-06-07, architect review)

Static lfu.c read = the decision gate; live profile = the before/after baseline
(run in parallel, NOT gating the build).  Exit criteria verdicts:

#1 LOOP PREDICATE -- PASS.  lfu_get_options_firmware (lfu.c 2275-2301): the CD
   search loop breaks the instant device[0] is set (a name[1]=='k'/'q' unit with
   INQUIRY type 5), and ONLY krn$_sleep(1000) when none is found.  Keys on device
   ABSENCE, not read failure.  A CD in ubs[] breaks it on iteration 0.

#2 POST-DISCOVERY -- PASS, with a locked CONTRACT.  After a CD is found, LFU does
   exactly two non-retrying fopens (iso9660:, then ods2:) with NO loop/sleep, then
   falls through to the floppy check.  lfu_check_fw_device (lfu.c 3667) is clean:
   one fopen + one 512-byte fread, no sleep/retry, returns 0 immediately on a
   no-media floppy.  So there is NO LFU-level post-discovery stall.  The ONLY
   residual risk is one layer down: the iso9660 fopen issues an ATAPI READ to the
   empty CD; the driver must FAIL FAST, not retry.  CONTRACT: the no-media CD
   returns sense 02/3A/00 (NOT READY / MEDIUM NOT PRESENT -- fail-fast), NEVER
   02/04/xx (becoming-ready -- retry-eligible).  dq_driver defines
   scsi_k_not_ready=2 (line 125); confirm its retry-on-not-ready handling at S2/S3.

#3 SLEEP INVENTORY -- the CD-search loop's 15 x krn$_sleep(1000) = 15 modeled
   seconds is the ONLY sleep on the path from GCT/FRU-done to the LFU prompt.  At
   ~1B cyc/modeled-sec (idle/timer_check) that is ~15B cycles = the observed
   ~25-50 min wall.  The floppy check has no sleep.  A no-media CD eliminates it.

#4 APPORTIONMENT -- baseline, architect-run: measure the modeled-to-wall ratio on
   the live dva0 run; do not assume it.  Re-pin the 15-modeled-second figure
   post-kCcMultiplier (the timer calibration moved; same RPCC path).

LOCKED CONTRACT ITEMS (fold into S2-S4):
 C1 ABSENT-DEVICE (review #1): every taskfile read to an unpopulated unit/channel
    returns a BSY-CLEAR status -- NEVER float-bus 0xFF (bit7=0x80 = phantom BSY ->
    dq_driver's ALT_STATUS&STATUS_BSY check at line 1241 stuck-polls).  Deterministic
    "no device" signature for the ch0 slave and both secondary-channel units.
 C2 MEDIUM-NOT-PRESENT (review #2): no-media CD answers TEST UNIT READY / READ with
    sense 02/3A/00 (fail-fast), INQUIRY still type 0x05.  VirtualIsoDevice null
    backend must produce 3A/00, not 04/xx.
 C3 PROG-IF (review #3): dq_driver branches on (progif & 0x04) at line 693 and
    programs BAR1=0x3F6/0x376 (lines 725/731).  Set func1 prog-IF for legacy/compat
    so fixed ports 0x1F0/0x170 are used.  Confirm enumeration binds by class 0x0101
    vs (vendor,device) before S4; treat func1 device ID as load-bearing until proven
    cosmetic (it is often NOT 0xC693 -- needs the real dump / AXPBox reference).
 C4 QT/STD (review #5): std-ize the taskfile hot path; keep QByteArray only inside
    the reused INQUIRY buffers; KNOWN seam to unify at the dqa-disk phase.

FALLBACK SPIKE (review): if S2/S3 cannot settle the ATAPI not-ready retry behavior
statically, hand-inject a single fake type-5 entry into ubs[] and observe LFU --
a throwaway probe, not scaffold code.

VERDICT: gate PASS on kill-criteria #1/#2 + #3 -> BUILD S1, with C1-C4 locked.
