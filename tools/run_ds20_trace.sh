#!/usr/bin/env bash
# ============================================================================
# run_ds20_trace.sh -- thin wrapper: DS20 comprehensive SRM trace.
# ----------------------------------------------------------------------------
# Purpose : cold-boot the DS20 firmware with the full SRM trace, by delegating
#           to run_srm_trace_full.sh with the model pinned to ds20.
# Host    : Windows / Git Bash (PC) and macOS/Linux -- the delegate is host-aware.
# Usage   : ./tools/run_ds20_trace.sh [relwithdebinfo|debug|release] [rebuild] [-- args]
# Output  : console + traces land under the run dir's traces/ (see the delegate).
# Note    : invoked via `bash` so it does not rely on the executable bit, which
#           an NTFS/Windows checkout frequently drops.
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="$SCRIPT_DIR/run_srm_trace_full.sh"
[ -f "$TARGET" ] || { echo "FATAL: sibling not found: $TARGET" >&2; exit 1; }
exec bash "$TARGET" ds20 "$@"
