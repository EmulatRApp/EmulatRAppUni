#!/usr/bin/env python3
# ============================================================================
# tools/find_bootstrap_image.py -- did the bootstrap image actually LAND where
#                                  the console said?  (JRN-SCSI-009 triage)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# ASCII(128) only.  Hex radix.
#
# WHY
#   The console prints "base = 5bc000" and "reading 1226 blocks ... bootstrap
#   code read in", then jumps.  A halt reported as
#       halted CPU 0 / halt code = 0 / PC = 20000000
#   means the CPU stopped AT the entry point.  0x00000000 decodes as
#   CALL_PAL #0 == HALT, so an entry page full of zeros produces exactly that
#   signature: the read was reported, but the bytes are not at `base`.
#
#   This tool answers, from a halt snapshot:
#     (1) is there code at the expected physical base?
#     (2) if not, WHERE did the image land?  It sweeps memory for APB's first
#         three instructions, taken from the known-good NOIOVEC snapshot:
#             d3800000  BSR   r28,.+0x0
#             201f0001  LDA   r0,0x1(r31)
#             203f001c  LDA   r1,0x1c(r31)
#
# USAGE
#   python3 tools/find_bootstrap_image.py <file.axpsnap>
#     --base   <PA>   expected physical base   (default 0x5bc000)
#     --limit  <n>    bytes of guest memory to sweep (default 0x10000000, 256 MiB)
#     --start  <PA>   begin the sweep here (default 0) -- for resuming a sweep
#     --sig    <hex>  override the signature (comma-separated LE words)
# ============================================================================
import os, sys, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snap_va_disasm import find_payload0          # single source of truth
from alpha_disasm import dis

SIG = (0xd3800000, 0x201f0001, 0x203f001c)


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    path = sys.argv[1]
    a = sys.argv[2:]
    base, limit, start = 0x5bc000, 0x10000000, 0
    sig = list(SIG)
    i = 0
    while i < len(a):
        if   a[i] == '--base':  base = int(a[i+1], 0); i += 2
        elif a[i] == '--limit': limit = int(a[i+1], 0); i += 2
        elif a[i] == '--start': start = int(a[i+1], 0); i += 2
        elif a[i] == '--sig':   sig = [int(x, 16) for x in a[i+1].split(',')]; i += 2
        else: print("unknown option %s" % a[i]); sys.exit(2)

    sz = os.path.getsize(path)
    fh = open(path, 'rb')
    import mmap
    mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
    pay, mem = find_payload0(mm, sz)
    if pay is None:
        print("*** memory payload not located ***"); sys.exit(3)
    print("# snapshot   : %s" % os.path.basename(path))
    print("# cycleCount : %d" % struct.unpack_from('<Q', mm, 0x1C)[0])
    print("# PA 0 @ file 0x%x   memSize 0x%x" % (pay, mem))

    # (1) expected base
    fh.seek(pay + base); buf = fh.read(0x40)
    nz = sum(1 for x in buf if x)
    print("\n-- expected base PA 0x%x (VA 0x20000000) : %d/64 bytes nonzero --" % (base, nz))
    for k in range(8):
        w = struct.unpack_from('<I', buf, k*4)[0]
        print("   %08x: %08x  %s" % (0x20000000 + k*4, w, dis(w)))
    ok = struct.unpack_from('<I', buf, 0)[0] == sig[0]
    if nz == 0:
        print("\n   *** ENTRY PAGE IS ALL ZEROS -- 0x00000000 = CALL_PAL #0 = HALT.")
        print("       This IS the 'halt code = 0 / PC = 20000000' signature.")
    print("\n   verdict: image %s at the expected base" %
          ("PRESENT" if ok else "ABSENT/WRONG"))

    if ok:
        return

    # (2) where did it land?
    needle = b"".join(struct.pack('<I', w) for w in sig)
    print("\n-- sweeping PA 0x%x .. 0x%x for the APB entry signature --" % (start, start + limit))
    CH = 8 << 20
    fh.seek(pay + start); pos = start; prev = b""; found = []
    while pos < start + limit:
        blk = fh.read(min(CH, start + limit - pos))
        if not blk: break
        win = prev + blk
        j = win.find(needle)
        while j >= 0:
            found.append(pos - len(prev) + j)
            j = win.find(needle, j + 1)
        prev = blk[-16:]; pos += len(blk)
    if not found:
        print("   NOT FOUND in the swept range.  Re-run with a wider --limit,")
        print("   or --start past this range to continue the sweep.")
    for pa in found:
        delta = pa - base
        print("   FOUND at PA 0x%x   (expected 0x%x, delta %s0x%x)" %
              (pa, base, "+" if delta >= 0 else "-", abs(delta)))


if __name__ == "__main__":
    main()
