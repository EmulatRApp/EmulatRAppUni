#!/usr/bin/env python3
# ============================================================================
# gen_fp_leaves.py -- emit ONE distinct fBox leaf per FP opcode.func row
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# PURPOSE: per the architect's "distinct leaf per opcode.func" decision, emit a
#   separate hand-written-style leaf function for EVERY floating-point dispatch
#   row that currently has no leafBase (i.e. is stubbed).  Each leaf decodes its
#   qualifier from the encoding at runtime (fpVariantFromEncoded) and calls the
#   IFpBackend op for its operation, then folds raw flags into the FPCR.
#
#   Leaf name == codegen convention: "exec" + PascalCase(mnemonic), underscores
#   as word breaks (CVTTS_C -> execCvttsC).  Matches genGrains.py computeLeafName.
#
# OUTPUTS (review the diff before committing):
#   - fBoxLib/grains/FloatVariants.cpp  : the generated leaf bodies.
#   - appends the qualified names to codegen/handwritten.tsv (fBox::/eBox::).
#
# BODY by base operation:
#   IEEE arith   ADD/SUB/MUL/DIV {S,T}  -> addS/subS/.../divT (binary, opA,opB)
#   IEEE compare CMPT{EQ,LT,LE,UN}      -> cmpT(kind) (binary)
#   IEEE convert CVT{TS,TQ,QS,QT,ST}    -> cvtTS/TQ/QS/QT/ST (unary, opB)
#   IEEE sqrt    SQRT{S,T}              -> sqrtS/sqrtT (unary, opB)
#   VAX (F/G/D) + SQRTF/G              -> TODO(fp-vax) stub: IFpBackend has no VAX
#                                          kernels yet (extend it, then re-gen).
#   CVTLQ/CVTQL, FCMOVxx, FBxx, ITOFF  -> TODO stub (bit-reformat / select /
#                                          branch / int-move; custom, follow-up).
#
# USAGE:
#   python gen_fp_leaves.py --master ../GrainMasterV4.tsv \
#       --handwritten handwritten.tsv --out ../../fBoxLib/grains/FloatVariants.cpp
# ============================================================================

import argparse


def pascal(mnem):
    return "".join(p.capitalize() for p in mnem.split("_") if p)


# base op (qualifier stripped) -> (kind, backend-call-or-None)
# kind: 'binA' (binary arith), 'cmp', 'cvt', 'sqrt', 'stub'
def classify(mnem):
    base = mnem.split("_")[0]  # row names use _ for the qualifier slash
    BIN = {"ADDS": "addS", "SUBS": "subS", "MULS": "mulS", "DIVS": "divS",
           "ADDT": "addT", "SUBT": "subT", "MULT": "mulT", "DIVT": "divT"}
    CVT = {"CVTTS": "cvtTS", "CVTTQ": "cvtTQ", "CVTQS": "cvtQS",
           "CVTQT": "cvtQT", "CVTST": "cvtST"}
    SQRT = {"SQRTS": "sqrtS", "SQRTT": "sqrtT"}
    CMP = {"CMPTEQ": "Eq", "CMPTLT": "Lt", "CMPTLE": "Le", "CMPTUN": "Un"}
    if base in BIN:
        return ("binA", BIN[base])
    if base in CVT:
        return ("cvt", CVT[base])
    if base in SQRT:
        return ("sqrt", SQRT[base])
    if base in CMP:
        return ("cmp", CMP[base])
    # VAX F/G float (backend kernels in fp_backend_softfloat.cpp).
    VAXBIN = {"ADDF": "addF", "SUBF": "subF", "MULF": "mulF", "DIVF": "divF",
              "ADDG": "addG", "SUBG": "subG", "MULG": "mulG", "DIVG": "divG"}
    VAXSQRT = {"SQRTF": "sqrtF", "SQRTG": "sqrtG"}
    VAXCVT = {"CVTGF": "cvtGF", "CVTGD": "cvtGD", "CVTDG": "cvtDG",
              "CVTGQ": "cvtGQ", "CVTQF": "cvtQF", "CVTQG": "cvtQG"}
    VAXCMP = {"CMPGEQ": "Eq", "CMPGLT": "Lt", "CMPGLE": "Le"}
    if base in VAXBIN:
        return ("binA", VAXBIN[base])
    if base in VAXSQRT:
        return ("sqrt", VAXSQRT[base])
    if base in VAXCVT:
        return ("cvt", VAXCVT[base])
    if base in VAXCMP:
        return ("cmpG", VAXCMP[base])
    return ("stub", None)


FCMOV_COND = {"FCMOVEQ": "== 0.0", "FCMOVNE": "!= 0.0", "FCMOVLT": "< 0.0",
              "FCMOVGE": ">= 0.0", "FCMOVLE": "<= 0.0", "FCMOVGT": "> 0.0"}
FBRANCH = {"FBEQ": "== 0.0", "FBNE": "!= 0.0", "FBLT": "< 0.0",
           "FBLE": "<= 0.0", "FBGE": ">= 0.0", "FBGT": "> 0.0"}


def custom_body(name, mnem):
    """Deterministic leaves needing no backend: CVTLQ/CVTQL (bit reformat,
    AARM 4.10.x formulas) and FCMOVxx (conditional move, no write on false)."""
    base = mnem.split("_")[0]
    head = (f"AXP_HOT AXP_FLATTEN\n"
            f"auto {name}(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult\n{{\n")
    if base == "CVTLQ":
        # Fc = SEXT(Fb<63:62> || Fb<58:29>)  -- longword (FP-reg form) -> quad
        return (head +
                "    uint64_t const fb = c.opB;\n"
                "    uint32_t const lw = static_cast<uint32_t>((((fb >> 62) & 0x3ULL) << 30)\n"
                "                                            | ((fb >> 29) & 0x3FFFFFFFULL));\n"
                "    return fpWrite(g, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(lw))));\n}\n")
    if base == "CVTQL":
        # Fc = Fb<31:30> || 0<2:0> || Fb<29:0> || 0<28:0>  -- quad -> longword (FP-reg form)
        return (head +
                "    uint64_t const fb = c.opB;\n"
                "    uint64_t const r = (((fb >> 30) & 0x3ULL) << 62) | ((fb & 0x3FFFFFFFULL) << 29);\n"
                "    return fpWrite(g, r);\n}\n")
    if base in FCMOV_COND:
        cond = FCMOV_COND[base]
        return (head +
                f"    // FP conditional move: Fc <- Fb when Fa {cond}, else Fc unchanged.\n"
                f"    double const fa = std::bit_cast<double>(c.opA);\n"
                f"    if (fa {cond}) return fpWrite(g, c.opB);\n"
                f"    BoxResult r; r.semFlags = g.semFlags; r.regWriteIdx = coreLib::kNoRegWrite; return r;\n}}\n")
    if base in FBRANCH:
        cond = FBRANCH[base]
        return (head +
                f"    // FP conditional branch: divert to PC+4 + SEXT(disp21)*4 when Fa {cond}.\n"
                f"    double const fa = std::bit_cast<double>(c.opA);\n"
                f"    int64_t const disp = (static_cast<int64_t>(static_cast<int32_t>((g.encoded & 0x1FFFFFu) << 11)) >> 11) * 4;\n"
                f"    bool const taken = (fa {cond});\n"
                f"    BoxResult r; r.semFlags = g.semFlags;\n"
                f"    r.divert = taken;\n"
                f"    r.divertTarget = taken ? (g.pc + 4 + static_cast<uint64_t>(disp)) : 0;\n"
                f"    return r;\n}}\n")
    return None


def body(name, mnem):
    cb = custom_body(name, mnem)
    if cb is not None:
        return cb
    kind, op = classify(mnem)
    head = (f"AXP_HOT AXP_FLATTEN\n"
            f"auto {name}(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult\n{{\n")
    if kind == "stub":
        return (head +
                f"    // TODO(fp-leaf): {mnem} -- backend/custom semantics not yet wired\n"
                f"    //   (VAX F/G needs IFpBackend VAX kernels; FCMOV/CVTLQ/CVTQL/FBxx/ITOFF\n"
                f"    //   need custom bit/branch bodies). Emitted as a distinct leaf per the\n"
                f"    //   no-consolidation decision; returns 0 until the body lands.\n"
                f"    (void)c;\n    return fpWrite(g, 0);\n}}\n")
    ctx = "    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };\n"
    if kind == "binA":
        call = f"    fpBox::FpResult const r = fpBox::activeBackend().{op}(c.opA, c.opB, ctx);\n"
    elif kind == "cmp":
        call = f"    fpBox::FpResult const r = fpBox::activeBackend().cmpT(fpBox::FpCompare::{op}, c.opA, c.opB, ctx);\n"
    elif kind == "cmpG":
        call = f"    fpBox::FpResult const r = fpBox::activeBackend().cmpG(fpBox::FpCompare::{op}, c.opA, c.opB, ctx);\n"
    else:  # cvt / sqrt: unary, source Fb
        call = f"    fpBox::FpResult const r = fpBox::activeBackend().{op}(c.opB, ctx);\n"
    return (head + ctx + call +
            "    foldFpcrExc(c.cpu->fpcr, r.exc);\n    return fpWrite(g, r.bits);\n}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--master", required=True)
    ap.add_argument("--handwritten", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    # Split handwritten.tsv at the generated-section marker so re-runs are
    # idempotent: base_lines = the hand-written (Float.cpp) leaves; the generated
    # section below is rewritten wholesale, never appended.
    MARKER = "# --- generated distinct FP leaves (gen_fp_leaves.py) ---"
    base_lines = []
    for _l in open(a.handwritten, encoding="latin-1"):
        if _l.rstrip("\n") == MARKER:
            break
        base_lines.append(_l.rstrip("\n"))
    have = set(l.strip() for l in base_lines if "::" in l)

    leaves = []   # (qualified, name, mnem)
    for ln in open(a.master, encoding="latin-1"):
        if ln.startswith("#") or not ln.strip():
            continue
        f = ln.rstrip("\n").split("\t")
        if len(f) < 7 or not f[1].startswith("0x"):
            continue
        op = int(f[1], 16)
        if op not in (0x14, 0x15, 0x16, 0x17, 0x31, 0x32, 0x33, 0x35, 0x36, 0x37):
            continue
        if len(f) >= 9 and f[8].strip():
            continue                    # has leafBase -> shared leaf, skip
        mnem, box = f[0], f[5]
        name = "exec" + pascal(mnem)
        ns = "eBox" if box == "Ebox" else "fBox"
        q = f"{ns}::{name}"
        if q in have:
            continue                    # already hand-written
        leaves.append((q, name, mnem, ns))

    # Emit only the fBox leaves into FloatVariants.cpp (eBox stubs, if any, are
    # reported for the eBox file). Group by namespace.
    fbox = [x for x in leaves if x[3] == "fBox"]
    ebox = [x for x in leaves if x[3] == "eBox"]

    hdr = ('// ============================================================================\n'
           '// fBoxLib/grains/FloatVariants.cpp -- generated distinct FP leaves (1 per\n'
           '// opcode.func). GENERATED by codegen/gen_fp_leaves.py -- edit the generator,\n'
           '// not this file. Copyright (C) 2025, 2026 eNVy Systems, Inc.\n'
           '// ============================================================================\n'
           '#include "coreLib/BoxResult.h"\n#include "coreLib/ExecCtx.h"\n'
           '#include "coreLib/InstructionGrain.h"\n#include "coreLib/axp_attributes_core.h"\n'
           '#include "coreLib/CpuState.h"\n#include "fBoxLib/grains/FpExec.h"\n'
           '#include "fpBoxLib/fp_backend.h"\n#include "fpBoxLib/fp_backend_active.h"\n'
           '#include <bit>\n#include <cstdint>\n\n'
           'namespace fBox {\n'
           'using coreLib::BoxResult; using coreLib::ExecCtx; using coreLib::InstructionGrain;\n\n'
           '// Local copy of Float.cpp\'s fpWrite (that one has internal linkage). Commits a\n'
           '// 64-bit value to FP register Fc (encoded[4:0]) and propagates semantic flags.\n'
           'namespace {\n'
           'AXP_HOT AXP_FLATTEN\n'
           'BoxResult fpWrite(InstructionGrain const& g, uint64_t value) noexcept {\n'
           '    BoxResult r;\n'
           '    r.semFlags      = g.semFlags;\n'
           '    r.regWriteIdx   = static_cast<uint8_t>(g.encoded & 0x1Fu);\n'
           '    r.regWriteIsFp  = true;\n'
           '    r.regWriteValue = value;\n'
           '    return r;\n'
           '}\n'
           '}  // anonymous namespace\n\n')
    with open(a.out, "w", encoding="latin-1") as o:
        o.write(hdr)
        for q, name, mnem, ns in fbox:
            o.write(f"// {mnem}\n" + body(name, mnem) + "\n")
        o.write("}  // namespace fBox\n")

    # Rewrite handwritten.tsv = base (hand-written) + marker + generated names.
    with open(a.handwritten, "w", encoding="latin-1") as o:
        for line in base_lines:
            o.write(line + "\n")
        o.write(MARKER + "\n")
        for q, name, mnem, ns in fbox:   # fBox only; eBox ITOF leaves need eBoxLib bodies
            o.write(q + "\n")

    real = sum(1 for _, _, m, _ in fbox if classify(m)[0] != "stub")
    print(f"fBox leaves: {len(fbox)} ({real} real backend-wired, {len(fbox)-real} TODO stubs)")
    print(f"eBox leaves (ITOFF etc., NOT emitted here): {len(ebox)} -> {[x[1] for x in ebox]}")
    print(f"appended {len(leaves)} names to {a.handwritten}")


if __name__ == "__main__":
    main()
