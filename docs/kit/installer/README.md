# EmulatR Kit -- Installer Definition

Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.

This directory versions the SOURCE of the Windows installer, not its
output.  Built `*-setup.exe` kits are gitignored (repo-wide `*.exe`
rule) and may sit here as local artifacts only -- the ds10_flash.rom
history purge is the standing lesson on large binaries in git.

## Contents

| File | Role |
|---|---|
| `EmulatR-V5.suf` | The Setup Factory project (XML).  Sources ONE folder recursively: `D:\EmulatR\dist\ASA-EmulatR`. |
| `icons/*.ico` | The product icon family (also embedded in the exes via `resources/version.rc.in`). |

## The kit pipeline (one straight line)

```bash
tools/build_emulatr.sh release      # builds Emulatr + syncs H&M version
                                    # + runs tools/make_dist.sh
# (launcher: cmake --build EmulatR-App/out/build/release, windeployqt runs
#  POST_BUILD; make_dist sweeps it in)
```

1. `tools/make_dist.sh` assembles `D:\EmulatR\dist\ASA-EmulatR` -- the
   exact installed layout, runnable in place for pre-MSI testing.
   It scrubs pdbs/build droppings, neutralizes the dev `diskDir` in the
   shipped `Emulatr.ini`, stages the empty release env envelope, and
   ships `firmware\` EMPTY (SRM images are user-supplied; licensing).
2. Open `EmulatR-V5.suf` in Setup Factory; it packages that one dist
   folder recursively.  Install target (fixed, by design):
   `C:\Program Files\eNVy Systems, Inc\ASA-EmulatR`.
3. Build the setup exe / MSI; test-install; per policy testers UNINSTALL
   an existing kit before installing a new one (see the H&M topic
   "Installing with the Windows Installer").

Per-file destination rules inside the .suf are deliberately avoided --
the dist folder's structure IS the install layout, which is what made
the 2026-08-14 Qt-plugin flattening class of error structurally
impossible.
