#!/usr/bin/env bash
# ============================================================================
# trace_halt.sh -- console-armed retire trace + TIG bring-up canary.
# Deployed to the run dir by CMake POST_BUILD (source: Emulatr/scripts/).
#
# Cold-boot to >>> with no trace noise; arm the trace FROM THE PROMPT, then
# `b dqa1` streams ONLY the boot command to _srm.trc (HW_LD a=<pa> v=<value>).
# HALTPROBE auto-logs nonzero TIG reads; EMULATR_TIG_TRACE logs any access to
# an UNMODELED TIG register (so a new one cannot hide behind a plausible 0).
#
#   At >>> :
#     e pmem:80130000040        <- smir (the Halt gate); expect 0 now
#     b dqa1                     <- should cross into the OpenVMS bootstrap
#     e pmem:80130000FF8         <- (if still blocked) ARM trace, then b dqa1
#     d pmem:80130000FF8 200000  <- (alt) bounded N-instruction window; 0 = off
#
# Run from Git Bash on Windows.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$SCRIPT_DIR"
EXE="Emulatr.exe"
FIRMWARE="firmware/ds10_v7_3.exe"
STAMP="$(date +%Y%m%d_%H%M%S)"
CONSOLE_LOG="trace_halt_${STAMP}.console.log"

export EMULATR_TRACE_DIR="${EMULATR_TRACE_DIR:-/d/EmulatR/traces}"
mkdir -p "$EMULATR_TRACE_DIR"
# Window-only sink: opens _srm.trc but emits ONLY while the console arms the
# window via the TIG trace-arm reg.  Do NOT pass --trace (that would arm the
# continuous RETIRE_COMPACT stream -> multi-GB cold-boot trace).
export EMULATR_TRACE_WINDOW=1
# Bring-up canary: announce any access to an unmodeled TIG control register.
export EMULATR_TIG_TRACE="${EMULATR_TIG_TRACE:-1}"

cd "$RUN_DIR"
[[ -x "$EXE"      ]] || { echo "FATAL: $EXE not found in $RUN_DIR" >&2; exit 1; }
[[ -f "$FIRMWARE" ]] || { echo "FATAL: firmware missing: $RUN_DIR/$FIRMWARE" >&2; exit 1; }

cat <<MSG
[trace] EMULATR_TRACE_WINDOW=1  EMULATR_TIG_TRACE=$EMULATR_TIG_TRACE
[trace] trace dir : $EMULATR_TRACE_DIR   (_srm.trc)
[trace] console   : $RUN_DIR/$CONSOLE_LOG
[trace] at >>> :  e pmem:80130000040 (smir; expect 0)   then   b dqa1
[trace] HALTPROBE / EMULATR_TIG_TRACE lines (auto) appear on the console.
----------------------------------------------------------------------
MSG

./"$EXE" --no-autoload --firmware "$FIRMWARE" 2>&1 | tee "$CONSOLE_LOG"

echo "----------------------------------------------------------------------"
echo "[triage] HALTPROBE / TIG_TRACE (firmware TIG accesses):"
grep -niE "HALTPROBE|EMULATR_TIG_TRACE" "$CONSOLE_LOG" || echo "  (none -- clean)"
echo "[triage] halt console lines:"
grep -niE "Halt Button|BOOT NOT POSSIBLE|AUTO_ACTION" "$CONSOLE_LOG" || echo "  (none)"
SRM="$(ls -t "$EMULATR_TRACE_DIR"/*_srm.trc 2>/dev/null | head -1 || true)"
echo "[triage] newest _srm.trc: ${SRM:-none}"
if [[ -n "${SRM:-}" && -f "$SRM" ]]; then
  echo "--- _srm.trc tail (last 80 lines) ---"; tail -80 "$SRM"
fi
echo "[done] full console: $RUN_DIR/$CONSOLE_LOG"
