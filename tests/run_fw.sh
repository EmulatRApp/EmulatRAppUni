#!/usr/bin/env bash
# ============================================================================
# run_fw.sh -- launch EmulatR V4 against one v7_3 firmware image, 4 GiB RAM.
# Project: EmulatR -- Alpha AXP / EV6 (V4).  Git Bash on Windows.
# ----------------------------------------------------------------------------
# Usage:   ./run_fw.sh <ds10|ds20|ds25|es40|es45>   [extra Emulatr.exe args...]
# Example: ./run_fw.sh es40 --max-cycles 0x10000000
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
  *) echo "usage: $0 <ds10|ds20|ds25|es40|es45> [extra args]"; exit 2 ;;
esac

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

echo "=== EmulatR run ============================================="
echo "  firmware : $DST_FW"
echo "  model    : $MODEL   (ini-driven; no --model flag exists)"
echo "  memory   : $MEM bytes (4 GiB)"
echo "  console  : TCP 10023  (connect PuTTY/plink, raw mode)"
echo "  log      : $BUILD_DIR/$LOG"
echo "  extra    : $*"
echo "============================================================="

# Run. 2>&1 | tee captures StopReason / fault / exit cycle into the log.
./Emulatr.exe --firmware "$DST_FW" --mem "$MEM" "$@" 2>&1 | tee "$LOG"
