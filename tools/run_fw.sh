#!/usr/bin/env bash
# ============================================================================
# run_fw.sh -- launch EmulatR V4 against one v7_3 firmware image, 4 GiB RAM.
# Project: EmulatR -- Alpha AXP / EV6 (V4).  Git Bash on Windows.
# ----------------------------------------------------------------------------
# Usage:   ./run_fw.sh <ds10|ds20|ds25|es40|es45> [cold] [extra Emulatr args...]
# Example: ./run_fw.sh ds20 cold
#          ./run_fw.sh es40 --max-cycles 0x10000000
#
#   cold  (optional 2nd arg) -- force a genuine COLD boot: pass --no-autoload
#         (ignore snapshots) AND purge stale auto_*.axpsnap to reclaim disk.
#         Without it the run is WARM: autoloads the newest snapshot and keeps
#         writing periodic autosnaps (the normal fast-resume path).
#
# What it does (faithful, self-contained):
#   1. Resolves the firmware basename to <name>_v7_3.exe in the SOURCE
#      firmware dir and copies it into the build-dir firmware/ (cwd-relative).
#   2. Sets [System] model in config/EmulatrV4.ini to the matching variant for
#      this run, backing up + restoring the ini on exit (trap).
#   3. Launches ./Emulatr.exe --firmware firmware/<name>_v7_3.exe --mem 4 GiB
#      with cwd = build dir, teeing console+stderr to fw_<name>_<ts>.out.
#
# Console: SRM console listens on TCP 10023 (see [SRMConsole] in the ini).
#   Connect with PuTTY/plink in raw mode to interact ( >>> , boot, etc.).
# ============================================================================
set -euo pipefail

NAME="${1:-}"
shift || true
case "$NAME" in
  ds10) MODEL=DS10 ;;
  ds20) MODEL=DS20 ;;
  ds25) MODEL=DS25 ;;
  es40) MODEL=ES40 ;;
  es45) MODEL=ES45 ;;
  *) echo "usage: $0 <ds10|ds20|ds25|es40|es45> [cold] [extra Emulatr args]"; exit 2 ;;
esac

# Optional 2nd positional arg: "cold" -> genuine cold boot.  Anything else is
# passed straight through to Emulatr.exe.
COLD=0
if [ "${1:-}" = "cold" ] || [ "${1:-}" = "COLD" ]; then COLD=1; shift; fi

# Anchor to this script's dir (the build dir) so all relative paths resolve.
BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$BUILD_DIR"

SRC_FW="$BUILD_DIR/../../../firmware/${NAME}_v7_3.exe"   # source firmware tree
DST_FW="firmware/${NAME}_v7_3.exe"                       # cwd-relative target
INI="config/EmulatrV4.ini"
MEM=4294967296                                           # 4 GiB in bytes
TS="$(date +%Y%m%d-%H%M%S)"
LOG="fw_${NAME}_${TS}.out"

# ---- preflight -------------------------------------------------------------
[[ -x "./Emulatr.exe" ]] || { echo "FATAL: ./Emulatr.exe not found in $BUILD_DIR"; exit 1; }
[[ -f "$SRC_FW" ]]       || { echo "FATAL: firmware not found: $SRC_FW"; exit 1; }
[[ -f "$INI" ]]          || { echo "FATAL: ini not found: $INI"; exit 1; }
mkdir -p firmware
cp -f "$SRC_FW" "$DST_FW"

# ---- set model in ini for this run; restore on exit ------------------------
cp -f "$INI" "$INI.runbak"
trap 'mv -f "$INI.runbak" "$INI"' EXIT
# Replace the first "model = ..." line under [System].
sed -i "s/^\(\s*model\s*=\s*\).*/\1${MODEL}/" "$INI"

# ---- stop-sentinel + (cold) snapshot hygiene -------------------------------
rm -f EMULATR_STOP                       # never start with a stale stop sentinel
COLD_FLAG=""
MODE="WARM (autoload newest + autosnap)"
if [ "$COLD" = "1" ]; then
    COLD_FLAG="--no-autoload"
    MODE="COLD (--no-autoload; stale snapshots purged)"
    # --no-autoload ignores snapshots; purge stale auto_*.axpsnap to reclaim
    # disk (4.3 GiB each) and guarantee a clean cold boot.  predig_* anchors
    # (non-prunable fast-forward checkpoints) are preserved.
    if ls snapshots/auto_*.axpsnap >/dev/null 2>&1; then
        freed=$(du -ch snapshots/auto_*.axpsnap 2>/dev/null | tail -1 | cut -f1)
        rm -f snapshots/auto_*.axpsnap
        echo "cold-start: purged stale auto_*.axpsnap (${freed} reclaimed)"
    fi
fi

echo "=== EmulatR run [$MODE] ====================================="
echo "  firmware : $DST_FW"
echo "  model    : $MODEL   (ini-driven; no --model flag exists)"
echo "  memory   : $MEM bytes (4 GiB)"
echo "  console  : TCP 10023  (connect PuTTY/plink, raw mode)"
echo "  log      : $BUILD_DIR/$LOG"
echo "  extra    : $*"
echo "============================================================="

# Run. 2>&1 | tee captures StopReason / fault / exit cycle into the log.
./Emulatr.exe --firmware "$DST_FW" --mem "$MEM" $COLD_FLAG "$@" 2>&1 | tee "$LOG"
