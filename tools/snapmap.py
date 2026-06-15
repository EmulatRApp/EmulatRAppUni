#!/usr/bin/env python3
# ============================================================================
# tools/snapmap.py -- EmulatR .axpsnap region mapper / 010-Editor-style hex map
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Decodes the snapshot header, chipset block, and HWRPB (AARM Table 26-1),
# then emits a 010-Editor-style hex dump (top axis 0..F, left address gutter,
# ASCII pane) of named critical guest-physical regions.  ASCII(128) output.
# Usage: python3 snapmap.py <file.axpsnap>
# ============================================================================
import sys, struct, mmap, os

CPU_OFF   = 0x12C
HDR_MAGIC = b'EMULATR1'
HWRPB_VALID = 0x0000004250525748      # "HWRPB\0\0\0" qword at HWRPB+8 (AARM 26.1)

def main():
    f  = sys.argv[1]
    sz = os.path.getsize(f)
    fh = open(f,'rb')
    mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
    out = []
    def P(s): out.append(s)
    def u32(o): return struct.unpack_from('<I',mm,o)[0]
    def u64(o): return struct.unpack_from('<Q',mm,o)[0]

    P("="*78)
    P("EmulatR snapshot map : %s" % os.path.basename(f))
    P("file size = %d bytes (0x%x)" % (sz, sz))
    P("="*78)
    magic = mm[0:8]
    P("magic            = %r %s" % (magic, "OK" if magic==HDR_MAGIC else "*** BAD ***"))
    P("formatVersion    = %d" % u32(0x08))
    P("cpuStateVersion  = %d" % u32(0x0C))
    P("chipsetVersion   = %d" % u32(0x10))
    P("timestamp(unix)  = %d" % u64(0x14))
    P("cycleCount       = %d (0x%x)" % (u64(0x1C), u64(0x1C)))
    cmt = mm[0x2C:0x2C+256].split(b'\x00',1)[0].decode('latin1')
    P("comment          = %r" % cmt)

    # locate memSize / payload0
    payload0 = None; memSize = None; o = CPU_OFF
    while o < CPU_OFF + 0x10000:
        v = u64(o)
        if (16*1024*1024) <= v <= (8*1024*1024*1024) and v % (16*1024*1024)==0:
            cand=o+8; cb=cand+v
            if cb+8<=sz and u32(cb)<16 and 0<u32(cb+4)<256 and u32(cb+4)%2==0:
                payload0, memSize = cand, v; break
        o += 1
    if payload0 is None:
        P("\n*** memory payload not located ***"); print("\n".join(out)); return
    P("memSize          = 0x%x (%d MiB)" % (memSize, memSize//(1024*1024)))
    P("guest PA 0       @ file 0x%x   sizeof(CpuState)=%d" % (payload0, payload0-8-CPU_OFF))

    def q(pa): return u64(payload0+pa)         # guest quadword by PA
    def hexdump(pa, length, label):
        P(""); P("=== %s : PA 0x%x .. 0x%x  (file 0x%x) ==="%(label,pa,pa+length-1,payload0+pa))
        P("           0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F   0123456789ABCDEF")
        for row in range(0,length,16):
            ch=mm[payload0+pa+row:payload0+pa+row+16]
            hx=" ".join("%02x"%c for c in ch).ljust(47)
            asc="".join(chr(c) if 32<=c<127 else "." for c in ch)
            P("%08xh: %s  %s"%(pa+row,hx,asc))

    # chipset block
    cb = payload0 + memSize
    variant=u32(cb); ql=u32(cb+4); model=mm[cb+8:cb+8+ql].decode('utf-16-le','replace')
    after=cb+8+ql; cpuCount=struct.unpack_from('<i',mm,after)[0]; chipMem=u64(after+4)
    P(""); P("--- CHIPSET BLOCK @ file 0x%x ---"%cb)
    P("variant=%d  model=%r  cpuCount=%d  chipMemSize=0x%x  <<< cpuCount drives CSC present-bits"
      %(variant,model,cpuCount,chipMem))

    # HWRPB (AARM Table 26-1)
    P(""); P("--- HWRPB (AARM Table 26-1) ---")
    needle=struct.pack('<Q',HWRPB_VALID)
    pos=mm.find(needle, payload0, payload0+memSize)
    if pos<0:
        P("HWRPB NOT BUILT: validation qword 0x%016x absent from guest memory."%HWRPB_VALID)
        P("  (SRM has not constructed the HWRPB at this capture -- e.g. it panics during")
        P("   MP init before HWRPB build.  Re-run after that clears and the map will fill in.)")
    else:
        h=pos-payload0-8
        P("HWRPB base PA    = 0x%x"%h)
        P("self phys_addr   = 0x%x"%q(h+0))
        P("revision         = %d"%q(h+16))
        P("hwrpb_size       = %d"%q(h+24))
        P("primary_cpu_id   = %d"%q(h+32))
        P("page_size        = %d"%q(h+40))
        P("pa_size_bits     = %d"%u32(payload0+h+48))
        P("max_valid_asn    = %d"%q(h+56))
        P("system_type      = 0x%x"%q(h+80))
        P("system_variation = 0x%x"%q(h+88))
        P("intrclock_freq   = %d"%q(h+104))
        P("cycle_cnt_freq   = %d"%q(h+112))
        P("vptb_va          = 0x%x"%q(h+120))
        scnt=q(h+144); ssize=q(h+152); soff=q(h+160)
        P("cpu_slot_count   = %d   <<< processors the firmware built"%scnt)
        P("cpu_slot_size    = %d   cpu_slot_offset = 0x%x"%(ssize,soff))
        P("ctb_count        = %d   ctb_size=%d  ctb_offset=0x%x"%(q(h+168),q(h+176),q(h+184)))
        P("crb_offset       = 0x%x  memdsc_offset=0x%x  config_offset=0x%x  fru_offset=0x%x"
          %(q(h+192),q(h+200),q(h+208),q(h+216)))
        P("restart_rtn_va   = 0x%x  checksum=0x%x  dsrdb_offset=0x%x"%(q(h+256),q(h+288),q(h+312)))
        for s in range(min(scnt,8)):
            sp=h+soff+s*ssize; st=q(sp+0x80)
            fl=[n for b,n in [(5,"CtxValid"),(6,"PalValid"),(7,"MemValid"),(1,"RestartCap"),(0,"BIP")] if st&(1<<b)]
            P("  slot[%d] @PA 0x%x state=0x%x [%s] cpu_type=0x%x"%(s,sp,st,",".join(fl) or "-",q(sp+0xB0)))
        hexdump(h, 0x140, "HWRPB header")

    # named regions
    hexdump(0x600000, 0x80, "PAL entry (palBase 0x600000)")
    hexdump(0x602000, 0x80, "console region 0x602000 (panic record)")
    mm.close(); fh.close()
    print("\n".join(out))

if __name__=="__main__":
    main()
