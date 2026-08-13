#!/usr/bin/env bash
# ============================================================================
# emulatr_launch.sh -- platform-safe EmulatR launcher.  ONE source of truth
# for the per-architecture arguments, so a mis-badged / over-allocated launch
# (the recurring "false start") is impossible to type by hand.
# ----------------------------------------------------------------------------
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix in docs; decimal bytes to --mem.
#
# WHY (2026-08-11): two false starts came from the SAME class of error --
#   (1) DS10 booted at the ini's 4 GiB (max 2 GiB) -> memory-config churn;
#   (2) a stale ini model=DS10 latched on DS20 firmware -> mis-badged hybrid.
# Both are prevented here: the PLATFORM argument is the single input, and it
# fixes firmware, silicon-max memory, AND the ini model together.  See
# journals/20260811_JRN-PLATID-001 (stem-wins + geometry clamp) and
# GAP-PLAT-001 CFG-6.
#
# USAGE
#   tools/emulatr_launch.sh <platform> [purpose] [-- <extra Emulatr args>]
#     <platform> : ds10 | ds15 | ds20 | ds25 | es40 | es45   (case-insensitive)
#     [purpose]  : short tag for the log name (default: run)
#     everything after `--` is passed through to Emulatr.exe verbatim
#
# ENV PASS-THROUGH (exported before launch, e.g. for the value-gate hunt):
#   EMULATR_VALUE_GATE, EMULATR_VALUE_GATE_FLOOR, EMULATR_TRACE_WINDOW, ...
#   -- set them in your shell before calling; this script does not clear them.
#
# EXAMPLES
#   tools/emulatr_launch.sh ds20 bgate -- --no-autoload --autosnapshot off
#   EMULATR_VALUE_GATE=0xB6D0 EMULATR_VALUE_GATE_FLOOR=3000000000 \
#     tools/emulatr_launch.sh ds20 bgate
#
# The console (PuTTY) is ALWAYS raised (EMULATR_NO_PUTTY is unset) -- the
# operator drives the SRM dialog (project rule: diagnostic runs = PuTTY +
# architect at the console).
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="${RUN_DIR:-$PROJ_DIR/out/build/relwithdebinfo}"

# ---- platform table: THE single source of truth -----------------------------
# fields: <firmware-stem> <silicon-max-bytes>
# Silicon maxes are the vendor CEILING (architect-verified 2026-08-11):
#   DS10 2G (Webbrick); DS15 4G (single EV68C, 1G DIMMs); DS20/DS20e 4G
#   (Tsunami, 4 banks); DS25 16G (Titan, 16 DIMM slots); ES40 32G (Model 2,
#   32 DIMMs @ 8/motherboard; Model 1 = 16G, 16 DIMMs); ES45 32G (Titan,
#   32 DIMMs, MS620 kits of 4 DIMMs, 512M..4G per kit).
#   SOURCES (architect-supplied 2026-08-11): DS15/DS15a -- Global IT Corp
#   specs + Island Computers; DS20/DS20e/DS25 -- vendor QuickSpecs; ES40 --
#   Manx Docs / Compaq "AlphaServer ES40 Systems Technical Summary" (1 C-chip
#   + 8 D-chips + 2 P-chips, two 256-bit memory buses, ~5.2 GB/s); ES45 --
#   HPE/Compaq AlphaServer ES45 Linux-Ready technical documentation (dual
#   memory buses, up to 4-way interleave when arrays are equally populated).
# These are the CEILING, not the default -- see the mem-selection block below:
# launching AT 16-32G would force EmulatR to allocate that much HOST RAM.  So
# the default is a practical, host-safe DEFAULT_MEM, clamped to this ceiling;
# an explicit MEM=<bytes> is honored but clamped to the ceiling (over-ceiling
# = the DS10-4G-churn class, refused).
plat_stem() { case "$1" in
    ds10) echo ds10_v7_3 ;;  ds15) echo ds15_v7_3 ;;  ds20) echo ds20_v7_3 ;;
    ds25) echo ds25_v7_3 ;;  es40) echo es40_v7_3 ;;  es45) echo es45_v7_3 ;;
    *) return 1 ;; esac; }
plat_maxmem() { case "$1" in
    ds10) echo 2147483648 ;;    # 2 GiB
    ds15) echo 4294967296 ;;    # 4 GiB
    ds20) echo 4294967296 ;;    # 4 GiB
    ds25) echo 17179869184 ;;   # 16 GiB
    es40) echo 34359738368 ;;   # 32 GiB (Model 2)
    es45) echo 34359738368 ;;   # 32 GiB
    *) return 1 ;; esac; }

# Practical host-safe default (4 GiB).  Override per-run with MEM=<bytes>.
DEFAULT_MEM="${DEFAULT_MEM:-4294967296}"

# ---- args --------------------------------------------------------------------
[[ $# -ge 1 ]] || { echo "usage: $0 <ds10|ds15|ds20|ds25|es40|es45> [purpose] [-- extra args]"; exit 2; }
PLAT="$(echo "$1" | tr '[:upper:]' '[:lower:]')"; shift
STEM="$(plat_stem "$PLAT")"  || { echo "FATAL: unknown platform '$PLAT'"; exit 2; }
MAXMEM="$(plat_maxmem "$PLAT")"
MODEL_UP="$(echo "$PLAT" | tr '[:lower:]' '[:upper:]')"

# Memory selection: requested (MEM env, else DEFAULT_MEM), clamped to the
# silicon ceiling.  Over-ceiling is REFUSED (clamped + loud) -- that is the
# DS10-at-4G churn class this tool exists to prevent.
MEM_REQ="${MEM:-$DEFAULT_MEM}"
if [[ "$MEM_REQ" -gt "$MAXMEM" ]]; then
    echo "NOTE: requested MEM=$MEM_REQ exceeds the $MODEL_UP silicon max $MAXMEM -- clamping to the max."
    MEM_USE="$MAXMEM"
else
    MEM_USE="$MEM_REQ"
fi

PURPOSE="run"
if [[ $# -ge 1 && "$1" != "--" ]]; then PURPOSE="$1"; shift; fi
[[ "${1:-}" == "--" ]] && shift    # drop the separator; rest is pass-through

cd "$RUN_DIR"
EXE="./Emulatr.exe"; [[ -x "$EXE" ]] || EXE="./Emulatr"
FW="firmware/${STEM}.exe"
INI="config/Emulatr.ini"
TS="$(date +%Y%m%d_%H%M%S)"
mkdir -p logs traces
LOG="logs/${STEM}_${PURPOSE}_${TS}.log"

[[ -x "$EXE" ]] || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW"  ]] || { echo "FATAL: firmware $FW not found in $RUN_DIR"; exit 1; }

# ---- enforce the ini model to the platform (backup + restore on exit) --------
# Belt-and-suspenders: works on ANY binary, stem-wins or not.  Prevents the
# stale-model mis-badge.  The geometry is nailed by --mem below regardless.
if [[ -f "$INI" ]] && grep -qiE '^[[:space:]]*model[[:space:]]*=' "$INI"; then
    cp -f "$INI" "$INI.launchbak"
    trap 'mv -f "$INI.launchbak" "$INI" 2>/dev/null || true' EXIT
    SED_TMP="$(mktemp)"
    sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1${MODEL_UP}/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"
fi

unset EMULATR_NO_PUTTY                 # ALWAYS raise the console (operator rule)
export EMULATR_CONSOLE_MIRROR="${EMULATR_CONSOLE_MIRROR:-1}"

echo "======================================================================"
echo " EmulatR launch: platform=$MODEL_UP  firmware=$FW"
echo "   memory   = $MEM_USE bytes (default $DEFAULT_MEM; ceiling $MAXMEM for $MODEL_UP)"
echo "   ini model= $MODEL_UP (enforced; restored on exit)"
echo "   log      = $RUN_DIR/$LOG"
echo "   extra    = $*"
[[ -n "${EMULATR_VALUE_GATE:-}" ]] && echo "   VALUE_GATE=${EMULATR_VALUE_GATE} FLOOR=${EMULATR_VALUE_GATE_FLOOR:-0}"
echo "======================================================================"

# --mem makes the geometry unambiguous and legal for THIS platform, whatever
# the ini says.  --firmware's stem also drives the manifest + (stem-wins
# binaries) the model, so all three identity channels agree.
( "$EXE" \
    --firmware "$FW" \
    --mem "$MEM_USE" \
    "$@" \
    2>&1 || true ) | tee "$LOG"

echo "----------------------------------------------------------------------"
echo "run ended.  log: $RUN_DIR/$LOG"
grep -a -m1 'platform latched' "$LOG" || true
grep -a -m1 'PLATFORM MISMATCH' "$LOG" && echo "*** MISMATCH still present -- investigate ***" || true
