#!/usr/bin/env python3
# v2: symtab value -> procedure descriptor; CODE ENTRY = u32 at descriptor+8.
import sys, struct, os
def name_index(B):
    idx={}; i=0; n=len(B)
    while i<n:
        if 33<=B[i]<=126:
            j=i
            while j<n and 32<=B[j]<=126: j+=1
            if j<n and B[j]==0 and j-i>=2: idx[i]=B[i:j].decode('ascii','replace')
            i=j+1
        else: i+=1
    return idx
def parse(path,out):
    B=open(path,'rb').read(); n=len(B); BASE=0x8000; idx=name_index(B)
    # locate SYM array: records {desc_ptr(u32), name_ptr(u32)}; name_ptr-BASE in idx,
    # desc_ptr-BASE in range, and *(desc_ptr+8) is a plausible code entry.
    def u32(addr):
        o=addr-BASE
        return struct.unpack_from("<I",B,o)[0] if 0<=o<n-4 else None
    best=(None,0); i=0; rs=None; cnt=0; gap=0
    while i+8<=n:
        dptr,nptr=struct.unpack_from("<II",B,i)
        ok = (0<=nptr-BASE<n and (nptr-BASE) in idx and 0<=dptr-BASE<n-12)
        if ok:
            ent=u32(dptr+8)
            ok = ent is not None and 0x8000<=ent<=BASE+n
        if ok:
            if rs is None: rs=i; cnt=1; gap=0
            else: cnt+=1; gap=0
        else:
            if rs is not None:
                gap+=1
                if gap>6:
                    if cnt>best[1]: best=(rs,cnt)
                    rs=None; cnt=0; gap=0
        i+=8
    if rs is not None and cnt>best[1]: best=(rs,cnt)
    start,run=best
    rows=[]; seen=set(); k=start
    miss=0
    while k+8<=n:
        dptr,nptr=struct.unpack_from("<II",B,k)
        nm=idx.get(nptr-BASE); ent=u32(dptr+8)
        if nm and ent is not None and 0x8000<=ent<=BASE+n:
            if nm not in seen:
                seen.add(nm); rows.append((nm,ent)); miss=0
        else:
            miss+=1
            if k>start+run*8 and miss>6: break
        k+=8
    with open(out,'w') as fh:
        fh.write("name,entry\n")
        for nm,e in rows: fh.write("%s,0x%08x\n"%(nm,e))
    return start,run,len(rows)

path=sys.argv[1]; out=sys.argv[2]
start,run,ncsv=parse(path,out)
print("sym_array_off=0x%x run=%d csv_rows=%d -> %s"%(start or 0,run,ncsv,out))
# validate known
import csv
d=dict((r[0],r[1]) for r in csv.reader(open(out)) if r and r[0]!="name")
for nm in ("probex","ddb_startup","powerup","platform","start_secondary"):
    print("   %-16s %s"%(nm, d.get(nm,"(absent)")))
