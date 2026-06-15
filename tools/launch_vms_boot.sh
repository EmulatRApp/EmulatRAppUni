#!/usr/bin/env bash
# ============================================================================
# tools/launch_vms_boot.sh -- cold-boot EmulatR (DS10/EV6) and boot OpenVMS
# Alpha V8.2 from dqa1.  Deployed to <run-dir>/tools/ by CMake POST_BUILD.
# Lives in tools/, operates on the run-dir ROOT (../).  Git Bash on Windows.
# ============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="Emulatr.exe"
MANIFEST="ds10_platform.win"
FIRMWARE="firmware/ds10_v7_3.exe"
ISO_NAME="alpha082.iso"
PORT=10023
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="boot_vms_${STAMP}.log"
cd "$RUN_DIR"
[[ -x "$EXE"      ]] || { echo "FATAL: $EXE not found in $RUN_DIR" >&2; exit 1; }
[[ -f "$FIRMWARE" ]] || { echo "FATAL: firmware missing: $RUN_DIR/$FIRMWARE" >&2; exit 1; }
grep -q "$ISO_NAME" "$MANIFEST" \
  || { echo "FATAL: $MANIFEST does not reference $ISO_NAME -- dqa1 not wired to OpenVMS" >&2; exit 1; }
echo "[launch] run dir : $RUN_DIR ; dqa1 -> $ISO_NAME ; cold boot (no autoload)"
echo "[launch] console TCP 127.0.0.1:${PORT}; at >>> :  e pmem:80130000040 (smir=0) ; b dqa1"
echo "----------------------------------------------------------------------"
./"$EXE" --no-autoload --firmware "$FIRMWARE" 2>&1 | tee "$LOG"
echo "----------------------------------------------------------------------"
echo "[triage] memory + halt + frontier:"
grep -niE "memorySize from ini|1024 Meg|64 Meg|Halt Button|BOOT NOT POSSIBLE|kFaultUnimplemented|OPCDEC|HALT|UNHANDLED" "$LOG" | tail -20 || echo "  (none)"
echo "[done] full log: $RUN_DIR/$LOG"
