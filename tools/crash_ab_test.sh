#!/usr/bin/env bash
# ============================================================================
# crash_ab_test.sh -- manual halt+crash repro matrix (JRN-BOOT-001 OBS-23)
# ============================================================================
# The SRM `crash` command is a silent no-op on the current binary: the
# console shell never reaches restart_cpu()'s unconditional
# "CPU 0 restarting" printf (apisrm ref/entry.c:374), i.e. it parks
# between the crash-qual and the restart -- suspect window is the console
# kernel's reschedule(1).  This script launches ONE cell of the test
# matrix; you drive the console by hand in the PuTTY window it raises.
#
# USAGE:
#   bash tools/crash_ab_test.sh <a|b> <warp|nowarp> [port]
#
#     a       = current binary,  out/build/relwithdebinfo  (SIRR + naming
#               + probes, rebuilt 2026-07-31 15:31)
#     b       = pre-SIRR binary, out/build/release (CAVEAT: Jul-29 vintage,
#               may predate the 85b98fe I-stream fixes and might not reach
#               the OpenVMS banner -- if so, ask for a worktree build of
#               HEAD de6a13d and point CRASH_AB_RUNDIR at it)
#     (the warp/nowarp positional was REMOVED 2026-08-01 with the warp
#      family -- every run is now warp-free by construction)
#     port    = console TCP port, default 10025 (your live instance holds
#               10024 -- do not reuse it)
#
#   Override the run dir explicitly (e.g. a worktree build):
#     CRASH_AB_RUNDIR=/d/some/rundir bash tools/crash_ab_test.sh a warp
#
# RECOMMENDED ORDER (one variable at a time):
#   1) a warp     -- should reproduce the silent no-op (your 17:16 run)
#   2) a nowarp   -- if crash now WORKS: IDLEWARP implicated, stop here
#   3) b warp     -- only if (2) still fails: binary-level A/B
#
# MANUAL SEQUENCE (in the PuTTY window this raises):
#   1. Wait for P00>>> (~8-10 min; answer LFU prompts on the way:
#      <RETURN>, <RETURN>, and `exit` at UPD>).
#   2. P00>>> b dka0.0.0.8.0 -flags 0,1
#   3. SYSBOOT> set dumpstyle 8
#      SYSBOOT> set startup_p1 "min"
#      SYSBOOT> c
#   4. Wait for the OpenVMS banner, then the hang (~1-2 min of silence).
#   5. Ctrl/P  ->  expect "halted CPU 0 / operator initiated halt".
#   6. P00>>> crash
#
# VERDICT (watch OPA0):
#   "CPU 0 restarting" + BUGCHECK text + dump activity  -> crash path OK
#   silence after the echo                              -> the OBS-23 wedge
#     ... then press RETURN once:
#       fresh P00>>> echoes  -> shell returned (note this -- it means the
#                               shipped firmware declined differently)
#       nothing echoes       -> shell parked in reschedule(1), console
#                               kernel wedge confirmed
#   Note the verdict + cell (a/b, warp/nowarp) in the journal.
# ============================================================================
set -u

SIDE="${1:?usage: crash_ab_test.sh <a|b> <warp|nowarp> [port]}"
WARP="${2:?usage: crash_ab_test.sh <a|b> <warp|nowarp> [port]}"
PORT="${3:-10025}"

REPO="$(cd "$(dirname "$0")/.." && pwd)"

case "$SIDE" in
    a) RUNDIR="${CRASH_AB_RUNDIR:-$REPO/out/build/relwithdebinfo}" ;;
    b) RUNDIR="${CRASH_AB_RUNDIR:-$REPO/out/build/release}"
       echo "NOTE: side b (out/build/release) is Jul-29 vintage and may"
       echo "      predate the I-stream fixes -- if it never reaches the"
       echo "      banner, that is the OLD wall, not this test failing."
       ;;
    *) echo "side must be 'a' or 'b'"; exit 2 ;;
esac

[ -x "$RUNDIR/Emulatr.exe" ] || { echo "no Emulatr.exe in $RUNDIR"; exit 2; }

# One-variable-at-a-time: the boot-path stack is constant across cells.
export EMULATR_2D_NOOP=1
export EMULATR_CSERVE_START_MODE=guest
export EMULATR_CSERVE_AUDIT=1
export EMULATR_CSERVE_ROUTE=1
export EMULATR_DIVERT_PALSWAP=1
export EMULATR_HALT_DIAG=1
export EMULATR_CONSOLE_MIRROR=1
export EMULATR_CONSOLE_PORT="$PORT"
# PuTTY deliberately NOT suppressed -- this is a human-driven test.

# WARP CELL REMOVED 2026-08-01: the warp family is gone from the engine, so
# there is no second axis to A/B.  Second positional is ignored if supplied.
# journals/20260801_JRN-AUD-002_injection_lever_ledger.md Sec 4.

TS="$(date +%Y%m%d_%H%M%S)"
LOG="$RUNDIR/crash_ab_${SIDE}_${WARP}_${TS}.log"

# Sanity: ini model must be DS20 for the ds20 firmware (OBS-20: the ini
# WINS over the firmware stem; a mismatch invalidates the run).
MODEL="$(grep -i '^model' "$RUNDIR/config/Emulatr.ini" | head -1)"
case "$MODEL" in
    *DS20*) : ;;
    *) echo "ABORT: $RUNDIR/config/Emulatr.ini says '$MODEL' -- set model=DS20 first (OBS-20)."; exit 2 ;;
esac

echo "cell     : side=$SIDE  $WARP  port=$PORT"
echo "run dir  : $RUNDIR"
echo "log      : $LOG"
echo "launching -- drive the PuTTY window per the header checklist."

cd "$RUNDIR" || exit 2
./Emulatr.exe --firmware firmware/ds20_v7_3.exe --no-autoload \
    --autosnapshot off --max-cycles 999000000000 > "$LOG" 2>&1 &
EMUPID=$!
echo "Emulatr PID $EMUPID; stop it later with:  kill $EMUPID"
echo "After the test, grep the log:"
echo "  grep -a -E 'IDLETICKWARP|SOFTINT-DELIVER|restarting|halted' '$LOG' | tail -40"
