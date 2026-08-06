# SPEC-LAUNCH-001 -- Gate evidence note

    Project   : EmulatR -- EmulatrLaunch
    Artifact  : SPEC-LAUNCH-001-GATE-EVIDENCE.md
    Status    : DRAFT -- evidence gathered, awaiting Tim's sign-off
    Date      : 2026-07-29
    Author    : Claude, from a core-tree read during the v1 implementation pass
    Spec      : SPEC-LAUNCH-001 Rev D.2, Section 13 (Gates)
    Audience  : Tim (sign-off)

This note records what the CORE TREE already says about the gates the spec
opened. It closes nothing by itself -- gates close when Tim signs them. What it
does is remove the assumption that G1/G2/G2a/G3 need new core work: three of
the four are answerable from code that is already there, and the fourth is a
census that only Tim can tier.

All citations are to `D:\EmulatR\emulatrappuniv5`.

--------------------------------------------------------------------------

## G1 -- PATH RESOLUTION AUDIT

**Finding: cwd-relative resolution is FIRST, but an exe-dir fallback exists.**

`config/Emulatr.ini` documents `configLib::IniLoader`'s search order verbatim:

    ;   1. ./Emulatr.ini
    ;   2. ./config/Emulatr.ini
    ;   3. <exe-dir>/Emulatr.ini
    ;   4. <exe-dir>/config/Emulatr.ini

Steps 1-2 are cwd-relative, which is what R1 needs. Steps 3-4 are the hazard
the spec warns about: with the launcher's cwd set to a run directory that has
NO `Emulatr.ini`, the emulator would silently fall back to the copy inside the
read-only install directory and run a configuration the tester cannot see.

Firmware and storage resolve relative as well:

* `[ROM] firmwareImage = firmware/ds20_v7_3.exe` -- relative, run-dir-relative
  in practice.
* `[Storage] diskDir` -- "Leave diskDir empty to resolve filenames against the
  launch CWD"; the seeded template sets `diskDir = disks`.

**Position taken in the implementation:** the launcher guarantees step 1 always
resolves, by seeding `Emulatr.ini` at system creation (R3) and failing preflight
when it is missing. The exe-dir fallback is therefore unreachable through the
launcher. Whether the fallback should exist AT ALL in the core is a separate
call and is left to you -- it is a real footgun for anyone launching by hand.

**Not verified:** where the flash `.rom` write-back resolves. `Machine.cpp:451`
and `:1004` consult `EMULATR_FLASH_ROM`, but the default path was not traced.
That is the one G1 sub-question still open, and it matters most of all, since a
flash file resolving exe-relative would land in Program Files.

--------------------------------------------------------------------------

## G2 -- INVOCATION CONTRACT

**Finding: the tester-path contract is nearly empty, which is the good outcome.**

Argument parsing is `systemLib::AppOptions::parse` (`main.cpp:123`). The
options that exist include `--firmware`, `--firmware-format`, `--load-pa`,
`--start-pa`, `--mem`, `--max-cycles`, `--trace`, `--snapshot-on-pc`,
`--autosnapshot`, `--snapshot-name-tag`, `--inject-interrupt-at-cycle`,
`--dump-disasm`, `--log-disable`, `--log-only`, `--log-verbose`, `--log-file`,
`--no-autoload`, `--pal-mode`, `--help`.

None of those is REQUIRED. Everything the launcher needs comes from
`Emulatr.ini` in the working directory. So the launcher invokes:

    "<abs path>\Emulatr.exe" --firmware firmware/<selected image>

with `workingDirectory` = the run directory, and nothing else.

`--firmware` is passed rather than left to the ini because the core's
precedence is **CLI > ini** (`main.cpp:183-195`): passing it makes the
launcher's W2 selection authoritative and means a stale `[ROM] firmwareImage`
can never silently win. Firmware is REQUIRED -- with neither source set, the
core exits 2 with "no firmware".

**Platform selection reaches the emulator by ini key**, not by argument:
`[System] model` selects `<lower(model)>_platform.json`. Note the dated comment
at `config/Emulatr.ini:44-50` recording that `model` is NOT the full master
switch the section header claims -- an explicit `firmwareImage` is still
required, and a model/firmware mismatch is a live incoherence that "spins in
PAL (PCSAMPLE 0x113xx), OPA0 never starts". The launcher treats that mismatch
as a preflight FAILURE for exactly this reason.

### Console binding keys (W3)

    [SRMConsole]
    port             = 10023      ; "OPA0 console TCP server. PuTTY connects here."
    rxBufferSize     = 4096
    defaultTimeoutMs = 30000
    echoEnabled      = true
    autoLaunchPutty  = false
    puttyPath        = putty.exe
    puttyExtraArgs   =

The listener EXISTS in the core today; it is not new work. `EMULATR_CONSOLE_PORT`
overrides the ini at launch (`systemLib/Machine.cpp:325`).

**Gap: there is NO bind-address key.** The spec's "bind address defaults to
127.0.0.1, exposing beyond localhost is an explicit per-system opt-in" has no
core surface to write to. The implementation therefore ships the opt-in
checkbox DISABLED with a note saying so, rather than writing a key the emulator
would ignore -- a silently-ignored setting is exactly the "silently-wrong
state" this launcher exists to prevent. **This is a core-tree task if you want
the opt-in.**

Listener semantics (telnet vs raw) were not established from the source; the
launcher connects PuTTY with `-telnet`, following the core's own
`autoLaunchPutty` convention. Worth confirming.

--------------------------------------------------------------------------

## G2a -- SHUTDOWN CHANNEL  (the significant finding)

**Finding: the channel already exists, and it is candidate (c).**

`systemLib/Machine.cpp:1249-1272`:

    // Graceful-stop sentinel (2026-06-06, task #9).  Polling a file on a
    // coarse cadence lets a background (&) run be stopped cleanly from bash
    // -- `touch <sentinel>' -- so run() returns and ~Machine's forceFlush()
    // persists the flash NVRAM (an `update srm' heal or env `set').  A hard
    // taskkill /F skips the destructor and loses the heal.

Mechanics: `Machine::run()` resolves the sentinel once per run --
`$EMULATR_STOP_FILE` if set, else `EMULATR_STOP` in the CWD -- pre-clears it so
a stale file cannot stop the new run, logs the resolved absolute path, and
polls for it in the per-cycle body (`stepCycle`).

**This satisfies the Rev D.1 SELECTION CONSTRAINT with no core change.**
Creating a file is something a session-0 service wrapper can do exactly as
easily as an interactive launcher, so the deferred run-as-a-service host drives
the identical channel. A console-control-event mechanism could not have been
driven that way. D.1 favored (c); (c) turns out to already be built.

Implementation consequences, all already in `EmulatorProcess.cpp`:

* Stop writes `{run-dir}\EMULATR_STOP` (or `$EMULATR_STOP_FILE` when the
  composed environment sets it, so the launcher and the core cannot disagree).
* `QProcess::terminate()` is never called. It posts WM_CLOSE, which a
  console-subsystem process never receives -- it would look like a clean stop
  while doing nothing.
* The sentinel is removed before start AND after exit; a leftover file is
  invisible to a tester and would stop the next run the instant it began.
* Force Stop is `kill()`, offered only after the visible timeout, logged as
  forced, never automatic.

**Open question for you:** the poll is "on a coarse cadence" in the per-cycle
body. If the guest is halted or spinning in a way that does not reach that
body, the sentinel may not be noticed and the escalation path is the only exit.
The 10 s default timeout is a guess at what "coarse" means in wall-clock terms
and should be checked against the real cadence.

--------------------------------------------------------------------------

## G3 -- CONTAINER FORMAT

**Finding: decided, and it is the simplest possible format.**

`deviceLib/scsi/FileBlockMedia.h:15-23`:

    // Raw flat-image backing (approved 2026-06-12).  Serves BOTH roles:
    //   - ATA fixed disk: blockSize 512, read-write.
    //   - ATAPI ISO-9660: blockSize 2048, read-only
    // Offset = lba * blockSize.

Its own `create_if_missing` path (`:41-66`) makes a file of exactly
`createBytes`, notes that "the file reads back as zeros (a blank install
target)", uses `resize_file` so a sparse-capable filesystem does not
pre-allocate, and states that "An existing file is NEVER overwritten."

`DiskImageFactory` reproduces exactly that: a file of `total_lbn * block_bytes`
bytes, no header, created by `QFile::resize` (the Win32 equivalent of
`resize_file` -- instant, allocated-but-uninitialized, reads as zeros), and a
refusal rather than an overwrite. Verified by test: the first 64 bytes of a
created container are zero, and the size matches to the byte.

### The 28-drive table

`config/dec_disk_media_types.tsv` holds exactly 28 geometry rows (12 RZ SCSI,
10 RA SDI, 4 RF DSSI, RC25 LESI, ESE20) -- the "verified 28-drive DEC table"
the spec names. `resources/data/dec_drive_geometries.tsv` is a **verbatim copy
of the geometry section only**, with provenance recorded in its header. The
device-name-prefix section of that file is not consumed here, and the parser
skips any row whose interface token is not one of scsi/sdi/dssi/lesi so it
cannot leak in as bogus drives.

If the core table changes, re-copy it. Do not hand-edit the copy.

--------------------------------------------------------------------------

## G2b -- ENVIRONMENT VARIABLE CENSUS  (NOT closed, and deliberately so)

**Measured scale: ~140 `getenv("EMULATR_*")` call sites** across coreLib,
chipsetLib, deviceLib, pipelineLib, mmuLib, memoryLib, palBoxLib, systemLib,
traceLib, and main.cpp.

That is a count, **not a classification**. G2b requires file/line anchors,
value kind, effect, and **your sign-off on the tiering** -- and tiering is the
part that cannot be delegated. Calling a knob "safe" without evidence of what
it does is precisely the failure the gate exists to prevent, and roughly a
third of these live in the pipeline and PAL paths where the worst case is
silent execution divergence rather than disk usage.

So `resources/data/emulatr_env_registry.tsv` ships with **one row**:
`EMULATR_RSCCWARP`, tier `denied`, which is transcribed from the spec itself
(Section 7.1 V2 names it as the founding denied member, "confirmed boot
corruption"). Shipping that one row means the **stripping path is live and
tested from day one**: the test suite proves that a variable set in the
launcher's own inherited environment is removed from the child's.

The panel renders only census output, so it currently shows a plain
explanation instead of an empty table.

**What is already built and waiting for the data:**

* tier semantics (safe / dev / denied), with unknown tiers failing CLOSED to
  denied and saying so;
* the "Show developer variables" toggle with its one-line caution;
* per-kind value editors (flag / path / integer / enum);
* per-SYSTEM persistence keyed by GUID, so selections survive rename and
  relocation;
* absent-is-not-empty composition (an unchecked variable is removed, never set
  to "");
* the mirror-log head recording the effective set and anything stripped.

Adding a censused variable is a one-line TSV edit. No code change.

--------------------------------------------------------------------------

## Items still genuinely open

1. **G2b tiering** -- needs you. Everything else about W6 is built.
2. **The bind-address key (W3)** -- no core surface exists. Core task if wanted.
3. **`docs/launcher_wireframe.qml` is absent from the tree.** Section 10 calls
   it NORMATIVE. The implemented layout was built from the Section 10 prose and
   the D1-D4 deviations alone and needs a reconciliation pass against the mock.
4. **Flash `.rom` default path (G1 remainder)** -- see above.
5. **Console poll cadence vs the 10 s escalation default (G2a remainder).**
6. **`SPEC-LAUNCH-001.md` sits at the project root**, but Section 3 places it at
   `docs/SPEC-LAUNCH-001.md`. Left where it is rather than moved without asking.
7. **`traces/` vs `trace/`** -- the spec's run-dir contract says `traces/`; the
   core's shipped ini defaults to `trace/cpu_trace.log`. The seeded template
   writes `traces/...` so the two agree inside a launcher-created run dir, but
   a hand-made dev run dir will differ.
8. **PuTTY and the launch discipline.** The seeded template sets
   `autoLaunchPutty = false` on the grounds that the launcher owns console
   launch via Open Console. If the house rule is that a console window must
   always be raised on every launch, then Start should auto-invoke Open
   Console -- say the word and it is a two-line change.

-- end of gate evidence note --
