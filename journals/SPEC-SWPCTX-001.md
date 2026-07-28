<!--
EmulatR V5 -- Design Brief SPEC-SWPCTX-001
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.  Design-first task: NO source edits land before
the Section 10 sign-off gate is cleared.
-->

# SPEC-SWPCTX-001 -- Faithful VMS SWPCTX (CALL_PAL 0x05), with the RPCC/CC
#                    packed-format fix (JRN-ISA-001 F-1) sequenced ahead of it

    Doc id   : SPEC-SWPCTX-001
    Date     : 2026-07-27 (drafted end of evening session)
    Status   : DRAFT for architect review, then handoff to Cowork.
    Relates  : JRN-SCSI-032 (GH compose root cause + fix; supersession of 031),
               JRN-ISA-001 (leaf fidelity matrix; F-1 RPCC packed format,
               F-10 HW_LD VPTE modes, FTOIS [CONFIRM]),
               gap-audit inventory (Cowork message, 2026-07-27 late session),
               emulatr_ghfix_retest.log (run_ds20_showdev 20260727_195403).
    Authority: (a) Alpha AARM, OpenVMS PALcode chapter -- the CONTRACT.
               (b) apisrm PALcode source, SWPCTX flows -- the GROUND TRUTH.
               Where (a) and (b) differ in detail: apisrm wins for behavioral
               fidelity; the AARM wins for what is guaranteed vs. incidental.
               Cite section/line for every design decision (house rule:
               primary-source verification, no memory-cited offsets).

--------------------------------------------------------------------------------
## 1. The wall (evidence, one paragraph)

  Post GH-compose fix, the DS20 boot cleared %SYSBOOT-F-LDFAIL and ran to
  cyc ~3.4608e9 -- through the console era, image load, relocation, and the
  bootstrap handoff -- before SYSBOOT issued its first privileged context
  swap: CALL_PAL 0x05 (SWPCTX) at VA 0x2F09C (pushed Exception PC 0x2F0A0
  = CALL_PAL PC+4).  DispatchTables.cpp:7265 routes it to execSwpctx_vms,
  which is the auto-generated stub returning kFaultUnimplemented; PALcode
  dispatches that to the guest as OPCDEC through SCB vector 0x420; SYSBOOT's
  pre-SCB handler prints the banner and HALTs.  The stub's own TSV row
  pre-names the prerequisites: "needs CpuState shadow regs + leaf-side
  memory accessor."  This is the LAST generated stub in the grain dispatch;
  the VMS privileged roster is otherwise fully wired (115 entries).

  Campaign lesson that shapes this brief: EXTxH, MTPR_VPTB, and the GH
  compose were all IMPLEMENTED code whose model was narrower than the
  architecture.  The deliverable here is not "make SWPCTX not stub"; it is
  the COMPLETE architectural surface of the swap, so the implementation
  cannot be a correct model of a wrong scope.

--------------------------------------------------------------------------------
## 2. Scope

  IN:
    S1. JRN-ISA-001 F-1: correct RPCC packed format (offset<63:32> |
        counter<31:0>) and the CC/CC_CTL model it implies.  OWN COMMIT,
        lands FIRST (Sec 5).
    S2. Faithful VMS SWPCTX leaf: full HWPCB save/restore, PCBB update,
        ASN/PTBR/TB semantics, CC swap, FEN/PME/AST state (Sec 4).
    S3. Prerequisite infrastructure named by the TSV row: CpuState shadow
        registers (if not already present for the 115 wired leaves) and the
        leaf-side PHYSICAL memory accessor (Sec 4.2).
    S4. Pinning doctests incl. the GH-span invalidation sibling test
        (Sec 7) and the do-no-harm gate (Sec 8).
    S5. F-10 (HW_LD VPTE modes) REVALIDATION in the same neighborhood --
        re-run its check against the post-SWPCTX world; fix only if the
        revalidation fails and the fix is local.

  OUT (explicitly fenced):
    X1. ASN-match TB semantics as a performance path (deferred lever,
        own gated task; Sec 4.4 records the decision).
    X2. BPT (both personalities) and MFPR_VIRBND -- ride behind, unchanged.
    X3. Policy hard-stops on unknown IPR selectors / reserved 0x2D --
        correct per the hard-stop-over-silent-degradation rule; DO NOT
        soften while in the neighborhood.
    X4. Any other PalEntries leaf.  The Track B audit reads them; this
        task does not touch them.
    X5. FloatVariants / FBOX leaves.  FEN adjacency is NAMED (Sec 9), not
        acted on.

--------------------------------------------------------------------------------
## 3. Design questions the brief must answer BEFORE code (architect gate)

  Q1. HWPCB field map: complete field/offset/width table transcribed from
      the AARM HWPCB figure, with the apisrm structure offsets cross-
      checked against it.  EVERY field, including the tempting-to-skip
      set: ASTEN, ASTSR, FEN, PME, DAT-era scratch, CC, per-process
      unique value if present.  No offset enters the code from memory;
      each row carries its AARM figure/table citation.
  Q2. Save/load ORDERING as an explicit sequence (Sec 4.3), including
      what is architecturally visible if the sequence is interrupted
      (it must not be: SWPCTX executes atomically from the guest's view).
  Q3. ASN/TB decision (Sec 4.4) with AARM citation.
  Q4. CC swap semantics against the F-1 model (Sec 4.5).
  Q5. Which IPRs the leaf touches directly vs. which live in CpuState
      shadow (per the TSV prerequisite), and where PCBB itself lives.
  Q6. Interaction with the TB/ComJIT tiers: what (if anything) above the
      SPAM TB must be invalidated on PTBR/ASN change (Sec 4.6).

--------------------------------------------------------------------------------
## 4. Design constraints (the contract, from tonight's review)

  4.1  Register interface.  R16 carries the PHYSICAL address of the new
       HWPCB (the value SWPCTX loads into PCBB).  Old context saves
       through the CURRENT PCBB before any new-context load.  Return
       value/clobbers per AARM.  [VERIFY exact register contract vs
       AARM -- do not trust this paragraph, cite it.]

  4.2  Physical access discipline.  ALL HWPCB reads and writes go through
       the leaf-side PHYSICAL accessor.  They never consult the DTB, never
       take a DTB miss, never touch the SPAM TB.  This is the classic
       stub-replacement failure point and the TSV row's own named
       prerequisite.  If the accessor does not exist yet, it is S3
       infrastructure and its own reviewable commit BEFORE the leaf.

  4.3  Ordering (to be finalized against apisrm in Q2, but the shape):
         (1) read old PCBB;
         (2) SAVE outgoing state into old HWPCB (stack pointers for all
             four modes -- current mode's SP from the live SP, others
             from shadow; CC per 4.5; ASTEN/ASTSR; FEN; PME);
         (3) load ALL incoming fields from new HWPCB;
         (4) PCBB <- R16;
         (5) install PTBR, ASN;
         (6) TB actions per 4.4;
         (7) install stack pointers, FEN, AST state, CC.
       The leaf is a single retired grain: no guest-visible intermediate
       state, no interrupt window inside the swap.

  4.4  ASN / TB decision -- DECIDED, pending AARM citation to record:
       CONSERVATIVE FIRST.  On swap: TBIAP (invalidate all non-ASM
       process entries).  Rationale: architecturally safe under every
       reading; removes stale-translation risk from the exact window
       where SYSBOOT's first MM-enabled execution will be debugged;
       costs performance only.  MANDATORY pre-check: determine whether
       SPAM lookup currently ENFORCES ASN match or stores-but-ignores
       ASN tags.  If ignored, TBIAP-on-swap is not merely safe, it is
       the ONLY correct choice, and the brief records that ASN-match is
       a future task that must add enforcement before it adds retention.
       ASM-bit pages survive TBIAP per architecture -- verify the SPAM
       invalidation path honors ASM.

  4.5  CC (cycle counter) -- F-1 FIRST.  The HWPCB CC field only has
       coherent semantics against the correctly packed RPCC model
       (offset<63:32> | counter<31:0>).  Sequencing: F-1 lands as its
       own small commit with its own pinned tests; the SWPCTX commit
       then CONSUMES that model for CC save/load.  Two reviewable diffs,
       one correct CC model.  [Record here the AARM statement of what
       SWPCTX does with CC -- save-and-load vs. offset recompute.]

  4.6  PTBR swap invalidation, including the GH lesson.  Whatever 4.4
       decides, the invalidation executed on swap MUST treat GH>0
       entries as covering their FULL block span (a GH=3 entry owns
       4MB, not 8KB).  The GH compose fix corrected the HIT path;
       SWPCTX is the first operation to stress the INVALIDATION path
       against GH entries under a live guest.  The sibling doctest
       (Sec 7, T4) is in-scope for THIS task, not deferred.  Also
       answer Q6: TB-tier (decode-amortization) and any ComJIT blocks
       keyed on process-virtual state -- what does a PTBR/ASN change
       invalidate above the SPAM layer?  (This is the recorded owed
       item "TB-tier invalidation constraint" -- it comes due here.)

--------------------------------------------------------------------------------
## 5. Sequencing (each step its own reviewable commit)

  C1. F-1: RPCC packed format + CC model + pinned tests.        [small]
  C2. Infrastructure (only what is missing): leaf-side physical
      accessor and/or CpuState shadow regs, with unit tests.    [small]
  C3. SWPCTX leaf per Sec 4, replacing the generated stub, with
      the Sec 7 doctests.                                       [main]
  C4. Boot retest + Sec 8 gate evidence, JRN entry recording
      results and the next gate.                                [run]

--------------------------------------------------------------------------------
## 6. Instrumentation for the retest (aim the probes where the boot IS)

  Window-edge lesson from tonight applies (three misses this campaign):
    - EMULATR_FAULT_CYCHI: extend to 4,000,000,000 minimum so the fault
      log covers the SWPCTX era and whatever follows it.
    - EMULATR_DIAG_PCLO/PCHI: re-aim one run at 0x2F000..0x2F100 to
      capture SWPCTX's own execution and retirement; keep a second
      profile aimed wide-open past it for the NEXT gate.
    - Success predicate visible in state: post-swap PTBR != 0, PCBB ==
      the R16 value from the faulting-era call, boot marches past
      VA 0x2F09C.

--------------------------------------------------------------------------------
## 7. Pinning doctests (minimum set; add per apisrm findings)

  T1. Round-trip: swap A->B->A restores every HWPCB field of A exactly
      (all four SPs, PTBR, ASN, ASTEN/ASTSR, FEN, PME, CC).
  T2. Physical-access: HWPCB pages intentionally UNMAPPED in the DTB;
      swap must succeed (proves 4.2) and must not allocate TB entries.
  T3. TB semantics: pre-swap process-private translation is NOT visible
      post-swap (TBIAP happened); ASM-marked translation IS still
      visible (ASM honored).
  T4. GH-span invalidation: install a GH=3 entry; swap; assert the
      ENTIRE 4MB reach is gone -- probe a non-base page of the block
      (the cross-page compose lesson, applied to invalidation).
  T5. CC continuity: CC saved on swap-out equals CC loaded on swap-in
      per the F-1 packed model; RPCC read after swap reflects the new
      context's offset.  [Exact assertion per the Q4/4.5 AARM answer.]
  T6. Ordering/atomicity: no guest-visible intermediate state (assert
      via single-grain retirement; no fault or interrupt delivery
      window inside the swap).

--------------------------------------------------------------------------------
## 8. Do-no-harm gate (evidence before merge)

  G1. Full existing suite green.
  G2. DS20 SRM cold boot to P00>>> unchanged (console never swaps
      context; any console-era delta is a regression by definition).
  G3. DS10 and ES40 P00>>> boots -- the OWED regression runs from
      tonight's ledger fold in here rather than as a separate errand.
  G4. The retest run per Sec 6, logged to {run-dir}/logs and traces to
      {run-dir}/traces, house naming.

--------------------------------------------------------------------------------
## 9. Named adjacencies (predicted, not actioned)

  A1. FEN: the first FEN=1 swap makes the FloatVariants gap REACHABLE.
      If a post-SWPCTX trap arrives FP-shaped, it is a PREDICTED gate
      (JRN-ISA-001 matrix), not a mystery.  No FBOX work in this task.
  A2. FTOIS [CONFIRM]: five-minute static dispatch-table check -- do it
      before or alongside C1 so the question mark does not ride into
      the era where it can fire.
  A3. The "at least 9 vetoes" -> current inventory reconciliation: map
      each of the original nine grep hits to its disposition (policy
      hard-stop / BPT / reclassified).  Belongs to the Track B audit
      doc, but the mapping must exist -- an inventory that silently
      loses six entries between drafts is two drafts, not an audit.
  A4. Next likely demand after SWPCTX per the era map: SCBB/VPTB
      programming and IPL machinery (already wired -- but PalEntries is
      unaudited against the AARM; Track B reads context/MM/IPL leaves
      FIRST for exactly this reason).

--------------------------------------------------------------------------------
## 10. Sign-off gates (house discipline)

  GATE-1 (design): Architect approves the Q1-Q6 answers -- the HWPCB
          table with citations, the ordering, the ASN citation, the CC
          semantics -- BEFORE any source edit.
  GATE-2 (infra): C2 reviewed and merged before C3 begins.
  GATE-3 (merge): C3 + Sec 7 tests + Sec 8 evidence reviewed together;
          JRN entry drafted; then merge to v5-tb.
  Cowork drift fence: this brief authorizes edits ONLY to the SWPCTX
  leaf, the C2 infrastructure, F-1, and their tests.  Anything else
  found along the way is REPORTED, not fixed.
