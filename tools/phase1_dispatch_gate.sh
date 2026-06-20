#!/usr/bin/env bash
# ============================================================================
# phase1_dispatch_gate.sh -- AlphaCpuAgent Phase-1 acceptance gate.
# Project: EmulatR -- Alpha AXP / EV6 (V4).  Git Bash on Windows.
# ----------------------------------------------------------------------------
# PURPOSE
#   Prove that the dispatcher-driven boot (EMULATR_DISPATCH=1 -> one
#   AlphaCpuAgent under SequentialDriver, calling Machine::stepCycle) is
#   BYTE-IDENTICAL to the legacy Machine::run boot.  This is the step-3 gate
#   for AlphaCpuAgent Phase 1 (see journals/20260619_alphacpuagent_phase1_design.md
#   and the 2026-06-19 23:07 checkpoint, which established the manual method this
#   script automates).
#
#   Both paths delegate to the IDENTICAL per-cycle kernel (stepCycle), so a
#   passing gate confirms the loop/clock/dispatch WIRING is pure -- it does not
#   re-verify the CPU kernel.
#
# USAGE
#   cd <run-dir>/tools && ./phase1_dispatch_gate.sh [model] [max-cycles]
#     model       ds10|ds20|ds25|es40|es45   (default: ds20)
#     max-cycles  hex/dec cap                 (default: 0x40000000, the 23:07 baseline)
#
# METHOD (mirrors the manual gate)
#   1. Equalize the factory-init confound: purge *_flash.rom before EACH run so
#      both boots build NVRAM identically (the 23:07 flash confound).
#   2. Run two COLD boots via run_fw.sh: (A) legacy, (B) EMULATR_DISPATCH=1.
#   3. NORMALIZE volatile tokens (wall-clock timestamps, snapshot/log filenames,
#      the RPCC-probe env line, any dispatch banner) so only SEMANTIC content
#      remains.
#   4. diff the normalized host logs.  EMPTY diff => PASS.
#
# NOTE ON SCOPE
#   This gates the HOST log (fw_<model>_<ts>.out): retire cadence, StopReason,
#   final cycle index, autosnap cadence, traces.  Because the agent path calls
#   the identical stepCycle, host-log-identical implies the guest SRM console
#   stream (TCP 10023) is identical too.  For belt-and-suspenders, also capture
#   the guest console of each run with plink and diff those (see TAIL note).
#
# IMPORTANT: this script does NOT build.  Build Emulatr.exe client-side first.
# ============================================================================
set -uo pipefail

MODEL="${1:-ds20}"
MAXCYC="${2:-0x40000000}"

# Resolve the run-dir ROOT = the directory that actually holds Emulatr.exe.
# This script may be invoked from the SOURCE tree (tools/) or the DEPLOYED copy
# (run-dir/tools/), and the user may be cd'd anywhere -- so do NOT blindly anchor
# to the script's own ../.  Probe, in order: $EMULATR_RUNDIR, the current dir,
# script-dir/.. , and script-dir; pick the first that contains Emulatr.exe.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT=""
for cand in "${EMULATR_RUNDIR:-}" "$PWD" "$SCRIPT_DIR/.." "$SCRIPT_DIR"; do
    [[ -n "$cand" ]] || continue
    cand="$(cd "$cand" 2>/dev/null && pwd)" || continue
    if [[ -x "$cand/Emulatr.exe" ]]; then ROOT="$cand"; break; fi
done
if [[ -z "$ROOT" ]]; then
    echo "FATAL: could not locate Emulatr.exe."
    echo "  Run this from the build-run dir (where Emulatr.exe lives), e.g.:"
    echo "    cd /d/emulatr/emulatrappuniv4/emulatr/out/build/release"
    echo "    /d/emulatr/emulatrappuniv4/emulatr/tools/phase1_dispatch_gate.sh ds20 0x40000000"
    echo "  or set EMULATR_RUNDIR=/path/to/build/release first."
    exit 1
fi
cd "$ROOT"

# ---- locate run_fw.sh -- MUST be the ROOT copy (it anchors to its own dir via
#      BASH_SOURCE and expects ./Emulatr.exe beside it; the tools/ copy would
#      anchor to tools/ and fail) ---------------------------------------------
RUNFW="$ROOT/run_fw.sh"
[[ -x "$RUNFW" ]] || { echo "FATAL: run_fw.sh not found beside Emulatr.exe at $RUNFW"; exit 1; }

# ---- normalize: blank out volatile tokens, drop non-semantic lines ----------
normalize() {  # stdin -> stdout
    sed -E \
        -e 's/[0-9]{8}-[0-9]{6}/<TS>/g' \
        -e 's/[0-9]{4}-[0-9]{2}-[0-9]{2}[ T][0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?/<TS>/g' \
        -e 's/(connected from [0-9A-Fa-f:.]+):[0-9]+/\1:<PORT>/g' \
        -e 's/auto_(halt_)?[0-9A-Fa-fx_]+\.axpsnap/<SNAP>/g' \
        -e 's/app_output_[0-9]+\.log/<LOG>/g' \
        -e 's/fw_[A-Za-z0-9]+_[0-9-]+\.out/<LOG>/g' \
    | grep -vE '\[RPCC probe\] armed' \
    | grep -vE 'EMULATR_DISPATCH|\[dispatch\]' \
    | grep -vE '^===|^  (firmware|model|memory|console|log|extra|mode) ' \
    || true
}

# ---- run one cold boot, capture its newest host log -------------------------
run_one() {  # $1=tag  $2=dispatch(0|1)
    local tag="$1" disp="$2"
    echo ">>> gate run [$tag]: model=$MODEL dispatch=$disp max-cycles=$MAXCYC"
    rm -f ./*_flash.rom 2>/dev/null || true   # equalize factory NVRAM init
    rm -f ./EMULATR_STOP 2>/dev/null || true
    if [[ "$disp" == "1" ]]; then
        EMULATR_DISPATCH=1 "$RUNFW" "$MODEL" cold --max-cycles "$MAXCYC" >/dev/null 2>&1 || true
    else
        "$RUNFW" "$MODEL" cold --max-cycles "$MAXCYC" >/dev/null 2>&1 || true
    fi
    local log
    log="$(ls -t "fw_${MODEL}_"*.out 2>/dev/null | head -1)"
    [[ -n "$log" ]] || { echo "FATAL: no fw_${MODEL}_*.out produced by run [$tag]"; exit 1; }
    cp -f "$log" "gate_${tag}.raw"
    normalize < "gate_${tag}.raw" > "gate_${tag}.norm"
    echo "    raw=$log -> gate_${tag}.raw / gate_${tag}.norm"
}

echo "=== AlphaCpuAgent Phase-1 dispatch gate ====================="
echo "  run-dir : $ROOT"
echo "  model   : $MODEL    max-cycles: $MAXCYC"
echo "============================================================="

run_one legacy   0
run_one dispatch 1

echo
echo "=== gate result ============================================="
if diff -u gate_legacy.norm gate_dispatch.norm > gate_diff.txt 2>&1; then
    echo "PASS: dispatcher boot is byte-identical to legacy (normalized)."
    echo "  StopReason (legacy)  : $(grep -iE 'stop|halt|exit' gate_legacy.raw   | tail -1)"
    echo "  StopReason (dispatch): $(grep -iE 'stop|halt|exit' gate_dispatch.raw | tail -1)"
    rm -f gate_diff.txt
    exit 0
else
    echo "FAIL: divergence found. Differences (normalized):"
    echo "  -> full diff in gate_diff.txt ($(wc -l < gate_diff.txt) lines)"
    head -40 gate_diff.txt
    echo
    echo "If the only diffs are NEW volatile tokens, add them to normalize()"
    echo "and re-run; otherwise the dispatch wiring diverged from legacy."
    exit 1
fi

# ============================================================================
# TAIL -- optional guest-console confirmation (full fidelity)
#   The host log gates the retire stream; to also diff the guest SRM console,
#   start a scripted client against TCP 10023 during each run, e.g.:
#     plink -raw -P 10023 127.0.0.1 < /dev/null > console_legacy.log &
#   then normalize+diff console_legacy.log vs console_dispatch.log the same way.
#   (DS20 idles at "wall-2", not >>>, so its console stream is short but still
#    a valid identity check.)
# ============================================================================
