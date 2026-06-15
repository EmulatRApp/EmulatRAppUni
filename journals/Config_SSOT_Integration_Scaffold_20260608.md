# Config Single Source of Truth — Integration Map & Scaffold (2026-06-08)

Status: SCAFFOLD / DESIGN. No code rides with this doc beyond the already-landed
console-port wiring (task #12). Discuss-before-code: slices below are proposed,
not yet applied. Architect sign-off on §5 decisions gates the edits.

---

## 1. Problem: five overlapping config mechanisms

| # | Mechanism | Source file(s) | Reads from | Live? |
|---|-----------|----------------|-----------|-------|
| 1 | `IniLoader` -> `EmulatorSettings` | `config/IniLoader.cpp` | `EmulatrV4.ini` | Now compiled (#12). Consumed only by `makeCom1Cfg` (port). |
| 2 | Raw `QSettings` | `deviceLib/OPA_Console_Config.h`, `systemLib/Machine.cpp` | `EmulatrV4.ini` (again) | Independent parse of the same file. |
| 3 | `PlatformConfig` | `systemLib/PlatformConfig.cpp` | `ds10_platform.json` | Live — device topology (IIC/PCI manifest). |
| 4 | `AppOptions` (CLI) | `systemLib/AppOptions.cpp`, `main.cpp` | argv | Live — `--firmware`, `--mem`. |
| 5 | `FirmwareDeviceManager` | `deviceLib/FirmwareDeviceManager.h` | `EmulatorSettings` | Accepts settings via `initializePhase0_FirmwareContext`, but the real load is COMMENTED OUT (`global_firmwaredevicemanager.cpp:58`) -> runs on struct defaults. |
| 6 | Hardcoded | `Machine.cpp` (`cpuCount=1`), `makeCom1Cfg` (`puttyPath="PuTTY.exe"`) | literals | Live, shadowing the ini. |

No single object owns configuration; consumers each reach for a different
mechanism, and two of them re-parse the same ini file.

---

## 2. Integration map — the six fields

| Field | ini key | struct field | default (ini) | loader | LIVE source today | target consumer | status |
|-------|---------|--------------|---------------|--------|-------------------|-----------------|--------|
| model          | `[System] model`          | `SystemSettings.model`        | `ES45`           | `loadSystem`     | `FirmwareDeviceManager` (on **defaults**, not real load) | FDM platform root + banner/HWRPB systype | DEAD (defaulted) |
| cpuCount       | `[System] cpuCount`       | `SystemSettings.cpuCount`     | `4`              | `loadSystem`     | `FirmwareDeviceManager` (defaults); `Machine` hardcodes chipset `cpuCount=1` | chipset CPU count + FDM | DEAD (hardcoded 1) |
| port           | `[SRMConsole] port`       | `SrmConsoleSettings.port`     | `10023`          | `loadSrmConsole` | `makeCom1Cfg` (WIRED #12) | console listen + PuTTY | **WIRED** |
| puttyPath      | `[SRMConsole] puttyPath`  | `SrmConsoleSettings.puttyPath`| `putty.exe`      | `loadSrmConsole` | `makeCom1Cfg` hardcodes `"PuTTY.exe"` | PuTTY auto-launch | DEAD (hardcoded) |
| firmwareImage  | `[ROM] firmwareImage`     | `RomSettings.firmwareImage`   | `es45_v7_3.exe`  | `loadRom`        | CLI `--firmware` (`opts.firmwarePath`) | `FirmwareLoader` | DEAD (CLI only) |
| firmwareSha256 | `[ROM] firmwareSha256`    | `RomSettings.firmwareSha256`  | `` (empty)       | `loadRom`        | (none) | `FirmwareLoader` integrity check | DEAD (no check) |

---

## 3. Landmines

- **Stale defaults.** The ini ships `model=ES45` / `firmwareImage=es45_v7_3.exe`,
  but you boot **DS10** (`ds10_v7_3.exe`) via CLI. If `firmwareImage`/`model`
  are consumed from the ini WITHOUT CLI precedence, the emulator boots the wrong
  image. Fix the ini defaults to DS10 as part of this work.
- **FDM on defaults.** `FirmwareDeviceManager` already builds a "platform root"
  from `system.model`/`cpuCount`/`memorySizeBytes`, but the real settings load is
  commented out, so its platform root is fiction (ES45/4-CPU/1 GiB defaults).
- **Double ini parse.** `OPA_Console_Config.h` and `IniLoader` both read
  `EmulatrV4.ini` with separate `QSettings` instances and separate key schemas.

---

## 4. Single source of truth — proposed design

One owner, loaded once, threaded everywhere:

1. **`main` loads `EmulatorSettings` once** via `IniLoader::loadDefault()`,
   immediately after `QCoreApplication` is up and argv is parsed.
2. **CLI overlay.** Apply `AppOptions` on top of the ini-loaded settings so
   `--firmware`, `--mem`, etc. override ini values. Precedence, low -> high:
   `struct default  <  EmulatrV4.ini  <  CLI`.
3. **Validate + log.** Call `settings.validate()`; log each warning. Refuse to
   start on hard errors (e.g. empty firmware after overlay).
4. **Thread it.** Pass `const EmulatorSettings&` into the `Machine` ctor (stored
   as `m_settings`). `makeCom1Cfg`, the chipset, and the firmware path all read
   from `m_settings` instead of re-loading or hardcoding.
5. **Initialize FDM from the same object** (uncomment + feed the real settings).
6. **Retire the parallels.** Remove `makeCom1Cfg`'s local `IniLoader` call (reads
   threaded settings instead) and the `OPA_Console_Config` `QSettings` reader
   (fold its keys into `EmulatorSettings`/the device section).
7. **PlatformConfig stays JSON, selected by `model`.** Device topology is a
   distinct concern (nested/repeating device records) and stays in
   `<model>_platform.json`. `model` is the single selector:
   - Mapping: `lower(model) + "_platform.json"` -> `DS10` resolves
     `ds10_platform.json`, `ES45` resolves `es45_platform.json`.
   - Override wins: explicit `EMULATR_PLATFORM_CONFIG` env (already in Machine)
     or an optional `platformConfigPath` ini key beats the derived name.
   - Missing file -> HARD ERROR, never a silent fallback. `model=ES45` with no
     `es45_platform.json` fails loudly (correct: ES45 isn't bootable until its
     manifest exists) instead of booting DS10 topology under an ES45 identity.
   `model` thus becomes the master switch: system identity (FDM root, banner /
   HWRPB systype) + topology file + the default firmware image. Setting
   `model=DS10` once makes firmware/topology/identity resolve consistently.

### Precedence (locked)

Two tiers for configuration:

```
struct compile-time defaults  <  EmulatrV4.ini  <  CLI flags
\__________________ EmulatorSettings (base) _____________/   \__ override __/
```

`EmulatorSettings` (defaults, then the ini layered on) is the source of truth;
CLI flags are how a single run deviates. The committed ini drives a no-flag launch.

Env vars are NOT a configuration tier. They split into two buckets:

- **Config-bearing** (`EMULATR_CONSOLE_PORT`, `EMULATR_FLASH_ROM`,
  `EMULATR_PLATFORM_CONFIG`) -> give each a real CLI flag
  (`--console-port`, `--flash-path`, `--platform-config`); keep the env var only
  as a documented alias (or retire it). They live inside the ini->CLI model.
- **Debug toggles** (`EMULATR_IDE_TRACE`, `EMULATR_IIC_TRACE`,
  `EMULATR_FLASH_TRACE`, `EMULATR_NO_PUTTY`) stay env-only -- not configuration,
  no CLI surface, no precedence interaction.

---

## 5. Decisions

1. **Precedence — LOCKED.** `struct defaults < EmulatrV4.ini < CLI`. Env is not a
   config tier (config-bearing env vars become CLI flags w/ alias; debug toggles
   stay env-only). See "Precedence (locked)" above.
2. **Fix ini defaults** `model` -> `DS10` (which derives `ds10_platform.json` +
   `ds10_v7_3.exe`). LOCKED yes.
3. **cpuCount scope — LOCKED: single CPU.** DS10 is a single-socket 21264, so the
   ini default becomes `cpuCount=1` (the `4` is an ES45 leftover). FDM / HWRPB /
   banner report 1; chipset stays 1; no multi-CPU instantiation. IMPORTANT: do NOT
   report >1 to the firmware while only one CPU executes — SRM/GCT can spin waiting
   for secondaries that never start (candidate cause for #5 boot-timeout loops).
4. **Retire `OPA_Console_Config` QSettings — AGREED.** Slice D (defer until A–C land).
5. **`Machine` ctor signature — LOCKED:** gains `const EmulatorSettings&`
   (default-constructed arg so the 407-test suite keeps building).

---

## 6. Scaffold — ordered slices (each: edit -> test -> verify)

### Slice A — central load + console (low risk)
- `main.cpp`: load `EmulatorSettings` (IniLoader), overlay CLI, `validate()`, log.
- `Machine` ctor: accept `const EmulatorSettings&` (default arg); store `m_settings`.
- `makeCom1Cfg`: read `m_settings.srmConsole.port` + `.puttyPath`; drop the local
  `IniLoader` call and the hardcoded `"PuTTY.exe"`. Keep `EMULATR_CONSOLE_PORT` env top layer.
- Tests: precedence unit test (default/ini/CLI/env) for port; puttyPath from ini.

### Slice B — firmware (image + integrity)
- `main`/`FirmwareLoader`: firmware path = CLI if present else `m_settings.rom.firmwareImage`.
- `FirmwareLoader`: if `rom.firmwareSha256` non-empty, compute SHA-256 of the loaded
  image and fail on mismatch; log the computed hash either way.
- Fix ini defaults to DS10 (per decision §5.2).
- Tests: CLI-overrides-ini; sha match passes / mismatch fails / empty skips.

### Slice C — model + cpuCount + model-driven platform resolution
- ini default `cpuCount = 1` (DS10 single-socket; was ES45's 4). FDM / HWRPB /
  banner report `m_settings.system.cpuCount` (=1); chipset stays 1; clamp
  `activeCpus = min(activeCpus, cpuCount)`. Single-CPU only — no instantiation.
  Guard: never advertise >1 CPU to firmware (secondary-wait hang risk, #5).
- FDM: uncomment + initialize from the threaded settings; platform root now real.
- Banner / HWRPB systype derived from `system.model`.
- Platform JSON resolution: path = `EMULATR_PLATFORM_CONFIG` env, else optional
  `platformConfigPath` ini key, else derived `lower(model) + "_platform.json"`;
  resolved against the ini search paths. Missing file -> hard error (no silent
  fallback). Replaces the bare env-only load in `Machine.cpp`.
- Tests: FDM platform-root reflects settings; banner shows model; `model=DS10`
  resolves `ds10_platform.json`; missing manifest errors loudly.

### Slice D — cleanup (optional, per §5.4)
- Retire `OPA_Console_Config` `QSettings`; one config file, one reader.
- Doc the final precedence in `EmulatrV4.ini` header + this journal's "DONE" note.

---

## 7. Out of scope (explicit)

- Actual multi-CPU execution (cpuCount>1 instantiation) — value is plumbed, not exercised.
- Merging `ds10_platform.json` into the ini — kept separate by design (§4.7).
- Trace/Snapshot/Logging settings consumers — same pattern, follow-up once A–C land.
