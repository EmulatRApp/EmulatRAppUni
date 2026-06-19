# schedLib -- deterministic/parallel agent scheduler (UNDER REVIEW, BUILD-INERT)

Status: **STAGED, NOT IN THE BUILD.** 2026-06-18.

This directory holds an incoming scaffold implementation of the toggle-able
deterministic-vs-parallel processor/agent execution model (the SMP scheduler).
It is dropped here for review and is **intentionally not compiled**: V4's CMake
lists sources explicitly (`EMULATR_SOURCES`, `EMULATR_TEST_SOURCES`) with no
`file(GLOB)`, so nothing in this directory builds until it is explicitly added.
Your F5 / normal build is unaffected.

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

## To integrate (only after review)

- Add the headers/`.cpp` to `EMULATR_SOURCES` (CMakeLists.txt:105).
- Add the doctest `.cpp` to `EMULATR_TEST_SOURCES` (CMakeLists.txt:495).
- Wire the Dispatcher into `systemLib::Machine::run` behind the driver toggle,
  defaulting to SequentialDriver (preserves current deterministic behavior).
