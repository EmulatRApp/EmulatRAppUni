<!--
EmulatR V5 -- SPEC-SWPCTX-001 GATE-1 answer package
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.  NO SOURCE EDITS have been made; this document
exists to clear GATE-1.  Drift fence respected throughout.
-->

# SPEC-SWPCTX-001 -- GATE-1 ANSWERS (Q1-Q6, cited)

    Doc id   : SPEC-SWPCTX-001-GATE1
    Date     : 2026-07-27 (late evening, same session as the brief landing)
    Status   : FOR ARCHITECT REVIEW.  Approval of this document clears
               GATE-1 per the brief Sec 10.
    Sources  : (a) AARM: Processor Support/alpha_arch_ref.txt --
                   SWPCTX operation + description, "PALcode Instruction
                   Descriptions (II-A) 10-88..10-90" (txt lines
                   21379-21520); HWPCB Figure 12-1 / Sec 12.2; PCBB
                   Sec 13.3.11.
               (b) apisrm: Processor Support/Palcode/palcode/apisrm/
                   apisrm/ref/ev6_vms_callpal.mar, CALL_PAL__SWPCTX
                   (lines 189-470); field offsets ev6_vms_pal_defs.mar
                   lines 326-350.
               Every claim below carries its source line.

--------------------------------------------------------------------------------
## Q1. HWPCB field map (ev6_vms_pal_defs.mar:326-350; AARM Fig 12-1 / 12.2)

  | Offset | Symbol      | Width | Content                                    |
  |--------|-------------|-------|--------------------------------------------|
  | 0x00   | PCB__KSP    | quad  | Kernel stack pointer                       |
  | 0x08   | PCB__ESP    | quad  | Executive stack pointer                    |
  | 0x10   | PCB__SSP    | quad  | Supervisor stack pointer                   |
  | 0x18   | PCB__USP    | quad  | User stack pointer                         |
  | 0x20   | PCB__PTBR   | quad  | Page table base (PFN form; PAL shifts by   |
  |        |             |       | page_offset_size_bits before PT__PTBR --   |
  |        |             |       | callpal.mar:418-419)                       |
  | 0x28   | PCB__ASN    | quad  | ASN<7:0> (PCB__ASN__M = 0xFF, defs:340)    |
  | 0x30   | PCB__AST    | quad  | ASTEN<3:0> (defs:341-342), ASTSR<7:4>      |
  |        |             |       | (defs:343-344)                             |
  | 0x38   | PCB__FEN    | quad  | FEN<0> (defs:345-346), PME<62> (defs:347-  |
  |        |             |       | 348), DAT<63> (defs:349-350)               |
  | 0x40   | PCB__CPC    | long  | Charged Process Cycles, mod 2^32 (AARM     |
  |        |             |       | 10-89; apisrm SAVES it with hw_stl/p --    |
  |        |             |       | callpal.mar:428 -- a LONGWORD store)       |
  | 0x48   | PCB__UNQ    | quad  | Process unique value (AARM: only if        |
  |        |             |       | internal storage exists; EV6 VMS PAL does  |
  |        |             |       | NOT touch it in SWPCTX -- the OS reads/    |
  |        |             |       | writes it via RD_UNQ/WR_UNQ)               |
  | 0x50   | PCB__SCT    | quad  | Software context ("SCT"); PAL does not     |
  |        |             |       | touch in SWPCTX                            |

  Alignment contract: R16<6:0> must be 0 -- 128-byte alignment.  AARM
  10-88: reserved operand exception; apisrm checks `and r16,#0x7F`
  (callpal.mar:199) and routes to SCB__ILLOP via trap__post_km
  (callpal.mar:461-468).  NOT OPCDEC.

--------------------------------------------------------------------------------
## Q2. Ordering (apisrm ground truth; AARM says sequence unspecified)

  AARM 10-89: "The actual sequence of the save and restore operation is
  not specified" -- overlap of old/new HWPCB storage is UNDEFINED.  The
  guest-visible contract is atomicity (single retired grain in our
  model, per brief 4.3).  The apisrm ACTUAL order, for fidelity:

    1. Alignment check R16<6:0> (:199,205) -> ILLOP on failure.
    2. Read old PROCESS_CONTEXT (for old ASTEN/ASTSR) (:202).
    3. LOAD new PCB__AST, PCB__FEN, PCB__ASN (hw_ldq/p, physical)
       (:209-211).
    4. Write new ASN -> DTB_ASN0/DTB_ASN1 (:249-250) and build new
       PROCESS_CONTEXT (ASN | ASTER | ASTRR | FPE | PPCE) -> write it
       (:257-285).  [ev6_p1 and spinlock_hack blocks: see Deviations.]
    5. LOAD new PCB__CPC, PCB__PTBR (:394-395).
    6. CC swap per Q4 (:399-411).
    7. Store new PTBR (PFN<<13) -> PT__PTBR (:418-419).
    8. Read old PT__PCBB; LOAD new PCB__KSP; SAVE to old HWPCB:
       CPC (hw_stl/p), AST (old ASTEN|ASTSR<<4), current r30 as KSP
       (:426-430).
    9. r30 <- new KSP; PT__PCBB <- R16; hw_ret_stall (:431-433).

  NOTE the brief's 4.3 sketch (save-all-then-load-all) does not match
  apisrm's interleave; per the authority rule apisrm wins for behavior,
  and since the AARM leaves the sequence unspecified AND our leaf is
  atomic, the IMPLEMENTATION should follow apisrm's field SET exactly
  and may order internally for clarity.  What matters architecturally:
  the value of CPC/AST/KSP saved must be the OLD context's, and every
  loaded value must come from the NEW HWPCB.

  ESP/SSP/USP: NOT touched by EV6 SWPCTX.  AARM 10-90 Note: processors
  without internal per-mode SP registers "keep only the stack pointer
  for the current access mode in SP and switch this with the HWPCB
  contents whenever the current access mode changes."  SWPCTX executes
  in kernel mode, so only KSP swaps here; the other three swap at mode
  transitions.  ADJACENT CHECK (report-not-fix): verify EmulatR's CHMx/
  REI flows source ESP/SSP/USP consistently with this model (CpuState
  ksp/esp/ssp/usp fields, CpuState.h:513-552).

--------------------------------------------------------------------------------
## Q3. ASN / TB decision -- both authorities cited

  AARM 10-88 (SWPCTX operation):
      IF {ASNs not implemented in TB} THEN
          IF {IPR_PTBR NE new PTBR} THEN
              {invalidate TB entries with PTE<ASM> EQ 0}
      ELSE
          IPR_ASN <- new ASN
  i.e. the ASM-preserving invalidate is prescribed ONLY for processors
  whose TB has no ASN tags.  EV6 has ASN tags; apisrm SWPCTX performs
  NO TB invalidation of any kind (the full handler contains zero TBIA/
  TBIAP/TBIS operations) -- it writes DTB_ASN0/1 + PROCESS_CONTEXT.ASN
  and relies on ASN match.

  MANDATORY PRE-CHECK from brief 4.4, answered: SPAM ENFORCES ASN.
  TlbEntry::matches (pteLib/TlbEntry.h:170-176) compares entry ASN to
  the lookup ASN unless the entry's ASM bit is set; pinned by
  tests/pteLib/test_spam.cpp ("ASN miss + private entry -> false",
  "ASN miss + ASM=1 entry -> true").  Lookup passes cpu.asn
  (MemDrainer -> dtbMgr.lookup(realm, va, cpu.asn)).

  DECISION MATRIX for the architect (both are correct; pick one):
    (a) GROUND-TRUTH FAITHFUL: install ASN, no invalidate.  Matches
        both authorities on an ASN-tagged TB.  Relies on the enforcement
        that is already pinned by tests.  Risk: latent ASN-plumbing bug
        elsewhere (e.g. a fill inserting under the wrong ASN) would
        surface as stale translations in the hardest-to-debug era.
    (b) CONSERVATIVE (brief 4.4 as drafted): TBIAP on swap.  SPAM's
        TBIAP is the epoch sweep that PRESERVES ASM=1 entries
        (SPAMShardManager, pinned by "TBIAP preserves ASM=1 entries"
        test) -- so it honors the ASM survival requirement.  Costs
        refill traffic only; masks (a)-class bugs rather than exposing
        them.
  RECOMMENDATION: (b) for the C3 landing (matches the brief's decided
  posture, removes stale-translation risk from the first MM-heavy debug
  window), with a named follow-up to switch to (a) once the boot is
  past the next 2-3 gates -- the switch is one line plus the T3 test
  flip, and (a) is the fidelity end-state.  Either way T4 (GH-span)
  runs against whichever invalidation lands.

--------------------------------------------------------------------------------
## Q4. CC swap semantics -- apisrm IS the F-1 packed model (verbatim)

  apisrm callpal.mar:399-411:
      rpcc  p4                 ; offset<63:32> | counter<31:0>  <- PACKED
      zap   p4,#0xF0,p5        ; tmp2 = ZEXT(counter<31:0>)
      srl   p4,#32,p4          ; tmp3 = ZEXT(offset<63:32>)
      addq  p4,p5,p4           ; CPC of OLD process = offset+counter
      ...
      zap   p7,#0xF0,p7        ; ZEXT(new PCB__CPC<31:0>)
      subq  p7,p5,p7           ; new offset = new CPC - current counter
      sll   p7,#32,p7
      hw_mtpr p7, EV6__CC      ; writes CC<63:32> ONLY (HRM 5.1.1)
  and the AARM operation (txt 21402-21446) is the same computation.
  Old CPC is stored to the old HWPCB as {counter+offset}<31:0> -- a
  32-bit value, longword store (:428).

  CONSEQUENCES, confirming the brief's 4.5 sequencing:
    - F-1 MUST land first: RPCC must return the packed form, and
      MTPR CC must write the UPPER HALF ONLY, or the leaf's arithmetic
      is built on the flat-sum model and CPC accounting is garbage.
    - kCcMultiplier (the named deviation) must scale the COUNTER field
      only (JRN-ISA-001 F-1) -- SWPCTX's subq otherwise mixes scaled
      and unscaled quantities.
    - T5's exact assertion: after swap to a process whose PCB__CPC = X,
      an immediate RPCC returns {(X - counter)<31:0> << 32 | counter},
      so offset+counter == X (mod 2^32) at swap time, and CPC charged
      to the outgoing process equals its offset+counter at swap.

--------------------------------------------------------------------------------
## Q5. State homes + the C2 inventory (what exists vs what is missing)

  ALREADY IN THE TREE (verified this session):
    - Leaf-side guest memory accessor: ExecCtx::memory
      (memoryLib::GuestMemory*, set by PipelineDriver::step for EVERY
      EX call; coreLib/ExecCtx.h:118-128, CSERVE-class precedent).
    - Physical single-quad discipline: execLdqp_vms/execStqp_vms
      (S_PhysAddr memEffect, leaf-enforced alignment,
      PalEntries.cpp:1382-1430).
    - Per-mode SPs + PCBB: CpuState ksp/esp/ssp/usp/pcbb with the VMS
      PT-alias mapping documented (CpuState.h:513-552); MFPR/MTPR_ESP/
      SSP/USP leaves live against them.
    - asn, ptbr, ccOffset (F-1's field), PAL shadow regs +
      swapPalShadowRegs (CpuState.h:113-133).
    - ASTEN/ASTSR/FEN state: reachable via the wired MFPR/MTPR leaves.
  C2 THEREFORE REDUCES TO:
    - A small physical multi-quad read/write helper for the leaf
      (bounded loop over ExecCtx::memory with the LDQP alignment
      discipline; the ~6 loads + 3 stores of Sec Q2 do not need a new
      subsystem).  Direct GuestMemory access during EX is the
      DOCUMENTED escape hatch; BoxResult stays for the architectural
      commit (none needed here beyond side-state).
    - A home for PME (and DAT if we model it): likely one CpuState
      field each; PME has no performance-counter backend today (see
      Deviations).
    - PCBB lives in CpuState::pcbb (exists).  PT__PTBR maps to
      CpuState::ptbr (exists).
  NOTHING ELSE.  If implementation discovers otherwise, that is a
  GATE-2 stop, not an inline improvisation.

--------------------------------------------------------------------------------
## Q6. Above-SPAM invalidation on PTBR/ASN change

  Today: NOTHING exists above the SPAM TB.  jitLib contains only
  "Trivial Examples"; the TB/ComJIT tier is unimplemented (V5 brief,
  POC-stage).  So SWPCTX C3 has no tier to invalidate, and the answer
  to Q6 is NONE -- TODAY, plus the standing constraint already recorded
  in JRN-SCSI-032 Sec 4: when the TB tier lands, translation-block keys
  anchored on physical page + generation must treat GH-block entries as
  spanning 8^g pages, and the tier must subscribe to the same
  invalidation events as the SPAM layer (SWPCTX's ASN/PTBR change
  included).  This paragraph discharges the "TB-tier invalidation
  constraint comes due here" item for the current tree state.

--------------------------------------------------------------------------------
## Named deviations proposed (for architect sign-off with Q1-Q6)

  D1. spinlock_hack PCTR/SPCE block (callpal.mar:287-380): performance-
      counter workaround manipulating I_CTL.SPCE + PCTR_CTL around PME
      transitions.  EmulatR has no PCTR performance-counter model; the
      block exists to keep REAL counters coherent.  PROPOSE: skip,
      named deviation in the leaf header.  PME itself IS stored/loaded
      (PCB__FEN<62>) so the architectural bit round-trips.
  D2. ev6_p1 CNS__FPE_STATE store (callpal.mar:270-274): EV6 pass-1
      silicon workaround.  PROPOSE: skip, named.
  D3. DAT (PCB__FEN<63>): AARM says load conditional on internal
      storage.  Decide: model as a CpuState bit that round-trips (zero
      behavioral consumers today) or named-skip.  PROPOSE: round-trip
      the bit (cheap, keeps the HWPCB image faithful for T1).
  D4. PCB__UNQ / PCB__SCT: untouched by SWPCTX per apisrm; RD_UNQ/
      WR_UNQ own UNQ.  No action in this leaf; T1's field list follows
      apisrm's touched set, with UNQ/SCT asserted UNCHANGED.

--------------------------------------------------------------------------------
## A2 discharge + inventory reconciliation input (brief Sec 9)

  A2. FTOIS (0x1C func 0x78): CONFIRMED UNWIRED -- no GrainMasterV4.tsv
      row, no DispatchTables entry (FTOIT 0x70 present).  An FP-era
      guest executing FTOIS gets OPCDEC.  Reported to the Track B
      ledger; fence forbids fixing here.
  A3 input -- the nine PalEntries kFaultUnimplemented sites mapped:
      :216 execBpt_tru64        -> X2 (rides behind)
      :230 execBpt_vms          -> X2 (rides behind)
      :258 execChmk_tru64       -> Tru64-only; VMS CHMK is wired
      :1591 MTPR_VPTB guard     -> policy hard-stop (X3 family)
      :1997 opcode 0x30 fan-out -> MFPR_VIRBND, X2 (awaits personality
                                   codegen extension)
      :2439 HW_RESERVED_2D      -> policy hard-stop, env-gated (X3)
      :2466 unknown MFPR sel    -> policy hard-stop (X3)
      :2986 0x2D MTPR path      -> policy hard-stop, env-gated (X3)
      :3013 unknown MTPR sel    -> policy hard-stop (X3)
      Plus the one generated stub: SWPCTX (this task).  Zero entries
      lost between drafts.

--------------------------------------------------------------------------------
## GATE-1 disposition requested

  Approve/adjust: Q1 table, Q2 ordering posture (apisrm field set,
  atomic grain), Q3 option (a) vs (b) -- recommendation (b) now,
  (a) as named follow-up -- Q4 semantics + F-1-first, Q5 C2 scope
  (helper + PME/DAT homes only), Q6 none-today + standing constraint,
  D1-D4 deviations.  On approval: C1 (F-1) begins, fence unchanged.
