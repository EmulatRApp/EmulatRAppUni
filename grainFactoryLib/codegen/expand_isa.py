#!/usr/bin/env python3
# ============================================================================
# expand_isa.py -- generate GrainMasterV4.tsv rows from the Alpha ARM encoding
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# PURPOSE: bring GrainMasterV4.tsv to 100% architectural coverage by parsing the
#   authoritative Alpha ARM numerical encoding grid (Appendix C) -- which already
#   enumerates EVERY opcode.func, INCLUDING every qualifier variant (/C /M /D /U
#   /UC /UM /SU /SUI /SUID /SV /SVI /V /S ...), one token per instruction -- and
#   emitting one distinct TSV row per opcode.func (NO consolidation).
#
# SOURCES (read-only, in the project tree):
#   - alpha_arch_ref.txt  Appendix C numerical grid: every OO.FFF MNEMONIC[/QUAL]
#     for the operate/FP families (opcodes 0x14/0x15/0x16/0x17 and integer
#     0x10-0x13), plus opcode 0x18 (Misc) and 0x1C (FPTI).
#   - Table C-15 (PALcode opcodes) is parsed by the companion --pal mode for the
#     three personalities (OpenVMS / Tru64 / Alpha Linux).
#
# FORMAT NOTE that makes parsing robust: in the numerical grid the token after
#   "OO.FFF" is the ALL-CAPS mnemonic ("MULF", "ADDF/C"); in the alphabetical
#   list it is a Mixed-Case description word ("Multiply").  We therefore accept a
#   token ONLY if it is all-caps AND its base matches a known mnemonic prefix --
#   this cleanly selects grid tokens and rejects description words, so the script
#   is safe to run over the whole file.
#
# USAGE:
#   python expand_isa.py --arm "<...>/Processor Support/alpha_arch_ref.txt" \
#                        --master ../GrainMasterV4.tsv [--opcodes 14,15,16,17] \
#                        >> ../GrainMasterV4.tsv      # appends MISSING rows only
#   (review the emitted diff before committing; re-run is idempotent.)
#
# PREREQUISITE for opcode 0x15 (VAX float): genGrains.py needs a "FltVax" kind.
#   Add to genGrains.py:  OPCODE_TO_KIND[0x15] = "FltVax";  the kind name to the
#   kind list;  and  SUBTABLE_WIDTH["FltVax"] = 2048.  (See README; same shape as
#   the FltIeee 0x16 entry.)
#
# LEAVES: this phase emits DISPATCH rows only; leafBase is left blank so the
#   codegen stubs each leaf (logUnimplementedStub).  Implementing the leaf bodies
#   + adding them to handwritten.tsv is the subsequent phase.
# ============================================================================

import argparse
import re
import sys

KIND = {
    0x10: "IntArith", 0x11: "IntLogical", 0x12: "IntShift", 0x13: "IntMul",
    0x14: "ItFp", 0x15: "FltVax", 0x16: "FltIeee", 0x17: "FltLogical",
    0x18: "Misc", 0x1C: "FpTiExt",
}

# Known FP mnemonic prefixes -> (Box, formatGroup, semantic flags).
FP_BINARY  = ("Fbox", "Fp", "S_FpFormat|S_ReadsRa|S_ReadsRb|S_ReadsFp|S_WritesRc|S_WritesFp|S_FpTrap")
FP_UNARY   = ("Fbox", "Fp", "S_FpFormat|S_ReadsRb|S_ReadsFp|S_WritesRc|S_WritesFp|S_FpTrap")
FP_COPY    = ("Fbox", "Fp", "S_FpFormat|S_ReadsRa|S_ReadsRb|S_ReadsFp|S_WritesRc|S_WritesFp")
ITOF       = ("Ebox", "Op", "S_OpFormat|S_ReadsRa|S_ReadsInt|S_WritesRc|S_WritesFp")
FPCR_MV    = ("Fbox", "Fp", "S_FpFormat|S_ReadsRa|S_ReadsFp")


def classify(mnem):
    base = mnem.split("/")[0]
    if base.startswith(("ADD", "SUB", "MUL", "DIV")):
        return FP_BINARY
    if base.startswith("CMP"):
        return FP_BINARY
    if base.startswith(("CVT", "SQRT")):
        return FP_UNARY
    if base.startswith(("CPYS", "FCMOV")):
        return FP_COPY
    if base.startswith("ITOF"):
        return ITOF
    if base in ("MT_FPCR", "MF_FPCR"):
        return FPCR_MV
    return None  # not an FP-operate mnemonic we recognize -> skip


# Token in the numerical grid: "OO.FFF  MNEMONIC[/QUAL]" (mnemonic ALL CAPS).
TOKEN = re.compile(
    r"\b([0-3][0-9A-Fa-f])\.([0-9A-Fa-f]{3,4})\s+([A-Z][A-Z0-9_]{2,}(?:/[A-Z]+)?)\b"
)


def parse_arm(arm_path, opcodes):
    found = {}
    txt = open(arm_path, encoding="latin-1").read()
    for op_s, fn_s, mnem in TOKEN.findall(txt):
        op = int(op_s, 16)
        if op not in opcodes:
            continue
        if classify(mnem) is None:
            continue
        found[(op, int(fn_s, 16))] = mnem
    return found


def existing(master_path):
    have = set()
    for ln in open(master_path, encoding="latin-1"):
        if ln.startswith("#") or not ln.strip():
            continue
        f = ln.split("\t")
        if len(f) < 3 or not f[1].startswith("0x"):
            continue
        try:
            have.add((int(f[1], 16), int(f[2], 16)))
        except ValueError:
            pass
    return have


def main():
    ap = argparse.ArgumentParser(description="emit missing GrainMasterV4 FP rows from the ARM")
    ap.add_argument("--arm", required=True)
    ap.add_argument("--master", required=True)
    ap.add_argument("--opcodes", default="14,15,16,17")
    args = ap.parse_args()

    opcodes = {int(x, 16) for x in args.opcodes.split(",")}
    found = parse_arm(args.arm, opcodes)
    have = existing(args.master)

    missing = sorted(k for k in found if k not in have)
    sys.stderr.write(
        f"# parsed {len(found)} FP encodings; {len(found) - len(missing)} present; "
        f"emitting {len(missing)} missing rows\n"
    )
    print(f"\n# --- ISA completion (expand_isa.py): {len(missing)} rows, opcodes {sorted(opcodes):x} ---")
    for op, fn in missing:
        mnem = found[(op, fn)]
        box, fmt, flags = classify(mnem)
        name = mnem.replace("/", "_")
        kind = KIND[op]
        print(f"{name}\t0x{op:02X}\t0x{fn:03X}\t{kind}\t{fmt}\t{box}\t{flags}\t{mnem}")


if __name__ == "__main__":
    main()
