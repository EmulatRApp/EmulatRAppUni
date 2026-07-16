<!--
running_from_run_dir.md -- EmulatR V4 operator note.
Project: EmulatR -- Alpha AXP / EV6 (V4).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
ASCII(128) only.
-->

# Running EmulatR From a Run Directory (tools/ model)

Operator-facing note for how the helper shell scripts are packaged and run.
Companion to `diagnostics_usage.md`.

## The model

- `<project>/tools/` is the SOURCE OF TRUTH -- the committed repository of
  run and diagnostic shell scripts covering the execution permutations
  (models ds10/ds20/ds25/es40/es45 x configs x trace modes).
- On EVERY build, CMake copies `tools/*.sh` and `tools/*.py` into the run
  directory at `<project>/out/build/<config>/tools/`.  The copy is
  GLOB-driven with CONFIGURE_DEPENDS, so a newly added script deploys on the
  next build with no hand-editing.
- You RUN from the run directory's copy:

      <project>/out/build/<config>/tools/<script>.sh

  The run directory (`out/build/<config>/`, the folder that holds Emulatr.exe)
  is the runtime root: firmware/, config/, logs/, traces/, snapshots/ and the
  platform manifests all live there.

## Why you can run it from anywhere

Every runtime script sources `tools/_emulatr_runenv.sh`, which self-locates the
run directory from the script's OWN path (via BASH_SOURCE) -- never a hardcoded
or source-tree path.  It then `cd`s into the run directory and pre-creates
`logs/ traces/ snapshots/`.  Because the C++ binary also writes relative to its
current directory, all output lands under the run directory automatically.

The upshot: the current working directory does not matter.  These are
equivalent, and both write only under `out/build/<config>/`:

      cd out/build/relwithdebinfo && ./tools/run_ds10_trace.sh
      /any/where $ /full/path/out/build/relwithdebinfo/tools/run_ds10_trace.sh

## Distributing a build (download, unzip, run)

A distributed artifact is the run directory itself -- zip
`out/build/<config>/` (Emulatr.exe + Qt DLLs + firmware/ + config/ + the
platform manifests + tools/).  The end user:

1. Unzips it anywhere.
2. Opens `tools/` and picks a script (e.g. `run_ds10_trace.sh`).
3. Runs `./tools/run_ds10_trace.sh` (Git Bash on Windows).

No source tree is required.  The scripts use only run-dir-relative paths;
firmware and manifests are the copies already shipped in the run directory.
(`emu_refresh_asset` refreshes them from a source tree ONLY when one is present,
which is the developer case; in a distributed run-dir it is a no-op.)

## Where output lands

Relative to the run directory (`out/build/<config>/`):

- `logs/`      -- run and console logs (`<purpose>_YYYYMMDD_HHMMSS.log`)
- `traces/`    -- retire / dec / machine / .trc traces
- `snapshots/` -- .axpsnap checkpoints
- `firmware/`  -- the v7_3 images the run boots
- `config/`    -- EmulatrV4.ini (model is set per run and restored on exit)

## Environment knobs

- `RUN_DIR=<dir>` -- force the run directory (overrides self-location).
- `CONFIG=relwithdebinfo|debug|release` -- steers ONLY the developer fallback
  used when a script is run from a source checkout instead of a shipped
  run-dir; ignored once a shipped run-dir is found.

## Adding a new script

1. Drop the `.sh` in `<project>/tools/`.
2. Give it the standard opening so it self-locates:

       set -euo pipefail
       SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
       source "$SELF_DIR/_emulatr_runenv.sh"     # sets RUN_DIR, BIN, HOST; cd's in

3. Use only run-dir-relative paths for output (logs/, traces/, ...).
4. Rebuild -- CMake auto-deploys it to `out/build/<config>/tools/`.
