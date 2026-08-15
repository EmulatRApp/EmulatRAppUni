# EmulatrLaunch -- Configuration Steps (H&M topic source material)

Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.
Date: 2026-08-14.  Describes EmulatrLaunch as of commit a7754fb (beta kit).

PURPOSE OF THIS FILE: source material for the Help & Manual authoring
session.  Facts are authoritative as of the date above; wording is free to
be adapted.  Suggested placement: a new "Configuring a System with the
Launcher" topic under 3. Quick Start or 4. Operating EmulatR, cross-linked
from "Installing with the Windows Installer".

---

## 1. Terminology the reader needs first

- **The product** is EmulatR.  The **launcher** (EmulatrLaunch.exe) and the
  **Platform Editor** (editors\platedit_qt.exe) are subsystems of it.
- **The installation** lives at
  `C:\Program Files\eNVy Systems, Inc\ASA-EmulatR` and is read-only.  It is
  replaced wholesale by kit upgrades.  Nothing the user customizes is stored
  there.
- **A named system** is the launcher's unit of configuration: one system =
  one platform (DS10/DS20/ES40, bound at creation) + one **profile
  directory** (the "run directory").  The profile directory holds
  everything that system owns: `Emulatr.ini`, the system's own copy of the
  platform manifest, `firmware\`, `disks\`, `logs\`, `traces\`.
- The launcher's catalog of systems lives in the per-user registry at
  `HKEY_CURRENT_USER\Software\eNVy Systems\EmulatR` (all launcher keys are
  prefixed `launch_`).  The registry only POINTS at profile directories; the
  substance is in the directories themselves.  Both survive kit
  uninstall/reinstall.

## 2. Configuration steps, in order

### Step 1 -- Install and start the launcher

Run the installer (see the "Installing with the Windows Installer" topic;
uninstall an existing kit first).  Start `EmulatrLaunch.exe` from the
installation directory.  No console window or shell is involved: the
launcher starts the emulator directly as a child process with an explicit
working directory and environment.

### Step 2 -- Create a named system

Systems tab -> **New System...**  Choose:

- **Name** -- any display name, unique case-insensitively.
- **Platform** -- DS10, DS20, or ES40.  Immutable after creation.
- **Profile (run) directory** -- anywhere the user owns, e.g. under
  Documents.  The launcher REFUSES locations under Program Files (Windows
  silently redirects writes there and the emulator's flash state would be
  lost).

On OK the launcher builds the profile:

| Created | Purpose |
|---|---|
| `Emulatr.ini` | seeded from a template with the model and console port filled in |
| `firmware\` + `firmware_readme.txt` | where the user's SRM image goes (never shipped) |
| `disks\` | default resolution root for relative disk-image names |
| `logs\`, `traces\` | run output |
| `<stem>_platform.json` (e.g. `ds20_v7_3_platform.json`) | the system's OWN copy of the platform manifest, copied from the installation and made writable |

The manifest copy at creation is the key design point: the profile is
self-contained from birth.  Editing it affects only this system, and kit
upgrades never touch it.  (Systems created by older launcher builds are
seeded on first "Open in PlatEd" instead.)

### Step 3 -- Supply the SRM firmware image

Copy the firmware image (e.g. `ds20_v7_3.exe`) into the profile's
`firmware\` folder.  EmulatR does not distribute firmware (licensing).
Back in the launcher, Details tab -> **Firmware -> Image** lists every file
found in `firmware\`; pick one.  A platform-mismatched image is flagged as
a warning, never refused.  The selection is written to `Emulatr.ini`
(`[ROM] firmware`), and the firmware's basename (the **stem**) names the
manifest the system uses: `ds20_v7_3.exe -> ds20_v7_3_platform.json`.

### Step 4 -- Configure devices and storage (the manifest)

Details tab -> **Devices -> Open in PlatEd** opens the system's manifest
copy (the note under the button shows the exact path and says
`(system-local)`).  The manifest declares the PCI devices and, under each
storage controller, the disk/CD rows.  The storage rows that matter to a
first boot, for DS20:

- `pka_53c810` (NCR SCSI HBA) rows: SCSI id 0 -> console device `dka0`,
  id 1 -> `dka100`, etc.  Each row's `media` names the backing image file.
- `cypress_ide` row: the ATAPI CD (`dqa1`); `media` names an .iso, empty =
  no disc.

**Media path resolution rule (one rule, two cases):**

- An **absolute** `media` path (`D:/Disks/dka0.vdisk`) is used as-is.
  This is the simplest way to attach an existing disk.
- A **relative** `media` path (`Alpha/dka0.vdisk`) resolves against
  `[Storage] diskDir` in the profile's `Emulatr.ini`.  New profiles are
  seeded with `diskDir = disks`, so `Alpha/dka0.vdisk` means
  `<profile>\disks\Alpha\dka0.vdisk`.

If a row's file does not exist at launch, the emulator leaves that unit
empty (the SRM then reports `device dka0 is invalid` on boot) and writes a
`Storage: ... not attached` line to the mirror log naming the exact path it
tried -- that log line is the first place to look when a disk is missing.

New blank disks can be created with the launcher's disk tool (Make Disk;
drive geometries come from the installation's verified `disk_types.json`)
or with `tools\mkdisk.py`.

### Step 5 -- The per-system knobs (Details tab)

| Control | Choices | Effect |
|---|---|---|
| **Emulator binary** | Discovered (recommended) / Release / RelWithDebInfo / Debug / Custom... | which Emulatr.exe this system launches.  Discovered = the installed one on a tester machine. |
| **Snapshots** | Safety net (default) / Diagnostic / Off | periodic machine-state snapshot cadence (safety = rare 50B-cycle saves; diagnostic = dense 1B-cycle saves, each pauses the guest; off = none) |
| **Execution mode** | ISP (recommended, default) / Silicon | ISP is the firmware's pre-silicon simulator path and boots to the SRM `>>>`.  Silicon is the faithful REAL_HW path -- experimental, does not yet reach the console. |
| **Console -> Port** | TCP port (default per system) | the telnet console port; **Open Console** connects a terminal (PuTTY if found) |
| **Runtime environment variables** | per-variable Enable + value | see Step 6 |

All knobs persist per system in the registry and take effect at the next
Start.

### Step 6 -- Runtime environment variables (diagnostic builds only)

The environment panel lists exactly what the SELECTED emulator binary
declares (each binary carries `config\env_registry.json` beside it).  The
installed release binary declares NONE -- the diagnostic layer is compiled
out of release, and the panel says so rather than offering dead knobs.
Pinning the system to a RelWithDebInfo/Debug binary lights up the full
curated registry (130+ variables with descriptions).  An unchecked
variable is ABSENT from the child environment, not empty; quarantined
variables are stripped even if set in the user's own shell.

### Step 7 -- Start, console, stop

- **Start** launches Emulatr.exe directly (no shell): working directory =
  the profile, `--firmware` from the Image selection, and the composed
  environment (knob overlays + checked variables).  When the profile owns a
  manifest copy, the launch points the emulator at it explicitly
  (`EMULATR_PLATFORM_CONFIG`), so the installed defaults are not consulted.
- Every run writes a **mirror log**: `<profile>\logs\run_launch_*.log`,
  headed by the effective diagnostic environment for that run.
- **Open Console** connects to `127.0.0.1:<port>`.  At `P00>>>`, verify the
  configuration took: `show dev` should list the disks configured in
  Step 4; then `b dka0`.
- **Stop** requests a clean shutdown by writing the `EMULATR_STOP` sentinel
  into the profile; the emulator exits after persisting its state.  If it
  has not exited after 10 seconds the launcher OFFERS escalation (Force);
  it never kills on its own.  Force-kill loses unpersisted flash state and
  is logged as such.

## 3. What survives what (the persistence contract)

| Event | Named systems (registry) | Profile directories |
|---|---|---|
| Launcher restart | kept | kept |
| Kit uninstall / reinstall / upgrade | kept | kept (incl. customized manifests) |
| Registry hive deleted | lost -- but **Add Existing...** re-registers any profile directory | kept |
| Profile directory deleted | stale entry flagged broken by preflight | lost (it IS the system) |

## 4. Troubleshooting quick table

| Symptom | Cause | Fix |
|---|---|---|
| `device dka0 is invalid` at SRM | storage row's media file not found | check the `Storage:` line in the mirror log; fix the row's `media` (absolute path simplest) or place the file where `diskDir` + relative path points |
| Devices note says `(installed default ...)` | profile predates manifest seeding | click Open in PlatEd once -- it copies the manifest into the profile |
| Environment panel says no variables | release binary selected | expected by design; pin a RelWithDebInfo/Debug binary for diagnostics |
| `memtest: No such command`, `net: No such command`, keyboard notice during boot | normal ISP-path console chatter | none needed |
| New System refused under Program Files | deliberate guard (VirtualStore write redirection) | choose a directory the user owns |

## 5. Cross-references for the topic author

- "Installing with the Windows Installer" (install/uninstall-first upgrade).
- "Firmware Requirements" (supplying SRM images).
- The beta disks & manifest guide source (`docs/beta_disks_and_manifest_guide_source.md`)
  for mkdisk usage and manifest row schema detail.
- "Environment Variables (EMULATR_*)" reference topic (note: four documented
  names are stale as of the 2026-08-14 census: EMULATR_2D_NOOP and the three
  EMULATR_RPCC_LOG_* -- the core no longer reads them).
