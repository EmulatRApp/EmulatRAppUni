# Design: EmulatR V4 SMP secondary-CPU (CPU1) bring-up

Status: DESIGN, tasking-ready. 2026-06-18. Scope: add a second executing
AlphaCPU (CPU1) to EmulatR V4 sufficient for (a) faithful modeling of the
secondary the DS20 firmware expects, and (b) genuine SMP guests (Tru64/VMS).
Grounded in: 21272 HRM (MISC/Cchip), TsunamiCchip.h (current source), and the
just-landed IPI delivery work. Chipset-side SMP scaffolding largely EXISTS;
the gaps are the CPU execution context, the scheduler, and the LDx_L/STx_C
interlock.

VERDICT framing: this is the real S0 SMP task, not an afternoon fix. It is
specified here so it can be tasked in independently-landable phases. It is NOT
the cheapest path to a DS20 `>>>` prompt -- see "Relationship to the DS20
unblock" -- and several phases are BLOCKED on measurements not yet taken.


## Relationship to the DS20 unblock (read first)

The current DS20 cold-boot hang is a busy-poll on memory `0xBFFC` waiting for
`0xCAFEBEEF` (RSCC-confirmed busy-delay, not a sleep). That MAY be a secondary
rendezvous -- in which case this SMP work models the CPU that satisfies it --
or it MAY be a self-handshake / device-completion, in which case SMP is
irrelevant to that hang. THAT IS NOT YET CONFIRMED (see Prerequisite P1).

Two distinct goals, do not conflate:

- Goal A: DS20 boots to `>>>` on a single CPU. If the 0xBFFC poll is
  env-gated, the cheap path is seeding `cpu_enabled=1` in the emulated NVRAM
  default -- NO second CPU, no scheduler, no interlock. This spec is overkill
  for Goal A.
- Goal B: DS20 (and ES40/ES45/DS25) run TWO real Alphas under an SMP guest.
  That requires this spec in full. Here the second CPU CHECKS IN; you do not
  disable it.

"Build SMP and then `set cpu_enabled` to disable the second CPU" is the one
combination that pays the full SMP cost and keeps none of the benefit, and it
is circular (the command writes NVRAM for the NEXT boot; it cannot unblock the
POST you are stuck in). This spec serves Goal B. Run P1 before committing to
either goal.


## Prerequisites -- MUST be measured before the gated phases (P-items block P5/P4)

These are measurements, not code. Building the gated phases without them means
writing a rendezvous responder to a guessed protocol -- a flag-flip stub that
answers the wrong handshake deadlocks mid-protocol, which looks identical to
the current hang.

- **P1 (blocks Phase 5): Confirm and capture the secondary handshake.**
  Data-watchpoint the physical write to `0xBFFC`; let DS20 run to the hang.
  Outcomes:
  - Writer gated on WHAMI/CPU-ID / secondary entry -> it IS the secondary
    rendezvous. CAPTURE THE FULL PROTOCOL: flag location, magic value(s),
    ordering, and any PCS-slot state writes that accompany it. The responder
    in Phase 5 must reproduce every step.
  - Writer is the primary (unconditional, earlier in boot) -> self-handshake;
    SMP does not satisfy this hang (revisit Goal A).
  - No writer in image -> something at runtime is expected to deposit it
    (device/DMA completion target); not rendezvous.

- **P2 (blocks Phase 5, informs Phase 1): Resolve PAL personality.**
  The RSCC decode found VMS personality (`ev6_vms_callpal.mar`), contradicting
  the "OSF/Tru64" assumption. This is unresolved and it determines PCS slot
  layout, the per-CPU console comm-area format, WHAMI semantics, and the IPL
  numbers. Decode which PAL the CONSOLE image runs (OSF vs VMS) before writing
  the PCS responder or fixing IPL values.

- **P3 (blocks Phase 5): Identify the presence-detect source.**
  Find what makes SRM believe socket 1 is populated and therefore attempt
  bring-up. This is the same question flagged at the very start of the IPI
  investigation. Without it you cannot gate Phase 5 correctly (model the
  socket present to attempt bring-up; absent to skip it). Likely candidates:
  HWRPB PCS slot state seeded by the platform model, a TIG/Cchip presence
  bit, or a hardware probe the console runs. Pin which.

- **P4 (blocks Phase 4): Pin IPI delivery values from PAL decode.**
  Already an open VERIFY in the IPI spec: `EI[3]=1<<36` and IPL value+rank are
  provisional. Cross-CPU IPI (Phase 4) cannot be validated until the PAL
  `sys__int_ipi` SCB entry / ISUM nibble test confirms the EI bit and the IPL.
  Decode it. (Storage/set/clear half of the IPI work is already safe to land.)


## Existing scaffolding to build on (verified in source this session)

Do NOT re-invent these; the SMP work threads through them:

- `TsunamiCchip(cpuCount=1..4)` ctor, `m_cpuCount`, `cpuCount()` accessor.
- `read(offset, cpuId)` / `write(offset, value, cpuId)` overloads; MISC reads
  INJECT `CPUID<1:0>` per spec when `cpuId >= 0`. So "which CPU is reading"
  is already plumbed into the chipset; WHAMI semantics hang off this.
- Per-CPU interrupt state arrays indexed by cpuId: `m_dim[]`, `m_iic[]`,
  `m_pendingIrq2[]`, and (just landed) `m_pendingIrq3[]`. `readDIR(cpuId)`,
  `readIIC(cpuId)`, `pendingIrq2(cpuId)`, `pendingIrq3(cpuId)` all exist.
- `miscWriteW1C(writeVal, cpuId)` -- cpuId param plumbed (currently unused);
  IPREQ/IPINTR (just landed) provide the inter-CPU signaling primitive.
- `kMaxCPUs = 4` -- arrays already sized for the full Typhoon/Titan 4-CPU
  case, so ES45/DS25 are covered by the same storage.

Implication: the CHIPSET is ~80% SMP-ready. The missing work is almost
entirely CPU-side and run-loop-side.


## Phase 0 -- Architectural decisions (RESOLVE BEFORE ANY CODE)

### D1 (load-bearing): Scheduling model -- RECOMMEND cooperative deterministic interleave, NOT one-host-thread-per-CPU.

Rationale, in priority order:

1. **Determinism is V4's verification methodology.** The validated
   instruction-deterministic snapshot/resume (the 20-cycle warm-trace
   comparison) and the AXPBox trace-diff oracle BOTH require a deterministic
   instruction interleave. One host thread per CPU introduces nondeterministic
   interleaving that destroys bit-identical resume and makes trace diffing
   impossible. That is not a cost to pay down later; it removes the tools you
   debug SMP WITH.
2. **The LDx_L/STx_C interlock collapses to a synchronous table check.** With
   no real host concurrency, cross-CPU lock_flag clearing is a deterministic
   check at store time -- no host atomics, no ABA, no fences. Phase 3 becomes
   tractable instead of a research problem.
3. **The chipset atomics become belt-and-suspenders.** Under cooperative
   single-thread, `m_misc` CAS and the pending-latch atomics are not
   load-bearing for correctness (no concurrent access), so the threading-model
   question that has been hanging over the chipset review resolves to "atomics
   are harmless overhead, keep or relax at leisure."

Alternative (per-host-thread): only justified if SMP throughput on the host
becomes the goal. It forfeits determinism, requires correct release/acquire on
every shared chipset access (the relaxed-load-claiming-acquire comment becomes
a real bug), and needs genuine guest-memory concurrency control. Do not take
this path for correctness bring-up; revisit only as a post-correctness
performance option behind a build flag.

DECISION: cooperative, deterministic, fixed CPU ordering (CPU0, CPU1, ...),
quantum-based round-robin.

### D2: Interleave granularity / quantum.
Instruction-granular (quantum = 1) during bring-up debug so the retire stream
trace-diffs against the oracle. Configurable coarser quantum (block-granular)
once correct, for performance. Quantum boundary is the commit point: staged
`commitPending` state applies at quantum end, matching V4's existing staged-
commit model.

### D3: Memory + barrier model.
Single shared physical memory. Define MB/WMB/IMB across CPUs: under cooperative
scheduling a barrier on the running CPU is a no-op w.r.t. the parked CPU
(nothing reorders because nothing runs concurrently), but the barrier MUST
still flush that CPU's own staged commits and lock state. Document this so
nobody "implements" cross-CPU barrier ordering that cooperative scheduling
makes vacuous.


## Phase 1 -- Second execution context (CPU1), parked

Objective: instantiate a full second AlphaCPU with independent per-CPU state,
held at reset. Single-CPU behavior unchanged.

Deliverables:
- A second AlphaCPU instance with its own: integer + FP register file, PAL
  shadow registers, ITB/DTB, per-CPU IPRs, decode cache, and commitPending
  staging. No shared mutable CPU state across instances.
- Per-CPU IPR correctness, minimum set: WHAMI returns this CPU's id (wire to
  the same id the Cchip CPUID injection uses); per-CPU PCBB, PTBR, ASN,
  lock_flag, lock_physical_address. Enumerate the full per-CPU vs shared IPR
  split against the 21264 HRM -- do NOT guess which IPRs are per-CPU.
- CPU1 constructed present-but-halted (reset PC parked, not fetching).
- `cpuCount` flows from the platform manifest to both the Cchip ctor (exists)
  and the new CPU-instance count.

Acceptance: with cpuCount=2 and CPU1 parked, CPU0 boots DS10 (single-socket
firmware, no rendezvous) to `>>>` bit-identically to the cpuCount=1 baseline.
Snapshot/resume still bit-identical. This proves the second context is inert
when parked.

Dependencies: P2 (per-CPU IPR layout is personality-sensitive in places).


## Phase 2 -- Deterministic scheduler

Objective: the run loop advances N CPUs per D1/D2.

Deliverables:
- Replace the single-CPU `Machine::run` step with a round-robin over
  `[0, cpuCount)`: each CPU executes one quantum, commits at the boundary,
  fixed order. Parked CPUs (halted) consume zero quanta until released.
- Per-CPU interrupt delivery: the divert polls currently keyed to CPU0
  (`pendingIrq2(0)`, the just-landed `pendingIrq3(0)`, and the b_irq<0/1>
  polls) become indexed by the currently-scheduled CPU. The Cchip already
  exposes all of these per-cpuId; this is wiring the index, not new chipset
  work.
- Console I/O arbitration: getChar/putChar and the EMULATR_CONSOLE_PORT path
  are currently single-CPU. Define which CPU owns the console (primary) and
  how the secondary's console output (its POST status) is routed -- on real
  hardware this is the per-CPU comm area + txrdy/rxrdy, consumed by the
  primary. Minimum for bring-up: secondary console output flows through the
  primary's console via the comm area (Phase 5 models the comm area).

Acceptance: two CPUs executing independent instruction streams advance
deterministically; a snapshot at any quantum boundary resumes bit-identically;
re-running from a fixed seed produces an identical interleaved retire trace.

Dependencies: Phase 1.


## Phase 3 -- LDx_L / STx_C cross-CPU interlock (THE make-or-break phase)

Objective: correct load-locked / store-conditional across CPUs. This is where
SMP lives or dies; every guest spinlock and the rendezvous handshake itself
are built on it.

Deliverables:
- Per-CPU `lock_flag` + `lock_physical_address` (granule-aligned; confirm
  granule size against the 21264 HRM -- typically the cache-block/lock range).
- LDx_L sets the issuing CPU's lock_flag and records the granule.
- STx_C succeeds iff the issuing CPU's lock_flag is still set AND the address
  matches the locked granule; on success it stores and clears lock_flag; on
  failure it stores nothing and returns 0.
- CROSS-CPU INVALIDATION (the crux): any store that writes a granule -- a
  normal STx, a successful STx_C, OR a device/DMA write -- by ANY agent must
  clear the lock_flag of every OTHER CPU holding that granule. Under
  cooperative scheduling this is a synchronous walk of the per-CPU lock table
  at store commit time. No host atomics required (D1 dividend).
- DMA participation: the PCI/Pchip DMA write path must call the same
  invalidation hook (ties to the existing DMA work) -- a device write to a
  locked granule must break the lock, or guest drivers using LDx_L/STx_C
  against device-touched memory corrupt.

Acceptance (build a dedicated micro-test, do not rely on firmware): two CPUs
contend on a single lock_flag-protected word in a tight acquire loop for N
iterations. EXACTLY one CPU holds the lock at a time; total successful
acquisitions == N; no double-acquire (corruption) and no zero-progress
(livelock). Note the failure modes are diagnostic: both-win => invalidation
missing; both-lose/livelock => invalidation too aggressive or lock_flag never
re-armed -- and the livelock presents EXACTLY like the current hang, so this
test must pass before trusting any firmware SMP result.

Dependencies: Phase 1. Independent of Phase 4/5 (can be tested in isolation
with the micro-test).


## Phase 4 -- Cross-CPU IPI delivery (integrate the landed Cchip work)

Objective: CPU-A interrupts CPU-B via the IPI path already wired in the Cchip.

Deliverables:
- Extend the Machine b_irq<3> divert poll from single-CPU `pendingIrq3(0)` to
  per-CPU `pendingIrq3(scheduledCpu)` (folds into the Phase 2 per-CPU divert
  indexing).
- Full round trip: CPU-A writes MISC<IPREQ<12+B>> -> Cchip sets IPINTR<8+B>,
  latches m_pendingIrq3[B] -> CPU-B's next quantum diverts on b_irq<3> at the
  IPI IPL -> CPU-B's handler W1Cs IPINTR<8+B> -> latch clears. The IPREQ SET
  loop is already bounded by m_cpuCount (landed), so multi-target masks work.
- Apply the P4-decoded delivery values (EI bit, IPL, rank) -- replace the
  provisional `1<<36` / IPL 20 with the confirmed constants.

Acceptance: CPU0->CPU1 IPI delivered and acked; CPU1->CPU0 likewise; a
ping-pong (each IPI handler IPIs the other) runs N round trips deterministically
and snapshot/resumes bit-identically across an in-flight (latched, unacked)
IPI.

Dependencies: Phase 2, Phase 3 (handlers take locks), P4.


## Phase 5 -- Secondary bring-up protocol / rendezvous responder (GATED)

Objective: make SRM POST successfully start CPU1 and reach `>>>` on a 2-CPU
config. This is the phase that satisfies the firmware's expectation of a
secondary.

Deliverables:
- HWRPB PCS (per-CPU slot) modeling: slot state fields (present / available /
  context-valid), HWPCB pointer per CPU, and the per-CPU console comm area
  (txrdy/rxrdy or equivalent) the primary uses to talk to the secondary.
  Layout per P2 personality and the Alpha SRM/console architecture -- pin bit
  layouts against the spec, do not guess.
- Presence: report socket 1 populated per P3 so SRM attempts bring-up; make it
  platform-manifest-gated (DS10 = 1 socket present; DS20 = 2) so single-socket
  configs still skip the rendezvous.
- Secondary release: model the mechanism SRM uses to start CPU1 -- set its
  start PC / HWPCB and release it from halt. CPU1 then runs its PALcode
  reset/init and checks in.
- Rendezvous responder: CPU1's init MUST reproduce the EXACT multi-step
  handshake captured in P1 (PCS state transition AND/OR the flag write at the
  captured location/value). A single flag-flip that skips intermediate steps
  deadlocks mid-protocol. If P1 shows the handshake is the 0xBFFC/0xCAFEBEEF
  write, CPU1's bring-up path performs it at the correct point in its init.

Acceptance: with cpuCount=2 and CPU1 modeled, SRM POST prints the secondary
bring-up status line, completes, and reaches `>>>`; `show cpu` reports both
processors present and available. On cpuCount=1 / single-socket manifest,
behavior is unchanged (no rendezvous attempted).

Dependencies: P1, P2, P3; Phases 1-4.


## Phase 6 -- Determinism extension + verification

Objective: preserve the snapshot/resume + trace-diff methodology under SMP.

Deliverables:
- Extend snapshot to serialize ALL N CPU contexts plus the scheduler interleave
  cursor (which CPU runs next, quantum position) so resume is bit-identical at
  any interleave point. Bump the snapshot version. (Note: this also closes the
  latent IPI-latch snapshot gap if the latches are rebuilt from m_misc on
  deserialize.)
- Verification oracle -- CAUTION: AXPBox SMP may be scaffolding more than
  function. VERIFY AXPBox actually EXECUTES a second CPU before trusting it as
  an SMP trace oracle; if it does not, AXPBox is only a single-CPU / presence-
  probe reference, not an SMP oracle. Fallback oracles, in order: (a) the
  firmware's own secondary-POST self-validation (does SRM accept the bring-up),
  (b) a 2-CPU guest OS boot (Tru64/VMS SMP) as the integration test, (c) if
  obtainable, real-hardware console traces.

Acceptance: deterministic re-run produces an identical 2-CPU interleaved
trace; snapshot/resume bit-identical across a cross-CPU IPI and across a
contended lock; SMP guest reaches multi-user / login.

Dependencies: Phases 1-5.


## Risks / cliffs

- **Phase 3 is the cliff.** Cross-CPU lock_flag invalidation wrong in either
  direction is a silent failure: both-win = memory corruption, both-lose =
  livelock indistinguishable from the current hang. The isolated micro-test is
  non-negotiable and must pass before any firmware SMP run is trusted.
- **Rendezvous protocol multi-step (Phase 5).** A stub that answers the wrong
  or partial handshake deadlocks mid-protocol. P1 must capture the FULL
  sequence, not just the final flag value.
- **Personality unresolved (P2).** VMS-vs-OSF changes PCS layout, comm-area
  format, and IPL numbers. Building Phase 5 against the wrong personality gives
  a clean implementation of the wrong protocol.
- **Determinism regression.** Any nondeterministic interleave (the per-host-
  thread temptation) silently breaks snapshot/resume and trace-diffing -- the
  tools used to debug everything else here. D1 exists to prevent this.
- **AXPBox weak as SMP oracle.** Do not assume it executes CPU1; verify first.


## Net / tasking guidance

Phases are ordered by dependency and are independently landable:
- Phase 1 (second context, parked) and Phase 3 (interlock micro-test) can
  proceed in parallel and are the foundation.
- Phase 2 (scheduler) gates Phase 4/5 integration.
- Phase 5 is BLOCKED on P1/P2/P3 measurements -- do not task it until those
  land, or it will be written against a guessed protocol.
- Phase 4 reuses the already-landed Cchip IPI work; only the Machine per-CPU
  poll indexing and the P4-confirmed delivery constants are new.

Run P1 (the 0xBFFC watchpoint) FIRST regardless -- it determines whether this
SMP work even touches the current DS20 hang, and it has been the right next
measurement for several iterations. Everything chipset-side is largely in
place; the new weight is the CPU context, the deterministic scheduler, and the
Phase 3 interlock.
