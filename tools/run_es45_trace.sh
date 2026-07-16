#!/usr/bin/env bash
# run_es45_trace.sh -- ES45 comprehensive SRM trace (full retire firehose -> traces/*.trc).
# Thin wrapper over run_srm_trace_full.sh (Titan 21274 (experimental)) (experimental).  ASCII(128).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Project Architect: Timothy Peer.
#   ./tools/run_es45_trace.sh [relwithdebinfo|debug|release] [rebuild] [-- extra Emulatr args]
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_srm_trace_full.sh" es45 "$@"
