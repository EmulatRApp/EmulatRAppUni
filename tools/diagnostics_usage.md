# EmulatR V4 Diagnostics -- Usage Guide

Operator-facing reference for the two diagnostic facilities introduced
2026-05-19: the centralized per-subsystem logging gate
(`coreLib/LogSubsystem.h`) and the physical-address disassembly dump
(`traceLib/PaDump.h`).  Companion to the `--snapshot-on-pc`,
`--inject-interrupt-at-cycle`, and `--trace` flags that already exist.

This guide answers four questions: what command-line flags exist, what
compile-time gates control them, how to recover from a stale snapshot
or stuck disassembly, and where each facility's output lands.

------------------------------------------------------------------------
## 1.  Compile-time gates  (CMakeLists.txt)

Two CMake `option()` switches gate the entire facility surface.  Both
default ON for Debug and RelWithDebInfo, and are forced OFF for
Release via `$<$<NOT:$<CONFIG:Release>>:...>`.  A production Release
binary therefore pays zero overhead.

| Option                          | Default | Effect when OFF                                                                   |
| ------------------------------- | ------- | --------------------------------------------------------------------------------- |
| `EMULATR_DIAGNOSTIC_LOGGING`    | ON      | `LOG_SUBSYS` / `LOG_SUBSYS_THROTTLED` macros become `((void)0)`; CLI accepts but ignores `--log-*` flags. |
| `EMULATR_PA_DUMP`               | ON      | `DUMP_PA` / `DUMP_DISASM` macros become `((void)0)`; CLI accepts but ignores `--dump-disasm`.            |

Override at configure time:

    cmake -DEMULATR_DIAGNOSTIC_LOGGING=OFF -DEMULATR_PA_DUMP=OFF ...

Mirrored onto `Emulatr_tests` so doctest cases see the same compile
state as the main executable.

------------------------------------------------------------------------
## 2.  --dump-disasm <pa>:<count>

Prints `count` Alpha instructions starting at physical address `pa`
to stderr, immediately after firmware load and snapshot autoload but
before the run loop begins.  Use to inspect bytes the about-to-run
CPU will fetch without capturing a full retire trace.

### Output format

    DEBUG: --dump-disasm pa=<pa> count=<count>
    # PaDump disasm: pa=<pa> count=<count>
    # format: PA=<hex16>  encoded=<hex8>  <primary-mnem>  <operands>
    PA=0x000000000060040c  encoded=0x6c1a1000  HW_LD      R00, 0x000(R26)
    PA=0x0000000000600410  encoded=0xe8800003  BLT        R04, +12
    ...

Primary mnemonic is the group name (HW_LD, BLT, INTA, ...).  Sub-leaf
mnemonics (ADDQ vs SUBQ inside the INTA group) are not resolved here
-- the encoded hex disambiguates for any reader who needs that.
Operand rendering matches the retire trace's `traceLib::Disassembler`
conventions.

### Cold-boot trap

If you run `--dump-disasm` on a fresh cold boot without a snapshot
present, the dump will show all-zero bytes for any PA that the
firmware has not yet written.  Firmware-decompressed PAL bytes at
0x600000+ are produced by the SRM decompressor only after several
million cycles of execution and an `HW_MTPR HW_PAL_BASE` to
relocate.  To inspect them, capture a snapshot first (see Section 5).

### Example

    Emulatr.exe --firmware firmware/es45_v7_3.exe \
                --dump-disasm 0x6003e0:32 \
                --max-cycles 1

Dumps 32 instructions at PA 0x6003e0, then exits after one retire.

------------------------------------------------------------------------
## 3.  Log subsystem flags

The eight diagnostic streams are individually toggleable.  Subsystem
names are case-insensitive.

| Subsystem        | Default throttle       | Where it fires                                                |
| ---------------- | ---------------------- | ------------------------------------------------------------- |
| `Cbox`           | LoudThenSummary        | mmuLib/CboxEventLog -- Cbox event counter                     |
| `Unalign`        | LoudThenSummary        | mmuLib/UnalignedEventLog -- synthetic UNALIGN-FIXUP events    |
| `IntervalTimer`  | LoudThenSummary        | Machine.cpp interval-timer fire and divert                    |
| `Snapshot`       | Unthrottled            | Snapshot save / load lifecycle                                |
| `PalRelocation`  | Unthrottled            | Step D PAL relocation triggers                                |
| `ChipsetCsr`     | LoudThenSummary        | chipsetLib CSR_LOG_R / CSR_LOG_W per-access                   |
| `StepD`          | Unthrottled            | Step D detect log (when relocation fires)                     |
| `Misc`           | LoudThenSummary        | Catchall for sites that haven't picked a category             |

### Throttle policies

| Policy             | Behavior                                                                |
| ------------------ | ----------------------------------------------------------------------- |
| `Unthrottled`      | every emit fires                                                        |
| `LoudThenSummary`  | first 16 emits loud, then one INFO heartbeat per 262,144 events         |
| `LoudThenMute`     | first 32 loud, then silent on stderr (file sink still receives)         |
| `Off`              | never emit (file sink still receives if configured)                     |

### Flags

| Flag                              | Effect                                                                 |
| --------------------------------- | ---------------------------------------------------------------------- |
| `--log-disable <subsys>`          | Disable one subsystem entirely.                                        |
| `--log-only <csv>`                | Disable every subsystem NOT in the comma-separated list.               |
| `--log-verbose <subsys>`          | Force throttle policy to `Unthrottled` for that subsystem.             |
| `--log-file <subsys>=<path>`      | Append every emit to the named file in addition to stderr.             |

Stack flags as needed; later flags override earlier ones for the same
subsystem.

### Examples

Investigate the interval timer without chipset CSR noise:

    Emulatr.exe ... --log-only IntervalTimer,Cbox --log-verbose IntervalTimer

Capture chipset CSR access to a file, leave everything else at default:

    Emulatr.exe ... --log-file ChipsetCsr=logs/chipset_csr.log

Silence the UNALIGN cascade entirely (useful when you know the
fixups are by-design and just clutter stderr):

    Emulatr.exe ... --log-disable Unalign

### File sinks

The file sink path is opened lazily on the first emit for that
subsystem.  The file is truncated on open and prefixed with one
comment header line.  No flush per event -- the OS buffers and the
destructor handles teardown.  On an unclean crash, the tail may be
lost; the stderr SPDLOG copy of the loud events is the backup.

------------------------------------------------------------------------
## 4.  Existing companion flags  (not new, included for cross-reference)

| Flag                                 | Purpose                                                                       |
| ------------------------------------ | ----------------------------------------------------------------------------- |
| `--snapshot-on-pc <pc>`              | One-shot predig snapshot trigger.  First retire at this PC writes a non-pruneable `predig_*.axpsnap` and disables auto-save. |
| `--snapshot-name-tag <s>`            | Tag inserted after `predig_` in the filename for the above.                    |
| `--inject-interrupt-at-cycle <n>`    | Experimental: one-shot synthetic INTERRUPT-class trap at the named cycle.      |
| `--trace <dec.log,machine.log>`      | Dual-channel retire trace.  Mask defaults to PAL_WINDOW + RETIRE_COMPACT.      |

Snapshot files land in `snapshots/` relative to the binary's working
directory.  `predig_` files are never pruned by the auto-save sweep
and serve as resume points for `autoloadLatest` on subsequent cold
starts.

------------------------------------------------------------------------
## 5.  Two-step pattern -- capture, then disassemble

The PAL bytes at 0x600000+ only exist in memory after the firmware
runs its decompressor.  A one-cycle `--dump-disasm` on cold boot
shows zeros there.  Pattern:

### Step 1.  Capture

    Emulatr.exe --firmware firmware/es45_v7_3.exe \
                --snapshot-on-pc 0x6003ec \
                --snapshot-name-tag spin_entry \
                --max-cycles 20000000

Look for these two stderr lines in order:

    Machine: predig snapshot armed at pc=0x00000000006003ec tag='spin_entry'
    ...
    Machine: predig snapshot fired at pc=0x00000000006003ec cyc=<N> -> 'snapshots\predig_spin_entry_cyc<N>.axpsnap' (success=1 bytes=68849371)

The 68 MB snapshot file is now in `snapshots/`.

### Step 2.  Disassemble

    Emulatr.exe --firmware firmware/es45_v7_3.exe \
                --dump-disasm 0x6003e0:32 \
                --max-cycles 1

Watch for the autoload line:

    Snapshot::autoloadLatest: selected 'snapshots\predig_spin_entry_cyc<N>.axpsnap' as newest

Followed by the dump output with real instruction bytes.

### Iterating

To capture a different region, change the `--snapshot-on-pc` value
and re-run Step 1 with a new `--snapshot-name-tag`.  Old `predig_*`
files stay on disk and `autoloadLatest` always picks the newest by
mtime, so a fresh capture displaces the old one without manual
cleanup.

------------------------------------------------------------------------
## 6.  Troubleshooting

### Dump shows all zeros after autoload

The autoload was confused -- check the `Snapshot::autoloadLatest`
line in stderr.  If it says "no `*.axpsnap`; cold boot", no snapshot
was found.  If a snapshot was loaded but the dump is still zeros,
the PA being dumped wasn't written before snapshot capture.  Pick a
later trigger PC and re-capture.

### Run hits MaxCyclesExceeded without snapshot firing

The trigger PC was never retired in `max-cycles` cycles.  Either
raise `--max-cycles` or pick an earlier PC.  For ES45 v7.3 firmware,
typical milestones:

| Cycle      | PC                | Event                                       |
| ---------- | ----------------- | ------------------------------------------- |
| ~4,194,400 | 0x6005c0          | Step D PAL relocation fires                 |
| ~5,242,886 | 0x6003ec          | First entry into the decompressor inner loop |
| 5,242,886+ | 0x6003ec / 0x6003f0 / 0x600400 / 0x600404 / 0x600408 | Steady-state decompressor spin |

### File sink path does not appear on disk

The path is relative to the executable's CWD, not the build dir.
If running from PowerShell at a different CWD, the file lands in
that CWD's resolution of the path.  Verify with `Get-Location`
before launch.  Parent directories ARE auto-created.

### Log flag errors out parse

Subsystem name typos fail loud.  Valid names are:
`Cbox`, `Unalign`, `IntervalTimer`, `Snapshot`, `PalRelocation`,
`ChipsetCsr`, `StepD`, `Misc`.  Case-insensitive.

------------------------------------------------------------------------
## 7.  References

- `coreLib/LogSubsystem.h`                 -- facility header, full API
- `coreLib/LogSubsystem.cpp`               -- implementation
- `traceLib/PaDump.h`                      -- disassembly dump header
- `traceLib/PaDump.cpp`                    -- implementation
- `chipsetLib/CsrDiag.h`                   -- pattern progenitor
  (chipset-only; will be migrated onto LogSubsystem in a future sweep)
- `systemLib/AppOptions.h` / `.cpp`        -- flag parsing
- `main.cpp`                               -- flag application sequence

------------------------------------------------------------------------
## 8.  Change history

- 2026-05-19  Initial draft.  Documents the LogSubsystem + PaDump
              facility scaffolding before the call-site sweep
              (UnalignedEventLog, CboxEventLog, chipsetLib CSR_LOG,
              Machine interval-timer SUPPRESSED throttle, Machine
              Step D detect log, Snapshot save/load DEBUG prints).
