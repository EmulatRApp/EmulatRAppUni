# Platform Interface Contract & Latch Plan — `<model>_platform.json`

**Date:** 2026-06-24
**Trigger:** DS20 firmware (`ds20_v7_3.exe`) reaching `P00>>>` but badging as
**“AlphaPC 264DP 100 MHz”** instead of **“AlphaServer DS20”**, despite the
OCP fixes in `ds20_v7_3_platform.json`.
**Purpose:** Document, end-to-end, the interfaces the guest SRM firmware and
EmulatR meet on; which of them the per-model manifest actually influences;
what the firmware expects of each; and a corrective-action plan to *latch*
the platform deterministically.

---

## 0. TL;DR — platform identity rides on THREE independent channels

EmulatR never stores “the current platform.” Identity is **emergent** from the
device population the firmware probes. Three channels feed that population, and
**they are driven by three different inputs that must agree** — today nothing
enforces agreement:

| Channel | What it sets | Driven by | Manifest-driven? |
|---|---|---|---|
| **A. Chipset variant + IIC decode base** | `TsunamiVariant` (DREV/MISC.REV) and the per-model IIC dense-mem base (`kIicBaseByModel`: DS10=`0xFFFF0000`, DS20/DS20E=`0xFFF80000`) | ini `[System] model` → `Machine` ctor `m_chipset(model,…)` | **No** — hardcoded model table |
| **B. IIC device population** (OCP discriminator + FRU EEPROMs) | the IIC nodes that ACK on the bus — **this is what the firmware probes to badge the box** | `<firmware-stem>_platform.json` → `configureDevices()` | **Yes** |
| **C. HWRPB system identity** | `system_type` / `system_variation` handed to the OS | hardcoded `DEC_TSUNAMI` / `0` | **No** — model-blind |

The **banner is decided purely by Channel B** (`pc264.c get_sysvar()` probes the
IIC OCP). But Channel B only works if **Channel A mapped the IIC controller at
the base the DS20 firmware writes to** (`0xFFF80000`). If the model string isn’t
`DS20`, or the manifest didn’t load, the OCP never ACKs → firmware falls back to
**SYSVAR member 1 = “AlphaPC 264DP.”**

---

## 1. The identity chain (firmware side — authoritative)

Reference: apisrm `pc264.c` (`get_sysvar()` ~L636–664; `build_dsrdb()` L549–634),
`hwrpb.c` L405–428, corroborated by `7.3 Notes/SMMTABLE.TXT` (SYSTYPE 34 /
member 6 = “AlphaServer DS20” for `DS20_V7_3.EXE`).

```
get_sysvar():
    sysvar = HWRPB$_SYSVAR                       # static base, SYSTYPE 34
    if   fopen("iic_ocp0",   "sr+")  -> sysvar |= 6 << 10   # member 6 = AlphaServer DS20
    elif fopen("iic_8574_ocp","sr+") -> sysvar |= 8 << 10   # member 8 = DS20E / Goldrack
    else                              -> sysvar |= 1 << 10   # member 1 = AlphaPC 264DP (DEFAULT)
hwrpb.c:414 (#if APC_PLATFORM): hwrpb->SYSVAR := get_sysvar()   # OVERWRITES static
build_dsrdb(): switch (hwrpb->SYSVAR >> 10)
    case 1 -> "AlphaPC 264DP %3d MHz"
    case 6 -> "AlphaServer DS20 %3d MHz"   (single/dual via dualCPU())
    case 8 -> DS20E / XP2000 family
    default-> "Error determining system type…"; default to 264DP
```

`fopen("iic_ocp0")` **succeeds iff** an IIC device at **node 0x40** ACKed a
1-byte read during `iic_init` (see §2). The banner does **not** depend on any
static HWRPB field on PC264 — it is the live IIC-probe result. Probe order:
**0x40 first, then 0x4E, else default.** Do **not** ACK at 0x4E (forces DS20E).

---

## 2. Interface-by-interface contract

Legend — **Mfst?** = is it configured by `<model>_platform.json`?
Host citations: `D:\EmulatR\EmulatRAppUniV4\Emulatr\`.
Firmware citations: apisrm `ref/` (PC264 build).

### 2.1 IIC / SMBus bus — **the identity surface**  (Mfst: YES)
- **EmulatR:** `deviceLib/Tsunami/IicPcf8584.h` — `configureDevices()`,
  `deviceLookup()`, `ioRead()/ioWrite()`, `Kind{FruEeprom,Nvram,Status}`.
  Mapped into PCI dense-mem at the **per-model base** by
  `chipsetLib/TsunamiChipset.h::wireDevices()` via
  `m_pchip.registerPciMemRange(base, base+2, &m_iic)`, where `base` =
  `kIicBaseByModel[]` (TsunamiChipset.h:647 — **DS10 0xFFFF0000, DS20 0xFFF80000**;
  unknown model = hard stop).
- **Manifest → state:** `iic_devices[]` → `IicDeviceEntry` → `Machine.cpp:474–493`
  builds an `IicPcf8584::IicDevice` per entry and calls
  `m_chipset.iic().configureDevices(devs)`. **The decode BASE is NOT manifest-driven**
  (it comes from the model string, Channel A).
- **Firmware expects:** `iic_driver.c` `iic_init` (L1060–1117) issues a 1-byte read
  to every `test==1` node; the **inode is created only if the read returns
  status 1 (ACK).** `fopen` later succeeds ⇔ that ACK happened. LED/OCP nodes need
  only an address ACK — no content. PC264 node list (`iic_node_list[]` L233–241):
  `iic_smb0`@0xA2, `iic_cpu0`@0xA4, `iic_cpu1`@0xAC, **`iic_ocp0`@0x40**,
  **`iic_ocp1`@0x42**, `iic_8574_ocp`@0x4E, `iic_system0`@0x70, `iic_system1`@0x72,
  `iic_rcm_nvram0..7`@0xC0–0xCE.

### 2.2 OCP / front-panel LED  (Mfst: YES — via IIC entries 0x40/0x42)
- **EmulatR:** modeled as two `class:"status"` IIC devices (0x40 control, 0x42 data)
  in the manifest; served by IicPcf8584 as `Kind::Status` (returns `image[0]=byte`).
- **Firmware expects:** `sable_ocp_driver.c` `sable_ocp_init()` (PC264 branch L128–150)
  opens **both** `iic_ocp0`@0x40 and `iic_ocp1`@0x42. PC264 semantics:
  - `iic_ocp0` open fails → `return(msg_success)` — **missing OCP is non-fatal** (but → 264DP badge).
  - `iic_ocp0` ok but `iic_ocp1` fails → prints `"Device Open Error: IIC_OCP1"`, `msg_failure` (non-fatal to boot).
  - both ok → bit-bangs LCD init + writes `"Console  Started"`.
- **Requirement to badge DS20:** IicPcf8584 must **ACK a 1-byte read at 0x40 and 0x42**.

### 2.3 FRU EEPROMs (SMB 0xA2, CPU 0xA4)  (Mfst: YES)
- **EmulatR:** `systemLib/PlatformConfig.cpp::synthesizeFruImage()` (L537–578) builds a
  256-byte JEDEC image from the `fru_eeprom` entry fields
  (`manufacturer`,`model`,`part_class`,`serial`,`revision_ro/rw`); served as `Kind::FruEeprom`.
- **Firmware expects:** `jedec_def.sdl` layout + `build_fru.c` (`jedec_checksum`, L1179–1194):
  256 bytes with three segment checksums at **0x3F/0x7F/0xFF** (unsigned byte sum,
  inclusive, over 0x00–0x3E / 0x40–0x7E / 0x80–0xFE). `show fru` sets error bits
  (bit3 unreadable, bit4–6 checksum fail, bit7 serial mismatch) but **does not halt boot**.
- **Status:** independent of the badge; keep checksums self-consistent for a clean `show fru`.

### 2.4 NVRAM / Flash ROM  (Mfst: NO)
- **EmulatR:** `chipsetLib/FlashRom.h/.cpp` — 2 MB Am29F016 on the TIG bus
  (`loadRaw`, `read/write`, debounced `tryFlush/forceFlush`); `m_chipset.flash()`.
- **Driven by:** `EMULATR_FLASH_ROM` env, else **hardcoded default `ds10_flash.rom`
  for ALL models** (`Machine.cpp:429–432`). **Gotcha:** a DS20 boot reads DS10’s
  persisted env/FRU unless `EMULATR_FLASH_ROM=ds20_flash.rom` (the source of the
  startup `memtest:` line — a stored `nvram` script from stale/wrong NVRAM).
- **Firmware expects:** holds the SRM env + `nvram` startup script; factory-fresh =
  0xFF backing (delete the file).

### 2.5 PCI config space + on-board NIC (DE500 / 21143)  (Mfst: parsed, **NOT wired**)
- **EmulatR:** `chipsetLib/TsunamiPchip.h` `readPciConfig0/writePciConfig0` dispatch
  to behavioral device classes registered by `wireDevices()` (south bridge @5.0,
  IDE @5.1). Absent device → `0xFFFFFFFF`; unmatched offset → `TsunamiPchip::write/read`
  **UNHANDLED OUTER WRITE/READ** fallthrough.
- **Manifest gap:** `pci_devices[]` (vendor/device/class/BARs) is parsed and
  `synthesizePciConfig()` exists (`PlatformConfig.cpp:580`) **but is referenced ONLY by
  `tests/systemLib/test_platform_config.cpp`** — there is **no production
  `ManifestPciDevice` registered into the Pchip.** So manifest PCI identity never
  reaches config space.
- **Firmware expects:** `dc287_def.h` — a 21143 answering config with a valid BAR;
  it then bit-bangs CSR9 (`BAR+0x48`) to read the MAC from the tulip SROM. With no
  enumeration the firmware reads an all-ones BAR → base `0xFFFF0000` (matches the
  `0xFFFF0000` reset masks in `dc287_def.h`) → pokes `0x800_FFFF_00xx` → the observed
  `UNHANDLED OUTER WRITE`. **Non-fatal today.**

### 2.6 Storage / IDE (Cypress CY82C693 func-1)  (Mfst: YES — media targets)
- **EmulatR:** `deviceLib/Tsunami/Cy82C693Ide.h` (ATA/ATAPI), `deviceLib/scsi/*`
  (`makeBlockMedia`, `FileBlockMedia`); attach seam `TsunamiChipset::setDiskMedia/setCdMedia`.
- **Manifest → state:** `pci_devices[].storage[]` (`channel/unit/type/media/media_kind/
  enabled/create_if_missing/size`) → `StorageTarget` → `Machine.cpp:509–565` resolves
  media vs `[Storage] diskDir` and attaches (dqa0 = primary master disk, dqa1 = ATAPI CD).
  The IDE controller itself is wired unconditionally (not manifest-gated).

### 2.7 TIG — halt / SMIR / CPU-START  (Mfst: NO)
- **EmulatR:** `chipsetLib/TsunamiTig.h` — `kSmir` (TIG+0x40) reads **0 = “no halt”**
  (the SRM boot gate); per-CPU CPU-START kick regs (`m_cpuStartReq`); halt-IPI regs.
  Hardcoded PAs/values; `EMULATR_TIG_TRACE` gates the canary.

### 2.8 Cchip / Dchip / Pchip CSRs + interrupts  (Mfst: NO)
- **EmulatR:** `chipsetLib/TsunamiCchip.h` (MISC, DIM/DIR/DRIR, IPI via MISC.IPREQ),
  `TsunamiDchip.h` (DREV/DSC), `CchipIntervalTimer.h` (interval clock → MISC.ITINTR),
  `Pic8259Pair.h` (ISA 8259 pair → Cchip DRIR<55>). Revisions from `TsunamiVariant.h`,
  selected by the **model string** (Channel A), not the manifest.

### 2.9 Console UART  (Mfst: NO)
- **EmulatR:** `deviceLib/Tsunami/Uart16550.h` (COM1 @0x3F8) → backend
  `deviceLib/SRMConsoleDevice.{h,cpp}` (QTcpServer). Port from `EmulatorSettings.srmConsole`
  (ini), default **10023**, `EMULATR_CONSOLE_PORT` override, `EMULATR_NO_PUTTY` suppresses launch.

### 2.10 HWRPB  (Mfst: NO — model-blind)
- **EmulatR:** `deviceLib/HwrpbBuilder.cpp::populateHwrpb()`, spec from
  `deviceLib/FirmwareDeviceManager.h::buildHwrpb()` (~L520–599). **`system_type =
  DEC_TSUNAMI`, `system_variation = 0`, `system_revision = 0` are hardcoded for every
  model** (FirmwareDeviceManager.h:553–555). Only `cpuCount` and `memSize` (from ini) vary.
- **Note:** harmless for the PC264 *banner* (overwritten by `get_sysvar`), but the
  OS-facing HWRPB never learns DS10 vs DS20 — a latent correctness gap.

---

## 3. Why DS20 badges as 264DP — ranked failure modes

1. **Manifest didn’t load → DS10 default bus (no OCP).** If `ds20_v7_3_platform.json`
   isn’t **next to the executable** (`applicationDirPath()`, not CWD — `Machine.cpp:464`),
   or fails validate, `PlatformConfig::load` returns `defaultDs10Manifest()`
   (`usedDefault=true`) which has **no 0x40/0x42** (`PlatformConfig.cpp:438–442`).
   → 0x40 never ACKs → member 1 → 264DP. **Decisive log check:**
   `PlatformConfig: manifest '…ds20_v7_3_platform.json' unusable (…); using built-in default DS10 bus`.
2. **IIC base mismatch (Channel A ≠ B).** The DS20 firmware writes IIC CSRs at
   `0xFFF80000`; `kIicBaseByModel` only maps the controller there when the chipset
   model is `DS20`. If `[System] model` in the ini isn’t `DS20` (run_fw.sh sets it via
   `sed`, but a stale/edited ini can drift), the controller sits at the DS10 base
   `0xFFFF0000` and the DS20 firmware’s probes miss it → no ACK → 264DP.
3. **OCP present but doesn’t ACK the 1-byte read.** Even with 0x40/0x42 in the loaded
   manifest and the correct base, IicPcf8584 must return “1 byte transferred” for the
   `iic_init` read of a `Status` device. If the status read path doesn’t ACK, `fopen`
   fails → 264DP.

> First action is simply to read the boot log for the §3.1 warning and to confirm
> `config/EmulatrV4.ini` shows `model = DS20`. Those two checks discriminate modes 1/2
> in one minute.

---

## 4. Corrective-action plan (latch the platform, make it self-checking)

Ordered by value / lowest-divergence first.

**P0 — Deploy the manifest next to the exe.** Add `<model>_platform.json` (all of
`ds10/ds20/ds25/es40/es45`) to the CMake **POST_BUILD** copy set (alongside
`run_fw.sh` / `launch_vms_boot.sh`) and have `run_fw.sh` copy the *firmware-stem*
manifest into the build dir next to `Emulatr.exe`. Removes the silent DS10 fallback
entirely. *(Files: root `CMakeLists.txt` POST_BUILD section; `run_fw.sh`.)*

**P1 — Single source of truth for “model” + ctor agreement assert.** Today Channel A
(ini `[System] model`) and Channel B (firmware-stem manifest) are independent. In the
`Machine` ctor, after load, **assert `manifest.platform` is consistent with
`m_settings.system.model`** (e.g. both `DS20`); on mismatch emit a hard `spdlog::error`
(or refuse to launch) instead of silently booting a hybrid. This is the literal “latch.”
*(Files: `systemLib/Machine.cpp` ctor; `systemLib/PlatformConfig.cpp::validate`.)*

**P2 — “platform latched” boot line.** At the end of the ctor, log one deterministic line:
`platform latched: model=DS20 manifest=DS20 usedDefault=0 iic_base=0xFFF80000 iic_acks=[0x40 0x42 0x70 0x72 0xA2 0xA4 0xC0]`.
Presence of `0x40`+`0x42` is a perfect, greppable predictor of the banner and a
regression canary. *(File: `systemLib/Machine.cpp`.)*

**P3 — Harden `validate()` for the discriminator.** When `platform=="DS20"`, require the
**OCP pair 0x40+0x42 present and 0x4E absent**; warn/fail otherwise. Stops a future DS20
manifest from silently degrading to 264DP. *(File: `systemLib/PlatformConfig.cpp::validate`.)*

**P4 — Unit-test the OCP ACK path.** Add a doctest: a `Status` device at 0x40 returns
“1 byte transferred” for the `iic_init`-style 1-byte read (mirroring `iic_rw_common`),
so `fopen(iic_ocp0)` would succeed. Locks the firmware contract in CI.
*(File: `tests/…/test_iic*` / `test_platform_config.cpp`.)*

**P5 — Wire `synthesizePciConfig()` into the Pchip (or a config stub).** Register manifest
`pci_devices[]` as config-answering devices so on-board parts (DE500/21143) enumerate with
a sane BAR. A passive stub that answers config + absorbs CSR9 writes silences the
`0xFFFF0000` UNHANDLED writes short-term; a full tulip model reads the MAC. Larger lift,
**non-fatal**, lower priority than the badge. *(Files: `systemLib/Machine.cpp`,
`chipsetLib/TsunamiPchip.h`, `systemLib/PlatformConfig.cpp::synthesizePciConfig`.)*

**P6 — Make HWRPB model-aware.** Feed `system_variation` (and any per-model `system_revision`)
from the model instead of hardcoded `0`, so the OS-facing HWRPB matches the badge. Latent
correctness; no current boot impact. *(File: `deviceLib/FirmwareDeviceManager.h:553–555`.)*

---

## 5. Reference index (firmware sources)

apisrm `ref/` (PC264 build): `pc264.c` (`get_sysvar` ~L636–664; `build_dsrdb` L549–634),
`hwrpb.c` (L405–428), `iic_driver.c` (`iic_node_list[]` L233–241; `iic_init` L1060–1117),
`sable_ocp_driver.c` (`sable_ocp_init` L107–175), `jedec_def.sdl`, `build_fru.c`
(`jedec_checksum` L1179–1194), `dc287_def.h` (21143 CSRs; `0xFFFF0000` reset masks L147–168).
Corroboration: `Processor Support/7.3 Notes/SMMTABLE.TXT` (SYSTYPE 34 / member 6 = AlphaServer DS20).

EmulatR host: `systemLib/PlatformConfig.{h,cpp}`, `systemLib/Machine.cpp` (resolution L445–470;
consumption L474–566), `deviceLib/Tsunami/IicPcf8584.h`, `chipsetLib/TsunamiChipset.h`
(`kIicBaseByModel` L647), `chipsetLib/TsunamiPchip.h`, `deviceLib/FirmwareDeviceManager.h`.
