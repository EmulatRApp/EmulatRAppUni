#!/usr/bin/env python3
# Join source_strings.csv (string->source_func) with ghidra_strings.tsv
# (string->ghidra func_entry) to produce renames.csv (func_entry,name,votes).
# Both sides represent control chars the same way (\n,\t escaped literally), so
# the primary match is exact; a backslash/whitespace-stripped key is the fallback.
import os, csv
from collections import defaultdict

D   = "/sessions/loving-kind-ritchie/mnt/EmulatR/EmulatRAppUniV4/Emulatr/tools/symbolication"
SRC = os.path.join(D,"source_strings.csv")
GH  = os.path.join(D,"ghidra_strings.tsv")
OUT = os.path.join(D,"renames.csv")

def norm(s):                       # fallback key: drop backslashes + collapse ws
    return "".join(s.replace("\\","").split())

# source: string -> set(func)   (+ normalized index)
s_exact=defaultdict(set); s_norm=defaultdict(set)
with open(SRC,newline='') as fh:
    for fn,fil,s in csv.reader(fh):
        if fn=="function": continue
        s_exact[s].add(fn)
        if len(norm(s))>=6: s_norm[norm(s)].add(fn)

# ghidra: string -> set(func_entry)
g_exact=defaultdict(set); g_norm=defaultdict(set)
if not os.path.exists(GH):
    print("MISSING %s -- run dump_ghidra_strings.py in Ghidra first."%GH); raise SystemExit
with open(GH) as fh:
    next(fh,None)
    for line in fh:
        p=line.rstrip("\n").split("\t")
        if len(p)<2: continue
        s,ep=p[0],p[1]
        g_exact[s].add(ep)
        if len(norm(s))>=6: g_norm[norm(s)].add(ep)

# vote: for each string matched 1:1 (one source func, one ghidra entry), cast a
# vote (entry -> source_func).  exact matches first, then normalized fallback.
votes=defaultdict(lambda: defaultdict(int))
def cast(smap_src, smap_gh):
    for s,funcs in smap_src.items():
        if len(funcs)!=1: continue
        eps=smap_gh.get(s)
        if not eps or len(eps)!=1: continue
        ep=next(iter(eps)); fn=next(iter(funcs))
        votes[ep][fn]+=1
cast(s_exact,g_exact)
cast(s_norm,g_norm)

rows=[]
for ep,cand in votes.items():
    name,v=max(cand.items(),key=lambda kv:kv[1])
    total=sum(cand.values()); conflict=len(cand)>1
    rows.append((ep,name,v,total,"CONFLICT" if conflict else "ok"))
rows.sort()
with open(OUT,'w',newline='') as fh:
    w=csv.writer(fh); w.writerow(["func_entry","name","votes","total_votes","flag"])
    for r in rows: w.writerow(r)

named=len(rows); clean=sum(1 for r in rows if r[4]=="ok")
print("ghidra functions named=%d (clean=%d, conflict=%d) -> %s"%(named,clean,named-clean,OUT))
