# EmulatR firmware listing export -- defined code units only.
# Emits TAB-separated: addr  bytes  mnemonic  operands  label  xrefs  eol
# Skips undefined bytes (the "?? 00h" noise) and uninitialized blocks.
# Output filename mirrors the program (ROM image) name, with .lst.
# Run from the Ghidra Script Manager against the analyzed program.
# ASCII(128) output only.
#
# @category EmulatR

import os
from ghidra.program.model.listing import Instruction

prog = getCurrentProgram()
listing = prog.getListing()
st = prog.getSymbolTable()
refMgr = prog.getReferenceManager()
mem = prog.getMemory()

name = prog.getName()
outdir = os.getcwd()
outpath = os.path.join(outdir, name + ".lst")


def hexbytes(cu):
    try:
        bs = cu.getBytes()
    except:
        return ""
    return " ".join("%02x" % (b & 0xff) for b in bs)


def label_at(addr):
    sym = st.getPrimarySymbol(addr)
    return sym.getName() if sym is not None else ""


def xrefs_to(addr):
    out = []
    for ref in refMgr.getReferencesTo(addr):
        out.append("%08x" % ref.getFromAddress().getOffset())
    return ",".join(out)


def eol(addr):
    c = listing.getComment(0, addr)  # 0 == CodeUnit.EOL_COMMENT
    if not c:
        return ""
    return c.replace("\r", " ").replace("\n", " ")


def ghidra_version():
    try:
        from ghidra.framework import Application
        return Application.getApplicationVersion()
    except:
        return "unknown"


# Gather instructions and DEFINED data only; undefined bytes never enter.
items = []
for ins in listing.getInstructions(True):
    items.append((ins.getAddress().getOffset(), ins))
for dat in listing.getDefinedData(True):
    items.append((dat.getAddress().getOffset(), dat))
items.sort(key=lambda t: t[0])

f = open(outpath, "w")
f.write("; program    : %s\n" % name)
f.write("; sha256     : %s\n" % prog.getExecutableSHA256())
f.write("; image base : %s\n" % prog.getImageBase())
f.write("; language   : %s\n" % prog.getLanguageID())
f.write("; ghidra     : %s\n" % ghidra_version())
f.write("; columns    : addr\tbytes\tmnemonic\toperands\tlabel\txrefs\teol\n")

for off, cu in items:
    addr = cu.getAddress()
    blk = mem.getBlock(addr)
    if blk is None or not blk.isInitialized():
        continue
    a = "%08x" % off
    b = hexbytes(cu)
    if isinstance(cu, Instruction):
        mn = cu.getMnemonicString()
        n = cu.getNumOperands()
        ops = ", ".join(cu.getDefaultOperandRepresentation(i) for i in range(n))
    else:
        mn = cu.getMnemonicString()
        ops = cu.getDefaultValueRepresentation()
    lbl = label_at(addr)
    xr = xrefs_to(addr)
    ec = eol(addr)
    f.write("%s\t%s\t%s\t%s\t%s\t%s\t%s\n" % (a, b, mn, ops, lbl, xr, ec))

f.close()
print("wrote %d code units to %s" % (len(items), outpath))
