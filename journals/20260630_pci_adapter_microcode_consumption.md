# PCI Adapter Microcode — How and When EmulatR Consumes the DS20 Option-ROM `.SYS` Files

**Date:** 2026-06-30
**Author frontier:** post-`>>>` / PCI enumeration (deferred)
**Status:** design note — nothing implemented yet. This is the "what it means to
consume these files, and when" reference for a future session that picks up the
`TsunamiPchip: UNHANDLED OUTER WRITE` / PCI-enumeration work item in `CLAUDE.md`.

---

## 1. What these files are

The DS20 firmware kit (`firmware/ds20_disk1|2|3/`) contains, besides the two
console images (`pc264srm.sys` = SRM, `PC264NT.SYS` = AlphaBIOS/ARC) and the
fail-safe booter (`PC264FSB.SYS`), a set of **PCI option-card microcode images**:

| File | dest (per `pc264fw.txt`) | Adapter | Onboard processor (executes the microcode) |
|------|--------------------------|---------|--------------------------------------------|
| `kzpsaa12.sys` | `kzpsa_fw`   | KZPSA PCI→SCSI FWD HBA | QLogic ISP-class SCSI RISC |
| `cd393a0.sys`  | `fca_2354_fw`| KGPSA Fibre Channel HBA | Emulex (i960 / ARC core) |
| `hd191x6.sys`  | `fca_2384_fw`| KGPSA FC HBA (variant)  | Emulex |
| `td191x6.sys`  | `fca_2684_fw`| KGPSA FC HBA (variant)  | Emulex |
| `dd393a0.sys`  | `kgpsa_8k_fw`| KGPSA FC HBA (8K)       | Emulex |
| `kg7320x7.sys` | `kgpsa_7k_fw`| KGPSA FC HBA (7K)       | Emulex |
| `kzpsaa12.sys` | `kzpsa_fw`   | KZPSA SCSI              | QLogic ISP |
| `kzpdc...` (`KZPDC356.SYS`) | `kzpdc_fw` | KZPDC RAID/SCSI | RAID controller CPU |
| `cipca420.sys` | `cipca_fw`   | CIPCA CI-to-PCI cluster | CI adapter CPU |
| `dfxaa320.sys` | `dfxaa_fw`   | DEFPA PCI FDDI          | FDDI adapter CPU |

**Not in the kit (important):** there is **no `ew*.sys` / tulip / 21143 image.**
The on-board DE500/DEC 21143 Ethernet (SRM name `EWA0`) has **no downloadable
operational microcode** — its "firmware" is a MAC-address SROM read (CSR9
bit-bang) plus descriptor-ring DMA. So the **EW interface does NOT consume any
file in this kit.** See §6.

### File format (all option images identical container)
- Outer wrapper: DEC **MOP `.SYS`** update container, format tag `0205`, fixed
  0xA8-byte header, packager stamps `MOP` / `V1.0` / `05-05` (uniform, not
  per-card). Sub-record offset table `{0xA8, 0x30, 0x44, 0x58}`; the `0x30`/`0x44`
  slots are **zero-filled** (no embedded device name).
- Payload record at **0xA8**: `10 00` marker, `+2` = **image size in 512-byte
  blocks** (the ONLY per-file variable header field), `+8` = `0x80`, `+12` = `2`,
  then the opaque adapter microcode.
- **No PCI vendor/device ID, no load address, no entry point** is embedded in a
  generically usable form. The payload is **adapter-CPU code, not Alpha code** —
  EmulatR's SrmLoader/FirmwareLoader signatures (`04 04 3F 44` decompressor,
  `WimC`, `c3c3 5a5a`) are all absent. Target identity comes **externally**: the
  filename → `pc264fw.txt` destination map, plus the SRM's built-in table of which
  microcode belongs to which card.

---

## 2. Where the microcode is loaded — the crux question

**It is NOT loaded into host GuestMemory, NOT into the chipset (Tsunami
Cchip/Dchip), and NOT stored in the Pchip / PCI host bridge.** Its execution
home is the **adapter's own local RAM — inside the HBA device model itself**
(the future `KzpsaDevice` / `KgpsaDevice` on the emulated PCI bus).

GuestMemory and the Pchip are only the **transport path**:

```
  SRM/driver on emulated Alpha CPU
      │  (1) reads image from console flash filesystem
      ▼
  Host RAM staging buffer  ── memoryLib::GuestMemory        [TRANSIT ONLY]
      │  (2) MMIO writes and/or a DMA descriptor
      ▼
  TsunamiPchip  (chipsetLib/TsunamiPchip.h) host→PCI bridge [CONDUIT ONLY]
      │  address translation + scatter/gather; stores nothing
      ▼
  Adapter model's onboard RAM  ── e.g. KzpsaDevice::m_ispRam [EXECUTION HOME]
      │  (3) adapter's own CPU (ISP RISC / Emulex i960) runs it
      ▼
  Adapter operational — presents SCSI/FC targets to the guest
```

Role of each layer, precisely:

- **GuestMemory** — host Alpha physical RAM. The image *transits* here (staged
  by the console, and/or is the DMA source for the download), but the microcode
  **does not execute here.** Nothing persistent lives here.
- **Chipset (Tsunami Cchip/Dchip)** — the host memory controller. Involved only
  insofar as it backs the staging buffer. It never holds microcode.
- **Pchip / PCI controller** (`TsunamiPchip.h`) — pure conduit: PCI config
  space (`writePciConfig0`), I/O and memory windows, address translation,
  scatter/gather. It **never stores microcode.** Today the SRM's BAR probes and
  register pokes for these (unmodeled) cards fall straight through
  `TsunamiPchip::write` to the `UNHANDLED OUTER WRITE` log (`TsunamiPchip.h:729`,
  gated by `EMULATR_BRINGUP_PROBES`) precisely because **no target device claims
  the transaction.**
- **FC / SCSI controller model** (to be written) — **the destination.** Holds a
  private local-RAM byte array representing the adapter's onboard instruction
  RAM (and, for KGPSA, its onboard flash). This is the only place the microcode
  "lives," and the only place it would ever execute.

### Answering the specific question directly
> *Where does one load the KZPSA and KGPSA files? GuestMemory, chipset, PCI
> controller, or the FC controllers?*

**Into the FC/SCSI controller models** (their emulated adapter-local RAM/flash).
GuestMemory and the Pchip are the wires the bytes travel over, not the
destination. In stub form (§4) the bytes are never even retained — the adapter
model just ACKs the download.

---

## 3. Two hardware load models (both end in adapter-local RAM)

1. **Host-download model (KZPSA / QLogic ISP).** The adapter powers up with a
   minimal boot ROM; the driver/console downloads the operational RISC firmware
   into the ISP's instruction RAM via the mailbox registers at init. Destination
   = adapter model's `m_ispRam`.
2. **Onboard-flash model (KGPSA / Emulex, CIPCA, DEFPA, KZPDC).** `fwupdate`
   burns the `.sys` payload into the adapter's **own flash**; the adapter
   self-loads from that flash at power-on. Destination = adapter model's
   emulated flash store, then its local RAM at boot.

In **both** cases the destination is the adapter, never host RAM or the bridge.

---

## 4. What "consuming" one actually requires in EmulatR

There is **no loader path today** and these are **not** loaded like an SRM ROM.
To consume one you build a **PCI adapter device model** and drive the SRM's own
download logic through it. The parameters the model needs come from the
**adapter datasheet, not the `.sys` file**:

1. **Target identity** — PCI `vendor:device` (+ subsystem) ID, so SRM
   enumerate/`fwupdate` matches the card. (This is the missing-BAR device behind
   the current `UNHANDLED OUTER WRITE`.)
2. **Download interface** — the BAR-mapped MMIO window + mailbox/DMA handshake
   the real chip exposes (QLogic ISP: RISC-RAM load via mailbox regs; Emulex:
   SLI mailbox). Chip-specific.
3. **Adapter-local load address + entry** — where in adapter RAM the code lands
   and starts. Chip-specific; **not** in the `.sys`.
4. **Execution decision** — either
   - **(a) execute** the microcode on an emulated adapter CPU (ISP RISC /
     Emulex i960) — an enormous lift with **no payoff for reaching `>>>` or even
     for guest-OS disk I/O**, since the guest talks to the *modeled behavior*,
     not the real chip; or
   - **(b) stub** it — accept the download writes, ACK them, present a valid
     BAR + config space, and expose the SCSI/FC target behavior directly. **This
     is the recommended path.** The `.sys` payload stays **inert** — never
     parsed, never run; the only datum you might echo back is its block-count/
     byte-size (§1) as a plausible "image size."

The existing `deviceLib/scsi/` stack (`VirtualScsiDevice`, `ScsiCommand`,
`BlockMediaFactory`, `FileBlockMedia`) already models SCSI at the
**command/logical-unit level** — i.e. exactly the "modeled behavior" a KZPSA
stub would expose upward, without any ISP microcode. FC (KGPSA) has **no**
equivalent stack yet; EW has none.

Device-tree hooks already exist: `deviceLib/FirmwareDeviceManager.h`'s
`DeviceNodeType` enumerates `SCSIController` (PKB0/PKC0), `NetworkInterface`
(EWA0), `PCIBus`, etc., and `DeviceNode` carries `mmioBase/mmioSize`,
`location` ("…/hose0/bus3/slot1"), and `classType` ("SCSI_HBA"/"NIC"). A new
adapter model registers as one of these nodes and claims a Pchip BAR range.

---

## 5. When to consume them (sequencing / gates)

Strictly **after `>>>`** and only when the corresponding subsystem is being
brought up for **guest-OS boot** (Tru64/VMS need disks via KZPSA/KGPSA; network
via EW). Ordered gates:

- **Gate 0 — reach `>>>`.** Unblocked frontier; do not divert here for HBAs.
- **Gate 1 — PCI config-space + BAR model.** Make the cards enumerate so the
  Pchip stops emitting `UNHANDLED OUTER WRITE`. Lowest-risk, highest-signal;
  can be a pure stub answering config reads with valid IDs + one sane BAR.
- **Gate 2 — per-HBA register/mailbox model that absorbs the download.**
  `fwupdate` completes; adapter reports "firmware present." Still no execution.
- **Gate 3 (usually skip) — adapter-CPU execution.** Only if a scenario truly
  needs bug-for-bug adapter behavior. Almost never worth it.
- **Gate 4 — upward behavior.** Wire the modeled SCSI/FC target stack
  (`deviceLib/scsi/…`) so the guest sees `DKA0`, etc.

**Priority ordering among cards** (lower = later): KZPSA SCSI and KGPSA FC are
the ones a booting guest actually needs (system disks). CIPCA (cluster), DEFPA
(FDDI), KZPDC (RAID) are niche — model only on demand. None of them block `>>>`.

---

## 6. EW (Ethernet) is a separate mechanism — do not conflate

The DS10/DS20 on-board NIC is a **DEC 21143 "tulip"** (SRM `EWA0`). It has **no
downloadable operational microcode and no `.sys` file in this kit.** Its
"firmware" is:
- a serial **SROM read** (MAC address + media table) via CSR9 bit-bang, and
- normal **descriptor-ring DMA** for packets.

So the EW interface, when implemented, consumes **none** of these microcode
files. It needs a 21143 register/CSR model + SROM contents (MAC), tied into the
host networking backend (see the Npcap/NDIS6 networking note). This is the same
device implicated in the current `UNHANDLED OUTER WRITE` CSR9 bit-bang poke —
so a 21143 model with a valid BAR + MAC SROM is what actually silences that log,
independent of any `.sys` microcode.

---

## 7. One-line summary

These `.SYS` payloads are **adapter-CPU microcode destined for the HBA's own
local RAM/flash — not host RAM, not the chipset, not the Pchip.** EmulatR does
not "load" them at all in the ROM sense; consuming one is a **device-modeling**
task keyed on PCI IDs and per-chip download protocols, best done as a **stub**
(absorb the download, present the modeled SCSI/FC behavior). The file itself
contributes only its **identity (filename → `pc264fw.txt`)** and **size
(block-count field)**. Gate the work behind `>>>` and the PCI-enumeration item;
EW is a distinct 21143/SROM path that consumes no file here.

---

### Cross-references
- `CLAUDE.md` → *Deferred / planned work* → "PCI device enumeration + on-board
  device models" (the `UNHANDLED OUTER WRITE` item this note elaborates).
- `chipsetLib/TsunamiPchip.h:729` — `UNHANDLED OUTER WRITE` fallthrough.
- `deviceLib/FirmwareDeviceManager.h` — `DeviceNodeType` / `DeviceNode` tree.
- `deviceLib/scsi/` — existing command/LU-level SCSI stack (the "upward
  behavior" a KZPSA stub exposes).
- `firmware/ds20_disk1/pc264fw.txt` — filename → destination microcode map.
- Networking note — Npcap NDIS6 policy / host backend for the EW path.
