# schedLib -- deterministic/parallel agent scheduler (IN BUILD; harness VALIDATED)

Status: **IN THE BUILD; harness validated green.** Updated 2026-06-19
(originally staged 2026-06-18).

This directory holds the toggle-able deterministic-vs-parallel processor/agent
execution model (the SMP scheduler). It is now compiled in:
`SmpHarness.h`, `SmpDrivers.h`, and `AlphaCpuAgent.{h,cpp}` are in
`EMULATR_SOURCES`, and `tests/schedLib/smp_harness_tests.cpp` is in
`EMULATR_TEST_SOURCES`. V4's CMake lists sources explicitly (no `file(GLOB)`).

**Validation (2026-06-19):** `determinism_equivalence` passes -- Sequential ==
Threaded, **bit-identical (1666 lock grants identical across 5 runs)** -- plus
`parked_agent_no_deadlock` and `lock_arbiter_semantics`. The harness is the
verification fixed point: a future determinism failure is the new agent's bug,
not the harness's.

**Live wiring:** the real CPU now steps behind the harness as `AlphaCpuAgent`
(single agent, cpuId 0, under `SequentialDriver`), gated in `Machine::run` by
the `EMULATR_DISPATCH` env var (unset => legacy loop, the default). The
remaining Phase-1 acceptance gate is an `EMULATR_DISPATCH=1` boot proven
byte-identical to the legacy boot. A real second CPU is deferred (Phase 2+:
CpuState ownership-lift, then the Phase-3 LL/SC interlock).

## What it is

A `step()`-per-quantum agent model:
- **Agent** -- abstract unit with `step()` (CPUs and storage/DMA both implement it).
- **Dispatcher** -- sequences agents, owns the logical clock, runs the per-quantum
  sync/commit phase.
- **Driver** (swappable strategy) -- `SequentialDriver` (cooperative, single
  thread = the DETERMINISTIC oracle) and `ThreadedDriver` (parallel, barrier-
  synchronized at quantum boundaries).
- `MockCpuAgent` + DOCTest demonstrating determinism across both modes via
  latch/release on shared lock words.

Design rationale + the phased SMP plan: `journals/20260618_smp_secondary_cpu_bringup_design.md`.

## Review gate (nothing lands in the build until these are checked)

1. Compiles standalone (Qt-free: std::thread/jthread/atomic/condition_variable only).
2. Determinism guarantee is HONESTLY scoped: cooperative is the bit-identical
   oracle; threaded determinism holds ONLY for effects routed through the staged
   channel (lock table, interrupts/IPI, commitPending), applied in a FIXED order
   at the barrier. Raw shared guest-memory read/written mid-quantum is OUTSIDE
   the guarantee -- the doctest should make that boundary explicit.
3. Fixed cross-agent effect-application order at the barrier (CPU0 before CPU1,
   devices in slot order), documented.
4. Scheduler state (logical clock, quantum position, next-agent cursor) is
   serializable for bit-identical snapshot/resume mid-interleave (SMP spec Phase 6).
5. Maps onto V4's existing `commitPending` staged-commit model rather than a
   parallel mechanism.

## Integration status (DONE / REMAINING)

DONE (2026-06-19):
- Headers + `AlphaCpuAgent.cpp` added to `EMULATR_SOURCES`; doctest added to
  `EMULATR_TEST_SOURCES`.
- Dispatcher wired into `systemLib::Machine::run` behind the `EMULATR_DISPATCH`
  toggle, defaulting to the legacy loop (preserves current deterministic boot).

REMAINING:
- Close the Phase-1 gate: `EMULATR_DISPATCH=1` boot byte-identical to legacy.
- Phase 2: lift `CpuState` ownership into `AlphaCpuAgent` (re-home
  `bindCycleSource`'s raw pointer and the interval-timer fire-edge onto the
  dispatcher's logical clock) -- the real prerequisite for a second CPU.
- Phase 3: real `LockArbiter` backing (`lock_flag` / cache-granule / DMA hook)
  + the cross-CPU LL/SC contention micro-test.
- See `journals/20260618_smp_secondary_cpu_bringup_design.md` (Phases 0-6) and
  `journals/20260619_next_steps_alphacpuagent_gap.md` (P0-P3) for the full plan.
