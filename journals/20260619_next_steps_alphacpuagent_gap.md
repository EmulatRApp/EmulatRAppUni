================================================================================
EmulatR V4 -- NEXT STEPS (corrected) + AlphaCpuAgent integration gap
================================================================================
Written: 2026-06-19 (session spans multiple days; use absolute dates -- a carried
session has no single "today").
Posture: pivot to the SMP harness. ISP_MODEL banks >>>. REAL_HW parked (probe
unrun, hypotheses unverified). Harness VALIDATED this session.

--------------------------------------------------------------------------------
STATUS DELTA vs the 06-18 draft
--------------------------------------------------------------------------------
- P0 two greens are ALREADY BANKED. Filtered MSVC run:
    Emulatr_tests.exe -tc=lock_arbiter_semantics,determinism_equivalence,
      driver_toggle,parked_agent_no_deadlock,determinism_across_quantum_sequential
    => 5 passed / 0 failed, 16 assertions. determinism_equivalence (Sequential ==
    Threaded, bit-identical) + parked_agent_no_deadlock are GREEN. The harness is
    the fixed point now: a future determinism failure is the NEW AGENT, not the
    harness. (Standalone g++ 11.4 C++20 run agreed, 5x stable.)
- Boot profiler was NOT newly built. It pre-exists in-tree as RetireProfiler
    behind the EMULATR_BRINGUP_PROBES compile option (default OFF). P3 "expect
    rework" applies to that existing code.

--------------------------------------------------------------------------------
P0 -- VALIDATE THE HARNESS (essentially done; two cleanups remain)
--------------------------------------------------------------------------------
[x] determinism_equivalence GREEN (Sequential == Threaded, bit-identical)
[x] parked_agent_no_deadlock GREEN
[ ] Build-flag cleanup: DELETE the two $<$<CONFIG:Debug>:/EHsc-> and
    $<$<CONFIG:Release>:/EHsc-> lines in CMakeLists (~689-690). CMake default is
    already /EHsc; there is no RelWithDebInfo /EHsc- line, so deletion gives all
    three configs exceptions with nothing to add. (Enabling exceptions on the
    TEST target supersedes the old "CHECK only, never REQUIRE" rule -- the suite
    has 58 REQUIREs across 22 files and the emulator target itself builds with
    exceptions ON by default.) Hygiene: confirm last /EH token is /EHsc,
    _HAS_EXCEPTIONS != 0 on Emulatr_tests.
[ ] Full-suite green additionally blocked by test_float null-CPU rot: execAddt
    (+ ~7 FP leaves) deref c.cpu==nullptr because the FP grains were refactored
    FPCR-aware (c.cpu->fpcr) but tests/fBoxLib/test_float.cpp still builds bare
    ExecCtx{}. Fix test-side: give each ExecCtx a CpuState (ctx.cpu = &cpu).
    Separate lane; harness fixed-point stands on the isolated green.

--------------------------------------------------------------------------------
P1 -- AlphaCpuAgent (Phase 1) -- build AGAINST the green tests
--------------------------------------------------------------------------------
DECIDE FIRST: quantum boundary == instruction retire (the existing commit-at-MEM
/ retire-at-WB point -- do NOT invent a new drain). Never yield MID-INSTRUCTION,
never mid-MMIO. (Note: NOT "LL+SC atomic" -- see the integration gap below.)

INTEGRATION GAP (MockCpuAgent -> real AlphaCpuAgent). Mock satisfies IAgent
because it holds only private state + outbox; the real CPU touches shared state
directly inside Machine::run. Missing wiring:
  1. RE-ENTRANT RETIRE-BOUNDED STEPPER (big lift). Invert Machine::run's cycle
     loop into step(q): execute <= q retired instrs, return at a clean retire
     boundary, carry pipeline + CpuState across calls. Dispatcher owns the clock.
  2. PER-CPU STATE vs SHARED-SERVICE SPLIT. AlphaCpuAgent owns CpuState +
     pipeline; GuestMemory (already one shared instance), chipset (~80% SMP-ready,
     per-cpuId arrays), LockArbiter become Dispatcher-level shared services.
  3. EFFECT COMPLETENESS. Harness EffectKind::Store today only touches the lock
     table -- extend Effect to carry a real store (addr/value/size) and have
     syncPhase WRITE GuestMemory (then break locks). Wire Effect::Ipi (no-op stub)
     to the landed Cchip IPREQ/IPINTR latch. quantum==1 keeps this cheap and kills
     the intra-quantum RAW hazard (Alpha = 1 mem op/instr; store publishes at the
     boundary, next instr sees it next quantum -> no private forwarding buffer).
  4. LL/SC SPLIT FSM + wording fix. Real STx_C writes 0/1 into Ra in the SAME
     instruction, but the arbiter resolves SC at the sync boundary -> agent needs
     MockCpuAgent's Tx-style "SC pending" state (stage StoreCond, yield, read ack
     next quantum, write Ra). The plan's "never yield between LDx_L and STx_C" is
     imprecise: other agents' stores MUST interleave at the LL->SC boundary and
     can break the reservation (that is how contention is detected). Precise rule:
     never yield MID-INSTRUCTION. This is the Phase-3 cross-CPU interlock CLIFF;
     equivalence test must hammer it.
  5. (OPEN, threaded-only) MMIO side effects. MMIO read is synchronous (instr
     needs the value now) but mutates shared chipset state (read-clears, FIFO
     pops). SequentialDriver + 1 CPU: direct-call is fine. Threaded + >1 agent:
     serialize MMIO through the dispatcher or make the chipset thread-safe. Flag,
     do not solve, in Phase 1.

PHASE 1 MINIMUM = #1 + #2 + staging hooks of #3, run under SequentialDriver with
ONE agent. Equivalence holds trivially; proves a real Alpha steps re-entrantly
behind IAgent without regressing boot. #4 cross-CPU correctness + #5 MMIO safety
are Phase-3 / threaded.

GUARDRAILS: determinism_equivalence stays green (a red = the agent leaked shared
state into its concurrent phase -- that is the bug). Do NOT integrate the
threaded path until the agent is green on Sequential AND equivalence holds. Run
the threaded path under ThreadSanitizer periodically (equivalence proves "same
result," TSan proves "not by luck").

--------------------------------------------------------------------------------
P2 -- PLATFORM LEVER (independent; lands any time; retires the hack)
--------------------------------------------------------------------------------
[ ] Add EMULATR_PLATFORM = isp | silicon. One knob, one truth: env -> 0xBFFC
    answer -> firmware platform(). EmulatR must NOT keep its own independent
    sim/silicon flag (no split-brain: guest REAL_HW while host acts ISP).
[ ] IMPLEMENTATION NIT: realize as the existing READ-INTERCEPT of PA 0xBFFC, NOT
    a one-time deposit. 0xBFFC sits at image offset 0x3FFC, inside the region the
    SRM self-decompressor overwrites -> a deposit-at-init can be clobbered before
    platform() reads it (that is why the current hack is a read-hook). Same single
    knob, robust mechanism. Supersedes EMULATR_CPU1_ALIVE.
[ ] This lever IS the future REAL_HW bisection instrument (same binary, one env
    var between "console" and "where does silicon hang").

--------------------------------------------------------------------------------
P3 -- BOOT PROFILER (pre-exists behind EMULATR_BRINGUP_PROBES; HOLD)
--------------------------------------------------------------------------------
[ ] Do NOT test until P0 green (done) and P1 agent exists.
[ ] Expect rework: single-CPU RetireProfiler likely must become per-agent
    (per-CPU) and read the LOGICAL clock, not host wall-time, or numbers are not
    reproducible under the deterministic model. Define samples: per-agent retire
    counts, sync-phase cost, lock-arbiter contention -- on the logical clock.

--------------------------------------------------------------------------------
Q2 DECISION -- start a real CPU1 today? NO.
--------------------------------------------------------------------------------
The harness ALREADY permits n+1 (addAgent dense IDs; drivers + equivalence run 4
agents) -- nothing to configure. Starting a real CPU1 now is (a) out of order
(rendezvous responder = Phase 5, gated on Phases 1-3), (b) a wall-2 repeat (a
CPU1 that cannot yet do correct cross-CPU LL/SC reproduces "alive but not
participating," now with real divergence), and (c) the wrong goal for the
frontier (CPU1/SMP = Goal B; live frontier is single-CPU >>> then OS boot, both
uniprocessor; ISP mode does not even ask for CPU1). Today = Phase 1 single
AlphaCpuAgent. That earns the second CPU later on a foundation that will not
silently diverge.

--------------------------------------------------------------------------------
PARKED -- PREDICTED, UNVERIFIED (see project_ds20_console_idle_secondary_cpu_stall)
--------------------------------------------------------------------------------
- REAL_HW hang root cause HYPOTHESIS: timer.c krn$_micro_delay (rscc busy-wait +
  cycles_per_microsecond). NOT confirmed. Probe (5 min, when REAL_HW next
  touched): EMULATR_PLATFORM=silicon, capture hang PC, CLASSIFY (a) rscc
  micro-delay -> timer fix is the gate; (b) device probe/ddb_startup; (c)
  uninitialized-state read after the ISP/REAL_HW branches rejoin -> also model
  what ddb_startup POPULATES. The fork map says what ISP SKIPS, not what it
  leaves UNSET.
- ISP is a CEILING, not an OS-boot path: single 0xBFFC flag, no cherry-pick.
  OS-boot day == REAL_HW day == all busy-waits + probes re-arm at once. ISP
  disables IDE (dq_driver msg_failure) + skips device enumeration, so dqa0 /
  Cypress-IDE / DE500-tulip work is implicitly REAL_HW work.

--------------------------------------------------------------------------------
SEQUENCING RULE
--------------------------------------------------------------------------------
P0 green (done) -> P1 agent green on Sequential -> equivalence holds -> THEN
async, THEN profiler. P2 platform lever is independent, land any time.
================================================================================
