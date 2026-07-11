#!/usr/bin/env bash
# ============================================================================
# tools/verify_aar_fix.sh -- build + do-no-harm verification for the Cchip AAR
# ASIZ decode-width fix (ES40 memtest ACV root cause, task #6).
# Project: EmulatR -- Alpha AXP / EV6 (V4).  ASCII(128) only.
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
# Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
# ----------------------------------------------------------------------------
# SELF-LOCATING: run from ANY directory (does not assume cwd) --
#     bash /d/EmulatR/EmulatRAppUniV4/Emulatr/tools/verify_aar_fix.sh
#
# Phases (all ON by default; skip individually via env):
#   1 BUILD  Emulatr + Emulatr_tests  (via tools/build_emulatr.sh)   SKIP_BUILD=1
#   2 TEST   Emulatr_tests: AAR/ASIZ subset THEN full suite          SKIP_TEST=1
#   3 BOOT   cold-boot DS10, DS20, ES40 headless + capped, grep      SKIP_BOOT=1
#            (do-no-harm: DS10/DS20 must still reach P00>>>; ES40 must
#             run PAST the old memtest ACV at cyc ~282M)
#
# ENV KNOBS
#   CONFIG   relwithdebinfo (default) | debug | release
#   MODELS   space list, default "ds10 ds20 es40"
#   MAXCYC   per-model cycle cap (default 0x60000000 ~ 1.6B: enough to reach the
#            SRM console P00>>> and, for ES40, well past the old ACV point)
#   TESTFILTER  doctest filter for the highlighted subset (default AAR/ASIZ/tiling)
#
# NOTE: the boot phase runs each model to MAXCYC in the FOREGROUND -- a full
# cold boot is minutes per model.  For a quick AAR-only signal use SKIP_BOOT=1
# (the doctests deterministically pin the 4x1GB tiling); the boots are the
# integration confirmation.
# ============================================================================
# NOT set -e: run every phase and report a consolidated verdict, never abort mid-run.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${CONFIG:-relwithdebinfo}"
MODELS="${MODELS:-ds10 ds20 es40}"
MAXCYC="${MAXCYC:-0x60000000}"
TESTFILTER="${TESTFILTER:-*AAR*,*ASIZ*,*Typhoon*,*Tsunami*,*M2*,*M3*,*ISystemBus*}"

case "$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')" in
    relwithdebinfo|rwdi) CONFIG=relwithdebinfo; VSCFG=RelWithDebInfo ;;
    debug|dbg)           CONFIG=debug;          VSCFG=Debug ;;
    release|rel)         CONFIG=release;        VSCFG=Release ;;
    *) echo "verify_aar_fix: bad CONFIG '$CONFIG'"; exit 2 ;;
esac

pass_build=SKIP; pass_test_aar=SKIP; pass_test_full=SKIP
declare -A boot_verdict

hr(){ printf '%.0s=' $(seq 1 74); echo; }

# ---- Phase 1: BUILD --------------------------------------------------------
if [ "${SKIP_BUILD:-0}" != "1" ]; then
    hr; echo "PHASE 1  BUILD ($CONFIG): Emulatr + Emulatr_tests"; hr
    TARGET=Emulatr       bash "$SCRIPT_DIR/build_emulatr.sh" "$CONFIG"; rc_e=$?
    TARGET=Emulatr_tests bash "$SCRIPT_DIR/build_emulatr.sh" "$CONFIG"; rc_t=$?
    if [ "$rc_e" -eq 0 ] && [ "$rc_t" -eq 0 ]; then pass_build=PASS; else pass_build=FAIL; fi
    echo "  Emulatr rc=$rc_e   Emulatr_tests rc=$rc_t   -> BUILD $pass_build"
    if [ "$pass_build" = FAIL ]; then
        echo "verify_aar_fix: BUILD FAILED -- fix compiler errors above before test/boot."
    fi
fi

# ---- locate the test binary (host/config tolerant) -------------------------
find_tests() {
    local c
    for c in \
        "$REPO/out/build/$CONFIG/Emulatr_tests.exe" \
        "$REPO/$VSCFG/Emulatr_tests.exe" \
        "$REPO/out/build/cli/Emulatr_tests.exe" \
        "$REPO/out/build/$CONFIG/Emulatr_tests" \
        "$REPO/out/build/mac-debug/Emulatr_tests" \
        "$REPO/out/build/mac-release/Emulatr_tests"; do
        [ -x "$c" ] && { printf '%s' "$c"; return 0; }
    done
    return 1
}

# ---- Phase 2: TEST (emulatrTest / Emulatr_tests) ---------------------------
if [ "${SKIP_TEST:-0}" != "1" ] && [ "$pass_build" != FAIL ]; then
    hr; echo "PHASE 2  TEST  Emulatr_tests (doctest)"; hr
    TEXE="$(find_tests || true)"
    if [ -z "${TEXE:-}" ]; then
        echo "  ERROR: Emulatr_tests binary not found for config '$CONFIG'."
        pass_test_aar=FAIL; pass_test_full=FAIL
    else
        echo "  test binary: $TEXE"
        echo "  --- AAR / ASIZ / tiling subset: $TESTFILTER ---"
        "$TEXE" --test-case="$TESTFILTER"; rc=$?
        [ "$rc" -eq 0 ] && pass_test_aar=PASS || pass_test_aar=FAIL
        echo "  AAR subset -> $pass_test_aar (rc=$rc)"
        echo "  --- full doctest suite ---"
        "$TEXE"; rc=$?
        [ "$rc" -eq 0 ] && pass_test_full=PASS || pass_test_full=FAIL
        echo "  full suite -> $pass_test_full (rc=$rc)"
    fi
fi

# ---- Phase 3: BOOT (DS10/DS20 regression + ES40 past-memtest) --------------
if [ "${SKIP_BOOT:-0}" != "1" ] && [ "$pass_build" != FAIL ]; then
    hr; echo "PHASE 3  BOOT (headless, cap=$MAXCYC): $MODELS"; hr
    for m in $MODELS; do
        echo ""
        echo "  ---- cold-boot $m (this takes minutes) ----"
        out="$(ARM=none HEADLESS=1 MAXCYC="$MAXCYC" \
                 bash "$SCRIPT_DIR/run_srm_trace_full.sh" "$m" "$CONFIG" 2>&1)"
        # console log path: the launcher prints it as "<dir>/..._console.out"
        log="$(printf '%s\n' "$out" | grep -oE '[^ ]*_'"${m}"'_console.out' | head -1)"
        if [ -z "${log:-}" ] || [ ! -f "$log" ]; then
            # fall back to newest matching console log under the repo
            log="$(ls -t "$REPO"/*/traces/*_"${m}"_console.out \
                        "$REPO"/out/build/*/traces/*_"${m}"_console.out 2>/dev/null | head -1 || true)"
        fi
        if [ -z "${log:-}" ] || [ ! -f "$log" ]; then
            echo "    (no console log captured -- launcher output tail:)"
            printf '%s\n' "$out" | tail -6 | sed 's/^/      /'
            boot_verdict[$m]="NO-LOG"
            continue
        fi
        echo "    console: $log"
        prompt="$(grep -c 'P00>>>' "$log" 2>/dev/null)"; prompt="${prompt:-0}"
        acv="$(grep -ciE 'access violation|ACVPROBE|kFaultAcv' "$log" 2>/dev/null)"; acv="${acv:-0}"
        memtest="$(grep -ciE 'testing memory|memory size' "$log" 2>/dev/null)"; memtest="${memtest:-0}"
        maxcyc_stop="$(grep -ciE 'MaxCycles' "$log" 2>/dev/null)"; maxcyc_stop="${maxcyc_stop:-0}"
        echo "    markers: P00>>>=$prompt  memtest=$memtest  ACV=$acv  MaxCyclesStop=$maxcyc_stop"
        echo "    boot-progress tail:"
        grep -aiE 'AlphaServer|AlphaPC|Console V|P00>>>|testing memory|memory size|initializ|HALT|MCHK|fault' \
             "$log" 2>/dev/null | tail -6 | sed 's/^/      /'
        case "$m" in
            ds10|ds20)
                if [ "$prompt" -gt 0 ]; then boot_verdict[$m]="PASS (P00>>>)";
                else boot_verdict[$m]="CHECK (no P00>>> at cap -- regression?)"; fi ;;
            es40)
                # FIX target: run PAST the old memtest ACV spin.  PASS if the
                # console reaches P00>>>; PROGRESS if memtest ran with no ACV
                # storm; FAIL if it still shows the ACV signature.
                if [ "$prompt" -gt 0 ]; then boot_verdict[$m]="PASS (reached P00>>>)";
                elif [ "$acv" -gt 0 ]; then boot_verdict[$m]="FAIL (memtest ACV still present: $acv)";
                elif [ "$memtest" -gt 0 ]; then boot_verdict[$m]="PROGRESS (memtest ran, no ACV; check tail for new stop)";
                else boot_verdict[$m]="CHECK (memtest marker not seen)"; fi ;;
            *) boot_verdict[$m]="INFO" ;;
        esac
        echo "    -> $m: ${boot_verdict[$m]}"
    done
fi

# ---- Consolidated verdict --------------------------------------------------
hr; echo "VERDICT"; hr
echo "  BUILD                : $pass_build"
echo "  TEST  AAR/ASIZ subset : $pass_test_aar"
echo "  TEST  full suite      : $pass_test_full"
if [ "${SKIP_BOOT:-0}" != "1" ]; then
    for m in $MODELS; do echo "  BOOT  $m              : ${boot_verdict[$m]:-SKIP}"; done
fi
hr
echo "Do-no-harm gate: BUILD + full suite green, DS10/DS20 P00>>>, ES40 past memtest."
echo "AAR encoding pinned by test_ticket02 (M3 cases: Typhoon 3-bit -> 4x1GB)."
