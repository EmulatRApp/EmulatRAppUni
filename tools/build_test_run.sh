#!/usr/bin/env bash
# ============================================================================
# tools/build_test_run.sh -- rebuild (RelWithDebInfo), run the chipset
# halt-register doctest, then hand off to launch_vms_boot.sh.  Deployed to
# <run-dir>/tools/ by CMake POST_BUILD.  Requires MSVC tools (cl/ninja) on
# PATH -- run from a VS Developer Git Bash, or build in the IDE then run
# tools/launch_vms_boot.sh directly.
# ============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"     # run dir == ninja build dir
TESTS_EXE="Emulatr_tests.exe"
cd "$BUILD_DIR"
command -v cmake >/dev/null 2>&1 || {
  echo "FATAL: cmake not on PATH. Run from a VS Developer Git Bash, or build" >&2
  echo "       in the IDE then run tools/launch_vms_boot.sh." >&2; exit 1; }
echo "[build] cmake --build $BUILD_DIR"
cmake --build "$BUILD_DIR"
[[ -x "$TESTS_EXE" ]] || { echo "FATAL: $TESTS_EXE not found after build" >&2; exit 1; }
echo "[test] TIG subcase (TsunamiChipset ISystemBus arbiter)"
./"$TESTS_EXE" --test-case="*ISystemBus arbiter*"
echo "[test] PASSED"
echo "[run] handing off to tools/launch_vms_boot.sh"
exec "$SCRIPT_DIR/launch_vms_boot.sh"
