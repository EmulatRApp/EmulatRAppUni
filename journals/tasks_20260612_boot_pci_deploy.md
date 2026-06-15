# Tasks queued -- 2026-06-12 (loaded into Memory.md)

> NEXT SESSION (2026-06-13): begin discussions + scaffold the PCI work
> (#41). Loaded into Memory.MD current-state queue 2026-06-12.

Boot-to-OS gates, PCI completion, and deployment staging, generated from
the dq-boot + PCI + documentation planning session. Ticket numbers
continue the existing scheme (last seen: #36 CSERVE); reconcile against
the live tracker if any collide.

---

## Boot-to-OS gates

- **#37  H&M topic: Create Disk.**  Document automatic provisioning
  (`create_if_missing`) and `tools/mkdisk.py`, the
  `config/disk_types.json` / `dec_disk_media_types.tsv` catalog, bare
  sparse-image semantics (no partition table / disklabel -- the guest
  install writes those), and the cross-platform sparseness note. Place
  under the USER (or DEV) hive. Deliverable drafted: `Create_Disk.xml`.
  STATUS: draft for review.

- **#38  H&M topic: Boot Disk.**  Document the SRM `boot` command, device
  names (dqa0/dqa1/dka0/ewa0), the three SRM env vars (`bootdef_dev`,
  `boot_osflags`, `auto_action`), boot flags (OpenVMS `-fl root,flags`;
  conversational bit 1 -> SYSBOOT>), the VMB -> SYSBOOT.EXE -> SYSBOOT>
  flow, and the install-from-CD example (`boot dqa1` -> install ->
  `boot dqa0`). Tru64 flags marked [CONFIRM]. Deliverable drafted:
  `Boot_Disk.xml`. STATUS: draft for review.

- **#39  SYSBOOT> scaffold (OpenVMS conversational boot).**  Ensure
  `boot dqa0 -fl 0,1` reaches the `SYSBOOT>` prompt and SYSGEN
  SET/SHOW/CONTINUE function. HARD DEPENDENCY on #41 (PCI complete):
  VMB, SYSBOOT, and the early exec traverse PCI to reach the boot device.
  Also depends on #32 (multi-block disk read -- SYSBOOT.EXE is many
  blocks). Acceptance: a cold-boot trace that reaches `SYSBOOT>` from a
  clean `>>>` and accepts a SET/CONTINUE.

- **#40  Tru64 boot-gate investigation (parallel).**  Identify the Tru64
  (OSF/1) equivalent of the SYSBOOT> gate: the interactive / single-user
  boot flags and the kernel's PCI / device-config path. Hypothesis:
  similar PCI gating applies (Tru64 has no SYSGEN conversational prompt;
  its analog is interactive boot to the single-user shell). Output: a
  short note pinning the Tru64 boot flags and the device-config
  dependency; feeds #41 acceptance. No code until the gate is pinned.

- **#41  PCI interface -- COMPLETE (blocks #39).**  PCI is not yet written
  to the depth the OS bootstrap needs. The SRM console probe was enough
  to enumerate dqa; VMB/SYSBOOT and OS adapter-init are not. Complete it,
  trace-first:
    - `EMULATR_PCI_CFG_TRACE`: capture the config cycles SRM/OS issue
      (0xFFFFFFFF BAR-sizing writes, read-back, final BAR-assignment
      writes) per device, BEFORE writing rebind code.
    - `IPciDevice` config/BAR seam (sibling of IBlockMedia): uniform
      config-space header + BAR-decode declaration. Cypress IDE = legacy
      (no relocatable BARs); DE500 = 2 BARs (io 0x80 / mem 0x80).
    - S2 dynamic BAR -> range rebind (the known gap): a BAR write must
      re-point bus routing for that device. Static registration bypasses
      this today. Drive it from the trace -- match SRM's exact sequence.
    - #7 DE500 tulip enumerates with BARs assigned; GCT entry correct and
      does not perturb the GCT Track A depends on.
  Acceptance: a BAR write observably rebinds decode (doctest + trace);
  DE500 enumerates; the OS bootstrap's PCI traversal reaches the disk.

---

## Deployment

- **#42  Run-dir rooting + staging (Setup Factory 9 prep).**  Every
  runtime dependency must be staged under the build-run directory and
  resolved relative to the executable, so a packaged deployment is
  self-contained. Principle: if the program depends on it -- a file, a
  facility, anything -- it is copied/staged below the run dir; nothing is
  resolved from an absolute path or an install-time location.
    - AUDIT every file/facility the running program touches: platform
      manifests (`ds10_platform.win` / `.linux`), `config/disk_types.json`,
      the platform JSON, `dec_disk_media_types.tsv`, firmware
      (`ds10_v7_3.exe`), the ini (`EmulatrV4.ini`), the disk-image dir,
      the snapshots dir, `tools/mkdisk.py` + its catalog, and the Qt
      runtime (platform plugin, `qt.conf`, required DLLs/.so).
    - PATH RESOLUTION: anchor every runtime path to the executable
      directory (`QCoreApplication::applicationDirPath()`), never
      CWD-relative and never absolute-hardcoded. One
      `resolveRuntimePath(relative)` helper; all QSettings file references
      flow through it.
    - STAGING: extend the existing CMake POST_BUILD copy so ALL of the
      audited items land in the run dir. Emit a staged-file inventory --
      the definitive manifest Setup Factory 9 packages.
    - FAIL LOUD: a missing dependency aborts at startup with a clear
      diagnostic, never a silent fallback (same discipline as the
      media-factory fail-closed and the platform gate).
  Acceptance: the app launches and runs correctly from a copied run dir
  on a clean machine (no source tree, arbitrary CWD), on both Windows and
  Mint; the staged-file inventory matches what the app actually opens at
  runtime (no unlisted dependency).
