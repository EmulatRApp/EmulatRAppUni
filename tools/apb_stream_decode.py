#!/usr/bin/env python3
# ============================================================================
# tools/apb_stream_decode.py -- decode APB pattern-VM streams + operand records
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY (JRN-SCSI-014/-015; T2/T3 of the JRN-SCSI-012 plan)
#   APB's device-name resolver is a pattern VM: a static token STREAM
#   (boot_dev copy at VA 0x99216..0x99328, image tail) whose match-bearing
#   tokens carry an end-of-record-relative rel32 to an OPERAND RECORD
#   { u64 typeHeader, u64 handlerVA }.  The walk context (a3 = 0x20063820)
#   is an OBJECT of the same record format (vtable-like).  This tool makes
#   all three readable from any snapshot:
#     --stream  <VA> <len>   parse tokens; resolve operands (+6 end-relative)
#     --records <VA> <len>   dump {header, handler} record pairs (QW view)
#     --ctx     [<VA>]       dump the walk ctx object (default 0x20063800)
#   Token word bit map (JRN-SCSI-014 Sec 2, partial):
#     bit15..12 class bits (14 = link/extension deref, 12 = alt helper),
#     bit10 = abandon record & advance, bit9 = extension byte follows,
#     bits[16:11] = 6-bit operand index.
#
# USAGE
#   python3 tools/apb_stream_decode.py <file.axpsnap> --stream 0x20099216 0x112
#   python3 tools/apb_stream_decode.py <file.axpsnap> --ctx
#   python3 tools/apb_stream_decode.py <file.axpsnap> --records 0x20063480 0x260
#   Options: --pa-base/--va-base as in snap_va_disasm.py (default 0x5bc000 /
#   0x20000000, the DS20 4GiB layout).
# ============================================================================
import argparse
import mmap
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snap_va_disasm import find_payload0            # noqa: E402


class Snap(object):
    def __init__(self, path, pa_base, va_base):
        self.f = open(path, 'rb')
        self.mm = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        self.p0, self.msz = find_payload0(self.mm, os.path.getsize(path))
        if self.p0 is None:
            raise SystemExit('cannot locate payload in %s' % path)
        self.pa_base, self.va_base = pa_base, va_base

    def rd(self, va, n):
        off = self.p0 + (va - self.va_base + self.pa_base)
        return self.mm[off:off + n]


def in_image(q):
    return 0x20000000 <= q < 0x20099400


def record_str(snap, va):
    """Format the {header, handler} operand record at va (8-aligned)."""
    d = snap.rd(va, 16)
    hdr, ptr = struct.unpack('<QQ', d)
    tag = 'handler 0x%08x' % ptr if in_image(ptr) else 'value   0x%x' % ptr
    return 'hdr=0x%-10x %s' % (hdr, tag)


def do_stream(snap, va0, ln):
    d = snap.rd(va0, ln)

    def u16(o):
        return d[o] | (d[o + 1] << 8)
    i = 0
    print('%-10s %-6s b10 idx  operand' % ('token VA', 'token'))
    while i < len(d) - 1:
        va = va0 + i
        tok = u16(i)
        if tok == 0x0000:
            print('0x%08x 0x0000          == section separator ==' % va)
            i += 2
            continue
        b10 = (tok >> 10) & 1
        idx = (tok >> 11) & 0x3f
        has32 = (i + 6 <= len(d) and d[i + 5] == 0xff
                 and d[i + 4] in (0xfc, 0xfa, 0xf4, 0xf2, 0xfe))
        if has32:
            rel = struct.unpack_from('<i', d, i + 2)[0]
            tgt = (va + 6 + rel) & 0xffffffff       # END-of-record relative
            rec = record_str(snap, tgt) if in_image(tgt) else '(out of image)'
            print('0x%08x 0x%04x  %d  %2d  -> 0x%08x  %s'
                  % (va, tok, b10, idx, tgt, rec))
            i += 6
        else:
            ext = u16(i + 2) if i + 4 <= len(d) else 0
            print('0x%08x 0x%04x  %d  %2d  ext16=0x%04x'
                  % (va, tok, b10, idx, ext))
            i += 4


def do_records(snap, va0, ln):
    d = snap.rd(va0, ln)
    for i in range(0, ln - 15, 8):
        hdr, ptr = struct.unpack_from('<QQ', d, i)
        if hdr and hdr < 0x100000000 and (hdr & 0xf000) == 0x3000 \
                and in_image(ptr):
            print('  0x%08x: %s' % (va0 + i, record_str(snap, va0 + i)))


def do_ctx(snap, va0, ln):
    d = snap.rd(va0, ln)
    for i in range(0, ln, 8):
        q = struct.unpack_from('<Q', d, i)[0]
        tag = '  -> APB' if in_image(q) else ''
        mark = '   <== walk a3' if va0 + i == 0x20063820 else ''
        print('  %08x: 0x%016x%s%s' % (va0 + i, q, tag, mark))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('snapshot')
    ap.add_argument('--pa-base', type=lambda x: int(x, 0), default=0x5bc000)
    ap.add_argument('--va-base', type=lambda x: int(x, 0), default=0x20000000)
    ap.add_argument('--stream', nargs=2, default=None,
                    metavar=('VA', 'LEN'))
    ap.add_argument('--records', nargs=2, default=None,
                    metavar=('VA', 'LEN'))
    ap.add_argument('--ctx', nargs='?', const='0x20063800', default=None,
                    metavar='VA')
    a = ap.parse_args()
    snap = Snap(a.snapshot, a.pa_base, a.va_base)
    if a.stream:
        do_stream(snap, int(a.stream[0], 0), int(a.stream[1], 0))
    if a.records:
        do_records(snap, int(a.records[0], 0), int(a.records[1], 0))
    if a.ctx is not None:
        do_ctx(snap, int(a.ctx, 0), 0x100)
    if not (a.stream or a.records or a.ctx):
        do_stream(snap, 0x20099216, 0x112)
        print()
        do_ctx(snap, 0x20063800, 0x100)


if __name__ == '__main__':
    main()
