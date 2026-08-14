#!/usr/bin/env bash
# ============================================================================
# tools/make_dist.sh -- assemble the installer staging tree (SSOT layout)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  ASCII(128) only.  2026-08-14.
#
# Builds D:\EmulatR\dist\ASA-EmulatR as an exact image of the installed
# layout (C:\Program Files\eNVy Systems, Inc\ASA-EmulatR).  Setup Factory
# sources this ONE directory recursively -- the folder structure IS the
# install layout, so per-file destination errors (the Qt-plugin flattening
# of 2026-08-14) are structurally impossible.  The tree is runnable in
# place for pre-MSI testing.
#
# Whitelist by construction: build droppings (ninja/CMake files, tests,
# pdb), vStorage, logs, traces, snapshots and dev tools never enter.
# firmware/ ships EMPTY: SRM images are user-supplied (copyright posture).
#
# Usage:  tools/make_dist.sh          # assembles from the release configs
# ============================================================================
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$SELF_DIR/.." && pwd)"                     # EmulatRAppUniV5
EMU="$SRC/out/build/release"                          # emulator run tree
LAU="$SRC/EmulatR-App/out/build/release"              # launcher payload
DIST="/d/EmulatR/dist/ASA-EmulatR"

[[ -x "$EMU/Emulatr.exe" ]]        || { echo "FATAL: $EMU/Emulatr.exe missing -- build release first"; exit 1; }
[[ -x "$LAU/EmulatrLaunch.exe" ]]  || { echo "FATAL: $LAU/EmulatrLaunch.exe missing -- build launcher release first"; exit 1; }

echo "=== make_dist: staging $DIST ==="
rm -rf "$DIST"
mkdir -p "$DIST"

# ---- emulator core ---------------------------------------------------------
cp "$EMU/Emulatr.exe" "$DIST/"
for d in config documentation tls networkinformation; do
    [[ -d "$EMU/$d" ]] && cp -r "$EMU/$d" "$DIST/"
done
# emulator runtime DLLs at root (Qt6Core/Network + icu; NOT the plugin dlls)
for f in "$EMU"/Qt6Core.dll "$EMU"/Qt6Network.dll "$EMU"/icuuc.dll; do
    [[ -f "$f" ]] && cp "$f" "$DIST/"
done
# platform manifests
cp "$EMU"/*_platform.json "$DIST/" 2>/dev/null || true
# firmware: EMPTY directory by policy (user supplies SRM images)
mkdir -p "$DIST/firmware"

# ---- editors (self-contained payload, already correctly structured) --------
cp -r "$EMU/editors" "$DIST/editors"
rm -f "$DIST"/editors/*.pdb 2>/dev/null || true

# ---- launcher + its windeployqt Qt runtime ---------------------------------
cp "$LAU/EmulatrLaunch.exe" "$DIST/"
for d in platforms styles imageformats iconengines generic; do
    [[ -d "$LAU/$d" ]] && cp -r "$LAU/$d" "$DIST/"
done
# launcher Qt dlls that the emulator set does not already provide
for f in "$LAU"/Qt6*.dll "$LAU"/icuuc.dll "$LAU"/dxcompiler.dll "$LAU"/dxil.dll; do
    base="$(basename "$f")"
    [[ -f "$f" && ! -f "$DIST/$base" ]] && cp "$f" "$DIST/"
done
# launcher tls/networkinformation merge (skip files already present)
for d in tls networkinformation; do
    if [[ -d "$LAU/$d" ]]; then
        mkdir -p "$DIST/$d"
        cp -rn "$LAU/$d/." "$DIST/$d/" 2>/dev/null || true
    fi
done

# ---- curated tools (beta-relevant only; needs user's python for .py) -------
mkdir -p "$DIST/tools"
for t in mkdisk.py srm_console_driver.py alpha_disasm.py; do
    [[ -f "$SRC/tools/$t" ]] && cp "$SRC/tools/$t" "$DIST/tools/"
done

# ---- scrub anything that must never ship -----------------------------------
find "$DIST" -name "*.pdb" -delete
find "$DIST" -name "CMake*" -prune -exec rm -rf {} + 2>/dev/null || true
find "$DIST" -name "*.ninja*" -delete 2>/dev/null || true
find "$DIST" -name "*_autogen" -prune -exec rm -rf {} + 2>/dev/null || true
find "$DIST" -name "deploy_*.cmake" -delete 2>/dev/null || true
find "$DIST" -name "Emulatr_tests*" -delete 2>/dev/null || true

echo "=== staged ==="
du -sh "$DIST"
find "$DIST" -maxdepth 1 | sort
