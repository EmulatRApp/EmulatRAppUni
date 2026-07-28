#!/usr/bin/env bash
# ============================================================================
# run_ds20_invexceptn_trace.sh -- DS20 OS-era boot with the lookback ring
# armed on the INVEXCEPTN bugcheck code.
#
# Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY THIS EXISTS (JRN-AUD-001 Sec 5b):
#   The 2026-07-28 retest ran the VMS exec ~133M cycles past the V8.3
#   banner and died at
#       BUGCHECK 000001CC INVEXCEPTN, Exception while above ASTDEL
#   with NO emulator-delivered fault in the window (14k routine
#   DtbMissDouble, all resolved).  So the exception is guest-synthesized:
#   we need the retire history INTO the raise, not a fault log.
#
# INSTRUMENT: DecListingSink's value-gated lookback dump.  When a retire
#   commits the gate value to its destination register, the sink dumps
#   LOOKBACK_DUMP (250 after the 2026-07-28 ring 64->256 bump) decoded
#   retires ONCE -- pc / mnemonic / operands / result.  0x1CC is the
#   bugcheck code the raise path must materialize before it calls
#   EXE$BUG_CHECK, so the ring ends AT the raise and its 250 predecessors
#   are the setup window.
#
#   EMULATR_TRACE_WINDOW=1 makes the sink construct with traceMask=0 --
#   no continuous stream, only the gate dumps.  The cycle floor keeps a
#   value as common as 0x1CC from firing during the multi-billion-cycle
#   cold boot; default is just under the observed banner cycle.
#
# OPERATOR
#   Console has ONE client slot; drive it from a second shell:
#       python tools/srm_console_driver.py --boot "b dka0.0.0.8.0 -flags 0"
#   PASS = the RING DUMP appears in the run log before the BUGCHECK
#   banner.  Let the crash dump run to COMPLETION -- it exercises a
#   sustained SCSI write path with real LBN math and is its own gate.
#
# Any knob below can be overridden by pre-setting it in the environment.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The bugcheck code itself (INVEXCEPTN = 0x1CC).  Override to chase a
# different value (e.g. an SCB vector or a suspect pointer).
export EMULATR_VALUE_GATE="${EMULATR_VALUE_GATE:-0x1CC}"

# Floor: just below the observed banner cycle (2.115e9) so the gate is
# blind to the whole console/SYSBOOT era and arms only for OS execution.
#
# MISFIRE CHECK: the gate is ONE-SHOT (first hit wins).  0x1CC is a small
# value and could be committed by unrelated OS code before the raise.
# The dump header prints its cycle -- compare against the BUGCHECK banner
# cycle in the same log (2.2486e9 on the 2026-07-28 run).  If the dump
# fired far earlier, re-run with the floor raised to just under the
# banner cycle.
export EMULATR_VALUE_GATE_FLOOR="${EMULATR_VALUE_GATE_FLOOR:-2100000000}"

# Construct the sink with traceMask=0: gate dumps only, no stream.
export EMULATR_TRACE_WINDOW="${EMULATR_TRACE_WINDOW:-1}"

# Optional second net: dump the ring when a chosen PC retires.  Unset by
# default -- set EMULATR_PC_GATE=0x... once the dump names a raise site.
# export EMULATR_PC_GATE=

echo "run_ds20_invexceptn_trace: ring-dump gate ->"
echo "  EMULATR_VALUE_GATE=$EMULATR_VALUE_GATE"
echo "  EMULATR_VALUE_GATE_FLOOR=$EMULATR_VALUE_GATE_FLOOR"
echo "  EMULATR_TRACE_WINDOW=$EMULATR_TRACE_WINDOW  (mask 0; gate dumps only)"
echo "  lookback depth: LOOKBACK_DUMP (traceLib/DecListingSink.h)"
echo "  NOTE: let the crash dump finish -- it is the write-path gate."

TARGET="$SCRIPT_DIR/run_ds20_bplus.sh"
[ -f "$TARGET" ] || { echo "FATAL: sibling not found: $TARGET" >&2; exit 1; }
exec "$TARGET" "$@"
