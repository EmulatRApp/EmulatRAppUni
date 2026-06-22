# Ghidra Script (Jython/Python2).  Category: EmulatR
# Dump every defined string and the ENTRY address of each function that
# references it -> ghidra_strings.tsv.  Run from Ghidra Script Manager with the
# DS20 decompressed program open.  Tab-separated; string field is sanitized
# (\n,\r,\t escaped) so it stays one line.
# @category EmulatR
OUT = r"D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\symbolication\ghidra_strings.tsv"

def san(s):
    return s.replace("\\","\\\\").replace("\t","\\t").replace("\r","\\r").replace("\n","\\n")

listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
refmgr  = currentProgram.getReferenceManager()

n = 0
fh = open(OUT, "w")
fh.write("string\tfunc_entry\tghidra_name\n")
for d in listing.getDefinedData(True):
    dt = d.getDataType()
    tn = dt.getName().lower()
    if ("string" not in tn) and ("char" not in tn):
        continue
    val = d.getValue()
    if val is None:
        continue
    s = unicode(val)
    if len(s) < 4:
        continue
    addr = d.getAddress()
    seen = set()
    for r in refmgr.getReferencesTo(addr):
        fn = fm.getFunctionContaining(r.getFromAddress())
        if fn is None:
            continue
        ep = fn.getEntryPoint().toString()
        if ep in seen:
            continue
        seen.add(ep)
        line = u"%s\t%s\t%s\n" % (san(s), ep, fn.getName())
        fh.write(line.encode("utf-8", "replace"))
        n += 1
fh.close()
print("wrote %d (string,func) rows -> %s" % (n, OUT))
