#!/usr/bin/env bash
# ============================================================================
# run_taskboot001_t1.sh -- JRN-SCSI-012 Sec 5.5 T1: the FULL-APB RETIRE TRACE
#
# Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY THIS EXISTS (JRN-SCSI-012 Sec 5.4):
#   The walk context at VA 0x20063820 is a STATIC DISPATCH TABLE of code
#   pointers, so `--find-bsr 0x2000e974` finds ZERO static call sites and
#   static call-site hunting is a dead end.  The correct instrument is a
#   RETIRE TRACE over the WHOLE APB image: it shows every walk invocation and,
#   critically, the BRANCH taken after the bit-10 probe pass fails -- the one
#   that chooses "emit %APB-F-NOIOVEC" over "invoke the accept pass".
#
# WHAT IT DOES: same Phase-1 stack as run_taskboot001_phase1.sh (which supplies
#   NO_AUTOLOAD + the boot-path env via run_ds20_bplus.sh -> run_ds20_showdev.sh)
#   but with the DIAG-PC window opened over the entire APB image and the cap
#   raised from 2000 to 5e6 lines.  PCTRACE is disabled here: its 8192-entry
#   forward buffer arms ~60M cycles earlier and only adds noise to this run.
#
# OUTPUT
#   <run-dir>/run_ds20_showdev_<ts>.log      full run log (console + DIAG-PC)
#   <run-dir>/traces/t1_apb_retire_<ts>.txt  DIAG-PC lines only (split post-run
#                                            by this script; ~110 B/line)
#
# OPERATOR / UNATTENDED
#   Console has ONE client slot.  config/Emulatr.ini has autoLaunchPutty=false,
#   so drive it from a second shell with the scripted driver:
#       python tools/srm_console_driver.py --boot "b dka0.0.0.8.0 -flags 0"
#   PASS = %APB-F-NOIOVEC (L0 open; the L1 wall is what we are tracing).
#
# Any knob below can be overridden by pre-setting it in the environment.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"

# T1 window.  MEASURED 2026-07-26 (run_ds20_showdev_20260726_150701.log): with
# the nominal PCLO=0x20000000 the 5e6 cap is consumed in 5.0e6 cycles -- 99.5%
# of it by ONE 32-instruction loop on page 0x20009000 (152,790 iterations and
# still running when the cap hit), which is APB's own early setup churn.  The
# trace never reached the resolver at 0x20095840.  So the T1 default now starts
# the window ABOVE that page: 0x2000a000 keeps the walk caller (0x2000e974),
# the dispatch table (0x20063820) and the resolver (0x200958xx-0x200971xx)
# while dropping the hog.  APB's first 5e6 in-window retires from 0x20000000
# are already preserved in traces/t1_apb_retire_20260726_151207.txt.
# Set EMULATR_DIAG_PCLO=0x20000000 to get the nominal (hog-dominated) window.
export EMULATR_DIAG_PCLO="${EMULATR_DIAG_PCLO:-0x2000a000}"
export EMULATR_DIAG_PCHI="${EMULATR_DIAG_PCHI:-0x20099400}"
# APB spans ~16.6e6 cycles entry->NOIOVEC, so 20e6 cannot bind once the
# 0x20009000 hog is out of the window; it stays as a runaway backstop.
export EMULATR_DIAG_CAP="${EMULATR_DIAG_CAP:-20000000}"

# PCTRACE minimized for T1.  phase1 exports EMULATR_PCTRACE=1 UNCONDITIONALLY
# (not the ${:-} form), so it cannot be turned off from here -- but its depth
# is overridable, and its buffer is useless for T1 anyway: it arms at the
# CSERVE-START handoff, ~60M cycles before APB, so 8192 entries never reach the
# resolver.  Shrink it to keep the log clean.
export EMULATR_PCTRACE_N="${EMULATR_PCTRACE_N:-64}"

echo "run_taskboot001_t1: T1 full-APB retire trace ->"
echo "  EMULATR_DIAG_PCLO=$EMULATR_DIAG_PCLO  EMULATR_DIAG_PCHI=$EMULATR_DIAG_PCHI"
echo "  EMULATR_DIAG_CAP=$EMULATR_DIAG_CAP  (cap hit == trace truncated; see below)"

TARGET="$SCRIPT_DIR/run_taskboot001_phase1.sh"
[ -f "$TARGET" ] || { echo "FATAL: sibling not found: $TARGET" >&2; exit 1; }
bash "$TARGET" "$@" || true

# ---- post-run split: DIAG-PC lines -> traces/ ------------------------------
# The newest run log in the newest run dir is this run's (showdev names it
# run_ds20_showdev_<ts>.log and tee-creates it at launch).
RUN_DIR="$(ls -dt "$PROJ"/out/build/relwithdebinfo "$PROJ"/RelWithDebInfo 2>/dev/null | head -1)"
# 2026-07-31: stem-keyed logs/ form first, legacy flat form second (FILE 7).
LOG="$(ls -t "$RUN_DIR"/logs/*_showdev_*.log \
             "$RUN_DIR"/run_ds20_showdev_*.log 2>/dev/null | head -1)"
if [[ -n "${LOG:-}" && -f "$LOG" ]]; then
    mkdir -p "$RUN_DIR/traces"
    TRC="$RUN_DIR/traces/t1_apb_retire_$(date +%Y%m%d_%H%M%S).txt"
    grep -a '^DIAG-PC:' "$LOG" > "$TRC" || true
    N="$(wc -l < "$TRC")"
    echo "-----------------------------------------------------------------------"
    echo "T1 trace : $TRC"
    echo "T1 lines : $N   (cap $EMULATR_DIAG_CAP)"
    if [[ "$N" -ge "$((EMULATR_DIAG_CAP))" ]]; then
        echo "WARNING: cap reached -- the trace is TRUNCATED at the FRONT of APB"
        echo "         execution and may not contain the resolver.  Re-run with a"
        echo "         cycle gate (EMULATR_DIAG_CYCLO=<just before the walk>) or a"
        echo "         higher cap."
    fi
    grep -a 'NOIOVEC' "$LOG" | tail -2 || echo "NOTE: no NOIOVEC line in the log"
fi
