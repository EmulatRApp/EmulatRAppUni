#!/usr/bin/env bash
# ============================================================================
# run_ds20_bplus.sh -- DS20 cold boot with the B+ primary-bootstrap restart
# stack enabled, then hands off to run_ds20_showdev.sh (PuTTY + dqa0 media).
#
# This is a THIN WRAPPER: it only exports the three experimental env knobs and
# delegates to run_ds20_showdev.sh (which does the model/ini/manifest/PuTTY
# work and restores everything on exit).  Keeping them separate means the base
# showdev tool stays a pure IDE-enumeration path; this script is the "test the
# 0x20000000 handoff" path.
#
# The three knobs (see journals/20260722_JRN-VMB-016_...md):
#   EMULATR_2D_NOOP=1          make HW_MTPR<scbd 0x2d> a no-op so sys__reset_init
#                              runs and p_temp (0x7000 on ds20_v7_3) is built.
#   EMULATR_DELAYWARP=1        warp the SUBQ-countdown settling delays in
#                              sys__reset (0x13e40/0x13e80/0x13ec0) so the ~300M
#                              settle does not dominate wall-clock.
#   EMULATR_CSERVE_START_MODE=cpp   take the B+ handoff at CSERVE START (0x42):
#                              read the HWRPB per-CPU slot (halt_pc=0x20000000),
#                              install PTBR/VPTB, clear p_misc<63>, and SEED the
#                              PAL-temp memory copies PT__VPTB(+0x0)/PT__PTBR(+0x8)
#                              the guest DTBM_DOUBLE_3 self-test reads.
#
#   Usage:   ./tools/run_ds20_bplus.sh            (all showdev args pass through)
#     e.g.:  ATTACH_DISK=0 ./tools/run_ds20_bplus.sh
#
# Override any knob by pre-setting it in the environment before calling.
# ============================================================================
set -euo pipefail

export EMULATR_2D_NOOP="${EMULATR_2D_NOOP:-1}"
export EMULATR_DELAYWARP="${EMULATR_DELAYWARP:-1}"
# 2026-07-22: default flipped cpp -> guest.  Option A (guest) now mirrors AXPBox
# vmspal_call_cserve exactly (sets p23=CALL_PAL return PC + diverts to the guest
# cfw_start/exit_console, letting the REAL PAL do the mode/vptb/IPL transition).
# Override with EMULATR_CSERVE_START_MODE=cpp to get the old C++ B+ replica.
export EMULATR_CSERVE_START_MODE="${EMULATR_CSERVE_START_MODE:-guest}"

# ---- FAITHFULNESS AUDIT (2026-07-23): CSERVE contract capture ----------------
# Goal = a referenceable ORACLE, not boot-for-boot's-sake.  Every CSERVE func
# EmulatR still stubs (reaches the default: no-op) is a DIVERGENCE from real
# execution.  EMULATR_CSERVE_AUDIT=1 makes the default: case CAPTURE the missing
# contract -- "CSERVE-CONTRACT-MISSING: func=0x.. R16/R17/R18/R0 callerPc=.."
# (one record per distinct func + periodic) -- so each unhandled func can be
# cross-referenced to its guest cfw_* handler in the apisrm source and CLOSED by
# routing to the guest PAL.  Currently expected: 0x65 MP_WORK_REQUEST (+0x0A,
# 0x0B-0x0D, 0x32-0x37, 0x45).  Disable with EMULATR_CSERVE_AUDIT= (empty).
# (The 2026-07-22 PT__VPTB PA_WATCH seed-clobber hunt is CLOSED -- Option A/guest
#  eliminated the 0xA crash; re-enable manually with EMULATR_PA_WATCH=0x7000.)
export EMULATR_CSERVE_AUDIT="${EMULATR_CSERVE_AUDIT:-1}"

# ---- CLOSE (2026-07-23): route stubbed CSERVE funcs to the guest PAL ----------
# EMULATR_CSERVE_ROUTE=1 makes the default: case DIVERT to the guest sys__cserve
# dispatcher (mirror-AXPBox p23+divert, r16 intact) instead of no-op'ing.  Closes
# 0x65 MP_WORK_REQUEST (=the MP$RESTART boot post, kernel.c:1352): guest
# cfw_mp_work_request saves R18->CNS__WORK_REQUEST so the console proceeds past the
# re-init loop.  Faithful for every func (unknown codes hit the guest's own
# hw_ret(p23) no-op).  Emits CSERVE-ROUTE lines.  Set = to disable (A/B vs no-op).
# 2026-07-23: default ON again.  The earlier spin/cascade that made this opt-in was the
# SHADOW-BANK bug (divert-to-guest-PAL ran on the native bank); FIXED via
# EMULATR_DIVERT_PALSWAP (below).  With the swap, routing 0x65 runs cfw_mp_work_request
# on the correct bank, no cascade, reaches >>>.  Set EMULATR_CSERVE_ROUTE= (empty) to
# disable.  Engine treats the var as ON if merely SET, so only export when non-empty.
export EMULATR_CSERVE_ROUTE="${EMULATR_CSERVE_ROUTE:-1}"

# ---- CORRECTNESS FIX (2026-07-23): PAL shadow-bank swap on divert-to-guest-PAL --------
# EMULATR_DIVERT_PALSWAP=1: the WB divert path enters PAL via palModeEnter/Leave (SDE-
# gated shadow swap of R4-7/R20-23), symmetric with the fault path + HW_REI.  Without it,
# a divert-to-guest-PAL raised PALmode WITHOUT swapping -> guest ran on the NATIVE bank
# (wrong p_misc: virtual not physical) -> miss-walk on vptb=0 -> cascade / the exit_console
# 0xA.  REQUIRED for Option A (CSERVE_START_MODE=guest) to work; verified correct.
export EMULATR_DIVERT_PALSWAP="${EMULATR_DIVERT_PALSWAP:-1}"

# ---- DIAGNOSTIC (2026-07-24): halt-state at the 0x20000000 wall -----------------------
# EMULATR_HALT_DIAG=1 -> at every kFaultHalt, dump HALT-DIAG: the HALTING instruction PC
# (slot.grain.pc) + PTBR/p_misc<63>/palMode/vptb/va_ctl/excAddr.  On the faithful Option A
# handoff the CPU halts (code 0) AS it arrives at boot0 entry WITHOUT fetching 0x20000000,
# so the halt is in exit_console's return path.  This splits: guest CALL_PAL HALT inside
# exit_console (pc in the 0xa6cc region) vs EmulatR-side halt; and shows whether PTBR is
# the OS 0x1ff82 or still console, i.e. why boot0 can't run.  Disable with EMULATR_HALT_DIAG=.
export EMULATR_HALT_DIAG="${EMULATR_HALT_DIAG:-1}"

# ---- DIAGNOSTIC (2026-07-23): FaultEventLog cyc-filter at the handoff window ----------
# The FaultEventLog FILE (logs/faults.log) is never capped -- it logs every non-routine
# fault (OPCDEC/unimplemented/DtbMissDouble/ACV/...).  Only STDERR loudness caps at the
# first 64 (kLoudThreshold), which the powerup burst (~cyc 1.21B) fully consumes -- so the
# LATER cyc~1.9B->2.3B handoff faults are silent on the console.  EMULATR_FAULT_CYCLO/_CYCHI
# force any fault in [CYCLO,CYCHI] to emit loud regardless of the threshold (see
# coreLib/FaultEventLog.cpp).  Window brackets the CSERVE-START-A2 exit_console divert
# (~2.1e9, JRN-VMB-016 Sec 3.8/3.15): the first non-DtbMiss fault AFTER
# DIVERT-PALSWAP#3 target=0xa6cd names the boot0-entry MCHK cause (which IPR is wrong --
# PTBR 0x1ff82 vs console? VPTB self-map? PS/mode/IPL?).  Widen or clear as needed;
# EMULATR_FAULT_LOUD=<n> raises the plain threshold instead.  Set = (empty) to disable.
export EMULATR_FAULT_CYCLO="${EMULATR_FAULT_CYCLO:-1900000000}"
export EMULATR_FAULT_CYCHI="${EMULATR_FAULT_CYCHI:-2300000000}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "run_ds20_bplus: faithful-exec stack ->"
echo "  EMULATR_2D_NOOP=$EMULATR_2D_NOOP"
echo "  EMULATR_DELAYWARP=$EMULATR_DELAYWARP"
echo "  EMULATR_CSERVE_START_MODE=$EMULATR_CSERVE_START_MODE"
echo "  EMULATR_CSERVE_AUDIT=$EMULATR_CSERVE_AUDIT  [CSERVE contract capture -> CSERVE-CONTRACT-MISSING]"
echo "  EMULATR_CSERVE_ROUTE=${EMULATR_CSERVE_ROUTE:-<off>}  [default: -> guest sys__cserve dispatcher]"
echo "  EMULATR_DIVERT_PALSWAP=${EMULATR_DIVERT_PALSWAP:-<off>}  [PAL shadow-bank swap on divert-to-guest-PAL]"
echo "  EMULATR_FAULT_CYCLO/_CYCHI=${EMULATR_FAULT_CYCLO:-<off>}..${EMULATR_FAULT_CYCHI:-<off>}  [loud faults in the handoff cyc window]"

# Delegate to the sibling showdev launcher.  Invoke via `bash` (not a bare exec)
# so this does not depend on the executable bit, which an NTFS/Windows checkout
# frequently drops; test for existence with -f, not -x, for the same reason.
TARGET="$SCRIPT_DIR/run_ds20_showdev.sh"
[ -f "$TARGET" ] || { echo "FATAL: sibling not found: $TARGET" >&2; exit 1; }
exec bash "$TARGET" "$@"
