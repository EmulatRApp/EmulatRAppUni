# ============================================================================
# ApplySrmSymbols.py -- recover SRM function names from the embedded name table
# ============================================================================
# Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (tooling)
#
# WHAT THIS DOES
#   The DS10 SRM image carries a packed table of NUL-terminated symbol-name
#   strings (krn$_idle, timer_check, yy_reset, eg_write, ...).  There is NO
#   {address,name} array in this build (ie.c ships pdlist[] = {{0,0}} and
#   find_pd() returns 0 unless #if EXTRA), so names are not paired with code
#   statically.  Instead each name is referenced -- from code (GP-relative,
#   e.g. krn$_create(entry,...,"name") or a routine that logs its own name)
#   or from a dispatch table.  Ghidra has ALREADY resolved those GP-relative
#   and pointer references during analysis, so we lean on getReferencesTo().
#
#   PASS A (string -> code xref): for every name string, gather the functions
#   that reference it from CODE.  If exactly one default-named (FUN_xxxx)
#   function references it, rename that function to the symbol and add a
#   plate comment recording the evidence (name-string address).
#
#   PASS B (data table): names referenced only from DATA are dispatch-table
#   entries.  We follow the linkage pointer sitting next to the name pointer,
#   resolve it to a function via Ghidra, and rename+comment if it is a single
#   default-named function.  Best-effort; everything is reported.
#
# SAFETY
#   - Never overwrites a name you have already set (only renames FUN_xxxx).
#   - Adds a plate comment on every change so provenance is auditable.
#   - Fully reversible with Ghidra Undo (one transaction).
#   - Prints a full report; ambiguous cases are listed, not guessed.
#
# USAGE (GUI): open the program, run from Script Manager (category EmulatR).
# @category EmulatR
# ============================================================================

from ghidra.program.model.symbol import SourceType
from ghidra.program.model.data import TerminatedStringDataType

# ---- Config: bounds of the embedded name-string table (file off == image VA)
NAME_LO = 0x1A0000
NAME_HI = 0x1B0000
RENAME  = True     # user choice: rename + comment (set False for comment-only)

mem  = currentProgram.getMemory()
fm   = currentProgram.getFunctionManager()
base = currentProgram.getImageBase()

def A(v):
    # address in the program's default space at flat offset v
    return base.getNewAddress(v)

def rb(v):
    return mem.getByte(A(v)) & 0xFF

def read_name(p):
    # return (name, byte_len_incl_nul) if a clean symbol-name string starts at p
    if rb(p - 1) != 0 and p != NAME_LO:
        return None
    s = []
    q = p
    while q < NAME_HI:
        c = rb(q)
        if c == 0:
            break
        # allow A-Z a-z 0-9 $ _ .
        if not (48 <= c <= 57 or 65 <= c <= 90 or 97 <= c <= 122 or c in (0x24, 0x5f, 0x2e)):
            return None
        s.append(chr(c))
        q += 1
        if q - p > 47:
            return None
    if not s:
        return None
    c0 = ord(s[0])
    if not (65 <= c0 <= 90 or 97 <= c0 <= 122 or c0 == 0x5f):
        return None
    return ("".join(s), q - p + 1)

def build_name_table():
    names = []          # list of (addr, name)
    p = NAME_LO
    while p < NAME_HI:
        r = read_name(p)
        if r:
            nm, ln = r
            names.append((p, nm))
            p += (ln + 7) & ~7      # advance to next 8-aligned slot
        else:
            p += 4
    return names

def funcs_referencing(addr_val, from_code):
    # set of Function objects that reference address_val; from_code gates on
    # whether the referring instruction is inside a function (code) vs data.
    out = {}
    refs = getReferencesTo(A(addr_val))
    for r in refs:
        fa = r.getFromAddress()
        fn = fm.getFunctionContaining(fa)
        if from_code and fn is None:
            continue
        if (not from_code) and fn is not None:
            continue
        if fn is not None:
            out[fn.getEntryPoint().getOffset()] = fn
        else:
            out[fa.getOffset()] = None      # data-site address, fn resolved later
    return out, refs

def apply(fn, name, why):
    cur = fn.getName()
    if not cur.startswith("FUN_"):
        return "skip(named:%s)" % cur
    if RENAME:
        fn.setName(name, SourceType.USER_DEFINED)
    setPlateComment(fn.getEntryPoint(), "SRM symbol: %s  [%s]" % (name, why))
    return "renamed"

# ----------------------------------------------------------------------------
print("ApplySrmSymbols: scanning name table 0x%06X-0x%06X ..." % (NAME_LO, NAME_HI))
names = build_name_table()
print("  found %d candidate name strings" % len(names))

renamed = 0
ambiguous = []
datacases = []
skipped = 0

monitor.initialize(len(names))
for naddr, nm in names:
    if monitor.isCancelled():
        break
    monitor.incrementProgress(1)

    # make the string real so xrefs/labels are clean (ignore if already typed)
    try:
        if getDataAt(A(naddr)) is None:
            createData(A(naddr), TerminatedStringDataType())
    except:
        pass

    code_fns, _ = funcs_referencing(naddr, from_code=True)
    if len(code_fns) == 1:
        fn = list(code_fns.values())[0]
        res = apply(fn, nm, "code-xref 0x%06X" % naddr)
        if res == "renamed":
            renamed += 1
        else:
            skipped += 1
        continue
    if len(code_fns) > 1:
        ambiguous.append((nm, naddr, len(code_fns)))
        continue

    # no code xref -> data table entry; remember for PASS B
    data_fns, drefs = funcs_referencing(naddr, from_code=False)
    if drefs.hasNext() if hasattr(drefs, "hasNext") else True:
        datacases.append((nm, naddr))

# ---- PASS B: dispatch-table names (best effort) ----------------------------
# For each data-referenced name, look at the 32-bit words adjacent to the name
# pointer in the table for one that Ghidra recognizes as a function entry.
print("ApplySrmSymbols: PASS B over %d data-table names ..." % len(datacases))
for nm, naddr in datacases:
    if monitor.isCancelled():
        break
    for r in getReferencesTo(A(naddr)):
        slot = r.getFromAddress().getOffset()
        hit = None
        for d in range(-24, 28, 4):
            q = slot + d
            if d == 0:
                continue
            try:
                w = mem.getInt(A(q)) & 0xFFFFFFFF
            except:
                continue
            f = fm.getFunctionAt(A(w))
            if f is not None:
                hit = f
                break
            # one level of linkage-descriptor indirection
            try:
                w2 = mem.getInt(A(w)) & 0xFFFFFFFF
                f2 = fm.getFunctionAt(A(w2))
                if f2 is not None:
                    hit = f2
                    break
            except:
                pass
        if hit is not None:
            res = apply(hit, nm, "table 0x%06X" % slot)
            if res == "renamed":
                renamed += 1
            break

# ---- report ----------------------------------------------------------------
print("=" * 60)
print("ApplySrmSymbols DONE")
print("  renamed + commented : %d" % renamed)
print("  skipped (already named): %d" % skipped)
print("  ambiguous (multi-xref, left for review): %d" % len(ambiguous))
if ambiguous:
    print("  --- ambiguous names (referenced by >1 function) ---")
    for nm, naddr, k in ambiguous[:60]:
        print("    %-28s @0x%06X  (%d functions)" % (nm, naddr, k))
print("=" * 60)
