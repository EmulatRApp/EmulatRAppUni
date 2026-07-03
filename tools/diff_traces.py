#!/usr/bin/env python3
# ============================================================================
# diff_traces.py -- normalize + compare two EmulatR trace logs.
# Project: EmulatR -- Alpha AXP / EV6 (V4).  2026-07-02.  ASCII(128) only.
# ----------------------------------------------------------------------------
# PURPOSE
#   Two runs (e.g. DS20 vs ES40) share the 21272/pc264 firmware path, so their
#   retired-instruction streams should match until a model-specific divergence
#   (chipset/south-bridge init, or the get_sysvar badge decision).  A raw diff
#   is useless because each line carries volatile fields (global retire ordinal,
#   RPCC, wall-clock, ephemeral TCP port) that differ every run.  This tool:
#     1. NORMALIZES each line -- strips the volatile fields so only the
#        deterministic content (pc=, opcode, PA, values) remains;
#     2. ALIGNS on the pc= control-flow sequence (or does a normalized line
#        diff), finds the FIRST real divergence, and reports it with context.
#
# INPUTS
#   Works on the machine.log / dec.log channels and on retire .trc files -- any
#   line-oriented EmulatR trace.  Default expectation: the *_machine.log channel
#   (PC / control-flow), which is the most meaningful cross-model comparison.
#
# USAGE
#   python tools/diff_traces.py A_machine.log B_machine.log
#   python tools/diff_traces.py A.trc B.trc --mode pc --context 8
#   python tools/diff_traces.py A B --mode line --max-lines 400 > diff.txt
#   python tools/diff_traces.py A B --keep rpcc,ord     # keep some fields
#   python tools/diff_traces.py A B --strip-dec-columns # also drop o<n>/cNN cols
#
# MODES
#   pc    (default) align the pc= sequences; report first divergent pc + the
#         retire index in each file + a window of surrounding normalized lines.
#   line  difflib unified diff of the normalized lines (classic side-by-side).
#
# EXIT CODE: 0 = streams identical after normalization; 1 = divergence found;
#            2 = usage/error.  (So it is script-friendly in a gate.)
# ============================================================================
import argparse
import difflib
import re
import sys

# Volatile key=value tokens stripped by default (case-insensitive on the key).
# These vary run-to-run and would swamp a diff.  pc / pa / opcode are KEPT.
DEFAULT_VOLATILE_KEYS = [
    "ord", "rpcc", "cyc", "cycle", "cycles", "wall", "wallclock",
    "ms", "millis", "elapsed", "port", "clientport", "tid", "heartbeat",
]

# Timestamp-ish patterns (PuTTY log headers, HH:MM:SS, [1234ms], ISO stamps).
TS_PATTERNS = [
    re.compile(r"\b\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?\b"),
    re.compile(r"\b\d{2}:\d{2}:\d{2}(?:\.\d+)?\b"),
    re.compile(r"\[\s*\d+\s*ms\s*\]"),
]

PC_RE = re.compile(r"\bpc=(0x[0-9a-fA-F]+)")


def build_key_re(keys):
    if not keys:
        return None
    # match  key=<token>  where token is a run of non-space chars
    alt = "|".join(re.escape(k) for k in keys)
    return re.compile(rf"\b(?:{alt})=\S+", re.IGNORECASE)


def normalize(line, key_re, strip_dec_columns):
    s = line.rstrip("\n")
    if strip_dec_columns:
        # DEC-listing leading ordinal column  o<digits>  and cpu column  c<digits>
        s = re.sub(r"^\s*o\d+\s+", "", s)
        s = re.sub(r"\bc\d+\b", "", s)
    if key_re:
        s = key_re.sub("", s)
    for p in TS_PATTERNS:
        s = p.sub("", s)
    # collapse whitespace so removed tokens do not leave ragged gaps
    s = re.sub(r"\s+", " ", s).strip()
    return s


def load(path, key_re, strip_dec_columns):
    raw, norm = [], []
    try:
        with open(path, "r", errors="replace") as fh:
            for ln in fh:
                raw.append(ln.rstrip("\n"))
                norm.append(normalize(ln, key_re, strip_dec_columns))
    except OSError as e:
        sys.stderr.write("FATAL: cannot read %s: %s\n" % (path, e))
        sys.exit(2)
    return raw, norm


def extract_pcs(norm_lines):
    """Return list of (line_index, pc_string) for lines carrying a pc=."""
    out = []
    for i, s in enumerate(norm_lines):
        m = PC_RE.search(s)
        if m:
            out.append((i, m.group(1)))
    return out


def mode_pc(a_raw, a_norm, b_raw, b_norm, args):
    a_pc = extract_pcs(a_norm)
    b_pc = extract_pcs(b_norm)
    if not a_pc or not b_pc:
        sys.stderr.write(
            "note: no pc= tokens found (%d in A, %d in B); "
            "fall back to --mode line.\n" % (len(a_pc), len(b_pc)))
        return mode_line(a_raw, a_norm, b_raw, b_norm, args)

    a_seq = [pc for _, pc in a_pc]
    b_seq = [pc for _, pc in b_pc]
    n = min(len(a_seq), len(b_seq))
    div = None
    for k in range(n):
        if a_seq[k] != b_seq[k]:
            div = k
            break

    print("=" * 70)
    print("PC-sequence comparison")
    print("  A: %s  (%d pc-lines)" % (args.file_a, len(a_seq)))
    print("  B: %s  (%d pc-lines)" % (args.file_b, len(b_seq)))
    print("=" * 70)

    if div is None and len(a_seq) == len(b_seq):
        print("IDENTICAL pc stream after normalization (%d retired PCs)." % n)
        return 0
    if div is None:
        print("Common prefix of %d PCs matches; streams differ only in LENGTH "
              "(A=%d, B=%d)." % (n, len(a_seq), len(b_seq)))
        return 1

    ai = a_pc[div][0]
    bi = b_pc[div][0]
    print("FIRST DIVERGENCE at retired-PC #%d:" % div)
    print("  A pc=%s  (line %d)" % (a_seq[div], ai + 1))
    print("  B pc=%s  (line %d)" % (b_seq[div], bi + 1))
    print("  common matched prefix: %d PCs" % div)
    ctx = args.context
    print("-" * 70)
    print("A context (normalized), lines %d..%d:" % (max(1, ai - ctx + 1), ai + ctx + 1))
    for j in range(max(0, ai - ctx), min(len(a_norm), ai + ctx + 1)):
        mark = ">>" if j == ai else "  "
        print("  %s A%6d | %s" % (mark, j + 1, a_norm[j]))
    print("-" * 70)
    print("B context (normalized), lines %d..%d:" % (max(1, bi - ctx + 1), bi + ctx + 1))
    for j in range(max(0, bi - ctx), min(len(b_norm), bi + ctx + 1)):
        mark = ">>" if j == bi else "  "
        print("  %s B%6d | %s" % (mark, j + 1, b_norm[j]))
    print("=" * 70)
    print("Interpret: the matched prefix is the shared firmware path; the first")
    print("divergent pc is where the two models part -- cross-reference it against")
    print("journals/ES40_significant_address_regions_checkpoint_ref_20260702.md")
    print("(get_sysvar ~0x7f5c0, chipset MMIO 0x801A.., smir 0x80130000040, etc).")
    return 1


def mode_line(a_raw, a_norm, b_raw, b_norm, args):
    print("=" * 70)
    print("Normalized unified line diff  (A=%s  B=%s)" % (args.file_a, args.file_b))
    print("=" * 70)
    diff = difflib.unified_diff(
        a_norm, b_norm,
        fromfile="A:" + args.file_a, tofile="B:" + args.file_b,
        n=args.context, lineterm="")
    emitted = 0
    diverged = False
    for line in diff:
        diverged = True
        print(line)
        emitted += 1
        if args.max_lines and emitted >= args.max_lines:
            print("... (truncated at --max-lines=%d)" % args.max_lines)
            break
    if not diverged:
        print("IDENTICAL after normalization (%d lines)." % len(a_norm))
        return 0
    return 1


def main():
    ap = argparse.ArgumentParser(
        description="Normalize and diff two EmulatR trace logs (DS20 vs ES40, etc).")
    ap.add_argument("file_a")
    ap.add_argument("file_b")
    ap.add_argument("--mode", choices=["pc", "line"], default="pc",
                    help="pc = align pc= sequences (default); line = normalized unified diff")
    ap.add_argument("--context", type=int, default=6, help="context lines (default 6)")
    ap.add_argument("--max-lines", type=int, default=500,
                    help="cap line-mode output (0 = unlimited; default 500)")
    ap.add_argument("--keep", default="",
                    help="comma list of otherwise-volatile keys to KEEP (e.g. rpcc,ord)")
    ap.add_argument("--strip-dec-columns", action="store_true",
                    help="also strip DEC-listing leading o<n> ordinal + cNN cpu columns")
    args = ap.parse_args()

    keep = {k.strip().lower() for k in args.keep.split(",") if k.strip()}
    keys = [k for k in DEFAULT_VOLATILE_KEYS if k not in keep]
    key_re = build_key_re(keys)

    a_raw, a_norm = load(args.file_a, key_re, args.strip_dec_columns)
    b_raw, b_norm = load(args.file_b, key_re, args.strip_dec_columns)

    if args.mode == "pc":
        rc = mode_pc(a_raw, a_norm, b_raw, b_norm, args)
    else:
        rc = mode_line(a_raw, a_norm, b_raw, b_norm, args)
    sys.exit(rc)


if __name__ == "__main__":
    main()
