<!--
EmulatR -- UNDER CONSIDERATION: comJIT (compiled TB units behind an fp-gated list)
Project: EmulatR (Alpha 21264 / EV6 emulator), active tree (emulatrappuniv5)
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Status: UNDER CONSIDERATION -- speculative future direction, NOT current scope.
No code. Companion to the loop-only TB design note
(20260716_vector_dispatch_tb_region_design.md). ASCII(128) only.
-->

# Under Consideration: comJIT

Date: 2026-07-16
Status: UNDER CONSIDERATION. Strategic/architecture direction only. Not scoped,
not scheduled, no edits implied. Discuss-before-code applies if/when pursued.

Companion: 20260716_vector_dispatch_tb_region_design.md (the loop-only TB design
that this would build on). comJIT presupposes that TB machinery exists and is
proven faithful first.

## 1. The idea

Represent a Translation Block's body as an array of function pointers -- call-
threaded code. The spin becomes "call fp[i]" down the array. Each pointer may
target EITHER an existing interpreter grain handler OR a compiled unit
("comJIT unit") implementing the same semantics as native host code. The call
site does not care which; interpreter grains and compiled units coexist behind
one uniform interface, are swapped under measurement, and fall back trivially
(an uncompiled or invalidated slot points back at the interpreter grain).

That uniform fp interface is the entire scaffold. It is the standard on-ramp
from interpreter to JIT, and it lets compilation be incremental and reversible.

## 2. The ladder (and the faithfulness gradient)

- L0 -- switch-dispatch interpreter. The current main pipeline.
- L1 -- fp array of grain handlers. The inner pipeline as call-threaded code.
  Modest win over switch dispatch; this is the substrate. Fully per-grain
  committed, so DROP-IN FAITHFUL: no new faithfulness risk.
- L2 -- per-grain compiled stubs swapped into hot slots. Still per-grain
  call/commit, still drop-in faithful. Limited: an indirect call and a commit
  remain per grain.
- L3 -- whole-body fusion. One native function for the entire loop body: guest
  registers pinned in host registers across the body, addresses strength-
  reduced, redundant CpuState traffic elided, back-edge and continuation test
  compiled in. The fp array collapses to a single pointer that runs the whole
  spin natively and returns next-PC on exit. THIS is the large win.

Faithfulness gradient: L1 and L2 keep per-grain commit and are trivially bit-
identical to the main pipeline. L3 RELAXES per-grain commit (state lives in host
registers mid-body), so it requires precise side-exits (Section 5). The ladder
therefore also lets us bank speed at L1/L2 with zero faithfulness risk and take
on the hard engineering only when reaching for the L3 win.

## 3. Loop-only synergy

comJIT rides the existing TB machinery. Crucially, the back-edge qualifier has
already done L3's hardest prerequisite: it FINDS and DELIMITS the exact hot unit
worth compiling -- a single-entry, proven-hot loop body -- and keys it, gates it,
and invalidates it. A general JIT must guess block boundaries; here the compiler
is handed a clean unit. Loop-only is a genuine on-ramp to comJIT, not a detour.

## 4. Codegen scaffolding (impressions)

- Backend candidates:
  - Copy-and-patch / stencils (recommended sweet spot): pre-compile a stencil
    per grain at EMULATOR build time; at runtime concatenate-and-patch stencils
    into one host function. Codegen cost is near-zero (memcpy plus relocation
    fixups), which fits an emulator that compiles a loop live mid-boot, and it
    still yields most of the fused-native speedup. No LLVM dependency.
  - asmjit: a light C++ x86/ARM assembler; full control, no heavy deps; more
    hand-work than stencils.
  - LLVM ORC: rejected for the inner loop -- compile latency is wrong for
    compiling a loop mid-execution.
- Guest-state ABI: pin a host register to the CpuState pointer; a fused unit
  loads needed guest registers into host registers at entry, computes in-
  register across the body, and commits to CpuState at boundaries / side-exits.
- Data side stays real: loads/stores still do EA generation, DTB translation,
  and memory access; the win is on the instruction stream, not the data stream.

## 5. The hard part: precise faithfulness (side-exits)

L3 fusion relaxes per-grain commit, so a trap (page fault on a load), an
asynchronous interrupt, or a self-modifying store landing MID-body must be able
to reconstruct the EXACT per-grain-committed guest state at the faulting grain --
the state the main pipeline would present at that instruction boundary. That is
precise-exception / side-exit design (the same problem QEMU solves with restore
points). It is where the real engineering goes.

Pure-accelerator discipline (non-negotiable, inherited from the loop-only note):
comJIT must be a pure accelerator. On-vs-off must yield a bit-identical retire
trace; the compile decision (which unit, when) must never leak into guest-
visible state or the timing model. Generated host code is non-architectural
metadata, sequenced so that its presence or absence cannot change guest
behavior. Invalidation extends naturally: the same write-watch scraps a compiled
unit like a grain TB; you additionally manage a host code cache.

## 6. Faithfulness verification -- the layered differential method

Faithfulness is not one property; it is layered, and each layer needs its own
check. Naming the layers (per the architect's framing, extended):

  (i)   instruction result -- the value each instruction computes.
  (ii)  side-effect content -- which registers/memory it writes.
  (iii) side-effect ORDERING and timing -- when, and in what order, effects
        become visible (matters for devices and interrupts).
  (iv)  fault/exception behavior -- whether, where, and with what state a trap
        or interrupt is taken.
  (v)   aggregate machine state -- the resulting architectural state.

The verification is differential co-simulation of two engines (classic vs
comJIT), the SAME harness EmulatR already uses for virtual-vs-silicon /
AXPBox-reference comparison -- a new engine pair, existing machinery.

### 6.1 Layer A -- machine-state snapshot equivalence (the baseline)

The architect's proposal, and the correct baseline. Run the same guest from the
same initial state under classic and comJIT; at checkpoints (every N
instructions, or at TB boundaries) snapshot the FULL architectural state -- all
registers, PC, PAL mode, and touched memory (a memory hash for cost) -- and
compare. Bit-identical means faithful at layer (v). Two modes:

- Lockstep: step both engines and compare each step. Expensive, but pinpoints
  divergence immediately.
- Checkpoint + bisect (matches the "execute n, snapshot, compare" framing):
  run each engine N instructions independently, snapshot, compare; on a mismatch
  bisect the N-window to localize. Cheaper; coarser localization.

### 6.2 Layer B -- retire-trace equivalence (what snapshots miss)

A snapshot AFTER N instructions checks the END state; it does NOT check the
ORDER of effects during those N instructions. Two engines can reach the same
end state via different intermediate orderings. That gap matters for:

- MMIO: a store to a device register acts at the instant it is issued. If a
  fused L3 unit reorders or batches stores (values held in registers), the
  DEVICE sees a different sequence while the CPU end-state may still match --
  a divergence the machine-state snapshot alone can miss. Any loop touching the
  chipset/console/UART must preserve store ordering and timing exactly. (The
  reference copy loop is benign here; MMIO loops are not.)
- Interrupt-delivery point: WHERE in the stream an interrupt is taken sets the
  PC the guest resumes from. If comJIT shifts interrupt-check boundaries, the
  guest diverts at a different PC.

So the strong test compares the RETIRE TRACE per instruction -- PC, register
writes, and memory/IO transactions in order -- not just the end state. EmulatR
already emits retire traces (the RET lines). Trace equivalence catches layers
(ii) and (iii), which the snapshot cannot.

### 6.3 Layer C -- fault/exception equivalence

This is the layer the architect flagged as outside his read; trace-equivalence
plus injection closes it:

- Natural coverage: a trap or interrupt appears IN the retire trace as a diver-
  sion to the handler. If classic and comJIT produce identical retire traces
  including the diversion PCs and the state at diversion, exceptions are faithful
  over whatever a long real boot exercises. No separate exception oracle needed.
- Targeted fault injection (for sparse natural coverage): for a compiled body,
  force a trap at each instruction boundary and an interrupt at chosen points,
  and compare the trap-PC and the reconstructed trap-state against classic
  forcing the same. The body is small and single-entry, so this is bounded and
  exhaustive per unit. This is the direct test of the precise-side-exit
  requirement of Section 5.

### 6.4 Summary of the method

Layer A (snapshot) is the necessary baseline; Layer B (retire-trace) is required
for anything touching devices or interrupt windows and is what actually proves
side-effect ordering; Layer C (natural trace diversions + injection) proves
fault/exception handling. Stand up A and B on the INTERPRETER TB first (classic
vs the L1 fp-list, which should be identical), so the harness is trusted before
any compiled unit exists; then C gates each climb to L3.

## 7. Chaining -- arrives with compilation

Block chaining (linking one unit's exit directly to the next, skipping the
dispatcher) is low value in the interpreter/loop-only world, because the hot
per-iteration back-edge is already dispatch-free via the continuation gate, and
inter-loop transitions are cold. Its value RISES with comJIT: once units are
native, block bodies are fast and inter-block dispatch becomes a larger fraction,
so a direct compiled-to-compiled host jump pays off. It requires predecessor
back-lists to unlink chains when a target is invalidated (self-modifying code),
and per-chain (mode, ASN) gating. Treat chaining as an L3-era optimization,
after fused units and the trace harness exist.

## 8. Decision triggers (when to pursue)

Pursue in this order, each gated on evidence:

1. Only after the interpreter TB vertical slice (birth/entry/spin/invalidation)
   and the Layer A+B trace harness are proven on real loops.
2. L1 (fp-list of grains) is worth doing early -- it costs nothing in
   faithfulness and is the substrate everything rides on.
3. Climb to L2/L3 only if profiling shows the fused-body win justifies the
   codegen backend and the precise-side-exit engineering.
4. Chaining only if, post-comJIT, profiling shows inter-unit dispatch dominating.

## 9. Open questions

1. Codegen backend choice: stencils vs asmjit (recommend stencils).
2. Side-exit representation: per-grain restore metadata vs re-derivation from a
   checkpoint at unit entry.
3. Interrupt/trap check granularity inside a fused unit (per grain boundary vs
   coarser with rollback).
4. Host code cache management: sizing, eviction, and its interaction with the
   pure-accelerator (no timing leak) rule.
5. How far L1/L2 alone close the interpreter gap before L3 is justified.
6. Snapshot granularity and memory-hash scheme for Layer A at acceptable cost.

## 10. Scope boundary

This document is UNDER CONSIDERATION. It commits nothing. The current scope
remains the loop-only interpreter TB and its faithfulness harness. comJIT is a
staged climb to be entered only on the evidence triggers in Section 8, and only
with the pure-accelerator and precise-side-exit disciplines intact.
