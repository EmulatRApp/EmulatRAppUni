#!/usr/bin/env bash
# ============================================================================
# make_redist.sh -- package a built EmulatR run directory into a versioned
# redistributable zip under <root>/redist/.
# ----------------------------------------------------------------------------
# Purpose : zip everything in out/build/<config>/ EXCEPT log and trace output
#           and the licensed vendor firmware images (firmware/*.exe), naming the
#           archive after the Help & Manual documentation version so the package
#           and the guide always agree. The tester supplies their own firmware
#           image; the .rom files are kept.
# Host    : Windows / Git Bash (PC) is the supported path; a macOS/Linux branch
#           is stubbed via the platform guard below. Requires `zip` on PATH
#           (ships with Git Bash / MSYS2).
# Location: EmulatRAppUniV5/tools/ (with the other tool scripts). It resolves the
#           project root as its PARENT dir, and excludes itself from the archive
#           (tools/ is packaged into the run dir) via EXCLUDE_PATHS below.
#
# Usage   : ./tools/make_redist.sh [config]   (config default: relwithdebinfo)
#   e.g.  : ./tools/make_redist.sh            -> redist/EmulatR_v1.4.4_relwithdebinfo.zip
#           ./tools/make_redist.sh release
#
# Env overrides:
#   CONFIG=<name>   build config (else 1st arg, else relwithdebinfo)
#   HMXP=<path>     Help & Manual project file to read the version from
#   VERSION=x.y.z   force the version string (skips reading the .hmxp)
#   OUTDIR=<path>   output directory (default <root>/redist)
# ============================================================================
set -euo pipefail

# ---- platform guard: PC (Windows/Git Bash) authoritative --------------------
# shellcheck disable=SC2034  # EMU_HOST reserved for the macOS/Linux branch owner
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;   # Git Bash / MSYS2 on Windows (PC)
    Darwin)               EMU_HOST=mac ;;   # macOS  (cross-platform owner)
    *)                    EMU_HOST=nix ;;   # Linux / other POSIX
esac

# This script lives in tools/, so the project root is its parent directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---- config + paths --------------------------------------------------------
CONFIG="${CONFIG:-${1:-relwithdebinfo}}"
CFG_DIR="$ROOT/out/build/$CONFIG"
OUTDIR="${OUTDIR:-$ROOT/redist}"
HMXP="${HMXP:-$ROOT/../H&M/HMDocs/claudeRV4.hmxp}"

[[ -d "$CFG_DIR" ]] || { echo "FATAL: build dir not found: $CFG_DIR (build config '$CONFIG' first, or pass another config)"; exit 1; }
command -v zip >/dev/null 2>&1 || { echo "FATAL: 'zip' not found on PATH. In Git Bash it ships with MSYS2; install or add it, then re-run."; exit 1; }

# ---- version string (match the documentation) ------------------------------
# Read versionmajor/minor/build from the H&M project file so the archive name
# tracks the guide. Override with VERSION=x.y.z if the .hmxp is unavailable.
if [[ -n "${VERSION:-}" ]]; then
    VER="$VERSION"
elif [[ -f "$HMXP" ]]; then
    maj="$(grep -oE 'versionmajor">[0-9]+' "$HMXP" | grep -oE '[0-9]+$' | head -1)"
    min="$(grep -oE 'versionminor">[0-9]+' "$HMXP" | grep -oE '[0-9]+$' | head -1)"
    bld="$(grep -oE 'versionbuild">[0-9]+' "$HMXP" | grep -oE '[0-9]+$' | head -1)"
    if [[ -n "$maj" && -n "$min" && -n "$bld" ]]; then
        VER="${maj}.${min}.${bld}"
    else
        echo "WARN: could not parse version from $HMXP -- using 0.0.0 (set VERSION=x.y.z to override)"
        VER="0.0.0"
    fi
else
    echo "WARN: H&M project file not found: $HMXP -- using 0.0.0 (set VERSION=x.y.z or HMXP=<path>)"
    VER="0.0.0"
fi

ZIP="$OUTDIR/EmulatR_v${VER}_${CONFIG}.zip"

# ---- exclusion policy ------------------------------------------------------
# Directories pruned entirely (find never descends -- important: traces/ can be
# hundreds of GB). Names match at any depth.
EXCLUDE_DIRS=(logs traces trace)
# File globs dropped anywhere in the tree.
EXCLUDE_GLOBS=('*.log' '*.out' '*.trc')
# Path globs dropped (case-insensitive, matched against the full relative path).
# firmware/*.exe = the licensed vendor firmware images -- NOT redistributable.
# This is path-scoped so the emulator's own root Emulatr.exe is kept, and any
# firmware/*.rom stays.
EXCLUDE_PATHS=('*/firmware/*.exe' '*/tools/make_redist.sh')
# --- OPTIONAL heavier excludes (uncomment to also drop dev/state artifacts) ---
#   EXCLUDE_DIRS+=(snapshots)
#   EXCLUDE_GLOBS+=('*.pdb' '*.svlPV_functionAndLineHooks2' '*_diag_flash.rom' 'flash_backup_*.rom')

# ---- build the file list (prune excluded dirs, drop excluded globs) ---------
LIST="$(mktemp)"; trap 'rm -f "$LIST"' EXIT
# find: -prune the excluded dirs so we never walk them, else -print files that
# do not match an excluded glob.
prune=(); for d in "${EXCLUDE_DIRS[@]}"; do prune+=(-name "$d" -o); done
unset 'prune[${#prune[@]}-1]'                       # drop the trailing -o
notglob=(); for g in "${EXCLUDE_GLOBS[@]}"; do notglob+=(! -name "$g"); done
notpath=(); for p in "${EXCLUDE_PATHS[@]}"; do notpath+=(! -ipath "$p"); done
( cd "$CFG_DIR" && find . -type d \( "${prune[@]}" \) -prune -o -type f \
    ${notglob[@]+"${notglob[@]}"} ${notpath[@]+"${notpath[@]}"} -print ) | sed 's|^\./||' > "$LIST"

COUNT="$(wc -l < "$LIST" | tr -d ' ')"
[[ "$COUNT" -gt 0 ]] || { echo "FATAL: no files to package under $CFG_DIR after exclusions."; exit 1; }

# ---- package ---------------------------------------------------------------
mkdir -p "$OUTDIR"
rm -f "$ZIP"
echo "=== EmulatR redist ========================================="
echo "  config   : $CONFIG"
echo "  source   : $CFG_DIR"
echo "  version  : $VER   (from $( [[ -n "${VERSION:-}" ]] && echo 'VERSION env' || basename "$HMXP" ))"
echo "  excluded : dirs[${EXCLUDE_DIRS[*]}]  globs[${EXCLUDE_GLOBS[*]}]  paths[${EXCLUDE_PATHS[*]}]"
echo "  files    : $COUNT"
echo "  archive  : $ZIP"
echo "==========================================================="
( cd "$CFG_DIR" && zip -q "$ZIP" -@ < "$LIST" )

SIZE="$(du -h "$ZIP" | cut -f1)"
echo "done. wrote $ZIP ($SIZE, $COUNT files)."
