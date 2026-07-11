#!/usr/bin/env bash
# tools/build_diag.sh -- one-command command-line build of Emulatr.exe with the
# EMULATR_BRINGUP_PROBES scaffolding ON (Hook B, VPTB-DIAG, ...), outside the VS
# IDE.  Self-locating: run from ANY directory --
#     bash /d/EmulatR/EmulatRAppUniV4/Emulatr/tools/build_diag.sh
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
# ASCII(128).

# Resolve the repo root from this script's own location -- never assume cwd.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$REPO" || { echo "build_diag: cannot cd to repo root: $REPO" >&2; exit 1; }
echo "build_diag: repo   = $REPO"

# Toolchain env (cmake, ninja, cl, QTDIR).  Sourced so its exports land here.
# shellcheck source=/dev/null
source "${SCRIPT_DIR}/env.sh"

if ! command -v cl >/dev/null 2>&1; then
    echo "build_diag: ERROR cl (MSVC) not on PATH; env.sh did not import the toolchain." >&2
    echo "build_diag: diagnose with:  bash ${SCRIPT_DIR}/diag_msvc.sh" >&2
    exit 1
fi

BUILD="${REPO}/out/build/cli"
echo "build_diag: build  = $BUILD (fresh)"
rm -rf "$BUILD"

cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$QTDIR" \
    -DEMULATR_BRINGUP_PROBES=ON \
    -DEMULATR_IRQDIAG=OFF
if [ $? -ne 0 ]; then echo "build_diag: CONFIGURE FAILED" >&2; exit 1; fi

cmake --build "$BUILD" --target Emulatr -j
if [ $? -ne 0 ]; then echo "build_diag: BUILD FAILED (see compiler errors above)" >&2; exit 1; fi

echo "=== probe check ==="
if command -v strings >/dev/null 2>&1 && strings "${BUILD}/Emulatr.exe" | grep -q "ACVPROBE HOOKB"; then
    echo "build_diag: OK   ACVPROBE HOOKB present in Emulatr.exe"
else
    echo "build_diag: WARN ACVPROBE HOOKB not found in the binary (BRINGUP_PROBES may not have compiled)"
fi
echo "build_diag: done -> ${BUILD}/Emulatr.exe"
