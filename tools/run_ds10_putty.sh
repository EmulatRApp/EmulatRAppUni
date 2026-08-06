#!/usr/bin/env bash
# ============================================================================
# tools/run_ds10_putty.sh -- cold-boot the DS10 firmware to P00>>> with a
# PuTTY console attached.
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
#
# Commercial use prohibited without separate license.
# Contact:        peert@envysys.com  |  https://envysys.com
# ============================================================================
#
# FILE: tools/run_ds10_putty.sh
# FUNCTION: DS10 sibling of run_ds20_putty.sh.  Written 2026-08-05 to complete
#           the DS10/DS20/ES40 set the do-no-harm gate requires ("full suite
#           plus DS10/DS20/ES40 to P00>>>") -- only the DS20 leg had a PuTTY
#           launcher, so the other two legs had no one-command path.
# CHANGE:   NEW FILE.  Three deliberate differences from run_ds20_putty.sh:
#           1. RUN_DIR is AUTO-PICKED (newest build dir carrying the exe, the
#              DS10 firmware AND the DS10 manifest) rather than hardcoded to
#              out/build/relwithdebinfo.  The hardcode is the recurring
#              stale-binary trap; RUN_DIR_OVERRIDE pins it when needed.
#           2. The log goes to logs/ via emulatr_run_env.sh.  run_ds20_putty.sh
#              predates the artifact-placement rule and drops run_ds20_<ts>.log
#              loose in the run-dir root.
#           3. The stale-source check is a soft NOTE swept over the lib trees,
#              not a FATAL against a hardcoded file list that goes stale.
#           No ini/manifest mutation beyond model=, and that is restored on
#           exit -- this script leaves no committed change.
#
# Host    : Windows / Git Bash (PC) is the supported path; the platform guard
#           below stubs the macOS/Linux branch for the cross-platform owner.
# Usage   : ./tools/run_ds10_putty.sh
# Toggles : RUN_DIR_OVERRIDE=<dir>     pin the build dir (stale-binary escape)
#           EMULATR_CONSOLE_PORT=<n>   console TCP port (default 10023)
#           EMULATR_INSTANCE=<tag>     concurrent-instance discriminator
#           MAXCYC=<n>                 max guest cycles
# Output  : logs/<TAG>_putty_<TS>.log  (TAG owned by emulatr_run_env.sh)
# ASCII(128) only.
# ============================================================================
set -euo pipefail

# ---- platform guard: PC (Windows/Git Bash) authoritative --------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;
    Darwin)               EMU_HOST=mac ;;
    *)                    EMU_HOST=nix ;;
esac

# ---- locate project --------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"
[[ -d "$PROJ/systemLib" ]] || { echo "FATAL: $PROJ does not look like the Emulatr project"; exit 1; }

FW_REL="firmware/ds10_v7_3.exe"
MANIFEST="ds10_v7_3_platform.json"

# ---- auto-pick the newest run dir that can actually boot DS10 ---------------
RUN_DIR=""
NEWEST=0
for cand in "RelWithDebInfo" "out/build/relwithdebinfo" "Release" \
            "out/build/release" "out/build/cli" "Debug"; do
    d="$PROJ/$cand"
    exe="$d/Emulatr.exe"; [[ -x "$exe" ]] || exe="$d/Emulatr"
    [[ -x "$exe" ]]            || continue
    [[ -f "$d/$FW_REL" ]]      || continue
    [[ -f "$d/$MANIFEST" ]]    || continue
    # GNU stat first, then BSD/macOS; reject non-numeric so set -u cannot trip.
    m=$(stat -c '%Y' "$exe" 2>/dev/null || stat -f '%m' "$exe" 2>/dev/null || echo 0)
    [[ "$m" =~ ^[0-9]+$ ]] || m=0
    if (( m >= NEWEST )); then NEWEST=$m; RUN_DIR="$d"; fi
done
if [[ -n "${RUN_DIR_OVERRIDE:-}" ]]; then
    if   [[ -x "$RUN_DIR_OVERRIDE/Emulatr.exe" ]]; then RUN_DIR="$RUN_DIR_OVERRIDE"
    elif [[ -x "$PROJ/$RUN_DIR_OVERRIDE/Emulatr.exe" ]]; then RUN_DIR="$PROJ/$RUN_DIR_OVERRIDE"
    else echo "FATAL: RUN_DIR_OVERRIDE has no Emulatr.exe: $RUN_DIR_OVERRIDE"; exit 1; fi
fi
[[ -n "$RUN_DIR" ]] || {
    echo "FATAL: no run dir under $PROJ carrying Emulatr.exe + $FW_REL + $MANIFEST"
    exit 1; }
cd "$RUN_DIR"

# ---- binary: prefer the host's own build ------------------------------------
if [ "$EMU_HOST" = "win" ]; then
    if   [ -x "./Emulatr.exe" ]; then EXE="./Emulatr.exe"
    elif [ -x "./Emulatr"     ]; then EXE="./Emulatr"
    else echo "FATAL: no Emulatr(.exe) in $RUN_DIR"; exit 1; fi
else
    if   [ -x "./Emulatr"     ]; then EXE="./Emulatr"
    elif [ -x "./Emulatr.exe" ]; then EXE="./Emulatr.exe"
    else echo "FATAL: no Emulatr in $RUN_DIR"; exit 1; fi
fi

FW="$FW_REL"
# Per-instance run environment (FILE 7): owns STEM/TAG/TS, the log-name helper,
# the per-instance flash NVRAM, the stop sentinels and the console port.
. "$SCRIPT_DIR/emulatr_run_env.sh"
INI="config/Emulatr.ini"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-222000000000}"
LOG="$(emulatr_log putty)"     # logs/<TAG>_putty_<TS>.log

# ---- soft stale-binary notice (informational; NOT a build gate) -------------
STALE="$(find "$PROJ/deviceLib" "$PROJ/chipsetLib" "$PROJ/coreLib" "$PROJ/pipelineLib" \
              \( -name '*.h' -o -name '*.cpp' \) -newer "$EXE" 2>/dev/null | head -5 || true)"
if [[ -n "$STALE" ]]; then
    echo "NOTE: these sources are NEWER than $EXE -- this run uses the"
    echo "      LAST-BUILT binary, not your latest edits.  Rebuild if that matters."
    printf '        %s\n' $STALE
fi

# ---- force model=DS10 for this run; restore on exit -------------------------
cleanup() {
    [[ -f "$INI.puttybak" ]] && mv -f "$INI.puttybak" "$INI" 2>/dev/null || true
}
trap cleanup EXIT
if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.puttybak"
    if grep -qiE '^[[:space:]]*model[[:space:]]*=' "$INI"; then
        # Portable across GNU and BSD/macOS sed: temp file, no -i, no \s.
        SED_TMP="$(mktemp)"
        sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1DS10/" "$INI" > "$SED_TMP" \
            && mv -f "$SED_TMP" "$INI"
    else
        echo "WARN: no model= line in $INI -- relying on the firmware stem for DS10"
    fi
fi

# ---- environment ------------------------------------------------------------
unset  EMULATR_NO_PUTTY               # KEEP PuTTY auto-launch ON
export EMULATR_CONSOLE_MIRROR=1       # banner + console -> stderr/log
export EMULATR_CONSOLE_PORT="$PORT"

if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
    echo "WARN: PuTTY.exe not on PATH -- attach manually:  putty -telnet localhost $PORT"
fi

# ---- launch -----------------------------------------------------------------
# The build stamp goes INTO the log (tee creates it) so every run records which
# binary produced it -- the datum JRN-SCSI-041 Sec 9.6 had to reconstruct by
# binary archaeology because the profile header carries none.
{
  echo "======================================================================="
  echo "run     = $(basename "$0")   $(date '+%Y-%m-%d %H:%M:%S')"
  echo "RUN_DIR = $RUN_DIR"
  echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
  echo "exe sha = $(sha256sum "$EXE" 2>/dev/null | cut -c1-16)"
  echo "fw      = $FW   (DS10 platform)"
  echo "console = PuTTY auto-launch on localhost:$PORT"
  echo "log     = $RUN_DIR/$LOG"
  echo "======================================================================="
  echo "Gate leg: reach  P00>>>  then halt the run (Ctrl/P or the stop file)."
  echo "======================================================================="
} | tee "$LOG"

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee -a "$LOG"

# ---- post-run: answer the gate question, and the census-landed question -----
echo "-----------------------------------------------------------------------"
if grep -qE 'P00>>>' "$LOG"; then
    echo "GATE   : P00>>> REACHED -- the DS10 leg passes."
else
    echo "GATE   : P00>>> NOT seen in the log -- the DS10 leg FAILS."
fi
CENSUS=$(grep -cE '^N810-CENSUS' "$LOG" 2>/dev/null || echo 0)
echo "CENSUS : $CENSUS N810-CENSUS lines"
if [[ "$CENSUS" == "0" ]]; then
    echo "         Zero is EXPECTED on a platform whose console never drives the"
    echo "         53C810.  On the DS20 SCSI path zero instead means"
    echo "         EMULATR_DIAG_N810 did not reach the compiler -- re-run CMake"
    echo "         configure, do not read it as 'the script touched no registers'."
fi
echo "log    : $RUN_DIR/$LOG"
