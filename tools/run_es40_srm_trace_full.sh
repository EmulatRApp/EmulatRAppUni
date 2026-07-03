#!/usr/bin/env bash
# ============================================================================
# run_es40_srm_trace_full.sh -- ES40 (CLIPPER) SRM cold boot, FULL comprehensive
# trace, PuTTY console attached.  Project: EmulatR -- Alpha AXP / EV6 (V4).
# Git Bash on Windows.  ASCII(128) only.  2026-07-02 (TEP / Claude).
# ----------------------------------------------------------------------------
# PURPOSE
#   First real bring-up attempt of firmware/es40_v7_3.exe (SRM Console V7.3) to
#   the interactive ">>>" prompt, capturing the MOST comprehensive trace the CLI
#   + env instruments give WITHOUT a rebuild, with PuTTY auto-launched so you can
#   drive the LFU / >>> live.
#
# TARGET  (ES40 = SRM "CLIPPER", pc264 family, GCT__TSUNAMI, Tsunami/Typhoon
#          21272 chipset; south bridge is the ALi M1543C -- the manifest ships a
#          Cypress STAND-IN that carries the ISA/UART console path).
#   model    = ES40  (enforced in the ini for this run; restored on exit)
#   cpuCount = 1      (ES40 is 4-socket; advertise 1 so GCT/SRM does not spin on
#                      absent secondaries -- already set in the ini)
#   memory   = 4 GiB  (32 MB-aligned per CLIPPER GCT data)
#
# WHAT "COMPREHENSIVE" MEANS HERE (honest scope)
#   * CLI  --trace  = TRACE_PAL_WINDOW (0x40) | TRACE_RETIRE_COMPACT (0x80) into
#     ./traces/<ts>_es40_{dec,machine}.log.  This is NOT TRACE_ALL (no per-instr
#     regfile dumps) -- TRACE_ALL needs an AppOptions change (deferred).
#   * ALL observe-only env instruments ON: console mirror, IIC + IIC-CTRL trace,
#     SYSVAR/GMEM watch, and a WINDOWED retire .trc armed on the HWRPB SYSVAR
#     store (get_sysvar / member-decision region).  The retire window is bounded
#     on purpose -- an unbounded per-instr .trc of a cold boot is multi-GB.
#   * retire-compact channel is ALSO hard-coded to X:\traces\<ts>_srm.trc inside
#     DecListingSink (NOT redirectable).  If drive X: does not exist that channel
#     is simply absent; the ./traces logs + .trc are the ones that matter.
#   NOTE: requires a RelWithDebInfo (or Debug) build -- the env instruments are
#   compiled out in Release (EMULATR_DIAGNOSTIC_LOGGING guard).
#
# COLD BOOT + BANNER
#   Genuine cold boot (--no-autoload + fresh diagnostic flash).  A fresh flash
#   also clears NVRAM oem_string, so the banner shows "Compaq AlphaServer ES40"
#   (a non-empty oem_string would MASK the platform name).  To reach the SRM
#   banner you typically drive the LFU at the PuTTY console (u srm / y / exit);
#   unattended it idles pre-banner.  Console: TCP 10023, PuTTY auto-launched.
#
# USAGE  (run dir auto-resolves to out/build/relwithdebinfo)
#   ./tools/run_es40_srm_trace_full.sh
#   ./tools/run_es40_srm_trace_full.sh --max-cycles 0x40000000   # tighter cap
#   ./tools/run_es40_srm_trace_full.sh rebuild         # build (RelWithDebInfo
#                                                      # + trace hooks) then run
#   REBUILD=1 ./tools/run_es40_srm_trace_full.sh       # same, via env
#   RUN_DIR=/d/.../RelWithDebInfo ./tools/run_es40_srm_trace_full.sh
#   MAXCYC=1500000000 ARM=sysvar HEADLESS=1 ./tools/run_es40_srm_trace_full.sh
#
# ENV KNOBS
#   RUN_DIR   override the build/run dir (default: out/build/relwithdebinfo)
#   MAXCYC    cycle cap (default 3000000000; guards a possible cbox MCHK spin)
#   ARM       retire-window arm point:  sysvar (default) | iic | none
#               sysvar = arm on the SYSVAR 0x2058 store (get_sysvar badge region)
#               iic    = arm on the first node-0x40 IIC START (device-probe region)
#               none   = no windowed .trc (dec/machine + console still captured)
#   HEADLESS  1 = no PuTTY window (TCP still up); default attaches PuTTY
#   PORT      console TCP port (default 10023)
# ============================================================================
set -euo pipefail

# ---- optional rebuild (opt-in): first arg 'rebuild'/'--rebuild', or REBUILD=1 --
# When set, (re)build RelWithDebInfo + trace hooks via the sibling build script
# BEFORE launching, so one command can build-and-run.  Default OFF so a normal
# run never pays build time.  Remaining args still pass through to Emulatr.exe.
: "${REBUILD:=0}"
if [ "${1:-}" = "rebuild" ] || [ "${1:-}" = "--rebuild" ] || [ "${1:-}" = "-r" ]; then
    REBUILD=1; shift
fi
_SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$REBUILD" = "1" ]; then
    echo "=== REBUILD requested -> $_SELF_DIR/build_es40_relwithdebinfo.sh ==="
    "$_SELF_DIR/build_es40_relwithdebinfo.sh" || { echo "FATAL: rebuild failed; not launching."; exit 1; }
    echo "=== rebuild done; continuing to launch ======================="
fi

# ---- locate the run dir (the dir that holds the built Emulatr.exe) ---------
# The CMake build is IN-SOURCE (VS 17 2022 generator at the project root), so
# the exe is in <root>/<Config>/Emulatr.exe.  The out/build/<cfg> dirs are
# deploy dirs, NOT CMake build dirs.  Probe the likely locations in priority
# order; RelWithDebInfo first (diag logging on, perf ok).  Override via RUN_DIR.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"          # tools/ -> project root (has CMakeLists.txt)
DEFAULT_RUN_DIR=""
for cand in \
    "$PROJ_ROOT/out/build/relwithdebinfo" \
    "$PROJ_ROOT/RelWithDebInfo" \
    "$PROJ_ROOT/out/build/debug" \
    "$PROJ_ROOT/Debug"; do
    if [ -x "$cand/Emulatr.exe" ] || [ -x "$cand/Emulatr" ]; then DEFAULT_RUN_DIR="$cand"; break; fi
done
RUN_DIR="${RUN_DIR:-$DEFAULT_RUN_DIR}"
RUN_DIR="$(cd "$RUN_DIR" 2>/dev/null && pwd || true)"
[[ -n "$RUN_DIR" ]] || { echo "FATAL: no dir with a built Emulatr.exe found (looked in out/build/relwithdebinfo, RelWithDebInfo, Debug). Build first, or set RUN_DIR to where Emulatr.exe is."; exit 1; }
cd "$RUN_DIR"
if [[ "${RUN_DIR,,}" != *relwithdebinfo* ]]; then
    echo "WARN: RUN_DIR is $RUN_DIR (not RelWithDebInfo). Diag instruments need a non-Release build; a Release exe here will emit NO GMEM-WATCH/IIC/.trc lines."
fi

# Binary name: MSVC -> Emulatr.exe; macOS/Linux clang -> bare Emulatr.
if   [ -x "./Emulatr.exe" ]; then BIN="Emulatr.exe"
elif [ -x "./Emulatr"     ]; then BIN="Emulatr"
else echo "FATAL: neither ./Emulatr.exe nor ./Emulatr found in $RUN_DIR -- build RelWithDebInfo first."; exit 1; fi

NAME="es40"
MODEL="ES40"
SRC_FW="$PROJ_ROOT/firmware/${NAME}_v7_3.exe"
DST_FW="firmware/${NAME}_v7_3.exe"
SRC_MANIFEST="$PROJ_ROOT/${NAME}_v7_3_platform.json"
INI="config/EmulatrV4.ini"
MEM=4294967296                                  # 4 GiB
PORT="${PORT:-${EMULATR_CONSOLE_PORT:-10023}}"
MAXCYC="${MAXCYC:-3000000000}"
ARM="${ARM:-sysvar}"
TS="$(date +%Y%m%d-%H%M%S)"
TRACE_DIR="traces"
DEC_LOG="${TRACE_DIR}/${TS}_es40_dec.log"
MACHINE_LOG="${TRACE_DIR}/${TS}_es40_machine.log"
CONSOLE_LOG="${TRACE_DIR}/${TS}_es40_console.out"
DIAG_FLASH="$RUN_DIR/es40_diag_flash.rom"       # dedicated, wiped each run

# ---- preflight -------------------------------------------------------------
[[ -f "$SRC_FW" ]] || { echo "FATAL: firmware not found: $SRC_FW"; exit 1; }
[[ -f "$INI" ]]    || { echo "FATAL: ini not found: $RUN_DIR/$INI"; exit 1; }
mkdir -p firmware "$TRACE_DIR"
cp -f "$SRC_FW" "$DST_FW"

# Refresh the ES40 manifest from source EVERY run.  CMake POST_BUILD only
# re-copies on relink, so a manifest edit can be left stale beside the exe; a
# missing/stale manifest silently falls back to the built-in DS10 bus (wrong
# device set for ES40).  This is watch-out #2.
if [ -f "$SRC_MANIFEST" ]; then
    cp -f "$SRC_MANIFEST" "./${NAME}_v7_3_platform.json"
    echo "manifest : refreshed ./${NAME}_v7_3_platform.json from source"
else
    echo "manifest : WARNING source $SRC_MANIFEST missing -- may fall back to DS10 bus"
fi

# ---- set model = ES40 in ini for this run; restore on exit -----------------
cp -f "$INI" "$INI.runbak"
trap 'mv -f "$INI.runbak" "$INI" 2>/dev/null || true' EXIT
SED_TMP="$(mktemp)"
sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1${MODEL}/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"

# ---- clean cold state ------------------------------------------------------
rm -f EMULATR_STOP                              # never start with a stale stop sentinel
rm -f "$DIAG_FLASH"                             # pristine flash/NVRAM => cold + clears oem_string
if ls snapshots/auto_*.axpsnap >/dev/null 2>&1; then
    freed=$(du -ch snapshots/auto_*.axpsnap 2>/dev/null | tail -1 | cut -f1)
    rm -f snapshots/auto_*.axpsnap              # predig_* anchors preserved
    echo "cold-start: purged stale auto_*.axpsnap (${freed} reclaimed)"
fi

# ---- console: PuTTY auto-launch (default) or headless ----------------------
: "${HEADLESS:=0}"
if [ "$HEADLESS" = "1" ]; then
    export EMULATR_NO_PUTTY=1                   # no window; TCP still open on $PORT
else
    unset EMULATR_NO_PUTTY                      # KEEP PuTTY auto-launch ON
    if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
        echo "WARN: PuTTY not on PATH -- auto-launch may warn; attach manually: putty -raw localhost $PORT"
    fi
fi
export EMULATR_CONSOLE_PORT="$PORT"

# ---- comprehensive observe-only instrumentation ----------------------------
export EMULATR_FLASH_ROM="$DIAG_FLASH"          # dedicated flash, not es40_v7_3.rom
unset  EMULATR_PLATFORM                          # ISP path (do not force silicon)
unset  EMULATR_PLATFORM_CONFIG                   # use <stem>_platform.json (es40_v7_3)
export EMULATR_CONSOLE_MIRROR=1                  # banner + console -> stderr/log
export EMULATR_IIC_TRACE=1                       # every IIC START + device byte read
export EMULATR_IIC_CTRL_TRACE=1                  # every S1 control write
export EMULATR_SYSVAR_WATCH=1                    # HWRPB SYSVAR store (badge member decision)
export EMULATR_GMEM_WATCH=0x2058                 # sink-level watch on the SYSVAR quadword

# Windowed retire .trc (bounded; an unbounded cold-boot .trc is multi-GB).
if [ "$ARM" = "none" ]; then
    unset EMULATR_TRACE_WINDOW EMULATR_RETIRE_TRACE_DIR \
          EMULATR_TRACE_ARM_ON_IIC EMULATR_TRACE_ARM_PA EMULATR_TRACE_ARM_VAL EMULATR_TRACE_ARM_INSTRS
else
    export EMULATR_TRACE_WINDOW=1
    export EMULATR_RETIRE_TRACE_DIR="$RUN_DIR/$TRACE_DIR"
    if [ "$ARM" = "iic" ]; then
        unset  EMULATR_TRACE_ARM_PA EMULATR_TRACE_ARM_VAL
        export EMULATR_TRACE_ARM_ON_IIC=0x40     # arm on first node-0x40 IIC START
        export EMULATR_TRACE_ARM_INSTRS=4000000
    else                                         # sysvar (default)
        unset  EMULATR_TRACE_ARM_ON_IIC
        export EMULATR_TRACE_ARM_PA=0x2058       # arm on the SYSVAR store...
        export EMULATR_TRACE_ARM_VAL=0x5         # ...base value 0x5, just before get_sysvar
        export EMULATR_TRACE_ARM_INSTRS=2000000
    fi
fi

echo "=== EmulatR ES40 (CLIPPER) SRM cold-boot -- FULL trace ======="
echo "  run dir  : $RUN_DIR"
echo "  binary   : ./$BIN"
echo "  firmware : $DST_FW"
echo "  model    : $MODEL   (GCT__TSUNAMI; ini-driven)"
echo "  memory   : $MEM bytes (4 GiB)"
echo "  flash    : $DIAG_FLASH (fresh -> cold + oem_string cleared)"
echo "  boot     : COLD (--no-autoload); runs to >>> console"
echo "  console  : $([ "$HEADLESS" = 1 ] && echo "HEADLESS (TCP $PORT)" || echo "PuTTY auto-launch, TCP $PORT")"
echo "  cap      : $MAXCYC cycles"
echo "  arm      : $ARM  (sysvar = get_sysvar badge | iic = node-0x40 | none)"
echo "  logs     : $RUN_DIR/$DEC_LOG"
echo "             $RUN_DIR/$MACHINE_LOG"
echo "             $RUN_DIR/$CONSOLE_LOG   (console + stderr)"
echo "             retire .trc -> $RUN_DIR/$TRACE_DIR  (+ X:\\traces\\${TS}_srm.trc if X: exists)"
echo "  extra    : $*"
echo "============================================================="

# ---- launch ----------------------------------------------------------------
set +e
"./$BIN" \
    --firmware "$DST_FW" \
    --mem "$MEM" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    --trace "$DEC_LOG,$MACHINE_LOG" \
    "$@" 2>&1 | tee "$CONSOLE_LOG"
RC=${PIPESTATUS[0]}
set -e

# ---- post-run summary ------------------------------------------------------
cnt() { local n; n="$(grep -c "$1" "$CONSOLE_LOG" 2>/dev/null || true)"; printf '%s' "${n:-0}"; }
NEWEST_TRC="$(ls -t "$TRACE_DIR"/*.trc 2>/dev/null | head -1 || true)"

echo ""
echo "=== ES40 RUN SUMMARY  (exit code $RC) ========================"
echo "  manifest load (want ES40, NOT DS10-fallback):"
grep -inE "manifest|platform|built-in default|DS10 bus|ES40|Cypress|ALi|M1543" "$CONSOLE_LOG" 2>/dev/null | head -8 | sed 's/^/    /' || true
echo "  banner / boot progress:"
grep -aiE "Console V|Compaq AlphaServer|AlphaServer ES40|AlphaPC|P00>>>|UPD>|LFU|initializ" "$CONSOLE_LOG" 2>/dev/null | tail -10 | sed 's/^/    /' || true
echo "  SYSVAR / SYSTYPE / member (ES40 badge decision):"
grep -inE "SYSVAR|SYSTYPE|GMEM-WATCH|member" "$CONSOLE_LOG" 2>/dev/null | tail -8 | sed 's/^/    /' || true
echo "  stall / halt / machine-check watch:"
grep -inE "HALTPROBE|MCHK|machine check|cbox|smir|StopReason|MaxCycles|fault|PANIC|deadlock" "$CONSOLE_LOG" 2>/dev/null | tail -10 | sed 's/^/    /' || true
echo "  PROFILE / WARP-ACCOUNTING:"
grep -inE "PROFILE|WARP-ACCOUNTING|MHz" "$CONSOLE_LOG" 2>/dev/null | tail -6 | sed 's/^/    /' || true
echo "  IIC-TXN total / node-0x40   : $(cnt 'IIC-TXN') / $(cnt 'IIC-TXN addr=0x40')"
echo "  retire trace (.trc)         : ${NEWEST_TRC:-<none written>}"
echo "--------------------------------------------------------------"
echo "  If you see 'built-in default DS10 bus' -> the ES40 manifest did NOT load."
echo "  If it stalls before the banner -> capture the last machine-log PC window;"
echo "  re-run with ARM=none for a lean pass, or ARM=iic to catch device probes."
echo "=============================================================="
echo ""
echo "Send back for analysis:"
echo "  1) $RUN_DIR/$CONSOLE_LOG"
echo "  2) $RUN_DIR/$MACHINE_LOG"
echo "  3) ${NEWEST_TRC:-<the newest traces/*.trc>}"
