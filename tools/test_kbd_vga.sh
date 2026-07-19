#!/usr/bin/env bash
# ============================================================================
# test_kbd_vga.sh -- boot-frontier test for the keyboard(8042)+VGA interfaces
# Project: EmulatR V5 (emulatrappuniv5).  JRN-VMB-006.  Git Bash on Windows.
# ASCII(128) only.
# ----------------------------------------------------------------------------
# Verifies the JRN-VMB-006 change: the SRM VGA color-text writes at 0xB8000
# now LAND in the VgaTextConsole (instead of faulting to UNHANDLED OUTER
# WRITE), the 8042 keyboard IRQ1 stays quiescent (no divert storm), and the
# boot advances at least to the SRM console frontier.
#
# Usage:  tools/test_kbd_vga.sh [model] [config] [extra Emulatr args...]
#   model   ds10|ds20|ds25|es40|es45      (default ds20)
#   config  relwithdebinfo|debug|release  (default relwithdebinfo)
# Env overrides:
#   MAXCYC  cycle cap (default 0 = UNCAPPED; stop cleanly with
#           'touch <rundir>/EMULATR_STOP', per the run_fw convention).  Set
#           e.g. MAXCYC=0x80000000 to bound a quick, self-terminating run.
#
# Outputs land under the run dir's ./logs (per the log/trace placement rule),
# timestamped:
#   logs/kbd_vga_boot_<model>_<ts>.log   console + stderr
#   logs/vga_screen_<model>_<ts>.txt     decoded 0xB8000 text framebuffer
# ============================================================================
set -euo pipefail

NAME="${1:-ds20}"; shift || true
CONFIG="${1:-relwithdebinfo}"
case "$CONFIG" in
    relwithdebinfo|debug|release) shift || true ;;
    *) CONFIG=relwithdebinfo ;;
esac

case "$NAME" in
    ds10) MODEL=DS10 ;; ds20) MODEL=DS20 ;; ds25) MODEL=DS25 ;;
    es40) MODEL=ES40 ;; es45) MODEL=ES45 ;;
    *) echo "usage: $0 [ds10|ds20|ds25|es40|es45] [relwithdebinfo|debug|release] [extra args]"; exit 2 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"    # project root (tools/..)
RUN_DIR="$ROOT/out/build/$CONFIG"
INI="$RUN_DIR/config/Emulatr.ini"
SRC_FW="$ROOT/firmware/${NAME}_v7_3.exe"
DST_FW="firmware/${NAME}_v7_3.exe"
MEM=4294967296
MAXCYC="${MAXCYC:-0}"
TS="$(date +%Y%m%d_%H%M%S)"
LOG="$RUN_DIR/logs/kbd_vga_boot_${NAME}_${TS}.log"
VGADUMP="$RUN_DIR/logs/vga_screen_${NAME}_${TS}.txt"

# ---- preflight -------------------------------------------------------------
[[ -x "$RUN_DIR/Emulatr.exe" ]] || { echo "FATAL: $RUN_DIR/Emulatr.exe not found -- build first: tools/build_kbd_vga.sh $CONFIG"; exit 1; }
[[ -f "$SRC_FW" ]] || { echo "FATAL: firmware not found: $SRC_FW"; exit 1; }
[[ -f "$INI" ]]    || { echo "FATAL: ini not found: $INI"; exit 1; }

cd "$RUN_DIR"
mkdir -p logs firmware
cp -f "$SRC_FW" "$DST_FW"

# ---- set [System] model for this run; restore on exit ----------------------
cp -f "$INI" "$INI.testbak"
trap 'mv -f "$INI.testbak" "$INI"' EXIT
sed -i "s/^\(\s*model\s*=\s*\).*/\1${MODEL}/" "$INI"
rm -f EMULATR_STOP

# ---- diagnostics: VGA snapshot + boot-frontier checkpoints -----------------
export EMULATR_VGA_DUMP="$VGADUMP"
export EMULATR_CHECKPOINTS="boot0:0x20000000,bootpal:0x20000001,handoff:0x1ade60"

MAXFLAG=()
[[ "$MAXCYC" != "0" ]] && MAXFLAG=(--max-cycles "$MAXCYC")

echo "=== EmulatR kbd/VGA test =============================================="
echo "  model    : $MODEL   config: $CONFIG"
echo "  firmware : $DST_FW"
echo "  maxcyc   : ${MAXCYC}  (0 = uncapped; touch EMULATR_STOP to stop cleanly)"
echo "  console  : TCP 10023  (PuTTY raw; type 'b dqa0' at >>> to boot an OS)"
echo "  log      : $LOG"
echo "  vga dump : $VGADUMP   (written at emulator teardown)"
echo "======================================================================"

./Emulatr.exe --firmware "$DST_FW" --mem "$MEM" --no-autoload "${MAXFLAG[@]}" "$@" 2>&1 | tee "$LOG"

# ---- verdict ---------------------------------------------------------------
echo ""
echo "=== VERDICT (JRN-VMB-006) ============================================"
UNH=$(grep -c 'UNHANDLED OUTER WRITE' "$LOG" || true)
B8=$(grep -c 'offset=0x0*b8000' "$LOG" || true)
echo "  UNHANDLED OUTER WRITE events (any addr) : ${UNH}"
echo "  ... of which at 0xB8000 (should be 0)   : ${B8}"
echo "  NOTE: the UNHANDLED log only prints in EMULATR_BRINGUP_PROBES builds;"
echo "  the POSITIVE proof the writes landed is the VGA dump below."
echo ""
echo "--- boot-frontier checkpoints ---------------------------------------"
grep -a -E 'CKPT|checkpoint|boot0|bootpal|handoff|HALT|StopReason|jumping to bootstrap' "$LOG" | tail -30 || echo "  (no checkpoint lines matched)"
echo ""
echo "--- VGA text framebuffer (0xB8000) ----------------------------------"
if [ -s "$VGADUMP" ]; then
    sed -n '1,55p' "$VGADUMP"
else
    echo "  (empty -- framebuffer never written; check the run reached console init)"
fi
echo "======================================================================"
