# Ghidra Script (Jython). Apply a recovered SRM symbol map (name,entry) to the
# current program. REQUIRES the decompressed image imported at base 0x8000
# (entry values are then Ghidra addresses 1:1). Set IN to the right CSV:
#   ds10_v7_3_symbols_entries.csv  or  es45_v7_3_symbols_entries.csv
# @category EmulatR
from ghidra.program.model.symbol import SourceType
IN = r"D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\symbolication\ds10_v7_3_symbols_entries.csv"
fm = currentProgram.getFunctionManager()
renamed=created=labeled=skipped=0
fh=open(IN); fh.readline()
for line in fh:
    line=line.strip()
    if not line: continue
    p=line.split(",")
    if len(p)<2: continue
    name=p[0].strip(); av=p[1].strip()
    try: addr=toAddr(long(av,16))
    except: skipped+=1; continue
    fn=fm.getFunctionAt(addr)
    try:
        if fn is not None: fn.setName(name, SourceType.USER_DEFINED); renamed+=1
        else:
            f2=createFunction(addr, name)
            if f2 is not None: created+=1
            else: createLabel(addr, name, True); labeled+=1
    except:
        try:
            u="%s_%s"%(name, av.replace("0x",""))
            if fn is not None: fn.setName(u, SourceType.USER_DEFINED); renamed+=1
            else: createLabel(addr, u, True); labeled+=1
        except: skipped+=1
fh.close()
print("applied: renamed=%d created=%d labeled=%d skipped=%d" % (renamed,created,labeled,skipped))
print("Then: File > Export Program > C/C++ for the named, searchable listing.")
