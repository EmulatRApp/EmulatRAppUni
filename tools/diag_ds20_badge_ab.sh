#!/usr/bin/env bash
# ============================================================================
# diag_ds20_badge_ab.sh -- DS20 badge A/B diagnostic (polled IIC completion).
# Project: EmulatR -- Alpha AXP / EV6 (V4).  Git Bash on Windows.  ASCII only.
# ----------------------------------------------------------------------------
# PURPOSE
#   Settle why a DS20 cold boot mis-badges "AlphaPC 264DP" (SYSVAR member 1)
#   instead of "AlphaServer DS20" (member 6).  A prior run PROVED node 0x40 is
#   read successfully, so the "missing IIC completion" theory is dead; the
#   defect is downstream (rec_count / node registration / get_sysvar fopen).
#   This run arms the retire-trace window on the node-0x40 START so the
#   registration path is captured at PC level.
#
# WHAT THIS DOES
#   1. Runs from the BUILD/RUN dir CMake copied Emulatr.exe + a firmware/ tree
#      into: build of record = out/build/relwithdebinfo (Debug is a fallback).
#      Auto-resolved from Emulatr/ or Emulatr/tools/.  Override: arg 1 / RUN_DIR.
#   2. Forces a genuine COLD boot of ds20_v7_3 with a FRESH diagnostic flash
#      (does NOT touch your ds20_v7_3.rom), model = DS20 in the ini (restored
#      on exit).
#   3. Arms the retire-trace window on the first IIC START to node 0x40 and
#      captures 4M retired instructions into ./traces (the .trc).  Mirrors the
#      COM1 console (banner + badge) and the SYSVAR store into the .out.
#   4. Tees console + stderr to fw_ds20_iicpoll_<ts>.out, then prints an A/B
#      summary from the captured signals.
#
# DECISION RULE (needs a build with EMULATR_DIAGNOSTIC_LOGGING = non-Release)
#   * "IIC-TRACE-ARM node=0x40" present -> the arm is compiled in and fired.
#     Absent -> stale/Release binary; rebuild RelWithDebInfo and rerun.
#   * With the arm live, read the retire .trc around the node-0x40 read to see
#     whether iic_init registers the node and what get_sysvar's fopen returns.
#   Do NOT sweep EMULATR_IIC_IRQ_BIT -- the interrupt path is compiled out on
#   PC264 and any bit is inert.
#
# USAGE  (run dir auto-resolves to out/build/relwithdebinfo, then Debug)
#   ./tools/diag_ds20_badge_ab.sh
#   ./tools/diag_ds20_badge_ab.sh /d/EmulatR/EmulatRAppUniV4/Emulatr/out/build/relwithdebinfo
#   RUN_DIR=/d/.../relwithdebinfo ./tools/diag_ds20_badge_ab.sh
#
# NOTE ON RUNTIME
#   Genuine cold boot; the IIC verify lands ~cycle 185M, so the run replays for
#   a while before the window arms.  The --max-cycles cap self-terminates.  To
#   stop early and cleanly, touch the EMULATR_STOP sentinel printed at startup.
#   Reaching the SRM banner on a cold boot needs the LFU driven at PuTTY
#   (u srm / y / exit); unattended it idles pre-banner blinking the OCP LED.
# ============================================================================
set -euo pipefail

# ---- locate the build/run dir ----------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Default run dir = the build/run dir CMake copies Emulatr.exe + firmware/ into.
# Build of record is RelWithDebInfo (out/build/relwithdebinfo); Debug is a
# fallback.  Probed in priority order and resolved whether this script lives in
# Emulatr/ or Emulatr/tools/.  Override with arg1 or env RUN_DIR.
DEFAULT_RUN_DIR=""
for cand in \
    "$SCRIPT_DIR/../out/build/relwithdebinfo" \
    "$SCRIPT_DIR/out/build/relwithdebinfo" \
    "$SCRIPT_DIR/../Debug" \
    "$SCRIPT_DIR/Debug"; do
    if [ -x "$cand/Emulatr.exe" ]; then DEFAULT_RUN_DIR="$cand"; break; fi
done
RUN_DIR="${1:-${RUN_DIR:-$DEFAULT_RUN_DIR}}"
RUN_DIR="$(cd "$RUN_DIR" 2>/dev/null && pwd || true)"
[[ -n "$RUN_DIR" ]] || { echo "FATAL: run dir not found; pass it as arg1 or set RUN_DIR (expected out/build/relwithdebinfo)"; exit 1; }
cd "$RUN_DIR"

FW="firmware/ds20_v7_3.exe"
INI="config/EmulatrV4.ini"
MEM=4294967296                 # 4 GiB in bytes
MAXCYC=22250000000               # ~250M cycles: comfortably past the ~185M verify
TS="$(date +%Y%m%d-%H%M%S)"
LOG="fw_ds20_iicpoll_${TS}.out"
DIAG_FLASH="$RUN_DIR/ds20_diag_flash.rom"   # dedicated, wiped each run

# ---- preflight -------------------------------------------------------------
[[ -x "./Emulatr.exe" ]] || { echo "FATAL: ./Emulatr.exe not found in $RUN_DIR"; exit 1; }
[[ -f "$FW" ]]           || { echo "FATAL: firmware not found: $RUN_DIR/$FW (CMake should copy it from ../../../firmware)"; exit 1; }
[[ -f "$INI" ]]          || { echo "FATAL: ini not found: $RUN_DIR/$INI"; exit 1; }
mkdir -p traces

# ---- set model = DS20 in the ini for this run; restore on exit --------------
cp -f "$INI" "$INI.diagbak"
trap 'mv -f "$INI.diagbak" "$INI" 2>/dev/null || true' EXIT
sed -i "s/^\(\s*model\s*=\s*\).*/\1DS20/" "$INI"

# ---- clean cold state ------------------------------------------------------
rm -f EMULATR_STOP                 # never start with a stale stop sentinel
rm -f "$DIAG_FLASH"                # pristine flash/NVRAM -> genuine cold boot
if ls snapshots/auto_*.axpsnap >/dev/null 2>&1; then
    rm -f snapshots/auto_*.axpsnap # ignore stale autosnaps (predig_* preserved)
fi

# ---- console mode: default attaches PuTTY; DIAG_HEADLESS=1 suppresses -------
: "${DIAG_HEADLESS:=0}"
if [ "$DIAG_HEADLESS" = "1" ]; then
    export EMULATR_NO_PUTTY=1                 # batch: no PuTTY window; TCP still up
else
    unset  EMULATR_NO_PUTTY                   # attach console (PuTTY/plink auto-launch)
fi

# ---- diagnostic environment (all verified against the live source) ---------
export EMULATR_NO_AUTOLOAD=1                 # cold (mirrors --no-autoload)
export EMULATR_AUTOSNAP=off                  # no periodic autosnap during diag
export EMULATR_FLASH_ROM="$DIAG_FLASH"       # dedicated flash, not ds20_v7_3.rom
unset  EMULATR_PLATFORM                       # don't force a platform override
unset  EMULATR_PLATFORM_CONFIG                # use <stem>_platform.json (ds20_v7_3)
export EMULATR_IIC_TRACE=1                    # log every IIC START + device byte read
export EMULATR_IIC_CTRL_TRACE=1              # log every S1 control write (START/PIN/NACK/STOP)
# ---- retire-window arm mode: DIAG_ARM=iic (default) | sysvar --------------
#   iic    : arm on the first node-0x40 IIC START (~cyc 185M) -- the read verify.
#   sysvar : arm on the base-SYSVAR store (PA 0x2058 == 0x5, ~cyc 222M) just
#            before get_sysvar()'s fopen("iic_ocp0") -- captures the badge
#            (member 1 vs 6) decision.  Needs a run that breaks past the
#            'Initializing table to defaults' point to reach cyc ~222M.
: "${DIAG_ARM:=iic}"
if [ "$DIAG_ARM" = "sysvar" ]; then
    unset  EMULATR_TRACE_ARM_ON_IIC          # don't burn the single window at 185M
    export EMULATR_TRACE_ARM_PA=0x2058       # arm on the SYSVAR store...
    export EMULATR_TRACE_ARM_VAL=0x5         # ...value 0x5 (base), just before get_sysvar
    export EMULATR_TRACE_ARM_INSTRS=2000000  # 2M-instr window over get_sysvar's fopen
else
    unset  EMULATR_TRACE_ARM_PA              # arm via the IIC START, not a PA store
    unset  EMULATR_TRACE_ARM_VAL
    export EMULATR_TRACE_ARM_ON_IIC=0x40     # arm retire window on first node-0x40 START
    export EMULATR_TRACE_ARM_INSTRS=4000000  # 4M-instr window
fi
export EMULATR_TRACE_WINDOW=1                 # force the DecListingSink retire-compact stream
export EMULATR_RETIRE_TRACE_DIR="$RUN_DIR/traces"
export EMULATR_GMEM_WATCH=0x2058             # watch the HWRPB SYSVAR-area store
export EMULATR_CONSOLE_MIRROR=1              # mirror COM1 console (banner + badge) into the .out
export EMULATR_SYSVAR_WATCH=1               # log the HWRPB SYSVAR store (encodes the badge member)

echo "=============================================================="
echo " DS20 badge A/B diagnostic"
echo "   run dir : $RUN_DIR"
echo "   firmware: $FW"
echo "   flash   : $DIAG_FLASH (fresh)"
echo "   log     : $RUN_DIR/$LOG"
echo "   traces  : $RUN_DIR/traces"
echo "   cap     : $MAXCYC cycles"
echo "   arm     : $DIAG_ARM  (iic = node-0x40 read | sysvar = get_sysvar badge)"
if [ "$DIAG_HEADLESS" = "1" ]; then
    echo "   console : HEADLESS (no PuTTY; TCP 10023 still open)"
else
    echo "   console : ATTACHED (PuTTY auto-launch, TCP 10023) -- DIAG_HEADLESS=1 to suppress"
fi
echo "=============================================================="

# ---- launch ----------------------------------------------------------------
set +e
./Emulatr.exe \
    --firmware "$FW" \
    --mem "$MEM" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e

# ---- post-run A/B summary --------------------------------------------------
# cnt PATTERN -> match count in the log, always a single clean integer (0 if
# none / missing).  grep -c prints "0" and exits 1 on no match; || true keeps
# set -e happy and avoids the double-print bug of "|| echo 0".
cnt() { local n; n="$(grep -c "$1" "$LOG" 2>/dev/null || true)"; printf '%s' "${n:-0}"; }

echo ""
echo "=============================================================="
echo " A/B SUMMARY  (exit code $RC)"
echo "=============================================================="
NEWEST_TRC="$(ls -t traces/*.trc 2>/dev/null | head -1 || true)"
ARM_LINE="$(grep -m1 'IIC-TRACE-ARM' "$LOG" 2>/dev/null || true)"

echo "  arm fired (node-0x40 START seen): ${ARM_LINE:-<none>}"
echo "  IIC-TXN   total / node-0x40     : $(cnt 'IIC-TXN') / $(cnt 'IIC-TXN addr=0x40')"
echo "  IIC-RD    total / node-0x40     : $(cnt 'IIC-RD') / $(cnt 'IIC-RD  addr=0x40')"
echo "  IIC-CTRL  total / node-0x40     : $(cnt 'IIC-CTRL') / $(cnt 'IIC-CTRL wr=.*node=0x40')"
echo "  retire trace (.trc)             : ${NEWEST_TRC:-<none written>}"
echo "  badge / SYSVAR markers:"
grep -inE 'SYSVAR|SYSTYPE|AlphaPC 264DP|AlphaServer DS20|member' "$LOG" 2>/dev/null | tail -n 12 | sed 's/^/    /' || true
echo "  console banner (last lines mirrored to stderr):"
grep -aiE 'Console V|AlphaPC|AlphaServer|P00>>>|UPD>|LFU' "$LOG" 2>/dev/null | tail -n 8 | sed 's/^/    /' || true
echo "--------------------------------------------------------------"
echo "  Read it as:"
echo "    * no IIC-TRACE-ARM line -> arm not in this binary; rebuild"
echo "      RelWithDebInfo (EMULATR_DIAGNOSTIC_LOGGING) and rerun."
echo "    * arm fired -> inspect the retire .trc around the node-0x40"
echo "      read for iic_init registration + get_sysvar fopen result."
echo "=============================================================="
echo ""
echo "Send back for analysis:"
echo "  1) $RUN_DIR/$LOG"
echo "  2) ${NEWEST_TRC:-<the newest traces/*.trc>}"
