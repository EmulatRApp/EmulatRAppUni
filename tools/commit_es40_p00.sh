#!/usr/bin/env bash
# ============================================================================
# commit_es40_p00.sh -- stage + commit + push the ES40 "boots to P00>>>" work.
# Project: EmulatR (Alpha 21264/EV6).  ASCII(128) only.  2026-07-11.
# Run from anywhere (self-locating): bash tools/commit_es40_p00.sh
# Safe by design: cancels the known false-staged-deletion trap (traceLib/,
# tools/vsenv.sh) before staging, shows status, and PAUSES for your y/N before
# it commits or pushes.
# ============================================================================
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SELF_DIR" && git rev-parse --show-toplevel 2>/dev/null || true)"
[ -n "$ROOT" ] || { echo "FATAL: not inside a git work tree (git rev-parse failed)."; exit 1; }
cd "$ROOT"
echo "repo root : $ROOT"
echo "branch    : $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"

# --- preflight: catch a corrupt/incomplete object store early ---------------
if ! git status --short >/dev/null 2>&1; then
    echo "FATAL: 'git status' failed (possible corrupt/locked index or object)."
    echo "       Try:  git fsck --full   and clear any stale .git/index.lock, then rerun."
    exit 1
fi

# --- cancel the false-staged-deletion trap ----------------------------------
# traceLib/ and tools/vsenv.sh exist on disk but git has (historically) staged
# their deletion.  Re-add them (only if present) so they are NOT wiped.
for p in traceLib tools/vsenv.sh; do
    if [ -e "$p" ]; then git add -A -- "$p" 2>/dev/null || true; fi
done

# --- stage everything (adds existing files back, cancels bogus deletions) ----
git add -A

echo
echo "=== staged changes (review) ============================================"
git status --short
echo "========================================================================"
echo "Deletions still staged (should be NONE unless truly deleted):"
git diff --cached --name-status | grep -E '^D' || echo "  (none)"
echo

read -r -p "Proceed to COMMIT these changes? [y/N] " ans
case "$ans" in [yY]|[yY][eE][sS]) ;; *) echo "Aborted (nothing committed)."; exit 0;; esac

git commit -F - <<'MSG'
ES40 SRM: implement CSERVE 0x66 = masked PAL_BASE -- boots to P00>>>

Root-caused the surviving ES40 powerup-memtest access violation (native
LDQ at guest 0x1B7DD4, VA 0xFFFFFFFF7F827F5F) to CSERVE 0x66 being a no-op.
Disassembly of the running image's OWN PAL shows sys__cserve dispatches
r16=0x66 to a handler that reads PAL_BASE (HW_MFPR index 0x10 per 21264ev67
HRM Fig 6-4) and clears the low 21 bits.  The memtest helper at guest 0x8C2D0
computes arg - cserve(0x66); the no-op left a stale walk pointer in R0,
producing the wild VA that was dereferenced -> ACV.

palBoxLib/grains/PalEntries.cpp: execCserve case 0x66 returns
(cpu.palBase >> 21 << 21), matching the PAL handler; deterministic, reuses
the HW_PAL_BASE source.  Does not reprise the 2026-07-08 SCB regression (that
returned a BCD TOY, not palBase).

pipelineLib/PipelineDriver.h: removed the temporary one-shot trace-window arm
(capture complete).
tools/run_srm_trace_full.sh: ARM=cyc gated-window mode + NOTRACE decoupling.
journals/: full diagnosis record (AAR-ASIZ context, extensive gated trace,
machine-code PAL confirmation, and the RESULT: ES40 reaches P00>>>).

Verified: ES40 cold boot reaches the SRM console prompt (show config
enumerates chipset + 4x1024Mb 4-Way = 4096 MB); DS10 and DS20 still reach
P00>>> (do-no-harm).
MSG

echo
echo "=== commit created ====================================================="
git log --oneline -1
echo

read -r -p "Push to origin now (git push)? [y/N] " ans2
case "$ans2" in [yY]|[yY][eE][sS]) ;; *) echo "Committed but NOT pushed. Push later with: git push"; exit 0;; esac

git push || git push origin HEAD
echo "=== pushed. Done. ======================================================"
