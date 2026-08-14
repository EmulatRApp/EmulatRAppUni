# SOURCE MATERIAL: Creating Bootable System Disks, Data Disks, and the
# platform.json Manifest (for H&M topic authoring)

Audience note for the authoring session: this file is the verified source
material for one or more H&M topics aimed at BETA USERS.  Every command and
field below was checked against the tree on 2026-08-14 (v5-tb, post
CVTGD-fix b53c0b4).  Style: step-by-step, bash-formatted commands (project
convention), strict-JSON examples (no // comments -- use "comment" fields).
Authoritative in-tree references: config/disk_types.json (storage SSOT,
schema 2), tools/mkdisk.py, Debug/ds20_v7_3_platform.json (annotated
exemplar), docs/release_notes/20260813_dcl_era_b2_reproducer_and_mechanism.md
(Ready-for-Testing list + migration guidance).

---

## 1. The storage model in one paragraph

EmulatR enumerates EXACTLY the storage devices declared in the platform
manifest (`<stem>_platform.json`, e.g. `ds20_v7_3_platform.json`) -- a
storage row MEANS the device exists on the bus; an undeclared SCSI id
selection-times-out, and a row with empty `media` does not enumerate.
Disk image files live in the run directory's `vStorage/` tree (manifest
`media` paths are relative to it, e.g. `"Alpha/dka0.vdisk"`).  Drive
identity/geometry comes from the drive `model` field, resolved against
the C++ DriveProfile table mirrored in `config/disk_types.json` (the
storage SSOT -- schema 2, authority+fidelity per field).

## 2. Creating a disk image (data disk or blank system disk)

Use `tools/mkdisk.py`.  `--type` is REQUIRED (a key from
`config/disk_types.json`); the SIZE IS DERIVED from the type -- you never
pass a size.  Withdrawn drive types are rejected with their reinstatement
evidence.

```bash
# a 512 MB scratch/data disk (synthetic profile, exact 1,048,576 blocks)
python tools/mkdisk.py --type EMULATR-512M vStorage/Alpha/dka1.img

# a full-size system-disk candidate (RZ29L: 8,380,080 blocks ~= 4.0 GB,
# geometry 113/20/3708, DEVDEPEND 0x0E7C1471 -- the CONFIRMED profile)
python tools/mkdisk.py --type RZ29L vStorage/Alpha/mysystem.vdisk
```

Available drive types (from disk_types.json; C++ table authoritative):
RZ28L, RZ29L (default/recommended system disk), RZ40, EMULATR-512M
(synthetic 512 MB).  Custom drives can be added under `custom_drives`
ONLY with a measured capture from a NON-EmulatR host (F$GETDVI
DEVTYPE/DEVCLASS/SECTORS/TRACKS/CYLINDERS/MAXBLOCK/DEVDEPEND + SHOW
DEVICE/FULL); a capture taken from EmulatR itself is circular and is not
an authority.

## 3. Declaring the disk -- THE PLATFORM EDITOR (primary workflow)

Beta users should NOT hand-edit the manifest.  EmulatR ships the
**Platform Editor** (`editors/platedit_qt.exe` beside the emulator in the
run directory; a TUI variant `platedit_tui` exists for terminal use).
It is a schema-validated manifest authoring tool with the drive catalog
built in, and its JSON I/O is ordered/format-preserving -- comments and
field order in the manifest survive an edit round-trip.

Editor workflow for the topic:

1. Launch `editors\platedit_qt.exe` from the run directory.
2. Open the platform manifest (`firmware/<stem>_platform.json`, e.g.
   `ds20_v7_3_platform.json`).
3. Navigate to the storage controller (SCSI HBA `pka_53c810` or IDE
   `cypress_ide`), add/edit a storage row; the drive `model` field
   offers the catalog's profiles (RZ29L, EMULATR-512M, ...), and
   validation flags illegal rows (unit 7, duplicate ids, missing media)
   before they ever reach a boot.
4. Save; launch EmulatR normally -- the new device enumerates at `>>>`.

(H&M authoring note: screenshot opportunities are the row editor and a
validation error.  The schema/ and catalog/ directories beside the
editor are its data; users do not edit those.)

## 3-REF. The manifest by hand (reference for the same topic's appendix)

The manifest is strict JSON.  Storage rows live under the controller's
`"storage"` array inside `"pci_devices"`.

### 3a. SCSI disks (the NCR 53C810 HBA -> console pka0, disks dka*)

Row fields:

| Field | Meaning |
|---|---|
| `channel` | SCSI channel, 0 on the 53C810 |
| `unit` | SCSI target id 0..6.  **id 7 is the HBA itself -- never declare it** |
| `lun` | 0 |
| `type` | `scsi_disk` |
| `model` | drive profile key (`RZ29L`, `EMULATR-512M`, ...) |
| `media` | image path relative to vStorage/, e.g. `"Alpha/dka1.img"` |
| `media_kind` | `image` |

SRM unit naming: `dka<id*100+lun>` -- id 0 = dka0, id 1 = dka100,
id 2 = dka200 ... (so "dka600" is SCSI id 6, not six hundred disks).

Example -- adding a data disk at SCSI id 2:

```json
{ "channel": 0, "unit": 2, "lun": 0, "type": "scsi_disk",
  "model": "EMULATR-512M", "media": "Alpha/mydata.img",
  "media_kind": "image",
  "comment": "SCSI id 2 -> dka200: project data disk." }
```

### 3b. IDE devices (Cypress 82C693 func1 -> console dqa*)

Same row shape under the `cypress_ide` device.  Console naming uses the
real-hardware 100*func+slot convention: slot 5 func 1 -> `105`, so
devices are `dqa<unit>.<unit>.<chan>.105.0`.  The CD-ROM is an IDE row:

```json
{ "channel": 0, "unit": 1, "lun": 0, "type": "atapi_cdrom",
  "model": "EMULATR VIRTUAL CDROM", "media": "Alpha/OpenVMS_v82.iso",
  "media_kind": "iso",
  "comment": "primary slave = dqa1, ATAPI CD.  media empty = no disc." }
```

Only IDE channel 0 is declared today (console reports dqa only; dqb is a
known gap).  DS10 is IDE-only (no SCSI rows).

### 3c. What NOT to touch

`iic_devices` (platform badge/FRU identity -- get_sysvar discriminators),
`pci_irq_table_hose0` (board interrupt routing, verbatim from console
sources), `vendor`/`device`/`class_code`/`bars` on existing devices.
These are board data, not user configuration.  Devices' interrupt pins
are silicon constants owned by the device models -- never add an
`interrupt_pin` field.

## 4. Making a system disk bootable

**Firmware note (copyright posture, MUST appear in the topic):** EmulatR
does not distribute DEC/HP SRM firmware images.  The installer creates
`firmware\` empty; the user places their platform's SRM console image
(e.g. `ds20_v7_3.exe`) there themselves.  The launcher's preflight
reports a missing firmware image with guidance rather than failing
silently.


Two supported routes:

### Route A -- migrate an existing disk (Charon / vtAlpha / real Alpha)

1. Copy the disk image file into `vStorage/Alpha/`.
2. Point the SCSI id-0 row's `media` at it (`model` should match the
   nearest profile; RZ29L for ~4 GB images).
3. Boot: at `>>>`: `b dka0`.
4. First-boot verification per the 8/13 release note: check
   `SHOW LOGICAL SYS$TIMEZONE_DIFFERENTIAL` matches the site's real
   offset (imported state travels with the disk).
5. NEVER attach the same image file to two emulators at once
   (single-writer; corruption otherwise).

### Route B -- fresh install from CD

1. Create a blank RZ29L image (Sec 2) and declare it at SCSI id 0.
2. Declare the OpenVMS distribution ISO on the IDE CD row (3b).
3. `>>> b dqa1` and follow the standard OpenVMS installation
   (BACKUP/IMAGE restore or full install) onto dka0.
4. Subsequent boots: `>>> b dka0`.

### Useful console/boot facts for the topic

- `>>> show dev` lists enumerated devices; the SRM prints the drive
  model's first token (e.g. "RZ29L-AA").
- Conversational boot (parameter dialog): `b -fl 0,1 dka0` -> `SYSBOOT>`;
  `SET/STARTUP SYS$SYSTEM:STARTUP.COM`, `SET STARTUP_P1 ""` restore
  normal automatic startup; `C` continues.
- Silicon memory ceilings are enforced per platform (DS10 2G, DS20 4G,
  ES40 32G ...); the launcher clamps `MEM` requests.
- Launch via `tools/emulatr_launch.sh <platform>` (fixes firmware +
  memory + model together; PuTTY console auto-raises).

## 5. Data disks inside VMS

After declaring + booting:

```
$ INITIALIZE DKA200: MYDATA
$ MOUNT/SYSTEM DKA200: MYDATA
```

(plus MOUNT in SYSTARTUP_VMS.COM for persistence across boots --
standard VMS administration, cite VMS docs rather than re-teach.)

## 6. Known caveats to carry into the topic (beta honesty)

- Reboot-after-halt (`Ctrl/P` -> `>>>` -> `b`) currently faults the
  second boot deterministically (kernel-stack-not-valid; JRN-KSNV-001,
  open).  Beta guidance: exit EmulatR and relaunch for a fresh boot
  instead of warm rebooting.
- The IDE secondary channel (dqb) is not yet declared.
- Tagged queuing / reselection on the SCSI bus are Phase C work; the
  target set presented is command-queue-free by design.
- disk_types.json geometry sanity is a +/-2% band, never equality (ZBR).
