#!/usr/bin/env bash
# ============================================================================
# tools/vsenv.sh -- put the native build toolchain (compiler + cmake + ninja)
# on PATH in the CURRENT bash, identically on Windows and macOS/Linux.
#
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# ============================================================================
#
# >>> SOURCE IT, do not execute <<<   (it must mutate the CURRENT shell)
#       source tools/vsenv.sh
#       ./tools/build_test_run.sh
#
#   Running it as ./vsenv.sh starts a subshell and the environment is lost
#   the moment it exits -- you'll see the toolchain "not ready" afterward.
#
# WHAT IT DOES
#   Windows (MINGW/MSYS bash): finds Visual Studio via vswhere, runs
#       vcvars64.bat through a generated temp .bat (avoids MSYS quote
#       mangling), and imports the resulting environment (CRLF stripped,
#       PATH converted to Unix form) into this shell.  Adds VS's bundled
#       CMake/Ninja if vcvars did not.
#   macOS/Linux: no-op beyond a warning if cmake is missing
#       (Homebrew: brew install cmake ninja qt).
#   Idempotent: if cl + cmake are already present it just reports.
# ============================================================================

_vsenv_have_cmake() { command -v cmake >/dev/null 2>&1; }

case "$(uname -s 2>/dev/null)" in
  MINGW*|MSYS*|CYGWIN*)
    if command -v cl >/dev/null 2>&1 && _vsenv_have_cmake; then
      echo "[vsenv] MSVC + cmake already on PATH -- nothing to do"
    else
      _vsenv_vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
      if [ ! -x "$_vsenv_vswhere" ]; then
        echo "[vsenv] ERROR: vswhere.exe not found -- is Visual Studio installed?" >&2
        return 1 2>/dev/null || exit 1
      fi
      _vsenv_root="$("$_vsenv_vswhere" -latest -property installationPath 2>/dev/null)"
      if [ -z "$_vsenv_root" ]; then
        echo "[vsenv] ERROR: no Visual Studio installation reported by vswhere" >&2
        return 1 2>/dev/null || exit 1
      fi
      _vsenv_root_u="$(cygpath -u "$_vsenv_root")"
      _vsenv_vcvars_u="$_vsenv_root_u/VC/Auxiliary/Build/vcvars64.bat"
      if [ ! -f "$_vsenv_vcvars_u" ]; then
        echo "[vsenv] ERROR: vcvars64.bat not found at $_vsenv_vcvars_u" >&2
        return 1 2>/dev/null || exit 1
      fi
      echo "[vsenv] loading MSVC x64 env from: $_vsenv_root"

      # Generate a temp .bat: call vcvars, then dump the environment.  Passing
      # a single .bat path through cmd //c avoids MSYS mangling the quotes.
      _vsenv_bat="$(mktemp).bat"
      printf '@echo off\r\ncall "%s" >nul 2>&1\r\nset\r\n' \
             "$(cygpath -w "$_vsenv_vcvars_u")" > "$_vsenv_bat"
      _vsenv_dump="$(mktemp)"
      cmd //c "$(cygpath -w "$_vsenv_bat")" > "$_vsenv_dump" 2>/dev/null
      rm -f "$_vsenv_bat"

      if [ ! -s "$_vsenv_dump" ]; then
        echo "[vsenv] ERROR: environment dump was empty -- vcvars64.bat did not run" >&2
        rm -f "$_vsenv_dump"
        return 1 2>/dev/null || exit 1
      fi

      # Import KEY=VALUE pairs (strip trailing CR; skip names bash can't export).
      while IFS='=' read -r _k _v; do
        _v="${_v%$'\r'}"
        [ -z "$_k" ] && continue
        case "$_k" in
          PATH|Path) export PATH="$(cygpath -u -p "$_v")"; continue ;;
          *[!A-Za-z0-9_]*) continue ;;   # e.g. ProgramFiles(x86) -- not a valid bash name
        esac
        export "$_k=$_v"
      done < "$_vsenv_dump"
      rm -f "$_vsenv_dump"

      # vcvars may not add VS's bundled CMake/Ninja -- add them if still missing.
      if ! _vsenv_have_cmake; then
        for _d in \
          "$_vsenv_root_u/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin" \
          "$_vsenv_root_u/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"; do
          [ -d "$_d" ] && PATH="$_d:$PATH"
        done
        export PATH
      fi
      unset _vsenv_vswhere _vsenv_root _vsenv_root_u _vsenv_vcvars_u _vsenv_bat _vsenv_dump _k _v _d
    fi
    ;;

  Darwin|Linux)
    _vsenv_have_cmake || echo "[vsenv] WARNING: cmake not on PATH (try: brew install cmake ninja qt)" >&2
    ;;

  *)
    echo "[vsenv] WARNING: unrecognized OS '$(uname -s 2>/dev/null)'; assuming toolchain already on PATH" >&2
    ;;
esac

if _vsenv_have_cmake; then
  echo "[vsenv] cmake : $(command -v cmake)  ($(cmake --version 2>/dev/null | head -1))"
  command -v ninja >/dev/null 2>&1 && echo "[vsenv] ninja : $(command -v ninja)"
  command -v cl    >/dev/null 2>&1 && echo "[vsenv] cl    : present (MSVC x64)"
else
  echo "[vsenv] cmake still not found -- toolchain NOT ready" >&2
fi

unset -f _vsenv_have_cmake 2>/dev/null || true
