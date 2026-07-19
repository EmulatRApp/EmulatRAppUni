<!--
EmulatR -- UNDER CONSIDERATION: persisted empirical loop summaries (e-WARP)
Project: EmulatR (Alpha 21264 / EV6 emulator), active tree (emulatrappuniv5)
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Status: UNDER CONSIDERATION -- consideration-only hypothetical, NOT current scope.
No code. Companion to the loop-only TB design note
(20260716_vector_dispatch_tb_region_design.md) and Under_Consideration_comJIT.md.
ASCII(128) only.
-->

# Under Consideration: persisted empirical loop summaries (e-WARP)

Date: 2026-07-17
Status: UNDER CONSIDERATION -- consideration-only. A hypothetical captured for
the record with its landmines mapped. Not scoped, not scheduled, no code.
Discuss-before-code applies if ever pursued.

Companions:
- 20260716_vector_dispatch_tb_region_design.md -- the loop-only TB (the
  substrate: discovery, per-grain execution, read/write tracking, harness).
- Under_Consideration_comJIT.md -- compiled TB units.

## 0. One-line summary and the honest verdict up front

e-WARP would replace the hard-coded WARP idle-loop skip with a DISCOVERED,
fingerprint-guarded, harness-verifiable loop summary learned by faithful
execution and persisted across runs. It is sound for fixed-footprint delay
loops (WARP's class) and it is the only way a DISCOVERED warp can reach
once-per-boot delays. BUT its summarizable class is essentially confined to the
SRM/firmware boot phase; once past SRM into OS guest execution (data-dependent
loops), almost nothing is summarizable, so its marginal value over the faithful
TB collapses. Recorded as a hypothetical, not recommended as scoped work.

## 1. The idea

A loop's effect is a transfer function: entry state -> exit state (plus a cycle
delta). e-WARP learns that function empirically by executing the loop faithfully
ONCE via the TB machinery, records a summary, persists it, and on a later run
applies the summary in place of re-running the iterations -- but only when a
guard confirms the current input matches what was learned.

This replaces WARP's mechanism (hard-coded recognition of specific SRM
rscc-deadline spins by register convention, jumping cycleCount by a hard-coded
formula) with a general, discovered, self-checking one.

## 2. Why persistence is the point (once-per-boot delays)

Within-boot TB discovery can only accelerate loops that RECUR within the boot:
learn on the first occurrence, reuse later. But WARP's real targets are largely
ONCE-PER-BOOT init/calibration delays -- they run exactly once, so within-boot
discovery can never warp them (by the time the loop is "learned" it is over).

Persistence breaks that: learn the loop on boot 1, apply the summary on boots
2+. That is precisely the class WARP hard-codes, reached instead by discovery
plus a cross-run cache. Persistence is not a minor add -- it is the specific
mechanism that lets a DISCOVERED warp cover the once-per-boot delays that
justify WARP's hand-tuning today.

## 3. Mechanism

Built on the TB substrate, which already executes every grain faithfully during
learning and can track what the loop touches.

1. Learn (faithful run via the TB). While executing the loop faithfully, capture
   its READ-SET (registers and memory addresses read) and its WRITE-SET
   (registers and memory written, with final values), the iteration count, the
   cycle delta, and the exit PC.
2. Summarize. summary = { head PC, read-set fingerprint (a hash of the values
   read), write-set (final values), cycle delta, exit PC, validity horizon
   (Section 4c) }.
3. Persist. Write summaries to a cross-run cache, keyed for trust (Section 4d).
4. Replay with a guard. On reaching a head with a persisted summary, recompute
   the current read-set fingerprint. Match -> apply the write-set + cycle delta
   + exit PC (the "precise, empirical warp"). Mismatch -> fall back to faithful
   execution (and optionally re-learn).
5. Probabilistic re-verification. Occasionally, even on a fingerprint match, run
   the loop faithfully and compare the result to the summary; on any divergence,
   invalidate the summary. This makes e-WARP SELF-CHECKING rather than
   assume-and-hope -- the fidelity upgrade over WARP, whose correctness is an
   unguarded hand-maintained assertion.

## 4. Correctness conditions (the landmines)

a. Fingerprint completeness. A summary is faithful only if the current entry
   state matches the recorded one across the loop's COMPLETE read-set. If the
   fingerprint captures every input (all registers and memory the loop reads), a
   match GUARANTEES identical output (deterministic loop, same in -> same out).
   Completeness of the read-set capture is the entire correctness argument; a
   missed input is a silent divergence source (the probabilistic re-verify of
   Section 3.5 is the backstop).

b. Fixed-footprint only. The technique works when the read-set is a stable set
   of registers/addresses -- pure or register-dominated delay loops. When the
   footprint is DATA-DEPENDENT (a copy loop reading src[0..N] and writing
   dst[0..N], the range a function of entry registers), the read-set is itself a
   function of the input, the fingerprint becomes a variable range, and the
   output is data-dependent, so "summarize to a fixed exit state" degenerates to
   "run the loop" -- no savings. Those loops are handled by faithful-fast TB
   execution, not by a summary. e-WARP is a COMPANION to the faithful TB, not a
   replacement.

c. Interrupt horizon (the hard one). Any loop-elision must be bounded by the
   next pending interrupt. If an interrupt was DUE during the elided span,
   faithful execution would have taken it mid-loop at a specific PC; a summary
   that jumps to the exit state skips a delivery that should have happened. So a
   summary carries a validity horizon ("valid for up to T cycles / until the
   next interrupt is due"); if an interrupt falls inside that window, run
   faithfully to the delivery point instead of applying the summary. This is the
   fragile bookkeeping WARP maintains by hand; the faithful TB avoids it entirely
   by taking the interrupt naturally, which is why the faithful TB is the safer
   default and the summary is an optimization layered on top.

d. Determinism / versioning guard. A cross-run cache is a new HIDDEN INPUT and
   must remain a pure accelerator: summary-on vs summary-off must be
   bit-identical (proven by the harness, design note Section 17). This mirrors
   the snapshot kCpuStateVersion concern -- a version-execution guard is required.
   Key each summary by (firmware/code-region hash, EmulatR build version, head
   PC canonical address, mode/ASN); invalidate on any code change, SMC event
   (write-watch, design note Section 13), firmware swap, or rebuild. A stale
   summary applied silently would reintroduce non-determinism through the cache
   -- the exact failure the architecture exists to prevent.

## 5. Scope limitation -- likely boot-phase-only

The decisive practical point. The summarizable class (fixed-footprint,
input-stable delay/init loops) is essentially a SRM/firmware-boot phenomenon:
memory training waits, device settling delays, rscc-deadline calibration spins.
Once execution passes SRM into OS guest code, loops carry data-dependent
footprints (buffer copies, list walks, checksum over variable data, driver
polling with data-varying conditions), and almost nothing meets condition 4b.

Therefore e-WARP is at best a BOOT-PHASE accelerator, not a general runtime one.
Its marginal value over "faithful TB everywhere" is whatever wall-clock the
fixed-footprint boot delays cost that the faithful TB cannot already absorb --
and the faithful TB already removes fetch/decode/dispatch from those same loops,
shrinking the gap. Past SRM the value approaches zero.

## 6. Relationship to WARP and the faithful TB

- Faithful TB (scoped work): covers ALL hot loops -- recurring and one-shot,
  fixed and data-dependent -- by removing front-end cost while running every
  iteration. Always faithful; no elision.
- WARP (current): hard-coded O(1) skip of specific SRM delay spins; a fidelity
  compromise maintained by hand.
- e-WARP (this hypothetical): discovered, guarded, verifiable O(1) skip of
  fixed-footprint delays, persisted across runs. Would let hard-coded WARP retire
  for its boot-delay class -- but only if the boot-time savings justify the
  read/write-set tracking, fingerprinting, interrupt-horizon, and cross-run
  keying machinery, given the Section 5 scope limit.

Most likely resolution: faithful TB is the general answer; for the handful of
extreme-count boot delays, EITHER accept the faithful cost OR keep a minimal
WARP behind a fidelity flag, rather than build the full e-WARP apparatus. e-WARP
is recorded as the "if we ever want discovered+verifiable delay elision" design,
not as a recommendation.

## 7. Decision triggers (if ever)

Pursue only if ALL hold: boot-phase fixed-footprint delay loops dominate
wall-clock even after faithful TB acceleration; their iteration counts are large
enough that O(N) faithful execution is genuinely painful; the read-set is
provably captured completely; the interrupt-horizon bookkeeping is tractable;
and the cross-run version-execution guard is trustworthy. Given Section 5, this
conjunction is unlikely to clear the bar over a plain faithful TB.

## 8. Open questions (deferred with the idea)

1. Read-set capture cost during learning (tracking every read address) vs the
   value of the summary.
2. Fingerprint scheme for a bounded-but-nontrivial read-set at acceptable cost.
3. Interrupt-horizon encoding and the fall-back-to-faithful-up-to-delivery path.
4. Cross-run cache format, keying, and the version-execution guard mechanics
   (mirror snapshot versioning).
5. Probabilistic re-verify rate vs confidence vs overhead.

## 9. Scope boundary

Consideration-only. Commits nothing. The current scope is the loop-only
interpreter TB and its harness. e-WARP is a boot-phase-limited, guard-heavy
optimization recorded so the discovered-warp idea and its landmines are
preserved -- not a roadmap item.
