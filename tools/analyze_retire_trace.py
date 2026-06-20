#!/usr/bin/env python3
# ============================================================================
# tools/analyze_retire_trace.py -- find dominant PC loops in a retire trace
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# ============================================================================
#
# Reads a TRACE_RETIRE_COMPACT trace file (line format documented in
# DecListingSink.h), counts hot PCs, and finds the dominant loop body
# in the tail of the file.  Output identifies which loop is consuming
# the bulk of cycles late in the run -- the candidate "wedge" pattern.
#
# Line format expected:
#   RET [cpu=<n> ]rpcc=<n> pc=<hex16> <mnem> pal=<0|1> exc=<hex16>[ R<dd>=<hex16>]*
#   (cpu= is the optional SMP slot tag; rpcc= was formerly cyc=, renamed
#    2026-06-20 -- it is the per-CPU PCC.  Both forms are accepted.)
#
# Usage:
#   python analyze_retire_trace.py <trace_file>
#   python analyze_retire_trace.py <trace_file> --tail 200000
#   python analyze_retire_trace.py <trace_file> --tail 0 --top 30
#
# Algorithm sketch:
#   1. Stream the file once, keeping every RET line's (cycle, pc, mnem).
#      Optional --tail N keeps only the last N records to bound memory.
#   2. Compute a Counter of pc -> visit count over the retained range.
#      Top hottest PCs are reported with their dominant mnemonic.
#   3. Scan for the dominant loop: for each candidate period P in
#      [1..max_period], slide through the retained records counting the
#      longest CONSECUTIVE run of repeated P-grams.  Score = run * P.
#      Highest-scoring (period, body) is reported.
#
# A real loop body emerges as a P-gram that repeats N times consecutively
# (each repeat lands at the same body offset).  Random hot PCs scattered
# across unrelated code paths will not.  This is the same pattern that
# manifests in the trace tail when the run wedges -- the last few thousand
# retires almost always cycle through one tight body.
#
# Notes on cost: O(n * max_period) worst case, but the inner skip-forward
# (jump past a detected run rather than re-scan within it) keeps real-trace
# runtime well under a second per million records on default settings.
# Default tail = 200,000 records is enough to surface the wedge body and
# bounds memory at ~50 MB.
# ============================================================================

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

# Match the RET line format emitted by DecListingSink::emitRetireCompact.
# Captures: cycle (rpcc), pc, mnemonic.  Anything after the mnemonic (pal,
# exc, register fields) is ignored -- this tool only needs the control-flow
# trace, not register state.  The optional `cpu=<n>` SMP slot tag and the
# `cyc=`->`rpcc=` rename (2026-06-20) are both tolerated; `cyc=` is still
# accepted so pre-rename trace files keep parsing.
RET_LINE = re.compile(
    r"RET\s+(?:ord=\d+\s+)?(?:cpu=\d+\s+)?(?:rpcc|cyc)=(\d+)\s+pc=([0-9a-fA-F]+)\s+(\S+)"
)


def parse_trace(path, tail_records):
    """
    Stream RET lines from `path`.  If tail_records > 0, keep only the
    last that many records (rolling buffer).  Returns parallel lists
    (cycles, pcs, mnems) ordered earliest-to-latest.
    """
    cycles = []
    pcs = []
    mnems = []
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for raw in f:
            if not raw.startswith("RET "):
                continue
            m = RET_LINE.match(raw)
            if m is None:
                continue
            cycles.append(int(m.group(1)))
            pcs.append(int(m.group(2), 16))
            mnems.append(m.group(3))
            if tail_records > 0 and len(cycles) > tail_records * 2:
                # Trim to the tail window.  Done in chunks (every 2x) so
                # we don't pay O(n) per insert on a list slice.
                cycles = cycles[-tail_records:]
                pcs = pcs[-tail_records:]
                mnems = mnems[-tail_records:]
    if tail_records > 0 and len(cycles) > tail_records:
        cycles = cycles[-tail_records:]
        pcs = pcs[-tail_records:]
        mnems = mnems[-tail_records:]
    return cycles, pcs, mnems


def first_mnem_per_pc(pcs, mnems):
    """Map each PC to the first mnemonic seen at that PC.  Used for the
    top-hot report; PCs are stable so first-seen is fine."""
    out = {}
    for pc, mn in zip(pcs, mnems):
        if pc not in out:
            out[pc] = mn
    return out


def find_dominant_loop(pcs, max_period):
    """
    Return (score, period, start_index, iters, body_tuple) for the
    longest consecutive repeating P-gram in `pcs`.  score = iters * P.
    Returns (0, None, None, 0, None) if nothing repeats.
    """
    best = (0, None, None, 0, None)
    n = len(pcs)
    if n < 2:
        return best
    pcs_list = list(pcs)
    for P in range(1, max_period + 1):
        if 2 * P > n:
            break
        i = 0
        while i + 2 * P <= n:
            body = pcs_list[i:i + P]
            iters = 1
            j = i + P
            # Walk forward as long as the next P-gram matches body.
            while j + P <= n and pcs_list[j:j + P] == body:
                iters += 1
                j += P
            if iters >= 2:
                score = iters * P
                if score > best[0]:
                    best = (score, P, i, iters, tuple(body))
            # Skip past the run we just measured -- whether it was a real
            # run or a single occurrence -- so we don't re-scan inside it.
            i = j if iters > 1 else i + 1
    return best


def fmt_pc(pc):
    return f"{pc:016x}"


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Find dominant PC loops in a V4 retire-compact trace."
    )
    ap.add_argument("trace", help="Path to retire-compact trace file (.trc)")
    ap.add_argument(
        "--tail", type=int, default=200000,
        help="Analyze only the last N RET records (default 200000; "
             "0 = whole file)"
    )
    ap.add_argument(
        "--top", type=int, default=15,
        help="Report top N hottest PCs (default 15)"
    )
    ap.add_argument(
        "--max-period", type=int, default=64,
        help="Max loop body length to consider (default 64)"
    )
    args = ap.parse_args(argv)

    path = Path(args.trace)
    if not path.exists():
        print(f"error: trace file not found: {path}", file=sys.stderr)
        return 1

    print(f"Reading {path} ...")
    cycles, pcs, mnems = parse_trace(path, args.tail)
    if not cycles:
        print("error: no RET lines parsed.", file=sys.stderr)
        return 1

    # ------------------------------------------------------------------
    # Summary stats.
    # ------------------------------------------------------------------
    print()
    print(f"Records analyzed:  {len(cycles):,}")
    print(f"Cycle range:       {cycles[0]:,} .. {cycles[-1]:,}")
    span = cycles[-1] - cycles[0]
    if span > 0:
        ipc = len(cycles) / span
        print(f"Avg retires/cycle: {ipc:.3f}")
    if args.tail > 0:
        print(f"(Analyzed tail of last {args.tail:,} records)")

    # ------------------------------------------------------------------
    # Hot PCs.
    # ------------------------------------------------------------------
    pc_count = Counter(pcs)
    pc_to_mnem = first_mnem_per_pc(pcs, mnems)
    print()
    print(f"Top {args.top} hottest PCs:")
    print(f"  {'pc':>16}  {'count':>10}  {'pct':>6}  mnemonic")
    print(f"  {'-' * 16}  {'-' * 10}  {'-' * 6}  --------")
    total = len(pcs)
    for pc, ct in pc_count.most_common(args.top):
        pct = 100.0 * ct / total
        print(f"  {fmt_pc(pc)}  {ct:>10,}  {pct:>5.1f}%  {pc_to_mnem.get(pc, '?')}")

    # ------------------------------------------------------------------
    # Dominant loop (longest consecutive repeating P-gram).
    # ------------------------------------------------------------------
    print()
    print(f"Searching for dominant loop body (max period = {args.max_period})...")
    score, period, start, iters, body = find_dominant_loop(pcs, args.max_period)
    if period is None or iters < 2:
        print("  No repeating P-gram with iters >= 2 found.")
        return 0

    end = start + period * iters
    cycles_consumed = cycles[end - 1] - cycles[start] if end <= len(cycles) else 0
    print(f"  Period (instructions): {period}")
    print(f"  Iteration count:       {iters:,}")
    print(f"  Retires in run:        {iters * period:,}")
    print(f"  Cycle span:            {cycles[start]:,} .. {cycles[end - 1]:,}")
    print(f"  Cycles consumed:       {cycles_consumed:,}")
    print()
    print("  Body:")
    print(f"    {'idx':>3}  {'pc':>16}  mnemonic")
    print(f"    {'-' * 3}  {'-' * 16}  --------")
    body_mnems = mnems[start:start + period]
    for i, (pc, mn) in enumerate(zip(body, body_mnems)):
        print(f"    {i:>3}  {fmt_pc(pc)}  {mn}")

    # ------------------------------------------------------------------
    # Hint: where in the file is the loop run located?
    # ------------------------------------------------------------------
    print()
    pct_into_window = 100.0 * start / max(1, len(pcs))
    print(f"  Run begins at record {start:,} of {len(pcs):,} "
          f"({pct_into_window:.1f}% into the analysis window)")
    if iters * period >= len(pcs) * 0.5:
        print("  *** This loop dominates the analyzed window; the run is "
              "almost certainly stuck (or strongly biased toward) here.")
    elif iters * period >= len(pcs) * 0.1:
        print("  *  Loop is significant but not dominant; the run may be "
              "progressing through nested phases.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
