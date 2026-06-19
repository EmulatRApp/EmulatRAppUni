# EmulatR V4 -- SMP agent/dispatcher harness (scaffold)

Status: SCAFFOLD, compiles + tested (g++ 13, C++20). 2026-06-18.
Qt-free core. Threading via `std::*` only (never QThread).

This is the harness converged on 2026-06-18: **symmetric** CPUs as independent
message-passing agents, behind a **deterministic dispatcher**, with the
execution model (deterministic-interleave vs host-parallel) a **swappable
driver** -- not a rewrite. Threading is the architecture; determinism vs raw
parallelism is a toggle.

## Why this shape

- SMP has been the project goal since inception; the agent/latch-release
  interface has to exist regardless of scheduling model. Building it now, with
  the model swappable, means the harness and its DOCTests are written once.
- Determinism is V4's verification methodology (instruction-deterministic
  snapshot/resume validated on the 20-cycle warm trace; AXPBox trace-diff).
  Free-running threads forfeit it. This harness keeps determinism as the
  DEFAULT and makes host-parallelism an opt-in, so you never lose the oracle
  during bring-up.
- CPUs are symmetric. There is no privileged "CPU0 sink." `primary` is a
  guest-elected property (pc264.c reads `*(uint32*)PAL$PRIMARY` and compares
  `whoami()`), and an SMP guest migrates timer/clock duty across CPUs at
  runtime -- so no host CPU object may own the timebase. Modeling CPU0 as
  privileged would be a correctness fault that surfaces deep in SMP guest boot.

## Files

```
include/emulatr/smp/
  SmpHarness.h    IAgent, LogicalClock, LockArbiter (LDx_L/STx_C), Effect,
                  IExecutionDriver, Dispatcher (owns agents + syncPhase).
  SmpDrivers.h    SequentialDriver (deterministic, 1 thread) and
                  ThreadedDriver (1 thread/agent, std::barrier-synchronized).
  MockCpuAgent.h  Stand-in symmetric CPU; proves the contract. NOT the real CPU.
tests/
  smp_harness_tests.cpp   DOCTest: arbiter semantics, determinism equivalence,
                          driver toggle, parked-agent-no-deadlock.
CMakeLists.txt    Header-only INTERFACE lib + test target.
```

## The determinism contract (the whole point)

Under `ThreadedDriver`, agents' `step()` run concurrently. To keep threaded
execution bit-identical to sequential:

1. During `step()`, an agent touches ONLY its own private state and its own
   outbox. It must NOT read/write another agent's state or shared
   memory/lock/IPI state directly.
2. All cross-agent interaction is STAGED as `Effect`s in `step()` and APPLIED
   by the dispatcher in `syncPhase()` -- single-threaded, in deterministic
   agent order (index ascending, submission order within an agent).

Hold the contract and the two drivers produce identical final state. The
`determinism_equivalence` test enforces this. If it ever fails under
`ThreadedDriver` but passes under `SequentialDriver`, an agent is touching
shared state inside `step()` -- that is the bug the test exists to catch.

This is the staged-commit discipline V4 already uses; the harness just makes
the quantum boundary the publish point.

## The toggle

```cpp
Dispatcher d(/*quantum*/ 1);
d.addAgent(&cpu0); d.addAgent(&cpu1);          // symmetric, dense ids

d.setDriver(std::make_unique<SequentialDriver>());   // deterministic: tests, trace-diff, snapshot
// ... or ...
d.setDriver(std::make_unique<ThreadedDriver>());     // host-parallel: profiling

d.run(/*untilTick*/ 100000);
```

`quantum = 1` is lock-step (tightest interleave, use for bring-up debug);
widen it to amortize per-boundary cost once correct. Quantum boundaries must
not fall inside an LL/SC sequence or mid-MMIO -- commit at retire only.

## Verified behavior (this scaffold)

- `threaded == sequential` across 8 trials (4 CPUs, 5000 ticks, quantum 1).
- LockArbiter mutual exclusion: with all agents phase-aligned, exactly one
  agent (the current granule holder) wins each round -- grants `0 0 0 1666`,
  never two winners. (The always-highest-index winner is an artifact of
  phase-aligned mocks; real CPUs with desynchronized timing rotate winners.
  The property that matters -- exactly-one-winner, deterministically -- holds.)
- Parked agent: a halted agent makes zero progress and does NOT deadlock the
  threaded barrier (it still arrives each quantum, skips `step()`); advances
  after `release()`.

## Expansion path (how to grow into the real thing)

- **AlphaCpuAgent** replaces MockCpuAgent: `step()` executes <= quantum Alpha
  instructions via the existing run loop; stage real memory writes / IPI sends
  / DMA as `Effect`s; map `id()` to WHAMI/CPUID (the Cchip already injects
  CPUID on MISC read per `cpuId`). Wire the real `lock_flag`/`lock_address` to
  `LockArbiter` (its semantics are already the LDx_L/STx_C interlock).
- **StorageHoseAgent** (one per SCSI bus): `step()` advances device state;
  stage DMA-completion as `Effect`s delivered in `syncPhase`, so I/O completion
  timing rides the logical clock instead of being host-thread-nondeterministic.
  This is what keeps boot traces reproducible once storage is threaded.
- **IPI delivery**: `EffectKind::Ipi` is stubbed in `Dispatcher::applyEffect`;
  wire it to the just-landed Cchip IPREQ/IPINTR latch of the target CPU, still
  applied in the deterministic sync phase.
- **Snapshot**: extend the serializer to capture all N agent contexts + the
  scheduler cursor (whose turn, quantum position); resume lands on the same
  interleave point. The dispatcher's `LockArbiter` state serializes too.

## Relationship to the DS20 boot

This harness is the foundation for genuine SMP (Goal B). It is NOT required to
reach `>>>` if the current DS20 hang turns out to be the `platform()`/ISP-flag
path or a `cpu_enabled`-gated `start_secondaries()` skip (Goal A) -- those are
cheaper. Build this because SMP is the end goal and the interface is needed
regardless; gate the DS20-specific fix on tomorrow's `cpu_enabled` read and the
`platform()` call-site audit.
