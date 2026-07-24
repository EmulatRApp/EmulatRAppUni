#!/usr/bin/env bash
# ============================================================================
# run_es40_rscc_ab_nowarp.sh -- A/B CASE B (WARP OFF).
#
# Cold-boot ES40 in SILICON mode with the RSCC/warp instrumentation armed
# (EMULATR_RSCC_DIAG=1) and the idle-tick warp OFF (EMULATR_IDLEWARP unset).
# This is the faithful-timing control: RSCC == retired cycles (no warp offset in
# cycleCount), so any RSCC-based delay target is the guest's true calibration.
#
# Pair with tools/run_es40_rscc_ab_warp.sh (CASE A, warp ON).  Diff the two
# .summary files:
#   - If CASE B's console_restart delay COMPLETES (reaches the outtig reset /
#     returns toward P00) while CASE A hangs -> a warp is inflating the
#     micro_delay target (H-warp CONFIRMED).
#   - If BOTH hang with an identical, sane target -> warp is exonerated; look at
#     DIVERT-REI-EXACT for register corruption of the loop operands (H-corrupt).
#
# COST: with no warp the real-hardware init/idle grind runs at full length, so
# reaching the console (and any LFU repro) takes far more cycles/wall time.
# MAXCYC is raised accordingly; expect a long run.  This is the price of a clean
# control.
#
# PREREQ: the EMULATR_RSCC_DIAG probes from
#   journals/20260713_es40_lfu_rscc_warp_instrumentation_spec.md
# must be landed and the diagnostic build (EMULATR_BRINGUP_PROBES) rebuilt.
#
# Location: EmulatRAppUniV4/Emulatr/tools/ (project tools dir).  Self-locates.
#
#   Usage:   ./tools/run_es40_rscc_ab_nowarp.sh
#            MAXCYC=<n>   -> override the raised default cap
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"
SHOWDEV="$SCRIPT_DIR/run_es40_showdev.sh"
# Test for existence with -f (NOT -x) and run via `bash` below: an NTFS/Windows
# checkout frequently drops the executable bit, so -x would false-FATAL here.
[[ -f "$SHOWDEV" ]] || { echo "FATAL: sibling not found: $SHOWDEV" >&2; exit 1; }

echo "=== ES40 RSCC A/B -- CASE B: WARP OFF (faithful timing) ==="
echo "    platform=silicon  rscc_diag=1  idlewarp=OFF"
echo "    NOTE: no warp -> the real-HW grind runs full length; this is a LONG run."

# CASE B knobs: silicon + diag, NO warp.  Explicitly ensure IDLEWARP is unset.
export PLATFORM="silicon"
export EMULATR_RSCC_DIAG="1"
unset  WARP || true
unset  EMULATR_IDLEWARP || true

# Raise the cycle cap since nothing is skipped (override with MAXCYC=...).
export MAXCYC="${MAXCYC:-400000000000}"   # 400B: give the un-warped grind room

bash "$SHOWDEV" || true

# ---- post-process: extract the A/B summary from the newest showdev log ------
LOG="$(ls -t "$PROJ"/*/run_es40_showdev_*.log \
             "$PROJ"/out/build/*/run_es40_showdev_*.log 2>/dev/null | head -1 || true)"
if [[ -z "$LOG" || ! -f "$LOG" ]]; then
    echo "NOTE: no run_es40_showdev_*.log found to summarize."
    exit 0
fi
SUM="${LOG%.log}.rscc_warpOFF.summary"
{
    echo "# CASE B (WARP OFF) summary of: $LOG"
    echo "## WARPLEDGER (should be EMPTY -- no warp in this case)"
    grep -E 'WARPLEDGER|IDLETICKWARP' "$LOG" | tail -40 || true
    echo "## RSCCDIAG-DELAY (delay-loop target vs current at 0x6a514)"
    grep -E 'RSCCDIAG-DELAY' "$LOG" | tail -60 || true
    echo "## RSCCDIAG-CAL (calibration cycles/us), if present"
    grep -E 'RSCCDIAG-CAL' "$LOG" | tail -20 || true
    echo "## DIVERT-REI-EXACT (matched-pair register diffs)"
    grep -E 'DIVERT-REI-EXACT' "$LOG" | tail -40 || true
    echo "## last console line"
    grep -E 'CON COM1|>>>|Initializing' "$LOG" | tail -6 || true
} > "$SUM"
echo "summary -> $SUM"
echo "A/B: diff this against the CASE A *.rscc_warpON.summary"
