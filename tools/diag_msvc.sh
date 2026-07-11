#!/usr/bin/env bash
# tools/diag_msvc.sh -- diagnose why vcvars64.bat is not populating cl / INCLUDE
# in Git Bash.  RUN it (do not source):  bash tools/diag_msvc.sh
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
# ASCII(128).

VS_EDITION="${VS_EDITION:-Community}"
BASE="/c/Program Files/Microsoft Visual Studio/2022/${VS_EDITION}/VC/Auxiliary/Build"
VCVARS="${BASE}/vcvars64.bat"

echo "=== MSVC env diagnostic (VS_EDITION=${VS_EDITION}) ==="

if [ -f "$VCVARS" ]; then echo "[1] vcvars64.bat: PRESENT ($VCVARS)"; else echo "[1] vcvars64.bat: MISSING ($VCVARS)"; fi
if command -v cygpath >/dev/null 2>&1; then echo "[2] cygpath: PRESENT"; else echo "[2] cygpath: MISSING"; fi
if command -v cmd >/dev/null 2>&1; then echo "[3] cmd: PRESENT"; else echo "[3] cmd: MISSING"; fi

if [ -f "$VCVARS" ] && command -v cygpath >/dev/null 2>&1; then
    WIN=$(cygpath -w "$VCVARS")
    echo "[4] windows path: $WIN"

    # Run vcvars from a temp .bat.  Quotes inside a file are parsed by cmd
    # normally; quotes embedded in a cmd command line get mangled by MSYS.
    BAT="/tmp/diag_vcv_$$.bat"
    printf '@echo off\r\n'         >  "$BAT"
    printf 'call "%s"\r\n' "$WIN"  >> "$BAT"
    printf 'set\r\n'              >> "$BAT"
    BATWIN=$(cygpath -w "$BAT")
    OUT=$(timeout 60 cmd //c "$BATWIN" </dev/null 2>&1)
    rm -f "$BAT"

    NINC=$(printf '%s\n' "$OUT" | grep -c '^INCLUDE=')
    echo "[5] INCLUDE lines vcvars produced: $NINC   (0 = did not run; nonzero = works)"
    echo "[6] raw tail:"
    printf '%s\n' "$OUT" | tail -6 | sed 's/^/      /'
fi
echo "=== end ==="
