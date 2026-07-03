#!/usr/bin/env bash
# run_es40_trace.sh -- thin wrapper: ES40 comprehensive SRM trace.
# Passes all args through (config/rebuild/-- extra) to the general launcher.
#   ./tools/run_es40_trace.sh [relwithdebinfo|debug|release] [rebuild] [-- args]
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_srm_trace_full.sh" es40 "$@"
