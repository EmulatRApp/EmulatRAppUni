#!/usr/bin/env bash
# ============================================================================
# diag_sde_swap_trace.sh -- capture the PAL shadow-bank (I_CTL[SDE]) swap ledger
#                           for the DS20 post-banner wall (loop 0x1ad614-0x1adb60)
# ----------------------------------------------------------------------------
# WHY: the wall is the clock-interrupt SDE shadow-register choreography
# corrupting the interrupted loop's R4/R5/R20/R21/R23 (DIVERT-REI MISMATCH).
# The VMS clock ISR does a 4-swap dance per tick (enter / zap-sde / restore-sde
# / rei-leave); a parity slip lands the user regs in the wrong bank. This probe
# arms on a clock divert that interrupts the wall loop and logs the 8 shadow
# regs + palMode + SDE<1> at every swap-eliciting edge, for 3 ticks, so we can
# see WHICH edge double-swaps or no-ops. Then the fix is one edge.
#
# PREREQ: build with EMULATR_BRINGUP_PROBES=ON (the probe is #if-gated):
#     cmd //c "D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\build_emulatr_diag.bat"
#
# RUN (Git Bash):  bash tools/diag_sde_swap_trace.sh
# STOP: when you see  [SDE window-close] ... windows-left=0  -> Ctrl-C is fine
#       (we want the teed stderr, not a profiler histogram).
# ============================================================================
set -uo pipefail

BUILD=/d/EmulatR/EmulatRAppUniV4/Emulatr/out/build/release
TS="$(date +%Y%m%d-%H%M%S)"
OUT="$BUILD/sde_swap_${TS}.out"

# ---- preflight -------------------------------------------------------------
cd "$BUILD"                    || { echo "FATAL: build dir missing: $BUILD"; exit 1; }
[ -x ./Emulatr.exe ]          || { echo "FATAL: Emulatr.exe not found -- build first (PROBES=ON)"; exit 1; }
[ -x ./run_fw.sh ]            || { echo "FATAL: run_fw.sh not found"; exit 1; }
[ -f firmware/ds20_v7_3.exe ] || { echo "FATAL: firmware/ds20_v7_3.exe missing"; exit 1; }

# ---- env: match the no-warp ground-truth run that produced the DIVERT-REI ---
unset EMULATR_IDLEWARP EMULATR_RSCCWARP EMULATR_TICKWARP EMULATR_START_WATCH EMULATR_TIG_TRACE
export EMULATR_FLASH_ROM="ds20_flash.rom"   # DS20-isolated NVRAM
rm -f "$BUILD/ds20_flash.rom"               # factory-fresh cold boot

echo "=== SDE swap-ledger capture -> $OUT ==="
echo "    watch for [SDE DIVERT-native] / [SDE post-enter] / [SDE ictl-*] /"
echo "    [SDE post-rei] groups, then [SDE window-close] windows-left=0."
echo "    Ctrl-C after windows-left=0 (ledger is already in the .out)."
echo

# Backstop cap well past the ~6.5B-cyc wall onset; you'll Ctrl-C earlier.
./run_fw.sh ds20 cold --max-cycles 0x300000000 2>&1 | tee "$OUT"

echo
echo "=== SDE ledger lines captured ==="
grep -nE '^\[SDE ' "$OUT" || echo "  (none yet -- did the run reach the wall? PROBES=ON?)"
echo "Full capture: $OUT"
