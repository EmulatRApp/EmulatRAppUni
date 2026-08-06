#!/usr/bin/env bash
# ============================================================================
# run_es40_showdev.sh -- cold-boot the ES40 firmware to P00>>> with a PuTTY
# console + IDE config-space trace, to diagnose task #32 (ES40 does NOT
# enumerate dqa0/dqa1, though DS20 does with the SAME CY82C693 model).
#
# This is the mirror of tools/run_ds20_showdev.sh.  The GOAL here is the IDE
# TRACE, not the show-dev result: EMULATR_IDE_TRACE logs every config-space
# read the ES40 pc264 console issues against the IDE at bus0/slot5/func1, so we
# can see WHICH register it reads and where it stops before issuing IDENTIFY.
# Compare that log to the DS20 baseline to decide stand-in-fix vs M5229 model.
#
# Expectation: at P00>>>, "show dev" currently lists only dva0 (floppy); dqa0/
# dqa1 are absent (that is the bug).  Attaching disks/dqa0.img does not change
# that -- it only ensures the disk WOULD show if the console enumerated it.
#
# Everything this script changes (ini model=, the run-dir ES40 manifest) is
# backed up and RESTORED on exit -- it leaves no committed change.  Diagnostic
# only; no source edits.  ASCII(128) only.
#
# Location: EmulatRAppUniV4/Emulatr/tools/ (project tools dir).  Self-locates.
#
#   Usage:   ./tools/run_es40_showdev.sh
#   Toggles: ATTACH_DISK=0   -> do NOT attach dqa0.img
#            EMULATR_CONSOLE_PORT=10023  -> console TCP port (default 10023)
#            MAXCYC=<n>       -> max guest cycles (default 2e9)
#
# Host    : Windows / Git Bash (PC) is the supported path; a macOS/Linux branch
#           is stubbed via the platform guard below for the cross-platform owner.
# Output  : run log -> <run-dir>/run_es40_showdev_<ts>.log
# ============================================================================
set -euo pipefail

# ---- platform guard: PC (Windows/Git Bash) authoritative --------------------
# One script set serves Windows and (owned elsewhere) macOS/Linux.  uname picks
# the host so the binary name and console tooling resolve per platform.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EMU_HOST=win ;;   # Git Bash / MSYS2 on Windows (PC)
    Darwin)               EMU_HOST=mac ;;   # macOS  (cross-platform owner)
    *)                    EMU_HOST=nix ;;   # Linux / other POSIX
esac

# ---- locate project + repo + newest run dir --------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO="$(cd "$PROJ/../.." && pwd)"
[[ -d "$PROJ/systemLib" ]] || { echo "FATAL: $PROJ does not look like the Emulatr project"; exit 1; }

# Auto-pick the newest run dir that has the exe + ES40 firmware + ES40 manifest.
RUN_DIR=""
NEWEST=0
for cand in "RelWithDebInfo" "out/build/relwithdebinfo" "Release" \
            "out/build/release" "out/build/cli" "Debug"; do
    d="$PROJ/$cand"
    { [[ -x "$d/Emulatr.exe" ]] || [[ -x "$d/Emulatr" ]]; } || continue
    [[ -f "$d/firmware/es40_v7_3.exe" ]]      || continue
    [[ -f "$d/es40_v7_3_platform.json" ]]     || continue
    m=$(stat -c '%Y' "$d/Emulatr.exe" 2>/dev/null || echo 0)
    if (( m > NEWEST )); then NEWEST=$m; RUN_DIR="$d"; fi
done
# Pin override: RUN_DIR_OVERRIDE=<dir> forces a specific build dir, bypassing the
# newest-exe auto-pick.  Use when your MSVC/Qt build target is NOT the dir the
# pick lands on (the recurring "stale binary" trap).  Accepts an absolute path or
# one relative to the Emulatr project.
if [[ -n "${RUN_DIR_OVERRIDE:-}" ]]; then
    if [[ -x "$RUN_DIR_OVERRIDE/Emulatr.exe" ]]; then RUN_DIR="$RUN_DIR_OVERRIDE"
    elif [[ -x "$PROJ/$RUN_DIR_OVERRIDE/Emulatr.exe" ]]; then RUN_DIR="$PROJ/$RUN_DIR_OVERRIDE"
    else echo "FATAL: RUN_DIR_OVERRIDE has no Emulatr.exe: $RUN_DIR_OVERRIDE"; exit 1; fi
fi
[[ -n "$RUN_DIR" ]] || { echo "FATAL: no run dir with Emulatr.exe + es40 firmware + es40 manifest under $PROJ"; exit 1; }
cd "$RUN_DIR"

# binary: Windows emits Emulatr.exe, POSIX a bare Emulatr -- prefer .exe so the
# Windows production path is unchanged, then fall back to the mac/Linux build.
if   [ -x "./Emulatr.exe" ]; then EXE="./Emulatr.exe"
elif [ -x "./Emulatr"     ]; then EXE="./Emulatr"
else echo "FATAL: no Emulatr(.exe) in $RUN_DIR"; exit 1; fi
FW="firmware/es40_v7_3.exe"
# 2026-07-31: per-instance run environment (FILE 7).  Sourcing this owns
# STEM/TAG/TS, the log-name helper, and the per-instance flash NVRAM,
# sentinels and console port -- concurrent instances no longer collide.
. "$SCRIPT_DIR/emulatr_run_env.sh"
INI="config/Emulatr.ini"
MANIFEST="es40_v7_3_platform.json"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-2000000000}"
ATTACH_DISK="${ATTACH_DISK:-1}"
LOG="$(emulatr_log showdev)"   # logs/<TAG>_showdev_<TS>.log (FILE 7)

# ---- soft stale-binary notice (informational; not a build gate) ------------
for src in "$PROJ/deviceLib/Tsunami/Cy82C693Ide.h" \
           "$PROJ/chipsetLib/TsunamiChipset.h"; do
    if [[ -f "$src" && "$src" -nt "$EXE" ]]; then
        echo "NOTE: $(basename "$src") is newer than Emulatr.exe -- this run uses the"
        echo "      LAST-BUILT binary, not your latest edits.  Rebuild if that matters."
    fi
done

# ---- restore-on-exit trap (ini + manifest) ---------------------------------
cleanup() {
    [[ -f "$INI.showdevbak"      ]] && mv -f "$INI.showdevbak"      "$INI"      2>/dev/null || true
    [[ -f "$MANIFEST.showdevbak" ]] && mv -f "$MANIFEST.showdevbak" "$MANIFEST" 2>/dev/null || true
}
trap cleanup EXIT

# ---- force model=ES40 for this run -----------------------------------------
if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.showdevbak"
    if grep -qiE '^[[:space:]]*model[[:space:]]*=' "$INI"; then
        # Portable across GNU and BSD/macOS sed: no in-place -i (needs a suffix
        # arg on BSD) and no \s / /I (GNU-only) -- use a temp file + [[:space:]].
        SED_TMP="$(mktemp)"
        sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1ES40/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"
    else
        echo "WARN: no model= line in $INI -- relying on firmware stem for ES40 platform"
    fi
fi

# ---- attach disks/dqa0.img (absolute path) into the ES40 manifest ----------
if [[ "$ATTACH_DISK" == "1" ]]; then
    IMG_BASH="$REPO/disks/dqa0.img"
    if [[ -f "$IMG_BASH" ]]; then
        if command -v cygpath >/dev/null 2>&1; then
            IMG_WIN="$(cygpath -m "$IMG_BASH")"
        else
            IMG_WIN="$(echo "$IMG_BASH" | sed -E 's#^/([a-zA-Z])/#\U\1:/#')"
        fi
        cp -f "$MANIFEST" "$MANIFEST.showdevbak"
        # Set ONLY the first empty media (the ata_disk / unit 0 entry).
        awk -v img="$IMG_WIN" '
            !done && $0 ~ /"media": ""/ {
                sub(/"media": ""/, "\"media\": \"" img "\"");
                done=1
            }
            { print }
        ' "$MANIFEST.showdevbak" > "$MANIFEST"
        if grep -q "$IMG_WIN" "$MANIFEST"; then
            echo "attach  = dqa0 (unit 0) <- $IMG_WIN"
        else
            echo "WARN: could not patch media into $MANIFEST -- dqa0 stays no-media"
        fi
    else
        echo "WARN: $IMG_BASH not found -- skipping disk attach"
    fi
else
    echo "attach  = OFF (ATTACH_DISK=0)"
fi

# ---- environment -----------------------------------------------------------
unset  EMULATR_NO_PUTTY               # KEEP PuTTY auto-launch ON
export EMULATR_CONSOLE_MIRROR=1       # banner + console -> stderr/log
export EMULATR_IDE_TRACE=1            # IDE config-space + I/O probe trace (the point)
export EMULATR_CONSOLE_PORT="$PORT"

if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
    echo "WARN: PuTTY.exe not on PATH -- attach manually:  putty -telnet localhost $PORT"
fi

# ---- launch ----------------------------------------------------------------
echo "-----------------------------------------------------------------------"
# Build stamp -> written INTO the log (tee CREATES it) so every run records the
# exact binary.  The exe SHA + build time answer "which binary ran?" precisely.
{
  echo "======================================================================="
  echo "run     = $(basename "$0")   $(date '+%Y-%m-%d %H:%M:%S')"
  echo "RUN_DIR = $RUN_DIR"
  echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
  echo "exe sha = $(sha256sum "$EXE" 2>/dev/null | cut -c1-16)"
  echo "fw      = $FW   (ES40 platform)"
  echo "console = PuTTY auto-launch on localhost:$PORT"
  echo "log     = $RUN_DIR/$LOG"
  echo "======================================================================="
  echo "At  P00>>>  type:   show dev     (dqa0 EXPECTED ABSENT until Phase 2)"
  echo "The IDE-TRACE C lines in the log are the diagnostic payload."
  echo "======================================================================="
} | tee "$LOG"

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee -a "$LOG"

# ---- post-run summary ------------------------------------------------------
echo "-----------------------------------------------------------------------"
echo "done.  IDE config-space reads the ES40 console issued (cfg probe):"
grep -iE "IDE-TRACE C|cfg reg" "$LOG" | tail -60 || true
echo "--- dqa / show dev lines (if any) ---"
grep -iE "dqa|82C693|show dev" "$LOG" | tail -20 || true
