#!/usr/bin/env bash
# ============================================================================
# run_ds20_h8b_gateb.sh -- H-8b attempt 2 Gate B (BRIEF-N810-RESEL-002 Sec 5):
# DS20 VMS boot with the reselection edits live.  Expect the park flood to
# BREAK: per deferred READ the row sequence
#   N810-DEFER -> N810-INT dsps=0x72 -> N810-SESSION (host DSP redirect) ->
#   N810-RESELECT -> N810-NEXUS keying -> N810-INT dsps=0xF79 ->
#   N810-SESSION -> N810-NEXUS answer -> (continuation) -> INT 10
# Decisive signal: the console progresses past the post-date silent wall.
#
# GATE A FIRST (same binary is fine): boot to P00>>>, `show dev d` must list
# dka0 RZ29L-AA + dka100-600 + dqa1 + dva0, and the log must contain ZERO
# N810-DEFER / N810-RESELECT rows.  This script's verdict checks that too
# if you simply stay at the SRM prompt and halt (rows absent = Gate A pass).
#
# BOOT (architect at PuTTY): show dev d -> b dka0 -> SYSBOOT>
#   set startup_p1 "min" -> c -> answer date -> WATCH THE CONSOLE.
#   If SYSINIT advances, let it run as far as it goes before halting.
#
# Usage   : ./tools/run_ds20_h8b_gateb.sh   (RUN_DIR=<dir> overrides)
# Output  : <run-dir>/logs/ds20_v7_3_h8b_gateb_<ts>.log (+ _extract)
# ============================================================================
set -euo pipefail

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;
    Darwin)               EMU_HOST=mac ;;
    *)                    EMU_HOST=nix ;;
esac

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
LOG="logs/${STEM}_h8b_gateb_${TS}.log"

[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: firmware $FW not found"; exit 1; }

for SRC in "$PROJ_DIR/deviceLib/Tsunami/Ncr53C810.h" \
           "$PROJ_DIR/deviceLib/Tsunami/Uart16550.h" \
           "$PROJ_DIR/chipsetLib/TsunamiChipset.h"; do
    if [[ -f "$SRC" && "$SRC" -nt "$EXE" ]]; then
        echo "FATAL: $EXE is OLDER than $(basename "$SRC") -- rebuild first"
        echo "       (H-8b and/or the console event-driven IRQ fix missing)."
        exit 1
    fi
done
# Facility guard: the attempt-2 DEFER row string proves the new code is in.
if [[ "$(grep -a -c 'N810-DEFER' "$EXE" || true)" -eq 0 ]]; then
    echo "FATAL: 'N810-DEFER' not found in $EXE -- rebuild (relwithdebinfo,"
    echo "       -DEMULATR_DIAG_N810=ON for the Gate C diag rows)."
    exit 1
fi
echo "preflight: H-8b attempt-2 code confirmed in binary."

if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
    echo "WARN: PuTTY.exe not on PATH -- attach manually: putty -telnet localhost $PORT"
fi

if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.h8bbak"
    trap 'mv -f "$INI.h8bbak" "$INI" 2>/dev/null || true' EXIT
    if grep -qiE '^[[:space:]]*model[[:space:]]*=' "$INI"; then
        SED_TMP="$(mktemp)"
        sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1DS20/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"
    fi
fi

unset  EMULATR_NO_PUTTY
export EMULATR_CONSOLE_MIRROR=1
export EMULATR_CONSOLE_PORT="$PORT"
unset  EMULATR_SCSI_TRACE
unset  EMULATR_SCSI_MOVE_PROBE

echo "RUN_DIR = $RUN_DIR"
echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "log     = $RUN_DIR/$LOG"
echo "-----------------------------------------------------------------------"

( "$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 || true ) | tee "$LOG"

echo "-----------------------------------------------------------------------"
EXTRACT="${LOG%.log}_extract.log"
{
    echo "# BRIEF-N810-RESEL-002 Gate B extract -- source: $(basename "$LOG")"
    grep -E 'N810-(DEFER|RESELECT|NEXUS|SESSION|INT|LAP|PARK)' "$LOG" | \
        grep -vE 'N810-PARK ' || true
} > "$EXTRACT"

c()  { grep -c "$1" "$LOG" 2>/dev/null || true; }
DEFER="$(c 'N810-DEFER')"
RESEL="$(c 'N810-RESELECT')"
I72="$(grep -c 'N810-INT.*dsps=0x00000072' "$LOG" || true)"
IF79="$(grep -c 'N810-INT.*dsps=0x00000F79' "$LOG" || true)"
NEXK="$(c 'N810-NEXUS keying')"
NEXA="$(c 'N810-NEXUS answer')"
RARM="$(c 'N810-LAP RECONNECT-ARM')"
PARKS="$(c 'N810-PARK ')"
echo "extract = $RUN_DIR/$EXTRACT"
echo "DEFER=$DEFER  INT72=$I72  RESELECT=$RESEL  NEXUS-keying=$NEXK"
echo "INT-F79=$IF79  NEXUS-answer=$NEXA  RECONNECT-ARM=$RARM  parks=$PARKS"
if [[ "$DEFER" -eq 0 ]]; then
    echo "VERDICT: no deferral occurred."
    echo "  - If this was a Gate A (SRM-only) run: PASS if show dev d listed"
    echo "    the full device set (check the console)."
    echo "  - If this was a VMS boot: the predicate never fired -- either no"
    echo "    qualifying READ arrived or provenance/DiscPriv gating is off;"
    echo "    deliver the extract for triage."
elif [[ "$RESEL" -ge 1 && "$IF79" -ge 1 ]]; then
    echo "VERDICT: reselection chain EXERCISED (defer -> reselect -> F79)."
    echo "  Gate B decides on the CONSOLE: did SYSINIT advance past the"
    echo "  post-date wall?  Deliver the extract either way (Gate C rides"
    echo "  the RECONNECT-ARM lap window in it)."
else
    echo "VERDICT: PARTIAL -- deferral fired but the chain broke."
    echo "  Compare against the expected sequence in the script header;"
    echo "  the first missing row names the broken leg.  Deliver the extract."
fi
