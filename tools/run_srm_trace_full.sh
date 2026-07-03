#!/usr/bin/env bash
# ============================================================================
# run_srm_trace_full.sh -- host-aware, model-parameterized SRM cold-boot with
# FULL comprehensive trace.  Project: EmulatR -- Alpha AXP / EV6 (V4).
# ASCII(128) only.  2026-07-02.
# ----------------------------------------------------------------------------
# Generalizes run_es40_srm_trace_full.sh across every model + both host builds:
#   * MSVC (Windows/Git Bash): Emulatr.exe in <root>/<Config>/; PuTTY console.
#   * macOS (clang):           Emulatr in out/build/<mac-subdir>/; nc console.
#
# ARGS (order-free after the model):
#   <model>                    REQUIRED: ds10 | ds20 | ds25 | es40 | es45
#   [relwithdebinfo|debug|release]   config; DEFAULT relwithdebinfo
#   [rebuild]                  build first (via tools/build_emulatr.sh <config>)
#   [-- <extra Emulatr args>]  everything after -- passes to the binary
#
# ENV KNOBS
#   RUN_DIR   force the dir holding the built binary (else auto-probed per host/config)
#   PORT      console TCP port (default 10023 + a per-model offset, so two models
#             can run CONCURRENTLY without colliding)
#   MAXCYC    cycle cap (default 3000000000; guards a possible cbox MCHK spin)
#   ARM       retire-window arm: sysvar (default) | iic | none
#   HEADLESS  1 = no PuTTY window (Windows); Mac is always headless (nc)
#
# EXAMPLES
#   ./tools/run_srm_trace_full.sh es40 rebuild
#   ./tools/run_srm_trace_full.sh ds20
#   ./tools/run_srm_trace_full.sh ds20 debug -- --max-cycles 0x20000000
#   PORT=10033 ./tools/run_srm_trace_full.sh es40      # explicit port
# ============================================================================
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SELF_DIR/.." && pwd)"

# ---- arg parse -------------------------------------------------------------
MODEL_IN="${1:-}"; shift || true
[[ -n "$MODEL_IN" ]] || { echo "usage: $0 <ds10|ds20|ds25|es40|es45> [relwithdebinfo|debug|release] [rebuild] [-- extra args]"; exit 2; }
CONFIG=relwithdebinfo
REBUILD=0
PASS=()
while [ $# -gt 0 ]; do
    case "$(echo "${1:-}" | tr '[:upper:]' '[:lower:]')" in
        relwithdebinfo|rwdi) CONFIG=relwithdebinfo ;;
        debug|dbg)           CONFIG=debug ;;
        release|rel)         CONFIG=release ;;
        rebuild|--rebuild|-r) REBUILD=1 ;;
        --) shift; PASS+=("$@"); break ;;
        *) PASS+=("$1") ;;
    esac
    shift
done

# ---- model map (name / MODEL string / console-port offset / family) --------
case "$(echo "$MODEL_IN" | tr '[:upper:]' '[:lower:]')" in
    ds10) NAME=ds10; MODEL=DS10; POFF=0; FAMILY="Tsunami 21272" ;;
    ds20) NAME=ds20; MODEL=DS20; POFF=1; FAMILY="Tsunami 21272" ;;
    ds25) NAME=ds25; MODEL=DS25; POFF=2; FAMILY="Titan 21274 (experimental)" ;;
    es40) NAME=es40; MODEL=ES40; POFF=3; FAMILY="Tsunami 21272" ;;
    es45) NAME=es45; MODEL=ES45; POFF=4; FAMILY="Titan 21274 (experimental)" ;;
    *) echo "FATAL: unknown model '$MODEL_IN' (ds10|ds20|ds25|es40|es45)"; exit 2 ;;
esac

# ---- optional rebuild ------------------------------------------------------
if [ "$REBUILD" = "1" ]; then
    echo "=== REBUILD ($CONFIG) -> tools/build_emulatr.sh ==="
    "$SELF_DIR/build_emulatr.sh" "$CONFIG" || { echo "FATAL: rebuild failed; not launching."; exit 1; }
    echo "=== rebuild done; continuing to launch ==="
fi

# ---- host + binary + run-dir resolution ------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST=win; BIN=Emulatr.exe ;;
    Darwin)               HOST=mac; BIN=Emulatr ;;
    *)                    HOST=nix; BIN=Emulatr ;;
esac
if [ "$HOST" = "win" ]; then
    case "$CONFIG" in
        relwithdebinfo) VSCFG=RelWithDebInfo ;;
        debug) VSCFG=Debug ;;
        release) VSCFG=Release ;;
    esac
    CANDS=( "$PROJ_ROOT/$VSCFG" "$PROJ_ROOT/out/build/$CONFIG" )
else
    case "$CONFIG" in
        relwithdebinfo) SUB=mac-debug ;;
        release) SUB=mac-release ;;
        debug) SUB=mac-debug-g ;;
    esac
    CANDS=( "$PROJ_ROOT/out/build/$SUB" )
fi
RESOLVED=""
if [ -n "${RUN_DIR:-}" ]; then
    RESOLVED="$RUN_DIR"
else
    for c in "${CANDS[@]}"; do
        if [ -x "$c/$BIN" ]; then RESOLVED="$c"; break; fi
    done
fi
[[ -n "$RESOLVED" ]] || { echo "FATAL: no $BIN found for config '$CONFIG' (looked: ${CANDS[*]}). Build first (add 'rebuild') or set RUN_DIR."; exit 1; }
RUN_DIR="$(cd "$RESOLVED" && pwd)"
cd "$RUN_DIR"
[[ -x "./$BIN" ]] || { echo "FATAL: ./$BIN not in $RUN_DIR"; exit 1; }

# ---- paths + run params ----------------------------------------------------
SRC_FW="$PROJ_ROOT/firmware/${NAME}_v7_3.exe"
DST_FW="firmware/${NAME}_v7_3.exe"
SRC_MANIFEST="$PROJ_ROOT/${NAME}_v7_3_platform.json"
INI="config/EmulatrV4.ini"
MEM=4294967296
PORT="${PORT:-$((10023 + POFF))}"          # per-model default so models can run concurrently
MAXCYC="${MAXCYC:-3000000000}"
ARM="${ARM:-sysvar}"
TS="$(date +%Y%m%d-%H%M%S)"
TRACE_DIR="traces"
DEC_LOG="${TRACE_DIR}/${TS}_${NAME}_dec.log"
MACHINE_LOG="${TRACE_DIR}/${TS}_${NAME}_machine.log"
CONSOLE_LOG="${TRACE_DIR}/${TS}_${NAME}_console.out"
DIAG_FLASH="$RUN_DIR/${NAME}_diag_flash.rom"

# ---- preflight -------------------------------------------------------------
[[ -f "$SRC_FW" ]] || { echo "FATAL: firmware not found: $SRC_FW (no image for $MODEL yet?)"; exit 1; }
[[ -f "$INI" ]]    || { echo "FATAL: ini not found: $RUN_DIR/$INI"; exit 1; }
mkdir -p firmware "$TRACE_DIR"
cp -f "$SRC_FW" "$DST_FW"
if [ -f "$SRC_MANIFEST" ]; then
    cp -f "$SRC_MANIFEST" "./${NAME}_v7_3_platform.json"
    echo "manifest : refreshed ./${NAME}_v7_3_platform.json from source"
else
    echo "manifest : WARNING source $SRC_MANIFEST missing -- may fall back to DS10 bus"
fi

# ---- set model in ini for this run; restore on exit ------------------------
cp -f "$INI" "$INI.runbak"
trap 'mv -f "$INI.runbak" "$INI" 2>/dev/null || true' EXIT
SED_TMP="$(mktemp)"
sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1${MODEL}/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"

# ---- clean cold state ------------------------------------------------------
rm -f EMULATR_STOP
rm -f "$DIAG_FLASH"                         # pristine flash/NVRAM -> cold + oem_string cleared
if ls snapshots/auto_*.axpsnap >/dev/null 2>&1; then
    rm -f snapshots/auto_*.axpsnap
    echo "cold-start: purged stale auto_*.axpsnap"
fi

# ---- console: PuTTY (Windows) or nc (Mac) ----------------------------------
: "${HEADLESS:=0}"
export EMULATR_CONSOLE_PORT="$PORT"
if [ "$HOST" = "win" ] && [ "$HEADLESS" != "1" ]; then
    unset EMULATR_NO_PUTTY
    if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
        echo "WARN: PuTTY not on PATH -- attach manually: putty -raw localhost $PORT"
    fi
    CONSOLE_DESC="PuTTY auto-launch, TCP $PORT"
else
    export EMULATR_NO_PUTTY=1               # Mac PuTTY is Windows-pinned; use nc
    CONSOLE_DESC="headless, TCP $PORT (connect: nc localhost $PORT)"
fi

# ---- comprehensive observe-only instrumentation ----------------------------
export EMULATR_FLASH_ROM="$DIAG_FLASH"
unset  EMULATR_PLATFORM EMULATR_PLATFORM_CONFIG
export EMULATR_CONSOLE_MIRROR=1
export EMULATR_IIC_TRACE=1
export EMULATR_IIC_CTRL_TRACE=1
export EMULATR_SYSVAR_WATCH=1
export EMULATR_GMEM_WATCH=0x2058
if [ "$ARM" = "none" ]; then
    unset EMULATR_TRACE_WINDOW EMULATR_RETIRE_TRACE_DIR \
          EMULATR_TRACE_ARM_ON_IIC EMULATR_TRACE_ARM_PA EMULATR_TRACE_ARM_VAL EMULATR_TRACE_ARM_INSTRS
else
    export EMULATR_TRACE_WINDOW=1
    export EMULATR_RETIRE_TRACE_DIR="$RUN_DIR/$TRACE_DIR"
    if [ "$ARM" = "iic" ]; then
        unset  EMULATR_TRACE_ARM_PA EMULATR_TRACE_ARM_VAL
        export EMULATR_TRACE_ARM_ON_IIC=0x40
        export EMULATR_TRACE_ARM_INSTRS=4000000
    else
        unset  EMULATR_TRACE_ARM_ON_IIC
        export EMULATR_TRACE_ARM_PA=0x2058
        export EMULATR_TRACE_ARM_VAL=0x5
        export EMULATR_TRACE_ARM_INSTRS=2000000
    fi
fi

# ---- config instrument warnings --------------------------------------------
if [ "$CONFIG" = "release" ]; then
    echo "WARN: RELEASE build -- EMULATR_DIAGNOSTIC_LOGGING + trace hooks are COMPILED OUT."
    echo "      No GMEM-WATCH / IIC / SYSVAR / .trc will emit. Use relwithdebinfo for a traced run."
elif [ "$CONFIG" = "debug" ]; then
    echo "NOTE: DEBUG build -- instruments ON but the cold boot is SLOW. RelWithDebInfo is recommended."
fi

echo "=== EmulatR $MODEL SRM cold-boot -- FULL trace ==============="
echo "  host     : $HOST     binary: ./$BIN"
echo "  config   : $CONFIG"
echo "  run dir  : $RUN_DIR"
echo "  firmware : $DST_FW   ($FAMILY)"
echo "  memory   : $MEM bytes (4 GiB)"
echo "  flash    : $DIAG_FLASH (fresh -> cold + oem_string cleared)"
echo "  console  : $CONSOLE_DESC"
echo "  cap      : $MAXCYC cycles   arm: $ARM"
echo "  logs     : $RUN_DIR/$DEC_LOG"
echo "             $RUN_DIR/$MACHINE_LOG"
echo "             $RUN_DIR/$CONSOLE_LOG"
echo "  extra    : ${PASS[*]:-<none>}"
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
    "${PASS[@]}" 2>&1 | tee "$CONSOLE_LOG"
RC=${PIPESTATUS[0]}
set -e

# ---- post-run summary ------------------------------------------------------
cnt() { local n; n="$(grep -c "$1" "$CONSOLE_LOG" 2>/dev/null || true)"; printf '%s' "${n:-0}"; }
NEWEST_TRC="$(ls -t "$TRACE_DIR"/*.trc 2>/dev/null | head -1 || true)"
echo ""
echo "=== $MODEL RUN SUMMARY  (exit code $RC) ======================"
echo "  manifest load (want $MODEL, NOT DS10-fallback):"
grep -inE "manifest|platform|built-in default|DS10 bus|$MODEL|Cypress|ALi|M1543" "$CONSOLE_LOG" 2>/dev/null | head -8 | sed 's/^/    /' || true
echo "  banner / boot progress:"
grep -aiE "Console V|Compaq AlphaServer|AlphaServer|AlphaPC|P00>>>|UPD>|LFU|initializ" "$CONSOLE_LOG" 2>/dev/null | tail -10 | sed 's/^/    /' || true
echo "  SYSVAR / SYSTYPE / member (badge decision):"
grep -inE "SYSVAR|SYSTYPE|GMEM-WATCH|member" "$CONSOLE_LOG" 2>/dev/null | tail -8 | sed 's/^/    /' || true
echo "  stall / halt / machine-check:"
grep -inE "HALTPROBE|MCHK|machine check|cbox|smir|StopReason|MaxCycles|fault|PANIC|deadlock" "$CONSOLE_LOG" 2>/dev/null | tail -10 | sed 's/^/    /' || true
echo "  IIC-TXN total / node-0x40   : $(cnt 'IIC-TXN') / $(cnt 'IIC-TXN addr=0x40')"
echo "  retire trace (.trc)         : ${NEWEST_TRC:-<none written>}"
echo "--------------------------------------------------------------"
echo "Send back for analysis:"
echo "  1) $RUN_DIR/$CONSOLE_LOG"
echo "  2) $RUN_DIR/$MACHINE_LOG"
echo "  3) ${NEWEST_TRC:-<newest traces/*.trc>}"
