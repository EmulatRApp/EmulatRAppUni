#!/usr/bin/env bash
# ============================================================================
# snap_vmb_capture.sh -- JRN-VMB-002 E-6 state capture (web's deterministic way).
#
# Captures the money-shot memory state at the VMB entry fetch (0x20000000) via
# a PC-watchpoint snapshot -- NOT a guest-console oem_string trigger (E-6.1
# rejects console-gated snapshots: non-deterministic + boundary violation).
#
# What it produces per boot:
#   - snapshots/predig_vmb_entry_cyc<cyc>.axpsnap  (PC hit 0x20000000; the state
#       that answers R-1 image@0x5bc000 / R-2 PTE@0x3ff04000 / R-3 vptb+va_ctl)
#   - snapshots/auto_halt_<ts>_<cyc>.axpsnap        (halt-save, --autosnapshot on)
#   - the console banner in the log = the image's TRUE version (E-5 cell A /
#       S-A), since flash was moved aside so the boot seeds from the .exe.
#   - "ITBPROBE ARMED va=..." proving the probe gate armed (E-4).
#
# ENVIRONMENT ALREADY PINNED (do NOT undo, do NOT run LFU 'u srm' -- P-6):
#   - flash .rom moved aside (cell A: fresh authoritative-image seed).
#   - platform coherent: Emulatr.ini model=DS20.
# REQUIRES a build with EMULATR_BRINGUP_PROBES=ON (E-4 arm-line + ITBPROBE):
#     cmake -DEMULATR_BRINGUP_PROBES=ON . && ./tools/build_emulatr.sh
#
# Two boots = two cells for the R-1/R-2/R-3 compare (web E-6.4 A-vs-A):
#     SRC=dqa0 bash tools/snap_vmb_capture.sh      # boot the disk
#     SRC=dqa1 bash tools/snap_vmb_capture.sh      # boot the CD
# At P00>>> type:  b $SRC     (the script prints the exact line).
#
# Overrides: PROBE_VA (default 0x20000000), SRC (default dqa0),
#            EMULATR_CONSOLE_PORT (10023), MAXCYC (22000000000).
# ASCII(128) only.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if   [[ -x "$SCRIPT_DIR/Emulatr.exe"    ]]; then RUN_DIR="$SCRIPT_DIR"
elif [[ -x "$SCRIPT_DIR/../Emulatr.exe" ]]; then RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else echo "FATAL: Emulatr.exe not found next to this script or one level up"; exit 1; fi
cd "$RUN_DIR"

EXE="./Emulatr.exe"
FW="firmware/ds20_v7_3.exe"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-22000000000}"
PROBE_VA="${PROBE_VA:-0x20000000}"
SRC="${SRC:-dqa0}"
mkdir -p logs traces snapshots
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="logs/snap_vmb_${SRC}_${STAMP}.log"

[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: $FW not found (DS20 firmware)"; exit 1; }

# Guard: warn if a live flash .rom exists (means NOT cell A -- boot would use
# persisted flash, not a fresh authoritative-image seed).
if [[ -f firmware/ds20_v7_3.rom ]]; then
    echo "WARN: firmware/ds20_v7_3.rom exists -- this is NOT cell A (flash absent)."
    echo "      Move it aside first for an authoritative-image baseline."
fi

export EMULATR_CONSOLE_PORT="$PORT"
export EMULATR_CONSOLE_MIRROR=1
export EMULATR_ITBPROBE_VA="$PROBE_VA"

echo "======================================================================="
echo "RUN_DIR    = $RUN_DIR"
echo "exe        = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "boot src   = $SRC   (type 'b $SRC' at P00>>>)"
echo "probe VA   = EMULATR_ITBPROBE_VA=$PROBE_VA"
echo "snap-on-pc = $PROBE_VA,$(printf '0x%x' $(( PROBE_VA | 1 )) )  tag=vmb_entry"
echo "log        = $RUN_DIR/$LOG"
echo "snapshots  = $RUN_DIR/snapshots/"
echo "flash      = $( [[ -f firmware/ds20_v7_3.rom ]] && echo PRESENT || echo 'ABSENT (cell A: fresh .exe seed)')"
echo "REQUIRES a build with EMULATR_BRINGUP_PROBES=ON."
echo "Watch for: 'ITBPROBE ARMED', the console banner (image version), and"
echo "           'predig snapshot fired at pc=...20000000'."
echo "======================================================================="

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot on \
    --snapshot-on-pc "${PROBE_VA},$(printf '0x%x' $(( PROBE_VA | 1 )) )" \
    --snapshot-name-tag "vmb_entry_${SRC}" \
    --max-cycles "$MAXCYC" \
    2>&1 | tee "$LOG"

echo "-----------------------------------------------------------------------"
echo "banner (image TRUE version, E-5.A):"
grep -aiE 'AlphaServer|Console V7' "$LOG" | tail -3 || true
echo "ITBPROBE arm-line (E-4):"
grep -aE 'ITBPROBE ARMED|ITBPROBE NOT-ARMED' "$LOG" | head -1 || echo "  (none -- build lacks EMULATR_BRINGUP_PROBES?)"
echo "predig snapshot fires:"
grep -aE 'predig snapshot fired' "$LOG" || echo "  (none -- PC 0x20000000 not reached this boot)"
echo "snapshots present:"
ls -t snapshots/*.axpsnap 2>/dev/null | head -6 || echo "  (no .axpsnap written)"
