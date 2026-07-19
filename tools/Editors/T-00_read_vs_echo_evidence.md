# T-00 -- Read-vs-Echo Evidence Pass

    Doc id   : SPEC-PLATED-001 / T-00 artifact
    Status   : EVIDENCE (no opinions, no design -- only what the parser reads)
    Date     : 2026-07-17
    Source   : ~/documents/emulatr/emulatrappuniv4/emulatr (git repo)
    Parser   : systemLib/PlatformConfig.{h,cpp}
    Consumer : systemLib/Machine.cpp (manifest -> device bus)
    Settings : config/EmulatorSettings.h + config/EmulatrV4.ini (second surface)

This is the evidence gate the spec (Section 6.1, Ticket T-00) requires before
Section 6.4 is written. It records only what the code does. Every claim carries
a `file:line` citation. Design consequences live in the spec, not here.

---

## 0. Method

    grep -rIl 'iic_devices|pci_devices|manifest_version' --include='*.cpp' --include='*.h'
      -> systemLib/PlatformConfig.{h,cpp}   (sole manifest parser)

    grep 'PlatformConfig::load'             -> systemLib/Machine.cpp:470  (sole consumer)
    grep 'synthesizePciConfig'              -> defined PlatformConfig.cpp:595; CALLED NOWHERE
    grep 'synthesizeFruImage'               -> called Machine.cpp:480

The manifest is parsed by Qt's `QJsonDocument` (`PlatformConfig.cpp:249`). Unknown
keys are ignored (`PlatformConfig.cpp:21-22`); a `comment` field is never read at
any level. Parse is best-effort per device; structural rules run in a separate
`validate()` pass (`PlatformConfig.cpp:324`).

---

## 1. Two configuration surfaces

The device manifest is not the only configurable input.

| Surface           | Source file             | Schema / loader                          | Governs                                                                 |
|-------------------|-------------------------|------------------------------------------|------------------------------------------------------------------------|
| Runtime settings  | `config/EmulatrV4.ini`  | `config/EmulatorSettings.h` (`IniLoader`)| `model`, `cpuCount`, `activeCpus`, `memorySize`, firmware image + SHA, flash image, trace mask/files, snapshot, logging levels, SRM console port / PuTTY, `[Storage] diskDir` |
| Device manifest   | `<stem>_platform.json`  | `systemLib/PlatformConfig.h`             | IIC bus, PCI topology, BARs, storage targets                           |

Couplings between the two (evidence):

  - `[System] model` is the master switch: it selects `<lower(model)>_platform.json`
    and the default firmware, and sets system identity
    (`EmulatorSettings.h:49`; `EmulatrV4.ini [System]`).
  - **Model/platform latch.** `Machine.cpp:499-517` upper-cases `ini [System] model`
    and manifest `platform` and logs `PLATFORM MISMATCH` at **error** level when
    they differ (currently warn-loud, does not refuse to launch).
  - **Media resolution.** `storage[*].media` is a bare filename resolved against
    `[Storage] diskDir` (`EmulatorSettings.h:122-130`). The manifest carries no host
    path; `diskDir` lives only in the INI.

## 2. Manifest path resolution (how a file becomes the running bus)

`Machine.cpp:445-470`:

  1. env `EMULATR_PLATFORM_CONFIG`, if set, is the path verbatim; else
  2. `<firmware-stem>_platform.json` next to the executable
     (`firmware/ds20_v7_3.exe -> ds20_v7_3_platform.json`); else
  3. empty path -> `PlatformConfig::load` falls back to the compiled-in default
     DS10 manifest (`PlatformConfig.cpp:432`, `usedDefault=true`).

`manifest_version` must equal `kSupportedVersion == 1` **exactly**
(`PlatformConfig.cpp:192, 266-270`). Any other value (newer *or* older) is a hard
failure -> fallback to the default DS10 manifest. There is no migration path.

**Any hard validation error discards the entire manifest** and boots the default
DS10 bus (`PlatformConfig.cpp:290-295`, `fallback()` at `:227`). Validation is not
per-field; one hard error loses the whole file.

---

## 3. Consumption status (what is wired TODAY)

| Array          | Parsed | Validated | Consumed at runtime                                             |
|----------------|--------|-----------|----------------------------------------------------------------|
| `iic_devices`  | yes    | yes       | **YES** -- `Machine.cpp:476-492` builds the IIC device list; `synthesizeFruImage` (`:480`) generates the on-wire EEPROM image; status byte copied (`:489`) |
| `pci_devices`  | yes    | yes       | **NOT YET** -- `synthesizePciConfig` (`PlatformConfig.cpp:595`) is called nowhere outside its own file; no PCI attach exists. The PCI consumer ("P4") is unlanded. |

The header states the structs "intentionally carry only the high-level fields at
this stage" and names PCI config synthesis as the intended path
(`PlatformConfig.h:26-38, 216-237`). So PCI identity fields are *read and
validated inputs whose consumer is not wired yet* -- not fields the device class
owns. This distinction is the crux of Section 6.1 and is left to the spec.

---

## 4. Keys read -- IIC device (`parseIicDevice`, `PlatformConfig.cpp:127-156`)

| Key            | Read as / line                              | Required?                      | Consumed for                          |
|----------------|---------------------------------------------|--------------------------------|---------------------------------------|
| `name`         | string, `:130`                              | no                             | informational only (validate tag / log; `Machine.cpp` never reads it) |
| `address`      | hex-or-dec, `:133` (`jsonHex :61`)          | yes -- issue if absent `:136`  | IIC node address (all classes)        |
| `class`        | enum, `:140` (`iicClassFromString :68-74`)  | yes -- issue if unknown `:142` | selects synthesis path                |
| `manufacturer` | string, `:146`                              | FruEeprom only (validate `:347`)| JEDEC EEPROM image                    |
| `model`        | string, `:147`                              | FruEeprom only (validate `:348`)| JEDEC EEPROM image                    |
| `part_class`   | string, `:148`                              | no                             | JEDEC EEPROM image                    |
| `serial`       | string, `:149`                              | no                             | JEDEC EEPROM image (6-bit packed)     |
| `revision_ro`  | hex byte, `:152`                            | no                             | JEDEC EEPROM image                    |
| `revision_rw`  | hex byte, `:153`                            | no                             | JEDEC EEPROM image                    |
| `size`         | int, `:154`                                 | Nvram: must be >0 (validate `:351`)| NVRAM byte count                   |
| `byte`         | hex, `:155`                                 | no                             | Status/Led register read value        |
| `comment`      | -- not read --                              | --                             | documentation only                    |

`class` enum values (`:70-74`): `fru_eeprom`, `nvram`, `status`, `led`.

## 5. Keys read -- PCI device (`parsePciDevice`, `PlatformConfig.cpp:158-224`)

| Key             | Read as / line                                | Required?                     | Consumed for                                  |
|-----------------|-----------------------------------------------|-------------------------------|-----------------------------------------------|
| `name`          | string, `:161`                                | no                            | informational (validate tag / log)            |
| `model`         | `:163` (`pciModelFromString :77-84`)          | yes -- issue if empty `:165`  | backing model: `generic`/`passive`/*named*    |
| `hose`          | int, `:171`                                   | no (default 0)                | BDF                                           |
| `bus`           | int, `:172`                                   | no (default 0)                | BDF                                           |
| `slot`          | int, `:173`                                   | no (default 0)                | BDF (PCI device number)                       |
| `func`          | int, `:174`                                   | no (default 0)                | BDF                                           |
| `vendor`        | hex, `:177`                                   | yes -- issue if absent `:178`; validate rejects 0x0000/0xFFFF `:366` | PCI config header (`synthesizePciConfig :600`) |
| `device`        | hex, `:179`                                   | yes -- issue if absent `:180` | PCI config header (`:601`)                     |
| `class_code`    | hex, `:181`                                   | no                            | PCI config header (`:604-606`)                 |
| `revision`      | hex, `:182`                                   | no                            | PCI config header (`:603`)                     |
| `subsys_vendor` | hex, `:183`                                   | no                            | PCI config header (`:608`)                     |
| `subsys_id`     | hex, `:184`                                   | no                            | PCI config header (`:609`)                     |
| `option_rom`    | bool, `:186`                                  | no (default false)            | expansion-ROM BAR presence                    |
| `interrupt_pin` | int, `:187`                                   | no (default 0)                | PCI config header offset 0x3D (`:611`)         |
| `bars[*]`       | array, `:189-199`                             | no                            | BAR sizing masks (`:613-637`)                  |
| `storage[*]`    | array, `:203-223`                             | no                            | storage targets behind a named controller     |
| `comment`       | -- not read --                                | --                            | documentation only                            |

`model` values (`:77-84`): `generic` -> Generic; `passive` -> Passive; **any other
non-empty string** -> Named (`modelName` = that string). It is not a closed set,
and there is **no `device_catalog.json` anywhere in the emulator** -- identity is
carried explicitly in the manifest, not derived from `model`.

### 5a. BAR sub-object (`:189-199`)

| Key        | Read as / line                     | Consumed for                     |
|------------|------------------------------------|----------------------------------|
| `index`    | int, `:193`                        | BAR slot (validate: <=5, unique) |
| `kind`     | `"mem"`? mem : io, `:194`          | mem-vs-IO type bits              |
| `size`     | hex, `:196`                        | size-probe readback mask (`:632`)|
| `prefetch` | bool, `:197`                       | prefetch bit (memory BARs)       |

### 5b. Storage target sub-object (`:203-223`)

| Key                | Read as / line                              | Consumed for                                     |
|--------------------|---------------------------------------------|--------------------------------------------------|
| `channel`          | int, `:207`                                 | IDE channel (validate: <=1)                       |
| `unit`             | int, `:208`                                 | IDE unit (validate: <=1)                          |
| `lun`              | int, `:209`                                 | logical unit                                      |
| `type`             | enum, `:210` (`:86-91`)                      | `ata_disk` \| `atapi_cdrom`                        |
| `model`            | string, `:216`                              | INQUIRY/identify string (informational)           |
| `media`            | string, `:217`                              | image path / host selector (resolves vs `diskDir`)|
| `media_kind`       | string, `:218`                              | `image` \| `iso` \| `host` (absent -> file)        |
| `enabled`          | bool default true, `:219`                   | false -> target skipped (may share channel/unit)  |
| `create_if_missing`| bool default false, `:220`                  | auto-provision a blank writable disk              |
| `size`             | K/M/G/T-suffixed, `:221` (`parseSizeBytes :96`)| capacity for `create_if_missing`               |

Note: three distinct keys named `size` at three paths, three formats -- IIC
`size` (int, `:154`), BAR `size` (hex, `:196`), storage `size` (suffix-string,
`:221`).

---

## 6. Structural rules actually enforced (`validate`, `PlatformConfig.cpp:324-426`)

Severity is as coded. A **hard error** discards the whole manifest (Section 2);
a **warning** (`WARN:` prefix) does not.

| # | Severity | Rule                                                                       | Line   |
|---|----------|----------------------------------------------------------------------------|--------|
| 1 | error    | `manifest_version != 1`                                                     | `:331` |
| 2 | error    | IIC `address` is odd (`& 0x01`)                                             | `:341` |
| 3 | error    | duplicate IIC `address`                                                     | `:343` |
| 4 | error    | FruEeprom missing `manufacturer`                                           | `:347` |
| 5 | error    | FruEeprom missing `model`                                                  | `:348` |
| 6 | error    | Nvram `size == 0`                                                          | `:351` |
| 7 | error    | duplicate PCI BDF `(hose,bus,slot,func)`                                    | `:364` |
| 8 | error    | PCI `vendor == 0x0000` or `0xFFFF`                                          | `:366` |
| 9 | error    | `model` Named but `modelName` empty                                        | `:368` |
| 10| error    | BAR `index > 5`                                                            | `:372` |
| 11| error    | duplicate BAR `index` within a device                                      | `:373` |
| 12| warn     | device has `storage` but `model` is not a named controller                | `:377` |
| 13| error    | storage `channel > 1`                                                      | `:382` |
| 14| error    | storage `unit > 1`                                                         | `:383` |
| 15| error    | duplicate storage `(channel,unit)` among **enabled** targets              | `:386` |
| 16| error    | `create_if_missing` on non-`ata_disk`                                      | `:389` |
| 17| error    | `create_if_missing` with `size == 0`                                       | `:392` |
| 18| error    | `create_if_missing` `size` not a multiple of 512                          | `:394` |
| 19| warn     | DS10 missing IIC 0x70 / 0x72 / 0xA2                                        | `:405-407` |
| 20| warn     | DS20 missing IIC 0x40 / 0x42; present 0x4E (get_sysvar discriminator)      | `:420-422` |

Duplicate-storage key is `(channel,unit)`, **not** `(channel,unit,lun)`, and only
across *enabled* targets (`:381-386`).

## 7. Rules NOT enforced by the emulator

The parser does **not** check any of: duplicate `name` (any array); hex-string
format regex (garbage parses to 0 or a missing-field issue, `jsonHex :61`); BAR
`size` power-of-two; `media` filesystem resolvability; ASCII-128 codepoints; the
`_PROVISIONAL` comment convention. Any tool rule for these is a tool-only value
add, not a reflection of emulator behavior.

---

## 8. Compiled-in default manifest (`defaultDs10Manifest`, `:432-502`)

Reference board when the file is missing/invalid: 5 IIC devices (`iic_system0`
0x70, `iic_system1` 0x72, `iic_smb0` 0xA2 FRU, `iic_cpu0` 0xA4 FRU, `iic_rcm_nvram0`
0xC0); 3 PCI devices (`cypress_isa` 0:0:5.0 Named, `cypress_ide` 0:0:5.1 Named with
2 storage targets, `de500_tulip` 0:0:7.0 **Generic** with 2 BARs). PCI identity
values are marked `_PROVISIONAL` in-source (`:464, 472, 493`).
</content>
