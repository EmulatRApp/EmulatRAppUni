# EmulatR Environment Variables -- Operational Reference

Generated 2026-06-08 from the `std::getenv` call sites in
`EmulatRAppUniV4\Emulatr`. ASCII-only.

This document lists the **runtime** environment variables the emulator
reads at launch, plus the **compile-time** flags that look like env vars
but are actually CMake build options. CLI flags that mirror an env var
are cross-referenced.

---

## How they are read (important gotchas)

- All values are read with `std::getenv`. Set them in the launching
  shell **before** starting the process. Inline bash assignment is the
  normal form:

  ```bash
  EMULATR_CONSOLE_SNAPSHOT=1 ./out/build/relwithdebinfo/Emulatr.exe --firmware firmware/ds10_v7_3.exe
  ```

- **Read-once gotcha.** The trace/diagnostic gates
  (`EMULATR_CONSOLE_SNAPSHOT`, `EMULATR_IIC_TRACE`, `EMULATR_FLASH_TRACE`,
  `EMULATR_PIC_TRACE`, `EMULATR_TICKWARP`, the RPCC probe knobs) cache
  their result in a function-local `static bool const` on first call.
  They are effectively read once at process start; exporting them
  mid-run has no effect.

- **Presence vs value.** Most gates test only *presence*
  (`getenv(...) != nullptr`), so `VAR=0` still ENABLES them -- any value,
  including `0` or empty, counts as set. The exceptions that parse a
  value are called out below (`EMULATR_AUTOSNAP`, `EMULATR_SNAPSHOT_MARKER`,
  the path vars, and the RPCC numeric knobs).

- **Relative paths resolve against the launch CWD.** `EMULATR_STOP_FILE`
  default and the `snapshots/` dir are CWD-relative; launch from the
  Emulatr root for the canonical snapshot set.

---

## Snapshot / autoload

| Variable | Values | Default | Effect |
|---|---|---|---|
| `EMULATR_CONSOLE_SNAPSHOT` | presence | unset (off) | Arms the Option-A console-snapshot marker watch in the UART. When set, a console input line matching the marker writes `snapshots/predig_oemsnap_cyc<N>.axpsnap`. Read once at start. |
| `EMULATR_SNAPSHOT_MARKER` | string | `set oem_string snapshot` | The exact line that triggers the console snapshot. Leading/trailing whitespace is trimmed; **internal whitespace is NOT normalized** (type single spaces). |
| `EMULATR_AUTOSNAP` | `off` (else on) | on | Disables periodic `auto_*.axpsnap` saves. Mirrors `--autosnapshot off`. Use for cold-boot mint runs to avoid the per-save disk cliff. |
| `EMULATR_NO_AUTOLOAD` | presence | unset | Skip autoload-newest at startup; forces a genuine cold boot instead of restoring the newest snapshot. Mirrors `--no-autoload`. |
| `EMULATR_SNAPSHOT_PRUNE` | presence | unset (no prune) | Enables pruning of old snapshots. Without it, `pruneOldSnapshots` is a no-op and nothing is evicted. |

## Process / platform control

| Variable | Values | Default | Effect |
|---|---|---|---|
| `EMULATR_STOP_FILE` | path | `EMULATR_STOP` (CWD-relative) | Path of the graceful-stop sentinel. `touch` it to stop the run cleanly so `~Machine` flushes flash NVRAM. The resolved absolute path is logged at startup (`graceful-stop sentinel = ...`). Polled ~every 1M steps; consumed when seen. |
| `EMULATR_NO_PUTTY` | presence | unset | Do not auto-launch PuTTY for the TCP console backend. The listen socket still comes up; attach your own client. |
| `EMULATR_FLASH_ROM` | path | `ds10_flash.rom` | Backing file for the flash/NVRAM image (persisted config: env vars, `update srm` heal). |
| `EMULATR_PLATFORM_CONFIG` | path | built-in DS10 manifest | Override the platform manifest (IIC device population, etc.). |

## Logging / diagnostics (runtime gates)

| Variable | Values | Default | Effect |
|---|---|---|---|
| `EMULATR_CHIPSET_DIAG_OFF` | presence/non-empty | unset (diag on) | Silences the chipset CSR diagnostic stream. Note the inverted sense: setting it turns diagnostics OFF. |
| `EMULATR_TRACE_WINDOW` | presence | unset | Forces the DecListingSink retire-compact trace window even without the `TRACE_RETIRE_COMPACT` mask bit. |
| `EMULATR_RETIRE_TRACE_DIR` | dir path | build-tree default | Output directory for retire-compact / breakpoint / profiler trace files. |

## Device traces (per-subsystem, read once)

| Variable | Values | Default | Effect |
|---|---|---|---|
| `EMULATR_IIC_TRACE` | presence | unset | Trace PCF8584 IIC bus transactions. |
| `EMULATR_FLASH_TRACE` | presence | unset | Trace FlashRom command/erase/write activity. |
| `EMULATR_PIC_TRACE` | presence | unset | Trace the 8259 PIC pair (IRQ assert/EOI). |

## Performance / experimental

| Variable | Values | Default | Effect |
|---|---|---|---|
| `EMULATR_IDLEWARP` | presence | unset | **#2 fast-forward (clean).** When the CPU idles in the `krn$_idle` loop (PC ~`0x7bad8`-`0x7bb10`) and can accept the interval timer, advances `cycleCount` to the NEXT single tick edge -- the real ISR then increments `0x3c970`. No out-of-band counter rewrite, so counter-polling loops stay coherent. Collapses the post-GCT/dva0/gap C970 tick-waits. Faithful: emulated time advances by exactly the skipped amount. |
| `EMULATR_RSCCWARP` | presence | unset | **QUARANTINED (do not use for real boots).** The old `EMULATR_TICKWARP` warps (tick-delay `0x7c314`, RSCC-deadline `0x7c304`, spin `0x1c655c`) -- they jump many ticks at once AND rewrite `0x3c970` out-of-band, the confirmed cause of the 0x7f4xx boot corruption. Kept gated for forensic comparison only. |
| `EMULATR_TICKWARP` | presence | unset | **DEPRECATED / no-op** as of 2026-06-12 -- its warps were split into `EMULATR_IDLEWARP` (clean) and `EMULATR_RSCCWARP` (quarantined). Setting it now does nothing. |
| `EMULATR_RPCC_LOG_AFTER` | uint (dec/hex) | `185000000` | Cycle count after which the RPCC probe begins logging. |
| `EMULATR_RPCC_LOG_MAX` | uint (dec/hex) | `200000` | Max RPCC probe log entries. |
| `EMULATR_RPCC_LOG_FILE` | path | `D:\EmulatR\EmulatRAppUniV4\rpcc_probe.txt` | RPCC probe output file. |

## Probe / debug gates

| Variable | Values | Default | Effect |
|---|---|---|---|
| `EMULATR_GCT_WATCH` | presence | unset | Store-watch on the GCT/FRU config-tree region (PA `0x3f32000`-`0x3f33fff`). |
| `EMULATR_BREAK_ON_PA10` | presence | unset | Conditional break on accesses to PA `0x10`-`0x17`. |
| `EMULATR_CHECKPOINTS` | spec string | unset | Named PC checkpoints, e.g. `shcreate:0x...,shentry:0x...,isatty:0x...,rwp:0x...`. |
| `EMULATR_GATE` | spec string | unset | PC/condition gate spec controlling trace windowing. |

---

## Compile-time flags (NOT runtime env vars)

These appear as `EMULATR_*` names but are CMake options / preprocessor
defines baked in at build time. Setting them in the shell does nothing;
they must be enabled in the build.

| Flag | Where | Notes |
|---|---|---|
| `EMULATR_MEMDIAG` | `CMakeLists.txt:263` (commented out) | Per-retired-instruction `fprintf+fflush`. ~900x slowdown; keep OFF. Disabled 2026-06-01. |
| `EMULATR_DIAGNOSTIC_LOGGING` | `CMakeLists.txt:315` option | Centralized per-subsystem diagnostic logging; defined only in non-Release configs. Mirrored onto the test target. |
| `EMULATR_PA_DUMP` | `CMakeLists.txt` (test mirror) | PA-access dump; build define. |
| `EMULATR_LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` | logging macros | spdlog level selection macros, not runtime env. |

---

## CLI flags that mirror env vars

| CLI | Env equivalent |
|---|---|
| `--autosnapshot off` | `EMULATR_AUTOSNAP=off` |
| `--no-autoload` | `EMULATR_NO_AUTOLOAD=1` |
| `--max-cycles <N>` | (no env; default unlimited = `~uint64_t{0}`) |
| `--snapshot-on-pc <pc>[,...]` | (no env) |
| `--snapshot-name-tag <s>` | (no env) |

---

## Operational recipes

**Reach `>>>` and capture diagnostics, interactive (PuTTY console live):**

```bash
cd /d/EmulatR/EmulatRAppUniV4/Emulatr
EMULATR_CONSOLE_SNAPSHOT=1 ./out/build/relwithdebinfo/Emulatr.exe \
  --firmware firmware/ds10_v7_3.exe --autosnapshot off  2> run.log
```

**Same, fully detached (interaction is via the PuTTY/TCP console, not the shell):**

```bash
cd /d/EmulatR/EmulatRAppUniV4/Emulatr
EMULATR_CONSOLE_SNAPSHOT=1 nohup ./out/build/relwithdebinfo/Emulatr.exe \
  --firmware firmware/ds10_v7_3.exe --autosnapshot off  > run.log 2>&1 &
disown; echo "pid=$!"
```

**Stop a running instance gracefully (flushes flash NVRAM):**

```bash
grep graceful-stop run.log          # find the exact sentinel path it watches
touch /d/EmulatR/EmulatRAppUniV4/Emulatr/EMULATR_STOP
```

**Genuine cold boot (no snapshot restore), quiet, for profiling:**

```bash
EMULATR_NO_AUTOLOAD=1 ./out/build/relwithdebinfo/Emulatr.exe \
  --firmware firmware/ds10_v7_3.exe --autosnapshot off  2> /dev/null
```

**Enable a single device trace (IIC bus, e.g. for the FRU/EEPROM path):**

```bash
EMULATR_IIC_TRACE=1 ./out/build/relwithdebinfo/Emulatr.exe \
  --firmware firmware/ds10_v7_3.exe  2> iic.log
```

---

*Maintenance: regenerate by grepping `getenv` across
`EmulatRAppUniV4\Emulatr` and diffing against the tables above. New
runtime gates should be added here and noted in their commit.*
