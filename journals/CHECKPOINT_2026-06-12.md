# Checkpoints — 2026-06-12

Rolling intra-day checkpoints (survives crashes). Memory.MD is the
end-of-session refresh; these are the in-flight record.

## Session checkpoint — memory fix + driver requirements review

**#26 (64 Meg → 1024 Meg) — root-caused, NOT a code defect.**
The `>>>` run still showed `64 Meg` (show memory + show config both
Array 0 = 64 MB). Diagnosis: the source fix IS in `main.cpp:194-209`
(2026-06-12) and `config/EmulatrV4.ini:36` has `memorySize=1073741824`
(default in `EmulatorSettings.h:52` is also 1 GiB), so no path through
the fixed binary yields 64 MB. The `>>>` was reached by **autoloading a
pre-fix snapshot** (banner/`show config` header = `snapshot`), which
freezes the old 64 MB and bypasses chipset construction. FIX TO CONFIRM:
rebuild + cold boot with `EMULATR_NO_AUTOLOAD=1`, watch stderr
`memory: using [System] memorySize from ini: 1073741824 bytes`, then
`1024 Meg` banner + `AAR0=0x7009`; then re-mint snapshots. NOTE: memory
authority is now `EmulatrV4.ini [System] memorySize`, NOT legacy
QSettings.

**Observations from the same `>>>` run:**
- `show config` shows `SROM Revision: P▒` (garbage ASCII) = task **#6**.
- CD enumerated as `dqa1`, not dqa0 (primary master disk not present).
- `exer: No such command` + `snapshot` banner leak = relates to #20.

**#27 (new):** `Halt Button is IN, AUTO_ACTION ignored` / "BOOT NOT
POSSIBLE" — needs software neutralization (default the modeled halt
button to OUT/released so auto_action/boot can proceed). Ties to the
halt-button thread from the 2026-06-08 handoff.

**dq_driver / ew_driver requirements review — DONE.**
Full writeup: `journals/20260612_dq_ew_driver_requirements_review.md`.
Summary: dq (IDE/ATAPI) is ~90% modeled (Cy82C693Ide enumerates, ATAPI
handshake works, dqa1 lists) — needs the media-backed read path for
`boot dqa0`. ew (DE500/21143 tulip) is a `generic` config-space stub in
`ds10_platform.json` — needs a real `Dc21143Tulip` model (CSR0-15 +
CSR9 microwire SROM/MAC/media-table) before `ewa0` can appear. The
`0xFFFF0000 UNHANDLED OUTER WRITE` chatter = ew_driver bit-banging the
missing NIC's CSR9 SROM. SRM naming (ewa/ewb/ewc) already auto-letters
in `SRMConsole.cpp`, so multiple NICs fall out by adding pci_devices[]
rows once the model exists.

**New tasks added this session (Cowork list):**
- #28 ew Tier-0: `Dc21143Tulip` enumerate model (ewa0 + MAC via CSR9 SROM)
- #29 ew Tier-1: 21143 link + media select (SIA/MII)
- #30 ew Tier-2: TX/RX descriptor rings + setup frame (netboot)
- dq media-backed read path (ATA READ 0x20 + ATAPI READ(10) → file backend)

**Also this session:** appended today's EOD journal
(`20260612_EOD_handoff_memory_size_64_to_1024.md`) and refreshed
`Memory.MD` (was stale since May 11). Set up a 2-hourly checkpoint
scheduled task (working hours) that writes to this file.

## 01:06 — StorageTarget `enabled` flag + IBlockMedia + `.win` manifest

- **Working on:** wiring a per-target `enabled` flag end-to-end so a
  disabled alternate can share a device slot with an enabled target
  (supports flipping `dqa1` between a Tru64 ISO and a host-passthrough
  drive). Session was finishing validation/doctest and offered to fold
  the work into this journal.
- **Done since last checkpoint:**
  - `StorageTarget.enabled` added; **absent = enabled (implicit true)**.
    Parser reads it via `toBool(true)`.
  - **Validation:** disabled targets are skipped in the duplicate
    channel/unit check, so an alternate can share a slot; two *enabled*
    targets on the same slot remains a hard error.
  - **Wiring:** `Machine` skips `!enabled` targets (parsed but not
    attached).
  - **Doctest** added to `test_platform_config.cpp`: implicit-true,
    explicit-false, disabled-alternate-shares-`(0,1)` passes, and a
    positive control (enable it → duplicate → hard error). Suite now
    449 + this new test.
  - `ds10_platform.win` updated: `dqa1` carries the Tru64 ISO (enabled)
    with a **disabled host-passthrough alternate on the same `(0,1)`
    slot**. Also IBlockMedia refactor + `.win` manifest CMake copy fix
    earlier in the session.
- **Open / next:** rebuild + cold boot; expect manifest to load clean
  and `Storage: attached ATAPI CD 'D:\isos\tru64v5.iso' to IDE ch0
  unit1` if the ISO exists, with the new platform-config test green.
  To later switch `dqa1` to the physical drive: flip ISO entry to
  `enabled: false`, host entry to `enabled: true` (host path is Phase B
  / task #34 — warns until it lands).
- **Watch-outs:** D: bash mount lags host writes — `python` JSON
  validation read a *truncated* manifest (cut ~line 31) twice; the host
  file was complete and correct. Verify manifest/journal edits with the
  Read tool (host view), not bash byte counts. Optional follow-up:
  extend `enabled` to top-level PCI devices (e.g. disable `de500_tulip`)
  and IIC devices — same one-line-per-layer addition.

## 19:29 — storage stack PROVEN end-to-end (both media attach, 454 green)

- **Milestone:** the full IBlockMedia / factory / create-if-missing /
  OS-suffixed-manifest arc is live and confirmed at runtime. Cold boot
  now emits, with no manifest parse error:
  - `Storage: created blank disk image 'dqa0.img' (4294967296 bytes)`
    on first boot, then `Storage: attached ATA disk 'dqa0.img' to IDE
    ch0 unit0` on every boot (never re-created — never-overwrite guard
    holds).
  - `Storage: attached ATAPI CD 'D:\isos\tru64v5.iso' to IDE ch0 unit1`
    once the real ISO was dropped at that path (factory had correctly
    fail-closed and left unit1 empty while the file was absent).
- **Tests:** suite now **454 / 5999 / 0** (the +5 create-if-missing +
  parse/validate doctests on top of the prior 449). The earlier C2660
  (test_block_media.cpp:195, 5-arg makeBlockMedia missed by a
  replace_all because of an inline `/*readOnly=*/` comment) is fixed —
  all callers now pass the 6-arg signature with `createBytes`.
- **Deploy:** `tools/mkdisk.py`, `config/disk_types.json`,
  `config/dec_disk_media_types.tsv` are POST_BUILD-copied into the run
  dir (run dir = `$<TARGET_FILE_DIR:Emulatr>` = out/build/<config>/).
- **Next gate:** the dq boot ACCEPTANCE trace — `boot dqa1` (Tru64
  installer from the ISO) installs onto the now-present dqa0.img, then
  `boot dqa0`. Optionally extend `EMULATR_IDE_TRACE` to log the
  IBlockMedia read result (N5) for the bounded cold-boot acceptance run
  (dqa enumerates, dqa1 MEDIA PRESENT + reads sectors, dqa0 returns
  LBN 0). Synchronous IIoPortHandler path retained — no IoSeam
  migration.
- **Still standing:** the `>>>` path is gated upstream by the 0x7bef0
  software-tick loop / R2 clock-interrupt return (unrelated to
  storage); the dqa1 "no media / RUN-STOP" line in older boot.logs was
  just the absent-ISO case, now resolved.

## 23:10 — Track A/B next-steps plan grounded against code; awaiting instrumented boot

- **Working on:** turning the COWORK NEXT STEPS plan (Track A: ISO BOOT
  critical path; Track B: PCI INTERFACE parallel) into a verified,
  task-tracked program. Discuss-before-code: validated the four
  load-bearing seams in source before any edits. Session ended on an
  AskUserQuestion for the one live fork (what to do while the user runs
  the boot).
- **Done since last checkpoint:**
  - Recorded the storage-proven milestone (454 green) into Memory.MD +
    this checkpoint; storage gate CLEARED.
  - **Seam verification (all four confirmed, one better than assumed):**
    - **#32 multi-block ATAPI** — `VirtualIsoDevice.h:204 doRead()`,
      single 2048-byte burst; BURST LIMIT comment at lines 34–36 names
      #32 as the follow-up. One-function seam.
    - **#36 CSERVE Namespace-4 migration** — `execCserve` in
      `iBoxLib/grains/ControlFlow.cpp` (+ `palBoxLib/grains/PalEntries.cpp`);
      default-arm `kFaultUnimplemented` is real. Trace-gated.
    - **B1 PCI cfg trace** — ALREADY BUILT: `EMULATR_PCI_CFG_TRACE` hook
      live at `TsunamiPchip.h:1113-1132` in readPciConfig0/writePciConfig0.
      So B1 is capture+interpret, not build — rides any Track-A boot that
      enumerates PCI.
    - **A1 blocker / static BAR** — 0x7bef0 clock path spans
      `Machine.cpp`, `Pic8259Pair.h`, `PalEntries.cpp`; static port/mem
      registration at `TsunamiPchip.h:413/449` with no BAR-write rebind
      (confirms the S2 gap, B3).
  - **Task tracking set up:** critical path A0 → A1 → (#32, #36) → A2c →
    A3; parallel B1 + B2 → B3 → #7 DE500.
- **Open / next:** the single instrumented cold boot that captures every
  signal at once — `EMULATR_NO_AUTOLOAD=1 EMULATR_CONSOLE_MIRROR=1
  EMULATR_PCI_CFG_TRACE=1 ./Emulatr.exe --firmware firmware/ds10_v7_3.exe
  --autosnapshot off` (clear `snapshots/*.axpsnap` first — old snaps bake
  in 64 MB). Readout greps for: A0 (`memorySize ... 1073741824` / "1024
  Meg" / AAR0=0x7009), #36 (first real `CSERVE entry`), B1
  (`PCICFG-TRACE` enum sequence), A1 stall (`0x000000000007bef0`).
- **Watch-outs:** A1 (0x7bef0 tick-loop / R2 clock-interrupt return) is
  the real long pole — #32, #36, CD boot-block read are all DOWNSTREAM
  and cannot be exercised (no-fix-before-trace) until a clean cold boot
  reaches `>>>` and `boot dqa1` issues those ops. Claude cannot run the
  Windows boot (sandbox is the lagging D: mount, not the runtime) — the
  user runs `Emulatr.exe` in MINGW and pastes traces; Claude does
  code/seams/tests/trace-interpretation.
