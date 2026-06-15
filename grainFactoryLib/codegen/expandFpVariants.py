#!/usr/bin/env python3
# ============================================================================
# expandFpVariants.py -- emit IEEE FP arithmetic variant rows for the TSV
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# Helper script.  Produces GrainMasterV4.tsv rows for the FLTI base ops
# (ADDS, ADDT, SUBS, SUBT, MULS, MULT, DIVS, DIVT) crossed with the
# 16-modifier matrix (rounding mode x trap mode).  Each base op
# expands to 16 rows, all sharing a single leafBase value so the
# codegen collapses them to one leaf per base.
#
# Function-field encoding (opcode 0x16, FLTI, 11-bit func [15:5]):
#
#   bits[5:0]  = base operation (ADDS=0x00, SUBS=0x01, MULS=0x02,
#                DIVS=0x03, ADDT=0x20, SUBT=0x21, MULT=0x22, DIVT=0x23)
#   bits[7:6]  = rounding mode (00 chopped, 01 minus-inf, 10 nearest,
#                11 dynamic-via-FPCR)
#   bits[10:8] = trap mode (000 default/imprecise, 001 underflow,
#                101 software+underflow, 111 software+underflow+inexact)
#
# Usage:
#   python expandFpVariants.py              # writes to stdout
#   python expandFpVariants.py >> ../GrainMasterV4.tsv
#
# Re-run when extending the base-op set or modifier matrix.  Output is
# stable -- same input produces the same rows in the same order.
#
# ============================================================================

import sys

BASES = [
    ("ADDS", 0x000), ("ADDT", 0x020),
    ("SUBS", 0x001), ("SUBT", 0x021),
    ("MULS", 0x002), ("MULT", 0x022),
    ("DIVS", 0x003), ("DIVT", 0x023),
]

MODIFIERS = [
    ("_C",    0x000, "chopped"),
    ("_M",    0x040, "minus-inf"),
    ("",      0x080, "nearest"),
    ("_D",    0x0C0, "dynamic"),
    ("_UC",   0x100, "underflow, chopped"),
    ("_UM",   0x140, "underflow, minus-inf"),
    ("_U",    0x180, "underflow, nearest"),
    ("_UD",   0x1C0, "underflow, dynamic"),
    ("_SUC",  0x500, "sw, underflow, chopped"),
    ("_SUM",  0x540, "sw, underflow, minus-inf"),
    ("_SU",   0x580, "sw, underflow, nearest"),
    ("_SUD",  0x5C0, "sw, underflow, dynamic"),
    ("_SUIC", 0x700, "sw, underflow, inexact, chopped"),
    ("_SUIM", 0x740, "sw, underflow, inexact, minus-inf"),
    ("_SUI",  0x780, "sw, underflow, inexact, nearest"),
    ("_SUID", 0x7C0, "sw, underflow, inexact, dynamic"),
]

FLAGS = "S_FpFormat|S_ReadsRa|S_ReadsRb|S_ReadsFp|S_WritesRc|S_WritesFp|S_FpTrap"


def main() -> None:
    out = sys.stdout
    for base, op_bits in BASES:
        out.write(f"# --- {base} family (16 variants; leafBase={base}) ---\n")
        for suffix, mod_bits, mode_desc in MODIFIERS:
            mnemonic = base + suffix
            sub = op_bits | mod_bits
            notes = f"IEEE {base} ({mode_desc})"
            out.write(f"{mnemonic}\t0x16\t0x{sub:03X}\tFltIeee\tFp\tFbox\t"
                      f"{FLAGS}\t{notes}\t{base}\n")
        out.write("\n")


if __name__ == "__main__":
    main()
