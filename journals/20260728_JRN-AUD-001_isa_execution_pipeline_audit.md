<!--
EmulatR V5 -- Implementation Journal JRN-AUD-001
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# JRN-AUD-001 -- COMPREHENSIVE ISA / DISPATCH / PIPELINE AUDIT (2026-07-28)
#                Dispatch, grain box leaves, execution pipeline, PAL
#                substrate, personality bifurcation.  Corrective actions
#                in brevity form; each landed fix cites its commit.

    Doc id   : JRN-AUD-001
    Date     : 2026-07-28
    Status   : AUDIT RECORD + CORRECTION LOG (living document today).
    Relates  : JRN-ISA-001 (2026-07-27 leaf audit -- this audit closes its
               Sec 2 coverage gaps), JRN-SCSI-033 (SWPCTX landing + CVTQG
               frontier), SPEC-SWPCTX-001 + GATE1.
    Authority: alpha_arch_ref.txt (AARM), 21264ev67_hrm.txt (EV6 HRM),
               DEC PAL sources (apisrm ev6_vms_*.mar).  AXPBox/SimH
               corroborative only.
    Method   : five parallel deep-read audits (FloatVariants+backend;
               PalEntries; coverage matrix; WB/MEM drainer; personality
               bifurcation) + the dispatch-seam reachability work that
               opened the day.  Finding IDs: FV-x (VAX FP), PE-x (PAL),
               WB-x (drainer), PB-x (personality), CM-x (coverage), DS-x
               (dispatch seam, found directly).

--------------------------------------------------------------------------------
## 0. EXECUTIVE SUMMARY

  Three FIDELITY-BLOCKING defect clusters found; the first two are FIXED
  and landed today, the third is the OS-era interrupt substrate and is
  the top remaining work item:

  1. DISPATCH SEAM (fixed: 307a8f5).  decode() had no FltVax case (all
     225 VAX-FP leaves unreachable -- the JRN-SCSI-033 frontier) and
     indexed ItFp 7-bit against a 2048-entry table (funcs >0x7F silently
     ALIASED onto _C forms).  Worse: buildCtx treated S_FpFormat like
     S_OpFormat for the literal path -- encoded[12] is FP function bit 7
     (rounding-qualifier high bit), so EVERY default-rounded FP operate
     (ADDT, MULT, CVTQT, SQRTT, CVTQG, ...) read a garbage 8-bit literal
     instead of F[Rb].  The pipeline had never correctly executed a
     default-rounded FP operate reading Fb; 9k existing assertions never
     saw it because leaf unit tests bypass buildCtx.  Found by the new
     reachability pins -- the first tests to drive FP through the full
     pipeline.
  2. VAX FP KERNELS (fixed: b469ed3).  Same-binade effective subtraction
     wrapped modulo 2^64 (SUBG 1.0-1.5 = +1.5); VAX conversions used
     IEEE ties-to-even where the AARM requires biased (away-from-zero)
     rounding; the underflow-enable qualifier bit was never decoded, so
     every VAX underflow went unrecorded.
  3. OS-ERA PAL SUBSTRATE (PE-1 fixed: 7368b35; PE-2..PE-5 OPEN).  The
     SWPCTX save set clobbered guest-maintained ESP/SSP/USP (fixed).
     Still open, one cluster: PCTX field decode scatters writes/reads
     across different cells (AST/FPE state black-holed), SIRR/software
     interrupts do not exist, AST interrupt composition absent, and the
     swap omits DTB_ASN installs.  OpenVMS scheduling stands on exactly
     these; they are the predicted post-banner walls.

  Standing rule confirmed AGAIN (6th+ instance): "every layer present,
  one seam unwired" -- and its new corollary: a leaf can be perfect and
  still never see a correct operand.  Pipeline-level reachability pins
  (encode -> decode -> operands -> leaf -> commit) are now part of the
  definition of done; they caught in one afternoon what 9k leaf-level
  assertions could not.

--------------------------------------------------------------------------------
## 1. CORRECTIVE ACTIONS TAKEN TODAY (brevity form)

  | ID    | Fix (file)                                          | Commit  |
  |-------|-----------------------------------------------------|---------|
  | DS-1  | decode(): ItFp + FltVax join FltIeee 11-bit group   | 307a8f5 |
  |       | (PipelineDriver.h)                                  |         |
  | DS-2  | buildCtx(): literal path gated on S_OpFormat ONLY   | 307a8f5 |
  |       | -- FP format has no literal form (AARM 3.3.4)       |         |
  | DS-3  | 4 reachability doctests: CVTQG frontier encoding,   | 307a8f5 |
  |       | SQRTT nearest-vs-chop discriminator, CVTQT control, |         |
  |       | FltVax bounds OPCDEC (test_pipelinedriver.cpp)      |         |
  | FV-1  | vax_float.h addsub: |a|>=|b| ordering includes      | b469ed3 |
  |       | equal-exp smaller-frac swap                         |         |
  | FV-2  | resolveRmVax: VAX normal rounding = near_maxMag     | b469ed3 |
  |       | for CVTGQ/CVTQF/CVTQG (AARM 4.7.6 biased)           |         |
  | FV-3  | FPVariant ctor sets .underflow for /U /SU /SUI      | b469ed3 |
  | FV-*  | 3 VAX pins (test_fp_backend.cpp)                    | b469ed3 |
  | PE-1  | writeHwpcbSaveSet: ESP/SSP/USP removed from save    | 7368b35 |
  |       | set (GATE-1 Q2: KSP+AST+CPC only); tests re-pinned  |         |
  |       | to the HWPCB-is-live-home model                     |         |
  | PE-2  | EV6 PCTX modeled as ONE register over live CpuState | pending |
  |       | homes: iprSelector maps raw 0x40-0x7F -> HW_PCTX,   |         |
  |       | composePctx (read, all fields) + applyPctxWrite     |         |
  |       | (index<4:0> field-select mask).  The EV5-era 32-cell |         |
  |       | palTemp scatter model and its PT round-trip tests   |         |
  |       | are RETIRED (4 PCTX pins replace them).             |         |
  | PE-3  | loadCpuFromHwpcb installs new ASN into dtbAsn0/1    | pending |
  |       | (fill-tag vs lookup-key desync at nonzero ASN)      |         |
  | CM-4  | Six /V integer overflow forms implemented (ADDL/V   | pending |
  |       | SUBL/V ADDQ/V SUBQ/V MULL/V MULQ/V) over the        |         |
  |       | existing alpha_int helpers; 13 pins.  DEVIATION     |         |
  |       | NAMED: no integer-overflow recording channel exists |         |
  |       | (no kFaultArith, no EXC_SUM state) -- leaves store   |         |
  |       | the exact AARM result, detect overflow, and trace    |         |
  |       | the undelivered trap; TODO(int-ov-trap) at three     |         |
  |       | sites.  Folding integer IOV into FPCR would be       |         |
  |       | architecturally wrong, so it was not done.           |         |
  | LAT-1 | LATENT BUILD HAZARD found en route (fixed): the     | pending |
  |       | execSwpctx_vms handwritten.tsv entry sat BELOW the  |         |
  |       | "generated distinct FP leaves" marker, and          |         |
  |       | gen_fp_leaves.py rewrites marker-to-EOF -- so ANY   |         |
  |       | regen silently dropped it and re-emitted a          |         |
  |       | conflicting stub (LNK2005).  Yesterday's SWPCTX     |         |
  |       | landing would have evaporated on the next codegen   |         |
  |       | run.  Entry restored above the marker with a dated  |         |
  |       | explanation.  THIS IS THE STUB-SHADOW CLASS'S       |         |
  |       | BUILD-SYSTEM TWIN -- the manifest is not append-    |         |
  |       | safe, and nothing warned.                           |         |

  Suite state after all fixes: 521 cases / 518 pass; the 3 failures are
  the pre-existing drift set (ide_wiring + 2x mmio_csc), untouched.

--------------------------------------------------------------------------------
## 2. OPEN FINDINGS -- ORDERED WORK QUEUE

  Severity legend: [B] = FIDELITY-BLOCKING on the OS path, [F] =
  fidelity defect (latent or off-path), [V] = verify, [D] = doc/design.

### 2.1 OS-era PAL substrate cluster (the predicted post-banner walls)

  PE-2 [B] EV6 PCTX decode wrong (PalEntries.cpp:177-195 + HW_IPR.h:
        244-288).  EV6 has NO PALtemp IPRs; raw 0x40-0x5F is PCTX with
        field-select bits (ASN 0x41, ASTER 0x42, ASTRR 0x44, PPCE 0x48,
        FPE 0x50, full 0x5F; reads legal 0x40-0x7F).  Current code maps
        each index to an independent cell -> MTPR_ASTEN writes cell 2,
        MFPR_ASTEN reads cell 31: ALL AST-enable/request/FPE updates via
        delegated CALL_PALs silently lost.  FIX: one cpu.pctx, masked
        field writes per index<4:0>, full-width reads, side effects into
        asn/aster/astrr/fpe.
  PE-4 [B] SIRR/ISUM.SI absent (PalEntries.cpp:2877,2385 no-ops;
        Machine.cpp stages EI only).  MTPR_SIRR-driven IPL 3/4 software
        interrupts are the heartbeat of VMS scheduling and I/O
        completion.  FIX: CpuState sirr field; compose ISUM.SI from
        SIRR & IER[SIEN] on SIRR writes and every IER/IER_CM write; stage
        INTERRUPT vector delivery.
  PE-5 [B] AST interrupt composition absent (depends PE-2 + PE-4):
        ISUM ASTK/E/S/U from PCTX[ASTRR&ASTER] & IER[ASTEN] & CM/IPL.
  PE-3 [B] SWPCTX omits DTB_ASN0/1 + PROCESS_CONTEXT installs (masked
        while all ASNs are 0; fatal at process creation).  FIX in leaf:
        install dtbAsn0/1 from new ASN + write composed PCTX (apisrm
        :249-285).
  PE-6 [F] CALL_PAL: no privilege check, no function-range OPCDEC
        (0x40-0x7F / >=0xC0 alias into valid vectors via &0x3F).
  PE-8 [F] HW_MFPR EXC_SUM reads 0 -- ARITH handler starved; reachable
        as soon as FEN-era FP traps fire.  Stage excSum at delivery.
  PE-7 [V] HW_REI bit-12 "STACKED" selector is not in the HRM (HW_RET
        target is always Rb; bit 12 is DISP).  Verify no PAL emission
        sets it; then remove the arm.
  PE-13 [F] CC_CTL write no-op (counter cannot be zeroed/gated).
  PE-12 [V] SWPCTX dispatch row still carries S_WritesRa|S_WritesInt
        (retired R0 contract).  Regenerate row; F-8 verdict (below)
        says the defaulted regWriteIdx makes it benign today.
  PE-9/10/11/14 [D] dead leaves (execBpt_vms/_tru64, execChmk_tru64,
        execSwpctxOsf), wrong func codes in two headers, SWPCTX
        misalignment deviation missing from the spec D-list, HW_CM
        layout note.

### 2.2 FP correctness (post-dispatch-fix, now-live territory)

  FV-4 [F] cmpG compares raw images as IEEE doubles: wrong for top-
        binade G (exp 0x7FF reads as Inf/NaN); missing reserved-operand
        INV.  FIX: compare via vax::unpack.
  FV-7 [F] CVTGQ/CVTTQ overflow stores saturated value; AARM requires
        low-order 64 bits (IOV flag already right).  Shared with the
        Float.cpp consolidated CVTTQ.
  FV-6 [F] CVTGF/CVTQF: no F-range (897..1151) OVF/UNF check; roundFreg
        hardcodes ties-to-even and ignores /C.  Route through rpack with
        vax::F geometry.
  FV-8 [F] dirty-zero policy inverted on both paths: default-mode must
        INV, /S must treat-as-zero; unpack does neither conditionally.
  FV-10 [F] CVTQL/V, /SV identical to plain CVTQL -- no IOV detection.
  FV-9 [F] FCMOVxx/FBxx LT/LE/GE/GT use host-double compare; AARM 4.10.3
        defines a pure sign/zero bit test (diverges on NaN patterns).
  FV-5 [F] CVTGD/CVTDG identity -- D-format (8-bit exp) != G (11-bit).
        Rare (legacy D-float apps); implement rebias+3-bit shift.
  FV-11 [F, deferred] no arithmetic trap DELIVERY (project-wide v1 cut).
        Unmaskable VAX traps (reserved operand, OVF, DZE) complete with
        sticky bits only.  Ride the trap-wiring work with PE-8.
  FV-12 [D] FpExec.h trap-decode drops /S (0x4) and mislabels 0x1 (/V on
        the CVT-integer ops); extends JRN-ISA-001 F-6.
  FV-13/14 [V] rpack OVF/UNF boundary vs AARM 4.7.6 thresholds
        (TODO(fp-vax-validate) still open); FPCR INE on VAX converts
        (AARM says Exceptions: None) -- EV6 HRM to arbitrate.
  FV-15 [V] CMPTUN SNaN must signal INV (shared with Float.cpp).

### 2.3 WB / MEM drainer

  F-8 SETTLED (JRN-ISA-001's top verify): regWriteIdx defaults to
        kNoRegWrite == 31 (the R31/F31 sink index); the commit gate is
        index-only (MemDrainer.h:209).  All three flag conventions are
        CORRECT today.  Codified convention: DEFAULT-IS-SENTINEL --
        no-write leaves leave regWriteIdx untouched; delete LoadStore's
        redundant explicit clears; state the contract in BoxResult.h.
  F-4 SETTLED: umulh correct on all shipping toolchains (WB-4: the
        portable #else fallback drops a carry -- fix the dead path);
        R31/F31 sink confirmed at the single gate.
  WB-1 [F] LDx R31/F31 prefetches + UNOP execute as real faultable
        loads; AARM: Exceptions: None.  Squash translation faults when
        !store && regWriteIdx==31.
  WB-2 [F] == JRN-ISA-001 F-5: LDS/STS drop the fraction when exp==0
        (denormal/bit-transport collapse to +-0).  Keep the frac term.
  WB-3 [F] LDF/STF word swap never performed (G does swap; F must
        mirror it).  Self-consistent round-trip masks it; misreads
        genuine VAX F data.
  WB-5 [D] BoxResult.h:89 says "WB skips the commit" -- commit lives at
        MEM.  Fold the F-8 convention statement into the same block.
  WB-6 [V, deferred] FP trap delivery unwired (== FV-11); applyToFpcr
        itself verified bit-exact vs AARM Fig 4-1.

### 2.4 Personality bifurcation (design + fidelity)

  PB-1 [F] palPersonality hardcoded =1 (VMS) in Machine::reset().
        Coincidentally faithful (shipped SRM PAL is VMS-personality).
  PB-2 [F] execSwppal never updates palPersonality -- the architectural
        bifurcation point (AARM 27.3.2.1, R16 variant) is a no-op for
        the host tables.
  PB-4 [B-if-Tru64] Tru64 0x3F binds execMfprWhami (host intrinsic);
        under OSF 0x3F is rti, the hottest kernel return.  Split the
        colliding TSV rows (PB-3 list: 0x2B/0x2E/0x30/0x32/0x3F/0x83/
        0x92).  Unreachable while PB-1 pins VMS.
  PB-5 [D] DESIGN ACCEPTED-INTENT: config key in [ROM]/RomSettings
        (palPersonality = vms|tru64|linux|auto, default vms) -- it is a
        property of the loaded firmware/PAL image, NOT the platform
        JSON (hardware manifest, OS-agnostic per SSOT doctrine).
        Runtime: SWPPAL updates the field from R16 (map HWRPB variant
        1=VMS 2=Tru64 -- note EmulatR's enum is 0=Tru64/1=VMS/2=Linux,
        PB-7 mapping required).  Snapshot already round-trips the field.
  PB-6 [D] S_PalLinux codegen extension confirmed ~15 lines, gated on
        the PB-3 row split; palPersonality==2 currently falls to the
        TRU64 table silently.
  PB-8 [V] ExecCtx.palPersonality dead (never populated, no consumers)
        -- populate in buildCtx or delete.
  PB-9 [V] both PAL tables' default arm diverts any unlisted func with
        &0x3F masking == PE-6 (same fix site).

### 2.5 Coverage matrix

  (Section reserved -- the full 0x00-0x3F primary/sub-table matrix vs
  AARM Appendix C is being generated and lands as Sec 5 of this
  document.  Known-ahead items: FTOIS 0x1C/0x78 absent from the TSV
  entirely -- wire or OPCDEC-by-record; reachability sweep of every
  DispatchKind is green after DS-1/DS-2.)

--------------------------------------------------------------------------------
## 3. TASK LIST RECONCILIATION

  Against JRN-SCSI-033 Sec 5/5b (standing owed):
    A. FltVax dispatch fix + ItFp + reachability pins + boot retest --
       DONE (307a8f5; retest in flight at writing).
    B. DS10 + ES40 P00>>> tri-platform gate -- STILL OWED, now covers
       GH fix + C1 + C3 + today's five commits.
    C. FTOIS -- OPEN (CM section; wire or record).
    D. Track B kickoff (PalEntries context/MM/IPL read + reachability
       sweep) -- the PalEntries read is DONE (PE-1..PE-14 supersede the
       vague "Track B row"); the sweep is DONE for DispatchKind level.
    - Q3 TBIAP->no-invalidate flip -- unchanged, waits for OS-era
      stability.
    - MTPR_FEN HWPCB write-through -- DISCHARGED by audit: delegation
      to guest PAL performs the hw_stl/p PCB__FEN write (PE table row
      0x0B/0x0C); the owed item closes with no code change.
  Against JRN-ISA-001 Sec 5 batches:
    Batch 1 (verifies) -- F-8, F-4, F-12 all SETTLED by today's audit
      (F-12: no divergent duplicates; 0x0A5 is CMPGEQ under 0x15 and
      CMPTEQ under 0x16 -- different opcodes, single backend).  F-10
      (HW_LD type-mode under VMS PAL) remains OPEN [V].
    Batch 2 (doc fixes) -- F-2 (RPCC probe deletion) verified already
      done by C1; F-3/F-7/F-9/F-11/B-1 remain OPEN [D].
    Batch 3 -- F-1 landed (C1, both sites verified); F-5 == WB-2 OPEN;
      F-6 == FV-12 extended, OPEN.
    Batch 4 (byte-ops oracle test debt) -- OPEN, unchanged.
    Batch 5 (residual risk) -- PalEntries + FloatVariants + drainer all
      READ today; findings above replace the placeholder.

  NEW priority queue (recommended execution order):
    1. PE-2 + PE-4 + PE-5 + PE-3 as one "OS scheduling substrate" spec
       (SPEC-PCTX-SIRR-001), gates + doctests, since OpenVMS login-era
       execution dies without them.
    2. PE-8 + FV-11/WB-6 arithmetic-trap delivery wiring (one seam).
    3. FV-4/6/7/8/10 VAX backend correctness batch (doctests per fix).
    4. WB-1/WB-2/WB-3 drainer batch.
    5. PE-6 privilege/range OPCDEC + PB-3/PB-4 TSV row split (+PB-1/
       PB-2/PB-5 config + SWPPAL personality plumbing).
    6. FV-9 bit-test predicates; FV-5 D-float; doc batch (PE-9/10/11/
       14, WB-5, FV-12, F-3/F-7/F-9/F-11/B-1).
    7. Tri-platform gate rides after each behaviour batch (owed now).

  CODEGEN HARDENING (new, from CM-1 + LAT-1 -- do these together, they
  are the same failure shape one layer down):
    - genGrains.py must die() on a TSV row whose subDecode exceeds its
      table size instead of silently dropping it (CM-1's payload: two
      real leaves vanished and the runtime mask aliased them onto a
      DIFFERENT instruction).  Fix the false ":157 only values <0x80
      used in 21264" comment in the same change (CM-8).
    - gen_fp_leaves.py rewrites handwritten.tsv from its marker to EOF.
      Any hand-added row below the marker is silently destroyed on the
      next regen (LAT-1 -- it ate the SWPCTX entry).  Either make the
      generator preserve unknown rows, or emit its block to a SEPARATE
      generated file so the hand-written manifest is never rewritten.
      A manifest that eats hand edits is the codegen twin of the
      stub-shadow class this campaign keeps paying for.

--------------------------------------------------------------------------------
## 4. WHAT PASSED TODAY (do not re-walk)

  - Dispatch: every other DispatchKind case + sub-decode width correct;
    JmpClass/Misc/Pal routing verified; CALL_PAL vector math locked by
    static_asserts (Ev6EntryVectors.h).
  - SWPCTX C1-C3 contract vs GATE-1: CC packed model both sites, save-
    first ordering, pcbb==0 guard, PT-cell mirrors at verified offsets,
    TBIAP realm policy, D1-D4 respected (deviation PE-11 excepted).
  - handwritten.tsv bijection: no LIVE stub-shadowing anywhere in
    palBox (the LDQP/SWPCTX class is clean today; 4 dead leaves noted).
  - MemDrainer: commit gate, R31/F31 sink, load extension table, store
    truncation (Tsunami path), STx_C reservation pair, LDG/STG swap.
  - FPCR: applyToFpcr layout bit-exact vs AARM Fig 4-1/Table 4-11.
  - VAX kernels: mul/div/sqrt algebra, CVTLQ/CVTQL repositioning, FBxx
    displacement math, CMPGxx 0.5-image convention, F/G geometry
    windows, qualifier plumbing structure (each leaf passes its own
    encoding; _C genuinely chops).
  - Personality: dispatch selection mechanics, genuinely-shared CALL_PAL
    codes, VMS-only/Tru64-only row partitions, snapshot round-trip.

--------------------------------------------------------------------------------
## 5. COVERAGE MATRIX (completed sweep vs AARM Appendix C.6/C.7/C.8)

  Summary verdict: dense coverage is EXCELLENT -- zero phantom encodings
  anywhere; fully populated vs AARM: 0x08-0x0F, 0x11, 0x12, 0x14 (51),
  0x15 (106), 0x16 (186), 0x18 (12), 0x1A, 0x20-0x3F, all HW_ formats;
  0x01-0x07 correctly reserved-OPCDEC.  GrainStubs has ZERO live stubs;
  the LDQP/SWPCTX name-mismatch class is fully retired (482/482 leaves
  bound); 7 orphans (2 = the CM-1 payload, 5 benign/dead).  All 19
  DispatchKinds have switch cases post-DS-1.

  GAPS (the complete list):
  | ID   | Gap | Class |
  |------|-----|-------|
  | CM-1 | CVTQL/V 0x130, CVTQL/SV 0x530 (opcode 0x17): TSV rows +   |
  |      | leaves EXIST but codegen silently drops subDecode >= 128  |
  |      | and the 7-bit runtime mask ALIASES both onto plain CVTQL  |
  |      | (IOV lost, no fault).  FltLogical is architecturally an   |
  |      | 11-bit field.  FIX: 2048-entry table + 11-bit switch case |
  |      | + codegen die() on out-of-range subDecode (CM-8: fix the  |
  |      | false "only values <0x80 used" comment).  | BLOCKING      |
  | CM-2 | == PB-4 (Tru64 0x3F rti hijacked by execMfprWhami).       |
  | CM-3 | FTOIS 1C.78 absent entirely while AMASK advertises FIX -- |
  |      | AMASK-probing RTLs will emit it.  Wire it (FTOIT pattern).|
  | CM-4 | ADDL/V SUBL/V ADDQ/V SUBQ/V (0x10) + MULL/V MULQ/V (0x13) |
  |      | absent -- base-arch /V forms, VMS compiler-emitted.  Fit  |
  |      | existing 128-entry tables.  (Fix in flight.)              |
  | CM-5 | execWtint returns immediately -- busy-spins OS idle loop  |
  |      | (perf, not correctness).  [VERIFY]                        |
  | CM-6 | PAL lookup misses (VMS 0x31/0x33, Tru64 0x13/0x14/0x81)   |
  |      | fall to the generic divert into real PALcode -- faithful. |
  |      | ACCEPTED-GAP.                                             |
  | CM-7 | Reserved opcodes OPCDEC per AARM C.14.  ACCEPTED-CORRECT. |

  AARM erratum note: Table C-10's prose column misprints MTPR_FEN/
  MTPR_IPL codes (duplicates the MFPR codes); the numerical table at
  :47869+ and the real PAL agree with EmulatR's 0x07/0x08/0x0C/0x0F.

--------------------------------------------------------------------------------
## 5b. SESSION ADDENDUM -- first OS-era retest + INVEXCEPTN frontier

  Retest (post 307a8f5/b469ed3/7368b35, DS20 b dka0): SYSBOOT ran,
  banner printed, exec ran ~133M cycles past the banner (cyc 2.115e9 ->
  2.249e9), then:
      **** OpenVMS ... BUGCHECK **** 000001CC INVEXCEPTN,
      Exception while above ASTDEL; no current process (EXE$INIT era);
      compressed selective dump written via the SCSI stack.
  FARTHEST-EVER by a wide margin -- the exec's crash machinery itself
  (bugcheck formatter, dump writer, console CSERVE) all executed.
  Evidence so far: zero emulator-delivered non-TB faults in the window
  (14k routine DtbMissDouble, all resolved); the underlying exception
  is GUEST-SYNTHESIZED (PAL walk -> ACV shape) or interrupt-delivery
  anomaly -- consistent with the PE-2/PE-4 substrate convictions.  The
  dump banner quadword 0x00A4F1EEC5960000 is NOT corruption: it is VMS
  system time = exactly 2006-01-01 00:00:00 (default TOY epoch).
  Instrumentation staged for the next run: LOOKBACK_SIZE 64 -> 256,
  ring-dump trigger EMULATR_VALUE_GATE=0x1CC (the bugcheck code the
  raise path must load) + EMULATR_VALUE_GATE_FLOOR past the banner +
  EMULATR_TRACE_WINDOW=1 (sink constructed, mask 0).  Wrapper:
  tools/run_ds20_invexceptn_trace.sh (gate is ONE-SHOT -- verify the
  dump's cycle against the banner's before trusting it).

  READING OF THE EVIDENCE (important, and it reframes the hunt):
  "no emulator-delivered fault" does NOT mean "no exception".  The
  guest VMS PAL posts ACV/TNV to the SCB from its OWN page-table-walk
  conclusions -- those never appear in logs/faults.log, which records
  only EmulatR-raised kFault* codes.  So the INVEXCEPTN is most likely
  a PAL-posted memory-management exception whose cause is a walk that
  read a wrong PTE.  That places TODAY'S PE-2 and PE-3 fixes directly
  in the causal path (PCTX ASN was being lost on every delegated
  MFPR_ASN, and DTB fills were tagged with a stale ASN), alongside the
  still-open PE-4/PE-5 interrupt substrate.  The next run is therefore
  a genuine retest, not just an instrumented repeat: three of the four
  named suspects have moved since the run that produced this banner.

--------------------------------------------------------------------------------
## 5c. STANDING RULES PROPOSED FROM THIS AUDIT

  R1. PIPELINE-LEVEL REACHABILITY IS PART OF DONE.  A leaf can be
      perfect and still never see a correct operand.  9,000+ leaf-level
      assertions never caught that EVERY default-rounded FP operate read
      a garbage literal instead of F[Rb] (DS-2), because leaf unit tests
      call the leaf directly and bypass buildCtx.  For every DispatchKind
      and every operand-resolution path, at least one test must drive a
      real encoding through the WHOLE pipeline: encode -> decode ->
      operand resolve -> leaf -> commit.  The four pins added in 307a8f5
      are the template.

  R2. A HALF-WIRED IPR IS A LIVENESS CHANGE, NOT JUST A FIDELITY ONE.
      When a register currently reads as a silent zero, making it
      truthful is safe ONLY IF every consumer of the truthful value also
      exists.  Concrete case (SPEC-SIRR-AST-001 Sec 2): giving SIRR a
      real backing store without wiring delivery turns "poll SISR until
      the ISR clears it" from a wrong-but-progressing no-op into an
      infinite spin -- a NEW hang at a LATER point that reads as a
      regression caused by the fix.  State and its consumer land in one
      commit, or neither does.

  R3. THE MANIFEST IS NOT APPEND-SAFE, AND NOTHING WARNS.  Two separate
      instances today (LAT-1: gen_fp_leaves.py rewrites handwritten.tsv
      marker-to-EOF and ate the SWPCTX entry; CM-1: genGrains silently
      drops TSV rows whose subDecode exceeds the table size, and the
      runtime mask then ALIASES them onto a different instruction).  A
      third, self-inflicted: a bare-LF line inserted into a CRLF TSV
      merged two rows and the FTOIS row vanished with no diagnostic.
      Every codegen input needs a loud failure mode; see the CODEGEN
      HARDENING items in Sec 3.

--------------------------------------------------------------------------------
## 6. FILES TOUCHED TODAY

  Commits 307a8f5, b469ed3, 7368b35 (see Sec 1 table).  This journal:
  NEW.  memory.md index update rides the closing commit.
