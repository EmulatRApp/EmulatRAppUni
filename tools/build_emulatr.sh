#!/usr/bin/env bash
# ============================================================================
# build_emulatr.sh -- host-aware build of Emulatr, any config, trace-ready.
# Project: EmulatR -- Alpha AXP / EV6 (V4).  ASCII(128) only.  2026-07-02.
# ----------------------------------------------------------------------------
# Supports BOTH host toolchains from one entry point:
#   * MSVC (Windows, Git Bash) -- the build is IN-SOURCE at the project root
#     (VS 17 2022 generator; root has CMakeLists.txt + CMakeCache.txt).  Exe ->
#     <root>/<Config>/Emulatr.exe.  Toolchain via tools/vsenv.sh (vcvars).
#   * macOS (clang) -- out-of-source Ninja build, mirroring scripts/build_mac.sh
#     (Qt via aqt under ~/Qt).  Exe -> out/build/<mac-subdir>/Emulatr (bare).
#
# CONFIG (1st arg, default relwithdebinfo):
#   relwithdebinfo  -O2 -g -DNDEBUG ; diagnostics + trace hooks ON (recommended)
#   debug           full debug; instruments ON but SLOW cold boot
#   release         -O3 -DNDEBUG ; diagnostics + trace hooks COMPILED OUT (perf)
#
# EMULATR_DIAGNOSTIC_LOGGING is auto-ON for any non-Release config (GMEM-WATCH /
# IIC / SYSVAR watches).  -DEMULATR_TRACE_HOOKS=ON (added here for non-release)
# enables the per-commit retire callback so the windowed .trc actually emits.
#
# USAGE
#   ./tools/build_emulatr.sh                 # relwithdebinfo
#   ./tools/build_emulatr.sh debug
#   ./tools/build_emulatr.sh release
#   TARGET=Emulatr_tests ./tools/build_emulatr.sh
# ============================================================================
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$SELF_DIR/.." && pwd)"          # project root (has CMakeLists.txt)
TARGET="${TARGET:-Emulatr}"

# ---- config arg -> canonical config + CMake build-type string --------------
CFG_IN="${1:-relwithdebinfo}"
case "$(echo "$CFG_IN" | tr '[:upper:]' '[:lower:]')" in
    relwithdebinfo|rwdi|rel-with-deb-info) CONFIG=relwithdebinfo; BUILD_TYPE=RelWithDebInfo ;;
    debug|dbg)                             CONFIG=debug;          BUILD_TYPE=Debug ;;
    release|rel)                           CONFIG=release;        BUILD_TYPE=Release ;;
    *) echo "usage: $0 [relwithdebinfo|debug|release]"; exit 2 ;;
esac
# Trace hooks on for anything but release (release strips the diag layer anyway).
if [ "$CONFIG" = "release" ]; then HOOKS=OFF; else HOOKS=ON; fi

# ---- host detection --------------------------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST=win ;;
    Darwin)               HOST=mac ;;
    *)                    HOST=nix ;;   # Linux: treat like the mac clang branch
esac

echo "=== build_emulatr: host=$HOST config=$CONFIG ($BUILD_TYPE) hooks=$HOOKS target=$TARGET ==="

if [ "$HOST" = "win" ]; then
    # ---- MSVC / in-source VS build at the project root ---------------------
    if ! command -v cmake >/dev/null 2>&1 || ! command -v cl >/dev/null 2>&1; then
        # shellcheck disable=SC1091
        source "$SRC/tools/vsenv.sh"
    fi
    command -v cmake >/dev/null 2>&1 || { echo "FATAL: cmake not on PATH after vsenv.sh"; exit 1; }
    [[ -f "$SRC/CMakeCache.txt" ]] || {
        echo "FATAL: no CMakeCache.txt at $SRC -- root is not configured. One-time configure:"
        echo "   cmake -S \"$SRC\" -B \"$SRC\" -G \"Visual Studio 17 2022\" -A x64"
        exit 1
    }
    cd "$SRC"
    echo "--- reconfigure (reuses VS 17 2022 cache; EMULATR_TRACE_HOOKS=$HOOKS) ---"
    cmake -DEMULATR_TRACE_HOOKS="$HOOKS" .
    echo "--- build $TARGET ($BUILD_TYPE) ---"
    cmake --build . --config "$BUILD_TYPE" --target "$TARGET"
    EXE="$SRC/$BUILD_TYPE/Emulatr.exe"
else
    # ---- macOS / Linux clang, out-of-source Ninja (mirrors build_mac.sh) ---
    case "$BUILD_TYPE" in
        Release)        SUB="mac-release" ;;
        RelWithDebInfo) SUB="mac-debug" ;;      # legacy dir name, kept for parity
        Debug)          SUB="mac-debug-g" ;;
    esac
    BUILD="$SRC/out/build/$SUB"
    # Qt prefix (aqt layout: ~/Qt/<ver>/macos).  Same discovery build_mac.sh uses.
    QT_PREFIX="$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -1 || true)"
    QT_ARG=()
    if [ -n "${QT_PREFIX:-}" ] && [ -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        QT_ARG=(-DCMAKE_PREFIX_PATH="$QT_PREFIX"); echo "Using Qt: $QT_PREFIX"
    else
        echo "WARN: Qt6 not found under \$HOME/Qt/<ver>/macos -- relying on cached/prefix path"
    fi
    command -v ninja >/dev/null 2>&1 || echo "WARN: ninja not on PATH (brew install ninja)"
    echo "--- configure ($BUILD) EMULATR_TRACE_HOOKS=$HOOKS ---"
    cmake -S "$SRC" -B "$BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DEMULATR_TRACE_HOOKS="$HOOKS" \
        "${QT_ARG[@]}"
    JOBS="$( (command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu) || nproc 2>/dev/null || echo 4)"
    echo "--- build $TARGET ($BUILD_TYPE, -j$JOBS) ---"
    cmake --build "$BUILD" --target "$TARGET" -j"$JOBS"
    EXE="$BUILD/Emulatr"
fi

echo "=== done ==="
if [ -f "$EXE" ]; then
    echo "built: $EXE  ($(date -r "$EXE" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
else
    echo "NOTE: expected exe not found at $EXE -- check the build output above."
fi
