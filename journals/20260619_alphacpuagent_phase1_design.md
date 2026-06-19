================================================================================
EmulatR V4 -- AlphaCpuAgent (Phase 1) DESIGN -- CONFIRMED, build it
================================================================================
Written: 2026-06-19.  Confirms task #6.  Supersedes the P1 sketch in
20260619_next_steps_alphacpuagent_gap.md with the agreed gated-cutover shape.

GOAL
  Wrap the real AlphaCPU behind IAgent so the SMP harness (Dispatcher +
  SequentialDriver + syncPhase) drives it, WITHOUT regressing the SRM boot.
  Single agent in Phase 1; determinism_equivalence holds trivially (no second
  observer) and stays the guardrail.

CONFIRMED DECISIONS (all three + the scope amendment)
  1. SCOPE = full Phase 1 (AlphaCpuAgent under a real Dispatcher with
     SequentialDriver + syncPhase), but the PRODUCTION-LOOP SWAP is the LAST
     internal step and lands behind a GATE.  "Build the agent+dispatcher" and
     "make it the shipping boot path" are SEPARATE RISK EVENTS and must NOT land
     atomically.  The dispatcher-driven path and the legacy Machine::run loop
     COEXIST behind a flag (env var or build switch); legacy stays DEFAULT until
     the dispatcher path demonstrably reaches >>> with a byte-identical trace;
     then flip the default and DELETE the legacy loop in a SEPARATE commit.
     - NOT a smaller rung: a bare agent.step(1) (no dispatcher) has a DIFFERENT
       surrounding contract (who runs syncPhase, who owns clock advance) than
       the dispatcher-driven call, so it is throwaway scaffolding rewritten the
       moment the dispatcher lands.  Build it once, against the dispatcher.
     - NOT a big-bang swap: that forfeits the ability to bisect a boot
       regression against the old path.
  2. CPUSTATE = REFERENCE Machine's m_cpu in Phase 1; do NOT move ownership into
     the agent now.  The ownership lift is a separable change with its own
     hazard surface (see PHASE-2 LANDMINES); doing it while inverting the run
     loop would make any boot regression ambiguous (ownership vs loop).
     Reference now freezes the ownership question so any Phase-1 regression is
     unambiguously the loop inversion.
  3. NO STAGING = direct memory/MMIO for the single agent.  Correct, not a
     compromise: with one agent under SequentialDriver there is no concurrent
     observer, so a direct store and a staged-then-applied store are
     observationally identical and equivalence holds trivially (nothing to
     interleave).  Staging only earns its cost when a SECOND agent can observe a
     mid-quantum write -- the CPU1/threaded work (Phase 3), exactly where it is
     deferred.  Building it now = speculative complexity no test can exercise.

STAGING = SEAMS, NOT STUBS  (explicit, to prevent two failure modes)
  The agent's step() has the CALL SITES where effect-staging will later insert
  -- the LL/SC retire point, the store commit, the IPI write.  In Phase 1 those
  sites perform the DIRECT operation (the existing path) with a COMMENT marking
  where Phase-3 staging inserts.  They are NOT stubbed-out TODOs that no-op a
  real operation.
    - WRONG: `// TODO stage` that no-ops a real store  -> breaks boot.
    - WRONG: building the full staging path now         -> premature, untested.
    - RIGHT: direct operation at the seam, comment marking the seam.

ACCEPTANCE GATE (concrete, blocks the default flip)
  Dispatcher-driven single-CPU boot must produce a BYTE-IDENTICAL retire trace
  to legacy Machine::run, to the >>> prompt.  Until that diff is clean, legacy
  stays the default.  After it is clean, flip the default; delete the legacy
  loop in a separate commit.
  DESIGN AID: extract the per-cycle CPU kernel (interrupt poll + PipelineDriver
  ::step) into one method called by BOTH the legacy loop and the agent.  Then
  the two paths run the IDENTICAL kernel by construction, so the gate tests the
  loop/clock/dispatch wiring (the actual new code), not the CPU kernel.

PHASE-2 LANDMINES (recorded now; correct as-is for single-CPU Phase 1; both must
route through the dispatcher's LOGICAL clock when the agent OWNS its CpuState)
  L1. bindCycleSource(&m_cpu.cycleCount) hands a RAW pointer into per-CPU
      CpuState to the RTC.  Owning CpuState in the agent means re-homing this
      (and the snapshot save/restore reach into m_cpu, and the tryFetch/
      onBeforeFetch override is this-bound to Machine).
  L2. The interval-timer FIRE-EDGE keys off m_cpu.cycleCount as if it were the
      SYSTEM clock -- the CPU0-as-sink assumption in disguise.  Must key off the
      dispatcher's logical clock once there is >1 CpuState.
  Phase 2 (ownership lift) is also where the per-agent-PCC-vs-system-timebase
  split gets resolved cleanly -- its own focused change, not a rider on the loop
  inversion.

DECOMPOSITION (boot-safe; each step independently verifiable)
  STEP 1  Extract the per-cycle CPU kernel into Machine (behavior-neutral: the
          legacy loop calls the same method).  Add AlphaCpuAgent (header) that
          references m_cpu + shared chipset/GuestMemory/sink + a Machine*
          back-pointer and whose step(q) calls that kernel q times, breaking on
          halt.  runnable()=!cpu.halted; id()=cpuId (WHAMI/CPUID).  Compiles;
          legacy path unchanged + still default.
  STEP 2  Add the dispatcher-driven path: Machine builds a Dispatcher + one
          AlphaCpuAgent + SequentialDriver, calls agent.step(1)+syncPhase() per
          quantum with the machine-global bookkeeping (snapshot/sentinel/predig
          triggers) wrapped around it.  GATED behind a flag; legacy is DEFAULT.
  STEP 3  ACCEPTANCE: byte-identical retire trace, dispatcher vs legacy, to >>>.
  STEP 4  Flip the default to the dispatcher path; DELETE the legacy loop in a
          SEPARATE commit.

NON-GOALS (Phase 1) -- deferred, recorded so they are not quietly assumed built
  - Effect-staging of stores/locks/IPI (Phase 3; seams marked, not built).
  - Cross-CPU LDx_L/STx_C LockArbiter interlock (Phase 3, the cliff).
  - MMIO sync-read thread-safety (Phase 3).
  - Owning a second CpuState / per-agent PCC vs system timebase (Phase 2;
    resolves L1+L2).
================================================================================

--------------------------------------------------------------------------------
EXTRACTION -- SIGNED OFF 2026-06-19 + 4 MECHANICAL CHECKS (Tim)
--------------------------------------------------------------------------------
Shape confirmed: whole-body VERBATIM lift; stepCycle stays a Machine method; the
agent is a thin driver; do NOT narrow what the agent drives (narrowing = deciding
the CPU-vs-bookkeeping split during the extraction = the Phase-2 concern). Keep it
a PURE RELOCATION. Verify these four -- diff, do not eyeball:
  1. STATIC LOCALS move WITH the body (s_idleTickWarp, s_warpLog, s_cnt @~1251,
     plus any other `static` in the block).  function-static -> method-static
     keeps one-instance-per-process, BUT (a) confirm none are left behind or
     duplicated (changes init timing / instance count), and (b) a getenv-init'd
     static shared across agent threads is a contention/correctness question
     under the FUTURE ThreadedDriver -- write the comment AT each static site NOW:
     "process-global; revisit under threaded driver."  Cheaper than rediscovery.
  2. CONTROL FLOW: the two OUTER breaks (stop-sentinel @~1014, halt-from-step
     @~1021) -> return false; fall-through -> return true.  Inner trigger-loop
     break @~1112 is UNTOUCHED.  DIFF the control flow: confirm no loop-tail code
     after the synthetic-INTERRUPT block runs on fall-through but NOT on the break
     paths -- return false must skip exactly what the old break skipped; return
     true must run exactly the old fall-through tail.
  3. CYCLE COUNTER: the agent's m_cycleIndex++ must reproduce run()'s `i` EXACTLY
     (same start, same increment, same relation to m_cpu.cycleCount used by predig
     snapshot naming / IDLEWARP / the interval-timer edge).  THE maxCycles CAP
     STAYS in legacy run()'s for(i < maxCycles); the agent's step(q) just runs q
     and lets the dispatcher/caller enforce the ceiling -- NEVER two caps that can
     disagree.
  4. GATE = a REAL boot-trace diff, not "compiles + reaches >>>".  Capture a
     BASELINE (retire-trace, or cycle-count-at->>> + a predig-snapshot state hash)
     of the PRE-extraction legacy build FIRST; rebuild post-extraction; diff.
     Pure relocation => ZERO diff.  Nonzero => look at checks 1-3.  Capture the
     baseline BEFORE touching the body.

SINGLE-AGENT BY SHAPE (not just by choice) -- ON THE RECORD: because the agent
transitively runs the machine-global bookkeeping (snapshot / IDLEWARP / flash
debounce) inside step(), a SECOND agent in Phase 1 would DOUBLE-FIRE those global
side effects.  So Phase 1 is structurally single-agent, and the Phase-2 CPU-work
/ bookkeeping split is the PREREQUISITE for a second agent -- not a cleanliness
nicety.  Dependency chain: [2nd agent] needs [Phase-2 split] needs [CpuState
ownership lift].

SEQUENCING (Tim): pure relocation -> baseline-trace the legacy boot FIRST -> diff
after -> wire the agent (.cpp + CMake) ONLY once the diff is clean.

--------------------------------------------------------------------------------
GATE PASSED 2026-06-19 -- stepCycle relocation PROVEN PURE
--------------------------------------------------------------------------------
LANDED: Machine.h (stepCycle decl + m_stopSentinel member); Machine.cpp (loop
body lifted verbatim into bool Machine::stepCycle(uint64_t i); run() collapsed to
setup + for(i){if(!stepCycle(i))break;} + verbatim tail; 2 outer breaks ->
return false; kStopPollMask moved in; 5 statics carry "process-global; revisit
under threaded driver"). AlphaCpuAgent.h created, NOT in build yet.
ACCEPTANCE: baseline (pre-extraction) vs post-extraction guest SRM console
(PuTTY sessionlogs, both ISP + cold + factory flash) diff == EMPTY except the
PuTTY log-header timestamp line. Byte-identical guest boot to wall-2. Host log
identical bar timestamps + an unrelated [RPCC probe] config line (flash confound
cleared by rm-ing ds10_flash.rom so both factory-init). Check #3 (m_cycleIndex vs
i) moot this pass -- run() still owns the loop and passes i; m_cycleIndex enters
only when the agent drives (step 2). Checks #1 (statics) + #2 (control flow) held.

NEXT = STEP 2: wire AlphaCpuAgent.cpp (runnable()=!cpu().halted; step(q) calls
stepCycle(m_cycleIndex++) q times, break on false) + CMake (EMULATR_SOURCES) +
the dispatcher-driven path in Machine::run BEHIND A FLAG (legacy default).
STEP-2 DESIGN POINT (decide first): the harness SequentialDriver runs until
untilTick and does NOT stop when an agent halts, but legacy run() breaks on
stepCycle()==false. To match -- and for the step-3 dispatcher-path byte-identical
gate -- the driver needs a "terminate when no agent is runnable" condition (agent
marks itself non-runnable once stepCycle returns false). Small, general, correct
addition to SmpDrivers.h; re-run determinism_equivalence after to confirm no
regression.

--------------------------------------------------------------------------------
PHASE 1 COMPLETE 2026-06-19 -- AlphaCpuAgent LIVE, byte-identical
--------------------------------------------------------------------------------
Step 2 landed: SmpDrivers.h (Sequential + Threaded both terminate when no agent
is runnable); AlphaCpuAgent.h/.cpp (m_stopped; runnable()=!m_stopped &&
!cpu().halted; step(q) calls stepCycle(m_cycleIndex++) q times, latches
m_stopped + returns early on false); CMakeLists (schedLib sources added to BOTH
EMULATR_SOURCES and EMULATR_TEST_SOURCES -- Machine.cpp compiles into both
targets, so the agent's vtable/out-of-line virtuals must too; that was the
LNK2001 fix); Machine.cpp (EMULATR_DISPATCH-gated path: one AlphaCpuAgent under
SequentialDriver; legacy direct loop is the default).
GATES PASSED:
  - Step-3 dispatcher gate: EMULATR_DISPATCH=1 + ISP + cold boot -> guest SRM
    console BYTE-IDENTICAL to the legacy baseline (PuTTY sessionlog diff = only
    the log-header timestamp). The agent issues the IDENTICAL stepCycle(i)
    sequence as legacy run().
  - Harness: determinism_equivalence + parked_agent_no_deadlock GREEN after the
    driver-termination change (2 cases / 6 assertions, 0 failed).
STATE: a real Alpha CPU runs behind IAgent under Dispatcher+SequentialDriver,
byte-identical to legacy; the toggle DEFAULTS to legacy (EMULATR_DISPATCH opt-in)
until the cutover is chosen.
DEFERRED (by design, sequenced):
  - STEP 4: flip the default to the dispatcher path + delete the legacy loop --
    SEPARATE commit, when you want the dispatcher to be the shipping boot path.
  - PHASE 2: CpuState ownership lift into the agent; re-home bindCycleSource
    (L1) + the interval-timer fire-edge (L2) onto the dispatcher logical clock.
    PREREQUISITE for a 2nd agent (single-agent-by-shape -- see above).
  - PHASE 3: Effect-staging of stores/locks/IPI + cross-CPU LDx_L/STx_C interlock
    (the cliff) + MMIO sync-read thread-safety.
