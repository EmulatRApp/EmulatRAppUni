#!/usr/bin/env python3
# ============================================================================
# tools/scsi_read_diff.py -- byte-diff the SCSI READs a run performed against
#                            the backing disk image
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY (JRN-SCSI-027)
#   %SYSBOOT-F-LDFAIL / %LOADER-E-BADIMGOFF says the image bytes ARRIVED and
#   do not parse -- the loader found an offset inside no image section.  The
#   same dka0.vdisk boots OpenVMS under Charon, so the image is good and the
#   read path is suspect.  Geometry is ruled out (INQUIRY type 0x00, READ
#   CAPACITY block length 512, MODE SENSE descriptor from the same source,
#   file size an exact multiple of 512), which leaves a TRANSFER-SHAPE
#   defect: padding leaking into READ data-in, a per-command cap truncating
#   with GOOD status, the READ(6)/READ(10) split, or a data pointer that does
#   not advance across multi-MOVE SCRIPTS.
#
#   EMULATR_SCSI_TRACE=1 makes the HBA log, per command:
#     N810-CMD id=.. lun=.. op=0x.. len=.. lba=.. cnt=.. ->
#              status=.. xfer=.. fnv=0x.. head=.. tail=..
#   fnv is FNV-1a/64 over the payload the target RETURNED.  This tool recomputes
#   the same checksum over the SAME LBA range read straight from the image file
#   and reports the first mismatch -- which names the defect (shift, pad,
#   truncation, wrong offset) with no interpretation left to argue.
#
# USAGE
#   python3 tools/scsi_read_diff.py <run.log> <disk.vdisk> [--block-size 512]
#                                   [--limit N] [--show-ok]
# ============================================================================

import argparse
import re
import sys

LINE = re.compile(
    r"N810-CMD id=(\d+) lun=(\d+) op=0x([0-9A-Fa-f]{2}) len=(\d+) "
    r"lba=(-?\d+) cnt=(-?\d+) -> status=(\d+) xfer=(\d+) "
    r"fnv=0x([0-9a-f]{16}) head=(\S+) tail=(\S+)")

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


def fnv1a(data):
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & MASK64
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('image')
    ap.add_argument('--block-size', type=int, default=512)
    ap.add_argument('--limit', type=int, default=0,
                    help='stop after N read commands (0 = all)')
    ap.add_argument('--show-ok', action='store_true',
                    help='print matching reads too, not just mismatches')
    a = ap.parse_args()

    reads = 0
    mismatches = 0
    first_bad = None
    with open(a.image, 'rb') as img, \
            open(a.log, 'r', encoding='ascii', errors='replace') as log:
        for raw in log:
            if 'N810-CMD' not in raw:
                continue
            m = LINE.search(raw)
            if m is None:
                continue
            op = int(m.group(3), 16)
            lba = int(m.group(5))
            cnt = int(m.group(6))
            status = int(m.group(7))
            xfer = int(m.group(8))
            fnv_dev = int(m.group(9), 16)
            head = m.group(10)
            if op not in (0x08, 0x28) or lba < 0:      # READ(6) / READ(10)
                continue
            reads += 1
            if a.limit and reads > a.limit:
                break

            img.seek(lba * a.block_size)
            want = img.read(xfer)
            fnv_img = fnv1a(want)
            ok = (fnv_img == fnv_dev) and (len(want) == xfer)

            if not ok:
                mismatches += 1
                # Localize: first differing byte against what the device
                # returned is not recoverable from a checksum alone, but the
                # head bytes are logged, so compare those directly.
                img.seek(lba * a.block_size)
                head_img = img.read(8).hex().upper()
                note = ''
                if head_img != head:
                    note = '  HEAD DIFFERS img=%s dev=%s' % (head_img, head)
                elif len(want) != xfer:
                    note = '  SHORT IMAGE READ (%d < %d)' % (len(want), xfer)
                else:
                    note = '  head matches -> divergence is later in the payload'
                print('MISMATCH lba=%-10d cnt=%-5d xfer=%-7d status=%d%s'
                      % (lba, cnt, xfer, status, note))
                if first_bad is None:
                    first_bad = (lba, cnt, xfer, status)
            elif a.show_ok:
                print('ok       lba=%-10d cnt=%-5d xfer=%-7d' % (lba, cnt, xfer))

    print()
    print('read commands checked : %d' % reads)
    print('mismatches            : %d' % mismatches)
    if first_bad:
        print('FIRST BAD READ        : lba=%d cnt=%d xfer=%d status=%d'
              % first_bad)
        print('  -> compare cnt*%d against xfer: a short xfer with GOOD status'
              % a.block_size)
        print('     is the per-command cap / truncation shape; an equal xfer')
        print('     with a differing payload is the pad or pointer shape.')
    elif reads:
        print('every READ payload matches the image at its LBA.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
