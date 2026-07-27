<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-029
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-029 -- BADIMGOFF check DECODED to the instruction; digest-table
#                 reframe; EXTxH cleared BY MEASUREMENT; era-gated
#                 snapshot trigger landed (--snapshot-pc-cyclo)

    Doc id   : JRN-SCSI-029
    Date     : 2026-07-27
    Status   : DECODE RECORD + instrument.  Read after JRN-SCSI-028.
    Relates  : JRN-SCSI-028 (handoff), JRN-SCSI-020/021 (EXTxH precedent),
               Charon ground truth: journals/diags/Image Analysis.txt

--------------------------------------------------------------------------------
## 1. Ground truth (architect, Charon + real VMS): file layout is now exact

  OpenVMS V8.3, HW_NAME "AlphaServer DS20" -- same platform EmulatR models.
  Both files CONTIGUOUS, single extent (DUMP/HEADER retrieval pointers):

    SYS$PUBLIC_VECTORS.EXE  (3829,1,0)  LBN 697408  count 160   EOF 157/496
    SYSBOOT.EXE             (3174,1,0)  LBN 350816  count 1184  EOF 1180/0

  dka0.vdisk is RAW (size == 8380080 blocks * 512 exactly); LBN n = byte
  offset n*512.  Carve recipe: read count*512 at LBN*512 (share-tolerant
  open required -- a live Charon session holds the file write-open).
  VERIFIED byte-exact against the VMS DUMP output at VBN1 (EIHD), VBN2
  (code page), and the FF fill tail.  Guest memory ALSO verified == file
  at every checked site (Sec 4).

## 2. CORRECTION to JRN-SCSI-028 Sec 5 ground truth

  The recorded EISD parse ("secsize 36, VBNs 2176/138/74") read wrong
  struct offsets: 36 is EISD$L_EISDSIZE, 2176 (=0x880) is EISD[0] FLAGS.
  Correct SYS$PUBLIC_VECTORS map (Alpha EISD: +8 size, +12 secsize,
  +16 va quad, +24 flags, +28 vbn):

    EISD[0]  va 0x0000-0x27FF  secsize 0x2800  vbn  2  flags 0x880
    -- VA HOLE 0x2800-0x3FFF (0x1800 bytes, no section) --
    EISD[1]  va 0x4000-0xBFFF  secsize 0x8000  vbn 22  flags 0x8A
    EISD[2]  va 0xC000-0xC3FF  secsize 0x400   vbn 86  flags 0x4A

  The hole is EXACTLY the image of file-offset-as-VA confusion for
  section 1's first 0x1800 bytes (file pos 0x2800 vs VA 0x4000, bias
  0x1800).  EPISTEMIC GUARD: Charon boots this image, so the algorithm
  is right -- a file-offset-shaped value under EmulatR means an
  INSTRUCTION LIED, and the shape names which computation to examine.
  "Why this image": SYS$PUBLIC_VECTORS is the first image through
  SYSBOOT's section-walking loader AND the first with a VA hole --
  first exposure of both the code path and the trap.

## 3. THE CHECK, DECODED TO THE INSTRUCTION (carved SYSBOOT, verified)

  SYSBOOT carve: EIHD size 720, isdoff 344; sections va 0/0xF400/
  0x20A00/0x71800/0x91600/0x92000 vbn 3/125/264/911/1166/1171; GBL
  deps SYS$BASE_IMAGE_001 ident 0x7F30A9E2, SYS$PUBLIC_VECTORS_001
  ident 0x7F30A77F (matches PV EIHD ident).  Transfer va 0xC78 = PDSC
  (entry 0x24820) -- OpenVMS calling standard, R27-relative linkage,
  so literals are per-procedure: static GP chase CLOSED.

  Status literal 0x0013809A: exactly ONE longword in the whole image,
  va 0x3320.  Inline builds (LDAH 0x14 / LDA -0x7F66): exactly TWO,
  both in the RESOLVER at va 0x61654:

    resolve(ctx=r16, offset=r17, index=r18, out=r19):
      0x61684  ADDL r31,r3,r3            ; index = sext32(index)
      0x6168c  LDL  r20, 0x118(ctx)      ; section COUNT
      0x61690  CMPULT r3,r20 ; BNE ok    ; index bound (bad -> gated
               msg print of 0x13809A, call, CALL_PAL HALT @0x616e4)
      0x616ec  LDL  r20, 0x11c(ctx)      ; digest TABLE base (longword)
      0x616f0  MULL r3,#0x14,r21         ; ENTRY SIZE = 20 BYTES
      0x616f4  ADDL r20,r21,r20          ; &entry -> FP+8
      0x61700  LDL  r22, 0(entry)        ; entry.BASE  (longword)
      0x61708  ADDQ offset,r22           ; addr = offset + entry.base
      0x61710  STQ  addr -> *out         ; RESULT WRITTEN BEFORE CHECK
      0x61718  LDL  r25, 4(entry)        ; entry.LIMIT (longword)
      0x61720  CMPULT offset,r25         ; UNSIGNED
      0x61724  BNE -> r0=1               ; else:
      0x61728  r0 = 0x0013809A           ; BADIMGOFF  <- our exit

  DIGEST-TABLE REFRAME (architect): SYSBOOT parses raw EISDs ONCE into
  20-byte entries; the walk faithfully reports a bad table.  The hole
  predicted well without being the mechanism -- a bent section-1 entry
  fails exactly the offsets adjacent to it.  NOTE: STQ-before-validate
  means a failing call still leaves a plausible address in *out --
  read caller-frame registers with that in mind.

  Failure epilogue (grounds the halt): caller at va 0x24ee0 BSRs the
  loader routine va 0x5dcd0; r0 status -> STL 0x38(r29); BLBC -> print
  calls -> CALL_PAL HALT at va 0x24f58 (word 0x00000000).  Halt state
  reproduced BYTE-IDENTICAL across three runs; survivors R21=0x3000
  (in-hole-shaped) and R12=0x4200 (section-1-VA-shaped) belong to
  CALLER frames (resolver's regs are epilogue-rebuilt).

  VERDICT TABLE for the capture (architect):
    offset >= limit, limit correct        -> offset-argument branch
    entry.limit wrong vs host parse       -> table-builder branch
    offset in 0x2800-0x3FFF, file-shaped  -> 0x1800 bias never added
    count/table base wrong                -> earlier still (digest hdr)

## 4. Runs, brackets, and the -fl 0,1 answer (branch c)

  - `b dka0 -fl 0` (3 runs): deterministic LDFAIL -> HALT @0x24f58.
  - `b dka0.0.0.8.0 -flags 0,1` (scripted): reaches SYSBOOT> -- early
    phase (param files, console callbacks, parsing) HEALTHY.  CONTINUE
    reproduces LDFAIL.  Architect's console-loop reports were -fl 0
    runs (PuTTY log confirms; conversational bit never set there).
  - Mid-SYSBOOT snapshot predig_oemsnap_cyc2381544638.axpsnap taken AT
    the SYSBOOT> prompt via the UART marker (fires from ANY era).
    Page tables are the boot table (PTBR 0x3ff04000); SYSBOOT image at
    PA 0x6b0000 with VA == image offset; memory == carved file at all
    checked sites (code AND data; sec1 has 143 runtime-state quads).
  - RESTORE-gated capture FAILED: restored machine spins in the load
    path (device state e.g. TsunamiTig not serialized -> post-restore
    disk I/O never completes).  Snapshot-restore is NOT yet a valid
    platform for I/O-dependent continuation runs.

## 5. EXTxH / leaf-side suspects CLEARED BY MEASUREMENT

  - coreLib/alpha_int_byteops.h EXT/INS/MSK/ZAP lanes read AARM-
    faithful post-06d6587 (incl. the byte_loc<5:0> aligned case).
  - logs/unaligned.log: ZERO unaligned fixups in the SYSBOOT loader
    window (all rows console-era PCs 0x93388-0x140488).  The unaligned
    fixup path is data-correct (byte-offset PA -> memcpy) AND was not
    exercised by the loader: the builder uses compiler-emitted LDQ_U
    idioms.  Plainly: the extract idioms were not merely correct
    there -- THEY WERE NOT EXERCISED.  Leaf-side suspects effectively
    cleared (audit INTERSECT loader-window inventory = empty).
  - F-8 (MEM drainer) read: formatLoadValue LDL sext32 is the textbook
    double-cast; commit gated on faultCode with R31 RAZ enforced;
    LDBU/LDWU zero-extend via GuestMemory sized reads.  No defect
    shape by inspection.  (Grain-family oracle audit delegated to a
    parallel comprehensive review -- rides any fix commit as the
    confirming test + permanent guard.)

## 6. Era lessons -> the instrument that landed

  - --snapshot-on-pc one-shots at reused low VAs get EATEN by the
    console era: five triggers armed across the resolver all fired in
    ONE straight-line console pass at cyc 1.1738e9 (deltas 8-118 cyc).
  - The emulator EXITS at guest CALL_PAL HALT (HaltedClean): there is
    no post-halt console in this configuration, so no post-failure
    marker snapshot either.  Both no-code paths exhausted -> approved
    ~20-line instrument:

  --snapshot-pc-cyclo <n> / EMULATR_SNAPPC_CYCLO (CLI wins): retire-
  cycle FLOOR for snapshot-on-pc; below it triggers neither match nor
  are consumed.  Fire line now logs REGIME DISCRIMINANTS (ptbr,
  palBase, r29) so the artifact SELF-CERTIFIES its era (0x42790
  lesson: gate may be a proxy, but the capture carries evidence), and
  r29 doubles as the frame anchor for reading the captured stack.
  Files: systemLib/AppOptions.{h,cpp}, systemLib/Machine.{h,cpp},
  main.cpp.  Floor for this hunt: 1.6e9 (console fire 1.17e9 <<
  floor << SYSBOOT era 1.9e9+, drift ~40M).

## 6b. Instrument-integrity ledger, instances 5 and 6 (this session)

  5. FLOOR THREADED TOO HIGH: first verdict run used floor 1.6e9 on
     the reasoning "halt at ~2e9, check shortly before".  WRONG: the
     LDFAIL message emission through the console callback costs
     hundreds of millions of cycles, so the CHECK retires far below
     the halt -- the floor suppressed the SYSBOOT-era fire.  The rule
     JRN-SCSI-028 Sec 6 already stated: floors go just above the era
     you exclude (console pass 1.174e9, driver-timed, reproducible),
     NOT threaded near the event you want.  Corrected floor: 1.25e9.
  6. BARE-CONFIGURED WORKTREE BUILD DIVERGES: `cmake -S . -B .` in a
     fresh worktree takes DEFAULT option values -- EMULATR_IRQDIAG
     came up ON (main tree: OFF), drowning the run in 2.1 GB of
     IRQDIAG stderr and slowing boot past the driver timeout.  A
     worktree build must inherit the parent tree's EMULATR_* cache
     options (copy them from the parent CMakeCache.txt) or it is a
     DIFFERENT instrument answering confidently.

## 7. Capture plan (running / next)

  Fresh -fl 0 boot, --snapshot-on-pc 0x61728 --snapshot-pc-cyclo
  1600000000, EMULATR_NO_PUTTY=1, driver to LDFAIL.  From the fired
  snapshot: r29 (fire line) -> FP; read FP+0x18 offset, FP+0x20 ctx,
  FP+0x10 out, FP+8 &entry; ctx+0x118 count, ctx+0x11c table; dump
  all 20-byte entries; diff vs host-parsed EISDs; apply Sec 3 verdict
  table.  One capture decides the branch.

## 8. CAPTURE RESULT (verdict4, cyc 2053183936): DIGEST RANGE 3 UNFILLED

  Triggers at 0x61728 never fire in SYSBOOT era even above a correct
  floor: the 0x61654 resolver is NOT on the failing path (its two
  inline status builds unexecuted).  CORRECTION to Sec 6b instance 5:
  the 1.6e9-floor run failed because the PC never retires, not
  because the floor clipped it.  Era-safe trigger that DID fire:
  the outer status test VA 0x24ef0 (BLBC r0 after the loader call),
  floor 1.25e9.  Fire line self-certified: cyc 2.0532e9, ptbr 0,
  palBase 0x8000, r29 0x200dff40.

  Registers recovered from the CpuState blob (locate intReg by the
  logged r29 value):  R0 = 0x0013809A LIVE.  R26 = 0x24ee4 (RA of the
  failing BSR).  R27 = 0x3330 -- the failing callee's PV, SIXTEEN
  BYTES past the unique status literal (loaded as -0x10(r27)).
  PDSC @0x3330: entry VA 0x421C0 = the REAL translator: a range
  classifier over a digest struct (r18 = 0x14730, SYSBOOT sec1 data):
  per range {lo,hi,base}: offset in [lo..hi] -> (offset-lo)+base.

  Digest live values (snapshot, PA 0x6b0000+0x14730):
    range1 +0x58/+0x5c       = [0x0,     0x3FFF]  (sec0; hi spans hole)
    range2 +0x6c/+0x70/+0x60 = [0x4000,  0xBFFF] -> 0x88000000 (S0!)
                                +0x64 size 0x8000, +0x68 vbn 22: RIGHT
    range3 +0x94/+0x98/+0x88 = [0xFFFFFFFF, 0xFFFFFFFF] -> 0
                                *** NEVER FILLED (sentinels) ***

  VERDICT (Sec 3 table row 4): failure is in the DIGEST BUILDER --
  it filled sections 0/1 correctly and never filled section 2
  (va 0xC000, PV's SYMBOL VECTOR).  The translator then faithfully
  rejects every symbol-vector offset -> BADIMGOFF -> LDFAIL.  Prime
  suspect: the builder's per-EISD condition on EISD[2] (flags 0x4A
  vs section 1's 0x8A) or an early loop exit.  Charon fills all 3.

  NEXT (zero-code): EMULATR_PA_WATCH=0x6C4788 LEN=0x48 (digest
  +0x58..+0x9C) in a fresh -fl 0 run -> writer PCs for ranges 1/2
  (positive control) and the missing/wrong write for range 3.
