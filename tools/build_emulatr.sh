#!/usr/bin/env bash
# ============================================================================
# build_emulatr.sh -- host-aware build of Emulatr, any config, trace-ready.
# Project: EmulatR -- Alpha AXP / EV6 (V4).  ASCII(128) only.  2026-07-02.
# ----------------------------------------------------------------------------
# Both hosts converge on the project run convention (CLAUDE.md "Build & run
# conventions"): the launch artifact is out/build/<config>/Emulatr[.exe],
# <config> = release|relwithdebinfo|debug.
#   * MSVC (Windows, Git Bash) -- build stays IN-SOURCE at the project root
#     (VS 17 2022 generator; root has CMakeLists.txt + CMakeCache.txt), then the
#     exe + firmware/ are MIRRORED into out/build/<config> (non-destructive to
#     the VS/IDE cache).  Toolchain via tools/vsenv.sh (vcvars).
#   * macOS/Linux (clang) -- out-of-source Ninja build DIRECTLY into
#     out/build/<config> (Qt via aqt under ~/Qt).  Exe -> out/build/<config>/Emulatr.
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
    # Conform to the run convention WITHOUT disturbing the in-source VS cache/
    # IDE workflow: the in-source <root>/<Config>/ is a COMPLETE runnable dir --
    # Emulatr.exe PLUS its Qt6*.dll / runtime deps and the firmware/ tree that
    # CMake stages beside it. Mirror the WHOLE directory into out/build/<config>
    # so the launch path out/build/<config>/Emulatr.exe finds every DLL it loads
    # next to it (mirroring only the exe leaves it unable to load Qt6Core.dll).
    OUT="$SRC/out/build/$CONFIG"
    mkdir -p "$OUT"
    # Mirror runtime deps (exe, Qt6*.dll, icuuc.dll, config/, tls/,
    # networkinformation/, firmware/, *_platform.json, *.rom) -- and editors/,
    # the PlatformEditor payload (platedit_qt/platedit_tui + their Qt DLLs +
    # schema/ + catalog/ + webui/), which CMake now links/stages DIRECTLY into
    # <root>/<Config>/editors, so this one loop carries it: no special case.
    # SKIP the run-OUTPUT dirs -- traces/ alone can be 100+ GB; logs/ and
    # snapshots/ are also emitted at run time, not build inputs. Copying them
    # would be catastrophic.
    for item in "$SRC/$BUILD_TYPE"/*; do
        case "$(basename "$item")" in
            traces|logs|snapshots) continue ;;
            *) cp -rf "$item" "$OUT/" ;;
        esac
    done
    # NOTE(2026-07-25): the old <root>/editors/<Config> -> $OUT/editors copy is
    # GONE with the layout fix (run layout is <config>/editors).  Do not restore
    # it: that source-tree path is now stale build output at best, and copying it
    # AFTER the loop above would overwrite the fresh editors/ with old binaries.
    if [ -d "$SRC/editors/$BUILD_TYPE" ]; then
        echo "WARN: stale legacy output dir $SRC/editors/$BUILD_TYPE exists (pre-2026-07-25 layout) -- not mirrored; delete it."
    fi
    EXE="$OUT/$TARGET.exe"
else
    # ---- macOS / Linux clang, out-of-source Ninja (mirrors build_mac.sh) ---
    # Root the build at the project run convention: out/build/<config>,
    # <config> = release|relwithdebinfo|debug (see CLAUDE.md "Build & run
    # conventions"). CMake copies firmware/ into the build tree, so the launch
    # path is out/build/<config>/Emulatr with firmware/ alongside.
    BUILD="$SRC/out/build/$CONFIG"
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
    EXE="$BUILD/$TARGET"
fi

# ---- H&M version sync (badge <-> claudeRV4.hmxp, 2026-08-04) ---------------
# Mirrors project(Emulatr VERSION x.y.z) into the H&M config-values
# versionmajor/minor/build.  Sanctioned narrow write (see sync_hm_version.py
# header).  WARN-loud but NEVER fails the emulator build: exit 3 = project open
# in Help & Manual (skip, retry after closing), exit 4 = structural error.
PYBIN="$(command -v python3 || command -v python || echo /c/Users/tim/AppData/Local/Programs/Python/Python313/python)"
if "$PYBIN" "$SRC/tools/sync_hm_version.py"; then
    :
else
    echo "WARN: H&M version sync did not complete (rc=$?) -- see lines above; build continues."
fi

echo "=== done ==="
if [ -f "$EXE" ]; then
    echo "built: $EXE  ($(date -r "$EXE" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
else
    echo "NOTE: expected exe not found at $EXE -- check the build output above."
fi
