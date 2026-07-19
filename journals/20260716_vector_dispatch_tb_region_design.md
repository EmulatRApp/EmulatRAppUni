<!--
EmulatR -- Vector-Dispatch: Translation Cache / Translation Block (Loop-Only TB) Design
Project: EmulatR (Alpha 21264 / EV6 emulator), active tree (emulatrappuniv5)
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Purpose: design note (not generated code) capturing the settled Tier-1
fast-path model for the Translation Cache (TC) and Translation Block (TB):
qualification (birth), entry, the inner-pipeline execution machinery,
invalidation, and the faithfulness-verification harness. Implementation happens
in Cowork against the live tree after review. Treat all file/line references as
a point-in-time snapshot; Cowork is the source of truth for current file state.
ASCII(128) only.
-->

# Vector-Dispatch: TC / TB Design Note (Loop-Only TB)

Date: 2026-07-16
Status: DESIGN, pending review. No edits landed. Discuss-before-code applies.

Revision history:
- Rev 1 (2026-07-16): "region" model -- head to first indirect/PAL transfer.
  SUPERSEDED.
- Rev 2 (2026-07-16): narrowed to the loop-only TB (loop body only).
- Rev 3 (2026-07-16): added qualification (birth) ruleset and the presence-bit
  entry mechanism; moved SMC to invalidation; PC<0> a stated dependency.
- Rev 4 (2026-07-16): fully documented the inner-pipeline machinery (Section 9);
  birth commit at retire; invalidation scaffold (Section 13); three-stage hook
  map; non-architectural-metadata discipline.
- Rev 5 (2026-07-16): added the verification harness (Section 17) and the
  counter/interrupt-cadence machinery (Section 9.9); specified the recording
  state machine (Section 5); adopted the slice defaults (strict gate, N=1);
  added implementation-readiness items (Section 18). This document.

## 1. Purpose and scope

This note fixes the Tier-1 "Fast Path" that accelerates the existing 6-stage
PipelineDriver (the "Faithful Path" / main pipeline). It records decisions
reached in discussion so the shape does not drift before implementation.

In scope: what a Translation Block is, how a PC qualifies (birth), how the
machine enters a TB, the inner-pipeline machinery that executes it, how it
exits, how it is invalidated, and how it is proven faithful.

Out of scope: host code generation (see Under_Consideration_comJIT.md),
register allocation, and any change to EX/MEM/WB instruction semantics. The Fast
Path reuses the existing back-end unchanged.

## 2. The decision, and the hooks

A Translation Block is a cached LOOP BODY and nothing else. It is born from
dynamic back-edge detection, entered by a keyed cache probe, executed by an
internal pipeline that is the existing back-end driven from cached grains, and
left the instant control does anything other than loop back to its head.

The machine touches its accelerator tables at three points, at three pipeline
positions (full map in Section 15):

- BIRTH: detected at EX (branch resolution, taken), committed at RETIRE (WB).
  The qualifier (Sections 5-6).
- ENTRY: at IF -- a keyed presence-bit read then TC probe on the main pipeline's
  normal step (Section 8).
- INVALIDATION: at the store-commit primitive (MEM effect), shared by both
  pipelines (Section 13).

All three touch NON-ARCHITECTURAL metadata only (counters, presence bits, TB
storage, watch tables). None of it is guest-visible state; it must be sequenced
so that turning the accelerator on or off yields a bit-identical guest trace
(Section 17).

Everything that is not a cached loop body -- trailing tails, straight-line runs,
calls, the eventual unconditional/indirect transfer -- executes in the main
pipeline.

Rejected alternatives:
- Rev 1 region model (head to first indirect/PAL transfer). Caches run-once
  code, which buys nothing (Section 3) and enlarges the write-watch footprint.
- Strict single-branch basic blocks. Correct and faithful, but caches every
  block whether reused or not and fragments a hot loop into tiny blocks with a
  TC probe per branch per iteration.

## 3. Governing principle: cache only what is reused

Caching is justified only by reuse. The inner pipeline's sole advantage is
skipping the instruction front end (IF/DE/GR: fetch, decode, grain-resolve,
dispatch) on RE-execution. Code that runs once gains nothing from a cache entry:
the decode was already paid once and the code runs once, so the entry is pure
overhead -- grain storage, a larger write-watched footprint, more
self-modification exposure -- for zero speedup. The only source of reuse in a
straight instruction stream is a back-edge: a loop. Therefore the TB is
coextensive with the loop body. This single principle drives the rest.

## 4. Terminology

- TC (Translation Cache): map from a loop-head key to a TB.
- TB: a cached loop body -- pre-resolved grains for [head, back-edge], plus
  metadata to execute, gate, and invalidate it.
- Head / owner: the PC a back-edge targets; the loop entry.
- Back-edge: a branch whose taken target is the head; the loop bottom test in
  the common case.
- Canonical address: pc & ~1. On this target bit 0 of the PC is the PALmode flag
  (Sections 14, 18); comparisons and keys use the canonical address with mode
  carried separately.
- Candidate: a head PC observed as a back-edge target but not yet promoted.
- DraftBlock: the in-progress grain recording for a candidate being sealed.
- VALID: a candidate promoted and sealed (Section 5); only VALID heads enter.
- Continuation gate: the per-lap test next-PC == owner ("spin" vs "exit").
- Grain: the existing decoded-operation representation. A cached grain is frozen
  at the post-decode, ready-to-execute point (Section 9).
- Inner pipeline / spin: the loop that replays a TB's grains from EX each lap.
- Faithful Path / main pipeline: the existing 6-stage PipelineDriver; the
  architectural source of truth; runs everything not a cached loop.
- Deopt: abandoning inner-pipeline execution and returning to the main pipeline
  in a state indistinguishable from never having entered the TB.

## 5. TB birth: detection and the recording state machine

A TB is not created for a cold PC. It is born only when a loop is observed.

Detection (detect at EX, commit at retire):

1. Detect at EX. Branch direction and target are resolved at EX against live
   registers. Only a TAKEN branch traverses a back-edge; a conditional backward
   branch that falls through does not. Testing the static target at DE would
   miscount interior conditionals that never take. Detection fires at branch
   resolution, gated on taken.
2. Commit at retire (WB). Apply the effects -- count, promote, record, seal, set
   presence bit -- only for RETIRED branches. The iBox can squash and refetch
   after EX; retire-commit builds the accelerator from instructions that
   actually executed and aligns it with the retire-ordered trace. (By the
   robustness property in Section 6 a squashed miscount would only waste a TB,
   never mis-execute, but retire-commit is cleaner and is the chosen point.)
3. Back-edge test on canonical addresses. With target = branch target
   canonicalized and here = branch PC canonicalized, a taken retired branch with
   target < here (the run's earlier PC) is a back-edge. Head = target.

Recording state machine (arm -> record -> seal):

4. Arm. On a retired back-edge, get-or-create the candidate for the head and
   increment its count. When the count reaches N (slice default N=1, Section 6)
   and no sealed TB exists for the head, ARM recording for that head.
5. Record. On the next arrival at the head with recording armed, capture the
   grain at each PC from the head forward, advancing sequentially, until the
   back-edge PC is reached again. The captured PC-indexed set is the DraftBlock.
   Recording captures grains BY PC, never a linear executed sequence -- replaying
   a fixed sequence would bake in that pass's branch outcomes (the phantom-tail
   bug, one level down); the inner pipeline re-evaluates branches live.
6. Seal, or discard. If the recording pass reaches the back-edge cleanly (a
   contiguous head..back-edge run), SEAL: insert the VALID TB into the TC, then
   set the presence bit LAST -- "bit == 1" must always imply a complete VALID TB,
   so a mid-build bit can never let IF enter a half-built block. If the pass
   diverges before reaching the back-edge (an interior branch leaves the
   [head, back-edge] range -- i.e. it did not actually loop that pass), DISCARD
   the DraftBlock and re-arm; only a clean pass seals.

Timeline with N=1 (slice default): lap 1 executes head..back-edge faithfully,
the back-edge retires taken -> count=1=N -> arm. Lap 2 records head..back-edge ->
seal (VALID). Lap 3 onward the entry probe (Section 8) hits VALID -> spin. The
first two laps are faithful; the accelerator engages from the third.

## 6. Qualification ruleset

A candidate head is promoted under a deliberately small ruleset:

- Hotness (the only promotion gate). Reached via its retired back-edge at least
  N times. SLICE DEFAULT N=1 (form on the first retired back-edge). The harness
  (Section 17) surfaces churn immediately and N is trivial to raise; revisit
  post-measurement (Section 20).
- Mode/ASN constant across the body. A body must not contain an interior
  ASN/PALmode transition, because mode is part of the key and must be constant
  within the body. (A HW_REI/CALL_PAL inside a body is an exit anyway;
  Section 9.5.) This is the only structural exclusion.

Explicitly NOT qualification criteria:

- Self-modifying code. Cannot be screened at record time and need not be; it is
  a runtime event handled by write-watch and invalidation (Section 13),
  including a reactive blacklist that IS the correct home for an "ineligible"
  mark -- learned from an observed self-write, not predicted.
- Exit-determinism / single-exit / reducibility. Multi-exit and irreducible
  loops are faithful under the continuation gate; gating on exit structure would
  exclude common loops for no faithfulness benefit.

Robustness property (why the qualifier can be cheap and loose): a mis-qualified
PC is an EFFICIENCY loss, never a CORRECTNESS error. If a promoted head is not
really a hot loop, the gate misses on the next entry, the machine exits to the
authoritative main pipeline, and the wasted TB never pays off. Nothing wrong is
executed, because the gate and deopt -- not the qualifier -- are the source of
truth. This licenses the cheap "target < here" heuristic with a small N.

Candidate hygiene: give the candidate table an aging/eviction policy (or a small
fixed hot-set) so one-shot backward branches over a long boot do not accumulate.

## 7. TB extent: the loop body only

A TB contains exactly the grains executed in one traversal: [head, back-edge],
inclusive. For the common bottom-test loop this is a contiguous canonical-address
range; membership is a bounds test plus an index
(grain index = (canonical(pc) - head) >> 2). Nothing after the back-edge is a
member. Interior instructions (including interior conditional branches that fall
through each iteration) are ordinary members. A TAKEN interior branch is handled
by the continuation-gate policy of Section 10.

## 8. TB entry: the IF-stage probe

Entry is the handoff from the main pipeline into the inner pipeline. Note the
write/read asymmetry: the presence bit is WRITTEN at birth commit (retire) and
READ here at IF. This is forced -- the creating signal (a taken back-edge) is an
execute-time fact; the consuming decision (enter) is a fetch-time one.

- Presence-bit filter. A full TC hash lookup on every instruction would tax cold
  code. Bound it with a presence-bit table checked before any TC lookup:
  - A bit array indexed by the fetch PHYSICAL address (available after the ITB
    lookup fetch already performs), at a fixed block granularity (for example 64
    or 128 bytes). Physical indexing bounds the table to installed RAM and
    matches the TB key and write-watch domain.
  - At IF, after translation, read PresenceTable[pa >> blockShift]. Bit 0 ->
    cold: skip the TC lookup and run the faithful step (one array read). Bit 1 ->
    potential head: do the full TC lookup with the key (Sections 14, 18) to
    confirm and enter.
  - The bit is COARSE ("some head lives in this block"); exactness comes from the
    TC lookup, which misses on a false positive and falls through. While a loop
    still runs faithfully (pre-promotion or post-invalidation), interior
    instructions sharing the head's block pay a benign missing TC lookup;
    self-limited, since once entered interior fetches go through the inner
    pipeline and never touch the IF probe.
  - Set the bit at PROMOTION (Section 5), LAST; never for an un-promoted
    candidate.
  - Cost framing: this is an interpreter, so the win is replacing a hash lookup
    with a single array read on cold code, not host cycles. Entry mechanism does
    not affect faithfulness; the gate and deopt are authoritative regardless.

- Single-entry only. Enter at the owner PC. Arriving mid-body from elsewhere is a
  different PC that runs in the main pipeline.
- Zero state marshalling. The inner pipeline shares CpuState and commits per
  grain, so "enter" is: point the internal index at grain 0 and run. Registers,
  PC, and mode are already live and correct.
- Zero entry latency. The very back-edge whose retirement triggers promotion
  redirects control to the head, so the next IF sees the freshly-set bit.

## 9. The inner-pipeline machinery

The heart of the Fast Path: how a TB spins.

### 9.1 The cached grain enters at EX

A cached grain has been carried through IF, DE, GR ONCE at record time and is
frozen at the post-decode, ready-to-execute point. From the inner pipeline's
view EX is stage one: each lap is execute-and-test, front end already spent.
Front-end cost is amortized -- decoded once, replayed (about 1.5M times in the
reference trace) from EX. "Branch resolution is at EX, 50% into the pipeline"
describes the FAITHFUL 6-stage driver; the inner pipeline is not that driver and
has no stages to traverse -- it calls each grain's execute directly.

### 9.2 What is frozen vs what runs live

Frozen: the decode OUTPUT -- operation, register specifiers, immediate/
displacement, resolved handler. Live each lap: reading the live source registers
from CpuState, the EX computation, the MEM effect, and per-grain commit. The
branch grain is frozen as "conditional, test Rb, displacement D"; each lap it
reads the live counter and computes next-PC.

Instruction front end elided, data side NOT elided. Skipping IF/DE/GR removes
instruction fetch/decode/dispatch. It does NOT remove data-side work: a load or
store still computes its effective address, translates via the DTB, and touches
memory every lap, because those addresses are data-dependent.

### 9.3 No staging, no forwarding -- per-grain commit

The inner pipeline executes grains sequentially over shared CpuState and commits
each grain immediately (EX -> MEM -> WB in the existing order; BoxResult at EX,
applied at MEM, traced at WB). Committing before the next grain runs means no
pipeline hazard, no forwarding, no inter-stage bookkeeping to emulate. This is
what makes "resume at EX" both cheap and bit-faithful to the main pipeline's
per-instruction results.

### 9.4 The spin loop: execute-and-test

Per lap, for each grain in order: run its execute (EX/MEM/WB) and commit. The
terminating grain (the back-edge) produces a next-PC. Then the continuation gate:

  next-PC == owner (head)?
    yes -> reset the internal index to grain 0 and spin.
    no  -> exit to the main pipeline at next-PC (Section 11).

The "test in the loop" is a single integer compare after the terminating grain.
No fetch, decode, dispatch, or staging surrounds it.

### 9.5 The gate is uniform across transfer types

BNx, unconditional BR, JSR/JMP/RET, HW_REI, CALL_PAL need no bespoke handling.
The grain's execute handler encodes the semantics and hands back next-PC (and
mode/trap flags), so the gate is one comparison regardless of type.

CALL_PAL and HW_REI are never a loop back-edge -- they transfer to PAL dispatch
and change mode, not back to a head. They can only appear as an INTERIOR grain,
where the mode change makes the body ineligible (Section 6), or as a terminating
transfer, in which case there is no back-edge and no loop qualifies. Either way
the spin never special-cases them: such a grain yields next-PC != head, the gate
exits, and the mode change is handled by the main pipeline it exits into.

### 9.6 Exit without re-execution

On any gate miss the terminating grain has ALREADY executed and committed. The
machine hands off at its computed next-PC; it does NOT re-run that grain in the
main pipeline. No double execution, no state to reconcile.

### 9.7 In-flight self-invalidation deopt

A store grain inside the body can invalidate the executing TB (the reference copy
loop marches its dest through its own code pages). Handling: the store commits,
the write-watch fires (Section 13), and if the target is inside a live TB it sets
an "invalidated-in-flight" flag. The spin checks that flag at every grain
boundary; if set, it deopts immediately -- finishing the current committed grain,
then handing next-PC to the main pipeline instead of advancing the cached body.
The main pipeline re-fetches from modified memory. This guarantees any subsequent
instruction, this lap or the next, comes from current bytes. Exit-reason 2 of
Section 12, realized inside the spin.

### 9.8 Summary of one lap

Resume each grain at EX -> read live operands from CpuState -> EX compute ->
MEM (data-side DTB translate/access for loads and stores; write-watch on stores)
-> WB commit -> advance per-instruction counters (9.9) -> check
invalidated-in-flight and pending-interrupt at the boundary (9.9) -> at the
terminating grain, gate-compare next-PC to head -> spin or exit.

### 9.9 Counters and interrupt cadence (faithfulness-critical)

The spin must be indistinguishable from the faithful path in the retire trace,
so it must reproduce two per-instruction behaviors exactly:

- Counter accounting. Advance the same per-instruction counters the faithful
  path advances, per grain. The reference trace increments ord and rpcc by 1 per
  retired instruction (a 1-cycle-per-instruction, best-effort-deterministic
  model), so the spin increments them by 1 per grain, identically. Any cycle or
  retirement counter the faithful path ticks per instruction, the spin ticks per
  grain.
- Interrupt poll cadence. The spin polls for pending interrupts at the SAME
  boundary the faithful path does -- per grain boundary -- and on a pending
  interrupt deopts to the main pipeline so delivery occurs at the identical PC.
  The spin must not defer interrupt checks to the back-edge; deferring would
  shift the delivery point and diverge the trace.

These two are what make the retire trace bit-identical across accelerator on/off
(Section 17). They are acceptance-critical, not optional.

## 10. Continuation-gate policy (strict, adopted for the slice)

The body's interior conditional branches fall through every iteration in the
reference trace, so they never challenge the gate. The policy question is what
happens when an interior branch is TAKEN:

- Strict gate: continue ONLY on next-PC == head. Any other next-PC -- including
  an interior branch taken anywhere -- exits to the main pipeline. Smallest
  faithful surface (one equality test); captures exactly the bottom-test loops
  that dominate the hot path.
- Body-membership gate: continue if next-PC lands anywhere in [head, back-edge].
  Keeps loops with internal control flow cached, at the cost of a range test and
  a larger faithful surface.

DECISION (slice default): STRICT. It matches "the TB is responsible for one
thing" and is the smallest change that speeds the target loop. Body-membership
is deferred and revisited only if profiling shows hot loops with taken interior
branches (Section 20).

## 11. What executes in the main pipeline

Everything that is not a cached loop body: trailing tails; straight-line runs up
to and including their terminating BR/JSR/RET/PAL transfer; the body of any loop
not yet detected or not yet sealed; and any path the strict gate declines. The
main pipeline is authoritative and is where the next loop is detected. On a loop
exit, control returns to it at next-PC and proceeds until the next retired
back-edge.

## 12. Exit and deopt reasons

1. Loop exit (gate miss). Normal and cheap. Hand next-PC to the main pipeline.
   No rollback; completed grains committed per grain.
2. Store-into-body (self-modification). Invalidate and deopt in-flight
   (Sections 9.7, 13).
3. Trap / interrupt. A grain faults or an interrupt is pending mid-body
   (polled per grain boundary, Section 9.9). Deopt to the main pipeline so the
   event is taken exactly as hardware would.

Only reason 1 is routine. Reasons 2 and 3 are the deopt paths; per-grain commit
makes the boundary clean (fully committed at every grain).

## 13. Invalidation and write-watch (the invalidation scaffold)

Key a TB (for ENTRY) on (head canonical address, backing physical page(s), ASN,
PALmode). Mid-body these are constant by construction (Section 6), so only entry
confirms the tag.

- MemoryWatcher: a page-bucketed map, physical page -> list of TBs covering it,
  refined by an exact tb->contains(pa, size) range test. Register a TB under
  EVERY page its body touches, not just the head page.
- Hook at the shared store-commit primitive, not "the MEM stage of the 6-stage
  driver": the hot-loop stores execute in the inner pipeline, so the check sits
  at the single store primitive both pipelines call.
- Gate the map with a PA-indexed write-watch bit (same structure family as the
  entry presence bit; they can share one table with two bit-planes). Every store
  reads that bit first; if 0 -- the overwhelming majority -- it skips the map. Only
  stores to watched pages pay the map lookup and contains test. This keeps the
  store path cheap (no bare unordered_map on every store).
- Physical-only keying (required, not merely simpler): the same physical code
  page can be aliased at multiple VAs and ASNs; a store modifying code may arrive
  through a different mapping than the one the code executes from. Only physical
  keying catches that. No new page-table walk -- the store already computed its PA
  via the DTB and the TB recorded its code PA via the ITB. The watch map is keyed
  by BARE PA (no ASN/mode), so one physical write invalidates EVERY TB over those
  bytes across all ASNs/modes, even though the entry key carries mode/ASN. No
  TBIA/TBIS hook needed for correctness (only later for GC).
- Invalidate-always first. A store into a covered range sets tb->status = INVALID
  (and triggers in-flight deopt if that TB is executing, Section 9.7). Simple,
  100% faithful. Compare-then-invalidate is deferred: it adds a memory read per
  watched store and does not even help the copy loop (which writes DIFFERENT
  bytes). If built later, it must compare the PRE-store bytes against the store
  value (reading after commit makes current == value always true).
- Self-modifying blacklist (reactive ineligibility). Once dst marches into the
  code region a naive re-qualify would thrash. When a TB is invalidated by a
  store into its OWN body, add the head to a small blacklist/cooldown the
  qualifier checks, so it does not immediately re-birth a body being overwritten.
- GC and presence bit: unlink an INVALID TB from the MemoryWatcher; leave the
  entry presence bit set (a benign false positive) unless refcount-clearing
  proves worthwhile (Section 20).

## 14. Dependency: PALmode PC<0>

The TB key and every address comparison depend on the canonical PALmode
representation. The reference trace PCs are ODD -- 0x9003ed, 0x9003f1, 0x9003f5 --
because bit 0 is the PAL flag (pal=1 on every line); the real instruction
addresses are the even values. The comparison and key must operate on the
canonical address with mode carried separately. IMPORTANT (Section 18): confirm
the ACTUAL current CpuState representation before coding -- the PALmode briefing
states V4 holds mode in a separate CpuState::palMode bool, decoupled from pc, so
the odd PCs may be a trace-formatting artifact while pc is internally 4-aligned.
Implement against the real representation; treat pc & ~1 as the forward-compatible
form for when PC<0> lands.

## 15. Integration seam in PipelineDriver -- the three-stage hook map

- BIRTH -- detect at EX, commit at RETIRE (WB). On a resolved taken branch whose
  canonical target is the run's entry (target < here), note a back-edge; at
  retire, get-or-create the candidate, increment, and on promotion arm/record/
  seal (Section 5) and set the presence bit last.
- ENTRY -- read at IF. Before fetch, read the PA-indexed presence bit; on a set
  bit do the TC lookup; on a VALID hit whose head == this PC, enter the inner
  pipeline (Section 9); on exit resume at next-PC.
- INVALIDATION -- at the shared store-commit primitive. Read the PA-indexed watch
  bit; on a set bit consult the MemoryWatcher, invalidate covered TBs, trigger
  in-flight deopt if one is executing, and blacklist the head on a self-write.

With no detected loop the machine runs the existing 6-stage path, which also
detects loops and builds TBs. The Fast Path is additive; the main pipeline stays
authoritative. Exact insertion file/line, run-entry-PC tracking, and hand-off
shapes are pinned against the live PipelineDriver.h at implementation
(Section 18).

## 16. Worked example (reference trace)

PALmode; PCs carry bit 0 = 1 (PAL flag); instruction addresses are the even
values.

    -- loop body (the TB) --
    0x9003ed HW_LD    grain 0  (head / owner; address 0x9003ec)
    0x9003f1 BLT      grain 1  (interior conditional; falls through)
    0x9003f5 LDA      grain 2  (dst pointer bump)
    0x9003f9 SUBQ     grain 3  (counter decrement)
    0x9003fd HW_ST    grain 4  (dst store)
    0x900401 BLT      grain 5  (interior conditional; falls through)
    0x900405 ADDQ     grain 6  (src pointer bump)
    0x900409 BNE      grain 7  (back-edge; taken target 0x9003ed)
    -- tail (main pipeline, run once) --
    0x90040d HW_LD ... 0x90041d JSR (indirect -> 0x6005c0)

Birth (N=1): lap 1 executes grains 0..7 faithfully; the BNE retires TAKEN,
canonical target 0x9003ec < 0x900408 -> candidate(0x9003ec) count=1=N -> arm. Lap
2 records grains 0..7 by PC and seals: insert TB, set presence bit last. The tail
is never a member.

Entry: lap 3, the IF presence bit is set; the TC lookup hits VALID -> enter at
grain 0.

Spin: each lap resumes grains from EX, ord/rpcc +1 per grain, interrupts polled
per grain boundary. Interior BLTs fall through; the BNE yields next-PC ==
0x9003ed == owner -> spin. About 1.5M laps, zero TC probes, zero front end.

Self-modification: as dst reaches the code region an HW_ST lands inside the body;
the store commits, the watch fires, the invalidated-in-flight flag is set, the
spin deopts at the next grain boundary to the main pipeline (re-fetch), and the
head is blacklisted.

Loop exit (no-SMC case): the counter exhausts, the BNE falls through, next-PC
0x90040d != owner -> exit. The tail runs once in the main pipeline; the JSR
transfers to 0x6005c0.

## 17. Verification harness (faithfulness acceptance)

The accelerator is validated by differential retire-trace equivalence -- the same
harness EmulatR uses for virtual-vs-silicon / AXPBox comparison, a new engine
pair on existing machinery. It is the primary acceptance test for the slice and
must stay green at every increment.

- Method. Run the same guest from the same initial state twice: accelerator OFF
  (pure main pipeline) and accelerator ON (TB machinery active). Compare the
  RETIRE TRACE per instruction -- ord, rpcc, PC, register writes, and memory/IO
  transactions, in order. Acceptance = BIT-IDENTICAL.
- Guest-visible only. The harness compares guest retirement. Accelerator-internal
  events (enter, spin, deopt, invalidate, blacklist) are NON-ARCHITECTURAL and
  must leave the guest trace unchanged. A self-modification that deopts on ON must
  still produce the identical guest retire trace as OFF, where it was always
  faithful; the internal deopt is invisible to the comparison.
- Stand it up FIRST, before trusting any TB. Because a loop-only accelerator's
  results are supposed to EQUAL the faithful path exactly, any diff is a bug in
  the accelerator, localized to the trace window.
- Bounded windows only. Compare a gated window around the copy-loop region, per
  the project trace discipline (bounded tails / gated windows, never whole-file
  grep of multi-GB traces).
- What it proves for the interpreter TB. Per-grain commit makes instruction
  result, side-effect content, and side-effect ordering identical by
  construction; the diff confirms it and additionally catches the two
  Section 9.9 mistakes (counter drift, interrupt-cadence shift). Fault/exception
  equivalence is exercised naturally by the copy loop's self-modification
  (in-flight deopt): ON and OFF must retire identically across it.
- Increment discipline. Green the diff on the minimal slice first (Section 18),
  then add the presence bit and write-watch, re-running the diff after each.
- Forward link. This is the same harness the comJIT track
  (Under_Consideration_comJIT.md, Section 6) requires; standing it up now on the
  interpreter TB is the prerequisite for trusting compiled units later.

## 18. Implementation readiness (verify against the live source)

These cannot be resolved from design alone; confirm against the actual headers
before/at implementation. They do not change the model, only how it lands.

1. PC representation. Confirm whether CpuState.pc carries bit 0 or mode is a
   separate CpuState::palMode bool (the PALmode briefing says the latter for V4).
   Implement the key/comparison against the ACTUAL representation -- likely
   (pc, palMode-bool, asn) today; pc & ~1 is the forward-compatible form for
   PC<0>. Do not write pc & ~1 if pc is already 4-aligned internally.
2. Back-end separability. Confirm EX/MEM/WB can be invoked on an already-resolved
   grain outside IF/DE/GR, and that a grain is copyable/retainable into a TB array
   independent of live pipeline state. If not cleanly separable, a small refactor
   to expose an "execute resolved grain" seam precedes the slice. This is the
   item most likely to reshape day one.
3. next-PC exposure. Confirm the terminating branch grain exposes next-PC to the
   caller (ExecCtx/BoxResult) so the gate can read it.
4. ASN. Defer ASN from the slice key -- the decompressor runs in PAL with identity
   mapping; add ASN when caching mapped guest code.
5. PA plumbing. The copy loop is identity-mapped (va == pa), so the slice may use
   va as pa; generalize PA acquisition (ITB/DTB result) when caching mapped code.
6. Slice build order. Minimal first: a single-TB map and a direct store check, NO
   presence table and NO write-watch bitmap, to reach a green trace diff
   (Section 17) on birth -> entry -> spin -> in-flight-invalidate. Then add the
   presence bit and write-watch as optimizations, re-running the diff at each
   step.

## 19. Determinism and named trade-offs

The loop-only TB is a pure front-end cache over a proven-hot loop body:
single-threaded, same grains, same EX/MEM/WB ordering, per-grain commit, same
per-instruction counters and interrupt cadence, non-architectural metadata. Its
entire divergence surface from the authoritative pipeline is the continuation
gate (one test) plus the deopt paths, and the harness (Section 17) is what proves
that surface closed.

Trade named (per the V4 rule that determinism/scope trade-offs are stated): the
strict gate (Section 10) declines any path other than a clean back-edge to head,
so a loop with a taken interior branch is not kept cached across that branch and
drops to the main pipeline. Chosen to keep the faithful surface minimal; widen to
body-membership later if such loops prove hot. Safe-growth rule: a TB caches only
the loop body; do not extend it across the back-edge.

## 20. Open questions for follow-up

Decided for the slice (revisit on measurement): continuation gate = STRICT
(Section 10); hotness N = 1 (Section 6).

Still open:
1. Hotness N beyond the slice: raise if churn appears.
2. Presence-bit parameters: block granularity (64 vs 128) and bit lifecycle on
   invalidation (leave-stale vs refcount-clear) (Sections 8, 13).
3. Invalidation watch granularity beyond page bucket + range test (Section 13).
4. Self-modifying blacklist policy: cooldown window vs permanent, and its size
   (Section 13).
5. Compare-then-invalidate: if/when, with the pre-store-compare ordering trap
   (Section 13).
6. Loop-detection specifics: run-entry-PC tracking; back-edge match run-start
   only vs any earlier PC. Irreducible/scattered loops fall back to the main
   pipeline.
7. Nested loops and multiple back-edges to one head: initial policy is cache the
   simple single-back-edge loop and leave the rest to the main pipeline.
8. Body-membership gate: adopt only if hot loops with taken interior branches
   appear (Section 10).
9. PC<0> canonical representation agreement (Sections 14, 18).
10. Deopt state reconstruction: confirm per-grain commit makes the boundary
    bit-identical against the live back-end (Section 18.2).

## 21. Non-goals

- No host-native code generation (see Under_Consideration_comJIT.md). Grains stay
  interpreted; the win is removed fetch/decode/dispatch on loop re-execution.
- No caching of run-once code (tails, straight-line runs). Only loop bodies.
- No change to EX/MEM/WB semantics or to grain contents.
- No attempt to fix the separate boot halt at 0x60222c (load-base mismatch),
  which is unrelated to this work.
