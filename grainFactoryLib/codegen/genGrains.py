#!/usr/bin/env python3
# ============================================================================
# genGrains.py -- GrainFactory codegen for EmulatR V4
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
#
# Commercial use prohibited without separate license.
# Contact:        peert@envysys.com  |  https://envysys.com
# Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
# ============================================================================
#
# Purpose
# -------
# Reads SemanticFlags.tsv and GrainMaster*.tsv, validates the join, and
# emits generated headers under grainFactoryLib/generated/.
#
# Currently emits:
#   SemanticFlagsEnum.h  -- enum class GrainSem with all bit constants,
#                           per-category masks, bitwise operators, and
#                           predicates.
#   DispatchKinds.h      -- enum class DispatchKind with the canonical
#                           18 values plus a dispatchKindName() helper.
#
# Planned (after PipelineSlot.h / BoxResult.h land):
#   GrainsForward.h      -- per-box leaf function declarations.
#   DispatchTables.cpp   -- populated primary and per-kind tables.
#
# Invocation
# ----------
#   python genGrains.py --flags  ../SemanticFlags.tsv \
#                       --master ../GrainMasterV4.tsv \
#                       --out    ../generated         \
#                       [--validate-only]
#
# Exit codes:
#   0  success
#   1  argument or I/O error
#   2  validation failure (errors printed to stderr)
#
# ============================================================================

import re
import sys
import argparse
from pathlib import Path
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple


# ----------------------------------------------------------------------------
# Module constants
# ----------------------------------------------------------------------------

# Match a clean S_xxx flag identifier with no surrounding punctuation.
# Used by the mutex-trailer parser to distinguish member-list lines
# from explanatory prose that happens to mention flag names.
_FLAG_RE = re.compile(r"^S_[A-Za-z0-9_]+$")

# Canonical dispatchKind enumeration -- the only values accepted in
# GrainMasterV4.tsv's dispatchKind column.  Order is the order emitted
# into DispatchKind in DispatchKinds.h.
CANONICAL_DISPATCH_KINDS: List[str] = [
    "Direct",       # primary opcode IS the operation
    "IntArith",     # 0x10 INTA
    "IntLogical",   # 0x11 INTL
    "IntShift",     # 0x12 INTS
    "IntMul",       # 0x13 INTM
    "ItFp",         # 0x14 ITFP -- int <-> fp moves
    "FltVax",       # 0x15 FLTV -- VAX F/G/D float
    "FltIeee",      # 0x16 FLTI
    "FltLogical",   # 0x17 FLTL
    "Misc",         # 0x18 MISC
    "JmpClass",     # 0x1A JMP/JSR/RET/JSR_COROUTINE
    "FpTiExt",      # 0x1C BWX/CIX/MVI/FIX
    "Pal",          # 0x00 CALL_PAL
    "HwMfpr",       # 0x19
    "HwLd",         # 0x1B
    "HwMtpr",       # 0x1D
    "HwRei",        # 0x1E
    "HwSt",         # 0x1F
    "Reserved",     # 0x01..0x07, 0x15, unimplemented sub-table slots
]

# Canonical encoding-format enumeration -- the only values accepted in
# GrainMasterV4.tsv's format column.
CANONICAL_FORMATS: List[str] = [
    "Mem", "Bra", "Op", "Fp", "Pal", "Hw", "Jmp", "Misc",
]

# Canonical functional-box enumeration -- the only values accepted in
# GrainMasterV4.tsv's box column.
CANONICAL_BOXES: List[str] = [
    "Ibox", "Ebox", "Fbox", "Mbox", "Cbox", "PalBox",
]

# Mapping from box column values to C++ namespace names.  Lowered to
# camelCase per project naming convention; leaves under each namespace
# live in {box}Lib/grains/ source files.
BOX_TO_NAMESPACE: Dict[str, str] = {
    "Ibox":   "iBox",
    "Ebox":   "eBox",
    "Fbox":   "fBox",
    "Mbox":   "mBox",
    "Cbox":   "cBox",
    "PalBox": "palBox",
}

# Per-primary-opcode dispatch kind.  Architecturally fixed by Alpha;
# encoded here rather than derived from rows so the codegen knows the
# correct kind for primary opcodes that have no implemented rows yet
# (e.g., 0x01..0x07 reserved, 0x15 VAX FP).
PRIMARY_KIND_MAP: Dict[int, str] = {
    0x00: "Pal",
    0x01: "Reserved", 0x02: "Reserved", 0x03: "Reserved",
    0x04: "Reserved", 0x05: "Reserved", 0x06: "Reserved", 0x07: "Reserved",
    # 0x08..0x0F: Mem-format direct (LDA, LDAH, LDBU, LDQ_U, LDWU, STW, STB, STQ_U)
    0x08: "Direct", 0x09: "Direct", 0x0A: "Direct", 0x0B: "Direct",
    0x0C: "Direct", 0x0D: "Direct", 0x0E: "Direct", 0x0F: "Direct",
    0x10: "IntArith", 0x11: "IntLogical", 0x12: "IntShift",
    0x13: "IntMul", 0x14: "ItFp",
    0x15: "FltVax",  # FLTV (VAX F/G/D float; OpenVMS default G_float/F_float)
    0x16: "FltIeee", 0x17: "FltLogical", 0x18: "Misc",
    0x19: "HwMfpr", 0x1A: "JmpClass", 0x1B: "HwLd",
    0x1C: "FpTiExt", 0x1D: "HwMtpr", 0x1E: "HwRei", 0x1F: "HwSt",
    # 0x20..0x27: FP load/store direct (LDF, LDG, LDS, LDT, STF, STG, STS, STT)
    0x20: "Direct", 0x21: "Direct", 0x22: "Direct", 0x23: "Direct",
    0x24: "Direct", 0x25: "Direct", 0x26: "Direct", 0x27: "Direct",
    # 0x28..0x2F: Int load/store direct (LDL, LDQ, LDL_L, LDQ_L, STL, STQ, STL_C, STQ_C)
    0x28: "Direct", 0x29: "Direct", 0x2A: "Direct", 0x2B: "Direct",
    0x2C: "Direct", 0x2D: "Direct", 0x2E: "Direct", 0x2F: "Direct",
    # 0x30..0x3F: Branch direct (BR, FBEQ, FBLT, FBLE, BSR, FBNE, FBGE, FBGT,
    #                            BLBC, BEQ, BLT, BLE, BLBS, BNE, BGE, BGT)
    0x30: "Direct", 0x31: "Direct", 0x32: "Direct", 0x33: "Direct",
    0x34: "Direct", 0x35: "Direct", 0x36: "Direct", 0x37: "Direct",
    0x38: "Direct", 0x39: "Direct", 0x3A: "Direct", 0x3B: "Direct",
    0x3C: "Direct", 0x3D: "Direct", 0x3E: "Direct", 0x3F: "Direct",
}

# Sub-table sizes for dispatch kinds that use dense arrays.  Empty
# slots are filled with kOpcDecEntry so an unmapped sub-decode still
# reaches a valid (OPCDEC) executor.  Misc and Pal kinds are sparse
# and use lookup helpers instead of dense tables.
SUBTABLE_SIZES: Dict[str, int] = {
    "IntArith":   128,    # 7-bit func [11:5]
    "IntLogical": 128,
    "IntShift":   128,
    "IntMul":     128,
    "ItFp":       2048,   # 11-bit func [15:5]: ITOFx are low, but SQRTS/SQRTT
                          # (0x08B/0x0AB) and their variants need the full width
    "FltVax":    2048,    # 11-bit func [15:5]: VAX F/G/D variants, full width
    "FltIeee":   2048,    # 11-bit func [15:5]; populated densely by FP variants
    "FltLogical": 128,    # 11-bit func, but only values <0x80 used in 21264
    "JmpClass":     4,    # 2-bit selector [15:14]
    "FpTiExt":    128,    # 7-bit func
}

# Box value -> C++ enum literal (Box::Ibox, Box::Ebox, ...).  Identity
# mapping in this project; kept as a function for clarity at use site.
def boxEnumLiteral(box: str) -> str:
    return f"Box::{box}"

# Pretty-printed name per category for emitted mask identifiers and
# section comments.
CATEGORY_PRETTY: Dict[str, str] = {
    "format":    "Format",
    "operand":   "Operand",
    "regfile":   "Regfile",
    "memory":    "Memory",
    "control":   "Control",
    "privilege": "Privilege",
    "fptrap":    "FpTrap",
    "ipr":       "Ipr",
    "barrier":   "Barrier",
    "hint":      "Hint",
}


# ----------------------------------------------------------------------------
# Data model
# ----------------------------------------------------------------------------

@dataclass
class FlagDef:
    bit: int
    name: str
    category: str
    stages: List[str]
    description: str


@dataclass
class MutexGroup:
    name: str
    cardinality: str   # "exactly_one" or "at_most_one"
    members: List[str] = field(default_factory=list)


@dataclass
class GrainRow:
    mnemonic: str
    primaryOpcode: int
    subDecode: Optional[int]
    dispatchKind: str
    format: str
    box: str
    flags: List[str]
    notes: str = ""
    # When set, overrides leaf-name computation: all rows sharing a
    # leafBase value collapse to a single leaf function.  Used for FP
    # rounding/trap modifier variants where many encoding sub-decodes
    # share one base operation (e.g., MULT, MULT_C, MULT_SUID all map
    # to leafBase=MULT and collapse to execMult).  When empty, the
    # leaf name derives from mnemonic via pascalCase.
    leafBase: str = ""


# ----------------------------------------------------------------------------
# TSV reader
# ----------------------------------------------------------------------------

def readTsv(path: Path) -> Tuple[List[str], List[List[str]], List[str]]:
    """
    Read a TSV file with '#' comment lines and tolerant blank lines.
    Returns (header, rows, trailerComments).  Comments before the
    column header are discarded; comments appearing after at least
    one data row are returned for downstream parsing (e.g., mutex
    groups in SemanticFlags.tsv).
    """
    header: List[str] = []
    rows: List[List[str]] = []
    trailer: List[str] = []
    sawHeader = False
    sawAnyData = False

    with path.open("r", encoding="ascii") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line.strip():
                continue
            if line.startswith("#"):
                if sawAnyData:
                    trailer.append(line)
                continue
            cells = [c.strip() for c in line.split("\t")]
            if not sawHeader:
                header = cells
                sawHeader = True
            else:
                rows.append(cells)
                sawAnyData = True

    return header, rows, trailer


# ----------------------------------------------------------------------------
# SemanticFlags.tsv parser
# ----------------------------------------------------------------------------

def parseSemanticFlags(path: Path) -> Tuple[Dict[str, FlagDef], List[MutexGroup]]:
    header, rows, trailer = readTsv(path)
    expected = ["bit", "name", "category", "stages", "description"]
    if header != expected:
        die("SemanticFlags.tsv header mismatch:\n"
            "  expected: " + "\t".join(expected) + "\n"
            "  actual:   " + "\t".join(header))

    flags: Dict[str, FlagDef] = {}
    bitsUsed: Dict[int, str] = {}
    for row in rows:
        if len(row) != 5:
            die(f"SemanticFlags row has {len(row)} cells, expected 5: {row}")
        try:
            bit = int(row[0])
        except ValueError:
            die(f"SemanticFlags row bit not an integer: {row[0]}")
        name = row[1]
        category = row[2]
        stages = [s.strip() for s in row[3].split(",") if s.strip()]
        description = row[4]
        if bit < 0 or bit > 63:
            die(f"flag {name} bit {bit} out of range 0..63")
        if bit in bitsUsed:
            die(f"flag {name} bit {bit} collides with {bitsUsed[bit]}")
        if name in flags:
            die(f"flag {name} duplicate definition")
        if not name.startswith("S_"):
            die(f"flag name {name} must start with S_")
        flags[name] = FlagDef(bit, name, category, stages, description)
        bitsUsed[bit] = name

    groups = parseMutexTrailer(trailer)
    for g in groups:
        for m in g.members:
            if m not in flags:
                die(f"mutex group '{g.name}' references undefined flag {m}")
    return flags, groups


def parseMutexTrailer(trailer: List[str]) -> List[MutexGroup]:
    """
    Walk the trailer comment block looking for mutex group definitions.
    Recognized form:

        # group-name-mutex
        #   Exactly one of: A B C
        # group-name-2-mutex
        #   At most one of: D E

    Continuation lines are appended to the current group only when
    every whitespace-separated token on the line is a clean S_xxx
    identifier; this rejects explanatory prose that happens to mention
    flag names.  Sections labeled 'Stage consumers' or 'Reserved bits'
    or beginning a 'personality-flags' note end mutex parsing.
    """
    groups: List[MutexGroup] = []
    inSection = False
    cur: Optional[MutexGroup] = None

    def flush() -> None:
        nonlocal cur
        if cur is not None:
            groups.append(cur)
            cur = None

    for raw in trailer:
        text = raw.lstrip("#").strip()
        if "Mutex groups" in raw:
            inSection = True
            continue
        if not inSection:
            continue
        if "Stage consumers" in raw or "Reserved bits" in raw \
                or "personality-flags" in text:
            flush()
            inSection = False
            continue
        if text.endswith("-mutex"):
            flush()
            cur = MutexGroup(name=text, cardinality="", members=[])
            continue
        if cur is None:
            continue
        lower = text.lower()
        if lower.startswith("exactly one of"):
            cur.cardinality = "exactly_one"
            tail = text.split(":", 1)[1] if ":" in text else ""
            cur.members.extend(t for t in tail.split() if _FLAG_RE.match(t))
        elif lower.startswith("at most one of"):
            cur.cardinality = "at_most_one"
            tail = text.split(":", 1)[1] if ":" in text else ""
            cur.members.extend(t for t in tail.split() if _FLAG_RE.match(t))
        elif cur.cardinality:
            # Treat as a member-list continuation only when every token
            # on the line is a clean S_xxx identifier; this filters out
            # explanatory prose that happens to mention flag names in
            # parentheses or other punctuation.
            tokens = text.split()
            if tokens and all(_FLAG_RE.match(t) for t in tokens):
                cur.members.extend(tokens)
    flush()
    return groups


# ----------------------------------------------------------------------------
# GrainMaster.tsv parser
# ----------------------------------------------------------------------------

def parseGrainMaster(path: Path) -> List[GrainRow]:
    if not path.exists():
        return []
    header, rows, _ = readTsv(path)
    if not rows:
        return []
    needed = {"mnemonic", "primaryOpcode", "dispatchKind",
              "format", "box", "flags"}
    missing = needed - set(header)
    if missing:
        die(f"GrainMaster.tsv missing required columns: {sorted(missing)}")
    idx = {name: i for i, name in enumerate(header)}
    grains: List[GrainRow] = []
    for row in rows:
        if len(row) < len(header):
            row = row + [""] * (len(header) - len(row))
        mnemonic = row[idx["mnemonic"]]
        try:
            primary = int(row[idx["primaryOpcode"]], 0)
        except ValueError:
            die(f"GrainMaster row {mnemonic}: primaryOpcode not an int")
        sub_raw = row[idx["subDecode"]] if "subDecode" in idx else ""
        sub: Optional[int] = None
        if sub_raw:
            try:
                sub = int(sub_raw, 0)
            except ValueError:
                die(f"GrainMaster row {mnemonic}: subDecode not an int")
        kind = row[idx["dispatchKind"]]
        fmt = row[idx["format"]]
        box = row[idx["box"]]
        flags_raw = row[idx["flags"]]
        flags = [t.strip() for t in flags_raw.split("|") if t.strip()]
        notes = row[idx["notes"]] if "notes" in idx else ""
        leafBase = row[idx["leafBase"]] if "leafBase" in idx else ""
        grains.append(GrainRow(
            mnemonic=mnemonic,
            primaryOpcode=primary,
            subDecode=sub,
            dispatchKind=kind,
            format=fmt,
            box=box,
            flags=flags,
            notes=notes,
            leafBase=leafBase,
        ))
    return grains


# ----------------------------------------------------------------------------
# Validation
# ----------------------------------------------------------------------------

def validate(flags: Dict[str, FlagDef],
             groups: List[MutexGroup],
             grains: List[GrainRow]) -> List[str]:
    errors: List[str] = []
    referenced: Set[str] = set()
    valid_kinds = set(CANONICAL_DISPATCH_KINDS)
    valid_formats = set(CANONICAL_FORMATS)
    valid_boxes = set(CANONICAL_BOXES)

    for g in grains:
        if g.dispatchKind not in valid_kinds:
            errors.append(
                f"grain {g.mnemonic} has unknown dispatchKind '{g.dispatchKind}'")
        if g.format not in valid_formats:
            errors.append(
                f"grain {g.mnemonic} has unknown format '{g.format}'")
        if g.box not in valid_boxes:
            errors.append(
                f"grain {g.mnemonic} has unknown box '{g.box}'")
        for fname in g.flags:
            if fname not in flags:
                errors.append(
                    f"grain {g.mnemonic} references undefined flag {fname}")
            referenced.add(fname)
        for grp in groups:
            present = [m for m in grp.members if m in g.flags]
            if grp.cardinality == "exactly_one" and len(present) != 1:
                errors.append(
                    f"grain {g.mnemonic} violates {grp.name}: "
                    f"needs exactly one of {grp.members}, has {present}")
            elif grp.cardinality == "at_most_one" and len(present) > 1:
                errors.append(
                    f"grain {g.mnemonic} violates {grp.name}: "
                    f"at most one of {grp.members} permitted, has {present}")
    return errors


# ----------------------------------------------------------------------------
# Reporting
# ----------------------------------------------------------------------------

def report(flags: Dict[str, FlagDef],
           groups: List[MutexGroup],
           grains: List[GrainRow]) -> None:
    print(f"SemanticFlags: {len(flags)} flags defined across "
          f"{len(set(f.category for f in flags.values()))} categories")
    cats: Dict[str, int] = {}
    for f in flags.values():
        cats[f.category] = cats.get(f.category, 0) + 1
    for c in sorted(cats):
        print(f"  {c:12s} {cats[c]:3d}")
    print(f"Mutex groups: {len(groups)}")
    for g in groups:
        print(f"  {g.name:20s} ({g.cardinality:12s}) "
              f"{len(g.members)} members")
    print(f"GrainMaster rows: {len(grains)}")
    if grains:
        kinds: Dict[str, int] = {}
        for g in grains:
            kinds[g.dispatchKind] = kinds.get(g.dispatchKind, 0) + 1
        for k in sorted(kinds):
            print(f"  {k:14s} {kinds[k]:3d}")


# ----------------------------------------------------------------------------
# Emission helpers
# ----------------------------------------------------------------------------

def writeAtomic(path: Path, content: str) -> None:
    """Write content to path atomically: stage to a tmp sibling then replace."""
    tmpPath = path.with_suffix(path.suffix + ".tmp")
    tmpPath.write_text(content, encoding="ascii")
    tmpPath.replace(path)


def headerBanner(filename: str, sourceTsv: str) -> str:
    return (
        "// ============================================================================\n"
        f"// {filename} -- generated for EmulatR V4\n"
        "// ============================================================================\n"
        "// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)\n"
        "// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.\n"
        "// Licensed under eNVy Systems Non-Commercial License v1.1\n"
        "//\n"
        "// Project Architect: Timothy Peer\n"
        "// AI Collaboration:  Claude (Anthropic)\n"
        "//\n"
        "// Commercial use prohibited without separate license.\n"
        "// Contact:        peert@envysys.com  |  https://envysys.com\n"
        "// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/\n"
        "// ============================================================================\n"
        "//\n"
        "// AUTO-GENERATED -- DO NOT EDIT.\n"
        f"// Source:    {sourceTsv}\n"
        "// Generator: grainFactoryLib/codegen/genGrains.py\n"
        "//\n"
        "// Edit the source TSV and re-run the generator.  Hand-edits to this\n"
        "// file will be lost on the next codegen pass.\n"
        "// ============================================================================\n"
    )


# ----------------------------------------------------------------------------
# Leaf naming helpers
# ----------------------------------------------------------------------------

def pascalCase(s: str) -> str:
    """ADDL -> Addl; HW_MFPR -> HwMfpr; LDQ_L -> LdqL; JSR_COROUTINE -> JsrCoroutine"""
    return "".join(part.capitalize() for part in s.split("_") if part)


def computeLeafName(g: GrainRow) -> str:
    """
    Leaf function name for a grain.  When g.leafBase is set, the leaf
    name derives from leafBase rather than mnemonic; this is how FP
    rounding/trap variants collapse to a single leaf (16 MULT variants
    all carry leafBase=MULT and produce execMult).  When g.leafBase is
    empty, the leaf name derives from mnemonic.  Convention: 'exec' +
    pascalCase(name source).

    For PAL grains tagged with only one personality flag, appends
    _tru64 or _vms so two rows at the same (op, sub) pair (one per
    personality) produce distinct leaf names that the dispatch table
    populator can route to the correct PAL sub-table.

    Both personality flags set (encoding identical under both) and
    neither flag set (non-PAL grain) both yield no suffix.
    """
    nameSource = g.leafBase if g.leafBase else g.mnemonic
    base = "exec" + pascalCase(nameSource)
    hasTru64 = "S_PalTru64" in g.flags
    hasVms = "S_PalVms" in g.flags
    if hasTru64 and not hasVms:
        return base + "_tru64"
    if hasVms and not hasTru64:
        return base + "_vms"
    return base


# ----------------------------------------------------------------------------
# SemanticFlagsEnum.h emitter
# ----------------------------------------------------------------------------

def emitSemanticFlagsEnum(flags: Dict[str, FlagDef], outDir: Path) -> Path:
    by_cat: Dict[str, List[FlagDef]] = {}
    for f in flags.values():
        by_cat.setdefault(f.category, []).append(f)
    for cat in by_cat:
        by_cat[cat].sort(key=lambda f: f.bit)
    cat_order = sorted(by_cat.keys(),
                       key=lambda c: min(f.bit for f in by_cat[c]))

    out: List[str] = []
    out.append(headerBanner("SemanticFlagsEnum.h",
                            "grainFactoryLib/SemanticFlags.tsv"))
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace grainFactory {")
    out.append("")
    out.append("// Bit-defined semantic flags carried on each InstructionGrain.")
    out.append("// One enum value per row in SemanticFlags.tsv; bit number is")
    out.append("// the authoritative source of truth for cross-module ABI.")
    out.append("enum class GrainSem : uint64_t {")
    out.append("    None         = 0ULL,")
    out.append("")

    for cat in cat_order:
        pretty = CATEGORY_PRETTY.get(cat, cat)
        out.append(f"    // -- {pretty} --")
        for f in by_cat[cat]:
            stub = f"    {f.name:18s} = 1ULL << {f.bit:2d},"
            out.append(f"{stub:48s}// {f.description}")
        out.append("")

    out.append("};")
    out.append("")

    out.append("// Bitwise operators on GrainSem.  enum class requires explicit")
    out.append("// definition before it can be used as a bit-flag set.")
    for op in ['|', '&', '^']:
        out.append(f"constexpr GrainSem operator{op}(GrainSem a, GrainSem b) {{")
        out.append("    return static_cast<GrainSem>(")
        out.append(f"        static_cast<uint64_t>(a) {op} static_cast<uint64_t>(b));")
        out.append("}")
    out.append("constexpr GrainSem operator~(GrainSem a) {")
    out.append("    return static_cast<GrainSem>(~static_cast<uint64_t>(a));")
    out.append("}")
    for op in ['|', '&']:
        out.append(f"constexpr GrainSem& operator{op}=(GrainSem& a, GrainSem b) {{")
        out.append(f"    a = a {op} b;")
        out.append("    return a;")
        out.append("}")
    out.append("")

    out.append("// Predicates.")
    out.append("constexpr bool any(GrainSem a) {")
    out.append("    return static_cast<uint64_t>(a) != 0ULL;")
    out.append("}")
    out.append("constexpr bool has(GrainSem mask, GrainSem bit) {")
    out.append("    return any(mask & bit);")
    out.append("}")
    out.append("")

    out.append("// Per-category masks; OR of every flag in the category.")
    for cat in cat_order:
        pretty = CATEGORY_PRETTY.get(cat, cat)
        names = [f.name for f in by_cat[cat]]
        if not names:
            continue
        mask_name = f"k{pretty}Mask"
        if len(names) <= 3:
            joined = " | ".join(f"GrainSem::{n}" for n in names)
            out.append(f"constexpr GrainSem {mask_name} = {joined};")
        else:
            out.append(f"constexpr GrainSem {mask_name} =")
            for i, n in enumerate(names):
                sep = " |" if i < len(names) - 1 else ";"
                out.append(f"    GrainSem::{n}{sep}")
    out.append("")

    out.append("} // namespace grainFactory")
    out.append("")

    path = outDir / "SemanticFlagsEnum.h"
    writeAtomic(path, "\n".join(out))
    return path


# ----------------------------------------------------------------------------
# DispatchKinds.h emitter
# ----------------------------------------------------------------------------

def emitDispatchKinds(grains: List[GrainRow], outDir: Path) -> Path:
    out: List[str] = []
    out.append(headerBanner("DispatchKinds.h",
                            "grainFactoryLib/codegen/genGrains.py "
                            "(canonical list)"))
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace grainFactory {")
    out.append("")
    out.append("// Dispatch-kind discriminator for the primary opcode table.")
    out.append("// Each value names a sub-table layout that the dispatcher uses")
    out.append("// to extract the secondary decode field from the 32-bit")
    out.append("// instruction word.  See GrainMasterV4.tsv schema docs for the")
    out.append("// per-kind sub-decode width and bit position.")
    out.append("enum class DispatchKind : uint8_t {")
    for k in CANONICAL_DISPATCH_KINDS:
        out.append(f"    {k},")
    out.append("};")
    out.append("")

    out.append("// Returns the canonical name of a DispatchKind value.")
    out.append("constexpr char const* dispatchKindName(DispatchKind k) {")
    out.append("    switch (k) {")
    for k in CANONICAL_DISPATCH_KINDS:
        out.append(f"        case DispatchKind::{k}: return \"{k}\";")
    out.append("    }")
    out.append("    return \"<invalid>\";")
    out.append("}")
    out.append("")

    out.append("} // namespace grainFactory")
    out.append("")

    path = outDir / "DispatchKinds.h"
    writeAtomic(path, "\n".join(out))
    return path


# ----------------------------------------------------------------------------
# GrainsForward.h emitter
# ----------------------------------------------------------------------------

def emitGrainsForward(grains: List[GrainRow],
                      handwritten: Set[str],
                      outDir: Path) -> Path:
    """
    Emit GrainsForward.h: per-namespace forward declarations of every
    leaf function referenced by the dispatch tables.  Grouped by box
    in CANONICAL_BOXES order.  Leaf names are computed from mnemonic
    via computeLeafName(); each declaration carries a trailing comment
    from the GrainMasterV4.tsv notes column.

    Implementations live under {box}Lib/grains/ and are linked into
    the project.  If a row exists in GrainMasterV4.tsv but no
    implementation has been provided, the link fails on that leaf --
    the intended structural guarantee that every TSV row has a real
    implementation.

    Synthetic hand-written leaves: handwritten.tsv may list leaves
    that are NOT per-instruction grains in GrainMaster (e.g.
    palBox::execCallPalDispatch, a generic CALL_PAL dispatcher used as
    the default arm of lookupPalTru64 / lookupPalVms).  Such leaves
    have no GrainMaster row but still need a forward declaration
    visible to the dispatch tables and any test files.  This function
    appends them to the matching namespace block after the
    GrainMaster-derived rows.
    """
    by_box: Dict[str, List[GrainRow]] = {}
    for g in grains:
        by_box.setdefault(g.box, []).append(g)

    # Build the set of leaves the GrainMaster-derived emission produces,
    # so we can compute the synthetic-leaf complement of handwritten.tsv.
    grainmaster_leaves: Set[str] = set()
    for g in grains:
        ns = BOX_TO_NAMESPACE.get(g.box)
        if ns is not None:
            grainmaster_leaves.add(f"{ns}::{computeLeafName(g)}")

    # Index synthetic hand-written leaves (in handwritten.tsv but not
    # in GrainMaster) by namespace so we can append them to the matching
    # box block.
    synthetic_by_ns: Dict[str, List[str]] = {}
    for qualified in handwritten:
        if qualified in grainmaster_leaves:
            continue
        if "::" not in qualified:
            continue
        ns, leaf = qualified.split("::", 1)
        synthetic_by_ns.setdefault(ns, []).append(leaf)

    out: List[str] = []
    out.append(headerBanner("GrainsForward.h",
                            "grainFactoryLib/GrainMasterV4.tsv"))
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include \"coreLib/BoxResult.h\"")
    out.append("#include \"coreLib/ExecCtx.h\"")
    out.append("#include \"coreLib/InstructionGrain.h\"")
    out.append("#include \"coreLib/axp_attributes_core.h\"")
    out.append("")
    out.append("// Forward declarations of every leaf function referenced by")
    out.append("// the codegen-emitted dispatch tables.  Hand-written leaf")
    out.append("// implementations live under {box}Lib/grains/ and resolve at")
    out.append("// link time.  A missing implementation surfaces as a linker")
    out.append("// error naming the unresolved leaf -- the structural guarantee")
    out.append("// that every GrainMasterV4.tsv row has a corresponding body.")
    out.append("")

    for box_col in CANONICAL_BOXES:
        if box_col not in by_box:
            continue
        ns = BOX_TO_NAMESPACE[box_col]
        rows = sorted(
            by_box[box_col],
            key=lambda r: (r.primaryOpcode,
                           r.subDecode if r.subDecode is not None else 0,
                           r.mnemonic)
        )

        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(f"// {box_col} -- namespace {ns}")
        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(f"namespace {ns} {{")
        out.append("")
        out.append("using coreLib::BoxResult;")
        out.append("using coreLib::ExecCtx;")
        out.append("using coreLib::InstructionGrain;")
        out.append("")

        emitted: Set[str] = set()
        for g in rows:
            leaf_name = computeLeafName(g)
            if leaf_name in emitted:
                continue
            emitted.add(leaf_name)

            note = g.notes.strip() if g.notes else g.mnemonic
            out.append(f"// {g.mnemonic}: {note}")
            out.append("AXP_HOT AXP_FLATTEN")
            out.append(
                f"BoxResult {leaf_name}(InstructionGrain const& g, "
                f"ExecCtx const& c) noexcept;")
            out.append("")

        # Append synthetic hand-written leaves declared in handwritten.tsv
        # for this namespace but absent from GrainMaster (e.g.
        # execCallPalDispatch in palBox).
        for leaf in sorted(synthetic_by_ns.get(ns, [])):
            if leaf in emitted:
                continue
            emitted.add(leaf)
            out.append(f"// {leaf}: synthetic hand-written leaf "
                       f"(handwritten.tsv only; no GrainMaster row)")
            out.append("AXP_HOT AXP_FLATTEN")
            out.append(
                f"BoxResult {leaf}(InstructionGrain const& g, "
                f"ExecCtx const& c) noexcept;")
            out.append("")

        out.append(f"}} // namespace {ns}")
        out.append("")

    path = outDir / "GrainsForward.h"
    writeAtomic(path, "\n".join(out))
    return path


# ----------------------------------------------------------------------------
# DispatchTables.cpp emitter
# ----------------------------------------------------------------------------

def formatFlagsExpr(flags: List[str]) -> str:
    """Format a flag-name list as an OR'd C++ expression usable inside the
    grainFactory namespace block (where GrainSem is unqualified)."""
    if not flags:
        return "GrainSem::None"
    return " | ".join(f"GrainSem::{f}" for f in flags)


def grainEntryInitializer(g: GrainRow) -> str:
    """Render a single GrainEntry initializer for use in dispatch tables."""
    ns = BOX_TO_NAMESPACE[g.box]
    leaf = computeLeafName(g)
    flags_expr = formatFlagsExpr(g.flags)
    box_lit = boxEnumLiteral(g.box)
    return (f"{{ &{ns}::{leaf}, {flags_expr}, {box_lit}, "
            f"\"{g.mnemonic}\" }}")


def emitDispatchTables(grains: List[GrainRow],
                       handwritten: Set[str],
                       outDir: Path) -> Path:
    """
    Emit DispatchTables.cpp: the populated primary table, per-kind
    dense sub-tables, sparse Misc / Pal lookup helpers, and the
    OPCDEC sentinel definition.  References leaf functions declared
    in GrainsForward.h via the box namespaces.

    Loose-link path: this file is emitted but not added to the
    Emulatr or Emulatr_tests targets until enough leaves exist to
    satisfy the linker.  The pipeline driver and dispatch consumers
    can read the tables symbolically (e.g., for tests of the
    structure) without invoking the leaves until then.
    """
    # Bucket rows by dispatch role.
    direct_by_op: Dict[int, GrainRow] = {}
    subtable_by_kind: Dict[str, Dict[int, GrainRow]] = {}
    misc_rows: List[GrainRow] = []
    pal_rows_tru64: Dict[int, GrainRow] = {}
    pal_rows_vms: Dict[int, GrainRow] = {}

    direct_kinds = {"Direct", "HwMfpr", "HwLd", "HwMtpr", "HwRei", "HwSt"}

    for g in grains:
        kind = g.dispatchKind
        if kind in direct_kinds:
            if g.primaryOpcode in direct_by_op:
                die(f"primary opcode 0x{g.primaryOpcode:02x} has multiple "
                    f"Direct/Hw rows: {direct_by_op[g.primaryOpcode].mnemonic} "
                    f"and {g.mnemonic}")
            direct_by_op[g.primaryOpcode] = g
        elif kind == "Misc":
            misc_rows.append(g)
        elif kind == "Pal":
            sub = g.subDecode if g.subDecode is not None else 0
            if "S_PalTru64" in g.flags:
                if sub in pal_rows_tru64:
                    die(f"PAL Tru64 sub-decode 0x{sub:04x} appears twice: "
                        f"{pal_rows_tru64[sub].mnemonic} and {g.mnemonic}")
                pal_rows_tru64[sub] = g
            if "S_PalVms" in g.flags:
                if sub in pal_rows_vms:
                    die(f"PAL VMS sub-decode 0x{sub:04x} appears twice: "
                        f"{pal_rows_vms[sub].mnemonic} and {g.mnemonic}")
                pal_rows_vms[sub] = g
        elif kind in SUBTABLE_SIZES:
            sub = g.subDecode if g.subDecode is not None else 0
            slot = subtable_by_kind.setdefault(kind, {})
            if sub in slot:
                die(f"{kind} sub-decode 0x{sub:04x} appears twice: "
                    f"{slot[sub].mnemonic} and {g.mnemonic}")
            slot[sub] = g
        elif kind == "Reserved":
            pass  # nothing to dispatch; primary table holds OPCDEC sentinel
        else:
            die(f"unhandled dispatchKind '{kind}' for {g.mnemonic}")

    out: List[str] = []
    out.append(headerBanner("DispatchTables.cpp",
                            "grainFactoryLib/GrainMasterV4.tsv"))
    out.append("")
    out.append("#include \"coreLib/DispatchEntry.h\"")
    out.append("#include \"grainFactoryLib/generated/GrainsForward.h\"")
    out.append("#include \"grainFactoryLib/generated/SemanticFlagsEnum.h\"")
    out.append("#include \"grainFactoryLib/generated/DispatchKinds.h\"")
    out.append("")

    # OPCDEC sentinel definition (declaration in DispatchEntry.h).
    out.append("namespace coreLib {")
    out.append("")
    out.append("// OPCDEC sentinel: empty slots in every dense dispatch table")
    out.append("// reference this single entry so an unmapped (op, sub) pair")
    out.append("// reaches the OPCDEC executor rather than dispatching into")
    out.append("// garbage.")
    out.append("GrainEntry const kOpcDecEntry = {")
    out.append("    &coreLib::execOpcDec,")
    out.append("    grainFactory::GrainSem::S_OpcDec,")
    out.append("    Box::PalBox,")
    out.append("    \"OPCDEC\"")
    out.append("};")
    out.append("")
    out.append("} // namespace coreLib")
    out.append("")

    # Tables and helpers in grainFactory namespace.
    out.append("namespace grainFactory {")
    out.append("")
    out.append("using coreLib::Box;")
    out.append("using coreLib::GrainEntry;")
    out.append("using coreLib::PrimaryEntry;")
    out.append("using coreLib::kOpcDecEntry;")
    out.append("")

    # Forward-declare lookup helpers so PrimaryEntry initializers can
    # reference them (they are defined later in the same file).
    out.append("// Forward declarations for sparse-kind lookup helpers.")
    out.append("GrainEntry const* lookupMisc(uint32_t func) noexcept;")
    out.append("GrainEntry const* lookupPalTru64(uint32_t func) noexcept;")
    out.append("GrainEntry const* lookupPalVms(uint32_t func) noexcept;")
    out.append("")

    # Per-kind dense sub-tables.
    for kind in CANONICAL_DISPATCH_KINDS:
        if kind not in SUBTABLE_SIZES:
            continue
        size = SUBTABLE_SIZES[kind]
        rows_at_kind = subtable_by_kind.get(kind, {})
        var_name = f"g_{kind[0].lower()}{kind[1:]}SubTable"

        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(
            f"// {kind} sub-table: {size} entries, {len(rows_at_kind)} populated")
        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(f"GrainEntry const {var_name}[{size}] = {{")
        for i in range(size):
            row = rows_at_kind.get(i)
            if row is not None:
                out.append(f"    /* 0x{i:04x} */ {grainEntryInitializer(row)},")
            else:
                out.append(f"    /* 0x{i:04x} */ kOpcDecEntry,")
        out.append("};")
        out.append("")

    # Sparse Misc lookup.
    out.append(
        "// ---------------------------------------------------------------------------")
    out.append(
        "// Misc lookup (opcode 0x18; sparse 16-bit function field)")
    out.append(
        "// ---------------------------------------------------------------------------")
    out.append(
        "GrainEntry const* lookupMisc(uint32_t func) noexcept {")
    out.append("    switch (func) {")
    for row in sorted(misc_rows,
                      key=lambda r: r.subDecode if r.subDecode is not None else 0):
        sub = row.subDecode if row.subDecode is not None else 0
        out.append(f"        case 0x{sub:04x}: {{")
        out.append(f"            static GrainEntry const e = "
                   f"{grainEntryInitializer(row)};")
        out.append(f"            return &e;")
        out.append(f"        }}")
    out.append("        default: return &kOpcDecEntry;")
    out.append("    }")
    out.append("}")
    out.append("")

    # Sparse PAL lookups (per personality).
    #
    # Default-arm policy: if handwritten.tsv lists palBox::execCallPalDispatch,
    # the synthetic CALL_PAL generic dispatcher is emitted as the default
    # arm for both personalities (with the matching S_PalTru64 /
    # S_PalVms flag).  This catches every CALL_PAL function code not
    # pinned to a specific row and diverts into PALcode at palBase +
    # vector_offset per HRM 6.8.1.  Falls back to kOpcDecEntry if the
    # synthetic dispatcher is not present (older builds, minimal config).
    has_call_pal_dispatch = "palBox::execCallPalDispatch" in handwritten

    for personality_name, pal_rows in (("Tru64", pal_rows_tru64),
                                       ("Vms",   pal_rows_vms)):
        helper_name = f"lookupPal{personality_name}"
        personality_flag = (
            "GrainSem::S_PalTru64" if personality_name == "Tru64"
            else "GrainSem::S_PalVms"
        )
        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(
            f"// PAL lookup -- {personality_name} personality "
            f"({len(pal_rows)} entries"
            f"{' + synthetic dispatcher' if has_call_pal_dispatch else ''})")
        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(
            f"GrainEntry const* {helper_name}(uint32_t func) noexcept {{")
        out.append("    switch (func) {")
        for sub, row in sorted(pal_rows.items()):
            out.append(f"        case 0x{sub:04x}: {{")
            out.append(f"            static GrainEntry const e = "
                       f"{grainEntryInitializer(row)};")
            out.append(f"            return &e;")
            out.append(f"        }}")
        if has_call_pal_dispatch:
            out.append("        default: {")
            out.append(
                f"            // Synthetic CALL_PAL dispatcher (handwritten.tsv): "
                f"diverts unhandled function codes into PALcode at palBase + "
                f"vector_offset per HRM 6.8.1.")
            out.append(
                f"            static GrainEntry const e = "
                f"{{ &palBox::execCallPalDispatch, "
                f"GrainSem::S_PalFormat | GrainSem::S_PalEntry | "
                f"GrainSem::S_ChangesPC | GrainSem::S_Uncond | "
                f"{personality_flag}, "
                f"Box::PalBox, \"CALL_PAL\" }};")
            out.append(f"            return &e;")
            out.append(f"        }}")
        else:
            out.append("        default: return &kOpcDecEntry;")
        out.append("    }")
        out.append("}")
        out.append("")

    # Primary opcode table (64 entries).
    out.append(
        "// ---------------------------------------------------------------------------")
    out.append(
        "// Primary opcode table (64 entries, indexed by primary opcode [31:26])")
    out.append(
        "// ---------------------------------------------------------------------------")
    out.append("PrimaryEntry const g_primaryTable[64] = {")
    for op in range(64):
        kind = PRIMARY_KIND_MAP.get(op, "Reserved")

        if kind == "Reserved":
            out.append(f"    /* 0x{op:02x} */ {{ DispatchKind::Reserved, "
                       f"kOpcDecEntry, nullptr, 0, \"OPC{op:02x}\" }},")
            continue

        if kind == "Pal":
            out.append(f"    /* 0x{op:02x} */ {{ DispatchKind::Pal, "
                       f"kOpcDecEntry, nullptr, 0, \"CALL_PAL\" }},")
            continue

        if kind == "Misc":
            out.append(f"    /* 0x{op:02x} */ {{ DispatchKind::Misc, "
                       f"kOpcDecEntry, nullptr, 0, \"MISC\" }},")
            continue

        if kind in ("Direct", "HwMfpr", "HwLd", "HwMtpr", "HwRei", "HwSt"):
            row = direct_by_op.get(op)
            if row is not None:
                out.append(f"    /* 0x{op:02x} */ {{ DispatchKind::{kind}, "
                           f"{grainEntryInitializer(row)}, nullptr, 0, "
                           f"\"{row.mnemonic}\" }},")
            else:
                # Direct slot with no row (e.g., not yet implemented):
                # default to OPCDEC.
                out.append(f"    /* 0x{op:02x} */ {{ DispatchKind::{kind}, "
                           f"kOpcDecEntry, nullptr, 0, \"OPC{op:02x}\" }},")
            continue

        if kind in SUBTABLE_SIZES:
            var_name = f"g_{kind[0].lower()}{kind[1:]}SubTable"
            size = SUBTABLE_SIZES[kind]
            out.append(f"    /* 0x{op:02x} */ {{ DispatchKind::{kind}, "
                       f"kOpcDecEntry, {var_name}, {size}, "
                       f"\"{kind}\" }},")
            continue

        die(f"unhandled primary kind '{kind}' for opcode 0x{op:02x}")
    out.append("};")
    out.append("")

    # Cross-TU forwarder for the primary table.
    #
    # g_primaryTable above stays a TU-local symbol of DispatchTables.cpp;
    # callers reach the table through this function rather than via an
    # extern-array declaration.  Function symbols mangle unambiguously
    # under MSVC / permissive- with Qt AUTOMOC active, while bounded
    # arrays decay to pointer form on the importing side and produce
    # LNK2001 mismatches.  Same shape as the sparse helpers
    # lookupMisc / lookupPalTru64 / lookupPalVms which already cross
    # TUs cleanly.
    out.append(
        "// ---------------------------------------------------------------------------")
    out.append(
        "// primaryEntry -- cross-TU accessor for the primary opcode table.")
    out.append("//")
    out.append(
        "// g_primaryTable above is a TU-local symbol of this translation unit;")
    out.append(
        "// callers reach it through this function rather than through an")
    out.append(
        "// extern-array declaration.  Function symbols mangle unambiguously")
    out.append(
        "// under MSVC / permissive- with Qt AUTOMOC active, while bounded")
    out.append(
        "// arrays decay to pointer form on the importing side and produce")
    out.append(
        "// LNK2001 mismatches.  Same pattern used by the sparse helpers")
    out.append(
        "// lookupMisc / lookupPalTru64 / lookupPalVms which already cross")
    out.append("// TUs cleanly.")
    out.append("//")
    out.append(
        "// primaryOp is masked to 6 bits before indexing; callers do not")
    out.append("// have to mask first.")
    out.append(
        "// ---------------------------------------------------------------------------")
    out.append(
        "PrimaryEntry const& primaryEntry(uint8_t primaryOp) noexcept")
    out.append("{")
    out.append("    return g_primaryTable[primaryOp & 0x3Fu];")
    out.append("}")
    out.append("")

    out.append("} // namespace grainFactory")
    out.append("")

    path = outDir / "DispatchTables.cpp"
    writeAtomic(path, "\n".join(out))
    return path


# ----------------------------------------------------------------------------
# Hand-written leaf manifest
# ----------------------------------------------------------------------------

def readHandwrittenManifest(manifestPath: Path) -> Set[str]:
    """
    Read the hand-written leaf manifest -- one qualified leaf name per
    non-comment line, shape `<box-namespace>::<leaf-name>`.  Returns a
    set of qualified names.  Missing file is tolerated (returns empty
    set); the codegen still works, it just emits stubs for every leaf.
    """
    if not manifestPath.exists():
        return set()
    handwritten: Set[str] = set()
    with manifestPath.open("r", encoding="ascii") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            # Tolerate trailing comments after the qualified name.
            entry = line.split("#", 1)[0].strip()
            if entry:
                handwritten.add(entry)
    return handwritten


# ----------------------------------------------------------------------------
# GrainStubs.cpp emitter
# ----------------------------------------------------------------------------

def emitGrainStubs(grains: List[GrainRow],
                   handwritten: Set[str],
                   outDir: Path) -> Path:
    """
    Emit GrainStubs.cpp containing stub bodies for every leaf function
    declared in GrainsForward.h that is NOT already implemented by hand.
    The handwritten set names leaves to skip in qualified form (e.g.,
    "eBox::execAddl"); each skipped leaf is replaced with a one-line
    "see ..." comment in the emitted file.

    Each emitted stub returns a BoxResult with faultCode =
    kFaultUnimplemented so a dispatched call falls through to fault
    delivery rather than crashing on an unresolved symbol.  The stub
    file lets the linker resolve every leaf reference in
    DispatchTables.cpp; once a leaf has a real definition under
    {boxLib}/grains/ AND its qualified name appears in
    grainFactoryLib/codegen/handwritten.tsv, the codegen stops
    emitting the stub and the linker resolves to the hand-written
    body uniquely.
    """
    by_box: Dict[str, List[GrainRow]] = {}
    for g in grains:
        by_box.setdefault(g.box, []).append(g)

    out: List[str] = []
    out.append(headerBanner("GrainStubs.cpp",
                            "grainFactoryLib/GrainMasterV4.tsv"))
    out.append("")
    out.append("#include \"coreLib/BoxResult.h\"")
    out.append("#include \"coreLib/ExecCtx.h\"")
    out.append("#include \"coreLib/InstructionGrain.h\"")
    out.append("#include \"coreLib/axp_attributes_core.h\"")
    out.append("")
    out.append("#include <atomic>")
    out.append("#include <cstdint>")
    out.append("#include <cstdio>")
    out.append("")
    out.append("// Stub bodies for every leaf declared in GrainsForward.h.")
    out.append("// Each stub returns BoxResult with faultCode =")
    out.append("// kFaultUnimplemented so dispatch reaches a fault delivery")
    out.append("// rather than crashing on an unresolved symbol.")
    out.append("//")
    out.append("// Replace stubs with real implementations under")
    out.append("// {boxLib}/grains/ and remove the stub from this file when")
    out.append("// the real definition lands.  Multiple definition error")
    out.append("// surfaces immediately if a stub is left behind.")
    out.append("//")
    out.append("// Each stub also announces itself on stderr the first few")
    out.append("// times it runs.  Reaching a stub means the firmware hit an")
    out.append("// instruction V4 has not implemented yet; the call returns")
    out.append("// kFaultUnimplemented and diverts into PALcode's fault vector.")
    out.append("// The announcement names the next leaf that needs a real body")
    out.append("// -- it turns trace archaeology into a direct read.")
    out.append("")
    out.append("namespace {")
    out.append("")
    out.append("using coreLib::ExecCtx;")
    out.append("using coreLib::InstructionGrain;")
    out.append("")
    out.append("// Throttled per-stub: each stub passes its own counter, so a")
    out.append("// hot stub spinning in a loop cannot mute a rarely-hit stub's")
    out.append("// first announcement.  First 8 hits loud, then a summary every")
    out.append("// 64K -- matches the CboxEventLog / MmioRegistry stderr posture.")
    out.append("void logUnimplementedStub(char const* mnem,")
    out.append("                          InstructionGrain const& g,")
    out.append("                          ExecCtx const& c,")
    out.append("                          std::atomic<uint64_t>& counter) noexcept")
    out.append("{")
    out.append("    uint64_t const n = counter.fetch_add(1, std::memory_order_relaxed);")
    out.append("    if (n < 8) {")
    out.append("        std::fprintf(stderr,")
    out.append("                     \"GrainStub: UNIMPLEMENTED %s pc=0x%016llx \"")
    out.append("                     \"encoded=0x%08x cyc=%llu (hit %llu)\\n\",")
    out.append("                     mnem,")
    out.append("                     static_cast<unsigned long long>(g.pc),")
    out.append("                     static_cast<unsigned>(g.encoded),")
    out.append("                     static_cast<unsigned long long>(c.cycleCount),")
    out.append("                     static_cast<unsigned long long>(n));")
    out.append("        std::fflush(stderr);")
    out.append("    } else if ((n & 0xFFFFu) == 0) {")
    out.append("        std::fprintf(stderr,")
    out.append("                     \"GrainStub: %s hit %llu times \"")
    out.append("                     \"(loud-stderr muted past 8)\\n\",")
    out.append("                     mnem,")
    out.append("                     static_cast<unsigned long long>(n + 1));")
    out.append("        std::fflush(stderr);")
    out.append("    }")
    out.append("}")
    out.append("")
    out.append("} // namespace")
    out.append("")

    for box_col in CANONICAL_BOXES:
        if box_col not in by_box:
            continue
        ns = BOX_TO_NAMESPACE[box_col]
        rows = sorted(
            by_box[box_col],
            key=lambda r: (r.primaryOpcode,
                           r.subDecode if r.subDecode is not None else 0,
                           r.mnemonic)
        )

        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(f"// {box_col} -- namespace {ns}")
        out.append(
            "// ---------------------------------------------------------------------------")
        out.append(f"namespace {ns} {{")
        out.append("")
        out.append("using coreLib::BoxResult;")
        out.append("using coreLib::ExecCtx;")
        out.append("using coreLib::InstructionGrain;")
        out.append("")

        emitted: Set[str] = set()
        for g in rows:
            leaf_name = computeLeafName(g)
            if leaf_name in emitted:
                continue
            emitted.add(leaf_name)

            qualified = f"{ns}::{leaf_name}"
            if qualified in handwritten:
                # Hand-written elsewhere; emit a pointer comment instead
                # of a conflicting stub.
                out.append(f"// {g.mnemonic}: hand-written -- "
                           f"see {ns}Lib/grains/ for {qualified}")
                out.append("")
                continue

            note = g.notes.strip() if g.notes else g.mnemonic
            out.append(f"// {g.mnemonic}: {note}")
            out.append("AXP_HOT AXP_FLATTEN")
            out.append(
                f"BoxResult {leaf_name}(InstructionGrain const& g, "
                f"ExecCtx const& c) noexcept")
            out.append("{")
            out.append("    static std::atomic<uint64_t> s_cnt{ 0 };")
            out.append(f"    logUnimplementedStub(\"{g.mnemonic}\", g, c, s_cnt);")
            out.append("    BoxResult r;")
            out.append("    r.semFlags  = g.semFlags;")
            out.append("    r.faultCode = coreLib::kFaultUnimplemented;")
            out.append("    return r;")
            out.append("}")
            out.append("")

        out.append(f"}} // namespace {ns}")
        out.append("")

    path = outDir / "GrainStubs.cpp"
    writeAtomic(path, "\n".join(out))
    return path


# ----------------------------------------------------------------------------
# Hand-written manifest guard (2026-06-05)
# ----------------------------------------------------------------------------

def validateHandwritten(grains: List[GrainRow],
                        handwritten: Set[str]) -> None:
    """
    Guard against the silent-stub class: SCBB (2026-05-29), LDQP/STQP +
    VPTB (2026-06-05).  When a GrainMaster row carries a single
    personality flag, computeLeafName() appends a _vms / _tru64 suffix.
    If handwritten.tsv still lists the UN-suffixed name, that entry
    matches no derived leaf, codegen silently treats it as a 'synthetic'
    leaf, emits a kFaultUnimplemented stub for the real row, and binds
    the stub -- an OPCDEC trap at runtime that masquerades as a hardware
    fault (cost us the vector-420 / show-config crash hunt).

    Detection signature (precise): a handwritten leaf that matches no
    derived leaf, but whose name is the un-suffixed stem of one that
    DOES exist (handwritten 'execLdqp' while derived is 'execLdqp_vms').
    Genuine synthetic leaves (e.g. execCallPalDispatch -- no GrainMaster
    row at all) have no suffixed cousin and pass clean.

    Hard error, not a warning: every historical instance was a real
    runtime-only bug, and a warning scrolls past unseen in codegen
    output (which is how all of them shipped).
    """
    grainmaster_leaves: Set[str] = set()
    for g in grains:
        ns = BOX_TO_NAMESPACE.get(g.box)
        if ns is not None:
            grainmaster_leaves.add(f"{ns}::{computeLeafName(g)}")

    for qualified in sorted(handwritten):
        if qualified in grainmaster_leaves:
            continue                       # legit override of a derived row
        if "::" not in qualified:
            continue
        ns, leaf = qualified.split("::", 1)
        suspects = sorted(gm for gm in grainmaster_leaves
                          if gm.startswith(f"{ns}::{leaf}_"))
        if suspects:
            die(f"handwritten.tsv leaf '{qualified}' matches no "
                f"GrainMaster-derived leaf, but suffixed variant(s) "
                f"exist: {suspects}.  Silent-stub trap: the derived row "
                f"would get a kFaultUnimplemented stub bound instead of "
                f"your hand-written body (OPCDEC at runtime).  Fix: rename "
                f"the handwritten.tsv entry to the suffixed form (add the "
                f"_vms / _tru64 personality suffix), or correct the "
                f"GrainMaster row's personality flags so the derived name "
                f"matches the handwritten one.")


# ----------------------------------------------------------------------------
# emit() driver
# ----------------------------------------------------------------------------

def emit(flags: Dict[str, FlagDef],
         groups: List[MutexGroup],
         grains: List[GrainRow],
         handwritten: Set[str],
         outDir: Path) -> None:
    outDir.mkdir(parents=True, exist_ok=True)
    # Silent-stub guard (2026-06-05): fail loudly before emitting if a
    # handwritten leaf lost its personality suffix.  See
    # validateHandwritten() for the bug history.
    validateHandwritten(grains, handwritten)
    p1 = emitSemanticFlagsEnum(flags, outDir)
    p2 = emitDispatchKinds(grains, outDir)
    p3 = emitGrainsForward(grains, handwritten, outDir)
    p4 = emitDispatchTables(grains, handwritten, outDir)
    p5 = emitGrainStubs(grains, handwritten, outDir)
    print(f"emit: wrote {p1}")
    print(f"emit: wrote {p2}")
    print(f"emit: wrote {p3}")
    print(f"emit: wrote {p4}")
    print(f"emit: wrote {p5}")
    if handwritten:
        print(f"emit: skipped {len(handwritten)} hand-written leaves "
              f"per handwritten.tsv")


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------

def die(msg: str) -> None:
    sys.stderr.write(f"genGrains: error: {msg}\n")
    sys.exit(1)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="GrainFactory codegen for EmulatR V4")
    ap.add_argument("--flags", type=Path, required=True,
                    help="path to SemanticFlags.tsv")
    ap.add_argument("--master", type=Path, required=True,
                    help="path to GrainMasterV4.tsv")
    ap.add_argument("--out", type=Path, required=True,
                    help="output directory for generated files")
    ap.add_argument("--validate-only", action="store_true",
                    help="parse and validate only; do not emit")
    args = ap.parse_args()

    if not args.flags.exists():
        die(f"flags file not found: {args.flags}")
    # GrainMaster may legitimately be absent in early scaffold; parser tolerates.

    flags, groups = parseSemanticFlags(args.flags)
    grains = parseGrainMaster(args.master)
    errors = validate(flags, groups, grains)
    report(flags, groups, grains)

    if errors:
        sys.stderr.write("\n")
        for e in errors:
            sys.stderr.write(f"genGrains: error: {e}\n")
        sys.exit(2)

    if not args.validate_only:
        # Hand-written manifest lives next to this script.  Tolerated
        # when missing -- the codegen still works, just emits stubs
        # for every leaf.
        manifestPath = Path(__file__).parent / "handwritten.tsv"
        handwritten = readHandwrittenManifest(manifestPath)
        emit(flags, groups, grains, handwritten, args.out)


if __name__ == "__main__":
    main()
# end of genGrains.py
