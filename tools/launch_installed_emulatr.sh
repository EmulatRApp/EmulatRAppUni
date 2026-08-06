#!/usr/bin/env bash
# launch_installed_emulatr.sh
#
# Launch the Setup Factory-installed EmulatR from a writable run directory.
#
# The installed image lives under Program Files, which is read-only for a
# normal user process. Therefore this script NEVER runs Emulatr.exe with its
# cwd inside the install tree. Instead it:
#   1. cds into a writable RUN_DIR (firmware/, Emulatr.ini, flash .rom live there)
#   2. invokes the installed executable by absolute path
#   3. mirrors console output to {run-dir}/logs/ per house convention
#
# Usage:
#   ./launch_installed_emulatr.sh [RUN_DIR] [-- extra emulatr args...]
#
#   RUN_DIR defaults to the current working directory if it looks like a
#   run dir (contains Emulatr.ini), otherwise the script aborts and tells
#   you to name one explicitly.
#
# Placement: D:\EmulatR\EmulatRAppUniV4\Emulatr\tools  (house rule)

set -euo pipefail

# ---------------------------------------------------------------------------
# Installed executable (Git Bash form of
#   C:\Program Files\eNVy Systems, Inc\asa-emulatR\Emulatr.exe)
# Adjust EXE_NAME if the installer emitted a different executable name.
# ---------------------------------------------------------------------------
INSTALL_DIR="/c/Program Files/eNVy Systems, Inc/asa-emulatR"
EXE_NAME="Emulatr.exe"
EMULATR_EXE="${INSTALL_DIR}/${EXE_NAME}"

# ---------------------------------------------------------------------------
# Resolve run directory
# ---------------------------------------------------------------------------
RUN_DIR=""
if [[ $# -ge 1 && "$1" != "--" ]]; then
    RUN_DIR="$1"
    shift
else
    RUN_DIR="$(pwd)"
fi

# Anything after a literal -- is passed through to Emulatr.exe untouched.
if [[ $# -ge 1 && "$1" == "--" ]]; then
    shift
fi
EXTRA_ARGS=("$@")

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
if [[ ! -x "${EMULATR_EXE}" && ! -f "${EMULATR_EXE}" ]]; then
    echo "ERROR: installed executable not found:" >&2
    echo "       ${EMULATR_EXE}" >&2
    echo "       Check INSTALL_DIR / EXE_NAME at the top of this script." >&2
    exit 1
fi

if [[ ! -d "${RUN_DIR}" ]]; then
    echo "ERROR: run directory does not exist: ${RUN_DIR}" >&2
    exit 1
fi

case "${RUN_DIR}" in
    /c/Program\ Files/*|"C:/Program Files/"*|"C:\\Program Files\\"*)
        echo "ERROR: refusing to use a Program Files path as the run dir." >&2
        echo "       Program Files is read-only; pick a writable run dir" >&2
        echo "       holding Emulatr.ini, firmware/, and the flash .rom." >&2
        exit 1
        ;;
esac

cd "${RUN_DIR}"

if [[ ! -f "Emulatr.ini" ]]; then
    echo "ERROR: ${RUN_DIR} does not look like a run dir (no Emulatr.ini)." >&2
    echo "       Pass an explicit run dir:  $0 /d/EmulatR/runs/ds20" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Output placement per house convention: logs/ and traces/ under the run dir
# ---------------------------------------------------------------------------
mkdir -p logs traces
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="logs/run_installed_${STAMP}.log"

echo "EmulatR (installed) launch"
echo "  exe     : ${EMULATR_EXE}"
echo "  run dir : ${RUN_DIR}"
echo "  log     : ${RUN_DIR}/${LOG}"
[[ ${#EXTRA_ARGS[@]} -gt 0 ]] && echo "  args    : ${EXTRA_ARGS[*]}"
echo

# ---------------------------------------------------------------------------
# Launch, mirroring console to the log. Exit status of the emulator is
# preserved despite the tee pipeline.
# ---------------------------------------------------------------------------
set +e
"${EMULATR_EXE}" "${EXTRA_ARGS[@]}" 2>&1 | tee "${LOG}"
RC=${PIPESTATUS[0]}
set -e

echo
echo "EmulatR exited with status ${RC}; console mirrored to ${LOG}"
exit "${RC}"
