<!--
EmulatR V5 -- Session Journal JRN-VMB-018 PART 2
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-018 PART 2 -- APB.EXE static analysis: the NOIOVEC decision module
#                       is a TABLE-DRIVEN PATH MATCHER whose database is
#                       compiled INTO APB; the mismatch is in the RUNTIME INPUT.

    Doc id   : JRN-VMB-018 P2..P4 (addenda to JRN-VMB-018)
    Date     : 2026-07-24 (P2 static analysis), 2026-07-24/25 (P3 run
               readout, P4 root divergence)
    Status   : ROOT DIVERGENCE FOUND (P4): EmulatR delivers a TRUNCATED
               registered-format BOOTED_DEV string ("IDE 0 105 ...", len
               14) where the console source mandates the 19-char
               "IDE 0 105 0 0 0 0 0".  APB's grammar exhausts after the
               missing fields -> 0x158284 -> NOIOVEC.  NO code changed.
               PENDING: PA-WATCH probe + `show booted_dev` (P4 Sec) to
               split compose-short vs copy-clipped; then the fix.
    Map      : Sec 1-7 = P2 static analysis of APB.EXE (pre-run; two
               reader notes below correct assumptions the runs
               overturned).  P3 = confirmation-run readout.  P4 = the
               string truncation finding + next probes.
    Source   : D:\EmulatR\Processor Support\OpenVMS\APB.EXE;1 (628224 bytes,
               md5 25cf792cca13950a46d3bbb5222a2d8a).  VERIFIED byte-identical
               at every checked landmark to the image on the boot media
               (words at VA 0x20000000 and 0x2000a374 match JRN-VMB-016 2.2 /
               JRN-VMB-017 P2.2 exactly).  This file IS the running APB.
    Method   : EIHD/EISD parse; alpha disassembly (binutils-multiarch,
               objdump -b binary -m alpha, image body mapped VBN2 -> VA
               0x20000000); literal-pool + procedure-descriptor resolution;
               apisrm gct.h cross-checks.

--------------------------------------------------------------------------------
## 1. Image facts

- OpenVMS Alpha image, EIHD majorid 3.  ONE image section: VBN 2 (file offset
  0x200) -> VA 0x20000000, length 0x99400 = exactly the 1226 blocks the boot
  block loads.  file_offset = VA - 0x1FFFFE00.
- Transfer address 0x200000a8.  Image name "APB", ident "A13-03",
  linker "X-8".
- EIHS (symbol/debug) VBN fields are ALL ZERO: **no DST and no GST**.  Real
  VMS symbol names are NOT recoverable from this image; every routine below
  is named functionally.  (APB_BOOTP.EXE;1 sits beside it and was not
  examined; same format expected.)
- The APB message table is counted-string text at VA 0x2006bf00-0x2006c31x;
  NOIOVEC record at 0x2006c080, between ALOBTADP (0x2006c058) and
  NOSUCHDRVR (0x2006c0a8) -- exactly as JRN-VMB-018 placed it.

## 2. The condition values 0x158284 / 0x15828C

- Decode: facility 0x15 (21), message number 0x1050 (4176) and 0x1051
  (4177), severity 4 (SEVERE).  (JRN-VMB-018 Sec 1.5's "msg 1104" was a
  miscount; the mechanism reading is unchanged.)
- They are the ONLY facility-0x15 conditions anywhere in the image, each
  present twice (literal pools 0x20065320/0x20065598 and 0x200652b0/
  0x20065528 -- one pool per copy of the module family).  They are
  module-private sentinels ("no match here, continue" / sibling), not
  message-file conditions; the only user-visible text is NOIOVEC.

## 3. What the decision module 0x20095840 actually is

A general **recursive, table-driven matcher/dispatcher** ("find the node
matching this name/path"), duplicated as two variants (32-bit descriptor
variant rooted at 0x20095840 with helpers 0x20096f80/0x20097730; 64-bit
descriptor variant with helpers 0x20098830/0x200989f0 -- hence the two
literal-pool copies and the ~14 sentinel-return sites).

- ARGUMENTS: a0 = key record; a1 = token-stream cursor; a2 = mode (1 on the
  failing path); a3 = context.  Recursion at 0x200964fc passes a
  sub-stream cursor derived from a signed SELF-RELATIVE WORD link.
- KEY RECORD (built/defaulted at entry when key[0]==-1): +2 word; +4 long
  wildcard -1; +8 word = STRING LENGTH; +10 byte 0x0e; +11 byte 1;
  +12 long = STRING POINTER (defaulted -1); +20 long updated per step.
  The string is the parsed boot path.  [READER NOTE, corrected by P3/P4:
  the FAILING searches parse the REGISTERED-format string ("IDE 0 105
  ...", AARM space-delimited fields), NOT the presentation form
  "dqa0.0.0.105.0" assumed here -- the presentation form is what search
  #1 parses and accepts.]
- STRING SCANNERS 0x20096f80 (dsc: len word +0, ptr long +4) and
  0x20098830 (len quad +8, ptr quad +16) advance over spaces/tabs (0x20/
  0x09), decimal digits, or hex digits [0-9A-Fa-f] -- i.e. they parse the
  dotted numeric path components.
- TOKEN STREAM: u16 words; low 9 bits = code (space 0x1E0-0x1FF observed:
  0x1E6,0x1ED,0x1F1,0x1F3,0x1F4,0x1F5,0x1F6,0x1F7,0x1F8), bit 9 = has
  extension byte; entries with flag bit set carry an i32 SELF-RELATIVE
  POINTER to a VMS PROCEDURE DESCRIPTOR (PDSC kind bytes 0x3008/0x3089/
  0x300a visible) -- i.e. (token -> handler) registrations.  Code 0x1F6
  (502) is special-cased at both parse loops (0x20095ad4 / 0x2009783c).
- SENTINEL EXHAUSTION: search runs out of stream -> R0 = 0x158284 ->
  caller (report site 0x2000e844) prints %APB-F-NOIOVEC.

## 4. THE CENTRAL STATIC FINDING: the database is INSIDE APB

Resolved through the def0 shared service (entry 0x2000def0, procedure
descriptor 0x20063618 -- pool offsets are PDSC-relative, not entry-
relative):

  pool[-240] = 0x2009931e   ; token-stream anchor
  pool[-248] = 0x20098ff0   ; second stream/aux table
  pool[-304] = 0x20071270   ; -> [0x20071270] = VA 0x10000000 (console/
                            ;    HWRPB window)
  pool[-360] = 0x2006a8d4   ; static parameter block (the key record
                            ;    lives at +404/+412/+416 offsets = VA
                            ;    0x2006aa5x region)

  walk root (one path)  a1 = pool[-240] - 0x7c = 0x200992a2
  walk root (other path) a1 = 0x2009931e (short tail pattern)
  [READER NOTE, from P3: the LIVE run's observed walk root is
  0x200991d0 -- a further entry point reached via a caller outside the
  def0 paths computed above.  The stream region and mechanism are as
  described; the specific anchor arithmetic here is not the failing
  call's.]

The token stream occupies 0x20099280-0x20099329 (then zero padding to
image end) and its pointer entries land in a table of procedure
descriptors + stream pointers at 0x20063490-0x2006365x, which also
contains the PDSCs for 0x20095840 itself, 0x2000def0, and the handler
family 0x2000dc10-0x2000e450, plus the stream anchors 0x20098ff0/
0x2009931e.  ALL static, ALL inside the image, byte-identical to what a
real DS20 runs.

ROUTE-SELECT GATE (0x2000def0+0x68): v0=[0x20071270]=VA 0x10000000 (the
OS-mapped HWRPB window -- JRN-VMB-017 P2's "VA 0x10000050, the HWRPB
window"); ldl [0x10000000+0x50] = **HWRPB SYSTYPE**; if SYSTYPE in
{4,6,7} (big-IOP path grammar) take the 0x2000df90 branch with stream
anchor 0x200992a2, else (Tsunami 0x22) the 0x2000dfa0 branch with stream
anchor 0x20099216 = [pool-240]-0x108 and a2 = 0x20098ff0 (aux pattern
table).  Both reach 0x20095840.

## 5. What this does to the Sec 2 hypothesis
   [SUPERSEDED by P3: the run showed ZERO console/GCT accesses in the
    matcher window -- the hypothesis is now DEAD, and the "len 14 =
    dqa0.0.0.105.0" equation below was wrong (see P4).  Kept for the
    reasoning record.]

The GCT/FRU-content hypothesis is WEAKENED but not dead:

- The match database is NOT console memory: it is compiled into APB.
  So "the console's tree lacks a node" is wrong AS STATED.
- What IS runtime: the INPUT -- the parsed topology string from GETENV
  (len 14, "dqa0.0.0.105.0"), the context block (a3 = [24(gp)] chain),
  the HWRPB SYSTYPE gate, and whatever the registered token handlers
  dereference while matching (which MAY include console/GCT data --
  static analysis cannot exclude a handler walking console structures).
- Since a real DS20 with this same APB.EXE and this same path string
  boots, the divergence is in the runtime values EmulatR's console
  stack hands APB: topology string bytes, HWRPB fields consumed by the
  gate/handlers, or console-region data a handler reads.

## 6. CONFIRMATION RUN -- unchanged command, sharper READ
   [EXECUTED -- results in P3.  Outcome: read branch 2 (input-level).]

  EMULATR_DIAG_WREG= EMULATR_DIAG_PCLO=0x20095840 \
  EMULATR_DIAG_PCHI=0x20099000 EMULATR_DIAG_CAP=300000 \
  tools/run_ds20_bplus.sh ; exit LFU (no u srm) ; b dqa0
  (set all three DIAG vars explicitly -- stale-env hazard, JRN-018 Sec 3.)

READ, in order:
 1. memAddr rows in 0x101xxxxx or 0x3ff3xxxx (console/GCT region) ->
    a token handler consumes console data -> the content gap is in that
    structure; the specific addresses name it (GCT hypothesis revives,
    now with the exact field).
 2. memAddr rows ONLY in APB statics (0x2006xxxx, 0x20099xxx), stack,
    and the GETENV string buffer -> the INPUT is the mismatch.  Then:
    dump the 14 GETENV result bytes EmulatR returned and diff against a
    real/AXPBox DS20 BOOTED_DEV ("dqa0.0.0.105.0" -- check for case,
    trailing field count, and the exact numeric encoding of "105");
    also record [VA 0x2000+0x50] (SYSTYPE seen by the gate) and the key
    record at 0x2006aa54 region (len/ptr/fields) at module entry.
 3. Either way the compare fields at the final sentinel (key +12/+15/
    +20/+23) name what a match needed.

## 7. Key addresses (APB image, VA)

  entry (transfer)                    0x200000a8
  report site (WREG capture)          0x2000e844   caller thunk 0x2000e770
  shared service def0 entry           0x2000def0   PDSC 0x20063618
  def0 SYSTYPE gate                   0x2000df38-0x2000df80
  matcher entry (32-bit variant)      0x20095840   PDSC 0x20063510
  recursion site                      0x200964fc   sentinel epilog 0x20096514
  string scanners                     0x20096f80 (dsc32) / 0x20098830 (dsc64)
  accessor leaves (0x3fff field lim)  0x20097440-0x2009755x
  sentinel pools                      0x20065320/0x20065598 (=0x158284)
                                      0x200652b0/0x20065528 (=0x15828C)
  token stream                        0x20099280-0x20099329 (roots 0x200992a2,
                                                            0x2009931e)
  PDSC / stream-pointer table         0x20063490-0x2006365x
  static key/param block              0x2006a8d4 (+404 = key base)
  message table (NOIOVEC)             0x2006c080
  console window pointer cell         0x20071270 -> VA 0x10000000

## 8. HANDLER MAP (post-P2 continuation, same day) -- console-data surface
##    of the matcher family is THREE HWRPB CELLS + the string.  GCT ruled out
##    for the matcher itself.

APB_BOOTP.EXE;1 examined: raw headerless download image (starts with the
same boot0 code, diverges after 0x98 bytes); no EIHD, no DST/GST -- no
symbol names there either.

The pattern stream is a COMPILED GRAMMAR over the path string: 9-bit
token alphabet, 0x00-0xFF = literal characters (a literal "@wwid" sits
at 0x20099276 for FC paths; ','/'-' literals for cluster/MSCP syntax),
0x100-0x1FF = metacodes (0x1FF wildcard; 0x1F3/0x1F5/0x1F6 ops carrying
i32 self-relative refs to cells holding handler entries).  Three stream
anchors: 0x200992a2 ({4,6,7} systems), 0x20099216 (Tsunami-class),
0x2009931e (short tail).  Aux table 0x20098ff0.

Handler family (entries from the PDSC table 0x20063490-0x20063658), all
sharing the parse-state block 0x2006a8d4:

  0x2000dc10  action/commit handler (largest, 184 insns; calls into the
              0x200032xx main-flow services near the report block)
  0x2000def0  shared service (Sec 4) -- SYSTYPE route gate
  0x2000dfe0/0x2000e000/0x2000e2e0/0x2000e300/0x2000e450/0x2000e470
              trivial field accessors on the parse-state block
  0x2000e020  SYSTYPE test; {4,6,7}: copy 8 bytes from [HWRPB+0x40];
              Tsunami: STORE parsed number to [HWRPB+0x40] (scratch cell)
  0x2000e0b0  helper via 0x20074930
  0x2000e140  helpers 0x20007420/0x20074170/0x20075d50
  0x2000e1e0  console-window user (via 0x20071270)
  0x2000e260  store parsed component to state+104; on SYSTYPE==4 ONLY,
              remap slot number (+3) using SYSVAR bits 15:10 (member)
  0x2000e320  itoa (0x2000feb0) + re-entry into the matcher: formats a
              number to decimal text and re-matches (canonicalization)

CONSOLE-DATA SURFACE of the entire matcher family (exhaustive, from
pool resolution of every handler): HWRPB window cells +0x40, +0x50
(SYSTYPE), +0x58 (SYSVAR, consumed only when SYSTYPE==4) -- plus the
GETENV topology string.  NO handler walks the GCT/FRU region.  The
JRN-VMB-018 Sec 2 GCT-content hypothesis is now DEAD for the matcher
itself; if the DIAG trace shows the module window touching ONLY the
HWRPB cells above, APB statics, and stack, the mismatch is at the
string/number level (candidate 2), and the capture to examine is the
parse-state block 0x2006a8d4 (+104 slot cell, +404 key record) plus
key fields +12/+20 at the final sentinel return.

## 9. Provenance

Derived 2026-07-24 in the Cowork cloud sandbox from a staged copy of
D:\EmulatR\Processor Support\OpenVMS\APB.EXE;1.  Identity vs the live
boot image verified against the JRN-VMB-016/017 recorded words before
any conclusion was drawn.  gct.h / gct_nodeid.sdl (apisrm ref) consulted
to RULE OUT GCT node/TLV formats for the walked records (GCT TLV tags
are 1..4; the stream codes are 0x1E0-0x1FF -- different animal).

================================================================================
## P3 -- CONFIRMATION RUN READOUT (run_ds20_showdev_20260724_165020)
================================================================================
    Date   : 2026-07-24 (run), same-day analysis.  Window fired: 6931 DIAG-PC
             rows + 287 DIAG-WR rows (WREG= parsed as R0 -- return values).
             Extracts: traces/diagpc_window_20260724_165020.txt,
             traces/diagwr_20260724_165020.txt.

VERDICT 1 -- console-content hypothesis DEAD, input-level mismatch CONFIRMED.
Every memAddr in the window: APB statics 923, pattern stream 65, stack 823,
console region 0, GCT region 0, other 0.  The matcher consumed NOTHING from
console memory during the failing search.

VERDICT 2 -- the search structure (from R0 provenance):
  - THREE searches ran.  Search #1 (grammar section 0x200991d0+, input =
    descriptor A string at stack 0x200dfe10) ACCEPTED -- returned 1 twice
    (0x20096da0).  Searches #2/#3 (grammar 0x20099210-0x20099238, input =
    the static-copy string at 0x2006aab8, later re-pointed to stack
    0x200dfd40) both REJECTED with 0x158284.
  - The failing parse progressed correctly: 3-letter scan ("dqa"), 1-digit
    scans + '.' literals ("0.0."), then a 3-digit scan ("105") -- TWICE
    (two terminal alternatives, entries at 0x20099230/0x20099238) -- then
    each alternative's continuation produced inner status 0 and the
    terminal block (0x20096e44) manufactured 0x158284 via cmoveq.
  - The walk never fetched stream bytes past the 0x20099238 entry; the
    remaining alternatives (0x2009923e+) were never tried.
  - Handlers ran OUT-of-window in the cycle gaps (229/214/1233/6320 cyc);
    e260/e1e0 are unconditional-success stores on Tsunami (SYSTYPE 0x22),
    so the reject is structural, not a handler veto.
  - Anchor observed: 0x200991d0 (a further entry point; neither statically
    computed anchor).  Sentinel materialization site: 0x20096e58.

WHAT IS STILL MISSING: the actual BYTES of the two input strings (values
are not in the PC/R0 traces).  The scanners land every examined character
in r22 (t8) via extbl (0x20096fe4/0x20097034/0x20097084/...).

NEXT RUN (chars-visible; one variable changed):
  EMULATR_DIAG_WREG=22 EMULATR_DIAG_WMIN=0 \
  EMULATR_DIAG_PCLO=0x20095840 EMULATR_DIAG_PCHI=0x20099000 \
  EMULATR_DIAG_CAP=300000 tools/run_ds20_bplus.sh ; exit LFU ; b dqa0
READ: DIAG-WR rows spell the scanned strings char-by-char (0x30-0x39,
0x61-0x7a...).  Compare string B (the 0x2006aab8 copy) byte-for-byte
against "dqa0.0.0.105.0" and note the exact character where scanning
stopped; that character (or its absence) names the divergence.  If string
B is byte-correct, the divergence is the grammar's expectation AFTER
"105" -- disassemble the 0x1F3-op continuation semantics next.

================================================================================
## P4 -- THE STRING IS SHORT: BOOTED_DEV registered-format value is truncated
##       (R22 char trace, run_ds20_showdev_20260724_171729)
================================================================================
    Date   : 2026-07-25 (run 20260724_171729; 908 DIAG-WR R22 rows).
    Extract: traces/diagwr_r22_20260724_171729.txt.

THE INPUT STRING IS NOT "dqa0.0.0.105.0".  The failing searches parse the
REGISTERED-FORMAT boot device string (AARM Sec 26.x: fields delimited by
single spaces, list elements by commas, numerics decimal -- example
"0 4 MSCP,0 1 MOP").  Search #1 (accepted) parsed the presentation-form
name; searches #2/#3 parse the registered string at 0x2006aab8 (stack twin
0x200dfd40), whose scanned characters are:

    'I' 'D' 'E' ' ' '0' ' ' '1' '0' '5' ' '     (offsets +0..+9)

and the scanners NEVER advanced past offset +9.

THE AUTHORITY SAYS THE STRING SHOULD BE 19 CHARS.  apisrm ref/filesys.c
file2dev() (line 2812) composes booted_dev as:
    sprintf(dname, "%s %d %d %d %d %d%s", fd->device,
            hose, n3, channel, node, unit, fd->suffix)
with fd_table (line 2746): {"dq","dq",0,"IDE"," 0 0"}.  For
dqa0.0.0.105.0 the canonical value is:

    "IDE 0 105 0 0 0 0 0"        (19 characters)

EmulatR's GETENV returned len 14 (JRN-VMB-018 finding 3) and the grammar
died right after "105 " -- consistent with a value ~"IDE 0 105 0 0"
missing the unit field and/or the " 0 0" suffix.  boot.c:1252 ev_write's
dname at boot time; the value APB received is SHORT.  APB's grammar
(matching the canonical format) exhausts after the missing fields ->
0x158284 -> NOIOVEC.  This is the first concrete divergence found in the
entire chain, and it lives in EmulatR-influenced data, not in APB.

OPEN QUESTION (one probe closes it): WHERE is it short --
  (a) the console COMPOSED a short dname (file2dev inputs -- the inode
      name/topology data -- differ on EmulatR), or
  (b) the console stored the full 19 chars and the GETENV/callback copy
      path CLIPPED it to 14.

NEXT PROBES (both in one run):
  1. PA-WATCH the APB-side string buffer as it is built:
       VA 0x2006aab8 -> PA 0x5bc000+0x6aab8 = 0x626ab8
       EMULATR_PA_WATCH=0x626ab8 EMULATR_PA_WATCH_LEN=0x20 \
       tools/run_ds20_bplus.sh ; exit LFU ; b dqa0
     (verify facility in exe first:
       grep -a -c EMULATR_PA_WATCH out/build/relwithdebinfo/Emulatr.exe)
     READ: PA-WATCH rows carry the written VALUES (the full string bytes)
     and the WRITER PC -- console code (0x101xxxxx VAs) vs APB code.
  2. After the NOIOVEC failure, at P00>>> type: show booted_dev
     (and show boot_dev) -- the console prints its stored value; a short
     or odd display localizes the truncation to composition (a).

FIX SHAPE (pending the (a)/(b) split): supply/repair whatever EmulatR-side
input makes the console compose or deliver the full 19-char registered
string.  No APB patching, no grammar workarounds -- P-1 faithful.

================================================================================
## P5 -- IMAGE-INTEGRITY VERIFICATION (Pchip/IDE read path held harmless)
================================================================================
    Date: 2026-07-25.  Question: could the loaded APB image be corrupt
    (Pchip/polled-IDE delivery)?

NO.  All 6929 non-fault DIAG-PC rows from run 20260724_165020 were
checked against APB.EXE;1: every executed instruction encoding matches
the file byte-for-byte (752 distinct code addresses, 0x20095840-
0x200971b0, zero mismatches; the 2 skipped rows are ITB-miss entries
that carry no encoding).  Grammar DATA integrity corroborated
independently: the tag values the matcher fetched live (0x11f7, 0x102c,
0x89f8, 0x15f5, 0x5f6, 0xdf8, 0x85f6, 0x85f1, 0x85f3 in the R0 trace)
equal the file's words at those stream addresses.  Together with the
boot0/VMB landmarks and the byte-perfect NOIOVEC message text, the
Pchip and the polled-IDE path are formally exonerated for the image.
Only runtime-WRITTEN statics (parse state, string copies) are outside
this proof -- consistent with P4 locating the divergence exactly there.

## P6 -- AXPBox 1.1.2 alignment/divergence at this stage (source-checked)

  - AXPBox implements the VMS PAL in C++ (AlphaCPU_vmspal.cpp);
    vmspal_call_cserve() hardcodes per-image addresses for ITS ES40
    firmware (0x42 -> set_pc(0x13781), 0x43 -> set_pc(0x13261), 0x44 ->
    set_pc(r17)) and writes `p23 = state.pc` ONLY -- the PAL's view, not
    native R23.  Both corroborate EmulatR's JRN-VMB-017 P3 fixes (#4
    routing shape, #5 p23-only) and the MTPR_EXC_ADDR spec.
  - AXPBox's translation is all-C++ (never runs guest miss handlers);
    EmulatR runs the real PAL walks.  Structural difference, but P3
    showed only 2 in-window faults -- not a factor in this wall.
  - AXPBox is ES40-family: its APB parses ES40-topology registered
    strings.  "AXPBox boots VMS" therefore does NOT certify the DS20
    "105"-encoded parse path; the only validator for that path is the
    DS20 firmware itself running faithfully -- which is how EmulatR
    exposed the truncated-string divergence.

================================================================================
## P7 -- SHIPPED-FIRMWARE CONFIRMATION: the v7.3-2 console composes 19 chars;
##       the 14-byte value is a CLIP, not a composition
================================================================================
    Date: 2026-07-25.  Method: tools/host_decompressor/oracle_lin run in the
    sandbox against firmware/ds20_v7_3.exe (signature PASSED); string/byte
    scans of ref_decompressed.bin (base PA 0x8000).

  - The SHIPPED DS20 V7.3-2 console contains the SAME composer as the
    apisrm source: format "%s %d %d %d %d %d%s" at image off 0x18e8b0
    (+ the "@wwid%d" FC variant at 0x18e890), and the fd_table with
    {"dq","dq","IDE"," 0 0"} at 0x18e6b8.  Canonical booted_dev for
    dqa0.0.0.105.0 is therefore "IDE 0 105 0 0 0 0 0" (19 chars) on
    THIS firmware, not just in the source tree.
  - A six-argument sprintf through that format CANNOT emit 14 bytes from
    these inputs.  CONCLUSION: the console composed the full 19-char
    string at boot (boot.c ev_write), and the value APB received was
    CLIPPED to 13 chars + NUL somewhere between the console env storage
    and APB's key descriptor -- i.e. in ev_write storage, ev_read/GETENV
    retrieval (console callback code executing in OS context), or the
    APB-side copy into 0x2006aab8.  All are guest code running on
    EmulatR; the clip is an EmulatR execution/data artifact in that
    path.
  - The callback ENV_VAR_TABLE (VA 0x101ab130 = console PA 0x1ad130,
    file off 0x1a5130) is 16-byte entries {value ptr (runtime-filled),
    id}: ids 01..0F then 4F,44,45,46,48,49,4A,50,51.  BOOTED_DEV = id 4,
    entry 0x101ab160.  Pointers are zero in the image -- the value
    buffer is console heap; static analysis ends here.
  - PA-WATCH side note: the 20260724_173339 run (PA_WATCH=0x626ab8) died
    with a silent host crash at console idle (faults.log/unaligned.log
    cut mid-line, no abort text, boot never started); the 4 rows caught
    were decompressor scratch (cyc 158k).  PA-WATCH involvement
    UNCONFIRMED -- A/B on the next plain run.

NEXT (in order):
  1. PLAIN run (no DIAG/PA_WATCH): b dqa0 -> NOIOVEC -> at the returned
     prompt `show booted_dev`.  Full 19 chars shown => storage fine,
     clip is in the GETENV/copy path; short => storage/ev_write side;
     also A/B-tests the crash.
  2. Bounded callback-window trace (the "full trace" done per trace
     discipline): EMULATR_DIAG_PCLO=0x10000000 PCHI=0x10200000 with
     EMULATR_DIAG_CYCLO/CYCHI spanning the APB window and CAP=300000 --
     captures every console-callback instruction APB invokes (GETENV
     walk + value copy) with memAddr; names the env-value buffer PA and
     the copy loop.  A follow-up PA-WATCH on THAT buffer (or WREG on the
     copy register) reads the stored bytes and the clip point.

================================================================================
## P8 -- PA-WATCH CAPTURE: THE STRING IS FULL.  Truncation theory RETRACTED.
##       New prime suspect: the grammar ANCHOR / SYSTYPE-gate route.
================================================================================
    Date: 2026-07-25.  Run: run_ds20_showdev_20260724_183157 (bplus stack,
    PA_WATCH=0x626ab8 LEN=0x20; 55 rows; reached NOIOVEC, HaltedClean).

CAPTURED, byte by byte (writer pc=0x101ab4cc = console GETENV/copy code in
OS context, ra=0x2000e8f4 = APB caller; shift-insert quad builds at
+0/+8/+0x10, cyc 1937155470-1937155668):

    "IDE 0 105 0 0 0 0 0"     (19 bytes, canonical, COMPLETE)

landing intact in APB's buffer VA 0x2006aab8 (PA 0x626ab8).  Also seen:
console-era bzero of the region (pc 0x65b44) and decompressor scratch --
both benign.

CONSEQUENCES:
  - P4/P7's truncation conclusion is WRONG and hereby RETRACTED.  The
    JRN-VMB-018 finding-3 "len 14" was a misattribution (14 = the
    PRESENTATION string "dqa0.0.0.105.0" length, likely a different
    GETENV or misread field).  The console composes, stores, AND
    delivers the full registered string.  P-1 faithfulness of the
    compose/store/deliver chain: VERIFIED END TO END.
  - Therefore APB's matcher rejects a CORRECT canonical input.  The
    divergence must be in another runtime input to the match decision.

NEW PRIME SUSPECT (was flagged in P3 and left open): the WALK ANCHOR.
The failing search entered the token stream at 0x200991d0 -- NEITHER the
{4,6,7} anchor (0x200992a2) NOR the Tsunami anchor (0x20099216) computed
from def0.  If the def0 SYSTYPE gate ([VA 0x10000000 + 0x50]) reads a
wrong value on EmulatR (bad window mapping/content), the matcher parses
the Tsunami-format string against the WRONG platform family's grammar
section -- which would reject exactly as observed, after "105".
Alternative: the 0x200991d0 anchor belongs to the third caller
(0x2000e420, inside handler e320) legitimately -- must be decided.

NEXT PROBES:
  1. Gate capture: EMULATR_DIAG_PCLO=0x2000dee0 EMULATR_DIAG_PCHI=0x2000e000
     EMULATR_DIAG_WREG=0 EMULATR_DIAG_CAP=2000 (+ bplus stack) -- the
     def0 gate's `ldl v0,80(v0)` lands the SYSTYPE it saw in R0; the
     DIAG-WR rows show the value and the branch taken.
  2. Static: disassemble 0x2000e300-0x2000e450 (caller e320 tail + the
     0x2000e420 bsr's arg prep) and resolve ITS anchor arithmetic --
     if it computes 0x200991d0, the anchor is legitimate and the
     analysis moves to the grammar section semantics at 0x200991d0.
  3. Cross-check EmulatR's HWRPB window: what VA 0x10000000+0x50
     translates to and holds at APB time (expect PA 0x2050, value 0x22).

OPERATIONAL NOTES (same evening): the "vaporized process" episodes
correlated with a SECOND emulator instance running concurrently (port
10023 / vdisk / NVRAM contention) -- resolved by shutting it down; nView
(RTX Desktop Manager) was also disabled earlier.  PA-WATCH is exonerated
and verified working.  run_ds20_bplus.sh is the wrapper that sets the
faithful stack; bare run_ds20_showdev.sh hits the old 0x20000000 wall
(expected -- not a regression).

================================================================================
## P9 -- ARMED FULL TRACE CAPTURED (the decisive artifact).  Gate and anchor
##       VERIFIED CORRECT; the reject decoded to the instruction.
================================================================================
    Date: 2026-07-25.  Run: run_ds20_showdev_20260724_185949 (bplus stack +
    EMULATR_TRACE_WINDOW=1, ARM_PA=0x626ab8 ARM_VAL=0x49, 4M-instr window).
    Artifact: traces/20260724-185950_srm.trc -- 261,619 retire-compact rows
    covering the ENTIRE window from the GETENV string delivery to the final
    HALT at the report site (0x20003a38), with dest-reg values, ld/st
    va/pa/value, and shadow regs per row.  ARM fired exactly at the copy
    ("GMEM-TRACE-ARM pa=0x626ab8 v=0x49").  Run ended HaltedClean.

VERIFIED CORRECT (P8's two suspects both cleared):
  - SYSTYPE gate: pc=0x2000df38 LDL va=0x10000050 pa=0x2050 -> R0=0x22
    (DEC_TSUNAMI).  CMPEQ 4/7/6 all 0 -> BEQ -> Tsunami branch 0x2000dfa0.
  - Anchor: R17 <- 0x20099216 at 0x2000dfd4 (canonical [pool-240]-0x108);
    first tag fetch ldq_u va=0x20099210 (same quad -- the earlier run's
    "0x200991d0 anchor" was DIAG memAddr quad-granularity aliasing, not a
    different entry point).  P8's anchor suspicion RESOLVED-BENIGN.

THE REJECT, instruction-level (first sentinel, trace line ~3178):
  - The active grammar op scans the VMS IDENTIFIER class -- digits, letters,
    plus 0x24 '$' and 0x5f '_' (cmpeq pair at 0x2009718c/0x20097190) -- and
    consumed "105" (3 chars) at string offset 6..8.
  - Next char = 0x20 (space, offset 9) is NOT in the class -> scan ends,
    count=3 stored to key+16 (0x20096dfc/0x20096e00).
  - Terminal block: BNE v0(3)!=0 -> 0x20096e44; inner-status cell
    [sp frame, va 0x200dfce8] = 0 (no acceptance was ever recorded);
    R17 <- 0x158284 from pool 0x20065320; CMOVEQ -> R0 = 0x158284.
  - FOUR matcher invocations total this run; two distinct caller contexts
    (excAddr 0x20007420 and 0x20007344 -- APB code around 0x73xx/0x74xx,
    NOT the e770/e844 chain alone).  Two sentinels per caller context.

STATE OF THE CHAIN (everything now proven correct on EmulatR):
  string 19/19 chars -> GETENV delivery -> SYSTYPE gate -> anchor ->
  matcher execution (byte-perfect code).  The ONLY remaining unknown is the
  GRAMMAR-SEMANTIC question: which stream entry should accept the
  space-then-next-field continuation after the identifier/number ops, and
  why no alternative records acceptance before the list stops being tried.
  This is now a PURE READING EXERCISE on the .trc (stream-fetch sequence +
  handler dispatches with values) -- no further runs required.

NEXT SESSION RUNBOOK:
  1. Read this journal P2-P9, then the .trc (sandbox copy was at
     /home/claude/apbwin.trc; canonical: traces/20260724-185950_srm.trc).
  2. Reconstruct the walked entry sequence: all pc=0x20095a70/0x200964a4/
     0x200967cc stream fetches (va+value) in order, annotate each entry's
     tag/op/handler from the P2 stream decode, and mark where iteration
     stops vs the remaining entries (0x2009923e+).
  3. Identify the op that SHOULD consume ' ' + next field (the canonical
     string has 5 more fields) -- compare its guard registers/state cells
     in the trace against what would make it fire.
  4. The divergence found there names the EmulatR-side fix; discuss-first.
  Note: callers 0x20007344/0x20007420 (and the 0x2000e978 ra seen at def0
  entry) are un-analyzed APB code -- disassemble around them if step 3
  needs caller intent.
