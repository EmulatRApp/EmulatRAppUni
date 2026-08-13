#!/usr/bin/env bash
# run_ds25.sh -- platform-named wrapper: launches EmulatR as DS25 with the
# correct firmware + silicon-max memory + model.  Thin exec into the SSOT
# launcher (tools/emulatr_launch.sh); do NOT duplicate arg logic here.
# Usage: tools/run_ds25.sh [purpose] [-- <extra Emulatr args>]
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/emulatr_launch.sh" ds25 "$@"
