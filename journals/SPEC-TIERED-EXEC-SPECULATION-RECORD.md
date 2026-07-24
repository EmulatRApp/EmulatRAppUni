<!-- ADR-HEADER
  Title: Tiered Execution, POC Injection, and Performance Investigation - Speculation Record
  Status: PROVISIONAL - reasoning of record, NOT confirmed findings
  Scope: EmulatR V5 performance frontier; POC fast-tier injection; EV6 branch-predictor fidelity gap
  Origin: design session (single conversation), captured for continuity
  Disposition: supersedes the "decode-amortizing TB" fork as the candidate V5 performance
               frontier IF and ONLY IF the go/no-go gate in Section 9 passes
  Conventions: ASCII-128 only. _PROVISIONAL marks any claim not yet confirmed by
               measurement or source inspection.
-->

# Tiered Execution and POC Injection - Speculation Record

## 0. Status and reading instructions

This document is REASONING, not FINDINGS. Nothing here has been confirmed by a
profiler run or by source inspection of the POC. Every performance magnitude,
every ranking, and the entire "why the POC was fast" account is _PROVISIONAL and
may be overturned by the POC folder inspection and by a current-hardware profile.

The single most important discipline for any reader: do not treat the "120 vs
8-12 MiB/s-equivalent" delta, the pre-profile fruit ranking (Section 2), or the
POC-was-flat-and-register-resident story (Section 4) as established. They are
hypotheses of record, written down so a measurement can confirm or embarrass
them. The value of the document is the SHAPE of the plan and the GATES, which are
robust to the numbers moving.

Two facts are load-bearing and known:
- V4 boots SRM to `P00>>>` on DS10, DS20, ES40. V4 is therefore an Oracle: a
  known-correct execution of the guest firmware.
- The POC was abandoned 6-8 months ago. Reason cited at the time: "likely
  exceptions with side effects" that could not be diagnosed. Critically, there
  was no Oracle then. The abandonment was correct given no reference; it is not a
  reason to avoid reopening now, because the missing instrument now exists.

---

## 1. Classification: it is a Translation Block / trace, not a commit

The originally posed pipeline (full trace -> load firmware image -> per-instruction
JSON metadata -> DEC-asm-to-x64 instruction xref -> emit C++ for injection) is
trace-driven ahead-of-time binary translation. It is a TB/trace, keyed on static
guest code layout (or a hot path through it), translated once and cached.

A COMMIT is a different thing and must not be conflated with it. A commit is a
dynamic, per-instruction retire event: the point (stage_WB retirement boundary)
at which architectural state becomes precise. The commit is not the translation
unit; it is the safety rail the translation unit must respect. Every faultable
instruction inside a block needs a commit-consistent recovery point. Correct
label: "TB, gated by commit points." Commit is the anchor, not the alternative.

### 1.1 The DEC-to-x64 instruction xref is the wrong abstraction (REJECTED)

A DEC-asm to x64-native instruction dictionary cannot express the Alpha ISA:
- Alpha has no flags register; compares write GPRs. CMPxx is not one x64
  instruction; the mapping is many-to-many and context-dependent.
- FP was deliberately routed through Berkeley SoftFloat for bit-exactness.
  Lowering FP to native SSE discards that by construction.
- HW_MFPR/MTPR, HW_LD/ST, CALL_PAL, REI have no host analog at all.

If native lowering is ever pursued, lower through an IR (the TCG lesson), not an
N x M per-instruction table. The xref idea is retired.

### 1.2 If native code is emitted, prefer AOT-emit-C++ over a runtime JIT

Emitting C++ and letting MSVC compile it is AOT, debuggable (it is just C++), and
carries no asmjit/LLVM runtime to own. Key the emitter on UNIQUE STATIC PC, never
on dynamic trace position, or the translation units explode for zero benefit.

### 1.3 Fidelity tier of any result-computing block

The moment a block computes an ARCHITECTURAL result natively, it leaves the
cycle-accurate tier: per-cycle occupancy, hazard, and retire-ordering state are
discarded. As drawn, steps 4-5 describe a FUNCTIONAL fast-path (a WARP-family
member), not a faithful one. This is not a criticism; it is where the artifact
actually sits, and it must be LABELED as such rather than billed as the Oracle.

---

## 2. Performance investigation

### 2.1 Decode is _PROVISIONAL NOT the primary overhead

The TB lever's name ("decode-amortizing") must not smuggle in an unproven claim.
Alpha is fixed-width 32-bit RISC - the cheapest possible decode (a few shifts,
masks, a switch). That structural fact CAPS how much decode can dominate.

### 2.2 Pre-profile fruit ranking (_PROVISIONAL, falsifiable)

Most-likely-dominant first, across a representative steady-state window:

1. Host branch misprediction on the central dispatch. Top pick. A single shared
   indirect jump (opcode switch / function-pointer table) that every guest
   instruction funnels through is the textbook interpreter wall: ~15-20 host
   cycles flushed per guest instruction on mispredict, and one shared site
   mispredicts most of the time. Fix (threaded dispatch) is FIDELITY-FREE.
2. CpuState / IPR-file memory traffic (locality). The "3-5 instructions for an
   increment" symptom generalized. Fix is data layout; fidelity-free.
3. Guest memory access path (VA-translate -> TB lookup -> hierarchy). Modest at
   boot; _PROVISIONAL this is the term that GROWS at OS bring-up.
4. Pipeline-advance bookkeeping (BoxResult populate/apply/trace, stage advance,
   hazard/occupancy). Real cost, but load-bearing fidelity - mostly NOT "fruit,"
   it is the cost of being the Oracle. The decode-amortizing TB does not touch it.
5. Decode. Lower than the lever's name implies. See 2.1.

Near-noise: the cycle-counter increment itself (one register-resident add). Most
DECEPTIVE if present: trace/diagnostic emission leaking into a measured build,
which would masquerade as the #1 hotspot. Rule this out FIRST.

### 2.3 The counter-increment / bit-shift question (RESOLVED: not a lever)

Largest unsigned 64-bit value is 2^64 - 1 = 18,446,744,073,709,551,615. At 300
MHz sustained a 64-bit cycle counter wraps in ~1,950 years. Width is a non-issue.

A counter increment is ONE instruction (add/inc: 1 uop, 1-cycle latency). If it
shows as 3-5 instructions, the cost is MEMORY (load-modify-store because the
counter lives in memory / spills), not the arithmetic. Fix is register residency.

A shift is NOT cheaper than an add. add r64,imm and shl r64,1 are both 1 uop /
1-cycle, but add dispatches to 4 ports (~4/cycle) while shl/shr use 2 ports
(~2/cycle): the shift has HALF the throughput. Variable shift is worse. LFSR /
one-hot "shift counting" fails here twice: more uops, and non-monotonic output
that breaks cycle-floor ordering (the exact property the counter exists to
provide). For a flag-free increment use lea rax,[rax+1], not a shift.

### 2.4 Profiling method (Intel host confirmed; desktop is system of record)

Instrument: Intel VTune (free), Microarchitecture Exploration preset for the
top-down tree; Memory Access analysis for per-structure/per-line attribution.
A plain sampling profiler cannot disambiguate the four top-down buckets and will
only confirm "the interpreter loop is hot," which decides nothing.

Top-down bucket -> lever mapping:
- Bad Speculation / Branch Mispredicts -> confirms #1 (dispatch). Fix: threaded
  dispatch (fidelity-free).
- Back-End Bound / Memory Bound / L1-L2 Bound -> confirms #2 (locality). Fix:
  CpuState/IPR field-temperature reordering + GPR-file residency (fidelity-free).
- Retiring high -> genuine work; where the fidelity-tier cost shows up; NOT
  reclaimable without becoming WARP.

Method: two passes. Pass 1 coarse phase attribution over a snapshot-pinned
window (fetch/decode/execute-per-stage/mem/WB-retire/trace). Pass 2 top-down on
the winning phase. Do NOT profile cold-boot-to-prompt as one blob; decompressor,
PAL init, and steady-state console differ, and steady-state predicts OS bring-up.

Traps that corrupt the measurement (cost-no-object resolution: get it right once):
- Build config: profile relwithdebinfo (optimized + symbols). Selective noinline
  on phase-boundary functions so they resolve as distinct symbols; cross-check
  against VTune source-line attribution inside inlined regions.
- Diagnostics: profile with EMULATR_DIAG_* compiled OUT for core truth; profile
  a SECOND time diag-on to answer the separate question of trace cost. Do not blend.
- HW event-based sampling driver / privilege MUST be available, or top-down
  buckets are unavailable and only user-mode stacks are captured.
- Hybrid P/E-core parts (12th gen+) have different PMU event sets: pin the run to
  a P-core (affinity) or the buckets blend two microarchitectures. OPEN: confirm
  whether the desktop is hybrid or homogeneous - decides "set affinity" vs
  "just run."
- Frequency scaling (turbo/SpeedStep) smears cycle attribution; pin frequency for
  clean before/after differentials.

Determinism is leverage: reproducible byte-identical runs permit true
differential profiling (profile, change one thing, profile the identical window,
diff) with no scheduling noise. Exploit ruthlessly.

Home-grown gated timers still fit as a CI-able regression signal (per-window or
per-basic-block brackets, NOT per-guest-instruction), not as the primary instrument.

Cross-check host: the Intel MacBook is a second, likely-homogeneous Intel
microarchitecture. Use it to confirm the RANKING transports (architectural), not
for MAGNITUDES (part-specific) and not for the differential (same-host only).

---

## 3. EV6 branch-predictor fidelity gap

V1 implemented a FAITHFUL EV6 branch predictor (predicted next PC; tournament
predictor family). V4/V5 do NOT model it; next-PC is computed directly. This is a
FIDELITY gap living inside the tier billed as the Oracle - a WARP-tier
simplification that must be documented as such.

Consequences of the gap:
- Cycle timing diverges from silicon on mispredicted branches (~7-cycle EV6
  refill not modeled).
- EV6 mispredict performance counters (PCTR_CTL / PMC IPRs) read ZERO on
  workloads that provably mispredict. This is an architectural register reading
  wrong, not merely a timing approximation.
- Speculative-path side effects (speculative loads, deferred faults on the
  mispredicted path) are not modeled.

Does it matter now: _PROVISIONAL NO for SRM boot and OS bring-up via ATAPI - those
paths do not read mispredict PMCs and do not depend on speculative-path timing.
The ES40/DS20/ES40 boots reaching the prompt are not lying because of this.

Disposition: port V1 source into V5 DORMANT, behind a compile guard
(EMULATR_EV6_BPRED, OFF by default), UNCALLED. It must COMPILE in relwithdebinfo
so it cannot bit-rot unnoticed (uncalled-but-compiled is safer here than excluded;
the opposite of the diag-out-of-release instinct, because we want to catch drift,
not hide it). Re-integration GATED on: a target workload that reads EV6 mispredict
PMCs, OR a golden trace requiring silicon-matched cycle counts. Neither is on the
current frontier.

Status note: BranchPredictor.h/.cpp have been placed in
EmulatRAppUniV5/cBoxLib. A readiness review is queued (see Section 9). Placement
observed by report; files not yet inspected in this environment.

OPEN QUESTIONS (of record):
- Q-BP-1: Does the V1 code model JUST the predictor, or ALSO speculative
  wrong-path execution with squash / precise-state recovery? Sets restoration depth.
- Q-BP-2: Does ANY target workload (SRM, OpenVMS, Tru64 boot) actually read the
  mispredict PMCs? Decides whether this ever graduates from documented-boundary to
  must-fix.

Naming caution: V1's "branch predictor" is the GUEST EV6 predictor (a fidelity
feature). The #1 performance suspect in Section 2 is HOST branch misprediction on
EmulatR's own dispatch jump (a host-microarchitecture problem fixed by threaded
dispatch). Same word, unrelated mechanisms, opposite tiers. Keep them in separate
columns.

---

## 4. POC architecture - what is known, believed, and unknown

CAUTION: the POC is 6-8 months old and its execution architecture is not fully
recalled. The account below is partly _PROVISIONAL and is the primary target of
the reconstruction pass (Section 9). Several downstream conclusions shift if the
archaeology contradicts it.

Believed / reported characteristics:
- Table-driven dispatch, instruction-generated to dispatch files. Implication:
  the fast tier is a GENERATOR whose output can be regenerated against V5
  conventions. The family-dispatch shape and flat call stack are properties of
  EMISSION, controlled by editing the generator - not fragile hand-written
  artifacts. This is the detail that most de-risks injection.
- Per-family dispatchers (branch / integer / load-store / FP / PAL families),
  each holding its own dispatcher routing via function pointers. This distributes
  indirect-branch pressure across N family sites instead of 1, so the host
  predictor sees narrower, more-correlated targets. This is threaded dispatch
  achieved structurally, and it works in MSVC (two-level function-pointer routing,
  no computed-goto / label-address extension required).
- Flat, compressed call stack (measured ~2-5 frames, mostly 2-3). Signature of a
  tight loop not bleeding cycles to call overhead. GOOD news, not a problem.
- Exec-context state materialized as LOCALS within the executing function,
  registers updated in-place across a run of N instructions, synced to the
  in-memory context at BOUNDARIES. This is what let the compiler keep hot state
  (PC, working GPRs, counter) in HOST REGISTERS, avoiding the load-modify-store
  and the aliasing wall. "It seemed efficient" is the mechanistic consequence.

CORRECTION captured mid-session: an earlier worry that the POC passed a single
context pointer BY REFERENCE everywhere (which would trigger the compiler's
aliasing pessimism and force per-instruction spills) was WRONG. The POC held
context as locals in the function; the reference was the boundary object, synced
at edges. That is the aliasing-AVOIDING structure, not the aliasing-incurring one.

UNRESOLVED and important:
- The POC "had a pipeline." The scaffold it used is NOT recalled. This breaks the
  clean "POC = flat/functional, V4 = staged/faithful" dichotomy the rest of the
  session leaned on. If the POC's pipeline was closer to V4's than assumed, the
  register-residency premise weakens and the go/no-go gate may need reframing.
- The abandonment cause ("likely exceptions with side effects") is consistent
  with a POC pipeline that modeled stages but got PRECISE-STATE-AT-EXCEPTION
  subtly wrong - the hardest part of any pipeline model. The old undiagnosable
  bug may live in the POC's exception path. That path is the priority target of
  the reconstruction.

Also _PROVISIONAL: part of the "120" speed may have been BOUGHT with the same
fidelity gap that caused the divergence (fast BECAUSE slightly wrong). Closing
the correctness gap could cost some speed back. The 120 is a real datapoint but
an UNVERIFIED one.

---

## 5. Oracle-based differential harness

The instrument absent 6-8 months ago now exists: V4 defines the correct execution.
The POC's undiagnosable block becomes diagnosable by differential execution
against V4.

Invariant (per Tim's framing, adopted): for deterministic single-stream firmware
execution, the RETIRED PC SEQUENCE is identical between two correct models. If it
diverges, that IS the bug. First divergence localizes to a single instruction.

Why interrupts do not break this in a harness: the only thing that can legitimately
move an instruction boundary between two correct models is an ASYNCHRONOUS event
(external interrupt at a cycle-determined point in V4 vs an instruction-determined
point in the POC). But early SRM boot runs at high IPL with interrupts largely
masked, AND in a controlled harness the interrupt delivery schedule is controlled
/ replayed. Drive both models with the same deterministic schedule and PC-sequence
identity holds including across interrupts. The earlier "resync at interrupt
boundaries" machinery is therefore NOT needed for the first cut.

Diff at the ARCHITECTURAL retire projection only: PC, GPR/FPR file, memory write
set, PS/mode. EXCLUDE microarchitectural state (cycle count, pipeline latches,
timing) - the POC never claimed to model it and diffing it produces false positives.

The defect may PRECEDE the fork: PC forks at instruction K (a branch that tested a
wrong value), but the wrong value was produced at some J < K. PC-divergence
localizes K instantly; state-at-fork (diff registers/memory at K against the
Oracle) tells you what was poisoned, to trace back to J.

Divergence sorting (this IS the fast/slow boundary map, discovered empirically):
- POC BUG: V4's correct result was reachable by a flat computation the POC got
  wrong. Fixable in the POC (or its generator).
- FIDELITY BOUNDARY: the correct result DEPENDED on precise per-cycle/speculative
  state the flat tier cannot produce. Mark as an instruction class that must fall
  through to staged execution.

Unspecified-behavior carve-out: architecturally UNPREDICTABLE results can differ
legitimately. They will not trip the PC tripwire unless one feeds a branch; handle
reactively (allowlist an entry when a divergence traces to an UNPREDICTABLE site),
not by pre-filtering. Keeps the first cut simple.

First-cut harness = a PC-STREAM DIFFER: emit retired-PC from both models, diff the
streams, report first mismatch + state dump at the mismatch. Small tool. Escalate
to full state diffing only AT the divergence point. Cadence: coarse-to-fine -
diff at the boot milestones V4 already snapshots, find the first diverging window,
then lockstep per-instruction only inside that window.

Injection reframing of the harness: if the POC's executor is injected INTO V5 as a
tier (Section 6), the differential becomes SAME-PROCESS (fast tier vs slow Oracle
tier in one program), which is strictly better - a divergence is provably the tier
difference and nothing else, and the harness becomes a permanent V5 `--verify`
capability rather than throwaway tooling.

---

## 6. Injection strategy: tiered execution, not revert

DECISION: inject the POC as a SECOND EXECUTION TIER in V5 (fast path), NOT revert
V5's core to the POC, and NOT run the POC as a separate program. Fidelity stays the
trunk (V4/V5 Oracle-grade slow path); the POC's flat execution is the fast branch;
V5 dispatches between them at runtime and can verify one against the other.

Rationale over revert: revert throws away the Oracle and the fidelity already
built. Injection keeps both and adds the fast path as a VALIDATED tier.

Rationale over separate-program spike: same-process differential (Section 5) is
cleaner and yields a durable `--verify` mode. The fast/slow boundary becomes a
RUNTIME DISPATCH with a runtime fallback rather than an abstract equivalence proof:
clean non-faulting case runs flat/register-resident; fault-prone / PAL-transfer /
interrupt-sensitive classes fall into the staged path; `--verify` checks
continuously against the slow tier.

The generated/table-driven nature (Section 4) makes this a REGENERATION against V5
conventions, not a hand-port. Preserve the two structural properties that produced
the speed: (a) family dispatch (predictable indirect branches, MSVC-compatible),
and (b) flat register-resident state. Both are generator-emitted, so the state-sync
boundary code is emitted uniformly and correct-by-construction.

Hard engineering problem (named, not solved): two tiers sharing one state
representation. The fast tier is fast BECAUSE state lives in register-resident
locals; the slow tier needs state in pipeline latches / BoxResult in memory. The
fast tier must run register-resident across a run of N instructions and MATERIALIZE
to shared state at a boundary (hand-off to slow tier, fault, or interrupt). If
injection forces the fast tier through V5's memory-resident state, it runs at V5
speed and the point is lost. This is the make-or-break and is the go/no-go gate.

Deployment discipline: inject behind EMULATR_FAST_TIER (off by default), same guard
discipline as the BP move.

---

## 7. Lever obviation analysis (contingent on the gate passing)

If the fast-tier injection passes its gate, it reframes the existing levers. The
old frontier ("make the slow core less slow": TB, WARP-as-coping) is superseded by
"how far can the fast tier faithfully reach, Oracle-validated."

- TC / TB: OBVIATED, cleanly. TB's target (hot-path decode cost) is removed from
  the tier it optimized - the fast tier's generated decode is already flat, and the
  slow tier only runs the rare fault case where there is no volume to amortize over.
  TB is not outperformed; it is ORPHANED (no remaining referent). Retire from the
  frontier.

- Performance-WARP: MOSTLY obviated, but NOT to infinity. CORRECTION captured
  mid-session: a fast tier is still a per-instruction interpreter; loop cost is
  LINEAR in guest cycles regardless of tier speed. 120x raises the wall-clock
  threshold at which a loop is worth warping; it does not make a long loop free. A
  loop long enough is still worth skipping however fast the per-cycle is. Census
  per-WARP: re-measure the underlying loop length at fast-tier speed; keep the ones
  that still cross the tolerance line.

- Semantic-WARP: RETAINED. Skips of wall-clock / externally-paced waits survive - a
  faster core spins a wall-clock delay faster but cannot make real time arrive
  sooner. Sort existing WARPs: "unbearable at 8-12" (candidate for obviation) vs
  "waits on host/external time" (retained). Check the ES40 RSCC micro-delay
  specifically: if cycle-counted, the fast tier may let it run; if host-time
  calibrated, WARP stays.

- SNAPSHOT: UNTOUCHED, orthogonal, complementary. It eliminates cycles you do not
  need to run; the fast tier makes cycles you do run cheap. A long loop is still
  better SKIPPED than run-fast, so cycle-elimination and fast execution are
  complements, not substitutes. The lever hierarchy survives.

Fidelity ledger note: retiring a performance-WARP is a fidelity GAIN - every such
WARP is a place EmulatR currently does not faithfully execute. Where the fast tier
can run the region faithfully at acceptable speed, converting WARP -> faithful
execution buys speed AND buys back fidelity. Inverts the usual trade.

Discipline: obviation is EARNED per-instance. Do not delete any WARP or retire TB
on theory. Fast tier runs the region, differential confirms it matches the Oracle,
THEN the lever comes out.

---

## 8. Sequencing decision: fix secondary boot first, do not bifurcate

The $64 question was: fix the secondary boot first, or bifurcate to the POC now.
DECISION: fix secondary boot first.

Reasoning (structural, not preferential): the injection's entire value is the
Oracle differential, which requires the Oracle to EXECUTE the path being validated.
V4 is only an Oracle where it is green. If the secondary boot is broken in the slow
tier, that path has NO Oracle, and injecting the fast tier there recreates the
exact condition that killed the POC (divergence with no reference). Fixing secondary
boot EXTENDS the Oracle's validated reach, which is the prerequisite for validating
the fast tier there. Also: it removes one unknown before adding the injection
unknown, so any later divergence is attributable to the injection alone.

Legitimate parallelism: the injection's Oracle-INDEPENDENT early stages (POC folder
inspection, generator injection, register-residency go/no-go, first differential)
run on the ALREADY-GREEN SRM path and do not need the secondary boot. Start them in
parallel; they cost nothing to begin.

Do NOT extend the fast tier onto the secondary-boot path until the slow tier boots
it. Compounding benefit: every secondary-boot fix strengthens the Oracle BEFORE the
port needs it, and each fix is itself gated do-no-harm against the green DS10/DS20/
ES40 paths, so Oracle coverage only ever grows, never trades.

Flip condition: IF the secondary boot is blocked not by a bug but by a MISSING
MODEL (interrupt/chipset behavior the scaffold lacks), then "fix secondary boot"
and "build scaffold" merge, and it may be more efficient to build that scaffold
once to serve both tiers. Resolved by inspecting what the secondary boot actually
is and why it is stuck.

Scaffold status: mostly in place. Remaining named work: (1) SCSI virtual disk
(HBA + virtual disk), (2) networking (full implementation). Neither gates SRM (the
console reaches >>> without them). Both are past-SRM, for OS bring-up. Sequence
SCSI AHEAD of networking - the stated OS path is ATAPI/disk first, networking
deferred by existing plan. Scope each to what the boot/driver path actually touches
(the ATAPI-harness principle), Oracle-validated against V4 device/interrupt behavior.

---

## 9. Go/no-go gates and queued tasks

GATE-1 (make-or-break for the entire injection strategy):
After injecting the POC generator into V5 and regenerating dispatch files behind
EMULATR_FAST_TIER, read the DISASSEMBLY of a hot generated handler and confirm
guest state stays REGISTER-RESIDENT across a run inside V5's build. If yes,
injection is viable. If no, the fix is in the generator's state-access emission
(propagates to all handlers at once), and injection needs a state-representation
change first. An afternoon; needs no PMU driver.

PRE-GATE (reordered to FIRST by the "POC had a pipeline" corrective):
Reconstruct the POC's execution architecture from the folder BEFORE running
GATE-1 - because what the gate checks depends on what the POC actually was. If the
POC was flatter than V4, the gate is register-residency. If it had a comparable
pipeline, the gate is "can its exception / precise-state handling be made correct
against the Oracle WITHOUT losing the speed" - a different, harder question.
Priority target: the exception / side-effect path (the abandonment cause).

QUEUED TASKS:
- TASK-POC-1: Inspect D:/EmulatR/emulatrPOC. Reconstruct execution architecture
  (pipeline model, scaffold used, staging/latching, exception path). Determine
  copy-complete vs copy-distinct (reported: nothing duplicated). Owner: Cowork
  (live-tree read) or direct upload of core files.
- TASK-POC-2: Re-profile the POC on current Intel desktop hardware (VTune) to
  level-check that the ~120 ceiling is still real. SECONDARY to correctness; the
  fast part was never the doubt.
- TASK-BP-1: Readiness review of EmulatRAppUniV5/cBoxLib BranchPredictor.h/.cpp.
  Answer Q-BP-1 (predictor-only vs +speculative-squash) and Q-BP-2 (does any
  target workload read mispredict PMCs). Confirm it lands dormant behind
  EMULATR_EV6_BPRED, compiled, uncalled. Do NOT wire in; integration gate per
  Section 3. Confirm whether cBoxLib is the final home or a staging spot.
- TASK-PERF-1: First real profile run - relwithdebinfo, diag OUT, snapshot-pinned
  steady-state window, P-core affinity if hybrid, frequency pinned. Confirm or
  overturn the Section 2.2 ranking. Expected primary question: is Bad Speculation
  on the dispatch site the top bucket.
- TASK-SEC-BOOT-1: Determine what the secondary boot is and why it is stuck - bug
  (critical path; fix to extend Oracle) or missing-model gap (merges with
  scaffold; see Section 8 flip condition).
- TASK-SCAFFOLD-1: SCSI virtual disk (HBA + virtual disk), scoped to boot/driver
  path, Oracle-validated. Ahead of networking.

---

## 10. Caveats

Well-structured reasoning reads as more certain than it is. This document is
reasoning of record, not findings. The load-bearing unknowns remain unknown: the
120 is stale and possibly bought with the fidelity gap that broke the POC; the
POC's pipeline architecture is not fully recalled, so the flat-vs-staged framing
may rest on a wrong premise; the obviation analysis is downstream of a gate that
has not run. The folder inspection can overturn a fair amount of this, and that is
the correct order - reason first, then let the tree be dispositive.

What survives whatever the archaeology shows (the robust core):
- The Oracle is what makes reopening the POC legitimate now when it was not before.
- Correctness dominates speed as the question; the fast part was never the doubt.
- Fix secondary boot to extend Oracle reach before porting onto that path.
- Every reclaimed WARP and every retired lever is earned per-instance against the
  Oracle, not assumed.

<!-- END SPECULATION RECORD -->
