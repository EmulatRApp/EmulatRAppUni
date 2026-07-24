#!/usr/bin/env bash
# ============================================================================
# run_ds20_ide_trace.sh -- cold-boot the DS20 firmware with the IDE probe/command
# trace ON (EMULATR_IDE_TRACE=1) and the full retire trace OFF.  Console on TCP
# 10023 (PuTTY auto-launch).  Boots to P00>>> so you can `show dev` and
# `b dqa1` (CD) / `b dqa0` (disk).  Log -> ./logs/ds20_ide_trace_<ts>.log.
#
# Self-locates the run dir (the dir holding Emulatr.exe + firmware/ds20_v7_3.exe),
# whether this script lives in <run-dir>/tools/ or the run dir itself.
#
# Overrides:  MAXCYC=<n>            max guest cycles (default 22000000000)
#             EMULATR_CONSOLE_PORT  console TCP port (default 10023)
#             NO_TRACE=1            drop EMULATR_IDE_TRACE (plain boot, no trace)
# ASCII(128) only.
#
# Host    : Windows / Git Bash (PC) is the supported path; a macOS/Linux branch
#           is stubbed via the platform guard below for the cross-platform owner.
# ============================================================================
set -euo pipefail

# ---- platform guard: PC (Windows/Git Bash) authoritative --------------------
# One script set serves Windows and (owned elsewhere) macOS/Linux.  uname picks
# the host so the binary name and console tooling resolve per platform.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;   # Git Bash / MSYS2 on Windows (PC)
    Darwin)               EMU_HOST=mac ;;   # macOS  (cross-platform owner)
    *)                    EMU_HOST=nix ;;   # Linux / other POSIX
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# run dir = script dir if it holds the exe, else its parent (script in tools/).
# Accept Emulatr.exe (Windows) or a bare Emulatr (macOS/Linux build).
if   [[ -x "$SCRIPT_DIR/Emulatr.exe"    || -x "$SCRIPT_DIR/Emulatr"    ]]; then RUN_DIR="$SCRIPT_DIR"
elif [[ -x "$SCRIPT_DIR/../Emulatr.exe" || -x "$SCRIPT_DIR/../Emulatr" ]]; then RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else echo "FATAL: Emulatr(.exe) not found next to this script or one level up"; exit 1; fi
cd "$RUN_DIR"

# binary: prefer .exe (Windows) then bare (mac/Linux).
if   [ -x "./Emulatr.exe" ]; then EXE="./Emulatr.exe"
elif [ -x "./Emulatr"     ]; then EXE="./Emulatr"
else echo "FATAL: no Emulatr(.exe) in $RUN_DIR"; exit 1; fi
FW="firmware/ds20_v7_3.exe"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-22000000000}"
mkdir -p logs
LOG="logs/ds20_ide_trace_$(date +%Y%m%d_%H%M%S).log"

[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: $FW not found (DS20 firmware)"; exit 1; }

export EMULATR_CONSOLE_PORT="$PORT"
export EMULATR_CONSOLE_MIRROR=1
if [[ "${NO_TRACE:-0}" != "1" ]]; then export EMULATR_IDE_TRACE=1; fi

echo "======================================================================="
echo "RUN_DIR = $RUN_DIR"
echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "fw      = $FW  (DS20)"
echo "trace   = IDE_TRACE=${EMULATR_IDE_TRACE:-0}   (retire trace OFF)"
echo "console = localhost:$PORT  (PuTTY auto-launch; or: putty -telnet localhost $PORT)"
echo "log     = $RUN_DIR/$LOG"
echo "At P00>>>:   show dev      then   b dqa1  (CD)   /   b dqa0  (disk)"
echo "======================================================================="

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee "$LOG"

echo "-----------------------------------------------------------------------"
echo "done.  storage-attach + dqa/IDE lines:"
grep -inE "Storage: attached|Storage:.*not attached|dqa|IDE-TRACE|no media present|packet|signature" "$LOG" | tail -30 || true
