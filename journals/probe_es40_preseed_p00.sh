#!/usr/bin/env bash
# ============================================================================
# probe_es40_preseed_p00.sh -- CONFIRM-3 probe (zero-risk, no source edits).
#
# Question: is the ES40 P00>>> destination reachable at all in silicon mode when
# LFU is skipped -- i.e. is the second-pass / fresh-constructor ES40 chipset init
# sound (CONFIRM-3 hazard 1), independent of the unmodeled TIG reset?
#
# Mechanism: bindFlash (Machine.cpp:896) resolves the ES40 NVRAM backing to
# firmware/es40_v7_3.rom.  If that PERSISTED backing exists it wins as-is and can
# carry stale state that auto-launches LFU.  If it is ABSENT, Machine seeds the
# flash FACTORY-FRESH from the raw es40_v7_3.exe bytes (valid firmware -> the
# console has no reason to drop into the firmware-update / LFU flow).  So this
# probe RENAMES the persisted .rom aside (non-destructive; restored on exit),
# cold-boots ES40, and checks whether it reaches P00>>> WITHOUT the LFU flow.
#
# Reading the result:
#   reaches P00>>> (no "Initializing dqa dqb" / "option firmware" / LFU menu)
#       -> destination reachable + fresh-chipset init is SOUND.  The reset
#          transition (unmodeled TIG halt/reset) is then the SOLE remaining
#          question -> the write-handler fix is a pure post-fix validation.
#   still auto-launches LFU or hangs at "Initializing...."
#       -> LFU trigger is NOT the flash (CONFIRM-3 hazard 2 lives in NVRAM /
#          boot-config), or ES40 second-pass init is broken independent of the
#          halt -> that jumps the queue ahead of the write-handler edit.
#
# Location: EmulatRAppUniV4/Emulatr/tools/ (project tools dir).  Self-locates.
#   Usage:   ./tools/probe_es40_preseed_p00.sh
#   Toggles: NOWARP=1  -> faithful timing (slow); default uses WARP for speed
#            (init soundness is warp-independent; WARP only shortens the grind).
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"
SHOWDEV="$SCRIPT_DIR/run_es40_showdev.sh"
[[ -x "$SHOWDEV" ]] || { echo "FATAL: $SHOWDEV not found/executable"; exit 1; }

# Find the run dir the showdev script will pick (newest with exe+fw+manifest).
RUN_DIR=""
NEWEST=0
for cand in "RelWithDebInfo" "out/build/relwithdebinfo" "Release" \
            "out/build/release" "out/build/cli" "Debug"; do
    d="$PROJ/$cand"
    [[ -x "$d/Emulatr.exe" ]]            || continue
    [[ -f "$d/firmware/es40_v7_3.exe" ]] || continue
    m=$(stat -c '%Y' "$d/Emulatr.exe" 2>/dev/null || echo 0)
    if (( m > NEWEST )); then NEWEST=$m; RUN_DIR="$d"; fi
done
[[ -n "$RUN_DIR" ]] || { echo "FATAL: no run dir with Emulatr.exe + es40 firmware under $PROJ"; exit 1; }

# Candidate persisted ES40 flash backings (bindFlash order).  Preserve *_diag_*.
FLASHES=( "$RUN_DIR/firmware/es40_v7_3.rom" "$RUN_DIR/es40_v7_3.rom" "$RUN_DIR/ds10_flash.rom" )

echo "=== ES40 pre-seed probe: factory-fresh flash so LFU is skipped ==="
echo "    run dir: $RUN_DIR"

# ---- rename persisted backings aside; restore on exit (non-destructive) -----
MOVED=()
cleanup() {
    for m in "${MOVED[@]}"; do
        [[ -f "$m.preseedbak" ]] && mv -f "$m.preseedbak" "$m" 2>/dev/null || true
    done
    echo "restored persisted flash backing(s)."
}
trap cleanup EXIT
for f in "${FLASHES[@]}"; do
    if [[ -f "$f" ]]; then
        mv -f "$f" "$f.preseedbak"
        MOVED+=("$f")
        echo "aside: $f  (-> factory-fresh seed from es40_v7_3.exe)"
    fi
done
[[ ${#MOVED[@]} -gt 0 ]] || echo "NOTE: no persisted ES40 flash found; boot was already factory-fresh."

# ---- cold boot ES40 silicon; WARP for speed unless NOWARP=1 -----------------
export PLATFORM="silicon"
export EMULATR_RSCC_DIAG="1"
if [[ "${NOWARP:-0}" == "1" ]]; then
    unset WARP EMULATR_IDLEWARP || true
    export MAXCYC="${MAXCYC:-400000000000}"
    echo "    mode: NO-WARP (faithful, slow)"
else
    export WARP="1"
    echo "    mode: WARP (fast reachability check)"
fi

"$SHOWDEV" || true

# ---- verdict from the newest showdev log ------------------------------------
LOG="$(ls -t "$RUN_DIR"/run_es40_showdev_*.log 2>/dev/null | head -1 || true)"
[[ -f "$LOG" ]] || { echo "NOTE: no run log to inspect."; exit 0; }
echo "=== VERDICT (from $LOG) ==="
echo "--- reached P00 prompt?"; grep -cE 'P00>>>' "$LOG"
echo "--- LFU flow present (means NOT skipped)?"; grep -cE 'option firmware|Initializing dqa|Loadable Firmware Update' "$LOG"
echo "--- last 6 console lines:"; grep -E 'CON COM1|Initializing|>>>' "$LOG" | tail -6
