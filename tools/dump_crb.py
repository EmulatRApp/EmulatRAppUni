#!/usr/bin/env python3
# ============================================================================
# tools/dump_crb.py -- dump the HWRPB Console Routine Block (CRB) and the
#                      console-callback entry points from an .axpsnap
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# ASCII(128) only.  Hex radix.
#
# WHY (JRN-SCSI-006 Sec 8/9, JRN-SCSI-009)
#   APB reads console environment variables through the HWRPB CRB callbacks
#   (a0 = routine code; 0x22 = cbfunc$k_get_env).  Those callbacks are FIRMWARE
#   code living in the console's own address range -- NOT in APB's 0x2000xxxx
#   image and NOT in EmulatR's host-side CSERVE surface.  Every DIAG-PC capture
#   in the NOIOVEC track used PCLO = 0x20095840, i.e. inside APB, so the
#   callbacks have never been observed even once.  This tool prints the exact
#   PC range to point the instrument at.
#
# CHAIN (offsets from apisrm ref/apu_hwrpb_def.h, struct HWRPB)
#   HWRPB base (console prints "initializing HWRPB at <pa>")
#     + 0xC0  hwrpb$Q_CRB_OFFSET   -> CRB
#   CRB + 0x00 dispatch  descriptor VA   + 0x08 dispatch  descriptor PA
#       + 0x10 fixup     descriptor VA   + 0x18 fixup     descriptor PA
#       + 0x20 map entry count           + 0x28 map PA entries
#   descriptor + 0x08 = code entry point  (OpenVMS bound-procedure descriptor;
#   the same +0x08 indirection APB's caller does: LDQ r27,0x10(r0) /
#   LDQ r28,0x8(r27) / JMP)
#
# USAGE
#   python3 tools/dump_crb.py <file.axpsnap> [--hwrpb <PA>] [--disasm <n>]
#     --hwrpb   HWRPB physical base (default 0x2000)
#     --disasm  instructions to disassemble at the dispatch entry (default 12)
# ============================================================================
import os, sys, struct, mmap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snap_va_disasm import find_payload0
from alpha_disasm import dis

HWRPB_FIELDS = ["BASE","IDENT","REVISION","SIZE","PRIMARY","PAGESIZE","PA_SIZE",
    "ASN_MAX","SERIAL0","SERIAL1","SYSTYPE","SYSVAR","SYSREV","IT_FREQ","CC_FREQ",
    "VPTB","RSVD_ARCH","TBHINT_OFF","NPROC","SLOT_SIZE","SLOT_OFF","CTB_COUNT",
    "CTB_SIZE","CTB_OFF","CRB_OFF","MEM_OFF","CONFIG_OFF","FRU_OFF"]
CRB_FIELDS = ["DISP_DESC_VA","DISP_DESC_PA","FIXUP_DESC_VA","FIXUP_DESC_PA",
              "MAP_ENTRY_COUNT","MAP_PA_ENTRIES"]


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    path = sys.argv[1]; a = sys.argv[2:]
    hwrpb, ndis = 0x2000, 12
    i = 0
    while i < len(a):
        if   a[i] == '--hwrpb':  hwrpb = int(a[i+1], 0); i += 2
        elif a[i] == '--disasm': ndis = int(a[i+1], 0); i += 2
        else: print("unknown option %s" % a[i]); sys.exit(2)

    sz = os.path.getsize(path); fh = open(path, 'rb')
    mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
    pay, mem = find_payload0(mm, sz)
    if pay is None: print("*** payload not located ***"); sys.exit(3)
    def q(pa):
        fh.seek(pay + pa); return struct.unpack('<Q', fh.read(8))[0]

    print("# snapshot %s   cycle %d   memSize 0x%x" %
          (os.path.basename(path), struct.unpack_from('<Q', mm, 0x1C)[0], mem))
    v = {}
    print("\n=== HWRPB @ PA 0x%x ===" % hwrpb)
    for k, n in enumerate(HWRPB_FIELDS):
        v[n] = q(hwrpb + k*8)
        note = ""
        if n == "IDENT":
            ok = struct.pack('<Q', v[n]) == b"HWRPB\x00\x00\x00"
            note = "  %s" % ("OK" if ok else "*** NOT 'HWRPB' -- wrong base? ***")
        if n in ("CC_FREQ", "IT_FREQ"):
            note = "  = %d Hz (%.3f MHz)" % (v[n], v[n]/1e6)
        print("  +0x%03x %-11s 0x%016x%s" % (k*8, n, v[n], note))

    crb = hwrpb + v["CRB_OFF"]
    print("\n=== CRB @ PA 0x%x  (HWRPB + 0x%x) ===" % (crb, v["CRB_OFF"]))
    c = {}
    for k, n in enumerate(CRB_FIELDS):
        c[n] = q(crb + k*8)
        print("  +0x%02x %-16s 0x%016x" % (k*8, n, c[n]))

    print("\n=== callback entry points (descriptor + 0x08) ===")
    ranges = []
    for tag, dva, dpa in (("DISPATCH", c["DISP_DESC_VA"], c["DISP_DESC_PA"]),
                          ("FIXUP",    c["FIXUP_DESC_VA"], c["FIXUP_DESC_PA"])):
        if not dpa: continue
        ent = q(dpa + 8)
        delta = dva - dpa
        print("  %-8s descriptor VA 0x%x / PA 0x%x   ->  entry VA 0x%x"
              % (tag, dva, dpa, ent))
        print("           console VA->PA delta = 0x%x  (entry PA 0x%x)"
              % (delta, ent - delta))
        ranges.append((tag, ent, ent - delta))

    if ranges:
        tag, ent, entpa = ranges[0]
        print("\n=== disassembly at the DISPATCH entry (PA 0x%x) ===" % entpa)
        fh.seek(pay + entpa); buf = fh.read(ndis*4)
        for k in range(len(buf)//4):
            w = struct.unpack_from('<I', buf, k*4)[0]
            print("  %08x: %08x  %s" % (ent + k*4, w, dis(w)))
        lo = ent & ~0xfff
        print("\n=== INSTRUMENT THIS ===")
        print("  EMULATR_DIAG_PCLO=0x%x EMULATR_DIAG_PCHI=0x%x" % (lo, lo + 0x2000))
        print("  (dispatch entry 0x%x; a0 = routine code in R16 at entry --" % ent)
        print("   0x22 = cbfunc$k_get_env, 0x20 = set_env, 0x02 = puts, 0x01 = getc.")
        print("   Every callback the guest makes passes through this one entry,")
        print("   so a window here logs ALL of them, not just the env reads.)")


if __name__ == "__main__":
    main()
