#!/usr/bin/env python3
# ============================================================================
# tools/snap_ptwalk.py -- walk the console-built bootstrap page tables in an
#                         EmulatR .axpsnap and verify the AARM 27.4.1.3
#                         bootstrap address space (Regions 0/1/2/3)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# ASCII(128) only.  Hex radix.
#
# WHY (JRN-SCSI-009)
#   "jumping to bootstrap code" transfers to VA 0x20000000 with memory
#   management ON (AARM 27.4.1.3: all system software, including the primary
#   bootstrap, runs in a virtual memory environment).  A broken initial page
#   table is therefore indistinguishable, from the console transcript, from a
#   missing image: either way the first fetch fails and control re-enters the
#   console with BIP set -> failed bootstrap, reason-for-halt still 0.
#   This tool proves which, from a snapshot, by walking the actual PTEs.
#
#   8 KiB pages: offset VA<12:0>; L3 VA<22:13>; L2 VA<32:23>; L1 VA<42:33>.
#   PTE: V=<0>, FOR/FOW/FOE=<3:1>, ASM=<4>, GH=<6:5>, KRE=<8>, KWE=<12>,
#        PFN=<63:32>.  Next-level PA = PFN * 0x2000.
#
# USAGE
#   python3 tools/snap_ptwalk.py <file.axpsnap> [--ptbr <PA>] [va ...]
#     --ptbr  physical base of the L1 page table (console prints
#             "initializing page table at <pa>"; DS20 run of record: 3ff04000)
#     va      virtual addresses to walk (defaults: the AARM region anchors)
# ============================================================================
import os, sys, struct, mmap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snap_va_disasm import find_payload0

DEFAULT_VAS = [
    (0x20000000,  "Region 1: APB entry (the jump target)"),
    (0x20096000,  "Region 1: resolver module pages"),
    (0x20098000,  "Region 1: image tail"),
    (0x2009a000,  "Region 1: bootstrap stack page (image+3pg area)"),
    (0x10000000,  "Region 0: HWRPB base"),
    (0x101aa000,  "Region 0: CRB callback code page"),
    (0x40000000,  "Region 2: page-table space base"),
]


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    path = sys.argv[1]; a = sys.argv[2:]
    ptbr = 0x3ff04000; vas = []
    i = 0
    while i < len(a):
        if a[i] == '--ptbr': ptbr = int(a[i+1], 0); i += 2
        else: vas.append((int(a[i], 0), "operator-requested")); i += 1
    if not vas: vas = DEFAULT_VAS

    sz = os.path.getsize(path); fh = open(path, 'rb')
    mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
    pay, mem = find_payload0(mm, sz)
    if pay is None: print("*** payload not located ***"); sys.exit(3)
    def q(pa):
        fh.seek(pay + pa); return struct.unpack('<Q', fh.read(8))[0]

    print("# snapshot %s   cycle %d" %
          (os.path.basename(path), struct.unpack_from('<Q', mm, 0x1C)[0]))
    print("# L1 (PTBR) at PA 0x%x   memSize 0x%x" % (ptbr, mem))

    # AARM Region 3 sanity: L1 self-map "by the second PTE in the page"
    self_pte = q(ptbr + 1*8)
    ok = (self_pte & 1) and ((self_pte >> 32) * 0x2000 == ptbr)
    print("# L1[1] self-map: 0x%016x  %s" %
          (self_pte, "OK (VPTB Region 3 wired)" if ok else "*** BROKEN ***"))

    def fmt_pte(p):
        fl = []
        if p & 1: fl.append("V")
        if p & 0x10: fl.append("ASM")
        if p & 0x100: fl.append("KRE")
        if p & 0x1000: fl.append("KWE")
        gh = (p >> 5) & 3
        if gh: fl.append("GH=%d" % gh)
        return "PFN=0x%x [%s]" % (p >> 32, ",".join(fl) or "no-V")

    for va, why in vas:
        l1i = (va >> 33) & 0x3ff; l2i = (va >> 23) & 0x3ff; l3i = (va >> 13) & 0x3ff
        print("\nVA 0x%016x  -- %s" % (va, why))
        p1 = q(ptbr + l1i*8)
        print("  L1[0x%03x] @PA 0x%x = 0x%016x  %s" % (l1i, ptbr + l1i*8, p1, fmt_pte(p1)))
        if not (p1 & 1): print("  *** L1 invalid -- VA UNMAPPED ***"); continue
        l2 = (p1 >> 32) * 0x2000
        p2 = q(l2 + l2i*8)
        print("  L2[0x%03x] @PA 0x%x = 0x%016x  %s" % (l2i, l2 + l2i*8, p2, fmt_pte(p2)))
        if not (p2 & 1): print("  *** L2 invalid -- VA UNMAPPED ***"); continue
        l3 = (p2 >> 32) * 0x2000
        p3 = q(l3 + l3i*8)
        print("  L3[0x%03x] @PA 0x%x = 0x%016x  %s" % (l3i, l3 + l3i*8, p3, fmt_pte(p3)))
        if not (p3 & 1):
            print("  *** L3 invalid -- VA UNMAPPED (guard page if stack-adjacent) ***")
            continue
        pa = (p3 >> 32) * 0x2000 + (va & 0x1fff)
        first = q(pa)
        print("  => PA 0x%09x   first quadword 0x%016x" % (pa, first))


if __name__ == "__main__":
    main()
