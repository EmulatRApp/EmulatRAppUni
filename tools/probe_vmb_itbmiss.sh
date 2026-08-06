#!/usr/bin/env bash
# ============================================================================
# probe_vmb_itbmiss.sh -- JRN-VMB-001 Step 1.  Capture the ITB miss at the VMB
# entry VA (default 0x20000000) so we can read the miss state BEFORE PALcode
# runs: STRANDED (H2 VA_FORM base), palBase (retire() halt residual), and raw
# va + pal (H3 PC<0>).  Boots the DS20 CD path (the run that halts at VMB).
#
# REQUIRES a build with EMULATR_BRINGUP_PROBES=ON.  That gate is DEFAULT OFF
# (CMakeLists.txt) and is a COMPILE gate, not a runtime flag, so rebuild first:
#     cmake -DEMULATR_BRINGUP_PROBES=ON . && ./tools/build_emulatr.sh
# Without it the ITBPROBE block is not compiled in and this run is silent.
#
# Console on TCP 10023 (PuTTY).  At P00>>> issue:  b dqa1   (CD).  VMB then
# fetches 0x20000000, misses the ITB, and the probe fires (capped at 16 hits).
#
# Self-locates the run dir (dir holding Emulatr.exe + firmware/ds20_v7_3.exe),
# whether this script lives in <run-dir>/tools/ or the run dir itself.
#
# Overrides:  PROBE_VA=<hex>          target VA (default 0x20000000)
#             MAXCYC=<n>              max guest cycles (default 22000000000)
#             EMULATR_CONSOLE_PORT    console TCP port (default 10023)
#             IDE_TRACE=1             also turn the IDE probe trace on (noisy)
# ASCII(128) only.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# run dir = script dir if it holds the exe, else its parent (script in tools/)
if   [[ -x "$SCRIPT_DIR/Emulatr.exe"    ]]; then RUN_DIR="$SCRIPT_DIR"
elif [[ -x "$SCRIPT_DIR/../Emulatr.exe" ]]; then RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else echo "FATAL: Emulatr.exe not found next to this script or one level up"; exit 1; fi
cd "$RUN_DIR"

EXE="./Emulatr.exe"
FW="firmware/ds20_v7_3.exe"
# 2026-07-31: per-instance run environment (FILE 7).  Sourcing this owns
# STEM/TAG/TS, the log-name helper, and the per-instance flash NVRAM,
# sentinels and console port -- concurrent instances no longer collide.
. "$SCRIPT_DIR/emulatr_run_env.sh"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-22000000000}"
PROBE_VA="${PROBE_VA:-0x20000000}"
mkdir -p logs traces
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="$(emulatr_log probe_itbmiss)"   # logs/<TAG>_probe_itbmiss_<TS>.log (FILE 7)

[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: $FW not found (DS20 firmware)"; exit 1; }

export EMULATR_CONSOLE_PORT="$PORT"
export EMULATR_CONSOLE_MIRROR=1
export EMULATR_ITBPROBE_VA="$PROBE_VA"
if [[ "${IDE_TRACE:-0}" == "1" ]]; then export EMULATR_IDE_TRACE=1; fi

echo "======================================================================="
echo "RUN_DIR   = $RUN_DIR"
echo "exe       = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "fw        = $FW  (DS20)"
echo "probe VA  = EMULATR_ITBPROBE_VA=$PROBE_VA  (gate matches PC<0>-masked)"
echo "console   = localhost:$PORT  (PuTTY:  putty -telnet localhost $PORT)"
echo "log       = $RUN_DIR/$LOG"
echo "REQUIRES a build with EMULATR_BRINGUP_PROBES=ON (see header)."
echo "At P00>>>:   b dqa1   (boot the CD; VMB then faults at $PROBE_VA)"
echo "======================================================================="

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee "$LOG"

echo "-----------------------------------------------------------------------"
N="$(grep -c 'ITBPROBE' "$LOG" || true)"
echo "ITBPROBE lines: ${N}"
if [[ "${N:-0}" == "0" ]]; then
    echo "  NO ITBPROBE line.  Either the miss never happened (re-examine F-1),"
    echo "  or the build lacks EMULATR_BRINGUP_PROBES, or the gate VA is wrong."
    echo "  Do NOT conclude anything about PAL from an empty result."
else
    grep 'ITBPROBE' "$LOG" | tail -20 || true
    if grep -q 'STRANDED=1' "$LOG"; then
        echo "  -> H2 CONFIRMED: VA_FORM base stranded (vptb!=0, va_ctl<63:43>==0)"
    fi
    if grep -qE 'ITBPROBE MISS n=[0-9]+ .* pal=1' "$LOG"; then
        echo "  -> H3 present: PC<0> set at the fetch (raw va shows bit 0)."
    fi
    if grep -qE 'ITBPROBE MISS n=16' "$LOG"; then
        echo "  -> cap hit (n=16): PAL may be live-locking on the refill -> Step 2."
    fi
fi
