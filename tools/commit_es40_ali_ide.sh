#!/usr/bin/env bash
# ============================================================================
# commit_es40_ali_ide.sh -- safe commit+push of the ES40 ALi M1543C/M5229
# south-bridge fidelity work (+ accumulated session changes).
#
# The working index keeps acquiring corruption (stale index.lock; phantom
# staged-add "AD" entries and unmerged "UU" entries with control-char names;
# earlier: ~60 FALSE staged deletions of traceLib/ + tools/).  This script:
#   1. clears any stale index.lock,
#   2. rebuilds the index from HEAD (git reset --mixed HEAD) -- cancels the
#      phantom/false-deletion index state WITHOUT touching any working file,
#   3. stages tracked modifications (git add -u) + today's NEW files explicitly
#      (so untracked junk like control-char-named files is NOT committed),
#   4. HARD-GUARDS: aborts if any traceLib/ or tools/ file is staged for delete,
#   5. shows the staged set for review, then commits + pushes.
# Run from Git Bash / MINGW at the repo (self-locates).  ASCII(128) only.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"      # tools/ -> repo root (EmulatRAppUniV4/Emulatr)
cd "$REPO"
[[ -d .git ]] || { echo "FATAL: no .git at $REPO"; exit 1; }
echo "repo = $REPO"

# 1. stale lock
if [[ -f .git/index.lock ]]; then echo "removing stale .git/index.lock"; rm -f .git/index.lock; fi

# 2. rebuild index from HEAD (working tree untouched) -> clears phantom AD/UU/
#    false-deletion entries
echo "rebuilding index from HEAD (no working file touched)..."
git reset --mixed HEAD >/dev/null

# 3a. stage modifications/renames of TRACKED files only (skips untracked junk)
git add -u

# 3b. stage today's NEW source + tools + journal (untracked -> explicit)
NEW=(
  deviceLib/Tsunami/PciConfigSpace.h
  deviceLib/Tsunami/AtaTaskfileEngine.h
  deviceLib/Tsunami/ITsunamiIde.h
  deviceLib/Tsunami/AliM5229Ide.h
  tools/run_ds20_showdev.sh
  tools/run_es40_showdev.sh
  journals/20260712_es40_ali_m5229_ide_faithful_spec.md
)
for f in "${NEW[@]}"; do [[ -f "$f" ]] && git add -- "$f"; done

# 4. HARD GUARD: never let a traceLib/ or tools/ SOURCE deletion slip in
if git diff --cached --name-status --diff-filter=D | grep -E "^D[[:space:]]+(traceLib/|tools/)"; then
    echo "FATAL: a traceLib/ or tools/ file is staged for DELETION -- unstaging + aborting."
    git reset -q HEAD
    exit 1
fi

# 5. review
echo "==================== staged for commit ===================="
git status --short
echo "==========================================================="

# 6. commit + push
git commit -m "ES40 ALi M1543C/M5229 south-bridge fidelity (+ session work)

- Model-keyed SouthBridge lever (TsunamiVariant.h southBridgeFromModel);
  retire isAliPlatform; Machine.cpp manifest reconciliation warn; es40/es45/
  ds25 manifests declare ALi so PlatCap::SbAli derives honestly.
- B-comp IDE: shared AtaTaskfileEngine + PciConfigSpace; ITsunamiIde iface;
  new AliM5229Ide (M5229 identity); Cy82C693Ide refactored thin.  wireDevices
  selects the controller on m_southBridge.
- RESULT: ES40 console now recognizes 'Acer Labs M1543C IDE' (both channels
  dqa0/dqb0 in show config) -- Phase-A console fidelity.  show dev enumeration
  needs native-mode datapath (relocatable BARs + BAR I/O decode + BMIDE),
  deferred _PROVISIONAL -> TODO(ali-ide-dma-irq) (Phase B).  Separate track:
  'init' console re-decompression NXM (not IDE).
- tools: run_{ds20,es40}_showdev.sh (build-stamp + RUN_DIR_OVERRIDE).
- Spec: journals/20260712_es40_ali_m5229_ide_faithful_spec.md."

echo "pushing..."
git push
echo "DONE.  Committed + pushed."
