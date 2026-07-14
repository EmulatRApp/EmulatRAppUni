#!/usr/bin/env bash
# ============================================================================
# commit_20260713_tig_reset.sh -- guarded end-of-session commit for the
# 2026-07-13 ES40 TIG-reset / micro-delay-warp / Cchip-cleanup / instrumentation
# work.  RUN THIS IN YOUR MINGW SHELL (the Cowork sandbox mount reports stale
# git metadata, so a commit from there stages nothing).
#
# Guards, in order:
#   1. abort if an index.lock exists (a live git in another shell).
#   2. stage ONLY the explicit files this session touched (never `git add -A`,
#      which would pick up the recurring FALSE staged deletion of traceLib/ +
#      tools/).
#   3. HARD ABORT if the staged set contains a DELETION of anything under
#      traceLib/ or tools/ (the known "trace library wiped" trap) -- prints how
#      to cancel it (`git add traceLib/ tools/`), commits nothing.
#
# Self-locates the repo root (EmulatRAppUniV4/Emulatr).  ASCII(128) only.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"        # tools/ -> Emulatr (repo root)
[[ -d "$REPO/.git" ]] || { echo "FATAL: $REPO is not a git repo root"; exit 1; }
cd "$REPO"

echo "=== repo: $REPO"
echo "=== branch: $(git rev-parse --abbrev-ref HEAD)"

# ---- guard 1: index.lock ---------------------------------------------------
if [[ -f .git/index.lock ]]; then
    echo "FATAL: .git/index.lock present -- another git is running (close it or"
    echo "       rm .git/index.lock if you are sure none is)."
    exit 1
fi

# ---- guard 2: stage ONLY this session's explicit files ---------------------
FILES=(
  chipsetLib/TsunamiTig.h
  chipsetLib/TsunamiChipset.h
  chipsetLib/TsunamiCchip.h
  systemLib/Machine.cpp
  palBoxLib/grains/PalEntries.cpp
  pipelineLib/PipelineDriver.h
  journals/20260713_es40_tig_reset_and_cchip_register_audit.md
  journals/20260713_es40_lfu_rscc_warp_instrumentation_spec.md
  tools/run_es40_rscc_ab_warp.sh
  tools/run_es40_rscc_ab_nowarp.sh
  tools/probe_es40_preseed_p00.sh
  tools/commit_20260713_tig_reset.sh
)
echo "=== staging ${#FILES[@]} explicit files..."
for f in "${FILES[@]}"; do
    if [[ -e "$f" ]]; then git add -- "$f"; else echo "  (skip missing: $f)"; fi
done

# ---- guard 3: refuse any staged DELETION under traceLib/ or tools/ ---------
DELS=$(git diff --cached --name-status | awk '$1 ~ /^D/ && ($2 ~ /^traceLib\// || $2 ~ /^tools\//) {print $2}')
if [[ -n "$DELS" ]]; then
    echo "FATAL: staged set contains DELETIONS under traceLib/ or tools/ -- this is"
    echo "       the known false-staged-deletion trap.  Cancel with:"
    echo "           git add traceLib/ tools/"
    echo "       then re-run.  Offending paths:"; echo "$DELS" | sed 's/^/         /'
    exit 1
fi

echo "=== staged summary:"; git diff --cached --stat

MSG="ES40: TIG module-reset prototype + micro-delay warp + Cchip dead-block cleanup + RSCC/warp/DIVERT-REI instrumentation (2026-07-13)

- TsunamiTig: co-gated reset-triad detect (ARM +0x280 bit2, COMMIT +0x600
  bits4/5) raises resetRequested; behind EMULATR_TIG_RESET, _PROVISIONAL.
- TsunamiChipset: tigResetRequested()/clearTigResetRequest().
- Machine::systemTick: apply HRM 12.1.1 module reset (chipset.reset() +
  resetToLoadedEntry()) at a clean tick boundary.
- TsunamiCchip: delete dead commented kDIM2=0x0500 offset block (live map in
  Tsunami21272_RegisterMap.h is correct vs HRM).
- PipelineDriver: EMULATR_UDELAYWARP (0x6a514 micro_delay warp) + RSCCDIAG-DELAY.
- Machine/PalEntries: WARPLEDGER + exact-pair DIVERT-REI ledger.
- journals + A/B run scripts.
All new runtime code gated (EMULATR_BRINGUP_PROBES + knobs), inert by default;
ES40-scoped by absence (DS10/DS20 issue zero of the triad). Regression gate
(suite + DS10 + DS20 -> P00) NOT yet run -- diagnostic/prototype checkpoint."

echo "=== committing..."
git commit -m "$MSG"

echo "=== pushing to origin $(git rev-parse --abbrev-ref HEAD)..."
git push origin "$(git rev-parse --abbrev-ref HEAD)"
echo "=== done."
