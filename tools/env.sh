# tools/env.sh -- put the Visual Studio 2022 bundled CMake + Ninja on PATH for
# Git Bash / MINGW64 command-line builds of EmulatR.
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
# Contact: peert@envysys.com | https://envysys.com
#
# ninja.exe and cmake.exe ship INSIDE the VS install and are NOT on the default
# PATH, so a bare `cmake`/`ninja` from Git Bash fails to resolve.  SOURCE this
# (do not execute it) so the exports land in your current shell:
#
#     source tools/env.sh
#
# Idempotent: re-sourcing does not stack duplicate entries.  ASCII(128).

# Adjust VS_EDITION if you run Professional/Enterprise instead of Community.
VS_EDITION="${VS_EDITION:-Community}"
VS_CMAKE="/c/Program Files/Microsoft Visual Studio/2022/${VS_EDITION}/Common7/IDE/CommonExtensions/Microsoft/CMake"

if [ ! -x "${VS_CMAKE}/Ninja/ninja.exe" ]; then
    echo "env.sh: WARNING ninja.exe not found under ${VS_CMAKE}/Ninja" >&2
    echo "env.sh: set VS_EDITION (Community|Professional|Enterprise) or edit VS_CMAKE." >&2
fi

case ":${PATH}:" in
    *":${VS_CMAKE}/Ninja:"*) : ;;                                    # already on PATH
    *) export PATH="${VS_CMAKE}/CMake/bin:${VS_CMAKE}/Ninja:${PATH}" ;;
esac

# Qt: CMake needs CMAKE_PREFIX_PATH pointing at the Qt kit.  Auto-detect the
# newest <drive>:/Qt/<version>/msvc2022_64 unless QTDIR is already exported
# (override with:  QTDIR=/d/Qt/6.9.1/msvc2022_64 source tools/env.sh ).
if [ -z "${QTDIR:-}" ]; then
    for d in $(ls -d /c/Qt/*/msvc2022_64 /d/Qt/*/msvc2022_64 2>/dev/null | sort -V -r); do
        QTDIR="$d"; break
    done
fi
export QTDIR

# --- MSVC toolchain (cl.exe, INCLUDE, LIB) --------------------------------
# EmulatR is a C++20 MSVC build; a plain MINGW64 shell only has MinGW g++,
# which CMake will not drive for C++20 here.  Import the x64 MSVC environment
# from vcvars64.bat unless cl is already visible -- this is what lets a normal
# Git Bash configure/build WITHOUT opening the VS "x64 Native Tools" prompt.
# Only VS/SDK bin dirs are added to PATH (never System32), so MSYS coreutils
# (find, sort, ...) are not shadowed.
if ! command -v cl >/dev/null 2>&1; then
    VCVARS="/c/Program Files/Microsoft Visual Studio/2022/${VS_EDITION}/VC/Auxiliary/Build/vcvars64.bat"
    if [ -f "$VCVARS" ] && command -v cygpath >/dev/null 2>&1; then
        # Run vcvars in cmd, dump the resulting environment to a temp FILE, then
        # parse the file.  Each of these fixed a real failure hit while wiring
        # this up, and together they make it safe to auto-source from ~/.bashrc:
        #   * write to a file, NOT a pipe/process-substitution -- a piped
        #     `cmd //c` deadlocked and hung the shell.
        #   * </dev/null so cmd never blocks waiting on the shell's stdin.
        #   * timeout so a misbehaving cmd can never wedge an interactive shell.
        #   * do NOT set MSYS_NO_PATHCONV / ARG_CONV_EXCL here: it also stops
        #     MSYS rewriting the //c argument to /c, so cmd starts interactively
        #     and never runs the batch.  The vcvars path is already Windows-form
        #     (cygpath -w) and quoted, so MSYS leaves it alone anyway.
        #   * strip trailing CR -- cmd emits CRLF; a stray \r corrupts the
        #     INCLUDE / LIB / PATH values.
        _vcout="/tmp/env_vcvars_$$.txt"
        _vcbat="/tmp/env_vcvars_$$.bat"
        # Put the quoted vcvars path INSIDE a .bat: MSYS mangles embedded quotes
        # when handing a command STRING to native cmd (turns "x" into \"x\"), but
        # quotes read from a .bat file are parsed by cmd normally.
        { printf '@echo off\r\n'
          printf 'call "%s" >nul 2>&1\r\n' "$(cygpath -w "$VCVARS")"
          printf 'set\r\n'; } > "$_vcbat"
        _vcbatwin="$(cygpath -w "$_vcbat")"
        timeout 60 cmd //c "$_vcbatwin" </dev/null >"$_vcout" 2>/dev/null
        rm -f "$_vcbat"
        if [ -s "$_vcout" ]; then
            while IFS='=' read -r _k _v; do
                _v="${_v%$'\r'}"
                case "$_k" in
                    INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCINSTALLDIR|VSINSTALLDIR|WindowsSdkDir|WindowsSDKVersion|UCRTVersion)
                        export "$_k=$_v" ;;
                    PATH)
                        _add=""; _oldIFS=$IFS; IFS=';'
                        for _seg in $_v; do
                            case "$_seg" in
                                *"Microsoft Visual Studio"*|*"Windows Kits"*)
                                    _u=$(cygpath -u "$_seg" 2>/dev/null) && _add="${_add:+$_add:}$_u" ;;
                            esac
                        done
                        IFS=$_oldIFS
                        [ -n "$_add" ] && export PATH="${_add}:${PATH}" ;;
                esac
            done < "$_vcout"
        fi
        rm -f "$_vcout"
    elif [ ! -f "$VCVARS" ]; then
        echo "env.sh: WARNING vcvars64.bat not found: ${VCVARS}" >&2
        echo "env.sh: set VS_EDITION=Community|Professional|Enterprise to match your install." >&2
    fi
fi

echo "cmake: $(command -v cmake 2>/dev/null || echo 'NOT FOUND')"
echo "ninja: $(command -v ninja 2>/dev/null || echo 'NOT FOUND')"
echo "cl:    $(command -v cl 2>/dev/null || echo 'NOT FOUND')"
echo "QTDIR: ${QTDIR:-NOT FOUND}"
