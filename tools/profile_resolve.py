# ============================================================================
# profile_resolve.py -- annotate RetireProfiler dumps with SRM symbols
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
#
# Usage:  py profile_resolve.py <profile_*.txt> [more dumps...]
#
# Reads one or more traceLib/RetireProfiler.h dump files, labels each
# bucket with the best-known SRM/PAL symbol or region, and prints two
# tables per file: the labeled top buckets and a rollup by region.
#
# The symbol table below is hand-curated from live-trace work (SYSFAULT
# dumps, BreakpointSink captures, Ghidra ApplySrmSymbols sessions,
# 2026-05/06).  Ranges are [start, end) in PA space of the DS10 SRM
# V7.3 DECOMPRESSED image (base 0) plus the OS-PAL window at 0x8000 and
# the pre-takeover staging at 0x600000+.  Refine as more symbols land.
# ============================================================================

import sys

# (start, end, label) -- first match wins; keep sorted by start, most
# specific (narrow) entries before broad fallbacks within a region.
REGIONS = [
    # ---- OS PAL (VMS personality, palBase 0x8000) ----------------------
    (0x008100, 0x008180, "PAL:DTBM_DOUBLE_3 vector"),
    (0x008280, 0x008300, "PAL:UNALIGN vector"),
    (0x008380, 0x008400, "PAL:DFAULT vector"),
    (0x008400, 0x008480, "PAL:OPCDEC vector"),
    (0x008480, 0x008500, "PAL:IACV vector"),
    (0x008500, 0x008580, "PAL:MCHK vector"),
    (0x008580, 0x008600, "PAL:ITB_MISS vector"),
    (0x008600, 0x008680, "PAL:ARITH vector"),
    (0x008680, 0x008700, "PAL:INTERRUPT vector"),
    (0x008000, 0x00A000, "PAL:hw vectors/entry region"),
    (0x00A000, 0x00B000, "PAL:CALL_PAL dispatch (MTPR_IPL path 0xa3xx)"),
    (0x00B700, 0x00B800, "PAL:RSCC service (0xb74x)"),
    (0x00D500, 0x00D700, "PAL:interrupt dispatch (0xd5xx)"),
    (0x00D700, 0x00DA00, "PAL:int frame build (0xd79x-0xd8xx)"),
    (0x00DB00, 0x00DD00, "PAL:OPCDEC/trap frame build (0xdbxx-0xdcxx)"),
    (0x00E900, 0x00EA00, "PAL:MTPR_IPL handler (0xe9xx)"),
    (0x00F600, 0x00F800, "PAL:interrupt exit/IER table (0xf6xx-0xf7xx)"),
    (0x010F00, 0x011200, "PAL:sys__int_clk service (0x11xxx)"),
    (0x00B000, 0x01A000, "PAL:other OS-PAL"),

    # ---- SRM console kernel + shell (decompressed image, base 0) ------
    (0x044500, 0x044600, "yy_reset (shell prompt setup)"),
    (0x05A600, 0x05A700, "file layer (0x5a6xx)"),
    (0x061E00, 0x062500, "krn$_idle + IPL churn (0x61ed8-0x624xx)"),
    (0x06AA00, 0x06AC00, "kernel sync prims (0x6aaxx-0x6abxx)"),
    (0x06B200, 0x06B500, "kernel prims (0x6b2xx-0x6b4xx)"),
    (0x06DC00, 0x06DE00, "krn$_process (0x6dd58)"),
    (0x070000, 0x070500, "file/io layer (0x700xx-0x704xx, read_with_prompt)"),
    (0x071D00, 0x072000, "showmem helpers / bitmap walk callers (0x71dxx)"),
    (0x078400, 0x078500, "show command (0x784a0)"),
    (0x07BE00, 0x07C000, "tick counter + timer tick (0x7bef0/0x7bfb8)"),
    (0x07C000, 0x07C400, "timer_check + delay loops (0x7c1xx-0x7c3xx)"),
    (0x07F700, 0x07F900, "I/O port wrappers (outb 0x7f700 family)"),
    (0x083C00, 0x083F00, "showmem/showconfig (0x83cxx/0x83e00)"),
    (0x0B0F00, 0x0B1000, "wait primitive (0xb0f08)"),
    (0x0B1700, 0x0B1E00, "scheduler/IPL callers (0xb17xx-0xb1dxx)"),
    (0x0B7500, 0x0B7600, "kernel misc (0xb755c)"),
    (0x1C6400, 0x1C7000, "runtime I/O stubs + ISRs (stb 0x1c6a80, ldbu "
                         "0x1c69e8, clock ISR 0x1c6d4c, getbit64 0x1c6b24)"),
    (0x000000, 0x200000, "SRM console image (unlabeled)"),

    # ---- Pre-takeover (initial PAL + compressed image) -----------------
    (0x600900, 0x601200, "SROM/decompressor inner (0x6009xx-0x6011xx)"),
    (0x600000, 0x700000, "initial PAL @0x600000 (pre-takeover)"),
    (0x900000, 0xA00000, "compressed image / decom startup (0x900000)"),
]

CATCH_ALL_BASE = None  # filled per-file from the last bucket if flagged


BUCKET_BYTES = 1024  # overwritten from the dump header when present


def label_for(pa):
    # OVERLAP match, not base-containment: a 1 KiB bucket whose base
    # falls just before a region start still mostly belongs to it
    # (e.g. bucket 0x61c00 contains krn$_idle at 0x61ed8).  First
    # overlapping entry wins; REGIONS is ordered specific-first.
    lo, hi = pa, pa + BUCKET_BYTES
    for start, end, name in REGIONS:
        if start < hi and lo < end:
            return name
    if pa >= 16 * 1024 * 1024:
        return "CATCH-ALL (pc >= 16MiB)"
    return "unmapped"


def process(path):
    global BUCKET_BYTES
    rows = []
    total = 0
    with open(path, "r") as f:
        for line in f:
            if line.startswith("#"):
                for tok in line.split():
                    if tok.startswith("total_retires="):
                        total = int(tok.split("=")[1])
                    elif tok.startswith("bucket_bytes="):
                        BUCKET_BYTES = int(tok.split("=")[1])
                continue
            parts = line.split()
            if len(parts) < 6:
                continue
            pa    = int(parts[0], 16)
            cnt   = int(parts[1])
            pct   = float(parts[2])
            since = int(parts[3])
            first = int(parts[4])
            last  = int(parts[5])
            rows.append((pa, cnt, pct, since, first, last))

    print("=" * 78)
    print(f"{path}  total_retires={total}")
    print("=" * 78)

    print(f"{'bucket_pa':>10} {'count':>13} {'pct':>6} {'since_mark':>12}  label")
    for pa, cnt, pct, since, first, last in rows[:40]:
        print(f"{pa:>10x} {cnt:>13} {pct:>6.2f} {since:>12}  {label_for(pa)}")

    # Rollup by label.
    rollup = {}
    for pa, cnt, _, since, _, _ in rows:
        name = label_for(pa)
        c, s = rollup.get(name, (0, 0))
        rollup[name] = (c + cnt, s + since)

    print()
    print(f"{'pct':>6} {'count':>13} {'since_mark':>12}  region rollup")
    for name, (cnt, since) in sorted(rollup.items(),
                                     key=lambda kv: -kv[1][0]):
        pct = (100.0 * cnt / total) if total else 0.0
        print(f"{pct:>6.2f} {cnt:>13} {since:>12}  {name}")
    print()


def main():
    if len(sys.argv) < 2:
        print("usage: py profile_resolve.py <profile_*.txt> [...]")
        return 1
    for path in sys.argv[1:]:
        process(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
