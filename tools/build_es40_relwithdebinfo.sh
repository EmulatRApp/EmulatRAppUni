#!/usr/bin/env bash
# ============================================================================
# build_es40_relwithdebinfo.sh -- turnkey RelWithDebInfo build of Emulatr.exe
# into the EXECUTION build dir (out/build/relwithdebinfo), with the diagnostic
# + retire-trace instruments compiled in.  Git Bash on Windows.  2026-07-02.
# ----------------------------------------------------------------------------
# WHY THIS EXISTS
#   The tree carries a Visual Studio 17 2022 CMake cache in out/build/
#   relwithdebinfo.  VS2022 "Open Folder" mode tries to reconfigure that dir
#   with Ninja and fails with a generator-mismatch error.  This script sidesteps
#   the GUI: it reuses the EXISTING VS cache (no generator change) and builds
#   from the command line, so the exe lands where the run scripts expect.
#
#   Config = RelWithDebInfo:
#     * EMULATR_DIAGNOSTIC_LOGGING is ON for any non-Release config -> the
#       GMEM-WATCH / IIC-TRACE / SYSVAR-watch instruments are compiled in.
#     * -DEMULATR_TRACE_HOOKS=ON adds the per-commit retire callback -> the
#       windowed .trc actually emits (off by default).
#
# USAGE
#   ./tools/build_es40_relwithdebinfo.sh              # trace hooks ON (default)
#   TRACE_HOOKS=OFF ./tools/build_es40_relwithdebinfo.sh   # skip the .trc hooks
#   TARGET=Emulatr_tests ./tools/build_es40_relwithdebinfo.sh   # build the tests
#
# NOTE: do NOT delete out/build/relwithdebinfo/CMakeCache.txt to "fix" the VS
#   Ninja error -- that discards the working VS build.  This script never does.
# ============================================================================
set -euo pipefail

# The CMake binary dir is the PROJECT ROOT itself (in-source build: root has
# CMakeLists.txt + CMakeCache.txt + Emulatr.sln, VS 17 2022 generator).  The
# out/build/<cfg> dirs are NOT CMake build dirs.  Build at root; exe lands in
# root/<Config>/Emulatr.exe.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJ_DIR}"
CONFIG="RelWithDebInfo"
TARGET="${TARGET:-Emulatr}"
: "${TRACE_HOOKS:=ON}"

[[ -d "$BUILD_DIR" ]] || { echo "FATAL: build dir not found: $BUILD_DIR"; exit 1; }
[[ -f "$BUILD_DIR/CMakeCache.txt" ]] || {
    echo "FATAL: no CMakeCache.txt in $BUILD_DIR -- this dir is not a configured CMake build."
    echo "       Configure it once with the VS generator, e.g. from that dir:"
    echo "         cmake -G \"Visual Studio 17 2022\" -A x64 ../../.."
    exit 1
}

# Put the MSVC toolchain (cl + cmake) on PATH in THIS shell.
if ! command -v cmake >/dev/null 2>&1 || ! command -v cl >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    source "$PROJ_DIR/tools/vsenv.sh"
fi
command -v cmake >/dev/null 2>&1 || { echo "FATAL: cmake not on PATH after vsenv.sh"; exit 1; }

# Sanity: refuse to run if the cache is not the VS generator (would mean a
# different binary dir than the run scripts assume).
GEN="$(grep -i '^CMAKE_GENERATOR:' "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2- || true)"
echo "build dir : $BUILD_DIR"
echo "generator : ${GEN:-<unknown>}"
echo "config    : $CONFIG"
echo "target    : $TARGET"
echo "trace hook: $TRACE_HOOKS  (EMULATR_TRACE_HOOKS)"
case "$GEN" in
    *Visual\ Studio*) : ;;
    *) echo "WARN: cache generator is not Visual Studio -- '--config $CONFIG' may be ignored (single-config generator)." ;;
esac

cd "$BUILD_DIR"

echo "=== reconfigure in place (reuses existing generator; EMULATR_TRACE_HOOKS=$TRACE_HOOKS) ==="
cmake -DEMULATR_TRACE_HOOKS="$TRACE_HOOKS" .

echo "=== build $TARGET ($CONFIG) ==="
cmake --build . --config "$CONFIG" --target "$TARGET"

echo "=== done ==="
# Report where the exe landed (run scripts expect it directly in $BUILD_DIR).
for cand in "$BUILD_DIR/Emulatr.exe" "$BUILD_DIR/$CONFIG/Emulatr.exe"; do
    if [[ -f "$cand" ]]; then
        echo "Emulatr.exe : $cand  (built $(stat -c '%y' "$cand" 2>/dev/null | cut -d. -f1))"
    fi
done
if [[ ! -f "$BUILD_DIR/Emulatr.exe" && -f "$BUILD_DIR/$CONFIG/Emulatr.exe" ]]; then
    echo "NOTE: exe is in the $CONFIG/ subfolder, not the build-dir root."
    echo "      The run scripts do ./Emulatr.exe in $BUILD_DIR -- pass RUN_DIR=$BUILD_DIR/$CONFIG"
    echo "      to the launcher, or check CMAKE_RUNTIME_OUTPUT_DIRECTORY."
fi
