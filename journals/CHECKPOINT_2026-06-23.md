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

## 23:07 — Planning EmulatR V4 migration to Intel iMac (build-dependency scoping)
- **Working on:** Helping Tim move the V4 project to an Intel iMac "for the next few days." Scoped what the iMac needs beyond the source tree by inspecting the actual V4 build setup (CMake / vcpkg / Qt config), not a generic list.
- **Done since last checkpoint:**
  - Identified V4 as a C++20 / Qt6 / CMake project, currently MSVC2022 on Windows, pulling header-only spdlog + fmt from vcpkg.
  - Produced the macOS prerequisite list: Xcode CLT (Clang w/ C++20; CMake already handles Clang, e.g. `-ffp-contract=off` for the strict-FP SoftFloat path), Homebrew, CMake ≥3.16, Qt6 (Core/Network/Concurrent), spdlog+fmt (via vcpkg or `brew install`), Git, Python 3. Optional: Ghidra (+JDK), `host_decompressor` (builds with same Clang, no extra deps).
  - Flagged three hardcoded CMake paths to override at configure time: `CMAKE_TOOLCHAIN_FILE`, `VCPKG_ROOT`, `QT_INSTALL_ROOT` (Windows pins `D:/Qt/6.10.2/msvc2022_64`). Windows-specific blocks (windeployqt) already guarded by `if(WIN32)` → no-op on macOS.
  - Confirmed repo is NOT using Git LFS (`.gitattributes` marks `*.rom/*.bin/*.m64` binary but no `filter=lfs`) → plain git transfer is fine.
- **Open / next:** Tim to choose between (a) a `setup-imac.sh` that brew-installs prereqs and prints the exact `cmake` configure command with path overrides, or (b) a minimal transfer manifest (copy git-tracked source only). Awaiting his pick (offered both).
- **Watch-outs:**
  - Size trap: V4 `Emulatr/` working dir is ~131 GB (plus a 6.8 GB `Emulatr.zip` and a large `.git`); much of it is logs (`boot.log` ~103 MB, `rpcc_probe.txt` ~13 MB) and build artifacts. Copy git-tracked source only — do NOT drag 100+ GB to the iMac.
  - On Intel brew the Qt prefix is `/usr/local/opt/qt`, not `/opt/homebrew/...` (that's Apple Silicon) — easy mismatch to make on an Intel iMac.

## 01:07 — Mac↔PC git reconciliation (clean-tree reapply); doctests green; PC fast-forward pending
- **Working on:** Synchronizing the two machines after the iMac picked up V4. Mac Claude reapplied its portability edits onto a clean tree as a fresh commit (no blob, no merge tangle, no rewritten SHAs) so the push lands as a clean fast-forward. Tim is driving git on each machine; Claude is advising read-only and holding all repo writes per his explicit "make no changes until we've checked in on the Mac."
- **Done since last checkpoint:**
  - Provided the Mac its host-platform header **without writing to the repo** (respected the hold): proposed a NEW file `coreLib/axp_platform.h` — OS detection (`AXP_OS_WINDOWS/MACOS/LINUX/POSIX`), arch detection (`AXP_ARCH_X64/ARM64`), and host primitives `axp::alignedAlloc/alignedFree` (_aligned_malloc vs posix_memalign), `bswap16/32/64`, `hostCycles()` (`__rdtsc`, gated `AXP_HAS_HOST_TSC`), `AXP_DEBUG_BREAK()`. `kCacheLineBytes=64` (correct for the Intel iMac).
  - Key finding handed to Mac Claude: the COMPILER-attribute layer `coreLib/axp_attributes_core.h` already routes Clang (`AXP_COMPILER_CLANG`) through the shared GCC/Clang branch → Apple Clang needs **zero** changes there; do NOT fork/re-guard it (would cause duplicate definitions). Distinguished from `systemLib/PlatformConfig.h` (DS10 device manifest, unrelated) and SoftFloat's own `berkeley-softfloat-3-master/.../platform.h` (do not touch).
  - **Doctests passed** after the Mac's reapplied edits + new platform guards — clean signal before the machine move.
  - Diagnosed the PC sync state: local `9beee38` → remote `64b8451` is a clean **fast-forward** (Mac pushed on top). NOT using Git LFS, so plain transfer is fine.
- **Open / next:** Tim runs the sync in **Windows Git Bash** (sandbox can't do it): `rm -f .git/index.lock` (clear stale lock), `git status` / `git diff -- .gitignore` (authoritative dirty check), restore `.gitignore` if truly modified, keep-or-drop `journals/CHECKPOINT_2026-06-23.md`, then `git pull --ff-only origin main`. After he pulls, Claude to re-verify (read-only) the PC matches `64b8451` and offer to review the Mac's portability diff before building.
- **Watch-outs:**
  - **Stale `.git/index.lock`** (0-byte, from an earlier failed stash) still present — blocks any pull/checkout; the sandbox mount **cannot** unlink `.git` files, so Tim must `rm -f` it on Windows.
  - D:-mount phantom truncation struck again: through the sandbox `.gitignore` read as truncated ("# Linux / FUSE m…") and `CHECKPOINT_2026-06-23.md` showed 12 lines removed, but committed HEAD `9beee38` is correct (ghidra ignore rules at lines 73–76). Trust `git status` on Windows, not the mount view.
  - Don't push new Windows work in the gap before pulling the Mac's commit, or it re-diverges. Memory + journal instructions still need Mac-guard conventions added (Tim flagged; deferred until after sync).
