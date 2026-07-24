#!/usr/bin/env bash
# tools/run_es40_hookb.sh -- run the ES40 boot from the CLI build and pull the
# ACVPROBE HOOKB lines (the memtest-ACV PTE verdict: valid/kre/pfn).
# Self-locating: run from ANY directory --
#     bash /d/EmulatR/EmulatRAppUniV4/Emulatr/tools/run_es40_hookb.sh
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
# Host: Windows / Git Bash (PC) is the supported path; the platform guard below
# stubs a macOS/Linux branch for the cross-platform owner.
# ASCII(128).

# ---- platform guard: PC (Windows/Git Bash) authoritative --------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;   # Git Bash / MSYS2 on Windows (PC)
    Darwin)               EMU_HOST=mac ;;   # macOS  (cross-platform owner)
    *)                    EMU_HOST=nix ;;   # Linux / other POSIX
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD="${REPO}/out/build/cli"

# binary: prefer .exe (Windows) then bare (mac/Linux).
if   [ -x "${BUILD}/Emulatr.exe" ]; then EXE="./Emulatr.exe"
elif [ -x "${BUILD}/Emulatr"     ]; then EXE="./Emulatr"
else
    echo "run: ${BUILD}/Emulatr(.exe) not found -- build first:  bash ${SCRIPT_DIR}/build_diag.sh" >&2
    exit 1
fi
cd "$BUILD" || exit 1

# firmware master is the repo-root firmware/; make sure the ES40 image is here.
mkdir -p firmware
[ -e firmware/es40_v7_3.exe ] || cp "${REPO}/firmware/es40_v7_3.exe" firmware/ 2>/dev/null
if [ ! -e firmware/es40_v7_3.exe ]; then
    echo "run: firmware/es40_v7_3.exe missing (build dir + ${REPO}/firmware) -- cannot run." >&2
    exit 1
fi

LOG="es40_hookb.log"
echo "run: launching ES40 to max-cycles (this takes a while)..."
EMULATR_VPTB_DIAG=1 "$EXE" --firmware firmware/es40_v7_3.exe --mem 4294967296 \
    --no-autoload --max-cycles 0x50000000  2> "$LOG"

echo "=== ACVPROBE HOOKB (count: $(grep -ac 'ACVPROBE HOOKB' "$LOG")) ==="
grep -a "ACVPROBE HOOKB" "$LOG" | head -60
echo "=== VPTB-DIAG: MTPR_VPTB writes vs va_ctl (count: $(grep -ac 'VPTB-DIAG' "$LOG")) ==="
grep -a "VPTB-DIAG" "$LOG" | head -20
echo "=== MEMDIAG-MTPR: MMU-ctl IPR writes w/ vptbHint (count: $(grep -ac 'MEMDIAG-MTPR' "$LOG")) ==="
grep -a "MEMDIAG-MTPR" "$LOG" | head -40
echo "=== full log: ${BUILD}/${LOG} ==="
