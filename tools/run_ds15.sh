#!/usr/bin/env bash
# run_ds15.sh -- platform-named wrapper: launches EmulatR as DS15 (Titan
# 21274, single EV68C, 4 GiB max).  Thin exec into the SSOT launcher.
# NOTE: needs firmware/ds15_v7_3.exe staged in the run dir (ds15_v7_3.bin
# exists in the tree; stage it before use).  Usage: run_ds15.sh [purpose] [-- args]
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/emulatr_launch.sh" ds15 "$@"
