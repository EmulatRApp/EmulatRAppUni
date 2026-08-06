#!/usr/bin/env bash
# ============================================================================
# tools/emulatr_run_env.sh -- per-instance run environment (SOURCE, do not run)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# FILE 7: tools/emulatr_run_env.sh
# FUNCTION: single owner of every per-instance name a run needs, so two or more
#           EmulatR instances can share one run directory without clobbering
#           each other.
# CHANGE (2026-07-31): NEW FILE.  Concurrent multi-instance execution is now a
#           first-class workflow.  Before this, each launcher invented its own
#           log name and every instance shared one flash NVRAM image, one stop
#           sentinel, one HWRPB-scan sentinel and one console port.
#
# CONTRACT -- the caller sets FW (firmware path) then sources this file:
#     FW="firmware/ds20_v7_3.exe"
#     . "$SCRIPT_DIR/emulatr_run_env.sh"
#
# PROVIDES (shell variables):
#     STEM      firmware basename, no extension          ds20_v7_3
#     INSTANCE  optional discriminator ($EMULATR_INSTANCE, may be empty)
#     TAG       STEM, or STEM_INSTANCE when INSTANCE is set
#     TS        run timestamp, YYYYMMDD_HHMMSS
#     emulatr_log <purpose> [ext]   ->  logs/<TAG>_<purpose>_<TS>.<ext>
#
# EXPORTS (consumed by the emulator itself):
#     EMULATR_LOG_STEM         -> coreLib/LogArtifactPath; keys the three event
#                                 logs to logs/<TAG>_{faults,unaligned,cbox_csr}.log
#     EMULATR_FLASH_ROM        -> Machine::bindFlash; per-instance NVRAM so two
#                                 same-platform runs cannot corrupt one image
#     EMULATR_STOP_FILE        -> Machine::run graceful-stop sentinel
#     EMULATR_HWRPB_SCAN_FILE  -> HWRPB-scan sentinel
#     EMULATR_CONSOLE_PORT     -> Machine.cpp:325; distinct TCP port per instance
#
# CONCURRENCY RECIPE -- two DS20s side by side:
#     EMULATR_INSTANCE=a EMULATR_CONSOLE_PORT=10023 ./tools/run_ds20_showdev.sh &
#     EMULATR_INSTANCE=b EMULATR_CONSOLE_PORT=10024 ./tools/run_ds20_showdev.sh &
#
# DETERMINISM: every name derives from argv/env only.  TS is the one clock read
# and it names files -- it never reaches guest-visible state.
# ============================================================================

# ---- stem: firmware basename, extension dropped ----------------------------
if [[ -z "${FW:-}" ]]; then
    echo "emulatr_run_env.sh: FW is unset -- set it before sourcing" >&2
    return 1 2>/dev/null || exit 1
fi
STEM="$(basename "$FW")"
STEM="${STEM%.*}"

# ---- optional per-instance discriminator ------------------------------------
# Two instances of the SAME platform need more than the stem to stay apart.
# EMULATR_INSTANCE is folded to [A-Za-z0-9_-] so it is always filename-safe.
INSTANCE="$(printf '%s' "${EMULATR_INSTANCE:-}" | tr -c 'A-Za-z0-9_-' '_' | sed 's/_*$//')"
if [[ -n "$INSTANCE" ]]; then TAG="${STEM}_${INSTANCE}"; else TAG="$STEM"; fi

TS="$(date +%Y%m%d_%H%M%S)"

# ---- artifact directories (run-dir relative, per the placement rule) --------
mkdir -p logs traces

# ---- log-name helper --------------------------------------------------------
# emulatr_log <purpose> [ext]  ->  logs/<TAG>_<purpose>_<TS>.<ext>
emulatr_log() {
    local purpose="$1"
    local ext="${2:-log}"
    printf 'logs/%s_%s_%s.%s' "$TAG" "$purpose" "$TS" "$ext"
}

# ---- per-instance emulator environment --------------------------------------
export EMULATR_LOG_STEM="$TAG"

# NVRAM: give each instance its own image, seeded from the platform's shared
# one so it inherits the console settings instead of factory-0xFF booting.
# Without INSTANCE the default <stem>.rom is left alone (existing behavior).
if [[ -n "$INSTANCE" ]]; then
    _base_rom="$(dirname "$FW")/${STEM}.rom"
    _inst_rom="$(dirname "$FW")/${TAG}.rom"
    if [[ -f "$_base_rom" && ! -f "$_inst_rom" ]]; then
        cp -f "$_base_rom" "$_inst_rom"
    fi
    export EMULATR_FLASH_ROM="$_inst_rom"
    unset _base_rom _inst_rom
fi

# Sentinels: shared filenames would let one operator stop EVERY instance in the
# run dir, and one HWRPB scan would fire in the wrong machine.
export EMULATR_STOP_FILE="EMULATR_STOP_${TAG}"
export EMULATR_HWRPB_SCAN_FILE="EMULATR_HWRPB_SCAN_${TAG}"

# Console port: the server does NOT fall back when the port is taken
# (SRMConsoleDevice.cpp:165 errors out), so concurrent instances must be given
# distinct ports.  Honor an explicit setting; otherwise keep the 10023 default.
export EMULATR_CONSOLE_PORT="${EMULATR_CONSOLE_PORT:-10023}"

echo "run-env: TAG=$TAG port=$EMULATR_CONSOLE_PORT stop=$EMULATR_STOP_FILE flash=${EMULATR_FLASH_ROM:-<default ${STEM}.rom>}"
