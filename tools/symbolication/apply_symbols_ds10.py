# Ghidra Script (Jython).  Apply the recovered SRM symbol table to the DS10
# program.  REQUIRES the program imported at base 0x8000 (so a CSV linked_addr
# equals the Ghidra address 1:1 -- no delta).
# Input: ds10_v7_3_symbols.csv  (name,linked_addr)
# For each symbol: rename the function at that addr; if none, create one; if
# that fails (data symbol), drop a primary label so PCs still resolve to name.
# @category EmulatR
from ghidra.program.model.symbol import SourceType
IN = r"D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\symbolication\ds10_v7_3_symbols.csv"
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
        if fn is not None:
            fn.setName(name, SourceType.USER_DEFINED); renamed+=1
        else:
            f2=createFunction(addr, name)
            if f2 is not None: created+=1
            else: createLabel(addr, name, True); labeled+=1
    except:
        uniq="%s_%s"%(name, av.replace("0x",""))
        try:
            if fn is not None: fn.setName(uniq, SourceType.USER_DEFINED); renamed+=1
            else: createLabel(addr, uniq, True); labeled+=1
        except: skipped+=1
fh.close()
print("DS10 symbols applied: renamed=%d created=%d labeled=%d skipped=%d (of ~4110)"
      % (renamed,created,labeled,skipped))
print("Then: File > Export Program > C/C++ for the searchable named listing.")
