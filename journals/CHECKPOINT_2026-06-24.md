# Checkpoints — 2026-06-24

## 15:07 — Bash-native MSVC toolchain bootstrap (`tools/vsenv.sh`)
- **Working on:** Making the Windows build drive from plain Git Bash (consistent with the Mac, which builds from bash natively). Goal: same `./tools/...` entry point on both machines, with Windows loading the MSVC env itself instead of requiring a manually-opened VS Developer prompt.
- **Done since last checkpoint:**
  - Added new file `EmulatRAppUniV4/Emulatr/tools/vsenv.sh` — a *sourceable* helper that loads the MSVC x64 environment into the current bash. On Windows it locates VS via `vswhere`, runs `vcvars64.bat`, and imports the resulting env; on Mac/Linux it is a no-op (Homebrew already has cmake on PATH).
  - Rewrote vsenv.sh after first version failed: now uses a tiny temp `.bat` to avoid MSYS→cmd nested-quote mangling, and strips Windows CRLF (`\r`) from the env dump. (First run failed with `vcvars64.bat failed` + `/tmp/tmp.* No such file or directory`.)
  - Deliberately left `build_test_run.sh` and `CMakeLists.txt` **untouched** to avoid cross-machine divergence while the Mac session may still be editing shared files.
- **Open / next:**
  - User to run `source tools/vsenv.sh` (sourced, not `./vsenv.sh` — subshell loses the env) and paste output; confirm `cmake :`, `ninja :`, `cl : present` lines appear.
  - Diagnostic on standby if vcvars still unhappy: `cmd //c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"'` to surface the real error (likely missing "Desktop development with C++" workload / x64 toolset).
  - After confirmation, planned shared-file edits (land deliberately, then Mac pulls): (a) have `build_test_run.sh` auto-source `vsenv.sh`; (b) add `vsenv.sh` to the CMake POST_BUILD deploy so it travels into the run dir like the other scripts.
- **Watch-outs:**
  - User's earlier symptom: `FATAL: cmake not on PATH` from `build_test_run.sh` in a plain Git Bash — root cause is missing MSVC env, which vsenv.sh addresses.
  - Writes over the D: mount have occasionally read back truncated from the agent side — user advised to sanity-check `tail -5 tools/vsenv.sh` ends with the `unset -f` line.
  - VS detected at `C:\Program Files\Microsoft Visual Studio\2022\Community`.

## 19:07 — Clean Windows build + DS20 run-line confirmed
- **Working on:** Validating the freshly-configured Windows build and pinning down the exact DS20 launch invocation ("messaging for ds20").
- **Done since last checkpoint:**
  - CMake **configure + generate succeeded** after clearing the stale in-source cache (`Configuring done 0.5s / Generating done 0.6s`, build files in `out/build/release`). Mac CMakeLists Qt-deployment changes confirmed healthy.
  - Root-caused the failing `cmake --build --preset Qt-Release`: presets file defines configure presets but **no build preset**. Correct equivalent is `cmake --build out/build/release -j` (optionally `--target Emulatr` / `Emulatr_tests`).
  - **Full build clean, tests green:** doctest `472 cases / 472 passed`, `6058 assertions passed`, Status SUCCESS. No trainwreck.
  - Established the canonical DS20 run line: from `out/build/release`, `./run_fw.sh ds20 cold` (model is a positional/ini-driven arg — no `--model` flag). Wrapper copies `firmware/ds20_v7_3.exe`, sets `[System] model = DS20`, launches `Emulatr.exe --firmware firmware/ds20_v7_3.exe --mem 4 GiB --no-autoload`; console on **TCP 10023**.
- **Open / next:**
  - User to actually launch DS20 (`./run_fw.sh ds20 cold`, connect raw to 10023) and observe boot toward `>>>`; for init-wall chasing use `bash tools/diag_ds20_nowarp.sh` (warp-off + profiler + both fixes baked in).
  - In VS, select **Emulatr.exe** as the Startup Item (now that configure sticks).
- **Watch-outs:**
  - **DS20 platform manifest must sit next to the exe.** `Machine.cpp` resolves `ds20_v7_3_platform.json` from the *executable's* dir (`out/build/release`); `run_fw.sh` copies the firmware but **not** the manifest. If missing → log `PlatformConfig: manifest ... unusable; using built-in default DS10 bus` = DS20 firmware on a DS10 device tree (silently wrong). Fix: `cp ../../../ds20_v7_3_platform.json .`. Agent's sandbox view of `out/build/release` read back empty — verify on Windows (D: mount has shown stale reads).
  - **Flash/NVRAM defaults to `ds10_flash.rom` for every model** — DS20 reads DS10's persisted env/FRU (suspected init-wall cause). For a factory-fresh cold boot: `export EMULATR_FLASH_ROM=ds20_flash.rom; rm -f ds20_flash.rom`.

## 21:07 — Platform-mismatch latch/canary + DS10 manifest mislabel fix
- **Working on:** Making the DS20-vs-DS10 platform badge bug deterministic to diagnose (firmware still prints "AlphaPC 264DP" instead of DS20), and fixing the discovered `ds10_v7_3_platform.json` mislabel. Four surgical edits landed; build/test runs on the user's Windows side (no MSVC in-session).
- **Done since last checkpoint:**
  - `ds10_v7_3_platform.json` — `"platform": "DS20"` → `"DS10"` (the mislabel that made DS10 silently load the DS20 tree).
  - `systemLib/Machine.cpp` (~line 499, after the `usedDefault` warn) — **P1 latch:** case-insensitive `ini model` vs `manifest platform` compare → `spdlog::error("PLATFORM MISMATCH…")` on disagreement (warn-loud, non-blocking). **P2 canary:** one `spdlog::info("platform latched: model=… manifest=… usedDefault=… ocp40=Y/N ocp42=Y/N iic_acks=[…]")` per boot.
  - `systemLib/PlatformConfig.cpp::validate()` — **P3:** when `platform=="DS20"`, warn if OCP 0x40/0x42 missing or 0x4E present.
  - `tools/run_fw.sh` — refresh `<name>_v7_3_platform.json` next to the exe every run, so manifest edits take effect without a relink (directly addresses the 19:07 "manifest must sit next to the exe" watch-out).
- **Open / next:**
  - User to build on Windows (`source tools/vsenv.sh; cmake --build out/build/release -j`), run `Emulatr_tests.exe` (expect 472 cases pass), then `./run_fw.sh ds20 cold` and read the new canary line. Expected healthy: `model=DS20 manifest=DS20 usedDefault=0 ocp40=Y ocp42=Y iic_acks=[ 0x40 0x42 0x70 0x72 0xA2 0xA4 0xC0 ]`.
  - Interpretation guide: `ocp40=N`/`usedDefault=1` ⇒ badge bug pinpointed at manifest load; `ocp40=Y` but still "AlphaPC 264DP" ⇒ failure mode 3 (the `IicPcf8584` status device isn't ACKing the probe read) — next thing to model.
- **Watch-outs:**
  - Can't compile in-session — if the build throws, user pastes errors for an immediate fix.
  - **`ds10_v7_3_platform.json` is a full clone of the DS20 file** (comment + device tree, OCP included), not just a bad label. Relabeling to DS10 stops the silent cross-load but DS10 still needs a proper DS10 device-tree review before DS10 runs are trustworthy — not yet done.

## 01:07 — Ghidra hunt for the SysType/badge decision (`build_dsrdb`/`get_sysvar`)
- **Working on:** Reverse-engineering the SRM firmware to find *why* the DS20 image badges itself "AlphaPC 264DP" (and "100 MHz"). Goal: decompile the function that computes the SysType member and writes SYSVAR, so the badge bug can be fixed at its source rather than papered over with `oem_string`.
- **Done since last checkpoint:**
  - Localized the badge logic in the ds20 image: the banner/SysType lookup table lives at `0x153cd8` (member **1 = "AlphaPC 264DP"** = the default, `0x153cd8`; member 6 = "AlphaServer DS20", `0x153d04`; member 8 = "DS20E", `0x153d5c`). Decision strings are `0x19ad90` = "Defaulting system type to AlphaPC 264DP" and `0x19adc0` = "Error determining system type, SYSVAR = %x" — the code referencing them *is* the badge-deciding function.
  - Confirmed the IIC node list at `0x157578` probes `iic_ocp0`@**0x40**, `iic_ocp1`@**0x42**, `iic_8574_ocp`@**0x4E**, matching the wire ACKs and the apisrm reference exactly — so the device layer is correct; the badge is a firmware-internal SysType computation.
  - Established that manual Ghidra click-through is a dead end: rodata `0x153cd8`–`0x153f40` is mis-typed as instructions (the `CALL_PAL`/`BGT zero` listing lines are really 4-byte pointers + constants), and `build_dsrdb` indexes the table with a *computed* offset (`table_base + member*0x2c`), so there is no clean code→string XREF to follow.
  - Wrote new script `tools/ghidra_scripts/DumpSysvarFns.py` (alongside existing `DecompileToDir.java`): follows refs to the banner table + decision strings, decompiles the containing functions, writes C to `C:\Users\tim\sysvar_dump.txt`.
- **Open / next:**
  - User to run `DumpSysvarFns.py` from Ghidra **Script Manager** (add script dir `D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\ghidra_scripts`, refresh, Run) against the loaded ds20 program, then paste `C:\Users\tim\sysvar_dump.txt` (esp. `get_sysvar`/`build_dsrdb`).
  - Reading the decompile for: does the SysType if/else call `fopen("iic_ocp0")` and test the result, or key off an HWRPB field / SROM value / HW register — and **where it writes SYSVAR** (location still unknown).
- **Watch-outs:**
  - Fallback A — if the Ghidra console reports **0 refs** for the string anchors, auto-analysis didn't reference them; switch the script to a value-scan (search all instructions that load these addresses).
  - Fallback B — if `get_sysvar` shows up only under **ORPHAN REFS** (raw disasm, no clean function), the raw Alpha disassembly around the reference is enough to read the member logic by hand.
  - Badge is **non-blocking** — machine already runs to `>>>`; the "100 MHz" banner is the same class of issue (firmware computes it from an RPCC calibration that lands low in EmulatR). This is a fidelity/cosmetic fix, not a boot blocker.
