<!--
EmulatR V4 -- DQ / IDE Driver Support Design Specification
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Target platform: AlphaServer DS10, Tsunami/Typhoon 21272 chipset
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-10
Purpose: hand-off design plan for Cowork to implement console DQ (IDE/ATA)
device support, so SRM "show device" lists dqa/dqb and "boot dqa0" can
read the Alpha disk boot block from a backing image. Implementation and
all line-number verification happen in Cowork against the live tree.
ASCII(128) only. Reconcile any generated .cpp/.h headers with
docs/notes/ADR-0001-source-file-headers.md and the header_cpp.txt template.

Revision (2026-06-11): the IDE binding is now platform-model gated. The
shared controller model consumes a per-platform identity record selected
by the QSettings platform profile (see new section 2A; sections 1, 4.6,
4.7, 5, 6, 8, 11 updated to route through it). DS10 is the only validated
model this cycle; DS20 / ES40 / ES45 are scaffolded placeholders that MUST
fail closed until populated. Hardware-identity values below are the DS10
record unless a per-platform table says otherwise.
-->

# EmulatR V4 -- DQ / IDE Driver Support Specification

## 0. What this document is and is not

This is a DESIGN PLAN, not generated code. It decides the IDE/ATA device
model strategy and specifies the seam-by-seam changes. Cowork (the agent
with live file access) turns the plan into diffs, locates and verifies
every line number, confirms the provisional identity values against the
DS10 firmware source, and runs the MSVC build.

Treat every file path, register layout, and hardware-identity value here
as a point-in-time design proposal. Cowork is the source of truth for
current file state. Nothing in here is bound until a trace, a
disassembly, or a firmware-source read confirms the branch (house rule:
no fix before trace confirmation).

Where this document needs a value that has to come from the live tree or
real hardware, it is marked one of:

- `[LOCATE]`     -- a code seam Cowork must find in V4 and report back.
- `[CONFIRM]`    -- a behavior that must be confirmed by trace or by
                    reading the DS10 console / firmware source before code.
- `_PROVISIONAL` -- a hardware-identity value (PCI ID, port base, IRQ)
                    that must be HRM/firmware-verified before it is allowed
                    to drive any decode (per the C2 data-fidelity rule).

## 1. Goal and definition of "full DQ support"

On the DS10 the `dq` device class is the southbridge IDE/ATA function.
The SRM console probes it and names the channels `dqa` (primary) and
`dqb` (secondary); attached drives appear as `dqa0`, `dqa1`, `dqb0`,
`dqb1`. Target probe line for this cycle (DS10, from a V7.3 power-up; this
is the DS10 row of the platform identity table in section 2A):

    bus 0, slot 13 -- dqa -- Acer Labs M1543C IDE
    bus 0, slot 13 -- dqb -- Acer Labs M1543C IDE

For contrast, an ES45/Titan power-up (same console driver family, different
chipset) shows the same controller at bus 0, slot 16. The slot / bus-dev-func
is a platform-gated value, NOT a constant -- see section 2A.

"Full DQ support" is delivered in layers; the success criteria, in order:

1. SRM `show device` (or the power-up "probe I/O subsystem" pass) lists
   `dqa0` (and any other configured drive) with a model string. This
   requires PCI config-space presence + ATA IDENTIFY DEVICE only.
2. SRM `boot dqa0` reads LBN 0 (the Alpha disk boot block), follows the
   LBN descriptor, and loads the primary bootstrap to virtual
   0000_0000_2000_0000. This requires ATA READ SECTORS in addition.
3. The booted operating system's own DQDRIVER can perform block I/O
   (reads, then writes). This requires the full PIO read/write command
   set and, for acceptable throughput, busmaster DMA.

Items 1 and 2 are the near-term target (they get a console that can see
and boot a disk). Item 3 is the long-horizon target and is phased.

The console DQDRIVER is the first consumer. The OS DQDRIVER is a later
consumer of the same emulated controller.

## 2. Background facts that matter

- The DS10 southbridge providing IDE is the same Acer Labs (ALi) M1543C
  family used on the ES45 probe above. The exact IDE-function PCI
  identity for the DS10 build MUST be read from the DS10 firmware/probe
  before binding -- `[CONFIRM]` against `pc264_io.c` and the console
  device-probe code. Do not copy the ES45 string blindly.
- The IDE *device model* (M1543C / M5229, ATA register behavior, legacy
  port decode) is platform-invariant across DS10 / DS20 / ES40 / ES45 --
  same southbridge family. What differs per platform is the *binding*: the
  PCI bus/device/function (the "slot" the probe prints) where the function
  enumerates, the PCI hose count, and the chipset interrupt fabric the
  channel IRQ routes through (Tsunami Cchip on DS10/DS20/ES40; Titan on
  ES45). The spec therefore separates a shared controller model from a
  platform-gated identity record -- see section 2A. Do not bake any
  platform-specific binding value into the controller.
- IDE on these systems is reached as legacy ISA I/O ports decoded by the
  southbridge. The CPU issues PIO to the Pchip PCI/ISA I/O space; the
  southbridge decodes the legacy IDE port ranges. This is the SAME ISA
  I/O dispatch seam the existing SuperIO (FDC37C669) and keyboard
  controller already register against -- IDE is a new client of that
  dispatcher, not a new transport. `[LOCATE]` the ISA-port dispatch
  table/function in V4.
- Legacy IDE port assignments (ISA I/O), to be `[CONFIRM]`ed against the
  DS10 probe but standard for PC-compatible IDE:
    Primary command block   0x1F0 - 0x1F7   _PROVISIONAL
    Primary control block    0x3F6          _PROVISIONAL (alt status / dev ctrl)
    Secondary command block 0x170 - 0x177   _PROVISIONAL
    Secondary control block  0x376          _PROVISIONAL
- IDE interrupts on the ISA side: primary = IRQ14, secondary = IRQ15,
  delivered through the 8259 PIC pair, whose output reaches the CPU via
  the Cchip device-interrupt path (DRIR -> DIM gate -> b_irq<1> -> EI[1],
  IPL23 device). The 8259 is already known to be on the critical path.
  See section 7 for the open question of polling vs interrupt.
- PIO is sufficient for items 1 and 2. Busmaster DMA is a Phase 5
  concern. Do not build DMA to reach a booting console.
- AXPBox is the reference implementation for chipset CSRs, PCI config
  space access, and the device-interrupt latch. Use AXPBox `src/System.cpp`
  (config-space and `interrupt()` paths) and its ALi/IDE device model as a
  known-good but lossy reference; prefer the ATA spec and the DS10
  firmware for exact behavior.
- The Alpha disk boot block is at LBN 0. It carries the starting LBN and
  block count of the primary bootstrap; the console reads that contiguous
  range into a virtually contiguous buffer starting at virtual
  0000_0000_2000_0000. No native code lives in the boot block itself, so
  item 2 needs only correct sector reads, not any OS-structure knowledge.

## 2A. Platform-model gating (configuration bifurcation)

The DQ/IDE device model is shared across the AlphaServer line, but the way
that model BINDS to a given machine is not. DS10, DS20, ES40 and ES45 each
present the M1543C IDE function at a different place in the PCI topology and
hang its interrupt off a different chipset fabric. This specification
therefore splits two concerns:

- The CONTROLLER (sections 4.1 - 4.5): platform-invariant. One
  IdeController / IdeChannel model, identical ATA behavior everywhere.
- The PLATFORM IDENTITY RECORD (this section): a small per-model struct that
  supplies the binding values the controller, the config-space seam, and the
  interrupt seam need. Selected once, at construction, from the active
  platform model. No platform branch is scattered through the device code.

For this cycle only the DS10 record is populated and validated. The gate and
the record contract are built now so the alternate paths can be consumed
later without re-architecting; DS20 / ES40 / ES45 are present as fail-closed
placeholders.

### 2A.1 Selection

The active platform model is read from the QSettings platform profile
`[LOCATE]` -- the same profile that already carries the system/device
sections (e.g. a `[System] platform_model = DS10` key, or the platform
manifest the profile references such as the scaffolded `*_platform.json`).
Cowork confirms the existing key name and loader. The dq subsystem performs
exactly ONE dispatch on that model to obtain its identity record; that
dispatch is the only place platform divergence is permitted.

### 2A.2 Identity record contract

Fields the dq subsystem requires from the per-platform record. Rows marked
SHARED are identical across the listed models (because every listed model
uses the M1543C in legacy ISA mode) but are still carried in the record for
uniformity and remain `_PROVISIONAL` until the DS10 probe confirms them
(section 6):

    model_name        selector string (DS10, DS20, ES40, ES45)
    chipset           Tsunami/Typhoon 21272 | Titan 21274
    pci_hose_count    number of PCI hoses (config-space + IRQ fabric scope)
    ide_pci_bus       PCI bus the IDE function enumerates on
    ide_pci_device    PCI device number (the "slot N" the probe prints)
    ide_pci_function  PCI function number of the IDE within its device
    ide_vendor_id     SHARED  ALi 0x10B9
    ide_device_id     SHARED  M5229 IDE (ALi)
    ide_cmd_base_pri  SHARED  0x1F0   (legacy ISA, M1543C legacy mode)
    ide_ctl_base_pri  SHARED  0x3F6
    ide_cmd_base_sec  SHARED  0x170
    ide_ctl_base_sec  SHARED  0x376
    ide_irq_pri       SHARED  14
    ide_irq_sec       SHARED  15
    intr_fabric       the chipset interrupt path the channel IRQ routes through
    superio_part      SuperIO part, for cross-checks (DS10: FDC37C669)
    validated         bool -- true ONLY for a model whose record is HRM /
                      firmware / trace confirmed end to end

A future non-M1543C platform would override the SHARED rows; they are
constant only for this model set, not by assumption.

### 2A.3 Per-platform table (this cycle: DS10 only)

DS10 -- VALIDATED target for this cycle:

    model_name        DS10
    chipset           Tsunami/Typhoon 21272 (single Pchip, 1 hose)
    pci_hose_count    1
    ide_pci_bus       0
    ide_pci_device    13          (probe shows "bus 0, slot 13 -- dqa")
    ide_pci_function  [CONFIRM]   single-function M5229 vs function 1 of the
                                  multifunction southbridge device
    intr_fabric       Tsunami Cchip: DRIR -> DIM -> b_irq<1> -> EI[1] (IPL23)
    superio_part      FDC37C669
    validated         set true ONLY after the section 11 boot-proof trace
    (SHARED rows as in 2A.2, each _PROVISIONAL pending the DS10 probe)

DS20 / ES40 -- PLACEHOLDER, Tsunami family, NOT validated:

    chipset           Tsunami 21272 (expected 2 Pchips / 2 hoses) [CONFIRM]
    pci_hose_count    [CONFIRM]
    ide_pci_bus/device/function   [CONFIRM] -- do NOT assume DS10 slot 13
    intr_fabric       Tsunami Cchip, but hose / IRQ routing differs [CONFIRM]
    validated         false

ES45 -- PLACEHOLDER, Titan family, NOT validated:

    chipset           Titan 21274 (dual hose; different Cchip interrupt regs)
    pci_hose_count    [CONFIRM] (2 expected)
    ide_pci_device    16 observed in one ES45 power-up -- reference only,
                      NOT bindable until confirmed against ES45 firmware
    intr_fabric       Titan interrupt fabric -- re-derive; do NOT reuse the
                      Tsunami DRIR/DIM path
    validated         false

### 2A.4 Fail-closed rule

If the active model's record has `validated == false`, or the model is
unknown, the dq subsystem MUST refuse to construct the IDE binding and emit
"platform IDE profile not validated for <model>". It MUST NOT fall through
to DS10 values, MUST NOT let any `[CONFIRM]` placeholder drive config-space
decode or interrupt routing, and MUST NOT silently disable IDE without a
diagnostic. This is the C2 data-fidelity rule applied at the platform
boundary: a half-correct ES45 binding that inherits DS10's slot 13 is exactly
the silent-corruption case this gate exists to prevent.

## 3. First decisions (scope and shape)

Decide these before any code. Recommended defaults are given; Cowork may
revise with a trade-off note.

- D1. Channel scope for Phase 1: implement the PRIMARY channel only
  (dqa), with the secondary stubbed to "no device present," or implement
  both channels symmetrically from the start.
  Recommended: build the channel as a reusable unit, instantiate two,
  but only require the primary to carry a real drive for Phase 1. Cost is
  near-zero and it avoids a later retrofit.

- D2. Device type for Phase 1: ATA hard disk only, or also ATAPI CD-ROM.
  On real hardware dqa is frequently the CD-ROM (ATAPI) and the disk is
  elsewhere. For the emulator the simplest booting target is an ATA hard
  disk presented at the position the console boots from.
  Recommended: ATA hard disk first (Phases 1-3). ATAPI CD-ROM is Phase 4,
  because ATAPI is a different command transport (PACKET) and is not
  needed to boot from an image.

- D3. Transfer model for Phase 1: PIO only. DMA deferred to Phase 5.
  Recommended: PIO only. Locked.

- D4. Backing store: raw flat image file (one host file per drive,
  512-byte logical sectors, byte offset = LBA * 512). No sparse or
  compressed formats in Phase 1.
  Recommended: raw flat image. Locked for Phase 1.

- D5. Interrupt dependency: see section 7. The answer to "does the
  console DQDRIVER poll or wait on IRQ14?" decides whether Phase 1 pulls
  in the Cchip device-interrupt path. Resolve D5 by trace/source BEFORE
  starting Phase 1 implementation.

- D6. Platform-model gating: the IDE binding (PCI bus/dev/func and probe
  slot, hose count, interrupt fabric, SuperIO part) is selected at
  construction from the active platform model in the QSettings profile
  (section 2A), NOT hardcoded. For this cycle only the DS10 record is
  populated and validated; DS20 / ES40 / ES45 records are scaffolded
  placeholders. The gate MUST fail closed (2A.4): an unvalidated or unknown
  model refuses to bind with a clear diagnostic rather than inheriting DS10
  values or any guessed value.
  Recommended: build the gate and the record contract now; populate only
  DS10. Locked for this cycle.

## 4. Component design

### 4.1 New unit: the IDE channel / controller

Add one new translation unit pair modeling a single IDE channel (two
possible drives: master/slave). Proposed names (Cowork confirms the
directory and the include-guard prefix convention in use):

    IdeChannel.h / IdeChannel.cpp     -- one ATA channel, 2 drive slots
    IdeController.h / IdeController.cpp -- owns primary + secondary channels,
                                          presents the PCI function

Include guards, never `#pragma once`, pattern `<DIR>_<FILE>_H`, e.g.
`DEVICELIB_IDECHANNEL_H` (substitute the actual library dir `[LOCATE]`).
ADR-0001 header on every new file. doctest `CHECK` only.

The controller is constructed and owned wherever the SuperIO and keyboard
controllers are constructed today `[LOCATE]`, and it registers its port
handlers with the same ISA I/O dispatcher.

### 4.2 ATA register file (per channel)

Command block (offsets relative to the channel's command base, e.g.
0x1F0). Reads and writes dispatch on (port - base):

    +0  Data           RW  16-bit PIO data port (sector buffer window)
    +1  Error (R) / Features (W)
    +2  Sector Count   RW
    +3  LBA Low  / Sector Number   RW
    +4  LBA Mid  / Cylinder Low    RW
    +5  LBA High / Cylinder High   RW
    +6  Drive/Head     RW  bit4 = drive select (0=master,1=slave),
                            bit6 = LBA mode, bits<3:0> = head/LBA<27:24>
    +7  Status (R) / Command (W)

Control block (offset relative to control base, e.g. 0x3F6):

    +0  Alternate Status (R) / Device Control (W)
        Device Control bits: nIEN (bit1) = interrupt disable, SRST (bit2)
        = software reset.

Status register bits (write a command -> controller updates these):

    bit7 BSY   busy
    bit6 DRDY  device ready
    bit5 DF    device fault
    bit4 DSC   device seek complete
    bit3 DRQ   data request (buffer ready to transfer)
    bit2 CORR  corrected data (legacy, leave 0)
    bit1 IDX   index (legacy, leave 0)
    bit0 ERR   error (read Error register for detail)

Reading Status (port +7) clears the pending interrupt for that channel
(this is the standard ATA acknowledge mechanism); reading Alternate
Status (control +0) does NOT clear the interrupt. `[CONFIRM]` the console
driver relies on this distinction before depending on it.

The Data port at +0 is the 16-bit PIO window into the active sector
buffer. The existing SuperIO finding -- that 16-bit word writes fold
into a single low-port access and drop the high byte -- is directly
relevant here: the IDE Data port is inherently 16-bit and the same
width-trace (`w=` field) discipline must confirm `w=2` accesses arrive
intact before the Data-port path is trusted. Do not implement the Data
port until the word-write width handling is settled, or implement it
behind the same width-correct path.

### 4.3 Command-processing state machine

Model a simple, deterministic PIO state machine per drive (single-threaded,
no host threads -- consistent with the best-effort deterministic
architecture). States: IDLE, BUSY, DATA_IN (drive->host), DATA_OUT
(host->drive), ERROR.

On a command write to +7:
1. Latch the command and the current task-file registers.
2. Validate drive present / LBA in range; on failure set ERR + Error
   register and (if the command was an interrupting one) raise the channel
   interrupt, then return to IDLE.
3. For a data-in command, fill the sector buffer, set DRQ, assert the
   interrupt per the command's protocol, and let the host drain the buffer
   through the Data port. When the buffer empties, either load the next
   sector (multi-sector reads) or clear DRQ and finish.

Because V4 is deterministic and not cycle-pressured here, BSY may be
modeled as transient (set during command latch, cleared before the host
reads Status) rather than time-accurate, UNLESS a trace shows the console
driver depends on observing BSY=1. `[CONFIRM]`. Name this determinism
trade-off in the file header if taken.

### 4.4 Command set, phased

Phase 1 (enumerate + boot-block read):
- 0xEC  IDENTIFY DEVICE        -- returns the 256-word identify block (4.5)
- 0x20  READ SECTORS (retry)   -- PIO, 1..n sectors, CHS or LBA28
- 0x21  READ SECTORS (no retry) -- alias behavior
- 0x90  EXECUTE DEVICE DIAGNOSTIC -- return passing diagnostic code 0x01
- 0xEF  SET FEATURES           -- accept and no-op the common subcommands
                                  (e.g. set transfer mode); return success
- 0x91  INITIALIZE DEVICE PARAMETERS -- accept CHS geometry set, success
  `[CONFIRM]` which of 0x90/0xEF/0x91 the DS10 console driver actually
  issues during probe; implement those, stub the rest to a clean success
  or a clean "aborted" as the trace dictates.

Phase 3 (OS block I/O writes):
- 0x30  WRITE SECTORS
- 0xC4  READ MULTIPLE / 0xC5 WRITE MULTIPLE (if word 47 advertises it)
- 0xE7  FLUSH CACHE

Phase 4 (ATAPI CD-ROM, if pursued):
- 0xA1  IDENTIFY PACKET DEVICE
- 0xA0  PACKET (SCSI-command transport over ATA)

Phase 5 (busmaster DMA):
- 0xC8  READ DMA / 0xCA WRITE DMA, plus the busmaster IDE registers
  (command, status, PRD table pointer) in the IDE PCI function's BAR4 I/O
  space, plus the Pchip DMA window translation (PCI address -> system
  address) which V4 does not yet model.

Any unimplemented command must complete cleanly: set ERR with Error<2>
(ABRT) rather than hang BSY. A hung BSY will wedge the console driver.

### 4.5 IDENTIFY DEVICE data block

512 bytes / 256 little-endian words returned through the Data port after a
0xEC command. Populate at least the words the console probe reads
`[CONFIRM]` from the driver; the conservative minimum:

    word 0      general configuration (0x0040 = fixed non-removable ATA),
                _PROVISIONAL until matched to what the driver checks
    word 1      number of cylinders (CHS)
    word 3      number of heads
    word 6      sectors per track
    words 10-19 serial number, ASCII, byte-swapped per ATA convention
    words 23-26 firmware revision, ASCII
    words 27-46 model number, ASCII (this is the "show device" string)
    word 47     <7:0> max sectors per READ/WRITE MULTIPLE (0 if unsupported)
    word 49     capabilities; bit9 = LBA supported, bit8 = DMA supported
    word 53     <1> words 54-58 valid, <2> word 88 valid
    words 54-58 current CHS, current capacity
    word 60-61  total addressable sectors (LBA28), dword
    word 80-81  major/minor version
    word 88     UDMA modes (only if DMA advertised)

Derive geometry and sector count from the backing file size: total
sectors = filesize / 512; pick a plausible CHS (e.g. 16 heads, 63
sectors) and set cylinders accordingly. ASCII fields use the ATA
byte-swap (each word is big-endian-within-word in the buffer). Mark word
0 and any capability bits `_PROVISIONAL` until a real-drive identify dump
or the driver's field checks confirm them.

### 4.6 PCI configuration header (the IDE function)

The console PCI probe must find the IDE function so it emits the `dqa`/`dqb`
line. Provide a config-space header for the function at the bus/device/
function given by the active platform identity record (section 2A). For
DS10 that is bus 0 / device 13 (probe "slot 13"); the function number is
still `[CONFIRM]`, and the whole BDF must be reconciled with `pc264_io.c` /
the probe before it gates decode. Do NOT hardcode the BDF in the controller
-- it comes from the record, and the SHARED Vendor/Device rows (ALi 0x10B9 /
M5229) likewise come from the record. Minimum config fields:

    Vendor ID     ALi = 0x10B9                     _PROVISIONAL
    Device ID     M5229 IDE function (ALi)         _PROVISIONAL
    Class code    0x01 (mass storage)
    Subclass      0x01 (IDE)
    Prog IF       legacy/native bits per channel   _PROVISIONAL
    Header type   0x00
    BARs          legacy-mode IDE uses fixed ISA ports; native-mode uses
                  BAR0-3 for the two channels and BAR4 for busmaster.
                  Phase 1 targets legacy mode (fixed 0x1F0/0x3F6 ...),
                  so BAR0-3 may read as 0 / not-implemented. `[CONFIRM]`
                  the console expects legacy mode on this platform.

This requires a PCI config-space access path to exist in V4. `[LOCATE]`
the config-space read/write dispatcher (AXPBox does this in System.cpp);
if V4 has none yet, standing up a minimal config-space responder for this
one function is part of this work and should be its own reviewed step.

### 4.7 Interrupt wiring

If section 7 concludes the console driver waits on IRQ14 (not polls), wire
the path named by the platform record's `intr_fabric` field (section 2A).
For DS10 that is the Tsunami Cchip path below; on a Titan platform (ES45)
this path uses different interrupt registers and a different hose routing
and MUST be re-derived, not reused:

    IDE channel asserts nINT
      -> ISA IRQ14 (primary) / IRQ15 (secondary) latched in the 8259 pair
      -> 8259 INTR output -> southbridge interrupt routing
      -> Cchip DRIR device bit set, gated by DIM
      -> b_irq<1> -> EI[1] (IPL23 device) -> CPU divert

This makes IDE the first real consumer of the Cchip device-interrupt path
(the path that currently carries only the timer). Standing up DRIR/DIM
delivery for a device source is the chipset-side prerequisite and should
be tracked as the concrete instance of the previously-scoped device-
interrupt work, not invented ad hoc here. Interrupt acknowledge: the
host reading the IDE Status register (+7) deasserts the channel's IRQ;
the 8259 EOI sequence and the DRIR/DIM W1C/clear semantics must be
honored so the line does not stick. `[CONFIRM]` each clear edge by trace.

### 4.8 CLI / configuration surface

Add options to attach backing images, following the existing AppOptions /
ini conventions `[LOCATE]`:

    --dqa0 <path>     attach image as primary master  (or --ide-pm)
    --dqa1 <path>     primary slave
    --dqb0 <path>     secondary master
    --dqb1 <path>     secondary slave

Empty / unspecified slot = "no device present" (IDENTIFY returns
device-not-present, probe skips it cleanly). Mirror these as ini keys in
the DS10-locked configuration block, consistent with how the H&M
configuring topic documents the other device keys. Document defaults as
the DS10 target state.

## 5. Integration seams Cowork must locate and verify

No line numbers are asserted here. Cowork finds each, reports the file and
line, and confirms the edit shape before writing:

1. `[LOCATE]` ISA I/O port dispatch -- where 0x3F0 (SuperIO) and 0x60/0x64
   (keyboard) reads/writes are routed today. IDE registers its port
   ranges here. Confirm word (16-bit) Data-port accesses arrive with
   `w=2` intact (the SuperIO byte-drop finding).
2. `[LOCATE]` PCI config-space read/write dispatcher (or confirm none
   exists and one must be added).
3. `[LOCATE]` device construction/ownership site (where SuperIO + keyboard
   controllers are instantiated and wired to the bus).
4. `[LOCATE]` Cchip DRIR / DIM device-interrupt delivery (the seam that
   currently raises only the timer's b_irq<2>; device IRQ uses b_irq<1>).
5. `[LOCATE]` 8259 PIC model -- confirm it exists, supports IRQ14/15
   latch + EOI, and how its output reaches the Cchip.
6. `[LOCATE]` AppOptions / ini parsing for the new --dqaN keys.
7. `[CONFIRM]` against the DS10 console / `pc264_io.c`: the IDE PCI
   identity, the bus/device/function, the legacy port bases, the IRQ
   numbers, and whether the driver polls or interrupts (section 7).
8. `[LOCATE]` the QSettings platform-profile loader and the point where the
   active platform model is determined -- the dq gate (section 2A.1) hooks
   there to select the identity record. Confirm the existing key name for
   the platform model and whether the per-platform binding lives in the .ini
   sections or in a platform manifest the profile references.

## 6. Data-fidelity gate (do not skip)

Every value tagged `_PROVISIONAL` above is acceptable for C1 storage
(holding the value in a struct) but MUST be HRM- or firmware-verified
before it is allowed to drive any decode or any PCI/IDENTIFY field the
console actually inspects. Specifically: the IDE PCI Vendor/Device/Prog-IF,
the port bases, and the IRQ numbers must be confirmed against the DS10
firmware probe before they gate config-space decode or interrupt routing.
Silent identity corruption here produces a probe that half-works and is
very hard to debug later.

With platform gating (section 2A) this rule applies PER RECORD: a value is
only "confirmed" for the model it was verified on. A DS10-confirmed BDF does
not confirm DS20 / ES40 / ES45. Each record carries its own `validated`
flag, and the fail-closed gate (2A.4) enforces that an unvalidated record
can never reach decode.

## 7. The one question to resolve first: poll vs interrupt

The single highest-leverage unknown. Many console-resident disk drivers
poll the Status register (spin on BSY clear / DRQ set) rather than depend
on IRQ14. If the DS10 console DQDRIVER polls:

- Phase 1 needs NO interrupt wiring. Sections 4.7 and seam 4/5 drop out of
  the critical path. Blast radius shrinks dramatically.

If the DS10 console DQDRIVER waits on IRQ14:

- The Cchip device-interrupt path + 8259 IRQ14 delivery become Phase 1
  prerequisites, and must be stood up before READ SECTORS can complete a
  command the driver will observe.

Resolve this by reading the DS10 console DQDRIVER source (apisrm tree) or
by tracing the probe's access pattern to the IDE ports -- BEFORE writing
Phase 1 code. This is a discuss-before-code, trace-first decision.

## 8. Phased delivery plan

- Phase 0  Resolve section 7 (poll vs interrupt). Confirm port bases,
           PCI identity, bus/dev/func from DS10 firmware. Stand up the
           section 2A platform gate and identity-record contract; populate
           and validate the DS10 record only; leave DS20 / ES40 / ES45 as
           fail-closed placeholders. No device code beyond the gate scaffold.
- Phase 1  PCI config presence + IDENTIFY DEVICE + READ SECTORS, PIO,
           primary channel, ATA hard disk, raw image backing, legacy
           ports. Plus interrupt wiring only if Phase 0 says it is needed.
           Exit criteria: `show device` lists dqa0 with a model string;
           `boot dqa0` reads LBN 0 and the primary bootstrap.
- Phase 2  Robustness: secondary channel, slave drives, multi-sector
           reads, SET FEATURES / INIT DEV PARAMS as the driver needs,
           clean ABRT for everything unimplemented.
- Phase 3  WRITE SECTORS, READ/WRITE MULTIPLE, FLUSH CACHE -- enough for
           an OS DQDRIVER to do block I/O.
- Phase 4  ATAPI CD-ROM (IDENTIFY PACKET DEVICE + PACKET) if a CD boot
           path is wanted.
- Phase 5  Busmaster DMA (READ/WRITE DMA, PRD tables, Pchip DMA window
           translation). Largest blast radius; do last.

## 9. Test and assertion plan (doctest, CHECK only)

Unit tests, deterministic, no host threads, exceptions disabled so `CHECK`
only:

- Register file: write each task-file register, read it back; verify
  drive-select bit routing and LBA/CHS field packing.
- IDENTIFY: issue 0xEC against a known image size; `CHECK` total-sector
  word matches filesize/512, `CHECK` the model-string bytes decode after
  the ATA byte-swap, `CHECK` capability bits match the advertised feature
  set.
- READ SECTORS: seed an image with a known pattern (e.g. LBA number in
  the first word of each sector); issue 0x20 for an LBA, drain the Data
  port, `CHECK` the bytes equal the seeded pattern; `CHECK` multi-sector
  read advances correctly; `CHECK` out-of-range LBA sets ERR+ABRT and
  does NOT hang BSY.
- Status/interrupt: `CHECK` that an unimplemented opcode returns ERR with
  Error<2> set and clears BSY; if interrupts are wired, `CHECK` reading
  Status (+7) deasserts the channel IRQ and reading Alt Status (control+0)
  does not.

Integration / boot proof (the real acceptance test):

- Attach a minimal bootable image, run the boot path, and capture a
  bounded trace window around the IDE port accesses and the boot-block
  read. Confirm: the probe emits the dqa line; the console reads LBN 0;
  the primary bootstrap lands at virtual 2000_0000. Use gated/bounded
  trace windows only -- never whole-file grep over a multi-GB trace.
- Width check: confirm 16-bit Data-port transfers carry `w=2` and the
  high byte is not dropped (the SuperIO finding applied to IDE).

## 10. Out of scope here

- Busmaster DMA design detail (Phase 5; only sketched).
- Pchip DMA window / scatter-gather TLB modeling (pulled in only by DMA).
- The OS-side DQDRIVER internals; this spec covers the emulated controller
  the OS driver talks to, not the driver.
- Any change to the current boot halt or decompressor work; if a boot
  issue surfaces that is not an IDE-port or boot-block-read issue, it is a
  separate track.
- The SuperIO byte-split fix itself -- that is its own in-flight change;
  this spec depends on it being settled for the 16-bit Data port but does
  not redo it.

## 11. Deliverables expected from Cowork

1. Phase 0 findings: poll-vs-interrupt answer, confirmed PCI identity,
   port bases, bus/dev/func, IRQ numbers, each with its source citation.
2. The located seams from section 5 (file + line + edit shape), proposed
   back for approval before writing (discuss-before-code).
3. Phase 1 implementation: IdeChannel/IdeController, config-space presence,
   IDENTIFY + READ SECTORS, CLI/ini keys, ADR-0001 headers, include
   guards, doctest CHECK tests.
4. The boot-proof trace window showing dqa0 enumerated and LBN 0 read.
5. The section 2A platform gate: the identity-record struct / contract, the
   single dispatch point that reads the active platform model from the
   QSettings profile, the DS10 record populated, DS20 / ES40 / ES45 wired as
   fail-closed placeholders, and a doctest CHECK that an unvalidated model
   refuses to bind and emits the "platform IDE profile not validated"
   diagnostic.

---

## Standing EmulatR V4 rules (apply to all implementation work)

### Workflow
- Discuss before code. Non-trivial changes are proposed first as prose
  with file paths, line numbers, and the concrete edit shape; wait for
  approval before editing. Applies even to one-line changes with
  architectural meaning. Exception: trivial typo/formatting fixes the
  user pointed out.
- Documentation at header and source line. Every source change updates a
  header block (rationale, date, behavior/bug addressed, in the
  "FILE N: ... FUNCTION: ... CHANGE: ..." style) and leaves an inline
  comment at the changed line referencing it. No anonymous changes.
- TODO discipline. Incomplete-on-purpose code is documented at both the
  file header (a named TODO table) and the call site
  (// TODO(<tag>): <summary>). Greppable tags; removed in the same edit
  that lands the wiring.
- Best-effort deterministic architecture. Single-threaded by default, no
  nondeterministic timing or race-prone shared state, predictable
  side-effect ordering (BoxResult populated at EX, applied at MEM, traced
  at WB). Name any determinism trade-off in the spec.

### File / source conventions
- ASCII(128) only in all file content -- no smart quotes, em dashes,
  Unicode arrows, or box-drawing glyphs, in source, comments, docs, or
  generated artifacts. The MSVC pipeline expects unformatted ASCII(128).
- Copyright/attribution header on every generated source/header (and
  Markdown specs as an HTML comment), per
  docs/notes/ADR-0001-source-file-headers.md, template at
  docs/notes/templates/header_cpp.txt. Hard rule, no "small file"
  exemption.
- Include guards, never #pragma once. Pattern
  #ifndef <DIR>_<FILE>_H / #define / #endif, e.g.
  PIPELINELIB_PIPELINEDRIVER_H. (Qt MOC + pragma once under MSVC
  /permissive- causes LNK2001.)
- Hex radix for all switch/case dispatch labels, never mixed; convert
  dec->hex by value (16 -> 0x10, not 0x16), never digit substitution.
- Prefer surgical Edit over whole-file rewrites in V4; treat
  V0/V1/V2 and Processor Support as read-only.

### C++ / build specifics
- doctest: CHECK only, never REQUIRE (exceptions disabled in V4).
- Never name an enum's printable helper toString (doctest ADL clash) --
  use <typeName>Name(T) plus operator<<.
- Codegen vs hand-written differentiator: hand-written leaves use
  auto X() -> Y; codegen emits direct Y X(). Generated-header edits are
  lost on regen -- change genGrains.py, not the output.

### Logging / Qt
- Logging is implemented behind CMake compile guards (zero-cost
  ((void)0) expansion in Release), with a runtime mute knob via
  LogSubsystem. Runtime trace toggles such as EMULATR_IIC_TRACE and
  EMULATR_GCT_WATCH are environment variables, NOT CMake compile guards.
- Qt surface stays minimal and is used sparingly, only at named seams
  (e.g. threading) with inline justification; defer to the std library
  whenever possible. QJsonDocument/QJsonParseError is the approved JSON
  exception.

### Data fidelity
- Provisional IPR/SCBD/PCI-identity values are OK for C1 storage but never
  for C2 decode -- mark guessed values _PROVISIONAL and HRM/firmware-verify
  before any dispatch or decode matches them (silent corruption otherwise).

### Trace / debug discipline
- Multi-GB traces: bounded tails / gated windows only, never whole-file
  grep (times out, wedges the sandbox).
- Verify every file write via bash (wc -l / grep); prefer heredoc for
  large writes.

### Collaboration
- For analytically-heavy, open-ended work: claude.ai web for the
  analysis/design, Cowork for the edits. Claude web provides the
  instructional design; Cowork implements against the live tree.
