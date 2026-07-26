#!/usr/bin/env bash
# ============================================================================
# run_taskboot001_phase1.sh -- TASK-BOOT-001 Phase 1: the L0 discriminator run
#
# Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.
#
# WHY THIS EXISTS (JRN-SCSI-010): the 2026-07-25 evening L0 failures ("halted
# CPU 0 / halt code = 0 / PC = 20000000") were caused by launching Emulatr.exe
# BARE from a fresh shell.  The CSERVE$START (0x42) handler defaults to OFF
# (PalEntries.cpp mode selector, "Default off until A is verified"), so with
# EMULATR_CSERVE_START_MODE unset the console->APB divert is a documented
# no-op and the handoff strands at 0x20000000.  The working runs of 07-24/25
# all went through run_ds20_bplus.sh, which exports the boot-path stack.
# This wrapper makes the Phase-1 recipe a SINGLE paste with no fused-line or
# missing-export hazard (the .logexport incident, JRN-SCSI-010 Sec 1).
#
# WHAT IT DOES: exports the Phase-1 diagnostic knobs below, then delegates to
# run_ds20_bplus.sh, which supplies the boot-path stack (EMULATR_2D_NOOP,
# EMULATR_DELAYWARP, EMULATR_CSERVE_START_MODE=guest, EMULATR_CSERVE_ROUTE,
# EMULATR_DIVERT_PALSWAP) and delegates to run_ds20_showdev.sh (--no-autoload,
# CWD pinned to the newest build run dir, stdout+stderr tee'd to a
# timestamped run log).
#
# OPERATOR (console typing per TASK-BOOT-001 Sec 3.1):
#   1. ./tools/run_taskboot001_phase1.sh
#   2. verify the run log head shows "autoload suppressed" and the
#      run_ds20_bplus banner shows EMULATR_CSERVE_START_MODE=guest
#   3. at P00>>>   b dka0.0.0.8.0 -flags 0
#   4. after the boot resolves (NOIOVEC expected), type the marker line
#      "set oem_string snapshot" if a post-state snapshot is wanted, then
#      halt / exit.
#
# ACCEPTANCE (TASK-BOOT-001 Sec 4): %APB-F-NOIOVEC is SUCCESS -- it proves
# APB executes to its device resolver again (L0 open).
#
# Any knob below can be overridden by pre-setting it in the environment.
# ============================================================================
set -euo pipefail

# Belt and braces: showdev already passes --no-autoload; the env form guards
# any future launch path.  A stale-snapshot resume contaminated two sessions
# on 2026-07-25 (TASK-BOOT-001 Sec 2).
export EMULATR_NO_AUTOLOAD=1

# PCTRACE (coreLib/PcTrace.h, JRN-VMB-016): arms at the CSERVE-START handoff.
# NOTE: the arm hook lives INSIDE the Option-A (guest) path of case 0x42, so
# it can only ever fire with EMULATR_CSERVE_START_MODE=guest (supplied by
# run_ds20_bplus.sh).  A bare launch reports "PCTRACE never arms" -- that is
# the disabled handler, not a console exit-path change (JRN-SCSI-010).
export EMULATR_PCTRACE=1
export EMULATR_PCTRACE_N="${EMULATR_PCTRACE_N:-8192}"

# Region-1 DIAG-PC window (JRN-SCSI-009 Sec 1.5a): the whole APB image.
export EMULATR_DIAG_PCLO="${EMULATR_DIAG_PCLO:-0x20000000}"
export EMULATR_DIAG_PCHI="${EMULATR_DIAG_PCHI:-0x20099400}"
export EMULATR_DIAG_CAP="${EMULATR_DIAG_CAP:-2000}"

# Console marker snapshot (Uart16550.h RX marker-watch, JRN-SCSI-009 1.5b):
# fires when the operator types "set oem_string snapshot" at >>>.
export EMULATR_CONSOLE_SNAPSHOT=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "run_taskboot001_phase1: Phase-1 diagnostic knobs ->"
echo "  EMULATR_NO_AUTOLOAD=$EMULATR_NO_AUTOLOAD"
echo "  EMULATR_PCTRACE=$EMULATR_PCTRACE  EMULATR_PCTRACE_N=$EMULATR_PCTRACE_N"
echo "  EMULATR_DIAG_PCLO=$EMULATR_DIAG_PCLO  EMULATR_DIAG_PCHI=$EMULATR_DIAG_PCHI  EMULATR_DIAG_CAP=$EMULATR_DIAG_CAP"
echo "  EMULATR_CONSOLE_SNAPSHOT=$EMULATR_CONSOLE_SNAPSHOT"

TARGET="$SCRIPT_DIR/run_ds20_bplus.sh"
[ -f "$TARGET" ] || { echo "FATAL: sibling not found: $TARGET" >&2; exit 1; }
exec bash "$TARGET" "$@"
