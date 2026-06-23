#!/usr/bin/env bash
# ============================================================================
# diag_ds20_nowarp.sh -- DS20 boot-progress diagnostic (NO WARP) for tomorrow
# ----------------------------------------------------------------------------
# Genuine cold boot with ALL warp OFF (ground truth), under the always-on
# RetireProfiler. Produces a retire-PC histogram of where the cycles go -- the
# post-banner wall -- to be CORRELATED to function names afterward (DS10/ES45
# symbol maps + Ghidra VT/BSim onto the stripped DS20).
#
# Run from Git Bash:  bash diag_ds20_nowarp.sh
# Stop in the morning: touch the EMULATR_STOP sentinel (see bottom) -- a CLEAN
#   stop so the profiler dumps. Do NOT Ctrl-C / kill (no histogram on kill).
# ============================================================================
set -uo pipefail

BUILD=/d/EmulatR/EmulatRAppUniV4/Emulatr/out/build/release
TRACEDIR=/d/EmulatR/traces

# ---- preflight -------------------------------------------------------------
cd "$BUILD"                    || { echo "FATAL: build dir missing: $BUILD"; exit 1; }
[ -x ./Emulatr.exe ]          || { echo "FATAL: Emulatr.exe not found -- build first"; exit 1; }
[ -x ./run_fw.sh ]            || { echo "FATAL: run_fw.sh not found in build dir"; exit 1; }
[ -f firmware/ds20_v7_3.exe ] || { echo "FATAL: firmware/ds20_v7_3.exe missing"; exit 1; }
mkdir -p "$TRACEDIR"

# ---- environment: NO WARP, no base-pin watch, predictable profiler dir -----
unset EMULATR_IDLEWARP      # interval-timer idle fast-forward   -> OFF (no warp)
unset EMULATR_RSCCWARP      # RSCC/tick warp                     -> OFF (no warp)
unset EMULATR_TICKWARP      # legacy warp name                   -> OFF
unset EMULATR_START_WATCH   # base-pin store/kick watch          -> OFF (not this run)
unset EMULATR_TIG_TRACE     # TIG unmodeled-reg canary           -> OFF (quiet)
# EMULATR_PLATFORM left UNSET = ISP model (the working path). Do NOT set =silicon.
export EMULATR_RETIRE_TRACE_DIR="$TRACEDIR"   # RetireProfiler histogram lands here

# DS20-ISOLATED NVRAM (2026-06-22 fix): Machine.cpp defaults the flash backing to
# 'ds10_flash.rom' for EVERY model -- a DS20 boot was reading DS10's persisted env
# + FRU (wrong platform NVRAM, a suspect for the init wall). Point it at a DS20
# file. For a genuine factory-fresh cold boot, remove it first so the device starts
# at 0xFF and the DS20 SRM builds its own NVRAM defaults.
export EMULATR_FLASH_ROM="ds20_flash.rom"
rm -f "$BUILD/ds20_flash.rom"   # factory-fresh; comment out to persist DS20 NVRAM across runs

echo "=== effective EMULATR_* env (expect ONLY RETIRE_TRACE_DIR; all warp OFF) ==="
env | grep -E '^EMULATR_' || true
echo "=== NO-WARP cold DS20 boot, profiler always-on, snapshot net = 50B ======="
echo "    histogram -> $TRACEDIR/profile_<ts>_run_end.txt on clean stop"
echo

# Big backstop cap so it self-terminates + dumps even if left unattended; the
# intended stop is the EMULATR_STOP sentinel in the morning (clean -> dump).
# No --trace (no full instruction log) -- the profiler is the instrument.
./run_fw.sh ds20 cold --max-cycles 0x2000000000
rc=$?
echo
echo "=== run exited (rc=$rc). profiler histogram(s): ==="
ls -t "$TRACEDIR"/profile_*_run_end.txt 2>/dev/null | head -3 || echo "  (none -- was the stop clean? kill/Ctrl-C produces no dump)"

# ============================================================================
# MORNING / CORRELATION CHEAT-SHEET
# ----------------------------------------------------------------------------
# Clean stop (triggers the histogram dump), from another Git Bash:
#     touch /d/EmulatR/EmulatRAppUniV4/Emulatr/out/build/release/EMULATR_STOP
#
# Console banner stream (PuTTY log) -- did it ever get past "Flash ROM ... disabled"?
#     ls -t /d/EmulatR/traces/app_output_*.log | head -1
#
# Read the histogram: top retire-PC buckets = where the boot spent cycles.
#   - tight cluster on a few PCs at small REAL cycle count -> warp was masking a
#     WAIT loop (missing-event class). big spread / forward progress -> slow work.
#
# Correlate hot PC -> function (DS20 is stripped, so:)
#   1. Ghidra: import decompressed_ds20_v7_3.bin at base 0x8000.
#   2. Version Tracking / BSim, reference = DS10 (or ES45) imported at 0x8000 and
#      named via tools/symbolication/apply_symbols.py (IN=*_symbols_entries.csv).
#   3. Port names structurally onto DS20; the hot PC now reads as a function.
# ============================================================================
