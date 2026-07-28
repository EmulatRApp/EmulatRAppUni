<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-031
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.  All code addresses are SYSBOOT image VAs
(VA == image offset; loaded at PA 0x6b0000; carved-file offset = VA + 0x400).
-->

# JRN-SCSI-031 -- MECHANISM NAMED: %SYSBOOT-F-LDFAIL is a SECOND PASS of the
#                 symbol-vector relocation walk over a table the FIRST pass
#                 already relocated.  The "0x66666666 fill" never existed --
#                 it is the live relocation BITMAP.  Every conclusion below
#                 is static reads only; zero emulator runs this leg.

    Doc id   : JRN-SCSI-031
    Date     : 2026-07-27 (evening session)
    Status   : DECODE RECORD.  Partially supersedes JRN-SCSI-030 Sec 5-6
               (the 0x42af0 loop and the r25 residual -- see Sec 2).
    Relates  : JRN-SCSI-029/-030 (on branch worktree-snappc-cyclo-gate,
               NOT yet merged to v5-tb), JRN-SCSI-028 (handoff),
               journals/diags/20260727_sysboot_public_vectors_image_analysis.txt
               (Charon ground truth, provenance block added this session).
    Inputs   : predig_verdict4_cyc2053183936.axpsnap (era-certified capture),
               dka0.vdisk raw reads (carve recipe JRN-SCSI-029 Sec 1),
               static disassembly of VA 0x421c0..0x42b40 via
               tools/snap_va_disasm.py (parser fix, this session).

--------------------------------------------------------------------------------
## 1. The two kickstart reads (architect-directed), and what they returned

  (a) Sub-table content at symvec+0x58 (PV file offset 0xAA58, read from
      the raw vdisk):
        +0x00: 00000F00 00004000      <- chunk header {bitcount, image_off}
        +0x08: 66222000 66666666 ...  <- bitmap begins (NOT fill -- Sec 3)
        ...
        +0x1E0/+0x1E8 region tail: FF FF 07 00, then
        symvec+0x240: 00000000        <- NEXT-CHUNK COUNT = 0: chain TERMINATES.
      Symvec header while there: +0x18 = 0x54, +0x1C = 0x00000001,
      +0x20 = 0x58 (table offset -- the field the code actually uses).

  (b) r25 from the verdict4 snapshot register block (block self-certified:
      R0 = 0x13809A, R26 = 0x24EE4, R27 = 0x3330, R29 = 0x200DFF40 all match
      JRN-SCSI-029 Sec 8): R25 = 0x0.  Post-return value; the loop-time r25
      did not survive.  What DID survive (and decided this leg):
        R21 = 0xFFFFFFFF88000000   (sec1 S0 base, translate(0x4000))
        R22 = 0xFFFFFFFF8CC9A058   (chunk pointer, STILL AT CHUNK 0)
        R24 = 0x0000000000000EFF   (= bitcount 0xF00 - 1, INITIAL value)
        R16 = 0, R18 = 0x14730     (args intact)
      Verdict from the registers alone: the failing invocation died at the
      FIRST set bit of chunk 0 having done NO work.

--------------------------------------------------------------------------------
## 2. Correction to JRN-SCSI-030: the 0x42af0 loop never ran

  The full decode of 0x421c0..0x42b40 (Sec 3) shows the CMPLE r4,r25 loop
  at 0x42af0 sits on a leg gated at 0x42a70 by BEQ r19, where r19 = bit2 of
  *(0x14708).  Live value 0x1001 -> bit2 = 0 -> leg SKIPPED.  The loop that
  030 Sec 5 named as the mechanism never executed in EmulatR, and its r25
  provenance question (030 Sec 6) is DISSOLVED, not answered: that r25 is
  loaded two instructions above the loop (LDL r25,0(r17); ZAPNOT; LDA -1)
  from a count field, on a leg we never take.  The real mechanism is Sec 4.

--------------------------------------------------------------------------------
## 3. The translator decoded whole: a chunked bitmap RELOCATION walker

  Entry 0x421c0 (PDSC 0x3330).  Args r16 = *(digest+0x34), r17 = symvec S0,
  r18 = digest 0x14730.  Mode inputs read at entry:
    r1  (later r25) = bit2 of byte digest+0x3d   ("relocation done" flag)
    r19 (later r16) = bit2 of *( *(0x3328) ) = bit2 of *(0x14708)

  Structure walked, per chunk at r22 (first chunk at symvec + *(symvec+0x20)
  = symvec+0x58):
    { bitcount:u32 @+0, image_offset:u32 @+4, bitmap @+8 for bitcount>>3 bytes }
    next chunk header at +8 + bitcount>>3; count 0 terminates; on
    termination r17 += *(r17+0x24) chains to a next structure (0 here).

  Main leg (digest[0x3d] bit2 CLEAR -- ours), 0x42374..0x423f4:
    r24 = bitcount-1 = 0xEFF; r21 = classify(image_offset 0x4000) =
    entry[1].base = 0x88000000.  For each SET bit i in the bitmap:
      LDQ  r2, (r21 + i*8)          ; QUADWORD at the loaded sec1 copy
      classify r2 through the digest ranges (entry1, 3, 0, 2, 5, 4 in
      cascade order; sentinel entries lo=hi=0xFFFFFFFF sign-extend to -1
      and never match); in range -> r3 = (r2 - lo) + base
      STQ  r3, (r21 + i*8)          ; relocated IN PLACE
      NO range contains r2 -> LDQ r0,-0x10(r27) = 0x0013809A -> return.
    BADIMGOFF exits: 0x42790 / 0x427a0 / 0x427b0 (walk-time) and the
    0x42330 cascade tail at 0x427b0 (chunk-header time).

  With digest[0x3d] bit2 SET, BNE r25 at 0x42370 skips the quad walk for
  the chunk -- the flag is the walk's IDEMPOTENCE GUARD.  Two further
  legs (0x42540 second-table walk gated on r16; 0x42a74/0x42af0 count
  loop gated on r19) both key on bit2 of *(0x14708) and are skipped here.

--------------------------------------------------------------------------------
## 4. THE MECHANISM (static proof, three independent measurements)

  (1) FILE ORACLE.  Simulating the walk on raw vdisk bytes: 2497 set bits
      in the 0xF00-bit bitmap; EVERY selected quadword value in the file
      is <= 0xFFFF and classifies into a digest range; the chunk chain
      terminates at symvec+0x240 = 0.  The file walks CLEAN under this
      exact code.  Charon's success is reproduced from bytes on our disk.
      (Also: 0x66666666 as bitmap = relocate quad pairs, skip pairs --
      exactly a symbol vector's {entry, PV} descriptor layout.  The
      "fill" framing of 028/030 is retired.)

  (2) MEMORY STATE.  Page-table walk of the verdict4 snapshot (walker
      self-certified: VA 0xFFFFFFFF8CC9A000 -> PA 0x76E000, the 030
      ground truth) reads the sec1 S0 copy at VA 0xFFFFFFFF88000000
      (PA 0x1000000+).  EVERY bitmap-selected quad ALREADY HOLDS its
      correctly relocated value (file 0x10 -> 0xFFFFFFFF80000010, file
      0x4060 -> 0xFFFFFFFF88000060, ...).  A prior pass ran to
      completion, CORRECTLY.  EmulatR executed the whole relocation
      right at least once.

  (3) FAILING CALL.  Captured registers (Sec 1b) show the failing
      invocation at chunk 0, bit-counter initial, zero stores done.
      First set bit = quad 0x0D, memory value 0xFFFFFFFF80000000.
      Hand-trace of the cascade for that value (signed CMPLT vs lo =
      0x4000, -1, 0x0, -1, -1, 0xC000): every test routes onward and the
      entry[4] miss lands at the 0x42790 BADIMGOFF exit.  Deterministic.

  MECHANISM: pass 1 relocates the table correctly; digest+0x3d bit2 (the
  idempotence flag) is CLEAR at the second invocation (live digest+0x3c
  longword = 0x1); pass 2 re-walks, reads an already-relocated (huge/
  negative) quad at the first set bit, and faithfully reports BADIMGOFF.
  The loader then prints LDFAIL.  The instruction set is exonerated
  again; the divergence is WHO CALLS TWICE / WHO FAILED TO SET THE FLAG.

--------------------------------------------------------------------------------
## 5. Call-site map and the state word (all static)

  Translator 0x421c0 has exactly TWO call sites in the image:
    0x5ff00  BSR  -- inside wrapper 0x5fea0 (the failing chain; wrapper
                     itself has NO static BSR callers -> reached via its
                     PDSC, i.e. an indirect CALL)
    0x60980  BSR  -- inside a block 0x60948..0x609b8 with the IDENTICAL
                     argument recipe, gated at 0x60954 on bit2 of
                     *( *(r2-0x710) ) = bit2 of *(0x14708) (resolved from
                     the snapshot: the linkage quad holding PV 0x3330 is
                     at 0x186b8 -> r2 = 0x18678 -> cell 0x17f68 ->
                     0x14708), then per-call skip bits: digest+0x3c bit9
                     (first call) / bit11 (second call at 0x609b0 ->
                     routine 0x41620).
  Outer loader 0x5dcd0 has six BSR sites: 0x24ee0 (failing outer),
  0x24f98, 0x25af8, 0x273d8, 0x27468, 0x2ae64.

  The state word *(0x14708): FILE-INITIAL VALUE 0x00000000 (read from
  the carved SYSBOOT at file offset 0x14B08); LIVE VALUE 0x1001 (bits 0
  and 12).  It is RUNTIME-WRITTEN guest state, and with bit2 = 0 the
  0x60980 pass-1 site is SKIPPED in the captured run.  Therefore pass 1
  most plausibly came through wrapper 0x5fea0 ITSELF (invoked twice via
  its PDSC), with the done-flag store between passes either absent by
  design on this leg or dependent on state EmulatR gets wrong.

  digest+0x3c: live 0x00000001.  Bits 9/10/11 (the byte-0x3d flags the
  walker and the 0x60948 block key on) ALL CLEAR at capture.  NOTE: the
  JRN-SCSI-030 PA-WATCH window was +0x40..+0x9F -- the flags longword at
  +0x3c sits ONE LONGWORD outside the window edge.  The write ledger for
  the field that decides this failure was never captured.  (Instrument-
  integrity ledger: same species as 030 Sec 2's window-edge lesson.)

--------------------------------------------------------------------------------
## 6. Residual question (SINGULAR, now DYNAMIC) + probe plan

  WHO invokes the walk twice, and WHY is digest+0x3d bit2 clear at the
  second invocation?  The answer must terminate in machine-environment
  state (image bytes proven identical and clean).  Candidate surfaces,
  in the architect's pre-registered fork: IPR readback, HWRPB field,
  PALcode result, console-supplied datum -- now attached concretely to
  (a) the writer(s) of *(0x14708) and (b) the (missing) setter of
  digest+0x3d bit2.

  ONE BOOT, three probes (all existing facilities, no code):
    P1  EMULATR_PA_WATCH = 0x6C476C LEN 0x8   (digest +0x3c..+0x43):
        every writer of the flags longword -- catches the done-bit
        store, or proves its absence.
    P2  second run (or second watch if supported): PA 0x1000068 LEN 8
        (quad 0x0D of the sec1 S0 copy): pass-1's relocation store PC
        and cycle -- timestamps pass 1.
    P3  --snapshot-on-pc 0x5ff00 --snapshot-pc-cyclo 1250000000
        (instrument on branch worktree-snappc-cyclo-gate): fires at the
        FIRST wrapper invocation; its captured RA chain names pass-1's
        caller.  Fire-line discriminants self-certify era.
  VERDICT RULE: if a digest+0x3c done-bit store exists between the two
  invocations' windows on neither run -> the double CALL is the design
  question (walk the wrapper's PDSC callers, P3 RA); if a store exists
  under one flag regime but our *(0x14708)/HWRPB-derived state routes
  around it -> that state's writer is the root cause.

--------------------------------------------------------------------------------
## 7. Cleared / retired this leg

  - "Walk into 0x66666666 fill" (028/030 framing): RETIRED.  The bytes
    are the relocation bitmap; the file terminates the chain correctly.
  - 030's r25-provenance residual: DISSOLVED (leg never executes here).
  - Sec1 S0 copy integrity: VERIFIED CORRECT (post-pass-1 values exact).
  - Digest ranges/builder: stay exonerated (030), now corroborated by
    2497 correct relocations that USED them.
  - The failing call's zero-progress state: measured, consistent, closed.

--------------------------------------------------------------------------------
## 8. Files touched / artifacts

  - this journal                                NEW
  - tools/snap_va_disasm.py                     option-parser fix (the
    while-loop double-incremented i, breaking every two-option
    invocation, e.g. --pa-base + --va-base; found live this session)
  - scratchpad decode listing translator_421c0.asm (session-local; the
    load-bearing excerpts are inline above)
  - Branch note: JRN-SCSI-029/-030 + the --snapshot-pc-cyclo instrument
    live on worktree-snappc-cyclo-gate (pushed, unmerged).  Merge before
    running P3, or run P3 from that worktree's build.
