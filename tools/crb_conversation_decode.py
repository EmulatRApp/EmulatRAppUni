#!/usr/bin/env python3
# ============================================================================
# tools/crb_conversation_decode.py -- decode a CRB-window DIAG-PC capture
#                                     into the named callback conversation
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY (JRN-SCSI-011)
#   The L1 (IOVEC) investigation needs the full conversation APB has with the
#   console through the CRB dispatch entry (VA 0x101aac60).  A DIAG-PC run
#   over the window 0x101aa000..0x101ac000 captures every retire; this tool
#   turns that capture into "who called what and what was answered".
#
# HOW (decoded from the DS20 v7.3-2 dispatch, JRN-SCSI-011 Sec 2)
#   dispatch entry 0x101aac60: save regs, CMPULE r16,#0x36 bounds check,
#     BR r0,.+0x200 -> continuation 0x101aaeb8 with r0=r10=TABLE 0x101aacb8
#   continuation: SLL r16,#3; LDL off,(table + code*8); JMP (table+off)
#     => the LDL's memAddr NAMES THE ROUTINE CODE:
#          code = (memAddr - 0x101aacb8) / 8
#   0x101aaed0 = the unsupported-code stub: r0 = 1<<63 (CBS$FAIL), restore,
#     ret.  Codes 0x00,0x07..0x0f,0x15..0x1f,0x24..0x2f,0x31..0x36 land there.
#   get_env handler 0x101ab430: walks the env-desc list at 0x101ab130
#     ({desc_ptr, env_id} pairs, stride 0x10); the matched-entry desc gives
#     the value bytes.  Env IDs per apisrm apu_callbacks_def.h envid$*.
#   Console VA -> PA via the CRB map (2 entries on DS20 v7.3-2:
#     0x10000000->0x2000 x0x2dd pages, 0x105ba000->0x3ff02000 x0x7f pages);
#     read live from the snapshot, not hardcoded.
#
# USAGE
#   python3 tools/crb_conversation_decode.py <run.log> <snapshot.axpsnap>
#            [--hwrpb <PA>] [--dispatch <VA>]
#   <run.log>  = a run with EMULATR_DIAG_PCLO=0x101aa000 PCHI=0x101ac000
#   <snapshot> = any snapshot of the same console era (env values are read
#                from it; use a post-boot snapshot for exact value fidelity)
# ============================================================================
import re, sys, os, struct, mmap, collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dump_crb import find_payload0            # snapshot payload locator

NAMES = {0x01: 'getc', 0x02: 'puts', 0x03: 'reset_term', 0x04: 'set_term_int',
         0x05: 'set_term_ctl', 0x06: 'process_keycode', 0x10: 'open',
         0x11: 'close', 0x12: 'ioctl', 0x13: 'read', 0x14: 'write',
         0x20: 'set_env', 0x21: 'reset_env', 0x22: 'get_env', 0x23: 'save_env'}
ENVIDS = {1: 'auto_action', 2: 'boot_dev', 3: 'bootcmd_dev', 4: 'booted_dev',
          5: 'boot_file', 6: 'booted_file', 7: 'boot_osflags',
          8: 'booted_osflags', 9: 'boot_reset', 10: 'dump_dev',
          11: 'enable_audit', 12: 'license', 13: 'char_set', 14: 'language',
          15: 'tty_dev'}

RX = re.compile(r'DIAG-PC: cyc=(\d+) pc=0x([0-9a-f]+) .*fault=(\d+) '
                r'memAddr=0x([0-9a-f]+)')


def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    log, snap = sys.argv[1], sys.argv[2]
    hwrpb = 0x2000
    a = sys.argv[3:]
    i = 0
    while i < len(a):
        if a[i] == '--hwrpb': hwrpb = int(a[i+1], 0); i += 2
        else: print("unknown option %s" % a[i]); sys.exit(2)

    fh = open(snap, 'rb'); sz = os.path.getsize(snap)
    mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
    pay, _ = find_payload0(mm, sz)
    if pay is None: print("*** snapshot payload not located ***"); sys.exit(3)

    def q(pa):
        fh.seek(pay + pa); return struct.unpack('<Q', fh.read(8))[0]

    def rd(pa, n):
        fh.seek(pay + pa); return fh.read(n)

    # CRB: dispatch entry + VA->PA map
    crb = hwrpb + q(hwrpb + 0xc0)
    disp_pa = q(crb + 0x08)
    dispatch = q(disp_pa + 8)                 # entry VA (descriptor + 8)
    nmap = q(crb + 0x20)
    maps = []
    for k in range(nmap):
        va, pa, pg = q(crb + 0x30 + k*0x18), q(crb + 0x38 + k*0x18), \
                     q(crb + 0x40 + k*0x18)
        maps.append((va, pa, pg * 0x2000))

    def v2p(va):
        for base, pa, ln in maps:
            if base <= va < base + ln: return pa + (va - base)
        return None

    table = dispatch + 0x58                   # CMPULE..BR prologue = 0x58
    jmp_pc = None                             # the JMP retire pc (cont+0x14)
    # continuation = BR target: dispatch+0x54 is BR r0,.+0x200
    cont = dispatch + 0x58 + 0x200
    jmp_pc = cont + 0x14
    print("# dispatch=0x%x  table=0x%x  continuation=0x%x  jmp=0x%x"
          % (dispatch, table, cont, jmp_pc))
    print("# CRB map: " + "  ".join("VA 0x%x->PA 0x%x len 0x%x" % m
                                    for m in maps))

    recs = []
    for line in open(log, errors='replace'):
        m = RX.search(line)
        if m:
            recs.append((int(m.group(1)), int(m.group(2), 16),
                         int(m.group(3)), int(m.group(4), 16)))
    segs, cur = [], None
    for cyc, pc, fault, mem in recs:
        if pc == dispatch and fault == 0:
            if cur is not None: segs.append(cur)
            cur = {'cyc': cyc, 'r': []}
            continue
        if cur is not None: cur['r'].append((pc, mem))
    if cur is not None: segs.append(cur)

    GETENV_WALK_ID = 0x101ab478 - 0x101aac60 + dispatch   # keep VA-relative
    conv = []
    for s in segs:
        code = None
        for pc, mem in s['r']:
            # the table LDL retires at cont+0xc with memAddr = table+code*8
            if table <= mem < table + 0x37*8 and (mem - table) % 8 == 0 \
               and pc == cont + 0xc:
                code = (mem - table) // 8
                break
        name = NAMES.get(code, '0x%02x' % code if code is not None else '?')
        env = val = None
        if code == 0x22:
            ids = [mem for pc, mem in s['r'] if pc == GETENV_WALK_ID]
            if ids:
                eid = q(v2p(ids[-1]))
                env = ENVIDS.get(eid, 'envid_0x%x' % eid)
                srcs = [mem for pc, mem in s['r']
                        if v2p(mem) is not None and mem >= maps[-1][0]]
                if srcs:
                    b = rd(v2p(min(srcs)) + 4, 64)   # skip 4-byte length
                    val = b.split(b'\0')[0].decode('ascii', 'replace')
        conv.append((s['cyc'], code, name, env, val, len(s['r'])))

    print("\n=== %d callbacks ===" % len(conv))
    hist = collections.Counter(c[2] for c in conv)
    for nm, n in hist.most_common(): print("  %-16s x%d" % (nm, n))
    print("\nsequence (runs collapsed):")
    i = 0
    while i < len(conv):
        j = i
        while j+1 < len(conv) and conv[j+1][2] == conv[i][2] \
              and conv[j+1][3] == conv[i][3]:
            j += 1
        cyc, code, nm, env, val, n = conv[i]
        extra = ""
        if env is not None:
            extra = "  %s -> %r" % (env, val)
        tag = "#%03d" % i if j == i else "#%03d-#%03d" % (i, j)
        print("  %-10s cyc=%d %-16s recs=%d%s" % (tag, cyc, nm, n, extra))
        i = j + 1


if __name__ == "__main__":
    main()
