#!/usr/bin/env python3
# ============================================================================
# tools/snap_va_disasm.py -- VA-faithful disassembly of a loaded image out of
#                            an EmulatR .axpsnap snapshot.
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# ASCII(128) only.  Hex radix.
#
# WHY THIS EXISTS (2026-07-25, JRN-SCSI-006)
#   A snapshot's memory payload is guest-PHYSICAL.  The bootstrap image (VMB /
#   APB) executes at VIRTUAL addresses 0x20000000+, but the loader places it at
#   the PHYSICAL base the console reports on the boot line ("base = 5bc000" for
#   a 4 GiB DS20).  So reading PA == VA in the 0x2000xxxx range returns ZEROS
#   and looks like "the region is not mapped" -- it is mapped, just displaced.
#   The whole image is ONE contiguous linear window, so a single delta suffices:
#
#       PA = VA - IMAGE_VA_BASE + IMAGE_PA_BASE
#
#   No page-table walk is needed for the image region (a walk IS still needed
#   for stack/VA-only regions -- see JRN-SCSI-004 Sec 3 item 4).
#
# USAGE
#   python3 tools/snap_va_disasm.py <file.axpsnap> <VA> [count] [options]
#     count            instructions to decode (default 0x40)
#     --pa-base  <PA>  image physical base   (default 0x5bc000)
#     --va-base  <VA>  image virtual base    (default 0x20000000)
#     --raw-pa         treat <VA> as a physical address (no translation)
#     --find-bsr <VA>  scan the image for BR/BSR whose target is <VA>
#     --img-size <n>   image size for --find-bsr (default 0x99400)
#
# EXAMPLES
#   # the 0xf3-tail mode gate (JRN-SCSI-005 Sec 3)
#   python3 tools/snap_va_disasm.py snap.axpsnap 0x20096d00 0x40
#   # every call site of the resolver module entry
#   python3 tools/snap_va_disasm.py snap.axpsnap 0 --find-bsr 0x20095840
# ============================================================================
import os, sys, struct, mmap

CPU_OFF   = 0x12C                      # same probe window snapmap.py uses
HDR_MAGIC = b'EMULATR1'

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from alpha_disasm import dis           # single source of truth for decoding


def find_payload0(mm, sz):
    """Locate file offset of guest PA 0 (mirrors tools/snapmap.py)."""
    o = CPU_OFF
    while o < CPU_OFF + 0x10000:
        v = struct.unpack_from('<Q', mm, o)[0]
        if (16 * 1024 * 1024) <= v <= (8 * 1024 * 1024 * 1024) and v % (16 * 1024 * 1024) == 0:
            cand = o + 8
            cb   = cand + v
            if cb + 8 <= sz:
                a = struct.unpack_from('<I', mm, cb)[0]
                b = struct.unpack_from('<I', mm, cb + 4)[0]
                if a < 16 and 0 < b < 256 and b % 2 == 0:
                    return cand, v
        o += 1
    return None, None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    addr = int(sys.argv[2], 0)
    argv = sys.argv[3:]
    count = 0x40
    if argv and not argv[0].startswith('--'):
        count = int(argv[0], 0)
        argv = argv[1:]
    pa_base, va_base, raw_pa = 0x5bc000, 0x20000000, False
    find_bsr, img_size = None, 0x99400
    i = 0
    while i < len(argv):
        a = argv[i]
        if   a == '--pa-base':  pa_base = int(argv[i + 1], 0); i += 2
        elif a == '--va-base':  va_base = int(argv[i + 1], 0); i += 2
        elif a == '--raw-pa':   raw_pa = True; i += 1
        elif a == '--find-bsr': find_bsr = int(argv[i + 1], 0); i += 2
        elif a == '--img-size': img_size = int(argv[i + 1], 0); i += 2
        else:
            print("unknown option: %s" % a); sys.exit(2)

    sz = os.path.getsize(path)
    fh = open(path, 'rb')
    # mmap (not read()) -- the payload probe validates the qword just PAST the
    # memory block, so it needs the whole file addressable, and a 4 GiB
    # snapshot must not be pulled into RAM.
    mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
    hdr = mm
    if hdr[0:8] != HDR_MAGIC:
        print("*** bad magic %r ***" % bytes(hdr[0:8])); sys.exit(3)
    payload0, memSize = find_payload0(hdr, sz)
    if payload0 is None:
        print("*** memory payload not located ***"); sys.exit(4)
    print("# snapshot   : %s" % os.path.basename(path))
    print("# cycleCount : %d" % struct.unpack_from('<Q', hdr, 0x1C)[0])
    print("# PA 0       @ file 0x%x   memSize 0x%x" % (payload0, memSize))

    delta = 0 if raw_pa else (pa_base - va_base)

    def read(a, n):
        fh.seek(payload0 + a + delta)
        return fh.read(n)

    if find_bsr is not None:
        print("# scan VA 0x%x..0x%x for BR/BSR -> 0x%x" %
              (va_base, va_base + img_size, find_bsr))
        img = read(va_base, img_size)
        for off in range(0, len(img) - 3, 4):
            w = struct.unpack_from('<I', img, off)[0]
            op = w >> 26
            if op in (0x30, 0x34):                       # BR / BSR
                d = w & 0x1fffff
                if d >> 20:
                    d -= 1 << 21
                va = va_base + off
                if va + 4 + d * 4 == find_bsr:
                    print("  %08x: %08x  %s" % (va, w, dis(w)))
        return

    print("# %s 0x%x .. 0x%x  (file 0x%x)" %
          ("PA" if raw_pa else "VA", addr, addr + count * 4 - 1,
           payload0 + addr + delta))
    buf = read(addr, count * 4)
    for k in range(len(buf) // 4):
        w = struct.unpack_from('<I', buf, k * 4)[0]
        print("  %08x: %08x  %s" % (addr + k * 4, w, dis(w)))


if __name__ == "__main__":
    main()
