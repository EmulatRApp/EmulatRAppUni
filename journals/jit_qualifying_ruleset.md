# Rule set: JIT-qualifying Alpha instruction groups (EmulatR block registry)

Status: DESIGN / classification spec. Defines which Alpha basic blocks are
eligible for the hand-built / cached JIT block registry (`./jitLib/...`) versus
which must always fall back to the cycle-accurate interpreter. This is the
predicate the registration filter runs; it is NOT a runtime compiler design.

## The one principle everything derives from

A block is JIT-qualifying iff EVERY instruction in it is **pure**: its only
observable effects are (1) its result value(s) written to registers, and (2) its
modeled cycle cost. Anything whose behavior is observable in any OTHER way --
memory ordering, MMIO side effects, privileged-state mutation, traps/faults,
cross-CPU lock state, or a data-dependent control target the static block cannot
resolve -- disqualifies the block.

Rationale (established across the determinism work): a pure instruction is a
deterministic function of operands plus a deterministic cycle cost, so a flat /
compiled emission is observationally equivalent to the full pipeline path. The
moment an instruction has an effect outside (result + cycle cost), the compiled
path can diverge from the interpreter against the AXPBox oracle, and the block
must deopt.

## TIER A -- qualifying instructions (pure; may appear in a registered block)

Register-to-register, side-effect-free, non-trapping. A block whose body is
entirely Tier A (terminated per the Terminator rules below) qualifies.

- **Integer arithmetic, non-trapping** (opcode 0x10 INTA):
  ADDL, ADDQ, SUBL, SUBQ, S4ADDL, S8ADDL, S4SUBL, S8SUBL, S4ADDQ, S8ADDQ,
  S4SUBQ, S8SUBQ, CMPEQ, CMPLT, CMPLE, CMPULT, CMPULE, CMPBGE.
  NOTE: the L-forms (ADDL/SUBL/S4ADDL/...) require SEXT of bits <31:0> -- the
  compiled form is the 32-bit host op + sign-extend (movsxd), NOT a 32-bit
  truncate. Getting this wrong is a silent value bug.
- **Integer logical / conditional-move** (0x11 INTL):
  AND, BIC, BIS, ORNOT, XOR, EQV, CMOVEQ, CMOVNE, CMOVLT, CMOVLE, CMOVGT,
  CMOVGE, CMOVLBS, CMOVLBC, AMASK, IMPLVER.
  (AMASK/IMPLVER return fixed feature/version constants -- pure reads.)
- **Integer shift / byte manipulation** (0x12 INTS):
  SLL, SRL, SRA, EXTBL/WL/LL/QL/WH/LH/QH, INSBL/WL/LL/QL/WH/LH/QH,
  MSKBL/WL/LL/QL/WH/LH/QH, ZAP, ZAPNOT.
- **Integer multiply, non-trapping** (0x13 INTM): MULL, MULQ, UMULH.
- **Count** (Alpha CTPOP/CTLZ/CTTZ -> host POPCNT/LZCNT/TZCNT): pure.
- **Address arithmetic** (0x08 LDA, 0x09 LDAH): PURE. Despite the "LD" mnemonic
  these are Rc = Rb + sext(disp) [<<16 for LDAH] -- register arithmetic, NOT
  memory accesses. They qualify.

## TIER C -- disqualifying instructions (deopt; their presence rejects the block)

If any of these appears in the instruction group (before the terminator), the
block is NOT registered -- the whole group falls back to the interpreter. (Do
not split the block around them in the first cut; reject-and-interpret is the
clean, correct rule for a curated registry.)

- **All memory loads/stores** (0x0A-0x0F, 0x28-0x2F):
  LDL, LDQ, LDBU, LDWU, LDQ_U, STL, STQ, STB, STW, STQ_U, and the locked pair
  LDL_L, LDQ_L, STL_C, STQ_C.
  WHY: (a) the effective address may resolve to MMIO -- read side effects
  (read-clears, FIFO pops) the compiled path does not model; (b) memory ordering
  semantics; (c) LL/SC touch the per-CPU lock_flag -- the cross-CPU interlock.
  A static block generally cannot prove the EA is plain DRAM, so ALL memory ops
  deopt in the first cut. (Refinement: a provably-DRAM EA could be promoted to
  Tier A later -- see Open refinements.)
- **Privileged / hardware** (HW_MTPR, HW_MFPR, HW_LD, HW_ST, HW_REI):
  mutate or read internal processor registers / privileged memory / machine
  state. Always deopt. (This is why the trivial example's 0x13568 and 0x13590
  blocks -- full of HW_MTPR -- are NOT registrable.)
- **PALcode** (0x00 CALL_PAL, all functions): traps to PAL with arbitrary
  privileged side effects. Always deopt.
- **Trapping arithmetic /V variants**: ADDL/V, ADDQ/V, SUBL/V, SUBQ/V, MULL/V,
  MULQ/V -- trap on signed overflow. Their effect is not (result + cycle) when
  overflow occurs. Deopt (first cut). (Refinement: emit with an overflow check
  that deopts only on trap.)
- **Floating point** (0x14-0x17, all FP ops): rounding modes + exception flags
  live in FPCR (stateful), denormal handling, and FP traps. Until the FBOX /
  Tier-0 FPCR+rounding infrastructure is solid, ALL FP deopts. (After FBOX, FP
  is Tier B at best -- carries FPCR state.)
- **Barriers / serialization** (0x18 MISC): MB, WMB, IMB, TRAPB, EXCB -- memory
  / instruction / exception ordering side effects (critical under SMP). Deopt.
- **Side-effecting reads / hints** (0x18 MISC): RC, RS (read-and-clear interrupt
  flags -- side effect), ECB, WH64/WH64EN (cache hints), FETCH/FETCH_M (prefetch
  hints). RC/RS deopt (side effects). FETCH/WH64/ECB MAY be treated as NOPs
  later, but deopt in the first cut to stay conservative.
- **RPCC** (read processor cycle counter): reads dynamic cycle state. Tier B --
  permissible only if the block's carried cycle model supplies the exact value
  the interpreter would; deopt in the first cut.

## TERMINATORS -- end the block (do NOT disqualify it)

A block ends at the FIRST of: a control-flow instruction, OR a Tier-C
instruction. Two terminator classes:

- **Direct (statically-resolved) control flow** -- the ideal terminator:
  BR, BSR, and the conditional branches BEQ, BNE, BLT, BLE, BGT, BGE, BLBC,
  BLBS (and FP branch forms). Target is PC-relative and known at registration
  time. The branch predicate is pure. These permit block LINKING (compiled
  edge block->block). A registered block SHOULD end on one of these.
- **Indirect (data-dependent) control flow** -- a SOFT terminator:
  JMP, JSR, RET, JSR_COROUTINE (opcode 0x1A). The block body up to the jump may
  be Tier-A-pure and registrable, but the EXIT target comes from a register, so
  it cannot be statically linked -- the exit returns through the dispatch cache
  (exact-PA lookup). Registrable, but no outbound link; the exit is a lookup.

If the first non-Tier-A instruction is a Tier-C op rather than a branch, the
block is rejected (per Tier C), not terminated-and-registered.

## ENTRY VALIDITY -- what may be a block key

- Entry PA must be 4-byte aligned and a statically-proven instruction boundary.
- **Reject `+N` labels.** Alpha instructions are longword-aligned (low 2 PC bits
  always 0), so a target of PA+1/+2/+3 is architecturally impossible as code --
  it signals Ghidra analysis uncertainty (computed/indirect jump guessed, or
  data-in-code). Such PAs are NOT registry entries; they fall back to the
  interpreter. (In the trivial example: LAB_0001355c+2, LAB_000135a4+3,
  LAB_000135ac+1, LAB_000135c4+2 are all excluded on this rule.)
- Entry must be a real control-flow target (XREF kind (j)/(c)) or a function
  entry -- not solely a data reference (*).

## REGISTRATION PREDICATE (the filter)

    qualify(entryPA):
      if not aligned4(entryPA) or not provenInstrBoundary(entryPA): REJECT
      group = []
      pa = entryPA
      loop:
        I = decode(pa)
        if I in TIER_C:                      REJECT   # impure op in body
        if I is a TERMINATOR:
            group.append(I)
            record terminator kind + successor PA(s)
            ACCEPT(group)                              # subject to verify gate
            break
        if I in TIER_A:
            group.append(I); pa += 4; continue
        # unknown / unhandled encoding:        REJECT  # be conservative
        if len(group) > MAX_BLOCK_LEN:        REJECT   # runaway / unstructured

## VERIFY GATE -- a block is not "qualified" until proven equivalent

ACCEPT above is necessary, not sufficient. Before a block resolves at runtime it
MUST pass the equivalence harness: execute the compiled bytes on an exec page and
diff, over the same PA range, BOTH:
  (a) full architectural register state, AND
  (b) the modeled cycle count (the MHz profile),
against the interpreter. Only blocks that match on state AND cycles get
`verified: true` and are allowed to resolve. This is the same verify-don't-assert
discipline as the SMP determinism-equivalence test -- a hand-built block is an
assertion until the harness proves it.

## INVALIDATION

- **Lookup is exact-PA** (hash on entry PA, hit-or-miss; never containment).
- **Invalidation is range-overlap**: any guest write (CPU store or DMA) into a
  registered block's [entryPA, endPA) span evicts it. The SRM decompressor
  overwrites code -- this is live, not hypothetical. Exact-match in, range-
  overlap out.

## OPEN REFINEMENTS (future tiers, explicitly out of the first cut)

- **Provably-DRAM memory ops -> Tier A.** If a load/store's EA can be proven to
  land in plain DRAM (never MMIO) and outside any locked granule, it could be
  promoted. Requires EA range analysis; deferred.
- **/V trapping arithmetic -> conditional.** Emit with an overflow check that
  deopts only on trap, rather than blanket deopt.
- **FP -> Tier B after FBOX.** Once FPCR/rounding/exception state is modeled,
  FP ops carrying FPCR state may qualify.
- **RPCC / cycle-state reads -> Tier B** once the carried cycle model is proven
  to supply the interpreter-identical value.

## Net

First-cut qualifying group = a 4-byte-aligned, statically-proven entry whose body
is entirely Tier A, terminated by a direct or indirect control-flow instruction,
containing zero Tier-C instructions, and proven equivalent to the interpreter on
state AND cycles. Everything else falls back. From the trivial example: 0x8000
and 0x13540 qualify; 0x13568 and 0x13590 (HW_MTPR) do not; the `+N` labels are
excluded as entries.
