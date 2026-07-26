#!/usr/bin/env bash
# ============================================================================
# tools/extract_vmb.sh -- extract the VMB/APB bootstrap image from dqa0.img
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
#
# PURPOSE (JRN-VMB-017 follow-on, 2026-07-24): the invalid-PTBR halt at
# PC=0x2000a374 needs the VMB instruction bytes.  The Cowork sandbox reads
# the 4 GiB disks/dqa0.img as phantom zeros over the FUSE mount, so this
# script does the extraction NATIVELY (Git Bash / Mac shell), exactly as the
# SRM console does it (apisrm boot.c boot_disk + struct bb):
#   block 0 = boot block; u32 size(blocks)@offset 480, u32 lbn@offset 488;
#   the image is size*512 bytes starting at lbn*512.
# Output: <run-dir>/traces/vmb_extract_YYYYMMDD_HHMMSS.bin (+ echoed facts).
# Read-only with respect to the media; no EmulatR run involved.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$SCRIPT_DIR/.." && pwd)"

IMG="${1:-}"
if [[ -z "$IMG" ]]; then
    for cand in "$PROJ/../disks/dqa0.img" "/d/EmulatR/disks/dqa0.img"; do
        [[ -f "$cand" ]] && { IMG="$cand"; break; }
    done
fi
[[ -n "$IMG" && -f "$IMG" ]] || { echo "FATAL: dqa0.img not found (pass path as arg 1)"; exit 1; }

# Boot-block fields (little-endian u32), per apisrm boot.c struct bb.
size=$(od -A n -t u4 -j 480 -N 4 "$IMG" | tr -d ' ')
lbn=$(od  -A n -t u4 -j 488 -N 4 "$IMG" | tr -d ' ')
[[ "$size" =~ ^[0-9]+$ && "$lbn" =~ ^[0-9]+$ ]] || { echo "FATAL: bad boot block parse"; exit 1; }
echo "boot block: size=$size blocks  lbn=$lbn  (expect size=1226 per console banner)"
(( size > 0 && size < 65536 )) || { echo "FATAL: implausible size -- not a valid boot block?"; exit 1; }

# Output to the newest run dir's traces/ (fall back to repo-root traces/).
OUT_DIR="$PROJ/out/build/relwithdebinfo/traces"
[[ -d "$PROJ/out/build/relwithdebinfo" ]] || OUT_DIR="$PROJ/traces"
mkdir -p "$OUT_DIR"
TS=$(date +%Y%m%d_%H%M%S)
OUT="$OUT_DIR/vmb_extract_${TS}.bin"

dd if="$IMG" of="$OUT" bs=512 skip="$lbn" count="$size" status=none
echo "wrote $OUT ($(( size * 512 )) bytes)"

# Sanity: first 4 words must match the known boot0 disasm (JRN-VMB-016 2.2):
#   d3800000 201f0001 203f001c 48010720
echo -n "first 16 bytes: "; od -A n -t x4 -j 0 -N 16 "$OUT" | tr -s ' '
echo "expect        : d3800000 201f0001 203f001c 48010720"
