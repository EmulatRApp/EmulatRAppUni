================================================================================
EmulatR V4 -- AlphaCpuAgent (Phase 2) DESIGN -- CpuState ownership lift
================================================================================
Written: 2026-06-19.  Status: DESIGN, discuss-before-code (mirrors the Phase-1
process).  Follows Phase 1 COMPLETE (AlphaCpuAgent live behind EMULATR_DISPATCH,
step-3 gate byte-identical -- see 20260619_alphacpuagent_phase1_design.md and
tools/phase1_dispatch_gate.sh).

DECISIONS LOCKED (2026-06-19, Tim sign-off)
  D-1  PCC-vs-timebase = POLICY P-A, APPROVED -- per-CPU PCC == system clock for
       the RUNNING CPU; parked CPU's PCC frozen; RPCC read through the running
       agent's CpuState.  Faithful (real HW = per-CPU PCC + system interval
       timer), preserves current behavior so the byte-identical gate stays
       meaningful through the lift, minimum to unblock CPU2 with no invented skew.
       P-B (independent PCC offsets) PARKED until a guest exposes the difference
       (nothing in the boot path does).
  D-1a BINDING INVARIANT (approve as a PAIR with D-1): the system clock advances
       by the PER-STEP RETIRE CYCLE DELTA -- identical to the PCC -- NOT by 1 per
       loop iteration and NOT by 1 per quantum.  "P-A" + "per-iteration clock" is
       silently NOT P-A and FAILS at STEP 3.  systemNow() must increment by
       (newPCC - oldPCC) each step once it is decoupled (STEP 3).
  D-2  Legacy-loop deletion (the deferred Phase-1 STEP 4) is NOT folded into
       Phase 2.  Reason is structural: the legacy loop IS the oracle -- every
       Phase-2 gate (STEP 1-4) is "byte-identical vs legacy."  Deleting it
       mid-phase removes the comparison target during the exact sequence that
       needs it, and interleaves a behavior-changing cutover with the lift+clock
       split (three moving variables, regressions un-attributable).  Phase 2
       COMPLETES with legacy still present + still the gate.  The flip+delete is a
       deliberate SEPARATE one-variable commit AFTER Phase-2 acceptance, caught
       by the determinism harness (then the sole gate).  See STEP 5.

PURPOSE OF THIS DOC
  Phase 1 made one real Alpha CPU step re-entrantly behind IAgent while still
  REFERENCING Machine's single m_cpu.  Phase 2 lifts CpuState OWNERSHIP into the
  agent and untangles the "system timebase vs per-CPU PCC" conflation.  This is
  the PREREQUISITE for a second CPU -- not a cleanliness pass.  No code lands
  until this design is reviewed.

--------------------------------------------------------------------------------
WHY NOW (the dependency chain, restated)
--------------------------------------------------------------------------------
From the Phase-1 record: Phase 1 is SINGLE-AGENT BY SHAPE.  agent.step() today
transitively runs the machine-global bookkeeping (snapshot cadence, IDLEWARP,
flash flush, interval-timer fire, interrupt divert) INSIDE the per-cycle body,
so a second agent would DOUBLE-FIRE every global side effect.  Therefore:

    [2nd CPU / Phase 3 interlock] needs [per-agent CpuState ownership]
                                  needs [CPU-kernel vs system-tick split]

Phase 2 delivers the bottom two.  It is deliberately NOT the second CPU (that is
Phase 5, gated on measurements) and NOT effect-staging (Phase 3, the cliff).

--------------------------------------------------------------------------------
THE CORE PROBLEM: m_cpu.cycleCount is overloaded
--------------------------------------------------------------------------------
`m_cpu.cycleCount` is doing TWO jobs at once:

  (a) PER-CPU architectural PCC -- the value RPCC/the PCC IPR return for THIS
      CPU.  Belongs to the CpuState; must be per-agent.
  (b) THE SYSTEM TIMEBASE -- the monotonic "wall" that the RTC, the interval
      timer, the flash-flush debounce, the snapshot cadence, and the IDLEWARP
      all read.  Belongs to the machine/dispatcher; must be ONE shared clock.

With a single CPU these are the same number, so the conflation is invisible.
With >1 CpuState, reading CPU0's PCC as "the system clock" is the CPU0-as-sink
assumption in disguise (the same class of bug as the single-CPU interrupt
divert).  Phase 2 SEPARATES the two:

  - per-CPU PCC stays in each CpuState.cycleCount (owned by the agent), and
  - the system timebase becomes the Dispatcher's LogicalClock (m_clock.now()),
    which already exists (schedLib/SmpHarness.h: LogicalClock now()/advance()).

--------------------------------------------------------------------------------
INVENTORY -- every system consumer of m_cpu.cycleCount (must re-home to the
logical clock).  Line numbers are systemLib/Machine.cpp as of 2026-06-19.
--------------------------------------------------------------------------------
  L1  RTC time source        : m_chipset.rtc().bindCycleSource(&m_cpu.cycleCount)
                               @346  -- hands a RAW pointer into per-CPU CpuState
                               to the RTC.  Re-home to a pointer to the system
                               clock (or an accessor), NOT any one CPU's PCC.
  L2  Interval-timer edge    : chipsetLib::intervalTimerShouldFire(m_cpu.cycleCount)
                               @1292, @1310 -- the Cchip interval timer fire edge.
                               Must key off the system clock.
      IDLEWARP               : m_cpu.cycleCount = (c0 | kCchipTimerMask)+1  @1294
                               -- fast-forwards the SYSTEM clock past an idle
                               spin to the next timer edge.  Once the system
                               clock is separate, IDLEWARP advances THAT, and the
                               per-CPU PCC follows the warp rule we choose
                               (see "PCC-vs-timebase policy" below).
      Flash flush            : m_chipset.flash().tryFlush(m_cpu.cycleCount)  @1318
                               -- debounce keyed off the timebase -> system clock.
  L2b Inject-interrupt arm   : systemNow() >= m_injectInterruptCycle  @~1587 --
                               armInterruptInjection() one-shot fires at a target
                               SYSTEM cycle (same fire-when-clock-reaches-X class
                               as the interval timer).  ADDED to inventory
                               2026-06-19 (was missed in the first pass): dormant
                               in normal boots, so the gate would NOT catch it
                               reading the wrong clock after STEP 3 -- a latent
                               wrong-clock bug under the dispatcher + 2nd CPU.
                               Level-triggered (>=) + one-shot => IDLEWARP-safe
                               (a warp past the target is caught next retire).
                               Routed through systemNow() in STEP 1a.
  SNAP  Auto-save cadence    : m_nextAutoSaveCycle vs m_cpu.cycleCount
                               @952, @1109, @1125  -- cadence is a system concept
                               -> system clock.
  SNAP  Snapshot file naming : m_cpu.cycleCount in predig/auto/oem names
                               @1038, @1105, @1119, @1161  -- cosmetic; switch to
                               the system clock so names stay monotonic with >1 CPU.
  SNAP  Snapshot payload     : Snapshot.cpp serializes cpu.cycleCount @112, @256
                               and reads machine.cpu().  Phase 2 keeps this
                               working for agent0; the FULL N-CPU + clock-cursor
                               serialize is Phase 6 (flagged, not built here).

PER-CPU consumers that STAY in CpuState (do NOT touch): RPCC/PCC reads, the
retire counter inside PipelineDriver::step (it updates m_cpu.cycleCount as the
CPU's own PCC), palBase/pc/registers/ITB/DTB/lock_flag -- all architectural,
all per-agent by definition.

--------------------------------------------------------------------------------
THE STRUCTURAL MOVE: split stepCycle into a per-CPU kernel + a system tick
--------------------------------------------------------------------------------
Today bool Machine::stepCycle(uint64_t i) interleaves, in one body:
    [stop-sentinel poll] [step() = CPU kernel] [console-snapshot] [auto-save]
    [predig triggers] [IDLEWARP] [interval-timer fire + divert] [flash flush]
The first is the system's, the second is the CPU's, the rest are the system's.

Phase 2 cleaves this into two methods with explicit ownership:

  bool Machine::cpuKernel(coreLib::CpuState& cpu) noexcept
      // PER-AGENT.  The interrupt-acceptance poll + PipelineDriver::step for
      // EXACTLY this cpu.  Returns false on halt.  No machine-global side
      // effects.  Each agent calls this for its own CpuState.

  StepStatus Machine::systemTick(Tick now) noexcept
      // ONCE PER QUANTUM, dispatcher-level (NOT per agent).  Stop-sentinel poll,
      // interval-timer fire + interrupt staging, flash flush, snapshot cadence
      // + naming, IDLEWARP -- all keyed off `now` (the logical clock), not any
      // CPU's PCC.  This is what makes a 2nd agent safe: globals fire once.

  Transitional: stepCycle(i) becomes { cpuKernel(m_cpu); systemTick(clockNow); }
  so the LEGACY path and the single-agent dispatcher path stay byte-identical
  through the split (the Phase-1 gate re-proves it).

WHERE THE LOGICAL CLOCK COMES FROM
  - Dispatcher path: systemTick reads Dispatcher::clock().now(); the dispatcher
    advances it once per quantum (already does -- LogicalClock::advance).
  - Legacy path (until STEP 4 deletes it): Machine owns a fallback m_systemTick
    counter incremented once per loop iteration, so legacy keeps working with no
    dispatcher.  ONE definition of "now" per path; never two that can disagree
    (the Phase-1 maxCycles-cap discipline, applied to the clock).

PCC-vs-timebase policy (decide in review)
  With the clock split, what is each CPU's PCC relative to the system clock?
  Options:
    P-A (RECOMMENDED, simplest, matches today): per-CPU PCC == system clock at
        the same instant for the running CPU; a parked CPU's PCC is frozen.  PCC
        is read THROUGH the running agent's CpuState, so RPCC stays correct and
        IDLEWARP advancing the system clock also advances the running CPU's PCC
        (the current behavior, preserved).
    P-B (later, if a guest needs skew): independent per-CPU PCC offsets.  Not
        needed for boot; defer unless a guest exposes the difference.
  Recommend P-A for Phase 2; record P-B as a known extension.

--------------------------------------------------------------------------------
OWNERSHIP BOUNDARY (what moves, what stays)
--------------------------------------------------------------------------------
MOVES INTO AlphaCpuAgent (per-agent, owned):
  - coreLib::CpuState (the whole struct: registers, pc, palBase, ITB/DTB,
    lock_flag, cycleCount-as-PCC, FP state, shadow regs).
  - The agent's m_cycleIndex (already there) becomes its private retire ordinal.

STAYS SHARED (Dispatcher-level services, one instance):
  - GuestMemory (already one instance).
  - The chipset (TsunamiChipset) -- already per-cpuId internally (~80% SMP-ready:
    m_dim[], m_iic[], m_pendingIrq2[], m_pendingIrq3[], MISC CPUID injection).
  - The trace sink, SRM staging/relocation, snapshot subsystem.
  - The system clock + all systemTick bookkeeping.

COMPAT SHIM (keep the blast radius small):
  - Machine::cpu() currently returns m_cpu by reference and is read in ~dozens of
    places (Snapshot.cpp, tests, classifyStop, onBeforeFetch).  Phase 2 keeps
    Machine::cpu() returning the PRIMARY agent's CpuState (agent0) so those call
    sites are untouched in this phase.  The bindCycleSource/onBeforeFetch
    this-binding to Machine stays; only the cycleCount SOURCE changes (L1).
  - onBeforeFetch / tryFetch are Machine-bound (SRM relocation one-shot); they
    operate on "the fetching CPU."  In Phase 2 (single agent) that is agent0;
    when a 2nd agent lands they take the cpu as a parameter.  Mark the seam.

--------------------------------------------------------------------------------
DECOMPOSITION (boot-safe; each step re-uses the Phase-1 gate)
--------------------------------------------------------------------------------
  STEP 1  Introduce the system clock as a FIRST-CLASS concept WITHOUT moving
          ownership: add Machine::systemNow() returning m_cpu.cycleCount today
          (no behavior change), and route L1/L2/SNAP/flash/IDLEWARP through
          systemNow() instead of m_cpu.cycleCount directly.  Pure refactor.
          ALSO (per the logging addendum, REFINEMENT 1): fold the cpuId TAG into
          the trace + diagnostic formats now (cpuId from CpuState::cpuId(), which
          already reaches the sink via onCommit's postCommitCpu) and re-baseline
          any STORED golden/AXPBox reference once.  Add the cpuId tag ONLY here;
          the global retire ordinal is deferred to STEP 2-3 (it is new machinery
          whose meaning needs the split).
          GATE: phase1_dispatch_gate.sh -> byte-identical (both paths; the tag is
          on both sides so the dispatch gate needs no re-baseline).
          STATUS: STEP 1a (systemNow() refactor incl. L2b inject arm) DONE +
          GATE PASSED 2026-06-19 (ds20 0x40000000 byte-identical; both
          Stop=MaxCyclesExceeded PC=0x1ad930 fault=5).  STEP 1b (cpuId tag) is
          the next discrete commit, held for a clean post-1a-commit baseline.
  STEP 2  Split stepCycle into cpuKernel(cpu) + systemTick(now); stepCycle calls
          both in order.  Still references m_cpu; systemNow() still == m_cpu PCC.
          GATE: byte-identical again (this is the risky structural cut -- the
          gate is exactly the tool for it).
  STEP 3  Make systemNow() its own counter DECOUPLED from m_cpu.cycleCount.
          INVARIANT D-1a (load-bearing here): it advances by the PER-STEP RETIRE
          CYCLE DELTA (newPCC - oldPCC), identical to the PCC -- NOT by 1 per
          iteration and NOT by 1 per quantum.  A per-iteration/per-quantum clock
          is silently not P-A and fails this gate.  Under P-A the running CPU's
          PCC still tracks systemNow(), so the boot trace is unchanged.
          GATE: byte-identical.  (If non-empty, it pinpoints a place that still
          conflates the two clocks, OR the advance rate is wrong -- that is the
          finding, fix it.)
  STEP 4  Move CpuState ownership into AlphaCpuAgent: the agent holds the
          CpuState; Machine::cpu() returns agent0's.  Re-home bindCycleSource (L1)
          to systemNow(); confirm snapshot save/restore still round-trips agent0.
          GATE: byte-identical boot + determinism_equivalence + snapshot
          round-trip test all green.
  STEP 5  (SEPARATE, POST-PHASE-2 -- D-2, NOT folded in) flip the default to the
          dispatcher path + delete the legacy loop, as its OWN one-variable commit
          AFTER Phase-2 acceptance.  Until then legacy stays present and stays the
          gate's reference.  Once deleted, the determinism harness is the sole
          gate.

Each step is independently committable and independently gated.  Order matters:
clock-first (1-3) THEN ownership (4), so an ownership regression cannot hide
inside a clock change.

--------------------------------------------------------------------------------
ACCEPTANCE (Phase 2 done)
--------------------------------------------------------------------------------
  - Single-agent dispatcher boot BYTE-IDENTICAL to legacy (phase1_dispatch_gate.sh
    PASS) after the full lift.
  - determinism_equivalence + parked_agent_no_deadlock stay GREEN.
  - Snapshot save/restore round-trips agent0's CpuState bit-identically.
  - No remaining read of a CPU's cycleCount as the system clock (grep clean:
    rtc/interval-timer/flash/snapshot-cadence all go through systemNow()).
  - CpuState is owned by AlphaCpuAgent; chipset/memory/clock are shared services.

--------------------------------------------------------------------------------
RISKS / WATCH-OUTS
--------------------------------------------------------------------------------
  - STEP 2 (the stepCycle split) is the structural cliff of THIS phase: a piece
    of bookkeeping mis-assigned to cpuKernel would double-fire under a 2nd agent
    (the exact bug Phase 2 exists to prevent).  Audit each line's owner against
    the INVENTORY above; the gate proves single-agent equivalence but does NOT
    prove the 2nd-agent-safety of the split -- that needs a 2-agent dry-run
    (parked second agent) added to the harness once STEP 4 lands.
  - IDLEWARP semantics under the split: warping the SYSTEM clock past an idle
    spin must still advance the running CPU's PCC (P-A), or RPCC-based delays in
    the guest regress.  Keep IDLEWARP in systemTick but have it nudge the running
    agent's PCC under P-A.
  - The static locals carried into stepCycle (s_idleTickWarp, s_warpLog, s_cnt --
    "process-global; revisit under threaded driver") belong to systemTick, not
    cpuKernel.  Move them WITH the system bookkeeping; do not leave them in the
    per-agent kernel.
  - Snapshot is single-CPU-shaped (machine.cpu(), one cycleCount).  Phase 2 keeps
    it working for agent0 via the compat shim; the N-CPU + clock-cursor serialize
    is Phase 6 -- do NOT half-build it here.
  - D: mount can serve stale copies; validate edits via the Read (host) view, and
    builds/git are client-side.

--------------------------------------------------------------------------------
NON-GOALS (Phase 2) -- recorded so they are not quietly assumed built
--------------------------------------------------------------------------------
  - A second executing CpuState (Phase 5; gated on P1/P2/P3 measurements).
  - Effect-staging of stores/locks/IPI + cross-CPU LDx_L/STx_C interlock
    (Phase 3, the cliff).
  - N-CPU snapshot + scheduler-cursor serialize (Phase 6).
  - Independent per-CPU PCC skew (policy P-B; defer until a guest needs it).

--------------------------------------------------------------------------------
COMPANION
--------------------------------------------------------------------------------
  Per-CPU logging/tracing policy is settled in the addendum
  20260619_phase2_logging_policy_addendum.md (REVIEWED + ACCEPTED): single
  dispatcher-owned trace + diagnostic sinks; per-CPU is a property of the DATA
  (cpuId tag), not the destination -- mirrors this doc's per-CPU-PCC /
  system-clock split.  STEP 1 above carries its cpuId-tag action item.

--------------------------------------------------------------------------------
NEXT ACTION
--------------------------------------------------------------------------------
Review this design (esp. the PCC-vs-timebase policy P-A and the STEP 1-4 order).
On sign-off, implement STEP 1 (systemNow() indirection, pure refactor) and run
phase1_dispatch_gate.sh.  Then proceed step-by-step, gating each.
================================================================================
