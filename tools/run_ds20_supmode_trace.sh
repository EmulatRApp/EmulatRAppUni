#!/usr/bin/env bash
# ============================================================================
# run_ds20_supmode_trace.sh -- JRN-SUPMODE-001 Sec 12 retire-trace capture:
# DS20 VMS boot with the CHMS-armed retire window live.  The P-SUP-2 hook
# (PalEntries.cpp execChms_vms) arms the DecListingSink window AT the CHMS
# issuance, so the ~40-insn CHMS->EV6__PS corridor is the first thing in
# the .trc -- no console arming, no Ctrl/P timing.  Re-arms every CHMS.
#
# Pair with: python tools/srm_supmode_trace_driver.py   (headless boot:
#   b -fl 0,1 dka0 -> SYSBOOT> set startup_p1 "min" -> c -> date ->
#   wait for ACCVIO -> grace -> Ctrl/P halt)
#
# Verdict artifacts:
#   logs/ds20_v7_3_supmode_trace_<ts>.log   full run log (stderr+console)
#   traces/<ts>_srm.trc                     the corridor retire trace
#   "SUPMODE ARM window=..." rows in the log = the hook fired.
#
# Usage   : ./tools/run_ds20_supmode_trace.sh   (RUN_DIR=<dir> overrides)
# ============================================================================
set -euo pipefail

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
mkdir -p logs traces
LOG="logs/${STEM}_supmode_trace_${TS}.log"

[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: firmware $FW not found"; exit 1; }

# Facility guards: the P-SUP-2 arm row proves the new hook is in; the
# H-8b DEFER row proves the reselection binary did not regress.
for NEEDLE in 'SUPMODE ARM window=' 'N810-DEFER'; do
    if [[ "$(grep -a -c "$NEEDLE" "$EXE" || true)" -eq 0 ]]; then
        echo "FATAL: '$NEEDLE' not found in $EXE -- rebuild relwithdebinfo"
        exit 1
    fi
done
echo "preflight: P-SUP-2 arm hook + H-8b code confirmed in binary."

if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.supbak"
    trap 'mv -f "$INI.supbak" "$INI" 2>/dev/null || true' EXIT
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
# --- JRN-SUPMODE-001 Sec 12 stack ---
export EMULATR_PROBE_SUPMODE=1                 # P-SUP-1 issuance/census rows
export EMULATR_PROBE_SUPMODE_ARM=0x200000      # P-SUP-2: 2M-retire window per CHMS
export EMULATR_PROBE_SUPMODE_ARM23=0x200000    # P-SUP-5: 2M-retire window per 2->3 CM write
export EMULATR_TRACE_WINDOW=1                  # open the _srm.trc sink
export EMULATR_RETIRE_TRACE_DIR="$RUN_DIR/traces"

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
ARMS="$(grep -a -c 'SUPMODE ARM window=' "$LOG" || true)"
CHMS="$(grep -a -c 'SUPMODE CHMS#' "$LOG" || true)"
TRC="$(ls -t traces/*_srm.trc 2>/dev/null | head -1 || true)"
echo "CHMS rows=$CHMS  ARM rows=$ARMS  newest trc=${TRC:-none}"
if [[ "$ARMS" -ge 1 && -n "$TRC" ]]; then
    echo "VERDICT: window armed at CHMS -- decode the corridor:"
    echo "  grep -a -n 'pal=1' $TRC | head    (or read around the arm cyc)"
else
    echo "VERDICT: no arm fired -- either CHMS never issued (check CHMS rows)"
    echo "  or the env stack was incomplete; deliver the log for triage."
fi
