<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-013
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-013 -- T1 EXECUTED: THE NOIOVEC BRANCH IS NAMED.
#                 BLBS r0 @ 0x20003a10; r0 = the walk chain's return; the
#                 accept invocation DOES run (r18=1) and still fails.

    Doc id   : JRN-SCSI-013
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-012 Sec 5.5 (T-series plan; T1 = this), JRN-SCSI-011
               (conversation = 4 answers), JRN-VMB-021 (walk grammar),
               JRN-SCSI-010 (Phase-1 stack).

--------------------------------------------------------------------------------
## 1. Instrumentation (what ran)

  Two cold boots through tools/run_taskboot001_t1.sh (NEW; wraps the
  JRN-SCSI-010 Phase-1 stack, opens the DIAG-PC retire window, splits
  DIAG-PC lines into traces/ post-run).  Console: operator PuTTY,
  `b dka0.0.0.8.0 -flags 0`, NOIOVEC reached both times.

  Run A (15:07, window 0x20000000-0x20099400, cap 5e6): cap consumed in
  5.0e6 cycles -- 99.5% by ONE 32-instruction loop at 0x200098d0-0x20009960
  (152,790+ iterations when the cap hit).  Decoded from the trace: it is
  APB's page-frame-BITMAP build over the HWRPB memory descriptors (bit per
  8KB page, 4GiB guest = ~524k iterations; SRA/AND/S8ADDQ bit-address
  arithmetic + LDQ_U/BIS/STQ_U read-modify-write, outer +0x38 descriptor
  advance at 0x20009954).  Startup churn; unrelated to device resolution.
  Trace kept: traces/t1_apb_retire_20260726_151207.txt (5e6 rec, APB entry
  onward).

  Run B (15:20, window 0x2000a000-0x20099400 -- hog page excluded -- cap
  20e6): 272,661 records spanning cyc 1925486648..1942138209, NOT capped;
  covers everything from early APB to past the NOIOVEC emission.  Trace:
  traces/t1_apb_retire_20260726_152442.txt.  Analyzer: NEW
  tools/t1_apb_trace_analyze.py (streams the DIAG-PC line format; summary/
  calls/targets/gaps/branches/around/profile modes; alpha_disasm-annotated).

--------------------------------------------------------------------------------
## 2. T1 headline -- the branch that chooses NOIOVEC over accept

  Dynamic trace (run B) + static snapshot disasm (snap_va_disasm.py over
  auto_halt_1785040752_1903336426.axpsnap) agree end-to-end:

    0x20003a00: BSR  r26, 0x2000e700     ; invoke walk-wrapper chain
    0x20003a04: LDQ  r1,-0x38(r2)
    0x20003a0c: BIS  r31,#0x2,r25
    0x20003a10: BLBS r0, .+0x28          ; <<< THE BRANCH (T1 deliverable)
    ; -- not taken (r0 LBS clear = walk FAILED) --
    0x20003a14: LDA  r27,0x17a0(r2)      ; message descriptor
    0x20003a18: LDQ  r17,0x0(r1)         ; message byte loop head
    0x20003a24: BSR  r26, 0x2000f940     ; -> formatter -> 40x 0x2004b820
                                         ;    CRB puts = %APB-F-NOIOVEC
    ; -- taken (r0 odd = walk ACCEPTED) --> 0x20003a3c: --
    0x20003a3c: LDQ  r17,-0x28(r3)
    0x20003a40: BIS  r31,#0x22,r16       ; CRB callback selector 0x22
    0x20003a5c: LDL  r0,0xc0(r17)        ;   via HWRPB+0xc0 dispatch --
    0x20003a74: JMP/JSR r26,(r26)        ;   the open/IOVEC console call
                                         ;   APB never makes (JRN-SCSI-011)

  Timing fit is exact: run B shows the wrapper epilogue ret at 0x2000e850
  (cyc 1941883658), a 50-cycle out-of-window excursion (= the 9
  instructions above + DTB-miss PAL time; excAddr latches 0x20003a18),
  then entry at 0x2000f940 (cyc 1941883708) and the 40x CRB-callback
  message loop (gaps at 0x2004b8c8/0x2004ba68, matching JRN-SCSI-011's
  40 getc/puts).

  r0's provenance: the wrapper epilogue (0x2000e838-0x2000e850) ZAPNOTs
  the walk's return (0x2000e844: ZAPNOT r0,#0xf,r0) -- r0 comes straight
  out of the 0x2000def0 walk / resolver chain.

--------------------------------------------------------------------------------
## 3. CORRECTION to JRN-SCSI-012 Sec 5.2: the accept pass RUNS -- and fails

  The walk entry 0x2000def0 is invoked TWICE per boot attempt:

    #1  BSR  @ 0x2000e974 (cyc 1941875670), caller sets r18=0
        (0x2000e964: BIS r31,r31,r18); returns; then the ctx+0x8 handler
        0x2000e9d0 (via JSR @ 0x2000e990) CLASSIFIES the ident: compares
        the key record's +0x58 field against literal ASCII "DVA "/"RAID"/
        "SCSI"/"MSCP"/"FLOP" -- our "SCSI" MATCHES, it stores type code
        0x11 and returns r0=1.  Ident classification is NOT the failure.

    #2  JSR  @ 0x2000e834 (wrapper entered at 0x2000e770 from the
        BSR @ 0x20003a00 chain), caller sets r18=1
        (0x2000e828: BIS r31,#0x1,r18); the walk STORES the flag into its
        context (0x2000df34: STL r18,0x18c(r20)).  r18 is the probe(0) /
        accept(1) pass selector.  THE ACCEPT INVOCATION OCCURS -- and its
        r0 comes back with LBS clear, so 0x20003a10 falls through to the
        message.

  So Sec 5.2's "the accept pass is a second invocation that never occurs"
  is WITHDRAWN: both passes run; the SECOND fails.  The L1 question is now
  strictly: inside the r18=1 walk, which compare zeroes r0?

  Also refined: resolver entry 0x20095840 fires 7x per attempt -- 3x
  BEFORE walk #1 (entered from a 0x2000ab64 ret site; likely the env-var
  string walks), 2x within each walk invocation.  The tokenizer helper
  0x20096f80 fires 31x.  All resolver activity sits in ONE 16k-cycle
  window; there is no hidden earlier accept.

--------------------------------------------------------------------------------
## 4. Where the next probe aims (T2/T4 refinement)

  Inside the walk return path the trace shows the verdict being COMPARED,
  not just produced: after the resolver's final ret to 0x20096500:

    0x20096500: BIS r31,r0,r1            ; save resolver r0
    0x20096514: LDQ r12,0x28(r2)         ; expected value  <- VA 0x20065320
    0x20096518: XOR r1,r12,r12
    0x20096520: BEQ r12, .+0x4           ; equal-check (TAKEN in trace)

  0x28(ctx r2) -> static cell 0x20065320 is a named comparison operand of
  the verdict path.  T2 (decode the operand records at 0x200635xx,
  record-relative per -012 Sec 5.3) should now START at 0x20065320 and the
  key record whose +0x58 ident was read from 0x2006a308 by the classify
  handler (working buffer at 0x2006aa68/0x2006aa70 holds the topology
  string during tokenization; per-char class tables read at 0x200dfd40).

  T3 note: dispatch cells observed live -- ctx 0x20063820+0x8 ->
  0x2000e9d0 (classify), 0x20063718 -> chain -> 0x200712e8 records,
  0x20063840 -> 0x20071270 (dispatch base), 0x200638e8 -> 0x2000e770
  (wrapper #2).  The 0x20063820 table enumeration (T3) can be checked
  against these.

  The one dynamic gap left: PCs below 0x2000a000 (the decision block Sec 2
  and its callers) were traced only statically.  A complementary run with
  window 0x20000000-0x200097ff (excludes only the Sec 1 bitmap hog at
  0x20009800+) maps that low-region call graph if needed.

--------------------------------------------------------------------------------
## 5. Files touched

  - tools/run_taskboot001_t1.sh          NEW  T1 runner (window/cap/split)
  - tools/t1_apb_trace_analyze.py        NEW  DIAG-PC trace analyzer
  - traces/t1_apb_retire_20260726_*.txt  NEW  run-dir artifacts (not committed)
  - this journal                         NEW
  No emulator code changed.
