# Checkpoints — 2026-06-23

## 17:07 — Fleshing out technical-manual stub topics (H&M XML); 82077 Floppy Controller topic completed
- **Working on:** Expanding the Help & Manual technical-manual topics from brief overviews into full developer-reference topics. Just authored the complete `82077 Floppy Controller` topic (was a near-empty stub) — register map, MSR bit contract, full command set, completion/interrupt model, wiring, fast-fail contract, diagnostics, limitations — sourced from `deviceLib/Tsunami/Floppy82077.h`, not generic boilerplate.
- **Done since last checkpoint:**
  - DS20 snapshot baseline proven end to end: cold boot → marker snapshot at `P00>>>` (cycle ~2.02B, ~1.5-min cold boot after `memory_test none` + clean build) → **warm resume in ~15s** (~6× faster; time is mostly reconstructing the 4.3 GB full-memory image). Snapshot file `predig_oemsnap_cyc2022094226.axpsnap`.
  - Verified warm resume restores full machine + env state faithfully: `sys_serial_num test1234`, `oem_string snapshot`, 4096 MB good memory, 0 bad pages.
  - Reviewed uploaded `table_of_contents.xml`; proposed §6 Reference reorder (CLI Options → Env Vars → Subsystems → Glossary → Dependencies → Credits) plus 5 other TOC refinements.
  - Created a task for the NVRAM/flash persistence behavior (env changes only flush on clean `EMULATR_STOP`).
- **Open / next:** Save/import the completed 82077 topic as `.xml`; offer the same flesh-out pass to neighbor stubs (`EmulatR Subsystems`, `Virtual Floppy (dva0)`). Queued from prior session: commit IIC + floppy fixes (commit message ready), run DS10 regression (#23), pull the gated probes.
- **Watch-outs:**
  - Env changes persist to `ds20_flash.rom` only on a clean `EMULATR_STOP` exit — unclean exits silently drop NVRAM writes. `memory_test` came back as `full`, not the `none` set earlier, because that run didn't flush. (User confirmed sessions were stopped via `EMULATR_STOP`; circling back later.)
  - Snapshot filename `predig_oemsnap_…` has no model tag — rename to `predig_ds20_p00prompt_cyc2022094226.axpsnap` before relying on it so it can never autoload into a DS10 run (gap that task #25 closes).
  - Many manual topics are still brief overview-only stubs needing the same completion pass.

## 19:07 — DS20 platform-name fix (`iic_ocp0`) applied; banner masked by `oem_string`
- **Working on:** Getting the SRM banner to report "AlphaServer DS20" instead of "AlphaPC 264DP". Edited `ds20_platform.json` to add an `iic_ocp0` IIC device so `get_sysvar` → member 6 resolves the DS20 identity.
- **Done since last checkpoint:**
  - Added IIC entry to `ds20_platform.json`: `{ "name": "iic_ocp0", "address": "0x40", "class": "status", "byte": "0x00", "comment": "get_sysvar→member 6→\"AlphaServer DS20\"; do NOT add 0x4E" }`. File now 39 lines (`iic_ocp0` at 8–9, full `de500_tulip` block 32–39, valid JSON). No rebuild needed — manifest loads at boot.
  - Root-caused the banner not changing: it is NOT a fix failure. `kernel.c:2320` does `ev_read("oem_string", …)` and prints `oem_string` verbatim in place of the platform-name+MHz identity when it's non-empty. NVRAM has `oem_string="snapshot"` (left by the `set oem_string snapshot` snapshot marker), which masks both the DS20 name and the MHz.
- **Open / next:** On a DS20 run, `clear oem_string` (or `set oem_string ""`), then cold-boot WITHOUT `EMULATR_CONSOLE_SNAPSHOT=1` so the marker isn't re-armed; expect `AlphaServer DS20 100 MHz Console V7.3-2` if `iic_ocp0` took (else `AlphaPC 264DP` → investigate whether the status-class device satisfies the `fopen`). Exit via `EMULATR_STOP` to flush the cleared var into `ds20_flash.rom`. Proposed: change default `EMULATR_SNAPSHOT_MARKER` from `set oem_string snapshot` to a benign `set user_def1 snapshot` (folded into snapshot-tooling task #25).
- **Watch-outs:**
  - Hit the known D:-mount phantom truncation again: bash `json.load` failed mid-`de500_tulip` (~line 32) on a stale mount view, while the authoritative Read tool showed the full 39-line file. Verify D: writes with the Read tool, not bash byte counts.
  - `oem_string` is a poor snapshot-marker target precisely because it IS the banner identity — every console snapshot corrupts the banner until cleared.

## 21:07 — Platform manifest keyed to firmware stem (Option A); rebuild + verify pending
- **Working on:** Replacing the hardcoded `.win/.linux` platform-manifest leaf with a firmware-stem-derived name so each firmware image auto-selects its own `<name>_v7_3_platform.json` (closes the DS10/DS20 manifest-confusion gap, task #28).
- **Done since last checkpoint:**
  - `main.cpp:212` — writes the resolved firmware path back into `settings.rom.firmwareImage` before construction, so the ctor can derive the manifest from the actual image used.
  - `Machine.cpp:463` — manifest leaf now `stem(firmwareImage) + "_platform.json"`, resolved next to the exe; removed the `.win/.linux` guards.
  - `run_fw.sh` — now copies `Emulatr/<name>_v7_3_platform.json` next to the exe each run, mirroring the existing firmware copy.
  - All three edits verified ASCII-clean (house rule).
- **Open / next:** Rebuild (`tools/build_emulatr_diag.bat`) since `main.cpp` + `Machine.cpp` changed, then verify with `EMULATR_PLATFORM_CONFIG` UNSET (the workaround would mask the new derivation): `run_fw.sh ds20 cold`. Confirm (1) manifest loads — `PlatformConfig: ... using built-in default DS10 bus` warning gone, no `Device Open Error: IIC_OCP1`; (2) banner = `AlphaServer DS20 100 MHz` after `clear oem_string` + `EMULATR_STOP` flush. Then re-mint the `predig_ds20_p00prompt` snapshot using benign marker `EMULATR_SNAPSHOT_MARKER="set user_def1 snapshot"`.
- **Watch-outs:**
  - Commit batch now: `main.cpp` + `Machine.cpp` + `run_fw.sh` + the renamed `<model>_v7_3_platform.json` manifests — keep them together so the derivation never points at a missing/old-named manifest.
  - Must clear `EMULATR_PLATFORM_CONFIG` for the verify run or the override hides whether the stem derivation actually works.
