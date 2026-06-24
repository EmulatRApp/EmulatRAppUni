#!/usr/bin/env bash
# ============================================================================
# scripts/build_mac.sh -- configure + build EmulatR V4 on macOS (Intel)
# ============================================================================
# The Windows production build uses MSVC + vcpkg (C:/vcpkg) + the official Qt
# installer (D:/Qt); those paths are guarded behind if(WIN32) in CMakeLists so
# this host takes the portable branch.  Dependencies here:
#   - Compiler + macOS SDK : Xcode Command Line Tools  (NOT full Xcode)
#   - cmake + ninja        : brew install cmake ninja
#   - spdlog + fmt         : brew install spdlog fmt          (header-only)
#   - Qt 6.10.2            : aqtinstall, prebuilt, no Xcode:
#       python3 -m venv ~/.aqt-venv
#       ~/.aqt-venv/bin/pip install aqtinstall
#       ~/.aqt-venv/bin/aqt install-qt mac desktop 6.10.2 clang_64 --outputdir ~/Qt
# Homebrew's `qt` is intentionally NOT used: on this Tier-2 host it tries to
# source-build molten-vk, which requires full Xcode for a Vulkan shim the
# headless emulator never links.
# ============================================================================
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$SRC/out/build/mac-debug"

# Locate the aqt-installed Qt prefix (…/<ver>/macos).
QT_PREFIX="$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -1)"
if [ -z "${QT_PREFIX:-}" ] || [ ! -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
  echo "ERROR: Qt6 not found under \$HOME/Qt/<ver>/macos (see header for aqt install)" >&2
  exit 1
fi
echo "Using Qt: $QT_PREFIX"

cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX"

cmake --build "$BUILD" --target Emulatr Emulatr_tests -j"$(sysctl -n hw.ncpu)"

echo "Built: $BUILD/Emulatr , $BUILD/Emulatr_tests"
