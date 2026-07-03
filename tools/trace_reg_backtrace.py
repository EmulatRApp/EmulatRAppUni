#!/usr/bin/env python3
# ============================================================================
# trace_reg_backtrace.py -- data-flow backtrace of a register in an EmulatR
# retire trace.  Project: EmulatR -- Alpha AXP / EV6 (V4).  2026-07-02.
# ASCII(128) only.
# ----------------------------------------------------------------------------
# PURPOSE
#   "Wrong pointer from somewhere earlier" bugs (e.g. the ES40 kFaultAcv loop
#   at pc=0x1b7dd4 where R16 = 0xFFFFFFFF7F827F5F is garbage) are hard to pin
#   by eye.  Given a retire trace and a faulting register + PC, this walks
#   BACKWARD from the fault to the instruction that last WROTE that register,
#   then recurses on that instruction's SOURCE registers -- producing the
#   data-flow chain that computed the bad value, and from what inputs.
#
#   Single-trace, no reference needed -- robust to ES40-vs-DS20 binary
#   differences (unlike a cross-model trace-diff).
#
# INPUT
#   An EmulatR retire trace (the machine.log / dec channel or a .trc), lines
#   like:
#     INS ord=248655870 cpu=0 rpcc=... pc=00000000001b7500 instr=207bfff0 \
#         mnem=LDA ops="R03, 0xfff0(R27)" result="R03 = 0x00000000001b8780"
#
# USAGE
#   python tools/trace_reg_backtrace.py TRACE --reg R16 --pc 0x1b7dd4
#   python tools/trace_reg_backtrace.py TRACE --reg R16 --ord 282091103 --depth 6
#   python tools/trace_reg_backtrace.py TRACE --reg R16 --pc 0x1b7dd4 --occurrence last
#
# OPTIONS
#   --reg R16        register to backtrace (R0..R31; F regs too).  REQUIRED.
#   --pc 0x..        anchor at an instruction with this PC (the fault site).
#   --ord N          anchor at this retire ordinal instead of a PC.
#   --occurrence     first|last (default last) -- which matching PC to anchor on.
#   --depth N        recursion depth over source registers (default 5).
#   --max-back N     max instructions to scan backward per hop (default 5,000,000).
#
# OUTPUT
#   The writer of the target register, its source operands with their most
#   recent values, and (depth-limited) the writers of those sources -- a tree
#   showing where the value came from.  Exit 0 = writer found, 1 = not found.
# ============================================================================
import argparse
import re
import sys

INS_RE = re.compile(
    r"\bord=(\d+).*?\bpc=([0-9a-fA-F]+).*?\bmnem=(\S+).*?"
    r'\bops="([^"]*)".*?\bresult="([^"]*)"')
RESULT_REG_RE = re.compile(r"^\s*([RF]\d+)\s*=\s*(0x[0-9a-fA-F]+)")
REG_TOKEN_RE = re.compile(r"\b([RF]\d+)\b")


class Ins:
    __slots__ = ("idx", "ord", "pc", "mnem", "ops", "dst", "dstval", "srcs")

    def __init__(self, idx, o, pc, mnem, ops, dst, dstval, srcs):
        self.idx = idx; self.ord = o; self.pc = pc; self.mnem = mnem
        self.ops = ops; self.dst = dst; self.dstval = dstval; self.srcs = srcs


def parse(path):
    out = []
    idx = 0
    try:
        with open(path, "r", errors="replace") as fh:
            for line in fh:
                if "ord=" not in line or "result=" not in line:
                    continue
                m = INS_RE.search(line)
                if not m:
                    continue
                o = int(m.group(1))
                pc = int(m.group(2), 16)
                mnem = m.group(3)
                ops = m.group(4)
                result = m.group(5)
                dst, dstval = None, None
                rm = RESULT_REG_RE.match(result)
                if rm:
                    dst = rm.group(1)
                    dstval = int(rm.group(2), 16)
                # sources = reg tokens in ops minus the destination
                srcs = [r for r in REG_TOKEN_RE.findall(ops) if r != dst]
                out.append(Ins(idx, o, pc, mnem, ops, dst, dstval, srcs))
                idx += 1
    except OSError as e:
        sys.stderr.write("FATAL: cannot read %s: %s\n" % (path, e))
        sys.exit(2)
    return out


def find_anchor(records, pc, ordv, occurrence):
    if ordv is not None:
        best = None
        for r in records:
            if r.ord == ordv:
                return r.idx
            if r.ord > ordv and best is None:
                best = r.idx      # first at/after the ordinal
        return best
    # by PC
    hits = [r.idx for r in records if r.pc == pc]
    if not hits:
        return None
    return hits[-1] if occurrence == "last" else hits[0]


def last_writer(records, reg, before_idx, max_back):
    lo = max(0, before_idx - max_back)
    for i in range(before_idx - 1, lo - 1, -1):
        if records[i].dst == reg:
            return records[i]
    return None


def fmt(r):
    return ("ord=%d pc=0x%x %-7s ops=\"%s\" -> %s=0x%x"
            % (r.ord, r.pc, r.mnem, r.ops, r.dst,
               r.dstval if r.dstval is not None else 0))


def backtrace(records, reg, anchor_idx, depth, max_back, seen, indent=0):
    pad = "  " * indent
    w = last_writer(records, reg, anchor_idx, max_back)
    if w is None:
        print("%s%s <- (no writer found within --max-back before this point)" % (pad, reg))
        return
    print("%s%s written by: %s" % (pad, reg, fmt(w)))
    if depth <= 0:
        if w.srcs:
            print("%s  (sources: %s -- stop, --depth reached)" % (pad, ", ".join(w.srcs)))
        return
    for s in w.srcs:
        key = (s, w.idx)
        if key in seen:
            print("%s  %s <- (already shown)" % (pad, s))
            continue
        seen.add(key)
        backtrace(records, s, w.idx, depth - 1, max_back, seen, indent + 1)


def main():
    ap = argparse.ArgumentParser(description="Data-flow backtrace of a register in an EmulatR retire trace.")
    ap.add_argument("trace")
    ap.add_argument("--reg", required=True, help="register to backtrace, e.g. R16")
    ap.add_argument("--pc", help="anchor PC (hex), the fault site")
    ap.add_argument("--ord", type=int, dest="ordv", help="anchor retire ordinal (instead of --pc)")
    ap.add_argument("--occurrence", choices=["first", "last"], default="last")
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--max-back", type=int, default=5_000_000)
    args = ap.parse_args()

    if not args.pc and args.ordv is None:
        sys.stderr.write("error: give --pc or --ord to anchor the backtrace\n")
        sys.exit(2)
    pc = int(args.pc, 16) if args.pc else None

    records = parse(args.trace)
    if not records:
        sys.stderr.write("no INS records parsed -- is this an EmulatR retire trace with ops=/result= fields?\n")
        sys.exit(2)

    anchor = find_anchor(records, pc, args.ordv, args.occurrence)
    if anchor is None:
        where = ("pc=0x%x" % pc) if pc is not None else ("ord=%d" % args.ordv)
        sys.stderr.write("anchor %s not found in %d INS records "
                         "(ord range %d..%d)\n"
                         % (where, len(records), records[0].ord, records[-1].ord))
        sys.exit(1)

    a = records[anchor]
    print("=" * 72)
    print("Backtrace of %s at anchor: %s" % (args.reg, fmt(a)))
    print("  trace: %s  (%d INS records, ord %d..%d)"
          % (args.trace, len(records), records[0].ord, records[-1].ord))
    print("=" * 72)
    backtrace(records, args.reg, anchor, args.depth, args.max_back, set())
    print("=" * 72)
    print("Read up-to-down: each line is where the value above it came from.")
    print("The instruction that first introduces an obviously-wrong value")
    print("(bad base, off-by-N, wrong sign-extension) is the root.")


if __name__ == "__main__":
    main()
