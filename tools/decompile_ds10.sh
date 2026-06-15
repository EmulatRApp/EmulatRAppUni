#!/usr/bin/env bash
# ============================================================================
# decompile_ds10.sh -- dump C source from an ALREADY-ANALYZED Ghidra project.
# ============================================================================
# Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (tooling)
#
# Analysis is done in the Ghidra GUI; this script ONLY runs the decompile
# post-script against the saved program (no import, no analysis).
#
# BEFORE RUNNING:
#   1. Let the GUI auto-analysis finish.
#   2. Save the program (Ctrl+S) so the functions are on disk.
#   3. CLOSE the project in the GUI -- headless needs the project lock.
#
# Output: <OUT_DIR>/decompressed_ds10_v7_3.bin.c  (+ .index.txt)
# Full headless log: D:\EmulatR\traces\decompile.log
# ============================================================================

# ---- Config (only GHIDRA is likely to need editing) -----------------------
GHIDRA="${GHIDRA:-/m/Ghidra/ghidra_11.3.1_PUBLIC}"
# IMPORTANT: the first headless run failed with "osgi.ee=UNKNOWN" -- Ghidra's
# script compiler could not resolve the Java execution environment because it
# ran on JDK 26 (too new), so DecompileToDir.java never compiled.  Pin a JDK
# Ghidra recognizes (JDK 21 is the official choice for Ghidra 11.3.x).
# Verify this path; adjust if your jdk-21 lives elsewhere.
export JAVA_HOME="/c/Program Files/Java/jdk-21.0.11"

PROJECT_DIR="D:/EmulatR/EmulatRAppUniV4/ghidra"
PROGRAM="decompressed_ds10_v7_3.bin"
SCRIPT_PATH="D:/EmulatR/EmulatRAppUniV4/Emulatr/tools/ghidra_scripts"
OUT_DIR="D:/EmulatR/EmulatRAppUniV4/Emulatr/tools/host_decompressor/out/decompiled_src"
LOG="D:/EmulatR/traces/decompile.log"
# ---------------------------------------------------------------------------

HEADLESS="$GHIDRA/support/analyzeHeadless.bat"
[ -f "$HEADLESS" ] || HEADLESS="$GHIDRA/support/analyzeHeadless"

# Auto-detect the project (.gpr) name in PROJECT_DIR so we need not hardcode it.
gpr=$(ls "$PROJECT_DIR"/*.gpr 2>/dev/null | head -1)
if [ -z "$gpr" ]; then
  echo "ERROR: no .gpr project found in $PROJECT_DIR"
  echo "       Create + analyze the program in the Ghidra GUI first."
  exit 1
fi
PROJECT_NAME=$(basename "$gpr" .gpr)

echo "=============================================================="
echo " Ghidra   : $HEADLESS"
echo " Project  : $PROJECT_DIR  ($PROJECT_NAME)"
echo " Program  : $PROGRAM"
echo " Out dir  : $OUT_DIR"
echo " Log      : $LOG"
echo "=============================================================="
echo " NOTE: close the project in the Ghidra GUI before running"
echo "       (headless needs the lock) and Save the analysis first."
echo

if [ ! -f "$HEADLESS" ]; then
  echo "ERROR: analyzeHeadless not found at '$HEADLESS' -- set GHIDRA."
  exit 1
fi
if [ ! -f "$SCRIPT_PATH/DecompileToDir.java" ]; then
  echo "ERROR: DecompileToDir.java not found in $SCRIPT_PATH"
  exit 1
fi
mkdir -p "$OUT_DIR"

# Decompile-only: reuse the GUI-analyzed program, no re-analysis.  Output dir
# is NOT passed as a script arg (git-bash mangles path args); DecompileToDir
# has the correct path hardcoded as its default.
"$HEADLESS" "$PROJECT_DIR" "$PROJECT_NAME" \
  -process "$PROGRAM" -noanalysis \
  -scriptPath "$SCRIPT_PATH" \
  -postScript DecompileToDir.java 2>&1 | tee "$LOG"
rc=${PIPESTATUS[0]}

echo
if [ "$rc" -eq 0 ]; then
  echo "DONE (rc=0).  Verify the writer ran:"
  echo "    grep DecompileToDir \"$LOG\""
  echo "  expected source: $OUT_DIR/$PROGRAM.c"
else
  echo "analyzeHeadless exited rc=$rc."
  echo "If the log says the project is LOCKED, close the Ghidra GUI and retry."
fi
exit "$rc"
