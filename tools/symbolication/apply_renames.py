# Ghidra Script (Jython).  Apply renames.csv -> setName on each function.
# Skips CONFLICT rows by default; appends _<addr> on duplicate-name collisions.
# @category EmulatR
from ghidra.program.model.symbol import SourceType
IN = r"D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\symbolication\renames.csv"
SKIP_CONFLICT = True
fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory()
applied=0; skipped=0; coll=0
fh=open(IN); fh.readline()
for line in fh:
    p=line.rstrip("\n").split(",")
    if len(p)<2: continue
    ea,name=p[0],p[1]
    flag = p[4] if len(p)>4 else "ok"
    if SKIP_CONFLICT and flag=="CONFLICT": skipped+=1; continue
    addr=af.getAddress(ea)
    fn=fm.getFunctionAt(addr) if addr else None
    if fn is None: skipped+=1; continue
    try:
        fn.setName(name, SourceType.USER_DEFINED); applied+=1
    except:
        try:
            fn.setName("%s_%s"%(name,ea.replace(":","_")), SourceType.USER_DEFINED)
            applied+=1; coll+=1
        except: skipped+=1
fh.close()
print("applied=%d (name-collisions suffixed=%d) skipped=%d"%(applied,coll,skipped))
print("Next: File > Export Program > C/C++  (or Decompiler) for the searchable text.")
