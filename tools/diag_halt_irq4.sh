#!/usr/bin/env bash
# ============================================================================
# diag_halt_irq4.sh -- 2026-06-14 runbook: disambiguate the IRQ4/halt gate
# (World A: clr_irq* clears a real source  vs  World C: model a source read).
# Deployed to <run-dir>/tools/ by CMake POST_BUILD; anchors to run-dir ROOT.
# Git Bash on Windows.  NO rebuild needed -- this is diagnosis only.
# ============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="Emulatr.exe"
FIRMWARE="firmware/ds10_v7_3.exe"
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="diag_halt_${STAMP}.log"

# --- environment ------------------------------------------------------------
export EMULATR_TRACE_DIR="${EMULATR_TRACE_DIR:-/d/EmulatR/traces}"; mkdir -p "$EMULATR_TRACE_DIR"
export EMULATR_TRACE_WINDOW=1        # REQUIRED: builds the window-only retire sink (arming no-ops without it)
export EMULATR_TIG_TRACE=1           # canary: logs any UNMODELED TIG-window access (pa+dir)

cd "$RUN_DIR"
[[ -x "$EXE"      ]] || { echo "FATAL: $EXE not found in $RUN_DIR" >&2; exit 1; }
[[ -f "$FIRMWARE" ]] || { echo "FATAL: firmware missing: $RUN_DIR/$FIRMWARE" >&2; exit 1; }

cat <<MSG
======================================================================
[diag] run dir : $RUN_DIR
[diag] env     : EMULATR_TRACE_WINDOW=1  EMULATR_TIG_TRACE=1
[diag] trace   : $EMULATR_TRACE_DIR   (_srm.trc)   log: $RUN_DIR/$LOG
----------------------------------------------------------------------
WHEN '>>>' APPEARS (in PuTTY), do these IN ORDER and paste the output:

  1) SNAPSHOT THE INTERRUPT STATE (no rebuild; World B sanity):
       >>> e pmem:801a0000300      # Cchip DRIR  -- expect 0 / <55> / <63>, NOT all-ones
       >>> e pmem:801a0000280      # Cchip DIR0
       >>> e pmem:80130000040      # smir        -- expect 0 (already fixed)

  2) ARM THE RETIRE TRACE (must be a READ -- returns 7FFFFFFFFFFFFFFF):
       >>> e pmem:80130000FF8      # ARM  (a DEPOSIT to this PA DISARMS -- do not)

  3) BOOT (captured instruction-by-instruction):
       >>> b dqa1

  4) STOP CLEANLY (from a SECOND Git Bash) to flush _srm.trc:
       touch "$RUN_DIR/EMULATR_STOP"
======================================================================
MSG

./"$EXE" --no-autoload --firmware "$FIRMWARE" 2>&1 | tee "$LOG"

# --- triage (runs after the graceful stop / exit) ---------------------------
echo "----------------------------------------------------------------------"
echo "[triage] canary (unmodeled TIG accesses) + HALTPROBE + halt lines:"
grep -niE "EMULATR_TIG_TRACE|HALTPROBE|Halt Button|BOOT NOT" "$LOG" | tail -30 || echo "  (none)"
SRM="$(ls -t "$EMULATR_TRACE_DIR"/*_srm.trc 2>/dev/null | head -1 || true)"
echo "[triage] newest _srm.trc: ${SRM:-none}  ($( [[ -n "${SRM:-}" ]] && wc -l < "$SRM" || echo 0) lines)"
if [[ -n "${SRM:-}" && -f "$SRM" ]]; then
  echo "--- TIG (0x80130xxxx) + Cchip (0x801a0xxxx) accesses in the trace ---"
  grep -niE "pa=0x0*80130|pa=0x0*801a0" "$SRM" | tail -60 || echo "  (none)"
  echo "--- last 40 retire lines (the read just before BOOT NOT POSSIBLE) ---"
  grep -E "^RET " "$SRM" | tail -40 || echo "  (no RET lines -- window did not arm: confirm step 2 returned 7FFF...)"
fi
echo "[done] full log: $RUN_DIR/$LOG"
