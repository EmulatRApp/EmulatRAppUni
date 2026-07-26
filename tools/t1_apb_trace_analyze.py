#!/usr/bin/env python3
# ============================================================================
# tools/t1_apb_trace_analyze.py -- offline reader for a DIAG-PC retire trace
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY (JRN-SCSI-012 Sec 5.5 T1)
#   The APB walk is invoked THROUGH a static dispatch table (VA 0x20063820),
#   so there are no static BSR sites to find and control flow can only be read
#   from a RETIRE TRACE.  tools/analyze_retire_trace.py parses the
#   TRACE_RETIRE_COMPACT "RET ..." format; the retire-time DIAG facility in
#   pipelineLib/PipelineDriver.h emits a DIFFERENT line:
#
#     DIAG-PC: cyc=<d> pc=0x<h> enc=0x<h> pal=<d> fault=<d> memAddr=0x<h> excAddr=0x<h>
#
#   This tool reads that form, decodes `enc` with tools/alpha_disasm.py, and
#   reconstructs DYNAMIC control flow -- including indirect calls -- by pairing
#   each record with the NEXT retired PC.  Because the DIAG window is a PC
#   range, any excursion outside it (a console CRB callback, a PAL trap) shows
#   up as a GAP, which is itself evidence: that is how the %APB-F-NOIOVEC
#   emission is spotted -- the walk's caller starts calling out to the console.
#
# MEMORY: a full-APB T1 trace is ~15M records / ~2 GB.  Every mode here is
#   STREAMING (single pass, bounded state): aggregates keep only per-PC
#   counters, listings keep a bounded deque.  Never loads the file.
#
# MODES (combine freely; default = --summary)
#   --summary            record count, cycle span, unique PCs, hottest PCs
#   --calls              dynamic call sites (BSR / JSR) -> actual targets
#   --targets            entry points: PCs reached by a call, with hit counts
#   --gaps [--gap-min N] excursions out of the traced window (>= N cycles)
#   --branches           conditional branches: taken / not-taken counts
#   --invocations PC[,PC...]   hit count + cycle of every visit to those PCs
#   --tail N             annotated listing of the last N records
#   --head N             annotated listing of the first N records
#   --range LO HI        annotated listing of records with LO <= pc <= HI
#   --around PC[,PC] [--ctx N] [--max-hits N]  listing around visits to PC
#   --from-cyc C / --to-cyc C   restrict the whole pass to a cycle window
#   --profile            per-4KB-page record counts (where the time goes)
#
# Usage:
#   python tools/t1_apb_trace_analyze.py <trace.txt> --summary --calls --gaps
#   python tools/t1_apb_trace_analyze.py <trace.txt> --tail 400
#   python tools/t1_apb_trace_analyze.py <trace.txt> --around 0x2000e974 --ctx 30
# ============================================================================

import argparse
import os
import re
import sys
from collections import Counter, defaultdict, deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from alpha_disasm import dis                      # noqa: E402

LINE = re.compile(
    r"DIAG-PC:\s+cyc=(\d+)\s+pc=0x([0-9a-f]+)\s+enc=0x([0-9a-f]+)\s+"
    r"pal=(\d+)\s+fault=(\d+)\s+memAddr=0x([0-9a-f]+)\s+excAddr=0x([0-9a-f]+)"
)

OP_BSR = 0x34
OP_BR = 0x30
OP_JSR = 0x1a
COND_BR = set(range(0x30, 0x40)) - {OP_BR, OP_BSR}


class Rec(object):
    __slots__ = ('cyc', 'pc', 'enc', 'pal', 'fault', 'mem', 'exc')

    def __init__(self, cyc, pc, enc, pal, fault, mem, exc):
        self.cyc, self.pc, self.enc = cyc, pc, enc
        self.pal, self.fault, self.mem, self.exc = pal, fault, mem, exc


def stream(path, from_cyc, to_cyc):
    """Yield Rec objects in file order.  Never materializes the file."""
    with open(path, 'r', encoding='ascii', errors='replace') as f:
        for raw in f:
            if 'DIAG-PC:' not in raw:
                continue
            m = LINE.search(raw)
            if m is None:
                continue
            cyc = int(m.group(1))
            if cyc < from_cyc or cyc > to_cyc:
                continue
            yield Rec(cyc, int(m.group(2), 16), int(m.group(3), 16),
                      int(m.group(4)), int(m.group(5)),
                      int(m.group(6), 16), int(m.group(7), 16))


def opcode(enc):
    return enc >> 26


def is_call(r):
    """BSR with a real link register, or JSR proper (op 0x1a, sub 1)."""
    o = opcode(r.enc)
    if o == OP_BSR:
        return ((r.enc >> 21) & 31) != 31
    if o == OP_JSR:
        return ((r.enc >> 14) & 3) == 1        # 0=JMP 1=JSR 2=RET 3=COROUTINE
    return False


def is_ret(r):
    return opcode(r.enc) == OP_JSR and ((r.enc >> 14) & 3) == 2


def fmt(r, nxt=None):
    tag = ''
    if nxt is not None:
        seq = (nxt.pc == r.pc + 4)
        o = opcode(r.enc)
        if o in COND_BR:
            tag = '   <TAKEN>' if not seq else '   <not taken>'
        elif is_call(r):
            tag = '   --> call 0x%x' % nxt.pc
        elif is_ret(r):
            tag = '   <-- ret to 0x%x' % nxt.pc
        elif not seq:
            tag = '   -> 0x%x' % nxt.pc
        if nxt.cyc - r.cyc > 64:
            tag += '   [gap %d cyc]' % (nxt.cyc - r.cyc)
    flags = ''
    if r.fault:
        flags += ' fault=%d' % r.fault
    if r.pal:
        flags += ' pal=1'
    if r.mem:
        flags += ' mem=0x%x' % r.mem
    return 'cyc=%-12d 0x%08x  %08x  %-34s%s%s' % (
        r.cyc, r.pc, r.enc, dis(r.enc), flags, tag)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--summary', action='store_true')
    ap.add_argument('--calls', action='store_true')
    ap.add_argument('--targets', action='store_true')
    ap.add_argument('--gaps', action='store_true')
    ap.add_argument('--gap-min', type=int, default=1000)
    ap.add_argument('--branches', action='store_true')
    ap.add_argument('--profile', action='store_true')
    ap.add_argument('--invocations', default='')
    ap.add_argument('--tail', type=int, default=0)
    ap.add_argument('--head', type=int, default=0)
    ap.add_argument('--range', nargs=2, default=None)
    ap.add_argument('--around', default='')
    ap.add_argument('--ctx', type=int, default=20)
    ap.add_argument('--max-hits', type=int, default=8)
    ap.add_argument('--from-cyc', type=int, default=0)
    ap.add_argument('--to-cyc', type=int, default=(1 << 62))
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args()

    if not any([a.summary, a.calls, a.targets, a.gaps, a.branches, a.profile,
                a.tail, a.head, a.range, a.around, a.invocations]):
        a.summary = True

    want_around = [int(x, 0) for x in a.around.split(',')] if a.around else []
    want_inv = [int(x, 0) for x in a.invocations.split(',')] if a.invocations \
        else []
    rng = (int(a.range[0], 0), int(a.range[1], 0)) if a.range else None

    # --- streaming state ----------------------------------------------------
    n = 0
    first = last = None
    pc_hits = Counter()
    pc_enc = {}
    page_hits = Counter()
    call_sites = Counter()
    call_tgts = Counter()
    br_stat = defaultdict(lambda: [0, 0])
    gaps = []
    inv_hits = defaultdict(list)
    tail_buf = deque(maxlen=a.tail + 1) if a.tail else None
    head_buf = []
    ctx_buf = deque(maxlen=a.ctx + 1) if want_around else None
    around_out = []              # list of (pc, [lines])
    pending = []                 # active "collect N more" windows
    faults = 0

    prev = None
    for r in stream(a.trace, a.from_cyc, a.to_cyc):
        n += 1
        if first is None:
            first = r
        if r.fault:
            faults += 1

        if a.summary or a.top:
            pc_hits[r.pc] += 1
            if r.pc not in pc_enc:
                pc_enc[r.pc] = r.enc
        if a.profile:
            page_hits[r.pc & ~0xFFF] += 1
        if want_inv and r.pc in want_inv:
            inv_hits[r.pc].append(r.cyc)

        if prev is not None:
            if a.calls and is_call(prev):
                call_sites[(prev.pc, r.pc)] += 1
            if a.targets and is_call(prev):
                call_tgts[r.pc] += 1
            if a.branches and opcode(prev.enc) in COND_BR:
                br_stat[prev.pc][0 if r.pc != prev.pc + 4 else 1] += 1
            if a.gaps and (r.cyc - prev.cyc) >= a.gap_min:
                gaps.append((prev, r.cyc - prev.cyc, r))

        if tail_buf is not None:
            tail_buf.append(r)
        if a.head and len(head_buf) < a.head + 1:
            head_buf.append(r)
        if rng and rng[0] <= r.pc <= rng[1]:
            print('  ' + fmt(prev if False else r, None))

        if ctx_buf is not None:
            # emit for any window still collecting trailing context
            for w in pending:
                w['lines'].append(fmt(prev, r) if prev is not None else fmt(r))
                w['left'] -= 1
            pending = [w for w in pending if w['left'] > 0]
            if r.pc in want_around and \
                    sum(1 for x in around_out if x[0] == r.pc) < a.max_hits:
                lines = ['  ---- hit @ cyc=%d ----' % r.cyc]
                buf = list(ctx_buf)
                for i, b in enumerate(buf):
                    nx = buf[i + 1] if i + 1 < len(buf) else r
                    lines.append('  ' + fmt(b, nx))
                w = {'left': a.ctx, 'lines': lines}
                pending.append(w)
                around_out.append((r.pc, lines))
            ctx_buf.append(r)

        prev = r
        last = r

    if n == 0:
        print('no DIAG-PC records in %s' % a.trace)
        return 1

    if a.summary:
        print('== SUMMARY ==')
        print('  records   : %d' % n)
        print('  cyc span  : %d .. %d  (delta %d)'
              % (first.cyc, last.cyc, last.cyc - first.cyc))
        print('  pc span   : 0x%x .. 0x%x' % (min(pc_hits), max(pc_hits)))
        print('  unique pc : %d' % len(pc_hits))
        print('  faults    : %d records with fault!=0' % faults)
        print('  first     : ' + fmt(first))
        print('  last      : ' + fmt(last))
        print('  hottest PCs:')
        for pc, c in pc_hits.most_common(a.top):
            print('    0x%08x  %9d  %s' % (pc, c, dis(pc_enc[pc])))

    if a.profile:
        print('== PROFILE (records per 4KB page) ==')
        for pg, c in page_hits.most_common(a.top):
            print('    0x%08x  %9d  (%5.1f%%)' % (pg, c, 100.0 * c / n))

    if a.calls:
        print('== DYNAMIC CALL SITES (site -> target) ==  %d distinct'
              % len(call_sites))
        for (site, tgt), c in call_sites.most_common(a.top):
            print('    0x%08x -> 0x%08x  %8d  %s'
                  % (site, tgt, c, dis(pc_enc.get(site, 0))))

    if a.targets:
        print('== CALL TARGETS (entry points) ==  %d distinct' % len(call_tgts))
        for tgt, c in call_tgts.most_common(a.top):
            print('    0x%08x  %8d' % (tgt, c))

    if a.gaps:
        print('== GAPS (out-of-window excursions >= %d cyc) ==  %d found'
              % (a.gap_min, len(gaps)))
        for g, d, nx in gaps[:400]:
            print('  cyc=%-12d 0x%08x %-30s  gap %9d cyc  -> 0x%08x'
                  % (g.cyc, g.pc, dis(g.enc), d, nx.pc))

    if want_inv:
        print('== INVOCATIONS ==')
        for pc in want_inv:
            hits = inv_hits.get(pc, [])
            print('  0x%08x : %d hit(s)' % (pc, len(hits)))
            for c in hits[:200]:
                print('      cyc=%d' % c)

    if a.branches:
        print('== CONDITIONAL BRANCHES (pc: taken/not-taken) ==')
        for pc in sorted(br_stat):
            t, nt = br_stat[pc]
            print('    0x%08x  taken=%-9d not=%-9d  %s'
                  % (pc, t, nt, dis(pc_enc.get(pc, 0))))

    if want_around:
        for pc, lines in around_out:
            print('== AROUND 0x%x ==' % pc)
            for ln in lines:
                print(ln)

    if a.head:
        print('== HEAD %d ==' % a.head)
        for i, r in enumerate(head_buf[:a.head]):
            nx = head_buf[i + 1] if i + 1 < len(head_buf) else None
            print('  ' + fmt(r, nx))

    if tail_buf is not None:
        print('== TAIL %d ==' % a.tail)
        buf = list(tail_buf)
        for i, r in enumerate(buf):
            nx = buf[i + 1] if i + 1 < len(buf) else None
            print('  ' + fmt(r, nx))
    return 0


if __name__ == '__main__':
    sys.exit(main())
