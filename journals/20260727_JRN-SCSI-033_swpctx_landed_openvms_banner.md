<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-033
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-033 -- SWPCTX LANDED (SPEC-SWPCTX-001 C1-C4).  For the first
#                 time, EmulatR printed:
#                     OpenVMS (TM) Alpha Operating System, Version V8.3
#                 The new frontier is an OPCDEC at S0 exec PC
#                 0xFFFFFFFF8009D0EC -- a PREDICTED gate (A1/A2 family).

    Doc id   : JRN-SCSI-033
    Date     : 2026-07-27 (same evening as JRN-SCSI-032)
    Status   : LANDING RECORD (C4 of SPEC-SWPCTX-001).
    Relates  : SPEC-SWPCTX-001 + GATE1_ANSWERS (design record),
               JRN-SCSI-032 (GH aliasing fix that exposed this wall),
               JRN-ISA-001 F-1 (C1), JRN-SCSI-026 (PT-cell desync class).
    Commits  : 9608d90 C1 (F-1 packed CC), 301685d C2 (HWPCB guest I/O),
               ac2342e C3 (the leaf).  All pushed to origin/v5-tb.

--------------------------------------------------------------------------------
## 1. The wall's true nature (recorded for the failure-class ledger)

  The "Illegal inst" wall at VA 0x2F09C was NOT a missing implementation.
  A complete SWPCTX leaf existed in PalEntries.cpp as execSwpctxVms --
  SimH-informed, spec'd (palBoxLib/swpctx_spec.md refs in-body) -- but was
  NEVER DISPATCHED: the camelCase name missed handwritten.tsv, codegen
  emitted the kFaultUnimplemented stub, and DispatchTables silently bound
  the stub.  This is the LDQP failure class (PalEntries.cpp 2026-06-05
  note), second confirmed instance.  LEDGER RULE STRENGTHENED: when a
  CALL_PAL OPCDECs, check handwritten.tsv NAME MATCH before assuming a
  missing feature -- the leaf may exist and be shadowed by its own stub.

## 2. What landed (C1-C3, each its own reviewed commit)

  C1 9608d90: F-1 packed CC (RPCC/MFPR packed, MTPR upper-half-only,
     ccOffset = the OFFSET FIELD, kCcMultiplier counter-scoped, TEMP
     RPCC probe deleted per F-2, HwpcbContext migrated to apisrm CPC
     arithmetic).  DEC's own PAL source contains the packed model
     verbatim (ev6_vms_callpal.mar:399-411) -- SWPCTX's CC swap is
     built on it.
  C2 301685d: HWPCB guest-physical I/O (readHwpcbFromGuest full-image;
     writeHwpcbSaveSet = THE field-set policy: 4 SPs + AST + CPC
     longword, nothing else), CpuState pme/dat homes, FEN-quad packing
     FEN<0>|PME<62>|DAT<63>.
  C3 ac2342e: the leaf, upgraded to the GATE-1 contract -- alignment
     ILLOP-shape fault, save-first ordering, pcbb==0 first-swap guard,
     PAL-temp MEMORY mirrors (PT__PTBR = PFN<<13 and PT__PCBB at
     p_temp+0x8/+0x10 -- the guest TB-miss handlers read the CELLS, not
     CpuState; JRN-SCSI-026 desync class), TBIAP both realms (GATE-1
     Q3(b), ASM survives), no R0 output, D1-D4 deviations named in the
     leaf header.  Tests T1-T6 (45 asserts).  Suite 511/514 (the 3
     pre-existing drift failures).

## 3. Retest evidence (C4)

  DS20 `b dka0.0.0.8.0 -flags 0`, run emulatr_c3_retest2.log:
    - SWPCTX-DIAG fired EXACTLY ONCE: pc=0x2f09c cyc=1.9939e9
      newPcbb=0x1414080 (the same R16 the old failing run staged --
      JRN-SCSI-032 Sec 6), ptbr(PFN)=0x1ff82 (= boot table 0x3ff04000),
      asn=0, ksp=0xffffffff83658000 (S0).
    - SYSBOOT completed its entire load sequence and the OS took the
      console:  "OpenVMS (TM) Alpha Operating System, Version V8.3
      (c) Copyright 1976-2006 Hewlett-Packard Development Company, L.P."
      FIRST TIME EVER on EmulatR.
    - Execution continued AT S0 SYSTEM-SPACE PCs (final PC
      0xffffffff8368a660) -- the executive running in mapped kernel
      virtual space, ~128M cycles past the swap.

## 4. NEW FRONTIER (next hunt, one instruction to decode)

  VMS early-exec exception banner:
      * Exception taken befo[re ...]  /  * Unable to take c[...]
      Illegal inst
      Exception PC = FFFFFFFF.8009D0EC   Exception PS = 10000000.00001F00
  -> OPCDEC at an S0 executive PC, cyc ~2.12e9, then CALL_PAL HALT at
  0xffffffff8368a660 (HaltedClean).  This is the PREDICTED A1/A2-era
  gate (SPEC Sec 9).

  DECODED SAME EVENING, ZERO RUNS (the fault log already carried it --
  logs/faults.log: cyc 2113273477 pc 0xffffffff8009d0e8 encoded
  0x57e017c1 op 0x15 kFaultOpcDec):

    0x57e017c1 = CVTQG f0, f1   (opcode 0x15, func 0x0BE -- VAX
                 convert quadword -> G_floating; VMS-heritage time
                 arithmetic territory)

  ROOT CAUSE OF THE OPCDEC (static, complete):
    - The leaf EXISTS and is REAL: fBoxLib/grains/FloatVariants.cpp
      :1366 execCvtqg -> fpBox::activeBackend().cvtQG(...).
    - The dispatch entry EXISTS: g_fltVaxSubTable[0x0be]
      (DispatchTables.cpp:2841), and the primary table routes 0x15 ->
      FltVax (:7645).
    - The DECODE ENGINE'S DispatchKind switch HAS NO case for
      DispatchKind::FltVax (pipelineLib/PipelineDriver.h ~:1097-1140:
      7-bit group, FltIeee 11-bit, JmpClass, Misc, Pal -- FltVax
      absent) -> falls through -> kOpcDecEntry.  ALL 225 VAX-FP leaves
      are wired-but-unreachable.  The fix is ONE case label joining
      the FltIeee 11-bit group.  NOT FIXED TONIGHT: outside the
      SPEC-SWPCTX-001 drift fence -- reported per the fence rule.
    - ADJACENT FINDING (same defect family, same switch): ItFp
      (opcode 0x14) sits in the 7-BIT sub-decode group but
      g_itFpSubTable has 2048 entries -- any 0x14 function above 0x7F
      (SQRT qualifier forms, e.g. SQRTT/SUI 0x7AB) OPCDECs the same
      way.  Verify + fix together with FltVax.
    - Failure-class note: fourth instance tonight of "every layer
      present, one seam unwired" (stub-shadowed leaf, name-mismatch
      manifest, now a missing switch case).  Track B should add a
      REACHABILITY sweep: for every DispatchKind and sub-table, one
      test that a representative encoding actually reaches its leaf.

## 5. TOMORROW'S ACTIONABLES (in order), then standing owed items

  A. FltVax dispatch: add `case DispatchKind::FltVax:` to the FltIeee
     11-bit group in PipelineDriver.h (~:1110); same commit resolves
     the ItFp 7-bit/2048-entry question (verify 0x14 func widths
     against the TSV first); reachability doctests for both kinds
     (representative encodings reach their leaves -- CVTQG 0x57e017c1
     itself is the natural pin); then the boot retest.  EXPECTATION:
     the exec proceeds past 0x8009d0e8 into FloatVariants territory
     whose 225 bodies are UNAUDITED (JRN-ISA-001) -- the backend cvtQG
     result becomes live guest state, so the FIRST FP-shaped
     divergence after this fix is a FloatVariants-fidelity suspect,
     not a dispatch suspect.
  B. DS10 + ES40 P00>>> boots (the standing tri-platform gate debt --
     now covering the GH fix, C1, and C3 together).
  C. FTOIS: wire or explicitly OPCDEC-by-record (it is architecturally
     real, currently absent from the TSV entirely).
  D. Track B kickoff: PalEntries context/MM/IPL read + the
     reachability sweep from Sec 4.

## 5b. Standing owed / follow-ups

  - G3 tri-platform: DS10 + ES40 P00>>> boots (still owed from
    JRN-SCSI-032 Sec 6; fold into the next session's first gate run).
  - Q3 ground-truth follow-up: switch TBIAP-on-swap to the apisrm
    no-invalidate once the OS era stabilizes (one line + T3 flip).
  - MTPR_FEN/CLRFEN HWPCB write-through (AARM 17679) -- verify the
    leaves update the HWPCB image; Track B row.
  - execSwpctxOsf (0x30) remains a veto; Tru64 personality fan-out
    rides X2.  The old execSwpctxVms 9-quad save/R0 behavior is
    retired; Track B should NOT resurrect it.
  - Instrument note: the srm_console_driver cannot see stderr DIAG
    lines; --expect on them never fires (cost one dead driver run).

## 6. Files touched

  See commits 9608d90 / 301685d / ac2342e (this journal rides ac2342e's
  successor commit with the memory.md index update).
