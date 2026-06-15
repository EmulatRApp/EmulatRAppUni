#!/usr/bin/env python3
# alpha_disasm.py -- minimal Alpha (EV6) disassembler for inspecting raw
# firmware / decompressed PAL images. NOT a full disassembler: covers the
# integer/memory/branch/PAL formats we need for PAL + console code reading.
#
# Usage:
#   python3 alpha_disasm.py <image-file> <start-offset-hex> [count]
#   python3 alpha_disasm.py decompressed.bin 0x8ab0 40
#
# Offsets are byte offsets into the file. Little-endian. Each line prints
# file-offset, raw word, and a decoded mnemonic. Useful with the
# host_decompressor out/*.bin images to read PAL routines by offset.
import struct, sys

mem={0x08:"LDA",0x09:"LDAH",0x0a:"LDBU",0x0b:"LDQ_U",0x0c:"LDWU",0x0d:"STW",
0x0e:"STB",0x0f:"STQ_U",0x20:"LDF",0x21:"LDG",0x22:"LDS",0x23:"LDT",0x24:"STF",
0x25:"STG",0x26:"STS",0x27:"STT",0x28:"LDL",0x29:"LDQ",0x2a:"LDL_L",0x2b:"LDQ_L",
0x2c:"STL",0x2d:"STQ",0x2e:"STL_C",0x2f:"STQ_C"}
br={0x30:"BR",0x31:"FBEQ",0x32:"FBLT",0x33:"FBLE",0x34:"BSR",0x35:"FBNE",
0x36:"FBGE",0x37:"FBGT",0x38:"BLBC",0x39:"BEQ",0x3a:"BLT",0x3b:"BLE",
0x3c:"BLBS",0x3d:"BNE",0x3e:"BGE",0x3f:"BGT"}
hw={0x19:"HW_MFPR",0x1b:"HW_LD",0x1d:"HW_MTPR",0x1e:"HW_RET",0x1f:"HW_ST",0x1a:"JMP/JSR",0x18:"MISC"}
op10={0x00:"ADDL",0x02:"S4ADDL",0x09:"SUBL",0x0b:"S4SUBL",0x12:"S8ADDL",0x1d:"CMPULT",
0x20:"ADDQ",0x22:"S4ADDQ",0x29:"SUBQ",0x2b:"S8SUBQ",0x2d:"CMPEQ",0x32:"S8ADDQ",
0x3d:"CMPULE",0x40:"ADDLV",0x49:"SUBLV",0x4d:"CMPLT",0x60:"ADDQV",0x69:"SUBQV",0x6d:"CMPLE"}
op11={0x00:"AND",0x08:"BIC",0x14:"CMOVLBS",0x16:"CMOVLBC",0x20:"BIS",0x24:"CMOVEQ",
0x26:"CMOVNE",0x28:"ORNOT",0x40:"XOR",0x44:"CMOVLT",0x46:"CMOVGE",0x48:"EQV",
0x61:"AMASK",0x64:"CMOVLE",0x66:"CMOVGT"}
op12={0x02:"MSKBL",0x06:"EXTBL",0x0b:"INSBL",0x12:"MSKWL",0x16:"EXTWL",0x1b:"INSWL",
0x22:"MSKLL",0x26:"EXTLL",0x2b:"INSLL",0x30:"ZAP",0x31:"ZAPNOT",0x32:"MSKQL",
0x34:"SRL",0x36:"EXTQL",0x39:"SLL",0x3b:"INSQL",0x3c:"SRA",0x52:"MSKWH",
0x57:"INSWH",0x5a:"EXTWH",0x62:"MSKLH",0x67:"INSLH",0x6a:"EXTLH",0x72:"MSKQH",
0x77:"INSQH",0x7a:"EXTQH"}

def sx(v,b):
    m=1<<(b-1); return (v^m)-m

def dis(w):
    o=w>>26; ra=(w>>21)&31; rb=(w>>16)&31
    if o in mem:
        return f"{mem[o]:7} r{ra},{sx(w&0xffff,16):#x}(r{rb})"
    if o in br:
        return f"{br[o]:7} r{ra}, .{sx(w&0x1fffff,21)*4:+#x}"
    if o==0x1e:
        sub=(w>>14)&3; nm={0:'HW_JMP',1:'HW_JSR',2:'HW_RET',3:'HW_COROUTINE'}.get(sub,'HW_RET')
        return f"{nm:7} (r{rb})   [stall={(w>>13)&1} hint={w&0x1fff:#x}]"
    if o in (0x19,0x1d):
        return f"{hw[o]:7} r{ra},IPR={w&0xffff:#x}"
    if o in (0x1b,0x1f):
        return f"{hw[o]:7} r{ra},{sx(w&0xfff,12):#x}(r{rb}) [flags={(w>>12)&0xf:#x}]"
    if o in (0x10,0x11,0x12):
        islit=(w>>12)&1; lit=(w>>13)&0xff; rc=w&31; func=(w>>5)&0x7f
        tbl={0x10:op10,0x11:op11,0x12:op12}[o]; nm=tbl.get(func,f"OP{o:#x}.{func:#x}")
        src=f"#{lit:#x}" if islit else f"r{rb}"
        return f"{nm:7} r{ra},{src},r{rc}"
    if o==0x18: return f"MISC    {w&0xffff:#x}"
    if o==0x1a: return f"JMP/JSR r{ra},(r{rb}),hint={w&0x3fff:#x}"
    if o in (0x14,0x15,0x16,0x17): return f"FP      op={o:#x} {w&0x3ffffff:#x}"
    return f".long   {w:#010x}  (op={o:#x})"

def main():
    if len(sys.argv)<3:
        print(__doc__); sys.exit(1)
    path=sys.argv[1]; start=int(sys.argv[2],0); n=int(sys.argv[3],0) if len(sys.argv)>3 else 32
    d=open(path,"rb").read()
    for i in range(n):
        off=start+i*4
        if off+4>len(d): break
        w=struct.unpack_from("<I",d,off)[0]
        print(f"  {off:#08x}: {w:08x}  {dis(w)}")

if __name__=="__main__":
    main()
