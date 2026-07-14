#!/usr/bin/env bash
# ============================================================================
# run_es40_rscc_ab_warp.sh -- A/B CASE A (WARP ON).
#
# Cold-boot ES40 in SILICON mode with the RSCC/warp instrumentation armed
# (EMULATR_RSCC_DIAG=1) AND the idle-tick warp ON (EMULATR_IDLEWARP=1).  This is
# the configuration that reproduced the LFU "Initializing...." hang: warp is
# baked into the guest-visible RSCC (RSCC == cycleCount, and cycleCount carries
# warpCycles).
#
# Pair with tools/run_es40_rscc_ab_nowarp.sh (CASE B, warp OFF).  Diff the two
# .summary files: if CASE A shows an inflated RSCCDIAG-DELAY target that CASE B
# does not, a cycle-count warp is inflating the micro_delay target (H-warp).
#
# PREREQ: the EMULATR_RSCC_DIAG probes from
#   journals/20260713_es40_lfu_rscc_warp_instrumentation_spec.md
# must be landed and the diagnostic build (EMULATR_BRINGUP_PROBES) rebuilt.
# Until then this runs but emits no RSCCDIAG-* rows.
#
# Location: EmulatRAppUniV4/Emulatr/tools/ (project tools dir).  Self-locates.
#
#   Usage:   ./tools/run_es40_rscc_ab_warp.sh
#   To reproduce the exact hang: at P00>>> type `lfu`, then `exit`, while the
#   diagnostic is armed -- the RSCCDIAG-DELAY rows at 0x6a514 then show the
#   console_restart delay's target vs current.
#   Toggles: MAXCYC=<n> (default from run_es40_showdev.sh; warp keeps it modest)
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"
SHOWDEV="$SCRIPT_DIR/run_es40_showdev.sh"
[[ -x "$SHOWDEV" ]] || { echo "FATAL: $SHOWDEV not found/executable"; exit 1; }

echo "=== ES40 RSCC A/B -- CASE A: WARP ON (EMULATR_IDLEWARP=1) ==="
echo "    platform=silicon  rscc_diag=1  idlewarp=1"

# CASE A knobs: silicon + diag + warp.  PLATFORM/WARP are consumed by
# run_es40_showdev.sh; EMULATR_RSCC_DIAG is read by the new probes.
export PLATFORM="silicon"
export WARP="1"
export EMULATR_RSCC_DIAG="1"

# Run the shared ES40 silicon harness (handles model, disk, PuTTY, restore).
"$SHOWDEV" || true

# ---- post-process: extract the A/B summary from the newest showdev log ------
LOG="$(ls -t "$PROJ"/*/run_es40_showdev_*.log \
             "$PROJ"/out/build/*/run_es40_showdev_*.log 2>/dev/null | head -1 || true)"
if [[ -z "$LOG" || ! -f "$LOG" ]]; then
    echo "NOTE: no run_es40_showdev_*.log found to summarize."
    exit 0
fi
SUM="${LOG%.log}.rscc_warpON.summary"
{
    echo "# CASE A (WARP ON) summary of: $LOG"
    echo "## WARPLEDGER (cycle-count discontinuities)"
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
