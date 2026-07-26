<!--
EmulatR V5 -- Session Journal JRN-VMB-021
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-021 -- NOIOVEC part 5 (A3): the walk TRANSCRIBED end-to-end from
#                the trace.  The grammar CAN express the boot string
#                (ident + 7 numbers); the walk matches "IDE","0","105" then
#                exits with the number-entry HANDLERS never called.  The
#                gate that skipped them is the remaining unknown.

    Doc id   : JRN-VMB-021
    Date     : 2026-07-24
    Status   : ANALYSIS RECORD.  No code changed.  Offline analysis only
               (no new runs) -- everything below is read off the trace of
               record via the A2 replay model (register state reconstructed
               at every retire) + static APB.EXE bytes.
    Relates  : JRN-VMB-020 (A1/A2 clean), JRN-VMB-019/-018(+P2).
    Artifacts: scratchpad a3_annotate.py -> a3_walk_listing.txt (8,123-line
               annotated execution listing of trace lines 1-8130, every
               operand resolved; regenerate any time from the trace).

--------------------------------------------------------------------------------
## 0. Executive summary

 1. The failing module is a BYTECODE VM: the token stream at 0x99210-0x9932x
    is a static grammar PROGRAM (u16 ops), the boot string is its DATA.
    The 0xe7..0xf8 XOR ladder (JRN-019) is the op dispatch; op values come
    from the STREAM (token & 0x1ff - 0x100), not from the boot device.
    Dispatch = jump table [0x2006a0e0] (the A1 window -- verified intact),
    LDL [table + 4*(op-0xe4) + 0x48] + module base -> handler.

 2. THE GRAMMAR CAN EXPRESS OUR STRING.  Static decode of the subtree at
    0x2009922c (reached via op 0xf8 @0x9921c, link word [0x9921e]=0xc):

        0x9922c  85f6  ptr fffca496   anchor/begin  (code 0x1f6, special)
        0x99232  85f1  ptr fffca350   IDENT   -- matched "IDE" (captured)
        0x99238  85f3  ptr fffca392   NUMBER  -- field 1 ("0")
        0x9923e  85f3  ptr fffca324   NUMBER  -- field 2 ("105") <- DIED HERE
        0x99244  85f3  ptr fffca3ae   NUMBER  -- field 3   (never reached)
        0x9924a  85f3  ptr fffca328   NUMBER  -- field 4   (never reached)
        0x99250  85f3  ptr fffca302   NUMBER  -- field 5   (never reached)
        0x99256  85f5  ptr fffca3ac   (suffix field 6)     (never reached)
        0x9925c  85f5  ptr fffca37e   (suffix field 7)     (never reached)
        0x99262  85f6  ptr fffca2c8   ...
        0x99268  19f8  operand 000a   op 0xf8 again (next chain)

    ident + 5 numbers + 2 suffix entries = protocol + 7 numeric fields =
    EXACTLY the registered format "IDE 0 105 0 0 0 0 0" (and the apisrm
    sample "SCSI 0 1 0 2 200 0 0").  The database is not missing the
    production; the walk ABANDONS it after field 2.

 3. Transcript of the failing (second) invocation -- entry BSR @0x2000e974,
    key record a0=0x2006aa64 {.. len=0x13, ptr=0x200dfd40 ("IDE 0 105 0 0
    0 0 0")}, stream cursor a1=0x20099216, MODE a2=1 (R7=1 throughout),
    ctx a3=0x20063820:
      - anchor 0x85f6: dispatches op 0xf6 (handler 0x20095e70 via
        [0x2006a170]=0x630) -> 0x9661c: initializes scan pos=0 vs len=0x13.
      - ident 0x85f1: its PDSC HANDLER IS CALLED (thunk chain 0x2000e1a4 ->
        0x20075d00-region proc): tokenizes "IDE" (3 chars, ends at the
        space, the benign char-class test at 0x97184), COPIES it to
        0x2006a930 and space-pads to 8 ("IDE     " -- the cell seen in the
        A1 snapshot diff), returns 1 = matched.  Input advanced past
        "IDE 0 " (pos 6, remaining 0x13->0x10->0xd).
      - number 0x85f3 @0x9238: takes the flag-bit path (token bit14=0 ->
        0x96ac0): clears a bit in key[+6], and RE-ZEROES the result slot
        [fp+0x28] (STL @0x20096ae0 -- JRN-019 E5 called this a "prologue
        zeroing"; it is actually MID-WALK, inside this entry).  No scan.
      - number 0x85f3 @0x923e: inline scanner 0x20096f80 (mode probe:
        first whitespace-mode 0, then mode 4) scans "105" -- '1','0','5'
        accepted as digits, terminator space ends the token cleanly, all
        four char-class checks (digit/upper/lower/ident) behave correctly
        -- scan returns len 3.  THEN: BNE R0=3 -> 0x20096e44 EXIT PATH:
        loads result slot [fp+0x28] = 0 (never set), 0x20096e58 CMOVEQ
        mints 0x158284.  THE ENTRY'S PDSC HANDLER (fffca324) IS NEVER
        CALLED; the accept stores (0x20097e30/40/60/90) never execute;
        entries @0x99244.. never visited.

 4. THE REMAINING UNKNOWN (the actual root cause bottleneck): the gate in
    the 0xf3-class entry processing (0x20096d14-0x20096e44) that chose
    "inline probe then EXIT" instead of "call the PDSC handler and advance
    to the next entry".  Candidates observed in the transcript: the token
    flag bits (tests on bits 10/11/12/14 of the token word; the nibble
    size table 0x20099150 = per-class entry sizes) crossed with the MODE
    argument a2 (R7=1 on the failing path; JRN-018 P2 said mode=1) and the
    sub-request (r25=4 at the recursion).  The walk behaves like a
    VALIDATE/PROBE pass that was expected to be a MATCH/EXECUTE pass --
    or expects a different mode for full execution.

 5. What this KILLS: any remaining "database lacks the IDE production"
    theory (the production is right there, 5+2 fields) and any "input
    string malformed" theory (three tokens matched perfectly).  What it
    NARROWS to: the mode/flag gate decision at field 2 -- either APB is
    correct and something EARLIER should have set up a different
    mode/context for this call (console-side data APB consumed before the
    window), or the walk legitimately probes here and the real match was
    supposed to happen in a DIFFERENT invocation that never occurred.

--------------------------------------------------------------------------------
## 1. Corrections / refinements to prior records

  - JRN-018 P2 "token type 0x13 = ident" -- the 0x13 at 0x2006aa6c is the
    STRING LENGTH 19 (of "IDE 0 105 0 0 0 0 0"); it decrements as fields
    are consumed (0x13 -> 0x10 after "IDE" -> 0xd after " 0 ").  The A2
    checker's early "MEM INCONSISTENT 0x13 vs 0x10" false-positive was
    this length update via byte store.
  - JRN-019 E5 "result slot written ONLY by the two prologue zeroings" --
    the 0x20096ae0 store is INSIDE the first 0x85f3 entry's flag path,
    i.e. a mid-walk reset, not a prologue.
  - JRN-019 Sec 2 A2/A3 framing: A2 is closed (VMB-020); A3's "descriptor
    provenance" is NARROWED -- the walk reads NO parsed-descriptor fields
    (zero loads from 0x2006a3xx/0x2006a4xx in the failing window); its
    only inputs are the key record {len,ptr}, the string bytes, and the
    static stream.  Provenance now means: what should have set the MODE/
    context of this call, not the descriptor contents.

--------------------------------------------------------------------------------
## 2. Key addresses (adds to JRN-018/-019 maps)

  op dispatch            0x20095e30-0x20095e60 (AND 0xff; -0xe4; S4ADDQ
                         [0x2006a0e0]+0x48; JMP)
  op 0xf6 handler        0x20095e70 -> 0x2009661c (anchor: pos=0)
  op 0xf3 entry tail     0x20096d14-0x20096e44 (nibble size; mode gate;
                         probe scanner calls; THE BNE EXIT @0x20096e04->
                         0x20096e44)
  slot mid-walk reset    0x20096ae0 (inside 0x96ac0 flag path)
  scanner                0x20096f80 (modes: 0=whitespace probe, 4=digit/
                         hex scan; per-class ladders 0x97010/0x97060/
                         0x970d0/0x97160)
  ident capture proc     0x20075d00-region (via thunk 0x2000e1a4); pads
                         at 0x20074170 (space memset); dest 0x2006a930
  nibble size table      0x20099150: 6554433243322110 8776655465544332
                         (per-op-class stream entry sizes)
  subtree program        0x2009922c-0x20099261 (decoded in Sec 0.2)
  number-entry PDSCs     self-rel fffca392/fffca324/fffca3ae/fffca328/
                         fffca302 -> 0x200635xx region (NEVER CALLED)

--------------------------------------------------------------------------------
## 3. Next steps (A3 part 2)

  1. STATIC: disassemble 0x20096d14-0x20096e44 (the 0xf3 entry tail) and
     name the exact branch chain from "scan returned len=3" to the exit --
     which token bit / mode value / key-record field selects the PDSC-call
     continuation instead.  Then disassemble ONE number PDSC (fffca324 ->
     0x200635xx -> code) to confirm what a successful field match would
     have stored (presumably into the result slot / IOVEC under
     construction).
  2. STATIC: map the accept region 0x20097200-0x20097fff (success stores
     0x20097e30/40/60/90) backwards to the op/entry class that reaches it
     (which op code's handler lives there via the dispatch table at
     0x2006a0e0 -- read the table's other 0x48-slot entries).
  3. DYNAMIC (if needed): invocation #1 (listing lines 307-4800, caller
     ctx 0x2000e5d0) parsed the PRESENTATION form per JRN-018 P2's reader
     note -- transcribe it the same way and compare ITS gate decisions
     with invocation #2's; the first divergence names the gate.
  4. A4 remains the cheap decisive split if the static work stalls: same
     media on AXPBox, window 0x95840, diff the gate branch.

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
