#!/usr/bin/env python3
# Extract (function, file, string-literal) triples from the apisrm C sources.
# Source half of the symbolication pipeline: join its `string`->`function`
# against Ghidra's `string`->`func_addr` to produce rename pairs.
import os, csv
from collections import defaultdict

ROOT   = "/sessions/loving-kind-ritchie/mnt/EmulatR/Processor Support/Palcode/palcode/apisrm"
OUTDIR = "/sessions/loving-kind-ritchie/mnt/EmulatR/EmulatRAppUniV4/Emulatr/tools/symbolication"
OUT    = os.path.join(OUTDIR, "source_strings.csv")
MINLEN = 4   # ignore trivially short strings

KW = {"if","for","while","switch","return","sizeof","do","else","case","typedef",
      "struct","union","enum","static","extern","void","int","char","unsigned",
      "signed","long","short","const","volatile","register","goto","break",
      "continue","default","double","float"}

def lex(path):
    try: d = open(path,'r',errors='replace').read()
    except Exception: return []
    i,n,depth = 0,len(d),0
    cur=cand=None
    out=[]
    while i < n:
        c=d[i]
        if c=='/' and i+1<n and d[i+1]=='/':
            j=d.find('\n',i); i=n if j<0 else j; continue
        if c=='/' and i+1<n and d[i+1]=='*':
            j=d.find('*/',i+2); i=n if j<0 else j+2; continue
        if c=='#' and (i==0 or d[i-1]=='\n'):
            while i<n:
                j=d.find('\n',i)
                if j<0: i=n; break
                if d[j-1]=='\\': i=j+1; continue
                i=j; break
            continue
        if c=="'":
            i+=1
            while i<n and d[i]!="'":
                if d[i]=='\\': i+=1
                i+=1
            i+=1; continue
        if c=='"':
            j=i+1; buf=[]
            while j<n and d[j]!='"':
                if d[j]=='\\' and j+1<n: buf.append(d[j]); buf.append(d[j+1]); j+=2; continue
                buf.append(d[j]); j+=1
            s=''.join(buf)
            if cur and len(s)>=MINLEN: out.append((cur,s))
            i=j+1; continue
        if c.isalpha() or c=='_':
            j=i+1
            while j<n and (d[j].isalnum() or d[j]=='_'): j+=1
            ident=d[i:j]; k=j
            while k<n and d[k] in ' \t\n\r': k+=1
            if depth==0 and k<n and d[k]=='(' and ident not in KW: cand=ident
            i=j; continue
        if c=='{':
            if depth==0 and cand: cur=cand
            depth+=1; i+=1; continue
        if c=='}':
            depth-=1
            if depth<=0: depth=0; cur=None; cand=None
            i+=1; continue
        if c==';' and depth==0: cand=None; i+=1; continue
        i+=1
    return out

os.makedirs(OUTDIR,exist_ok=True)
rows=[]; nf=0
for dp,_,fs in os.walk(ROOT):
    for f in fs:
        if f.endswith('.c'):
            nf+=1
            for fn,s in lex(os.path.join(dp,f)):
                rows.append((fn,f,s))
with open(OUT,'w',newline='') as fh:
    w=csv.writer(fh); w.writerow(["function","file","string"])
    for r in rows: w.writerow(r)

sf=defaultdict(set)
for fn,_,s in rows: sf[s].add(fn)
uniq={s:next(iter(v)) for s,v in sf.items() if len(v)==1}
seed=set(uniq.values())
allf=set(fn for fn,_,_ in rows)
print(f"files={nf} rows={len(rows)} distinct_strings={len(sf)} "
      f"unique_strings={len(uniq)} funcs_with_any_string={len(allf)} "
      f"funcs_seedable_by_unique_string={len(seed)}")
print(f"CSV -> {OUT}")
