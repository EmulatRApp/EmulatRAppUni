#!/usr/bin/env bash
# ============================================================================
# run_ds20_putty.sh -- cold-boot the DS20 firmware with an attached PuTTY
# console, so the cosmetic badge rewrite ("AlphaServer DS20 <MHz_eff> MHz") is
# visible live in the console window and in the run log.  2026-07-01.
#
# Bash-first launcher (Git Bash on Windows).  It:
#   - resolves the build/run dir (out/build/relwithdebinfo),
#   - REFUSES to run a stale exe older than today's edited sources,
#   - forces ini model=DS20 for this run (restored on exit),
#   - leaves PuTTY auto-launch ON (does NOT set EMULATR_NO_PUTTY),
#   - turns on the console mirror so the log shows MHz_eff (IDLEWARP removed
#     2026-08-01 -- see journals/20260801_JRN-AUD-002 Sec 4),
#   - tees console/stderr into a timestamped log.
#
# Host    : Windows / Git Bash (PC) is the supported path; a macOS/Linux branch
#           is stubbed via the platform guard below for the cross-platform owner.
# Usage   : ./tools/run_ds20_putty.sh        (RUN_DIR=<dir> overrides the run dir)
# Output  : run log -> <run-dir>/run_ds20_<ts>.log
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

# ---- locate the build/run dir ----------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
#RUN_DIR="${RUN_DIR:-$PROJ_DIR/out/build/release}"
RUN_DIR="${RUN_DIR:-$PROJ_DIR/out/build/relwithdebinfo}"
cd "$RUN_DIR"

# binary: Windows emits Emulatr.exe, POSIX a bare Emulatr -- prefer .exe.
if   [ -x "./Emulatr.exe" ]; then EXE="./Emulatr.exe"
elif [ -x "./Emulatr"     ]; then EXE="./Emulatr"
else EXE="./Emulatr.exe"; fi   # keep a stable value; preflight below FATALs
FW="firmware/ds20_v7_3.exe"
INI="config/Emulatr.ini"
PORT="${EMULATR_CONSOLE_PORT:-10023}"
MAXCYC="${MAXCYC:-222000000000}"
LOG="run_ds20_$(date +%Y%m%d_%H%M%S).log"

# ---- preflight -------------------------------------------------------------
[[ -x "$EXE" ]]  || { echo "FATAL: $EXE not found in $RUN_DIR"; exit 1; }
[[ -f "$FW" ]]   || { echo "FATAL: firmware $FW not found (need the DS20 image)"; exit 1; }

# Stale-binary guard: the exe must be newer than every source file we edited,
# otherwise the badge rewrite is not in it -- rebuild first.
for src in "$PROJ_DIR/deviceLib/BadgeMhzGauge.h" \
           "$PROJ_DIR/deviceLib/Tsunami/Uart16550.h" \
           "$PROJ_DIR/main.cpp"; do
    if [[ -f "$src" && "$src" -nt "$EXE" ]]; then
        echo "FATAL: $EXE is OLDER than $(basename "$src") -- rebuild before testing."
        echo "       (the badge-rewrite change is not in this binary)"
        exit 1
    fi
done

# PuTTY reachable?  Non-fatal -- the TCP console still comes up on $PORT and you
# can attach manually (putty -telnet localhost $PORT) if auto-launch fails.
if ! command -v PuTTY.exe >/dev/null 2>&1 && ! command -v putty.exe >/dev/null 2>&1; then
    echo "WARN: PuTTY.exe not on PATH -- auto-launch will warn; attach manually to localhost:$PORT"
fi

# Force model=DS20 for this run; restore the ini on exit.
if [[ -f "$INI" ]]; then
    cp -f "$INI" "$INI.puttybak"
    trap 'mv -f "$INI.puttybak" "$INI" 2>/dev/null || true' EXIT
    if grep -qiE '^[[:space:]]*model[[:space:]]*=' "$INI"; then
        # Portable across GNU and BSD/macOS sed: no in-place -i (needs a suffix
        # arg on BSD) and no \s / /I (GNU-only) -- use a temp file + [[:space:]].
        SED_TMP="$(mktemp)"
        sed "s/^\([[:space:]]*model[[:space:]]*=[[:space:]]*\).*/\1DS20/" "$INI" > "$SED_TMP" && mv -f "$SED_TMP" "$INI"
    else
        echo "WARN: no model= line in $INI -- relying on firmware stem for DS20 platform"
    fi
fi

# ---- environment -----------------------------------------------------------
unset  EMULATR_NO_PUTTY               # KEEP PuTTY auto-launch ON
export EMULATR_CONSOLE_MIRROR=1       # banner + rewritten badge -> stderr/log
export EMULATR_CONSOLE_PORT="$PORT"

# NOTE 2026-08-01: the optional DELAYWARP -> EMULATR_RSCCWARP hook was removed
# with the warp family itself (injection: it rewrote the guest tick counter
# 0x3c970 out of band).  Delay spins now run literally.  See
# journals/20260801_JRN-AUD-002_injection_lever_ledger.md Sec 4.

# ---- launch ----------------------------------------------------------------
echo "RUN_DIR = $RUN_DIR"
echo "exe     = $EXE  (built $(stat -c '%y' "$EXE" 2>/dev/null | cut -d. -f1))"
echo "fw      = $FW"
echo "console = PuTTY auto-launch on localhost:$PORT"
echo "log     = $RUN_DIR/$LOG"
echo "-----------------------------------------------------------------------"

"$EXE" \
    --firmware "$FW" \
    --no-autoload \
    --autosnapshot off \
    --max-cycles "$MAXCYC" \
    2>&1 | tee "$LOG"

echo "-----------------------------------------------------------------------"
echo "done.  badge line:"
grep -iE "AlphaServer DS20|[0-9]+ MHz" "$LOG" | head || true
