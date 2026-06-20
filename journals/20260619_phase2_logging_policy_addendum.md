================================================================================
EmulatR V4 -- AlphaCpuAgent (Phase 2) DESIGN ADDENDUM -- per-CPU logging policy
================================================================================
Written: 2026-06-19.  Status: DESIGN, discuss-before-code.  Addendum to
20260619_alphacpuagent_phase2_ownership_lift_design.md.  Decides how logging and
tracing behave once CpuState is per-agent and a second CPU is on the roadmap.

--------------------------------------------------------------------------------
PURPOSE
--------------------------------------------------------------------------------
The ownership lift makes CpuState per-agent.  Logging is a cross-cutting concern
the base design does not address, and it is exactly the kind of seam that
quietly re-encodes the CPU0-as-sink assumption Phase 2 exists to remove.  This
addendum settles: ONE coalesced stream vs per-CPU files, where the sink lives in
the ownership boundary, and what must be done NOW (single agent) to avoid format
churn at the byte-identical gate when the second CPU lands.

--------------------------------------------------------------------------------
THE GOVERNING PRINCIPLE
--------------------------------------------------------------------------------
  per-CPU is a property of the DATA, not of the DESTINATION.

  The agent TAGS its output with who it is (cpuId).  The sink stays SINGULAR and
  system-owned.  This keeps the global retire order intact, keeps cross-CPU
  events coherent, and keeps logging on the correct side of the CPU0-as-sink
  line -- the same split Phase 2 draws for the clock (per-CPU PCC is agent-owned;
  the system clock is dispatcher-owned).  Logging mirrors that exactly:
  per-CPU trace CONTENT is agent-emitted; the trace SINK is dispatcher-owned.

--------------------------------------------------------------------------------
TWO CHANNELS -- they are different artifacts and want different rules
--------------------------------------------------------------------------------

(1) TRACE SINK  (the retire-granular, AXPBox-comparable stream)
    DECISION: COALESCED.  One single, totally-ordered stream.  Every line tagged
    with cpuId AND the global retire ordinal.

    WHY this is a CORRECTNESS requirement, not a preference:
      - The verification methodology is the trace diff against AXPBox and the
        byte-identical boot gate.  A trace is an ORDERED artifact; the gate
        compares an ORDERING.
      - Under cooperative deterministic interleave (quantum round-robin, single
        guest thread) there is a well-defined GLOBAL order of retires across
        CPUs: agent0's quantum, then agent1's, then back.  That global order is
        what the gate compares.
      - An ordering only EXISTS if all CPUs write into one sequence.  Two
        per-CPU files force the diff to reconstruct interleaving from timestamps
        or cycle values -- reintroducing the timebase-conflation ambiguity
        Phase 2 removes, and becoming NON-deterministic when two CPUs share a
        cycle value.
    => Coalesced-with-cpuId-tag is mandated by the oracle.  Per-CPU trace files
       are disallowed for the authoritative trace.

(2) DIAGNOSTIC / OPERATIONAL LOG  (spdlog; SPDLOG_ACTIVE_LEVEL=TRACE channels:
    IRQ / timer / flash / boot-phase / bring-up messages)
    DECISION: SINGLE sink, cpuId FIELD in every record, split BY THE READER.

    WHY:
      - These are NOT the comparison oracle, so the determinism constraint does
        not bind here.
      - Per-CPU SEPARATION is useful for READING (follow CPU1 bring-up without
        CPU0 interleaved), but that is a read-time concern.
      - One sink + a cpuId field (spdlog MDC / pattern flag) + filter-on-read
        gives BOTH: the single-stream interleave when you need ordering, and
        per-CPU views when you need one core's narrative -- without N file
        handles and without the "which file owns a cross-CPU event" problem.

--------------------------------------------------------------------------------
WHY PHYSICAL PER-CPU FILES ARE A TRAP (the cross-CPU event tell)
--------------------------------------------------------------------------------
An IPI (0 -> 1), a Cchip interrupt delivery, a cross-CPU reservation
invalidation (Phase 3) -- these events ARE ABOUT TWO CPUs.  In a per-file scheme
they are either duplicated into both files or arbitrarily assigned to one;
either way the ability to see the event ONCE, in its true global order, with
both CPU ids visible, is destroyed.  A single tagged stream renders such an
event once, in order, with both ids present.  Cross-CPU events are the entire
content of Phases 3/4, so a per-CPU-isolated log design would be designing
against the roadmap.

--------------------------------------------------------------------------------
OWNERSHIP PLACEMENT (fits the Phase-2 boundary)
--------------------------------------------------------------------------------
  SHARED (Dispatcher-level service, one instance) -- alongside GuestMemory, the
  chipset, the system clock:
    - The TRACE SINK.  Agents EMIT into it (per-CPU data); they do NOT OWN it.
    - The diagnostic spdlog sink(s).
  AGENT-OWNED (per-agent):
    - Nothing new for logging.  The agent owns its CpuState and emits trace/log
      records tagged with cpuId(); it holds no sink.
  This is the same shape as the cycleCount split: per-CPU PCC is agent-owned,
  the system clock is dispatcher-owned; per-CPU trace CONTENT is agent-emitted,
  the trace SINK is dispatcher-owned.

--------------------------------------------------------------------------------
FORMAT / EMISSION RULES
--------------------------------------------------------------------------------
  - Trace line carries, at minimum: <global_retire_ordinal> <cpuId> <pc> <...>.
    The global retire ordinal is the dispatcher's monotonic count across all
    agents (the order the gate compares), NOT any per-CPU cycleCount.
  - Diagnostic record carries a cpuId field via the spdlog pattern/MDC.
  - cpuId is sourced from CpuState::cpuId() (already present; mCpuId via
    setCpuId()).  No new per-CPU identity machinery is introduced.
  - The sink is written single-threaded (cooperative interleave) -- no logging
    lock needed today.  Under a future threaded driver, sink writes occur at the
    sync boundary (with the staged effects), NOT concurrently from agents; the
    sink stays a serialization point.  (Flagged for Phase 3+, not built here.)

--------------------------------------------------------------------------------
DO THIS NOW (single agent) -- forward-compat, zero behavior change
--------------------------------------------------------------------------------
  Put the cpuId tag into the trace and diagnostic formats NOW, in Phase 2, while
  only agent0 exists.  Emitting "cpu0" (or cpuId 0) on every line today costs
  nothing and means the FORMAT DOES NOT CHANGE when the second CPU lands.  This
  matters because a format change at the moment the second CPU appears would
  perturb the byte-identical gate exactly when concurrency is also being
  introduced -- the worst possible time to move two variables at once.  Tag from
  the start; the second CPU then emits "cpu1" into the same stream with no
  format churn and no gate perturbation.

  NOTE: adding the cpuId tag DOES change the trace text, so it must be folded in
  at a gate boundary: re-baseline the AXPBox/legacy reference WITH the tag
  present (a one-time reference refresh), then the byte-identical gate holds
  against the tagged format thereafter.  Do this as part of STEP 1 (the
  systemNow() pure refactor) so the re-baseline happens once, early, before the
  structural cuts.

--------------------------------------------------------------------------------
ACCEPTANCE (logging policy done)
--------------------------------------------------------------------------------
  - Trace sink is a SINGLE dispatcher-owned stream; no per-CPU trace files exist.
  - Every trace line carries cpuId + global retire ordinal; reference re-baselined
    with the tag; phase1_dispatch_gate.sh PASS against the tagged format.
  - Diagnostic logging is a SINGLE spdlog sink; every record carries a cpuId
    field; per-CPU views are produced by read-time filtering, not by separate
    writers.
  - cpuId is sourced from CpuState::cpuId(); no new per-CPU identity machinery.
  - The trace sink and diagnostic sink are placed as Dispatcher-level shared
    services (grep clean: no sink owned by AlphaCpuAgent).
  - Single agent emits cpuId 0 today; format is second-CPU-ready (verified by a
    dry-run emitting a synthetic cpu1 line and confirming the format is stable).
  - determinism_equivalence + parked_agent_no_deadlock stay GREEN (logging adds
    no nondeterminism: single-threaded sink, content is a pure function of the
    deterministic retire order).

--------------------------------------------------------------------------------
RISKS / WATCH-OUTS
--------------------------------------------------------------------------------
  - Re-baseline timing: the cpuId tag changes trace text, so the reference MUST
    be refreshed once (STEP 1).  If skipped, every subsequent gate shows a
    spurious diff on the tag column and masks real findings.
  - Do NOT let the global retire ordinal be any CPU's cycleCount -- that is the
    CPU0-as-clock bug in the trace.  It is the dispatcher's cross-agent monotonic
    count (same source of truth as systemNow() ordering, distinct from PCC).
  - Threaded driver (future): agents must not write the sink concurrently.  Sink
    writes belong at the sync boundary with staged effects.  Building the sink as
    a shared serialization point now keeps that door open; do not hand agents
    independent write paths.
  - spdlog level gating stays runtime-driven from INI (SPDLOG_ACTIVE_LEVEL=TRACE
    compiled in, gated at runtime) -- unchanged; the cpuId field is independent
    of level.

--------------------------------------------------------------------------------
NON-GOALS (this addendum)
--------------------------------------------------------------------------------
  - Per-CPU physical trace files (disallowed by the oracle).
  - Concurrent/lock-protected sink writes (Phase 3+ threaded-driver concern).
  - Any new per-CPU identity beyond CpuState::cpuId().
  - Changing spdlog channels/levels; only the cpuId field is added.

--------------------------------------------------------------------------------
NEXT ACTION
--------------------------------------------------------------------------------
Fold the cpuId tag into the trace + diagnostic formats as part of STEP 1, do the
one-time reference re-baseline, and confirm phase1_dispatch_gate.sh PASS against
the tagged format.  Keep both sinks as Dispatcher-level shared services when the
ownership lift (STEP 4) moves CpuState into the agent.

================================================================================
REVIEWER RECONCILIATION (added 2026-06-19, verified against V4 source)
================================================================================
This addendum was reviewed against the V4 tree and ACCEPTED.  Its concrete
claims check out; two refinements are folded in below.  Where this section and
the body above differ, THIS SECTION governs implementation.

VERIFIED CLAIMS
  - CpuState::cpuId()/setCpuId() exist as stated: coreLib/CpuState.h:153-155
    (`mCpuId`, cpuId(), setCpuId()).  There is already a TODO(whami-cpuid) to
    route PalEntries WHAMI through cpuId() -- this addendum pulls the same thread.
  - The trace sink is a SINGLE pure-virtual traceLib::TraceSink with
    onCommit(CommitRecord const&, coreLib::CpuState const& postCommitCpu)
    (traceLib/TraceSink.h:51-62).  cpuId is derivable from postCommitCpu today;
    no new plumbing is needed to tag trace lines.  The sink is Machine-owned
    (m_traceSink, passed into PipelineDriver::step) -> becomes a Dispatcher-level
    shared service cleanly.  Its header already notes multi-threading is a future
    concern and sinks must be callable from the pipeline's thread.
  - Diagnostic logging is spdlog with INI-driven level (config/LoggingInit.h),
    consistent with the "cpuId field, level unchanged" decision.

REFINEMENT 1 -- SPLIT THE "DO NOW" INTO TWO DIFFERENT-SIZED CHANGES.
  The cpuId TAG and the GLOBAL RETIRE ORDINAL are not the same size and must not
  both ride STEP 1:
    - cpuId tag: cheap, zero new infrastructure (cpuId already on CpuState,
      already reaches the sink via postCommitCpu).  DO IT IN STEP 1.
    - global retire ordinal: NEW machinery.  The dispatcher today has only a
      LogicalClock (quantum-tick count via now()/advance()); there is NO
      per-retire cross-agent ordinal yet, and its meaning only EXISTS once there
      is a split/second agent (under a single agent it would just equal the
      existing per-CPU retire count).  Introducing the ordinal source before the
      systemTick/clock decoupling that DEFINES it is moving two variables at once
      -- the exact hazard the addendum warns against.
  => STEP 1: add the cpuId tag only.  Introduce the global retire ordinal with
     the stepCycle split + clock decouple (STEP 2-3), where the dispatcher's
     cross-agent monotonic count becomes well-defined.  Keep the single-agent
     trace's existing cycle column as the per-CPU PCC (do NOT silently promote it
     to "the ordinal").

REFINEMENT 2 -- RE-BASELINE SCOPE IS NARROWER THAN "THE GATE."
  phase1_dispatch_gate.sh compares legacy-vs-dispatcher from the SAME build, so
  the cpuId tag appears on BOTH sides and cancels in the diff -- that gate needs
  NO re-baseline.  The one-time re-baseline applies ONLY to any STORED golden /
  AXPBox reference trace captured before the tag existed.  State it that way so
  no one "refreshes" the dispatch gate that doesn't need it.

NET: accept the addendum; in STEP 1 add the cpuId tag to trace + diagnostic
formats (single shared sinks, cpuId from CpuState::cpuId()), refresh any stored
golden/AXPBox reference once, and confirm phase1_dispatch_gate.sh PASS.  Defer
the global retire ordinal to STEP 2-3.  All other addendum decisions stand.

================================================================================
STEP 1b ACCEPTANCE CONTRACT (captured 2026-06-19; strings drafted AFTER 1a green)
================================================================================
Captured now because it is INVARIANT under however STEP 1a lands; the actual
DecListingSink format-string edits are deliberately NOT drafted yet (they touch
the same stepCycle/sink seam 1a refactors, so pre-drafting = drafting against a
moving target + a dirty-diff risk that would muddy 1a's gate).  Draft the strings
once 1a's gate is green, against a clean baseline.  SEQUENCING: 1a green -> THEN
1b, its own one-change-per-gate commit.

CONTRACT (stable):
  - The cpuId tag is sourced from CpuState::cpuId() (already present), emitted on
    EVERY trace line of the authoritative stream (DecListingSink INS / RET / DEC
    listing channels).
  - The trace sink stays a SINGLE shared stream (no per-CPU files).
  - The golden reference AND the AXPBox reference are refreshed ONCE, in the SAME
    commit as the format change (the tag changes trace text).
  - phase1_dispatch_gate.sh must PASS against the tagged format -- and needs NO
    re-baseline itself (legacy and dispatch are the same build, so the tag is on
    both sides and cancels; only STORED golden/AXPBox refs are refreshed).
  - Single agent emits cpuId 0 (e.g. "cpu0"); the format is second-CPU-ready
    (verify with a synthetic cpu1 line, confirm format stable).
  - Diagnostic spdlog records carry a cpuId field; per-CPU views are read-time
    filtering, not separate writers.  spdlog channels/levels unchanged.
  - GLOBAL RETIRE ORDINAL is NOT part of 1b -- it is STEP 2-3 (new machinery,
    meaning needs the split).  Keep the existing per-CPU cycle column as PCC; do
    not promote it to "the ordinal".
================================================================================
