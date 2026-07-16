#!/usr/bin/env bash
# run_ds20.sh -- DS20 SRM cold boot, NO trace firehose (fast boot / console check).
# Thin wrapper over run_srm_trace_full.sh (Tsunami 21272).  ASCII(128).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Project Architect: Timothy Peer.
#   ./tools/run_ds20.sh [relwithdebinfo|debug|release] [rebuild] [-- extra Emulatr args]
NOTRACE=1 exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_srm_trace_full.sh" ds20 "$@"
