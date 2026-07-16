#!/usr/bin/env bash
# ============================================================================
# es40_fast_decompress.sh -- drive the EMULATR_FAST_DECOMPRESS lever for the
# ES40 silicon boot without pasting env vars by hand.  Thin wrapper over
# run_es40_showdev.sh; all model/disk/PuTTY setup and the logs/ + traces/
# placement (emulatr-log-trace-placement) come from that script unchanged.
#
# The lever is WITHIN-RUN: "snapshot" still decompresses faithfully on the first
# init, but mints a complete-state snapshot at the firmware's init->console
# handoff (pc == entryPa) to firmware/<stem>.axpsnap, so a within-run module
# reset can re-enter there instead of re-paying the decompression.  The snapshot
# is delete-then-regenerated at the mint (save) phase, survives exit/abort as a
# diagnostic, and is NOT consumed by any later run.
#
# Usage:
#   tools/es40_fast_decompress.sh snapshot   # faithful first init + mint the
#                                            # entry snapshot for within-run reset
#   tools/es40_fast_decompress.sh faithful   # Oracle end-to-end (default)
#
# PLATFORM / WARP pass straight through (default silicon + warp).  After the run
# the minted snapshot is firmware/<stem>.axpsnap; this script reports it.
#
# Self-locating (tools/ -> repo root).  ASCII(128) only.
# ============================================================================
set -euo pipefail

MODE="${1:-faithful}"
case "$MODE" in
    snapshot|faithful) ;;
    *) echo "usage: $(basename "$0") {snapshot|faithful}"; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHOWDEV="$SCRIPT_DIR/run_es40_showdev.sh"
[[ -f "$SHOWDEV" ]] || { echo "FATAL: run_es40_showdev.sh not found beside this script"; exit 1; }

echo "=== EMULATR_FAST_DECOMPRESS=$MODE  (PLATFORM=${PLATFORM:-silicon} WARP=${WARP:-1}) ==="

EMULATR_FAST_DECOMPRESS="$MODE" \
PLATFORM="${PLATFORM:-silicon}" \
WARP="${WARP:-1}" \
    bash "$SHOWDEV" || true

# ---- post-run: report the minted entry snapshot (firmware/<stem>.axpsnap) ----
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
NEWEST="$(ls -t "$REPO"/out/build/*/firmware/*.axpsnap \
                 "$REPO"/*/firmware/*.axpsnap 2>/dev/null | head -1 || true)"
if [[ -n "$NEWEST" ]]; then
    echo "=== entry snapshot: $NEWEST ($(stat -c %s "$NEWEST" 2>/dev/null) bytes) ==="
elif [[ "$MODE" == "snapshot" ]]; then
    echo "=== NOTE: no <stem>.axpsnap minted -- did the boot reach entryPa? (check the log) ==="
fi
