<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-030
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.  All code addresses are SYSBOOT image VAs
(VA == image offset; loaded at PA 0x6b0000; carved-file offset = VA + 0x400).
-->

# JRN-SCSI-030 -- The digest is EXONERATED END-TO-END.  The LDFAIL
#                 mechanism is a symbol-vector sub-table walk that steps
#                 into 0x66666666 fill; the residual is the PROVENANCE
#                 OF THE WALK BOUND (r25 at VA 0x42af0).

    Doc id   : JRN-SCSI-030
    Date     : 2026-07-27 (same session as JRN-SCSI-029; read 029 first)
    Status   : DECODE RECORD.  READ 029 Sec 8 then THIS on resume.
    Relates  : JRN-SCSI-029 (instrument + first capture + Sec 8 verdict,
               which THIS journal partially supersedes -- see Sec 2).

--------------------------------------------------------------------------------
## 1. Where this leg started and what it used

  In hand: carved SYSBOOT.EXE + SYS_PUBLIC_VECTORS.EXE (Charon-verified,
  JRN-SCSI-029 Sec 1-2), two era-certified snapshots
  (predig_oemsnap_cyc2381544638 = SYSBOOT> prompt;
   predig_verdict4_cyc2053183936 = at the outer status test, R0=0x13809A
   live), and the PA-WATCH facility.  No emulator code changed this leg
  beyond the JRN-SCSI-029 instrument (--snapshot-pc-cyclo).

## 2. PA-WATCH run: the digest builder's COMPLETE write ledger

  EMULATR_PA_WATCH=0x6C4770 LEN=0x60 (digest 0x14730 fields +0x40..+0x9F),
  fresh -fl 0 boot to LDFAIL.  56 stores, four eras:
    (1) cyc 0.8M   PC 0x9003fd  -- memtest pattern (console init)
    (2) cyc 2.0276e9 PC 0xb8060 -- zero sweep (pre-SYSBOOT clear)
    (3) cyc 2.0462e9 PC 0x69fc0-0x69ff8 -- digest zero-init loop
    (4) the BUILDER:
        - sentinel init PCs 0x5fad8/0x5fae0: {lo,hi}=0xFFFFFFFF into
          FOUR slots (entries 0-3) -- fill-then-populate confirmed.
        - populate PCs 0x5f958(vbn)/0x5f974(size)/0x5f990(lo)/
          0x5f9b4(hi)/0x59d50(S0 base): exactly TWO full iterations
          (sections 0 and 1), every value correct.
        - PC 0x5f898 writes +0x9c = 0xffffffff8cc9a000: at first read
          as an anomaly; Sec 3 resolves it -- it is entry[4].base,
          i.e. SECTION 2 BEING POPULATED into a different slot.

  JRN-SCSI-029 Sec 8's "range 3 never filled" claim is CORRECTED here:
  the slots at +0x94/+0x98 (entry[3]) belong to a section CLASS this
  image does not have; correctly sentinel.  Section 2 lands in
  entry[4] (+0x9c), whose lo/hi at +0xa8/+0xac lay OUTSIDE the watch
  window -- the window edge manufactured the "missing write".

## 3. The classifier gate, decoded (VA 0x5fb40-0x5fca4)

  Per EISD, flags byte read via LDQ_U/EXTBL at EISD+0x18, bits tested
  with SRL #n / BLBx; classes route to 20-byte digest entries that
  START at +0x4c (stride 0x14 -- matching the 0x61654 resolver's
  MULL #0x14):
    entry[i] = { base(S0), size, vbn, lo, hi } at +0x4c + i*0x14.
    - bits 11+7 (sec0 flags 0x880) -> entry[0]  FIRED, correct
    - bit 7     (sec1 flags 0x08A) -> entry[1]  FIRED, correct
    - bit 6     (sec2 flags 0x04A) -> entry[4]  FIRED, correct
    - bit 4 -> entry[5]; ctx-flag/fallback -> entry[2]; (none here)
  Digest read from the verdict4 snapshot (all six entries):
    entry[0]: base 0x80000000 size 0x2800 vbn  2 [0x0000,0x3fff]
    entry[1]: base 0x88000000 size 0x8000 vbn 22 [0x4000,0xbfff]
    entry[2]: empty (sentinels)      entry[3]: empty (sentinels)
    entry[4]: base 0x8cc9a000 size 0x400 vbn 86 [0xc000,0xffff]
    entry[5]: empty (sentinels)
  => GATE RIGHT.  BUILDER RIGHT.  DIGEST COMPLETE AND CORRECT.
  The architect's pre-registered third fork (a COUNT/bound, not the
  flags) is the live one -- see Sec 5.

## 4. The failing call, walked from the dead frames (verdict4 snapshot)

  Translator dead frame found by its saved-r27 signature (0x3330) at
  stack VA 0x200dfa60: RA 0x5ff04 -> the failing BSR is at VA 0x5ff00
  in caller 0x5fea0; saved_r3 = 0xffffffff8cc9a000 = entry[4].base =
  the LOADED SYMBOL VECTOR (S0).  Caller decode (VA 0x5fea0..):
    - gate: entry[4].size (digest+0xa0) != 0, else success/skip
    - r3 = entry[4].base (digest+0x9c)
    - unaligned longword at symvec+0x50: NONZERO -> status 0x13808A
      (different code!); zero -> proceed.  File value: 0.
    - args: r16=*(digest+0x34), r17=symvec, r18=digest, 3 args ->
      BSR 0x421c0 (the real translator, PDSC 0x3330, its own
      BADIMGOFF literal at VA 0x3320 = PV(-0x10(r27))).
  Memory-vs-file: the in-memory symvec (PA 0x76e000) is BYTE-IDENTICAL
  to PV file VBN 86 for all 0x400 bytes.  Data clean.

## 5. THE MECHANISM: sub-table walk into 0x66666666 fill

  The translator walks a table INSIDE the symbol vector:
    r22 = symvec + *(symvec+0x20);  *(symvec+0x20) = 0x58.
  File content at symvec+0x58 (= PV file offset 0xAA58):
    pair 0: { 0x00000f00, 0x00004000 }   <- offset 0x4000: VALID (sec1)
    pair 1+: 0x66666666 0x66666666 ...   <- fill, NOT a terminator
  Per pair: first longword ZAPNOT/BEQ -> zero terminates; second
  longword is the IMAGE OFFSET translated through the digest and the
  result stored out (STL r3,0(r21)).  Iteration: LDA r4,1(r4);
  CMPLE r4,r25 at VA 0x42af0 -> loop while r4 <= r25.
    - Walk stops after pair 0  -> offset 0x4000 -> SUCCESS (Charon).
    - Walk reaches pair 1      -> offset 0x66666666 -> no range
      contains it -> r0 = 0x0013809A -> LDFAIL (EmulatR, observed).
  Since the image data is identical under both emulators and the fill
  is not zero-terminated, THE WALK BOUND DECIDES THE WALL.

## 6. The residual question (SINGULAR, STATIC): r25's provenance

  r25 at the 0x42af0 loop is the entry count.  At function entry r25
  is loaded with a MODE BIT (BIS r1,r25 at VA 0x42224 region), from:
    - r1  = bit2 of the byte at digest+0x3d
      (LDQ_U/EXTBL at 0x3d(r18), SRL #2, AND #1)
    - r16 = bit2 of *( *(linkage cell 0x3328) ) = bit2 of *(0x14708)
  Live values (verdict4): digest+0x3d byte = 0x00 -> bit2 = 0;
  *(0x14708) = 0x1001 -> bit2 = 0.  With r25 = 0 the loop would run
  ONCE and succeed -- yet it demonstrably reached pair 1.  Therefore
  r25 is REWRITTEN between entry and the loop; the last write of r25
  before VA 0x42aec is not yet decoded.  THAT is the whole remaining
  question.  If its source is guest STATE derived from the machine/
  PAL environment (not image data), the EmulatR-vs-Charon divergence
  in that state is the root cause of %SYSBOOT-F-LDFAIL.

  NEXT ACTION (one static pass, everything in hand): disassemble
  VA 0x421c0..0x42af0 continuously; find every write to r25; for each
  candidate source field, read its live value from the verdict4
  snapshot; name the provenance.  Fallback dynamic: WREG=25 probe
  gated DIAG_PCLO/PCHI = 0x421c0/0x42b40 + SNAPPC-style cycle floor.

## 7. Cleared / closed as by-products this leg

  - Digest builder, classifier gate, digest CONTENT: exonerated (measured).
  - Symbol-vector memory image: byte-identical to file (measured).
  - Extract lanes EXTLL/EXTLH/EXTBL: EXERCISED on this exact path with
    correct outputs (vbn/size/lo/hi/base all right) -- upgraded from
    "correct but unexercised" (029 Sec 5).  Leaf-side suspect space
    for this wall is FORMALLY EMPTY: every subsequent finding is
    guest-logic vs guest-state, not instruction semantics.
  - F-8 MEM-drainer LDL sext + commit gate: clean by inspection (029).
  - Instrument-ledger species #5 (029 6b): "decoding the RIGHT code at
    the WRONG function" -- the 0x61654 resolver decode was internally
    correct but not on the failing path; caught BY the discriminant
    instrument (R27=0x3330 on the fire line), the first ledger entry
    caught by an instrument rather than after the fact.

## 8. Artifacts of record (keep)

  - snapshots/predig_verdict4_cyc2053183936.axpsnap  -- THE capture
    (era-certified; registers recoverable from CpuState blob at
    file offset 0x12c, intReg base; payload PA0 at file 0x3df4).
  - snapshots/predig_oemsnap_cyc2381544638.axpsnap   -- SYSBOOT> prompt
    state.  WARNING: mtime-touched newest 2026-07-27 -- a run WITHOUT
    --no-autoload will restore it; restored runs HANG in disk I/O
    (device state not serialized, 029 Sec 4).
  - out/build/relwithdebinfo/emulatr_pawatch_digest.log -- the 56-store
    write ledger (Sec 2).
  - Branch worktree-snappc-cyclo-gate (pushed): --snapshot-pc-cyclo
    instrument + JRN-SCSI-029/-030.  PR:
    https://github.com/EmulatRApp/EmulatRAppUni/pull/new/worktree-snappc-cyclo-gate
  - Grain-family oracle audit: delegated to a parallel comprehensive
    review (architect, Claude web); rides any fix commit as the
    confirming test + permanent guard.
