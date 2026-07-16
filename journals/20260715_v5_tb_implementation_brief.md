<!--
EmulatR V5 -- Translation Buffer (TB) Implementation Brief
Project: EmulatR (Alpha 21264 / EV6 emulator)
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Origin: promotes EmulatR_TB_Speculation_Record.txt (2026-07-13) + the TB POC
spec into a concrete V5 implementation brief, now that the ES40/DS10/DS20 SRM
prompt milestone is met.  ASCII(128) only.
-->

# EmulatR V5 -- Translation Buffer (TB) Implementation Brief

## 0. What this document is

This is the implementation brief that the Speculation Record (Section 6) said
would be written "once ES40 is settled to SRM boot." That gate is now met: DS10,
DS20, and ES40 reach the SRM `>>>` prompt in the default (ISP) execution mode.
The remaining ES40 silicon-mode LFU-reset delay is a known, characterized edge
case (see the 2026-07-15 LFU handoff), not a boot blocker -- and, usefully, it
is the exact spin shape the TB layer is designed to recognize.

This brief carries the working design forward but does NOT relitigate the
Speculation Record's reasoning; read that first. It records one substantive
refinement to the warp position (Section 4), condenses the settled shape into
build-ordered work, and names the gates that still block code.

Standing rule reminder (applies to all V5 work): discuss-before-code for any
non-trivial change; documentation at header and source line; all diagnostics
behind compile/env gates; ASCII(128) only; best-effort deterministic
architecture, with any determinism trade-off named at the seam.

## 1. Milestone context -- why V5 now

The V4 objective was correctness: a faithful, best-effort-deterministic EV6
that boots real SRM firmware to `>>>` on the Tsunami/Typhoon platform family.
That is met. V4's execution model is a cycle-accurate interpreter (the Oracle):
every instruction is fetched, decoded, and executed through the full pipeline
on every encounter. This is correct and slow (~5-9 MHz effective), and the
slowness is now the dominant cost on exactly the workloads that matter next
(OS install/boot, which is loop-dense).

V5's objective is throughput WITHOUT surrendering correctness: keep the V4
Oracle as the authoritative floor, and add a decode-amortizing Translation
Buffer tier above it, with a native-emission (ComJIT) tier above that later.

## 2. The V4 -> V5 fork

Mechanics: copy the V4 tree to a V5 tree and build the TB infrastructure there.
V4 remains the frozen, shippable correctness baseline and the differential
oracle for every V5 acceptance test.

The load-bearing invariant of the fork:

  V4 instruction execution -- exact semantics AND side-effect ordering -- is
  the DEFAULT and the FLOOR. The TB and ComJIT tiers are accelerators layered
  on top; anything either tier cannot handle faithfully DEOPTS down to V4. The
  Oracle is never the "slow fallback you tolerate"; it is the definition of
  correct against which the fast tiers are validated (Speculation Record 1.1).

Practical consequence: the V5 dispatch loop must be able to enter the V4
interpreter at any committed instruction boundary with fully consistent
architectural state. That "committed state at every boundary" requirement
(Speculation Record 1.6, callout-commits-first) is the single most important
thing to get right early, because both deopt and precise exceptions ride on it.

## 3. Execution model -- three routes, two passes

This section records the model as the architect framed it (three routes, two
passes) and maps it onto the Speculation Record tiers.

Three routes an address can take:

  Route 1 -- Oracle (V4 interpreter).  Always available, always correct. The
  default. Executes serially exactly as V4 does today, side effects and all.

  Route 2 -- TB (decode-amortized).  A straight-line run decoded ONCE into a
  cached block of grains, then dispatched many times without re-fetch/re-decode.
  Same executors as the Oracle -- semantics are executed, not re-expressed --
  so it is faithful almost for free. Every grain that can appear in a block can
  live in a TB.

  Route 3 -- ComJIT (native emission).  A hot TB is compiled to host code
  (x86-64 / ARM64). This is the FIRST tier that re-expresses semantics rather
  than executing them through the interpreter, so it is the first tier where
  faithfulness can break; it therefore owns the inline/callout/terminate
  taxonomy (Speculation Record 1.5) and is folded into the pipeline last.

What a TB holds, and what it does NOT (the register-state-as-property
invariant -- load-bearing, do not blur it):

  A TB caches DECODE, never STATE. Each grain holds an opcode, register indices
  (ra/rb/rc), and an executor pointer -- NOT register values. When the block
  runs, every grain reads and writes the machine's register file across the
  boundary, the SAME file the Oracle uses; nothing about register state is
  cached, spun in the block, or held locally. This is load-bearing: if a TB
  ever held a register value, the loop would freeze at its first iteration's
  inputs and never terminate. The TB caches the decode; the state lives in the
  substrate.

  The terminating branch is INSIDE the block -- it is the final grain -- and it
  re-executes every pass. Its decode is cached and fixed; its OUTCOME is
  recomputed each iteration from whatever the register file currently holds.
  Most passes it resolves back to the block entry (loop, without re-decoding);
  on the final pass it resolves elsewhere and yields to the dispatcher. So the
  model is not "run the block, exit on the branch"; it is "run the block
  INCLUDING the branch, and the branch says where to go next -- usually back to
  the top."

  Cycle cost is ALSO decode-time grain data, not routing data. The modeled cost
  of an instruction is a property of the INSTRUCTION: compute it ONCE at decode
  and carry it on the grain, then apply it IDENTICALLY whether the grain is
  interpreted by the Oracle or dispatched from a cached block. Hard constraint on
  the TB struct -- a dispatched block advances the cycle counter identically to
  an interpreted one BY CONSTRUCTION. Recompute cost in a dispatch-specific path
  and cycle-accuracy silently breaks; on a cycle-accurate emulator that is a
  faithfulness regression, not an acceptable variation. Cheap now, structural to
  retrofit later.

Two passes (the discovery model). NOTE: "two passes" is PER-ADDRESS and
interleaved, NOT two global runs. There is no profiling run and no global
"Pass 1 complete" moment: each PA independently transitions on its OWN first
encounter -- built once, then dispatched on every subsequent hit. The two
passes are the two states an individual address moves through, not two phases
of the run. (Implementing "two passes" literally as a profiling run followed by
an execution run would build a phase that does not need to exist.)

  Pass 1 -- execute-and-discover.  Run serially on the Oracle exactly as V4
  does, AND concurrently capture the physical addresses (PAs) that qualify as
  TB entry points -- proven instruction boundaries, each block ending at the
  first TERMINATOR (the terminator set of Section 7 -- NOT the pure/impure
  eligibility ruleset, which governs Route 3 only). At Route 2 every instruction
  is TB-eligible, so discovery needs only boundaries and terminators. Pass 1
  produces a registry of valid TBs; it changes nothing about how the
  instructions execute or what they do.

  Pass 2 -- dispatch-from-TB.  On re-entry to a discovered, still-valid PA,
  dispatch the cached block instead of re-decoding. The Oracle remains the
  fall-to path on any miss, any anchor mismatch, or any deopt.

ComJIT is a later promotion of hot Route-2 blocks, gated behind a much higher
hotness threshold; it rides the SAME invalidation substrate the TB tier proves
out (Speculation Record 1.2). Default at every level remains Route 1 / V4
semantics and side effects.

## 4. The lever hierarchy -- eliminate, skip, cheapen

CORRECTION (2026-07-15, architect): an earlier draft of this section made
faithful-fast execution the default and demoted warp to an escape hatch. That
inverts the priority for a spin, and it rested on a category error about what a
TB buys. Rewritten to state the economics correctly.

What a TB actually saves. TB removes repeated DECODE, not EXECUTION. For a block
of n instructions run k times before invalidation:

    Oracle:  k*n*D  +  k*n*E     (decode every pass + execute every pass)
    TB:        n*D  +  k*n*E     (decode ONCE   + execute every pass)
    Saved:  (k-1)*n*D

The k*n*E term -- the executions -- is untouched. TB is a CONSTANT-FACTOR win
bounded by the decode fraction of per-instruction cost. If decode is ~1/3 of
that cost, TB's ceiling on a loop is roughly 1.5x wall time: a one-hour spin
becomes forty minutes. For a multi-billion-cycle loop a constant factor is not
a rescue -- billions of cycles times a cheaper dispatch is still billions of
dispatches. TB does not touch the order of magnitude.

The order-of-magnitude tools are the ones that make the cycles NOT HAPPEN. The
clean hierarchy, largest lever first:

  SNAPSHOT -- ELIMINATES the cycles. Restore committed state at entryPa and the
  billions of cycles never execute at all. A load-phase substitution; nothing to
  do with TB. We already built the instrument this session:
  firmware/es40_v7_3.axpsnap (the FAST_DECOMPRESS entry snapshot). Applies to a
  deterministic one-shot region whose output is a fixed function of its inputs
  (the decompressor is the canonical case). Multi-order-of-magnitude.
  NAMING (resolved 2026-07-15 -- a V5 task; V4 is FROZEN and is NOT touched):
  the V5 artifact is `<firmware>.snap`, e.g. es40_v7_3.snap. The extension `.snap`
  is set by the single constant kSnapshotExtension; V4 also has two stray
  hardcoded ".axpsnap" literals (main.cpp entry-snapshot path ~line 295;
  Machine.cpp predig-oemsnap name ~line 1627) that do NOT go through the constant.
  In V5, fold both through kSnapshotExtension so it is the sole source, then set
  it to ".snap". Stem stays lowercase, mirroring the firmware file
  (`<firmware>.rom` -> `<firmware>.snap`, same stem) -- the "ES40_v7_3" casing in
  an earlier note was the platform name typed casually, not a spec. Why not keep
  `.axpsnap`: it reads as an AXPBox-format snapshot, but EmulatR is the PRIMARY
  Oracle and AXPBox is secondary; the AXPBox cache layout (console at PA 0, entry
  0x8000) is the Realm-2 model we rejected as boot-corrupting. This artifact is a
  complete machine state at entryPa (Realm-1) -- a different object; name it for
  what it is. The existing V4 es40_v7_3.axpsnap is left as-is (V4 frozen); the
  rename to .snap rides the V5 build that mints/reads `.snap` (rename in place, no
  re-mint -- the bytes do not depend on the name).

  WARP -- SKIPS the cycles. For a spin/busy-wait -- reads a counter or IPR,
  compares, branches back, NO other architectural side effect -- do not execute
  it a billion times fast; recognize the shape, compute where it lands, and jump
  the counter. Gating such a spin faithfully is architecturally perfect and
  performance-useless (it calls out millions of times). The krn$_micro_delay /
  RSCC family (Speculation Record 1.7). One order of magnitude and up.

  TB -- CHEAPENS the cycles. Removes decode overhead per instruction. The
  smallest lever of the three by a wide margin, and the only one that helps a
  loop doing REAL WORK (the cycles must happen; TB just makes each cheaper).
  ComJIT extends this by re-expressing pure grains as native code -- a larger
  constant factor -- but it still executes the cycles.

THE DIAGNOSTIC QUESTION that decides the tool entirely: is the loop a SPIN, or
is it doing WORK?

  - Spinning on a counter/IPR, no other architectural side effect -> WARP. Wall
    time collapses; TB is the wrong tool.
  - Genuinely computing (copy, fill, init, build tables, checksum) -> the cycles
    are real work that must happen. TB gives its constant factor; the only large
    win is a SNAPSHOT past the whole region IF the region is deterministic and
    its output is a fixed constant.

Applying this to what we are staring at:

  - The ES40 LFU delay at 0x6a4f8-0x6a520 IS a spin: RSCC read (CALL_PAL 0x9d
    via stub 0x1b78e8 -> PAL handler 0xb740), CMPLT compare, branch back, no
    other architectural side effect in the body. Therefore WARP is the answer,
    not TB. TB would shave a constant factor off a ~10^10-cycle wait -- useless.
    The PAL-mediated counter read makes faithful gating EXTRA expensive (a PAL
    callout per pass), which only reinforces warp over execute-fast.
  - The decompressor is the SNAPSHOT case, already built (es40_v7_3.axpsnap):
    restore at entryPa and the decompression cycles never run.

Where the determinism virtue actually lives. Executing every cycle faithfully
(TB, then ComJIT) preserves the exact cycle trace -- a real, valuable property,
but it matters for REAL-WORK loops, where the cycles must happen and you want
them cheaper without diverging from the Oracle. It is NOT a substitute for warp
on a spin: no per-cycle speedup rescues a billion-cycle spin. This RE-AFFIRMS
Speculation Record 1.7 rather than correcting it -- warp is the answer for the
spin; TB is orthogonal, for work.

Where TB and warp still meet. The TB layer remains the natural HOST for the warp
RECOGNIZER -- TB identity (physical anchor + stable block shape) is where the
spin shape becomes reliable enough to trigger on, replacing today's brittle
PC-hardcoded, quarantined RSCCWARP. That is a HOSTING relationship, not an
economic one: the TB does not make the spin cheap; it is the stable place from
which warp decides to skip it.

## 5. The dispatch core -- split key (accelerator vs anchor)

From Speculation Record 1.3. The single lookup tuple does two jobs; split them.

  Lookup accelerator (fast candidate find):  ( Virtual PC, ASN, PAL-mode ).
  Validity anchor (prove candidate is still that code):  ( Physical page,
  Page generation ).

Dispatch MUST gate on the anchor, never the accelerator alone. This makes ASN
recycling, virtual aliasing, and PTE remap all collapse to a single
"generation/anchor mismatch -> rebuild" path -- which is the concrete reason the
anchor is PHYSICAL, not virtual. PAL-mode lives IN the accelerator (same numeric
address in PAL vs native is a different program), not merely as a terminator.

## 6. Invalidation substrate -- one shared, proven at TB tier first

From Speculation Record 1.2. TB and ComJIT are invalidated by the SAME events;
ComJIT just has more to discard. Prove invalidation at the TB tier against the
real SRM boot BEFORE layering ComJIT on the same substrate -- a ComJIT bug on a
wrong substrate is native code caching a stale answer, strictly harder to debug.

The SRM boot is deliberately the validation workload for this substrate: it
exercises decompression, firmware relocation, PAL transitions, and MMU/vector
setup end-to-end. It is a CORRECTNESS workload and a near-worthless PERFORMANCE
workload -- do not expect aggregate speedup from SRM; expect proof that the
dirty-page / generation / IMB machinery survives.

## 7. ComJIT (Route 3) block eligibility -- the pure/impure ruleset (condensed)

SCOPE (critical -- Speculation Record 1.5): this ruleset governs ROUTE 3
(ComJIT) ONLY. At Route 2 (TB) the RESIDUE IS EMPTY -- every instruction is
TB-eligible, because the TB executes the SAME executors as the Oracle and does
NOT re-express semantics, so there is nothing to get wrong. A load is an
ordinary grain; LD_L/STx_C is an ordinary grain; CALL_PAL is an ordinary grain.
At Route 2 only TERMINATORS matter, and terminators END blocks -- they do NOT
EXCLUDE them. The pure/impure split below is a property of the COMPILED tier,
where semantics are re-expressed as native code and impurity can diverge from
the Oracle; do NOT apply it to TB discovery (Section 3, Pass 1).

Why this scope line is load-bearing: apply the pure/impure filter to the TB
tier and the decompressor loop (load/store/increment/branch) is disqualified,
every memcpy/memset/table-fill is disqualified, and the CALL_PAL-straddling LFU
spin is disqualified. The TB tier would have nothing to cache (register-only
blocks are a rounding error in real firmware); the invalidation substrate
(Section 6) would never be exercised over relocation code because no TB exists
there; and Section 8's first milestone would be blocked by its own eligibility
rule (no TB identity for the warp recognizer to key on). The residue-empty rule
at Route 2 is what makes all of that work.

From journals/jit_qualifying_ruleset.md. One principle (Route 3): a block is
COMPILE-qualifying iff every instruction in it is PURE -- its only observable
effects are (1) result values to registers and (2) modeled cycle cost.

  TIER A (qualifying, pure): non-trapping integer arithmetic (INTA), logical /
  cmov (INTL), shift / byte-manip (INTS), non-trapping multiply (INTM), count
  ops, and address arithmetic LDA/LDAH (register math despite the "LD"
  mnemonic).

  TIER C (disqualifying -> reject block, interpret): all memory loads/stores
  (EA may be MMIO; ordering; LL/SC lock flag), HW_MxPR/HW_LD/HW_ST/HW_REI,
  CALL_PAL (all), trapping /V arithmetic, all FP (FPCR state) until FBOX is
  solid, barriers/serialization (MB/WMB/IMB/TRAPB/EXCB), side-effecting reads
  (RC/RS/ECB/WH64/FETCH), and RPCC (dynamic cycle state) in the first cut.

  TERMINATORS (end block, do NOT disqualify): direct control flow (BR/BSR/
  conditional branches -- statically resolved, permit block linking) and
  indirect control flow (JMP/JSR/RET -- registrable body, but exit is a
  dispatch-cache lookup, no static link).

  Entry validity: 4-byte aligned, proven instruction boundary, a real
  control-flow target; reject +N labels (architecturally impossible as code).

The registration predicate and the verify gate (a block is not "qualified"
until proven equivalent to the Oracle over the same input state) are in the
ruleset doc verbatim; V5 implements them as written.

## 8. First validation target -- the ES40 LFU spin (Shape 4)

The ES40 silicon LFU-reset delay is the natural first shape-recognition target:
it is the POC's SHAPE 4 (spin-on-counter) and the Speculation Record 1.7 case,
now with a real address and a real wrinkle to test the machinery against.

  - Loop body: 0x6a4f8-0x6a520. Reads RSCC via CALL_PAL 0x9d (stub 0x1b78e8 ->
    PAL handler 0xb740), CMPLT compare at 0x6a514/0x6a51c, conditional branch
    back at 0x6a520. No other architectural side effect in the body.
  - Wrinkle vs a clean Shape 4: the counter read is PAL-mediated, so the spin
    straddles a CALL_PAL (a TERMINATE per 1.5). The recognizer must key on the
    COMPOUND (delay block + RSCC PAL stub), not a single straight-line block.
    This is the concrete case that tells us whether the recognizer works on
    real firmware or only on the POC's idealized shape.
  - Success criterion (WARP, per Section 4): the recognizer identifies the
    compound spin shape at TB identity, computes the landing (the deadline the
    loop waits on), and jumps the counter -- the billions of cycles are SKIPPED.
    The LFU reset then reaches outtig(0xE00004) and completes to `P00>>>` in
    silicon mode. Success is reaching the reset, NOT any per-cycle speedup: TB
    decode-amortization would only shave a constant factor off a ~10^10-cycle
    wait and is explicitly not the tool here.

Note the ES40 silicon LFU hang does NOT need to be fixed to start V5 -- it is
fixable independently in V4 with a coherent deadline-warp, and that same warp is
what the V5 TB later HOSTS via shape-recognition. It is the ideal first
warp-recognition exercise for V5, not a faithful-fast one.

## 9. Open _PROVISIONAL gates -- resolve against primary sources first

These are from Speculation Record Section 3 and must NOT be laundered into
decisions. Each blocks a specific piece of the substrate.

  3.1 IMB-clean self-modification: is deferring invalidation to IMB / PAL
      boundaries faithful (Alpha has no I/D coherence guarantee)? Verify against
      PALcode source + 21264 HRM I-cache/IMB section for the relocation and any
      self-patching sequences. Blocks: the invalidation trigger model.

  3.2 Async memory observer across MB: on a single-threaded guest may MB relax
      toward a near-no-op? Verify against the EmulatR device/DMA model + the
      interrupt-delivery path. Blocks: MB gate shape (fence-action vs relaxable).

  3.3 TLB shootdown encodings + per-OS PAL handshake: settled shape is PTE write
      -> MB -> PAL TBI -> IPI -> remote invalidate/ack; UNVERIFIED are the exact
      TB-invalidate IPR set and which PAL entry points OpenVMS vs Tru64 drive.
      SMP-only; single-CPU SRM never exercises it. Verify against the ARM TB-
      management section + PAL source. Blocks: SMP shootdown (forward-looking).

  3.4 Firmware entry premise (load at 0x8000): does not affect TB economics;
      affects which pages are exec-tracked from instruction one. Verify against
      how EmulatR actually enters firmware (SROM/reset path).

## 10. Forward-compat seams -- build the slot, not the machinery

From Speculation Record Section 2. Cheap now, expensive to graft later:

  2.1 Reverse-dependency edge for block chaining: not chaining yet, but reserve
      the back-reference slot per TB so invalidation can later unlink chained
      predecessors.
  2.2 Per-CPU translation front-end, shared physical-keyed block store: keep the
      block store keyed on (physical page, generation) so it is shareable across
      CPUs, but make the virtual-PC -> physical-anchor resolution consult a
      per-CPU (private TLB) front-end. This is what makes SMP shootdown faithful
      for free later. Build the SEAM (per-CPU front-end + physical-keyed store)
      now; build shootdown machinery only when an SMP guest asks.
  2.3 Hotness-gated promotion: TB build may be eager for simplicity now, but
      leave a hotness gate droppable in front without restructuring. ComJIT
      hotness gating is MANDATORY -- never codegen once-run code.

## 11. Recommended sequencing -- POC-first vs infrastructure-first

An honest flag, because it is a real decision and the project's own discipline
speaks to it. The TB POC spec is explicit that the POC is a DISPOSABLE
MEASUREMENT INSTRUMENT answering two questions before infrastructure is
committed:

  Q1 (economic): does decode-once/dispatch-many beat re-decode-every-time, and
     by how much per workload shape (the k-amortization curve, including hash /
     anchor-check / bookkeeping overhead)?
  Q2 (mechanical): does anchor-checked dispatch + deferred batch invalidation
     work without thrash on a relocation shape, at an anchor-check cost small
     enough not to eat the decode saving?

Recommendation: run the POC measurement FIRST, but keep it OFF the V5 tree.
Branch it from the v4-beta tag as `tb-poc` -- same Oracle, same firmware, same
captured DS10/DS20/ES40 boot traces -- so its disposable, possibly-hanging code
never sits in a tree meant to live and there is zero merge temptation ("just
keep it" must not be one decision away). The POC's whole value is that it breaks
alone and gets deleted when Q1/Q2 are answered; its numbers, not its code, drive
the production design. Fork V4 -> V5 in parallel for the production tier, but do
NOT commit the production TB struct/lifecycle until the POC's k-amortization
curve is in hand -- skipping it means building infrastructure on an unmeasured
assumption, the one thing the POC exists to de-risk. If the measured curve is
favorable (very likely for the loop-dense install/boot workloads), proceed into
the production TB tier with the seams of Section 10 in place.

## 12. Concrete first steps for the V5 fork

1. Fork the tree: V4 -> V5. Freeze V4 as the differential oracle. Wire the V5
   acceptance gate to diff V5 boots against V4 (byte-identical cycle trace on
   the Oracle path; that is the floor before any TB is enabled).
2. Land the committed-state-at-every-boundary contract (Speculation Record 1.6)
   in the V5 dispatch loop -- the prerequisite for deopt and precise exceptions.
3. Build the POC measurement instrument (Section 11) against captured DS10/DS20/
   ES40 boot traces; produce the k-amortization table and the thrash/no-thrash
   verdict.
4. In parallel, resolve gates 3.1 and 3.4 against primary sources (they block
   the invalidation trigger and exec-page tracking; 3.2/3.3 are later/SMP).
5. Stand up the Route-2 TB tier: physical-keyed, generation-anchored block store
   with a per-CPU resolution front-end (seam 2.2) and the reverse-dep slot
   (seam 2.1). Discovery in Pass 1, dispatch in Pass 2, Oracle as fall-to. Carry
   cycle cost ON THE GRAIN (Section 3 invariant) so dispatch advances the counter
   identically to interpretation -- do not recompute cost in the dispatch path.
   TB discovery keys on boundaries + terminators only; the pure/impure ruleset
   (Section 7) is NOT consulted until the ComJIT tier (step 7).
6. Implement the compound spin-shape recognizer on the ES40 LFU loop and host
   the coherent deadline-warp behind it (Section 8) -- the first warp-recognition
   exercise. Success is reaching outtig(0xE00004) / P00>>> in silicon mode, not
   a per-cycle speedup. (The decompressor's order-of-magnitude win is the
   snapshot restore, already built -- not a TB concern.)
7. Only after the TB invalidation substrate is proven against the full SRM boot:
   design the ComJIT tier on the same substrate, hotness-gated, with the 1.5
   inline/callout/terminate taxonomy.

## 13. One-line summary

V5 = V4 (frozen correctness floor / Oracle) + a physical-anchored, decode-
amortizing TB tier discovered in a first pass and dispatched in a second. TB
CHEAPENS real-work cycles by a constant factor -- it does not remove executions,
so it is the smallest of three levers: SNAPSHOT eliminates cycles (decompressor;
es40_v7_3.axpsnap already built), WARP skips them (the confirmed ES40 LFU spin),
TB cheapens them (real-work loops). The TB is warp's stable HOST, not its
replacement; ComJIT is deferred until the shared invalidation substrate is
proven against SRM.
