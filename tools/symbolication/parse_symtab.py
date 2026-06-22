#!/usr/bin/env python3
import sys, struct
def name_index(B):
    idx={}; i=0; n=len(B)
    while i<n:
        if 33<=B[i]<=126:
            j=i
            while j<n and 32<=B[j]<=126: j+=1
            if j<n and B[j]==0 and (j-i)>=2:
                idx[i]=B[i:j].decode('ascii','replace')
            i=j+1
        else: i+=1
    return idx
def parse(path):
    B=open(path,'rb').read(); n=len(B); idx=name_index(B)
    best=None
    for base in (0x8000,0x0,0x10000,0x20000,0x4000):
        # valid record: name_ptr-base lands on a name; value is a linked addr in [base, base+n]
        valid=[0]*((n//8)+1)
        i=0
        while i+8<=n:
            value,nptr=struct.unpack_from("<II",B,i)
            off=nptr-base
            if 0<=off<n and off in idx and base<=value<=base+n:
                valid[i//8]=1
            i+=8
        # densest window allowing gaps<=4
        rs=None; cnt=0; gap=0; bestrun=(None,0)
        for k in range(len(valid)):
            if valid[k]:
                if rs is None: rs=k*8; cnt=1; gap=0
                else: cnt+=1; gap=0
            else:
                if rs is not None:
                    gap+=1
                    if gap>4:
                        if cnt>bestrun[1]: bestrun=(rs,cnt)
                        rs=None; cnt=0; gap=0
        if rs is not None and cnt>bestrun[1]: bestrun=(rs,cnt)
        tot=sum(valid)
        if best is None or bestrun[1]>best[3]:
            best=(base,bestrun[0],tot,bestrun[1])
    base,start,tot,runlen=best
    syms=[]
    if start is not None:
        k=start
        while k+8<=n:
            value,nptr=struct.unpack_from("<II",B,k)
            nm=idx.get(nptr-base)
            if nm and base<=value<=base+n: syms.append((nm,value))
            elif k>start+runlen*8+64: break  # past the array
            k+=8
    return base,start,tot,runlen,len(idx),syms
for path in sys.argv[1:]:
    base,start,tot,runlen,nnames,syms=parse(path)
    # dedup keep first
    seen=set(); rows=[]
    for nm,v in syms:
        if nm in seen: continue
        seen.add(nm); rows.append((nm,v))
    print("=== %s ===" % path.split('/')[-1])
    print("  base=0x%x sym_off=%s total_valid=%d run=%d names=%d unique_syms=%d"
          % (base, ("0x%x"%start) if start else "None", tot, runlen, nnames, len(rows)))
    want={"ddb_startup","start_secondary","start_secondaries","powerup","halt_switch_in","platform"}
    fnd={nm:v for nm,v in rows if nm in want}
    for nm in sorted(want):
        print("    %-20s %s" % (nm, ("0x%08x"%fnd[nm]) if nm in fnd else "(absent)"))
    out=path.rsplit('/',1)[-1].replace("decompressed_","").replace(".bin","")+"_symbols.csv"
    import os
    op=os.path.join("/sessions/loving-kind-ritchie/mnt/EmulatR/EmulatRAppUniV4/Emulatr/tools/symbolication",out)
    with open(op,'w') as fh:
        fh.write("name,linked_addr\n")
        for nm,v in rows: fh.write("%s,0x%08x\n"%(nm,v))
    print("    -> %s (%d syms)"%(op,len(rows)))
