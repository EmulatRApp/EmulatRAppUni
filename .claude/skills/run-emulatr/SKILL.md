---
name: run-emulatr
description: >-
  Build, launch, boot, test, and DRIVE the EmulatR Alpha/EV6 emulator
  (Emulatr.exe, V5 tree) fully scripted -- no human at the console needed.
  Use for any request to run/start/boot EmulatR or Emulatr.exe, reach or
  drive the SRM `>>>` / `P00>>>` prompt, boot a disk (b dka0/dqa0), run a
  diagnostic capture (EMULATR_DIAG_* / PA_WATCH / retire trace), take a
  post-halt snapshot, or run the doctest suite. The agent path is the
  committed telnet console driver tools/srm_console_driver.py -- headless
  boots to a PASS/FAIL verdict work and are the normal way to test.
---

# run-emulatr -- build, boot, and drive EmulatR (V5)

EmulatR is a DEC Alpha (EV6/21264) system emulator (Windows/MSVC + Qt).
Its interactive surface is the **SRM serial console: a single-client TCP
server on port 10023**. The agent path drives that console with the
committed scripted driver `tools/srm_console_driver.py` (telnet client
with the firmware's quirks encoded); the human path attaches PuTTY.
All paths below are relative to the repo root (`emulatrappuniv5/`).
All commands are bash (Git Bash) per house convention.

**FIRST, EVERY SESSION:** `git branch --show-current` -- boot/dev work
lives on `v5-tb`; concurrent sessions share this tree ("missing files"
usually = branch switch). Read `memory.md` Sec 7 for the live frontier.

## Prerequisites (Windows host -- the only verified platform)

- Visual Studio 2022 + CMake arrive via `tools/vsenv.sh`; never call
  `cmake` from a bare shell -- `tools/build_emulatr.sh` sources vsenv
  itself.
- Python 3 on PATH (3.13 verified) for the console driver and analysis
  tools.
- PuTTY on PATH is optional (human path only) -- and see Gotcha #1.

## Build

```bash
bash tools/build_emulatr.sh relwithdebinfo
```

Builds and mirrors the artifact to `out/build/relwithdebinfo/Emulatr.exe`.
Use `relwithdebinfo` (or `debug`) for any diagnostic run -- the
`EMULATR_DIAG_*` retire-time facility is compiled OUT of `release`.
The default target is **Emulatr only**; the test exe is a separate target:

```bash
TARGET=Emulatr_tests bash tools/build_emulatr.sh relwithdebinfo
```

(Skipping this after editing tests re-mirrors a STALE Emulatr_tests.exe
with a fresh timestamp -- Gotcha #4.) After a pull that touches runtime
code, confirm a needed facility is really in the binary before a long
run: `grep -a -c EMULATR_DIAG_PCLO out/build/relwithdebinfo/Emulatr.exe`
(expect >= 1).

## Run (agent path) -- launcher + scripted console driver

Terminal A (or `run_in_background`): the launcher. It exports the
boot-path env stack (EMULATR_2D_NOOP, CSERVE_START_MODE=guest, ROUTE,
DIVERT_PALSWAP -- see `tools/run_ds20_bplus.sh` header), pins CWD to the
newest build run dir, passes `--no-autoload --autosnapshot off`, and
tees everything to a timestamped `run_ds20_showdev_<ts>.log` in the run
dir:

```bash
bash tools/run_taskboot001_phase1.sh
```

Terminal B, ~30 s later: the driver. It waits for `P00>>>` (clearing
the firmware's LFU auto-entry prompts on the way), types the boot
command, and exits 0 when `--expect` appears / 1 on timeout or the
halt-0 wall / 2 if it cannot connect:

```bash
python tools/srm_console_driver.py \
    --boot "b dka0.0.0.8.0 -flags 0" --expect "halt code = 10" --timeout 1200
```

`--expect` is the current pass criterion -- as boot bringup advances,
take it from the live frontier in `memory.md` Sec 7 (as of 2026-07-26:
`halt code = 10` at PC 2a000 is the frontier; `NOIOVEC` was the old wall,
now fixed). Cold boot to `P00>>>` takes ~2-3 min of firmware init
(memory test); the boot itself ~1 min more. Cycle counts vary a few
percent between runs -- never key a probe to an exact cycle from a
previous run.

Diagnostic knobs are env vars prefixed onto the launcher, e.g.:

```bash
EMULATR_DIAG_PCLO=0x2000a000 EMULATR_DIAG_PCHI=0x20099400 \
EMULATR_DIAG_CAP=20000000 bash tools/run_taskboot001_phase1.sh
```

- `EMULATR_DIAG_PCLO/PCHI/CAP/CYCLO/CYCHI` -- retire-window trace
  (DIAG-PC lines on stderr -> the run log).
- `EMULATR_DIAG_WREG=<n>` + optional `WMIN` -- log writes to integer
  register n (gated by the same PC window).
- `EMULATR_PA_WATCH=<pa>` + `EMULATR_PA_WATCH_LEN=<bytes>` -- log every
  store to a physical range (who-writes-this-cell probes).
- Post-halt snapshot: the launcher sets `EMULATR_CONSOLE_SNAPSHOT=1`;
  typing `set oem_string snapshot` at the post-halt `P00>>>` writes
  `snapshots/predig_oemsnap_cyc<N>.axpsnap` (~4 GiB, independent of
  `--autosnapshot off`). Script it by extending the driver pattern --
  see the marker logic in `tools/srm_console_driver.py`.

Offline analysis of a captured run (all verified):

```bash
python tools/t1_apb_trace_analyze.py <log-or-trace> --summary --calls --gaps
python tools/snap_ptwalk.py snapshots/<snap>.axpsnap --ptbr 0x3ff04000 <va>...
python tools/snap_va_disasm.py snapshots/<snap>.axpsnap <va> <count> [--raw-pa]
python tools/apb_stream_decode.py snapshots/<snap>.axpsnap --ctx
```

## Direct invocation (no launcher)

The launcher is a thin env-stack + logging wrapper over:

```bash
cd out/build/relwithdebinfo && \
EMULATR_2D_NOOP=1 ./Emulatr.exe --firmware firmware/ds20_v7_3.exe \
    --no-autoload --autosnapshot off --max-cycles 999000000000
```

`EMULATR_2D_NOOP=1` is REQUIRED for bare DS20 boots (without it p_temp
is never built and the guest resets into the LFU). Model selection:
`config/Emulatr.ini` `model=` must match the `--firmware` stem. Prefer
the launcher unless you need a custom stack.

## Run (human path)

Attach PuTTY to the console: `putty -telnet localhost 10023` (or let
the auto-launch raise it). One client only -- see Gotcha #1. Type at
`P00>>>` directly. Useless for unattended verdicts; use the driver.

## Test

```bash
cd out/build/relwithdebinfo && ./Emulatr_tests.exe            # full suite
./Emulatr_tests.exe --test-case="*byteops*"                   # filtered
```

Full suite: 495 cases; **3 failures are pre-existing drift**
(ide_wiring + 2x mmio_csc, JRN-SCSI-003) -- 492/495 = clean. doctest
filter syntax needs the `*globs*`.

## Gotchas (all hit live)

1. **The console is single-client and PuTTY races the driver.** PuTTY
   may auto-launch with the emulator (same-second start) and steal the
   slot; the loser spins on "SRM Console: Rejected connection". If the
   driver logs endless reconnects, kill the auto-launched PuTTY (or the
   stale one from a previous run -- it outlives the emulator). Set
   `EMULATR_CONSOLE_PORT=<n>` to sidestep a held port.
2. **A previous emulator instance holds port 10023.** A new launch then
   dies with a startup dump; the driver connects to the OLD instance.
   `tasklist //FI "IMAGENAME eq Emulatr.exe"` first; stop stale
   instances before relaunching.
3. **The firmware auto-enters the LFU during init** -- sometimes twice
   -- and shows a transient `P00>>>` a boot command would be swallowed
   by. The driver handles both (clears the update prompts, only boots
   when the buffer ENDS at the prompt, re-sends after `--resend`).
4. **Stale-binary traps.** `build_emulatr.sh` builds only `Emulatr` by
   default -- `Emulatr_tests.exe` gets re-MIRRORED with a new timestamp
   but old code. Test-source or CMakeLists edits need
   `TARGET=Emulatr_tests`.
5. **DIAG cap fills at the FRONT.** `EMULATR_DIAG_CAP` keeps the first
   N records; a hot early loop (e.g. APB's page-bitmap build at
   0x200098xx, ~5M retires) can eat the whole budget before the code
   you care about. Narrow PCLO/PCHI or gate with DIAG_CYCLO.
6. **stderr diag lines and console output interleave in one log.**
   `grep -a` (logs contain binary bytes), and filter by line prefix
   (`^DIAG-PC`, `^\[CON`, `PA-WATCH`).
7. **Cowork/sandbox FUSE mounts truncate reads of this repo.** Run git
   and file-integrity checks on the native OS only (repo CLAUDE.md).

## Troubleshooting

- `FATAL: cmake not on PATH` -> you called cmake directly; use
  `tools/build_emulatr.sh` (sources vsenv).
- Driver exits 2 -> emulator not up yet (give the launcher ~30 s) or
  wrong port.
- Driver TIMEOUT but the log shows the expected text -> a second client
  held the console so the driver never saw output; see Gotcha #1.
- `halted CPU 0 / halt code = 0 / PC = 20000000` right after boot ->
  the boot-path env stack is missing (bare launch without
  CSERVE_START_MODE etc.); use the launcher (JRN-SCSI-010).
- Run log has zero DIAG lines despite knobs -> release build (facility
  compiled out) or the PC window never matched; check the binary grep
  from the Build section.
