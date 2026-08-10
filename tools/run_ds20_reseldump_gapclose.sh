#!/usr/bin/env bash
# ============================================================================
# run_ds20_reseldump_gapclose.sh -- DS20 VMS boot for the JRN-SCSI-044 Sec 6
# GAP CLOSURE capture (BRIEF-N810-RESEL-001 Edit 1, windows widened
# 2026-08-07): one-shot N810-RESELDUMP of THREE script-RAM windows at the
# first WAIT RESELECT park:
#     0xC0000000..0xC0000900  144 rows  GAP-1/2/3 (C000084C head, C00002D4,
#                                       C00007AC/BC/F4 disconnect branches)
#     0xC0000900..0xC0001700  224 rows  decoded region, alignment cross-check
#     0xC0008250..0xC0008270    2 rows  GAP-4 (msg-out constants)
#   expected total: 370 RESELDUMP rows (+ LAP windows from the still-armed
#   SELECT-seeking lap capture, bounded as before).
#
# ENV GATES: there are NONE at runtime for the N810 diag facility -- it is
# compiled in/out by -DEMULATR_DIAG_N810=ON (relwithdebinfo).  The only
# runtime SCSI env vars (EMULATR_SCSI_TRACE, EMULATR_SCSI_MOVE_PROBE) are
# older, separate facilities and stay UNSET for this capture.  This script
# therefore only sets console mirror/port and verifies the facility is in
# the binary before launching (BRIEF Sec 5 step 1: never discover a dead
# flag from a silent log).
#
# BOOT PROCEDURE (architect at the PuTTY console, per BRIEF Sec 5):
#   1. P00>>> show dev d      gate: dka0 RZ29L-AA LYJ0 + dka100-600 +
#                                   dqa1 + dva0
#   2. boot dka0, through SYSBOOT> c, answer the date/time prompt
#      ("Please enter date and time"), let the post-date park flood run --
#      the captures complete within the first parks.
#   3. Ctrl/P halt.  This script then extracts and verdicts the rows.
#
# Usage   : ./tools/run_ds20_reseldump_gapclose.sh   (RUN_DIR=<dir> overrides)
# Output  : <run-dir>/logs/ds20_v7_3_reseldump_gap_<ts>.log (+ _extract)
# ============================================================================
set -euo pipefail

# ---- platform guard (parity with run_ds20_putty.sh) -------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;
    Darwin)               EMU_HOST=mac ;;
    *)                    EMU_HOST=nix ;;
esac

# ---- locate the build/run dir ----------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="${RUN_DIR:-$PROJ_DIR/out/build/relwithdebinfo}"
cd "$RUN_DIR"

if   [ -x "./Emulatr.exe" ]; then EXE="./Emulatr.exe"
elif [ -x "./Emulatr"     ]; then EXE="./Emulatr"
else EXE="./Emulatr.exe"; fi
FW="firmware/ds20_v7_3.exe"
INI="config/Emulatr.ini"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-222000000000}"
STEM="ds20_v7_3"
TS="$(date +%Y%m%d_%H%M%S)"
mkdir -p logs
LOG="logs/${STEM}_reseldump_gap_${TS}.log"

# ---- preflight -------------------------------------------------------------
[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: firmware $FW not found"; exit 1; }

# Stale-binary guard: the widened-window edit lives in Ncr53C810.h; the exe
# must be newer or the old 224-row window is what will run.
SRC="$PROJ_DIR/deviceLib/Tsunami/Ncr53C810.h"
if [[ -f "$SRC" && "$SRC" -nt "$EXE" ]]; then
    echo "FATAL: $EXE is OLDER than Ncr53C810.h -- rebuild relwithdebinfo first."
    echo "       (the widened gap-closure windows are not in this binary)"
    exit 1
fi

# Facility-in-binary guard (BRIEF Sec 5 step 1): the diag rows are compile-
# time gated by EMULATR_DIAG_N810; confirm the strings are present so we do
# not discover a dead flag from a silent log.
if [[ "$(grep -a -c 'N810-RESELDUMP' "$EXE" || true)" -eq 0 ]]; then
    echo "FATAL: 'N810-RESELDUMP' not found in $EXE."
    echo "       Rebuild relwithdebinfo with -DEMULATR_DIAG_N810=ON."
    exit 1
fi
echo "preflight: N810 diag facility confirmed in binary."

if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
    echo "WARN: PuTTY.exe not on PATH -- attach manually: putty -telnet localhost $PORT"
fi

# Force model=DS20 for this run; restore the ini on exit (same pattern as
# run_ds20_putty.sh; firmware stem wins for platform derivation regardless).
if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.gapbak"
    trap 'mv -f "$INI.gapbak" "$INI" 2>/dev/null || true' EXIT
    if grep -qiE '^[[:space:]]*model[[:space:]]*=' "$INI"; then
        SED_TMP="$(mktemp)"
        sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1DS20/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"
    else
        echo "WARN: no model= line in $INI -- relying on firmware stem for DS20"
    fi
fi

# ---- environment -----------------------------------------------------------
unset  EMULATR_NO_PUTTY               # KEEP PuTTY auto-launch ON (house rule)
export EMULATR_CONSOLE_MIRROR=1       # console -> stderr/log
export EMULATR_CONSOLE_PORT="$PORT"
unset  EMULATR_SCSI_TRACE             # separate facility; keep the log clean
unset  EMULATR_SCSI_MOVE_PROBE        # separate facility; keep the log clean

# ---- launch ----------------------------------------------------------------
echo "RUN_DIR = $RUN_DIR"
echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "fw      = $FW"
echo "console = PuTTY auto-launch on localhost:$PORT"
echo "log     = $RUN_DIR/$LOG"
echo "boot    : show dev d gate -> boot dka0 -> SYSBOOT> c -> date/time ->"
echo "          let the park flood run a few parks -> Ctrl/P halt"
echo "-----------------------------------------------------------------------"

( "$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 || true ) | tee "$LOG"

# ---- extract + verdict ------------------------------------------------------
echo "-----------------------------------------------------------------------"
EXTRACT="${LOG%.log}_extract.log"
PARKS="$(grep -c 'N810-PARK' "$LOG" || true)"
{
    echo "# JRN-SCSI-044 gap-closure capture extract -- source run log: $(basename "$LOG")"
    echo "# N810-PARK rows in source at extract time: ${PARKS} (rows omitted here)"
    grep -E 'N810-(RESELDUMP|LAP)' "$LOG" || true
} > "$EXTRACT"

ROWS="$(grep -c 'N810-RESELDUMP' "$LOG" || true)"
echo "extract = $RUN_DIR/$EXTRACT"
echo "RESELDUMP rows: $ROWS (expected 370 = 144 + 224 + 2)"
W_A="$(grep -c 'N810-RESELDUMP 0xC0000[0-8]' "$LOG" || true)"  # 0xC0000000..08F0
W_C="$(grep -c 'N810-RESELDUMP 0xC0008'      "$LOG" || true)"  # 0xC0008250/60
echo "  window A rows (0xC00000xx..): $W_A   window C rows (0xC00082xx): $W_C"
if [[ "$ROWS" -eq 370 && "$W_C" -eq 2 ]]; then
    echo "VERDICT: PASS -- gap-closure windows captured; deliver $EXTRACT for decode."
else
    echo "VERDICT: FAIL -- row count off.  Was the first park reached before halt?"
fi
