#!/usr/bin/env bash
# ============================================================================
# run_initloop_trace.sh -- thin wrapper: capture the ES40 "Initializing...."
# stuck-loop body (0x12c270) with the retire-trace facility.
# ----------------------------------------------------------------------------
# Purpose : arm the DIAG-PC window over the stuck-loop range, then delegate to
#           the sibling run_es40_showdev.sh (which self-locates the run dir and
#           cd's into it).  0x12cxxx runs ONLY during the stuck phase (first hit
#           at cyc~429B), so a PC window alone is clean -- no cycle gate needed.
#           Round 1 goal: read what the loop polls (load memAddr), its compare,
#           and its exit branch -> "will it ever finish + why".
# Host    : Windows / Git Bash (PC); the delegate self-locates the run dir.
# Usage   : ./tools/run_initloop_trace.sh        (all args pass through)
#           repro: boot -> LFU 'exit' -> decline manual update -> Initializing....
# Output  : DIAG-PC lines land in the delegate's run log (see run_es40_showdev.sh).
# Note    : invoked via `bash` against a BASH_SOURCE-anchored sibling path, so it
#           neither relies on the executable bit nor on any project-root path math
#           (the old `cd ../../.. && exec TOOLS/...` was wrong-case and location-
#           dependent -- it broke when launched from EmulatRAppUniV5/tools/).
# ============================================================================
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export EMULATR_PLATFORM=silicon               # REAL_HW (the stuck path)
export EMULATR_UDELAYWARP=1                    # warp on (reproduce the exact scenario)
export EMULATR_DIAG_PCLO=0x12c000              # stuck-loop window (covers 0x12c270, 0x12d598)
export EMULATR_DIAG_PCHI=0x12d800
export EMULATR_DIAG_CAP=2000                   # up to 2000 retired insns in-window
export MAXCYC=600000000000                     # headroom past the update+Initializing (~432B)
echo "[initloop-trace] DIAG-PC armed 0x12c000-0x12d800 | silicon+warp | MAXCYC=$MAXCYC"
echo "[initloop-trace] repro: boot -> LFU 'exit' -> decline manual update -> Initializing...."
echo "[initloop-trace] once ~2000 'DIAG-PC:' lines are in the log, stop the run."

TARGET="$HERE/run_es40_showdev.sh"
[ -f "$TARGET" ] || { echo "FATAL: sibling not found: $TARGET" >&2; exit 1; }
exec bash "$TARGET" "$@"
