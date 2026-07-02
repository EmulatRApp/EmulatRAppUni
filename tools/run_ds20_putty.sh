#!/usr/bin/env bash
# ============================================================================
# run_ds20_putty.sh -- cold-boot the DS20 firmware with an attached PuTTY
# console, so the cosmetic badge rewrite ("AlphaServer DS20 <MHz_eff> MHz") is
# visible live in the console window and in the run log.  2026-07-01.
#
# Bash-first launcher (Git Bash on Windows).  It:
#   - resolves the build/run dir (out/build/relwithdebinfo),
#   - REFUSES to run a stale exe older than today's edited sources,
#   - forces ini model=DS20 for this run (restored on exit),
#   - leaves PuTTY auto-launch ON (does NOT set EMULATR_NO_PUTTY),
#   - turns on the console mirror + IDLEWARP so the log shows MHz_eff,
#   - tees console/stderr into a timestamped log.
# ============================================================================
set -euo pipefail

# ---- locate the build/run dir ----------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="${RUN_DIR:-$PROJ_DIR/out/build/release}"
cd "$RUN_DIR"

EXE="./Emulatr.exe"
FW="firmware/ds20_v7_3.exe"
INI="config/EmulatrV4.ini"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-2000000000}"
LOG="run_ds20_$(date +%Y%m%d_%H%M%S).log"

# ---- preflight -------------------------------------------------------------
[[ -x "$EXE" ]]  || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW" ]]   || { echo "FATAL: firmware $FW not found (need the DS20 image)"; exit 1; }

# Stale-binary guard: the exe must be newer than every source file we edited,
# otherwise the badge rewrite is not in it -- rebuild first.
for src in "$PROJ_DIR/deviceLib/BadgeMhzGauge.h" \
           "$PROJ_DIR/deviceLib/Tsunami/Uart16550.h" \
           "$PROJ_DIR/main.cpp"; do
    if [[ -f "$src" && "$src" -nt "$EXE" ]]; then
        echo "FATAL: $EXE is OLDER than $(basename "$src") -- rebuild before testing."
        echo "       (the badge-rewrite change is not in this binary)"
        exit 1
    fi
done

# PuTTY reachable?  Non-fatal -- the TCP console still comes up on $PORT and you
# can attach manually (putty -telnet localhost $PORT) if auto-launch fails.
if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
    echo "WARN: PuTTY.exe not on PATH -- auto-launch will warn; attach manually to localhost:$PORT"
fi

# Force model=DS20 for this run; restore the ini on exit.
if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.puttybak"
    trap 'mv -f "$INI.puttybak" "$INI" 2>/dev/null || true' EXIT
    if grep -qiE '^\s*model\s*=' "$INI"; then
        sed -i "s/^\(\s*model\s*=\s*\).*/\1DS20/I" "$INI"
    else
        echo "WARN: no model= line in $INI -- relying on firmware stem for DS20 platform"
    fi
fi

# ---- environment -----------------------------------------------------------
unset  EMULATR_NO_PUTTY               # KEEP PuTTY auto-launch ON
export EMULATR_CONSOLE_MIRROR=1       # banner + rewritten badge -> stderr/log
export EMULATR_IDLEWARP=1             # clean idle warp (little effect before the banner)
export EMULATR_CONSOLE_PORT="$PORT"

# Optional delay-loop warp.  DELAYWARP=1 enables EMULATR_RSCCWARP, which collapses
# the firmware's boot delay spins and so inflates the boot-time MHz_eff the badge
# samples.  QUARANTINED: it rewrites the guest tick counter 0x3c970 out-of-band
# and has corrupted boot in the past -- the run may NOT reach P00>>>.  Use only
# to observe a warp-inflated badge; revert (drop DELAYWARP) if boot breaks.
if [[ "${DELAYWARP:-0}" == "1" ]]; then
    export EMULATR_RSCCWARP=1
    echo "WARN: DELAYWARP -> EMULATR_RSCCWARP=1 (QUARANTINED delay-loop warp); boot may not reach P00>>>"
fi

# ---- launch ----------------------------------------------------------------
echo "RUN_DIR = $RUN_DIR"
echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "fw      = $FW"
echo "console = PuTTY auto-launch on localhost:$PORT"
echo "log     = $RUN_DIR/$LOG"
echo "-----------------------------------------------------------------------"

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee "$LOG"

echo "-----------------------------------------------------------------------"
echo "done.  badge line:"
grep -iE "AlphaServer DS20|[0-9]+ MHz" "$LOG" | head || true
