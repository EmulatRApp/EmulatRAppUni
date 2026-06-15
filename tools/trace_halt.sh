#!/usr/bin/env bash
# ============================================================================
# tools/trace_halt.sh -- console-armed retire trace + TIG bring-up canary.
# Deployed to <run-dir>/tools/ by CMake POST_BUILD (EMULATR_TOOL_FILES).
# Lives in tools/, operates on the run-dir ROOT (../) where Emulatr.exe is.
#
# Cold-boot to >>> with no trace noise; arm the trace FROM THE PROMPT, then
# `b dqa1` streams ONLY the boot command to _srm.trc (HW_LD a=<pa> v=<value>).
# HALTPROBE auto-logs nonzero TIG reads; EMULATR_TIG_TRACE logs any access to
# an UNMODELED TIG register (so a new one cannot hide behind a plausible 0).
#   At >>> :
#     e pmem:80130000040        <- smir (the Halt gate); expect 0 now
#     b dqa1                     <- should cross into the OpenVMS bootstrap
#     e pmem:80130000FF8         <- (if still blocked) ARM trace, then b dqa1
# ============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"   # run-dir root (next to Emulatr.exe)
EXE="Emulatr.exe"
FIRMWARE="firmware/ds10_v7_3.exe"
STAMP="$(date +%Y%m%d_%H%M%S)"
CONSOLE_LOG="trace_halt_${STAMP}.console.log"
export EMULATR_TRACE_DIR="${EMULATR_TRACE_DIR:-/d/EmulatR/traces}"; mkdir -p "$EMULATR_TRACE_DIR"
export EMULATR_TRACE_WINDOW=1                          # window-only sink (no GB cold-boot stream)
export EMULATR_TIG_TRACE="${EMULATR_TIG_TRACE:-1}"     # canary: unmodeled TIG access
cd "$RUN_DIR"
[[ -x "$EXE"      ]] || { echo "FATAL: $EXE not found in $RUN_DIR" >&2; exit 1; }
[[ -f "$FIRMWARE" ]] || { echo "FATAL: firmware missing: $RUN_DIR/$FIRMWARE" >&2; exit 1; }
cat <<MSG
[trace] run dir   : $RUN_DIR
[trace] env       : EMULATR_TRACE_WINDOW=1  EMULATR_TIG_TRACE=$EMULATR_TIG_TRACE
[trace] trace dir : $EMULATR_TRACE_DIR   (_srm.trc)
[trace] at >>> :  e pmem:80130000040 (smir; expect 0)   then   b dqa1
----------------------------------------------------------------------
MSG
./"$EXE" --no-autoload --firmware "$FIRMWARE" 2>&1 | tee "$CONSOLE_LOG"
echo "----------------------------------------------------------------------"
echo "[triage] HALTPROBE / TIG_TRACE:"; grep -niE "HALTPROBE|EMULATR_TIG_TRACE" "$CONSOLE_LOG" || echo "  (none)"
echo "[triage] halt lines:"; grep -niE "Halt Button|BOOT NOT POSSIBLE|AUTO_ACTION" "$CONSOLE_LOG" || echo "  (none)"
SRM="$(ls -t "$EMULATR_TRACE_DIR"/*_srm.trc 2>/dev/null | head -1 || true)"
echo "[triage] newest _srm.trc: ${SRM:-none}"
[[ -n "${SRM:-}" && -f "$SRM" ]] && { echo "--- _srm.trc tail ---"; tail -80 "$SRM"; }
echo "[done] console: $RUN_DIR/$CONSOLE_LOG"
