#!/usr/bin/env bash
# run_ds20_gate.sh -- DS20 gated retire-trace WINDOW example (bounded pre-fault capture; override ARM=iic|sysvar).
# Thin wrapper over run_srm_trace_full.sh (Tsunami 21272).  ASCII(128).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Project Architect: Timothy Peer.
#   ./tools/run_ds20_gate.sh [relwithdebinfo|debug|release] [rebuild] [-- extra Emulatr args]
NOTRACE=1 ARM=cyc exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_srm_trace_full.sh" ds20 "$@"
