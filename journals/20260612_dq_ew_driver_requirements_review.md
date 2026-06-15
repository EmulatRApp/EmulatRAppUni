# dq_driver / ew_driver — SRM Driver Support Requirements Review (2026-06-12)

Scope: what EmulatR V4 must model so the SRM console's own firmware
drivers `dq_driver` (IDE/ATAPI) and `ew_driver` (DE500 / 21143 "tulip"
Ethernet) probe and operate the emulated hardware faithfully, so a
session enumerates `dqa*` AND `ewa/ewb/ewc…` at `>>>` and `show device`.

Authority sources (read-only):
`Processor Support\Palcode\palcode\apisrm\apisrm\ref\`
 - `dq_driver.c` (1845 lines) + `ide_def.sdl`, `ide_csr_def.h`,
   `dq_pb_def.h`, `ide_pb_def.h`
 - `ew_driver.c` (6474 lines, "Port driver for the TULIP board") +
   `dc287_def.h` (21143 CSR map), `f21140_edit.c`

Key framing: these drivers run ON the emulated CPU. EmulatR does NOT
implement the drivers — it implements the *device hardware* the drivers
poke. "Support" = a faithful-enough device model that the firmware's
probe/init/IO sequences see the responses they expect.

---

## PART A — dq_driver (IDE / ATAPI)   STATUS: ~90% DONE

### What dq_driver touches (from the source)

1. **PCI config probe** (`dq_init_ide`, lines 676-734):
   - reads cfg 0x00 (id), expects `0xC6931080` (Cypress CY82C693).
   - reads cfg 0x09 (prog-IF) + 0x0A (class word), expects `0x0101` (IDE).
   - for the Cypress, FORCES legacy mode and writes BAR 0x10=0x1F0,
     0x14=0x3F6 (func1 primary) / 0x170,0x376 (func2 secondary).
   - => the model must answer config reads with id `0xC6931080`,
     class `0x0101`, and tolerate the legacy-BAR writes.

2. **Legacy ATA register block** (macros `rb/rw/wb/ww` at top of file):
   - command block `pb->csr` = 0x1F0 (primary) / 0x170 (secondary)
   - control block `pb->csr2` = 0x3F4..0x3F7 (alt-status/control 0x3F6)
   - register offsets used: DATA(0), FEATURES/ERROR(1), SECTOR_COUNT(2),
     LBA_LOW/MID/HI(3/4/5), DRIVE_HEAD(6), STATUS/COMMAND(7),
     ALT_STATUS(0x3F6).

3. **Status handshake** (`dq_wait_on_busy`, `dq_wait_for_drq`,
   `dq_long_wait_on_busy`, lines 1236-1286): polls ALT_STATUS for
   `STATUS_BSY (0x80)`, `STATUS_DRQ (0x08)`; ERR(0x01), DRDY(0x40).
   The model must clear BSY and raise DRQ/DRDY in the right order or
   the driver times out (5000 / 310000 spin iterations).

4. **IDENTIFY** (`dq_identify`, line 1730): issues IDENTIFY DEVICE
   (0xEC) / for ATAPI the signature 0x14/0xEB on LBA_MID/HI plus
   IDENTIFY PACKET DEVICE (0xA1); reads 256 words of identify data.

5. **ATAPI packet path** (`atapi_do_send`, line 1345): selects device
   (DRIVE_HEAD 0xA0|node<<4), writes FEATURES=0, byte-count into
   CYLINDER_LO/HI (LBA mid/hi), command PACKET(0xA0), then ships the
   12-byte CDB on the data port and transfers data per the interrupt-
   reason (sector-count) bits. Drives INQUIRY / TEST UNIT READY /
   READ(10) / READ CAPACITY for the CD.

### V4 status — already satisfied
- `ds10_platform.json`: `cypress_ide` func1, vendor 0x1080 / dev 0xc693,
  class 0x010100, with a `storage[]` block declaring **dqa0 = ATA disk**
  (primary master) and **dqa1 = ATAPI CD** (primary slave).
- Named model `Cy82C693Ide` owns config space + fixed legacy ports
  (pri 0x1F0/0x3F6, sec 0x170/0x376). Prior sessions: func1 enumerates
  (vendor 0x1080/dev 0xC693), legacy ports route with no Cypress
  catch-all shadow, ATAPI handshake works (reset → 0x14/0xEB sig →
  IDENTIFY PACKET → TEST UNIT READY → 02/3A/00 no-media), 407 tests
  green, `show device` lists the CD. (Today's run showed `dqa1`.)

### dq remaining work (the last ~10%)
1. **dqa0 vs dqa1.** Today's `>>>` listed only `dqa1` (the CD). Confirm
   the primary-MASTER ATA disk (dqa0) enumerates when `storage[0].media`
   names a real image; verify IDENTIFY DEVICE (0xEC, non-ATAPI) returns
   a valid 256-word block (the disk path, distinct from the CD's 0xA1).
2. **Real media reads.** `dq_read`/`dq_send_read_write` → READ(10) must
   return actual sector bytes from the backing file so a `boot dqa0`
   can pull LBN 0. Needs the ATA READ SECTORS (0x20) path + the ATAPI
   READ(10) path wired to a file backend.
3. **`exer` / disk test path** ("Testing the Disks" in the run) exercises
   `dq_setmode` + repeated reads — only matters if you want `test` clean.

NET: dq is an enumeration-complete model needing the **media-backed read
path** to go from "device present" to "bootable". This dovetails with
the Stratum-4 media-file backend (your tasks #13/#14).

---

## PART B — ew_driver (DE500 / DECchip 21143 "tulip")   STATUS: STUB ONLY

`ew_driver.c` is the TULIP port driver. For DS10 the on-board NIC is the
**DEC 21143 (DC287), PCI vendor 0x1011 / device 0x0019, class 0x020000**
— exactly what `ds10_platform.json` declares. BUT that entry is
`"model": "generic"`, i.e. a config-space-only stub with BARs and no
behavior. `ew_driver` will enumerate it and then fail at the first CSR /
SROM access. To get `ewa0` to appear, a real 21143 model is required.

### What ew_driver requires the device to implement (from the source)

1. **PCI config** (`tu_init_device` ~2151, `ew_init_pb` ~630):
   - cfg 0x00 id (DEC 0x1011 / 0x0019), cfg 0x08 rev (`TU_PCFRV_ADDR`),
   - cfg 0x10/0x14 BARs (I/O + mem CSR window), cfg 0x2C subsystem id
     (used to distinguish DE500-AA/BA/etc.),
   - `TU_CFDA_ADDR`/`TU_PCFDA_ADDR` (the 21143 "Configuration and Driver
     Area" cfg register at 0x40 — sleep/snooze power state; driver writes
     it to wake the chip). Must be writable/readable.

2. **CSR0–CSR15** (`dc287_def.h`, 8-byte-strided at offsets 0x00..0x78):
   - CSR0 bus mode (reset bit), CSR1 tx poll demand, CSR2 rx poll demand,
   - CSR3 rx ring base, CSR4 tx ring base, CSR5 status, CSR6 operating
     mode (sr/st/st rx-tx enables), CSR7 interrupt mask, CSR8 missed-frame,
   - CSR9 ROM/MII serial port (the SROM + MII bit-bang!), CSR11 full-dup,
   - CSR12 SIA status, CSR13/14/15 SIA connectivity/TxRx/general.
   - The header gives reset values (`DC287_ICSR*`), writable masks
     (`DC287_MCSR*`). A faithful model honors the reset values and the
     RW masks, and self-clears CSR0<0> after the soft reset.

3. **Serial-ROM read via CSR9** (`microwire_nirom` ~1667/1788,
   `tu_get_*nirom` ~1400-1619, `tu_check_nirom`, `tu_nirom_checksum`):
   - The driver bit-bangs the 93C46-style microwire SROM through CSR9
     (chip-select / clock / data-in / data-out bits) to read the
     **NI-ROM image**: the station MAC address (6 bytes) + the
     **media/connector table** the 21143 uses to pick TP/AUI/MII.
   - `tu_nirom_checksum` validates it. => the model MUST present a
     correct 128-byte SROM image with a valid checksum and a DEC
     media-table layout, or the driver rejects the adapter.
   - This is the single most important sub-model: no valid SROM ⇒ no
     MAC ⇒ no `ewa0`. (Note: the existing `UNHANDLED OUTER WRITE
     0xFFFF0000` chatter at boot is exactly the firmware bit-banging
     CSR9 against the missing NIC — see the PCI-enumeration deferred
     note in CLAUDE.md.)

4. **MII / PHY** (`tu_mii_rd/wr`, `tu_sendto_mii`, `tu_getfrom_mii`,
   `tu_auto_negotiate` ~5836-6180): for DE500-BA (10/100) the driver
   talks to an external PHY over the MII management bus (also through
   CSR9 GEP bits) and runs auto-negotiation. A minimal model can present
   a single PHY at a fixed address returning link-up + a fixed ability
   set, OR declare a TP-only media via the SROM so the SIA path
   (CSR13/14/15) is used and MII is skipped. The SROM media table
   choice decides which path the driver takes — pick the simpler SIA/TP
   path first.

5. **TX/RX descriptor rings** (`tu_init_tx/rx/st` ~2238-2428,
   `tu_out`/`tu_send`, `tu_rx`/`tu_process_rx`): standard tulip
   descriptor format in guest memory (OWN bit, buffer ptrs, status).
   For ENUMERATION + a `show device`/MAC display you do NOT need working
   rings. For an actual MOP/BOOTP network boot you do. The first
   milestone (ewa0 visible + correct MAC) needs only config + CSR reset +
   SROM read.

6. **Setup frame** (`tu_build_perfect`, `tu_init_st`): the driver sends a
   "perfect filtering" setup frame through the TX ring to program the
   address filter. Only needed for live RX.

### ew implementation tiers (recommended sequencing)

- **Tier 0 — enumerate (gets `ewa0` in show device + a MAC):**
  replace the `generic` model with a `Dc21143Tulip` named model that
  (a) answers config space (already true via generic), (b) implements
  CSR0 soft-reset semantics + CSR reset values, and (c) serves a valid
  128-byte SROM with a programmable MAC + a TP media entry over CSR9
  microwire. This satisfies `tu_init_device` → `tu_get_nirom` →
  `tu_check_nirom`/`checksum` and silences the 0xFFFF0000 CSR9 chatter.
- **Tier 1 — link + media select:** CSR12 SIA status returns link-up;
  CSR13/14/15 accept the TP programming; (or a stub MII PHY). Driver
  completes `tu_init_media`/`tu_auto_negotiate` and marks the port up.
- **Tier 2 — TX/RX rings + setup frame:** real packet movement to a host
  backend (tap/loopback) for MOP/BOOTP/TFTP netboot. Largest lift; only
  needed for actual network boot.

### Multiple interfaces (ewa / ewb / ewc …)

The SRM auto-letters NICs by enumeration order (`ewa`, `ewb`, …); V4's
`SRMConsole.cpp` already maps class "ethernet"→prefix "ewa" and counts
ports. So **multiple `ew*` units fall out for free** once the device
model exists: declare N tulip entries in `ds10_platform.json`
`pci_devices[]` (distinct slots, each its own SROM/MAC), and the
existing naming logic assigns ewa0/ewb0/ewc0. No driver-side work — the
gating item is the single `Dc21143Tulip` model; instance it per JSON row.

---

## V4 readiness summary

| Layer | dq (IDE/ATAPI) | ew (21143 tulip) |
|-------|----------------|------------------|
| PCI config-space answer | DONE (Cy82C693Ide) | partial (generic stub: id/class/BARs only) |
| Register/CSR behavior | DONE (legacy ATA block) | MISSING (CSR0-15) |
| Identity ROM | DONE (IDENTIFY/IDENTIFY PACKET) | MISSING (CSR9 microwire SROM + MAC + media table) |
| Link/PHY | n/a | MISSING (SIA or MII) |
| Data path | partial (media-backed read TODO) | MISSING (TX/RX rings) |
| SRM naming (dqa*/ewa*) | DONE | DONE (auto-letters once device exists) |
| Platform manifest entry | DONE | present but `model:"generic"` |

## Recommended next actions
1. **dq:** wire the media-backed read path (ATA READ 0x20 + ATAPI
   READ(10)) to the Stratum-4 file backend; confirm dqa0 master disk
   enumerates with a real image. (Unlocks `boot dqa0`.)
2. **ew Tier 0:** add a `Dc21143Tulip` named PCI model (mirror the
   `Cy82C693Ide` pattern: owns config space + a CSR0-15 register file
   from `dc287_def.h` reset/mask tables + a 128-byte microwire SROM with
   MAC and a TP media entry). Flip `ds10_platform.json` `de500_tulip`
   from `"generic"` to the new model. Target: `ewa0` in `show device`
   with the SROM MAC, and the 0xFFFF0000 CSR9 chatter gone.
3. **ew Tier 1+:** SIA/MII link-up, then rings — only when network boot
   is on the roadmap.

Cross-refs: CLAUDE.md "PCI device enumeration + on-board device models"
(the 0xFFFF0000 UNHANDLED OUTER WRITE root cause = this NIC's CSR9 SROM
bit-bang); `dc287_def.h` for the exact CSR reset/mask values; the
21143/DC287 HRM in `Processor Support` for SROM media-table format.
