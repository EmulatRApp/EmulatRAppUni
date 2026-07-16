#!/usr/bin/env bash
# ============================================================================
# tools/_emulatr_runenv.sh -- shared run-directory self-location for EmulatR
# helper scripts.  Project: EmulatR -- Alpha AXP / EV6 (V4).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
# Contact: peert@envysys.com | https://envysys.com
# ----------------------------------------------------------------------------
# SOURCE this (do not execute).  It makes the sourcing script location
# independent so a distributed run-dir works when a user unzips it ANYWHERE:
#
#     source "$SELF_DIR/_emulatr_runenv.sh"
#
# WHAT IT DOES (2026-07-14)
#   1. Self-locates the RUN DIRECTORY (the dir that CONTAINS the binary) from
#      THIS file's own path via BASH_SOURCE -- never a hardcoded or source-tree
#      path.  Deployed layout is <run-dir>/tools/_emulatr_runenv.sh, so the
#      binary sits one level up (../).  Honors an explicit RUN_DIR= override,
#      and falls back to the dev source-tree out/build/<config> when the script
#      is run from a checkout instead of a shipped run-dir.
#   2. cd's into the run dir so every relative path (logs/ traces/ firmware/
#      config/ snapshots/) resolves under it -- which is also where the C++
#      binary writes (FaultEventLog / SRMConsoleDevice do create_directories
#      ("logs") relative to CWD; the retire sink writes traces/ relative too).
#   3. Pre-creates logs/ traces/ snapshots/ and exports RUN_DIR, BIN, HOST,
#      EMU_SRC_ROOT, EMU_TOOLS_DIR, EMU_LOG_DIR, EMU_TRACE_DIR, EMU_SNAP_DIR,
#      plus helpers emu_ts and emu_refresh_asset.
#
# CONTRACT for callers
#   * Define SELF_DIR (this script's sibling dir) BEFORE sourcing if you also
#     call sibling tools; the helper does not need it.
#   * Set CONFIG=relwithdebinfo|debug|release BEFORE sourcing to steer the dev
#     fallback (ignored once a shipped run-dir is found).
#   * After sourcing, use ONLY run-dir-relative paths for outputs and assets.
#
# Legacy top-level <project>/RelWithDebInfo is intentionally NOT probed:
# out/build/<config> is the build of record.  Set RUN_DIR= to force anything.
# ASCII(128) only.  Include-guarded against double-source.
# ============================================================================
[ -n "${_EMULATR_RUNENV_SOURCED:-}" ] && return 0
_EMULATR_RUNENV_SOURCED=1

# --- own location -----------------------------------------------------------
_ER_SELF="${BASH_SOURCE[0]:-$0}"
EMU_TOOLS_DIR="$(cd "$(dirname "$_ER_SELF")" && pwd)"
EMU_SRC_ROOT="$(cd "$EMU_TOOLS_DIR/.." && pwd)"   # <run-dir> (dist) or <project> (dev)

# --- host + candidate binary names ------------------------------------------
case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) HOST=win; _ER_BINS="Emulatr.exe Emulatr" ;;
    Darwin)               HOST=mac; _ER_BINS="Emulatr" ;;
    *)                    HOST=nix; _ER_BINS="Emulatr" ;;
esac

# _er_has_bin <dir> -- true if <dir> holds a runnable binary; sets EMU_BIN.
_er_has_bin() {
    local d="$1" b
    for b in $_ER_BINS; do
        [ -x "$d/$b" ] && { EMU_BIN="$b"; return 0; }
    done
    return 1
}

# --- resolve the run dir (dir containing the binary) ------------------------
_ER_CFG="$(printf '%s' "${EMU_CONFIG:-${CONFIG:-relwithdebinfo}}" | tr '[:upper:]' '[:lower:]')"
_ER_RESOLVED=""
if [ -n "${RUN_DIR:-}" ] && _er_has_bin "$RUN_DIR"; then
    _ER_RESOLVED="$(cd "$RUN_DIR" && pwd)"
else
    for _d in "$EMU_TOOLS_DIR/.." "$EMU_TOOLS_DIR" "$EMU_TOOLS_DIR/../.." \
              "$EMU_SRC_ROOT/out/build/$_ER_CFG" \
              "$EMU_SRC_ROOT/out/build/relwithdebinfo" \
              "$EMU_SRC_ROOT/out/build/release" \
              "$EMU_SRC_ROOT/out/build/debug" \
              "$EMU_SRC_ROOT/out/build/mac-debug" \
              "$EMU_SRC_ROOT/out/build/mac-release"; do
        if _er_has_bin "$_d"; then _ER_RESOLVED="$(cd "$_d" && pwd)"; break; fi
    done
fi
if [ -z "$_ER_RESOLVED" ]; then
    echo "FATAL(_emulatr_runenv): no Emulatr binary found near $EMU_TOOLS_DIR." >&2
    echo "  Looked beside tools/ (../) and dev out/build/<config>.  Set RUN_DIR= to override." >&2
    return 1 2>/dev/null || exit 1
fi
RUN_DIR="$_ER_RESOLVED"
cd "$RUN_DIR" || { echo "FATAL(_emulatr_runenv): cannot cd $RUN_DIR" >&2; return 1 2>/dev/null || exit 1; }
# ensure EMU_BIN is set even on the RUN_DIR-override branch
[ -n "${EMU_BIN:-}" ] || _er_has_bin "$RUN_DIR" || {
    echo "FATAL(_emulatr_runenv): no runnable binary in $RUN_DIR" >&2
    return 1 2>/dev/null || exit 1
}
BIN="$EMU_BIN"

# --- standard output subdirs (emulatr-log-trace-output skill) ---------------
EMU_LOG_DIR="logs"
EMU_TRACE_DIR="traces"
EMU_SNAP_DIR="snapshots"
mkdir -p "$EMU_LOG_DIR" "$EMU_TRACE_DIR" "$EMU_SNAP_DIR" 2>/dev/null || true

# --- helpers ----------------------------------------------------------------
# emu_ts -- timestamp in the skill's purpose_YYYYMMDD_HHMMSS form.
emu_ts() { date +%Y%m%d_%H%M%S; }

# emu_refresh_asset SRC DST -- best-effort DEV refresh: copy SRC->DST only when
# SRC exists and is NOT already the same file.  In a distributed run-dir SRC and
# DST are the same shipped file (SRC==DST), so this is a no-op; a missing source
# tree is never fatal (the run-dir already ships the asset in place).
emu_refresh_asset() {
    local src="$1" dst="$2"
    [ -f "$src" ] || return 0
    [ "$src" -ef "$dst" ] 2>/dev/null && return 0
    mkdir -p "$(dirname "$dst")" 2>/dev/null || true
    cp -f "$src" "$dst" 2>/dev/null || true
}

export RUN_DIR BIN HOST EMU_SRC_ROOT EMU_TOOLS_DIR EMU_LOG_DIR EMU_TRACE_DIR EMU_SNAP_DIR
