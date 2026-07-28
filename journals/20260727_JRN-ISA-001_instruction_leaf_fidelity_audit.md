<!--
EmulatR V5 -- Implementation Journal JRN-ISA-001
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-ISA-001 -- INSTRUCTION LEAF FIDELITY AUDIT (session 2026-07-27)
#                Five leaf files read against the AARM and the EV6 HRM.
#                12 findings, 2 blocking-verify, 0 on the BADIMGOFF path.
#                Coverage is PARTIAL -- see Sec 2 for the honest matrix.

    Doc id   : JRN-ISA-001
    Date     : 2026-07-27
    Status   : AUDIT RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-020 (EXTxH aligned-Rb root cause -- the defect class
               this audit generalizes), JRN-SCSI-026/027/028 (halt-10 arc),
               JRN-VMB-017 Part 2 (HW_LD/HW_ST EA truncation).
    Authority: alpha_arch_ref.txt (AARM) and
               Alpha_21264-EV67_Microprocessor_Hardware_Reference_Manual.txt
               (EV6 HRM), both in project knowledge.  AXPBox and Charon are
               corroborative only and are NOT authority for any verdict here.

--------------------------------------------------------------------------------
## 0. READ THIS FIRST (scope guard)

  NONE of the 12 findings below is on the %SYSBOOT-F-LDFAIL / BADIMGOFF
  path.  The integer arithmetic, the CMPULT that raises BADIMGOFF, the
  loads, the address math, and the branch predicates all PASS.  This
  document is a work queue, not a hunt redirect.  Do not let it pull
  effort off the address-gap investigation.

  The one exception worth a glance while in the neighbourhood: F-10
  (HW_LD type-mode hints, incl. VPTE, are ignored) touches the VMS PAL
  DTBM path.  If the digest-table builder or its callers run through
  PAL, F-10 is the only item here that could plausibly intersect.

--------------------------------------------------------------------------------
## 1. WHY THIS AUDIT EXISTS

  JRN-SCSI-020 established the campaign's dominant defect class: a leaf
  that is a CORRECT IMPLEMENTATION OF A WRONG SCOPE.  EXTxH was faithful
  for unaligned Rb and wrong for the aligned case; MTPR_VPTB was faithful
  for the console era and wrong for the OS era.  Neither contained a
  visible mistake -- the code matched its author's model, and the model
  was smaller than the architecture.

  The rule that follows: a leaf is not done when it handles its
  motivating case.  It is done when it has been diffed against the FULL
  pseudocode, edge rows included, with the edges pinned in tests.  This
  audit applies that rule retroactively, file by file.

--------------------------------------------------------------------------------
## 2. COVERAGE MATRIX (honest -- coverage is NOT complete)

  AUDITED (bodies read, diffed against AARM/HRM pseudocode):

    eBoxLib/grains/IntArith.cpp        opcodes 0x10 0x11 0x12 0x13,
                                       0x14 (ITOF*), 0x18 (RC/RS/RPCC/
                                       IMPLVER/AMASK), 0x1C (FPTI)
    cBoxLib/grains/CacheOps.cpp        0x18 TRAPB EXCB MB WMB ECB
    iBoxLib/grains/ControlFlow.cpp     0x30-0x3F integer branches, 0x1A
    mBoxLib/grains/LoadStore.cpp       0x08-0x0F, 0x28-0x2F, 0x1B, 0x1F,
                                       0x18 FETCH/FETCH_M/WH64/WH64EN
    fBoxLib/grains/Float.cpp           0x17 CPYS family + MF/MT_FPCR,
                                       0x16 ADDT/SUBT/MULT/DIVT +
                                       S-forms + CMPTEQ/LT/LE,
                                       0x20-0x27 FP load/store
    fBoxLib/grains/FpExec.h            qualifier decode + FPCR fold
    fBoxLib/grains/FpFormat.h          S/T/F/G format conversion

  ENUMERATED BUT NOT AUDITED (bodies not read):

    fBoxLib/grains/FloatVariants.cpp   225 leaves, 2305 lines.  Covers
                                       0x15 VAX FP (ADDF/G, SUBF/G,
                                       MULF/G, DIVF/G, CMPGxx), the full
                                       CVT family (CVTQS/QT/TS/TQ/ST/
                                       GF/GD/DG/GQ/QF/QG/LQ/QL), SQRT
                                       (F/G/S/T), FP branches (FBEQ FBNE
                                       FBLT FBGE FBLE FBGT), FCMOV
                                       family.  NOT VERIFIED.

  NOT AUDITED AT ALL (no file seen this session):

    PalEntries.cpp                     CALL_PAL, HW_MFPR, HW_MTPR,
                                       HW_REI, HW_RET, execCserve,
                                       execSwpctxVms, execMtprVptb_vms.
                                       HIGHEST RESIDUAL RISK -- two of
                                       the campaign's three root causes
                                       lived here.
    coreLib/BoxResult.h                blocking for F-8
    MEM-stage drainer / WB commit      owns load sign-extension and the
                                       regWrite gate -- i.e. owns half
                                       the correctness of every leaf
                                       audited above
    coreLib/alpha_int_helpers.h        alpha_int::umulh (F-4)
    coreLib/alpha_fpcr_core.h          ArithmeticStatus::applyToFpcr
    fpBoxLib/fp_backend.*              SoftFloat seam

  KNOWN OPCODE GAP:

    FTOIS (0x1C func 0x78) was not found in any audited file.  FTOIT is
    present in IntArith.cpp.  [CONFIRM] whether FTOIS exists elsewhere
    or is genuinely unwired.

  Prior audit, carried forward from an earlier session:

    coreLib/alpha_int_byteops.h        all 27 helpers verified correct
                                       (post-JRN-SCSI-020 fix).  Two doc
                                       defects logged as B-1.

--------------------------------------------------------------------------------
## 3. FINDINGS

### F-8  [BLOCKING VERIFY]  BoxResult default regWriteIdx / commit gate

  Files: coreLib/BoxResult.h (read), plus call sites in
         iBoxLib/grains/ControlFlow.cpp, cBoxLib/grains/CacheOps.cpp,
         mBoxLib/grains/LoadStore.cpp

  Three files, two conventions:

    LoadStore.cpp   stores explicitly set  r.regWriteIdx = kNoRegWrite;
                    (execStb/Stw/Stl/Stq/StqU/HwSt)
    ControlFlow.cpp the eight conditional branches (BEQ BNE BLT BGE BLE
                    BGT BLBC BLBS) set only semFlags + divert fields and
                    leave regWriteIdx DEFAULTED
    CacheOps.cpp    all five barriers likewise leave it DEFAULTED

  kNoRegWrite is a named coreLib constant that only one of the three
  files uses.  If the default-constructed value equals kNoRegWrite, the
  LoadStore assignments are redundant-but-correct and the others are
  fine.  If it does not, then every conditional branch and every barrier
  packs a register-commit index, and correctness rests entirely on the
  MEM drainer gating on semFlags rather than on the index.

  ACTION (one file read, then one decision):
    1. Read coreLib/BoxResult.h -- what is regWriteIdx's default
       initializer, and what is kNoRegWrite's value?
    2. Read the MEM-stage drainer -- is the regfile commit gated on
       (regWriteIdx != kNoRegWrite), on a semFlags bit, or both?
    3. Whichever the answer, make the convention UNIFORM across the
       three files and state it once in BoxResult.h.  Silent reliance on
       a default is the shape this campaign keeps paying for.

  Priority: do this before anything else on the ledger.  Conditional
  branches are the most frequently retired instructions in the machine.

### F-1  [FIDELITY]  RPCC returns the wrong architectural format

  File: eBoxLib/grains/IntArith.cpp, execRpcc
  Also: execHwMfpr HW_CC case (PalEntries.cpp) -- the leaf's own comment
        states the two mirror each other, so both carry the defect.

  EV6 HRM Sec 5.1.1 (Cycle Counter Register CC):
    CC<31:0>  = COUNTER, increments once per CPU cycle when enabled by
                CC_CTL[32]
    CC<63:32> = OFFSET, 32 bits of register storage
    "A HW_MTPR instruction to the CC writes the upper half of the
     register and leaves the lower half unchanged.  The RPCC
     instruction returns the full 64-bit value of the register."

  AARM Sec 4.11.9 gives the canonical software idiom, which exists
  precisely BECAUSE hardware does not pre-sum the fields:

      RPCC    R0
      SLL     R0, #32, R1
      ADDQ    R0, R1, R0
      SRL     R0, #32, R0

  Current implementation:

      r.regWriteValue = (c.cpu->cycleCount + c.cpu->ccOffset)
                      * coreLib::CpuState::kCcMultiplier;

  Three deviations: the two fields are conflated into a flat sum; the
  counter is not truncated to 32 bits, so <63:32> carries counter
  overflow instead of OFFSET; and the canonical idiom computes garbage
  on the result.  The ZAPNOT R0,#15 idiom survives (it takes the low 32
  bits of a monotonic value), which is very likely why boot works --
  the JRN-SCSI-020 shape exactly: correct for the case tested.

  AARM Sec 3.1.5 notes OpenVMS supplies a per-thread value in PCC_OFF,
  so this is masked now and live under the OS.

  CORRECT FORM:
      value = ((offset & 0xFFFFFFFF) << 32) | (counter & 0xFFFFFFFF)
  with HW_MTPR CC writing ONLY the upper half.  kCcMultiplier is a
  named deviation and may stay, but it must scale the COUNTER FIELD
  ONLY, not the packed 64-bit value.

  TEST: doctest pinning the AARM idiom -- after RPCC/SLL/ADDQ/SRL the
  result must equal (PCC_OFF + PCC_CNT) mod 2^32.

### F-2  [CONVENTION + FREEZE LEAK]  TEMP RPCC probe still in a hot leaf

  File: eBoxLib/grains/IntArith.cpp, execRpcc (probe block), plus the
        two TEMP includes <cstdio> and <cstdlib> at the top of file.

  Dated 2026-06-01 and self-labelled "REVERT this whole block + the two
  TEMP includes after the gating run."  Eight weeks stale.  Four
  separate problems:

    1. Runtime-gated, not compile-guarded.  House rule: diagnostics live
       behind EMULATR_DIAG_* and compile to ((void)0) in release.
    2. ARMED BY DEFAULT.  s_rpccLogAfter defaults to 185000000 with no
       env var required, so the probe is live in every build.
    3. Hardcodes  D:\EmulatR\EmulatRAppUniV4\rpcc_probe.txt  -- a V4
       absolute path living in the V5 tree.  This is exactly the
       freeze-leak vector on the V5 housekeeping list.
    4. fopen/fprintf/fflush per RPCC retire on AXP_HOT code.  The
       per-call fflush is a serious drag and a determinism concern.

  ACTION: delete the probe block and the two TEMP includes outright.
  No replacement needed; if RPCC instrumentation is wanted again, it
  gets the standard two-tier treatment (compile guard outside, env key
  inside) and writes under the run directory's ./logs per the
  emulatr-log-trace-output convention.

### F-5  [FIDELITY, DATA LOSS]  S/F float conversions drop the fraction
                                when the exponent field is zero

  File: fBoxLib/grains/FpFormat.h
  Functions: convertS_FloatingToRegister, convertS_FloatingToMemory,
             convertF_FloatingToRegister, convertF_FloatingToMemory
  Propagates to: execItofs, execItoff (IntArith.cpp), and the LDS/STS/
                 LDF/STF leaves in Float.cpp.

  AARM LDS is a PURE CONCATENATION -- MAP_S touches only the exponent
  and the fraction is copied unconditionally:

      Fa <- (va')<31> || MAP_S((va')<30:23>) || (va')<22:0> || 0<28:0>

  MAP_S Table 2-2 row 4 says exp8 = 0 maps to exp11 = 0.  It does NOT
  say "and discard the fraction."  STS is more explicit still -- no
  special cases at all, pure bit extraction:

      (va')<31:0> <- Fav<63:62> || Fav<58:29>

  Current implementation:

      if (exp8 == 0)  { return sign << 63; }   // load:  drops frac<22:0>
      if (exp11 == 0) { return sign << 31; }   // store: drops frac

  WHY THIS MATTERS MORE THAN DENORMALS: the AARM mnemonics are
  literally "LDS -- Load S_floating (LOAD LONGWORD INTEGER)" and
  "STS -- Store S_floating (STORE LONGWORD INTEGER)".  These are the
  canonical path for moving raw 32-bit integers through FP registers
  (the LDS -> CVTLQ idiom).  Any longword whose bits <30:23> are zero
  -- i.e. every small positive integer below 0x00800000 -- loses its
  low 23 bits.  Load 0x00000005 via LDS and the register reads zero.
  AARM annotates STS/STT as "Bits Only -- No Exceptions."

  Same defect symmetrically in the F_floating pair.  Lower severity
  (a valid VAX F datum with exp = 0 has zero fraction; only reserved
  operands and dirty zeros are affected) but the AARM is equally clear
  that STF "does no checking of the low-order fraction bits."

  FIX: delete the four early returns; preserve the fraction in all
  four functions.  Load path becomes
      (sign << 63) | (exp11 << 52) | (frac << 29)   with exp11 = 0.
  Store path: run the general extraction unconditionally.

  CREDIT WHERE DUE -- DO NOT "FIX" THIS: the S/F asymmetry at exp8 =
  0xFF is handled CORRECTLY.  MAP_F Table 2-1 row 1 maps 1 1111111 to
  1 000 1111111 (0x47F, since VAX has no INF/NaN) while MAP_S Table 2-2
  row 1 maps it to 1 111 1111111 (0x7FF).  convertS_ has the 0xFF ->
  0x7FF special case; convertF_ correctly does NOT, and its general
  formula yields 0x47F.  That distinction is right.  Preserve it.

### F-7  [DOCTRINE]  AXPBox cited as behavioural authority (6 sites)

  File: iBoxLib/grains/ControlFlow.cpp
  Sites: execBr, execBsr, execJmp, execJsr, execRet, execJsrCoroutine

  Every link-write carries:

      r.regWriteValue = (g.pc + 4) & ~uint64_t{3};
          // clear PALmode/align bits from link (AXPBox: pc & ~3)

  AXPBox is corroborative only and is never ground truth for behaviour.
  The behaviour may well be correct; the JUSTIFICATION is not, and a
  future reader inherits a citation to the wrong oracle.  Replace with
  an AARM / EV6-HRM-grounded rationale, or mark _PROVISIONAL pending
  one.

  AND THE UNDERLYING QUESTION IS REAL.  AARM Jump-format operation:

      {update PC}
      va <- Rbv AND {NOT 3}
      Ra <- PC
      PC <- va

  "Ra <- PC" is unqualified.  Under PALmode Route B, g.pc bit 0 IS the
  PALmode flag -- so & ~3 STRIPS PALMODE FROM THE SAVED RETURN ADDRESS.
  The implementation compensates by carrying (g.pc & 1) onto the jump
  target:

      r.divertTarget = (c.opB & ~0x3ULL) | (g.pc & 1ULL);

  so BSR r26 ... RET (r26) inside PALcode round-trips correctly.  But
  the compensation is LOCAL.  If PALcode saves the link register and
  later reaches it via HW_REI (which takes mode from the target's bit
  0), the mode is lost.  Same shape as the F1 palMode landmine: masked
  under the current path, live under another.

  [CONFIRM] against ev6_vms_pc264_pal.mar: does any PAL routine consume
  a BSR/JSR link register through HW_REI (as opposed to RET/JMP)?  If
  yes, the link must preserve bit 0 and the compensation must move.

### F-10 [LATENT FIDELITY]  HW_LD type-mode hints ignored, incl. VPTE

  File: mBoxLib/grains/LoadStore.cpp, execHwLd / execHwSt

  Bits <15:13> of the Hw-format encoding carry PHYS / ALT / WRT_CHK /
  VPTE mode selectors.  The file documents ignoring them, with the
  rationale that "in PAL mode the translator already returns PA = VA
  for any kseg-style access, which covers every PHYS path the SRM
  decompressor exercises."

  That rationale was sound FOR THE SRM DECOMPRESSOR.  We are now
  running OS-era VMS PAL.  VPTE mode specifically selects the
  virtual-PTE fetch path used by the DTBM handlers -- the exact
  machinery JRN-VMB-010 and the VPTB desync arc worked in.

  ACTION: re-validate the assumption under VMS PAL and mark
  _PROVISIONAL if it has not been.  This is the ONLY finding in this
  document with a plausible line to the current hunt.

### F-3  [DOC DEFECT]  RC/RS comment block is inverted

  File: eBoxLib/grains/IntArith.cpp, header block above execRc

  Comment reads "RC: Ra <- intrFlag; then intrFlag <- 1.  RS: ... <- 0"
  and the placeholder sketch says "// RC: set;  RS: clear".  The AARM
  has it the other way: RS SETS, RC CLEARS.  execRc and execRs
  implement it CORRECTLY -- only the comment is wrong.

  Same class as the ZAP/ZAPNOT worked-example defect (B-1).  In an
  Oracle codebase a wrong worked example is a trap for the next
  auditor, who will "verify" against it.

### F-6  [UNNAMED DEVIATION]  FP trap qualifier 0x4 (/S alone) silently
                              maps to None

  File: fBoxLib/grains/FpExec.h, fpVariantFromEncoded

  The trap switch handles 0x0 (default), 0x1 (/U), 0x5 (/SU), 0x7
  (/SUI) and sends everything else to default: None.  Trap field 0x4 is
  /S alone -- valid on VAX forms (e.g. ADDF/S = 0x480).  Software-
  completion semantics are silently dropped.

  Low impact (no VAX FP in this workload) but it is an UNNAMED
  deviation.  Either handle it or state it explicitly in the comment,
  per the hard-stop-over-silent-degradation rule.

  VERIFIED CORRECT while in this function, for the record: func =
  encoded[15:5]; rounding at [7:6]; trap at [10:8].  Checked against
  real encodings -- ADDS 0x080 nearest, /C 0x000 chop, /M 0x040 -inf,
  /D 0x0C0 dynamic, /U 0x180, /SU 0x580, /SUI 0x780.  All correct.

### F-12 [ARCHITECTURAL INCONSISTENCY]  two competing variant patterns

  Files: fBoxLib/grains/Float.cpp  vs  fBoxLib/grains/FloatVariants.cpp

  FpExec.h's header states the design intent explicitly: "all 16
  trap-mode variants funnel through ONE consolidated leaf per base op
  (the leaf is qualifier-agnostic; the qualifier becomes data passed to
  the backend)."

  Float.cpp follows that pattern -- execAddt, execCmpteq etc. call
  fpVariantFromEncoded and pass the variant to the backend.

  FloatVariants.cpp does the OPPOSITE -- 225 explicitly expanded
  per-variant leaves (execAddfC, execAddfSu, execCvttqSvid, ...).

  And at least one instruction appears in BOTH shapes:
      Float.cpp          execCmpteq      (consolidated, handles /SU via
                                          fpVariantFromEncoded)
      FloatVariants.cpp  execCmpteqSu    (explicit variant leaf)

  [CONFIRM] against GrainMasterV4.tsv: which leaf does CMPTEQ/SU
  actually dispatch to?  If both exist and only one is wired, the other
  is dead code that will drift.  If the TSV routes some qualifier
  combinations to one file and some to the other, that is a correctness
  hazard -- two implementations of one instruction, diverging silently.

  This matters more under a TB/JIT tier than it does today: a
  translator that trusts semFlags needs exactly one authoritative leaf
  per (opcode, qualifier) pair.

### F-4  [VERIFY]  helper and sink confirmations

  1. coreLib/alpha_int_helpers.h -- alpha_int::umulh was not in scope
     this session.  Verify against the AARM definition (high 64 bits of
     the unsigned 128-bit product).
  2. Confirm the WB stage discards writes to R31 / F31.  Every leaf
     emits regWriteIdx = (encoded & 0x1F) unconditionally, which is
     correct ONLY IF the sink drops index 31.  Related to F-8 but a
     distinct question.

### F-9 / F-11  [DOC DEFECT]  stale file headers

  1. iBoxLib/grains/ControlFlow.cpp -- header claims "ten control-flow
     leaves" and lists only BR, BSR, BEQ, BLT, BNE, BGE for Bra-format.
     The file actually implements fourteen, adding BLE, BGT, BLBC,
     BLBS.  (All eight integer branch opcodes 0x38-0x3F ARE covered --
     only the comment is wrong.)
  2. mBoxLib/grains/LoadStore.cpp -- header claims "seventeen wired
     leaves" and says "HW_LD / HW_ST ... remain stubbed for a follow-up
     wave."  Both are fully implemented below, and FETCH_M / WH64 /
     WH64EN are wired.  Stale by at least the 2026-07-24 change that
     the file's own later comments document.

--------------------------------------------------------------------------------
## 4. WHAT PASSED (positive verdicts worth recording)

  These are not "not yet convicted" -- they were read against the
  pseudocode and found correct.  Recording them so the same ground is
  not re-walked.

  eBoxLib/grains/IntArith.cpp
    ADDL/SUBL          32-bit wrap then sign-extend -- correct
    ADDQ/SUBQ          mod 2^64 -- correct
    S4/S8 ADD/SUB x8   (Rav<<n)+Rbv on full 64 bits, then <31:0>
                       sign-extended for the L forms.  Matches
                       SEXT((LEFT_SHIFT(Rav,2)+Rbv)<31:0>) exactly.
    MULQ / MULL        truncating operands first is equivalent -- the
                       low 32 bits of a product depend only on the low
                       32 bits of the operands
    CMPEQ/ULT/ULE/LT/LE  correct signed / unsigned split.  CMPULT is
                       a plain 64-bit unsigned compare -- NO truncation,
                       NO canonicalization involvement.  *** This is
                       the instruction that raises BADIMGOFF.  It is
                       architecturally correct. ***
    CMOV x8            correct
    SLL/SRL/SRA        correct <5:0> masking; SRA arithmetic per C++20
    CMPBGE             correct, Rc<63:8> = 0
    SEXTB / SEXTW      correct
    CTPOP/CTLZ/CTTZ    correct, including the zero -> 64 cases
    MVI min/max x16    correct lane widths and signedness
    PERR               correct
    UNPKBW/UNPKBL/PKWB/PKLB  bit placements verified against the AARM
                       field maps
    AMASK              EV6 feature set BWX|FIX|CIX|MVI correct;
                       PAT/PMI correctly excluded
    IMPLVER            2 for EV6 -- correct
    FTOIT / ITOFT      raw bit copies -- correct
    EXT/INS/MSK/ZAP    thin wrappers over the previously audited
                       byteops helpers; opA = value, opB = offset --
                       correct pass-through

  cBoxLib/grains/CacheOps.cpp
    TRAPB EXCB MB WMB ECB  all five correct, and correct FOR THE STATED
                       REASON.  The file argues its own soundness from
                       the single-issue in-order model rather than
                       asserting no-op-ness.  ECB as a no-op is
                       explicitly permitted by the AARM.

  iBoxLib/grains/ControlFlow.cpp
    braDispSext        (enc<<11)>>11 sign-extends bits<20:0>, x4 for
                       longwords, target = pc+4+disp*4 -- matches AARM
    8 integer branch predicates  all correct and correctly signed;
                       BLBC/BLBS correctly distinguished from BEQ/BNE
    Jump-format        va <- Rbv AND NOT 3 -- correct.  Ra <- PC before
                       PC <- va ordering safely handles the AARM's
                       explicit "Ra and Rb may specify the same
                       register" case.  Hint bits ignored, as permitted.

  mBoxLib/grains/LoadStore.cpp
    memDispSext / hwDispSext   16-bit and 12-bit sign-extension correct
    LDA / LDAH         Rb+sext(disp) and Rb+(sext(disp)<<16) -- correct
    LDBU/LDWU/LDL/LDQ  correct sizes; zero- vs sign-extend split correct
                       and correctly DELEGATED to the drainer rather
                       than duplicated per leaf
    LDQ_U / STQ_U      EA & ~7 per spec
    LDx_L / STx_C      correct dual-effect shape (memory effect plus Ra
                       success indicator) with S_Locked carried
    HW_LD / HW_ST      EA truncation to access size is CORRECT and is
                       the best-documented comment in the audited set:
                       behaviour, EV6 rationale, the exact PAL walk that
                       depends on it, the concrete failure it caused,
                       and the journal reference.  This is the standard
                       the rest of the tree should match.
    FETCH / FETCH_M / WH64 / WH64EN  correct as no-ops.  The reasoning
                       recorded there -- "hints must be no-ops, never
                       OPCDEC" -- caught a real halt-code-2 bug.

  fBoxLib/grains/Float.cpp
    CPYS / CPYSN / CPYSE   bit-exact per AARM
    ADDT/SUBT/MULT/DIVT + S-forms   correctly routed to the SoftFloat
                       backend with the qualifier passed as DATA, FPCR
                       folded after -- the right architecture
    CMPTEQ / CMPTLT / CMPTLE   2.0-or-0.0 convention correct
    T_floating conversions   identity, correct
    VAX G word swap    self-inverse, matches LDG's mem<15:0> ->
                       reg<63:48> reversal

  fBoxLib/grains/FpExec.h
    fpVariantFromEncoded   field extraction and rounding/trap decode
                       verified against real encodings (see F-6)
    foldFpcrExc        sticky-bit fold is the correct shape

--------------------------------------------------------------------------------
## 5. SUGGESTED WORK ORDER

  Do NOT interleave these with the address-gap hunt.  Suggested batching
  when the hunt yields:

  BATCH 1 -- verification only, no behaviour change, ~1 hour
    F-8   read BoxResult.h + the MEM drainer; settle the default and
          unify the convention across the three files
    F-4   verify alpha_int::umulh; confirm R31/F31 sink
    F-12  check GrainMasterV4.tsv for CMPTEQ/SU dual dispatch
    F-10  re-validate HW_LD type-mode assumption under VMS PAL

  BATCH 2 -- deletions and comments, no behaviour change
    F-2   delete the TEMP RPCC probe block + two includes
    F-3   fix the RC/RS comment inversion
    F-7   replace the AXPBox citations with AARM/HRM rationale (or
          _PROVISIONAL), and file the [CONFIRM] on PAL HW_REI consumers
    F-9   fix the ControlFlow.cpp header
    F-11  fix the LoadStore.cpp header
    B-1   fix the ZAP/ZAPNOT worked examples in alpha_int_byteops.h and
          add the INSxH "bp==0 -> 0 is ARCHITECTED here, the INVERSE of
          the EXTxH <5:0> pass-through -- do NOT apply the JRN-SCSI-020
          fix pattern to this family" note

  BATCH 3 -- behaviour changes, each with doctests, under the full
             do-no-harm gate (suite + DS10/DS20/ES40 boot to P00>>>)
    F-1   RPCC / HW_MFPR CC packed format, HW_MTPR CC upper-half-only,
          kCcMultiplier scoped to the counter field
    F-5   FpFormat.h fraction preservation in all four functions
    F-6   handle or explicitly name trap qualifier 0x4

  BATCH 4 -- test debt (B-2), rides Batch 3
    Exhaustive AARM-pseudocode oracle: 23 byte-manipulation
    instructions x 8 alignment classes x 3 value patterns
    (0x0123456789ABCDEF, all-ones, 0x8000000000000001), CHECKed against
    a reference model that transcribes BYTE_ZAP and the shifted 16-bit
    masks literally.  Plus: the unaligned read idiom (EXTxL/EXTxH/OR)
    and write idiom (MSK/INS pair) round-trips at all 8 offsets, and
    the JRN-SCSI-020 regression PINNED BY NAME -- the pre-BWX signed
    byte load (LDQ_U; EXTQH rb=X+1; SRA #56) at X = 0..7 mod 8,
    asserting X = 7 yields the true byte and not NUL.  That last case
    is the test that would have caught the NOIOVEC root cause at commit
    time.

  BATCH 5 -- the residual risk (Sec 2)
    Audit PalEntries.cpp.  Two of the campaign's three root causes lived
    there.  Then FloatVariants.cpp's 225 bodies, then the MEM drainer.

--------------------------------------------------------------------------------
## 6. STANDING RULE PROPOSED FROM THIS AUDIT

  For CLAUDE.md, if it is not already stated:

    A leaf is not done when it handles its motivating case.  It is done
    when it has been diffed against the full architectural pseudocode,
    edge rows included, with the edges pinned in tests.  Every leaf is a
    standing claim to have transcribed the spec WHOLE.

    Fidelity obligations extend to every guest-observable surface --
    device models, bus behaviour, and delivered data included.
    "Scaffold" describes coverage scope, never a reduced correctness
    standard.

--------------------------------------------------------------------------------
## 7. FILES TOUCHED

  - this journal   NEW
  No emulator code changed.  No tests changed.  No build files changed.
