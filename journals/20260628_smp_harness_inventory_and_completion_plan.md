# SMP Execution Harness — Inventory + Completion Plan

> **Filed 2026-06-28.** Purpose: give a single, code-grounded map of the SMP
> execution harness (the deterministic ⇄ asynchronous bifurcated scheduler) and
> the concrete, ordered steps to finish it — code injection, wiring, and tests.
> This is the "what exists / what's left" view. It does **not** replace the two
> design documents it sits on top of; read those for rationale and protocol detail:
> - `journals/20260618_smp_secondary_cpu_bringup_design.md` (Phases 0–6, decisions D1–D3, prerequisites P1–P4)
> - `journals/20260619_next_steps_alphacpuagent_gap.md` (the MockCpuAgent → real AlphaCpuAgent integration gap, items #1–#5)
> - `journals/20260620_smp_levers_and_hrm_mp_wiring_map.md` (HRM MP-register wiring map)
> - `schedLib/README.md` (validation status + review gate)

---

## 1. What the harness IS (the mental model)

A **`step()`-per-quantum agent model** with a **swappable execution driver**. The
two paths you care about — *deterministic* and *asynchronous* — are **not separate
codebases**; they are two `Driver` strategies behind one `Dispatcher`. Choosing
one over the other is a runtime decision, not a rewrite.

```
                 ┌──────────────────────────────────────────┐
                 │             Dispatcher                     │
                 │  owns: LogicalClock, LockArbiter, agents[] │
                 │  runs: per-quantum step → syncPhase → tick │
                 └───────────────┬──────────────────────────┘
                                 │ setDriver(...)
              ┌──────────────────┴───────────────────┐
              ▼                                       ▼
   SequentialDriver (DETERMINISTIC)        ThreadedDriver (ASYNCHRONOUS)
   all agents step() serially,             each agent step() on its own
   one host thread, index order,           std::jthread, std::barrier
   then syncPhase. The bit-identical       rendezvous per quantum, syncPhase
   ORACLE.                                 single-threaded at the barrier.
              │                                       │
              └───────────────┬───────────────────────┘
                              ▼
                  IAgent (abstract steppable unit)
                              │
              ┌───────────────┴───────────────┐
              ▼                                ▼
      AlphaCpuAgent (real CPU)          MockCpuAgent (test stand-in)
      wraps Machine::stepCycle()        LL/SC contention proof vehicle
```

**The determinism contract (load-bearing).** During `step()` an agent may touch
**only its own private state**. Every cross-agent mutation — a lock grant, a store
to shared memory, an IPI — is **staged as an `Effect`** and applied
**single-threaded, in fixed agent order, inside `syncPhase()` at the quantum
barrier**. This is exactly why the threaded path can be proven bit-identical to
the sequential oracle: the only cross-agent channel is the fixed-order staged
effects. Raw shared guest-memory read/written *mid-quantum* is **outside** the
guarantee (see `schedLib/README.md` review-gate item 2) — `quantum == 1` during
bring-up keeps that window closed.

---

## 2. File inventory (verified on disk 2026-06-28)

### The harness proper — `./schedLib/`
| Path | Role |
|------|------|
| `./schedLib/SmpHarness.h` | **The scaffold.** `IAgent`, `Dispatcher`, `LogicalClock`, `LockArbiter` (per-CPU LDx_L/STx_C reservation table, no host atomics), `Effect` staging enum (`LoadLocked`, `StoreCond`, `Store`, `Ipi`). |
| `./schedLib/SmpDrivers.h` | **The bifurcation.** `SequentialDriver` (deterministic oracle), `ThreadedDriver` (async, `std::barrier`-synced), `joining_thread` fallback. Toggled via `Dispatcher::setDriver()`. |
| `./schedLib/AlphaCpuAgent.h` / `.cpp` | Real Alpha CPU wrapped as `IAgent`; `step(q)` calls `Machine::stepCycle()` ≤ q times. Single agent (cpuId 0) today. |
| `./schedLib/MockCpuAgent.h` | Synthetic symmetric CPU; two-quantum LL/SC on a shared granule. Determinism proof vehicle only. |
| `./schedLib/README.md` | Status, validation log, the 5-point review gate. |
| `./schedLib/CMakeLists.txt` | Adds the above to `EMULATR_SOURCES` (V4 enumerates sources explicitly — no glob). |

### Tests
| Path | Role |
|------|------|
| `./tests/schedLib/smp_harness_tests.cpp` | DOCTest suite: `lock_arbiter_semantics`, `determinism_equivalence` (Sequential == Threaded, bit-identical), `driver_toggle`, `parked_agent_no_deadlock`, `determinism_across_quantum_sequential`. The harness is the verification fixed point. |

### Integration points the harness reaches into (NOT the harness itself)
| Path | Role |
|------|------|
| `./systemLib/Machine.{h,cpp}` | `Machine::run()` builds the `Dispatcher`, adds `m_agent0`, picks a driver, calls `disp.run()`. Exposes the shared per-cycle kernel `stepCycle(i)`. **Gated by `EMULATR_DISPATCH`** per `schedLib/README.md` (unset ⇒ legacy loop). *VERIFY current state: one exploration pass reported a later commit may have retired the legacy loop and made the dispatcher path unconditional — confirm against the live `Machine::run()` before relying on the toggle.* |
| `./pipelineLib/PipelineDriver.h` | Single-CPU instruction engine (`step()`) each agent ultimately drives. |
| `./coreLib/CpuState.h` | Per-CPU architectural state (int/FP regs, ~50 IPRs, ITB/DTB, `cpuSlot`). **The unit that multiplies for real SMP.** |
| `./chipsetLib/TsunamiChipset.h` / `./chipsetLib/TsunamiCchip.h` | Chipset is ~80% SMP-ready: `kMaxCPUs=4`, per-CPU IRQ arrays (`m_pendingIrq2/3[]`, `m_dim[]`, `m_iic[]`), `read/write(offset,cpuId)`, IPREQ/IPINTR. |
| `./config/EmulatorSettings.h` | `cpuCount`, `activeCpus` (both default 1). |
| `./deviceLib/HwrpbBuilder.h`, `./deviceLib/Hwrpb.h` | `PerCpuConfig`, multi-CPU HWRPB/PCS layout — builder already supports N CPUs. |

### Design / reference docs
`./journals/20260618_smp_secondary_cpu_bringup_design.md`,
`./journals/20260619_next_steps_alphacpuagent_gap.md`,
`./journals/20260620_smp_levers_and_hrm_mp_wiring_map.md`,
`./docs/diagrams/emulatr_smp_bachman_diagram.{svg,jpg}`.

---

## 3. Current state (honest)

- **Harness: built, compiled, validated GREEN.** `determinism_equivalence` proves
  Sequential == Threaded, **bit-identical (1666 lock grants identical across 5
  runs)**. `parked_agent_no_deadlock` + `lock_arbiter_semantics` green. The
  harness is the fixed point: a future determinism failure is the *new agent's*
  bug, not the harness's.
- **Live wiring: ONE real CPU.** `AlphaCpuAgent` (cpuId 0) under
  `SequentialDriver`. Both deterministic and async drivers exist and are proven,
  but only a single agent is deployed.
- **Not yet wired:** a second *real* CPU. Multi-CPU HWRPB/chipset storage exists;
  the missing weight is CPU-side: re-entrant stepper, per-CPU `CpuState`
  ownership, full `Effect` (real stores), and the Phase-3 LL/SC interlock.

---

## 4. Completion plan — code injection, ordered

Sequencing rule (from the gap journal): **P0 green → Phase-1 agent green on
Sequential → equivalence holds → THEN async → THEN profiler.** The platform lever
(P2) is independent and lands any time.

### Step 0 — Close the P0 cleanups (small, unblocks full-suite green)
- [ ] `CMakeLists.txt` ~689–690: delete the two `$<$<CONFIG:Debug>:/EHsc->` /
      `$<$<CONFIG:Release>:/EHsc->` lines (default is already `/EHsc`; gives all
      three configs exceptions). Confirm `_HAS_EXCEPTIONS != 0` on `Emulatr_tests`.
- [ ] Fix `tests/fBoxLib/test_float.cpp` null-CPU rot: FP leaves now deref
      `c.cpu->fpcr` but the test builds bare `ExecCtx{}`. Give each `ExecCtx` a
      `CpuState` (`ctx.cpu = &cpu`). Separate lane; harness green stands without it.

### Step 1 — Phase 1: real `AlphaCpuAgent` re-entrant behind `IAgent` (single agent)
This is the **code-injection core**. The four sub-items map to the gap journal #1–#3.
- [ ] **#1 Re-entrant retire-bounded stepper.** Invert `Machine::run`'s cycle loop
      into `step(q)`: execute ≤ q **retired** instructions, return at a clean
      retire boundary, carry pipeline + `CpuState` across calls. Dispatcher owns
      the clock. **Quantum boundary == instruction retire** (the existing
      commit-at-MEM / retire-at-WB point — do not invent a new drain). **Never
      yield mid-instruction, never mid-MMIO.**
- [ ] **#2 Per-CPU vs shared-service split.** `AlphaCpuAgent` owns `CpuState` +
      pipeline. `GuestMemory` (already one shared instance), chipset (per-cpuId
      arrays), and `LockArbiter` become **Dispatcher-level shared services**.
      Re-home `bindCycleSource`'s raw pointer and the interval-timer fire-edge onto
      the dispatcher's `LogicalClock`.
- [ ] **#3 Effect completeness (staging hooks only in Phase 1).** Extend
      `Effect::Store` (today touches only the lock table) to carry a real store
      (addr/value/size); `syncPhase` writes `GuestMemory` then breaks locks. Wire
      `Effect::Ipi` (no-op stub) to the landed Cchip IPREQ/IPINTR latch.
      `quantum == 1` kills the intra-quantum RAW hazard (Alpha = 1 mem op/instr;
      store publishes at the boundary, next instr sees it next quantum).

**Phase-1 minimum = #1 + #2 + staging hooks of #3, under `SequentialDriver` with
ONE agent.** Equivalence holds trivially; this proves a real Alpha steps
re-entrantly behind `IAgent` without regressing boot.

**Acceptance:** `EMULATR_DISPATCH=1` boot **byte-identical** to the legacy boot;
snapshot/resume still bit-identical. (This is the open Phase-1 gate in
`schedLib/README.md`.)

### Step 2 — Enable the async path for the single agent (validation, not throughput)
- [ ] Run the single real `AlphaCpuAgent` under `ThreadedDriver`. With one agent
      the barrier is trivial, but this exercises the threaded path end-to-end on a
      real CPU and is the gate before any multi-agent threading.
- [ ] Run it under **ThreadSanitizer** periodically. Equivalence proves "same
      result"; TSan proves "not by luck." (Gap journal guardrail.)
- [ ] **#5 MMIO side effects — FLAG, don't solve.** MMIO read is synchronous but
      mutates shared chipset state (read-clears, FIFO pops). Sequential + 1 CPU:
      direct call is fine. Threaded + >1 agent: serialize MMIO through the
      dispatcher or make the chipset thread-safe. Defer to Phase 3 / multi-agent.

### Step 3 — Phase 2: deterministic scheduler over N CPUs
- [ ] Replace the single-CPU step with round-robin over `[0, cpuCount)`; commit at
      quantum boundary, fixed order (CPU0, CPU1, …). Parked (halted) CPUs consume
      zero quanta.
- [ ] **Per-CPU interrupt divert indexing.** The divert polls keyed to CPU0
      (`pendingIrq2(0)`, `pendingIrq3(0)`, `b_irq<0/1>`) become
      `…(scheduledCpu)`. Chipset already exposes all per-cpuId — this is wiring
      the index, not new chipset work.
- [ ] **Console arbitration.** Define primary owns the console; secondary POST
      status routes through the per-CPU comm area (modeled fully in Phase 5).
- [ ] Flow `cpuCount` from the platform manifest to both the Cchip ctor (exists)
      and the new CPU-instance count.

### Step 4 — Phase 3: LDx_L / STx_C cross-CPU interlock (THE CLIFF)
- [ ] Per-CPU `lock_flag` + `lock_physical_address` (granule-aligned; confirm
      granule vs 21264 HRM).
- [ ] **#4 LL/SC split FSM.** Real `STx_C` writes 0/1 into Ra in the *same*
      instruction, but the arbiter resolves SC at the sync boundary → the agent
      needs Mock-style "SC pending" state (stage `StoreCond`, yield, read ack next
      quantum, write Ra). Precise rule: **never yield mid-instruction** (other
      agents' stores MUST interleave at the LL→SC boundary — that's how contention
      is detected).
- [ ] **Cross-CPU invalidation (the crux).** Any store to a granule — normal STx,
      successful STx_C, or device/DMA write — by ANY agent clears every *other*
      CPU's lock_flag on that granule. Under cooperative scheduling this is a
      synchronous walk of the per-CPU lock table at commit (no host atomics).
- [ ] **DMA participation.** The Pchip DMA write path calls the same invalidation
      hook.

### Step 5 — Phase 4: cross-CPU IPI (integrate landed Cchip work)
- [ ] Extend `b_irq<3>` divert from `pendingIrq3(0)` to `pendingIrq3(scheduledCpu)`
      (folds into Step 3 indexing). Full round trip MISC<IPREQ> → IPINTR latch →
      target CPU diverts → handler W1Cs. Apply P4-decoded delivery constants (EI
      bit, IPL, rank) — replace provisional `1<<36` / IPL 20.

### Step 6 — Phase 5: secondary bring-up / rendezvous responder (GATED on P1/P2/P3)
- [ ] **Do not task until the P1/P2/P3 measurements land** — a stub answering a
      guessed/partial handshake deadlocks mid-protocol, indistinguishable from a
      hang. HWRPB PCS slot modeling, presence-detect (manifest-gated), secondary
      release, exact multi-step responder. Per the 06-18 editor's note, the real
      check-in to capture is `start_secondary` (pc264.c:333 `outtig 0xC00028+id`)
      / `secondary_start` (entry.c:425) — **not** 0xBFFC (that's the ISP flag).

### Step 7 — Phase 6: determinism extension + verification
- [ ] Serialize ALL N CPU contexts **plus the scheduler interleave cursor**
      (next-agent, quantum position); bump snapshot version. Also closes the latent
      IPI-latch snapshot gap if latches rebuild from `m_misc` on deserialize.
- [ ] Boot profiler (`RetireProfiler` behind `EMULATR_BRINGUP_PROBES`) → make
      per-agent and read the **logical** clock, not wall time, or numbers aren't
      reproducible.

### Independent lane — P2 platform lever (land any time)
- [ ] `EMULATR_PLATFORM = isp | silicon` — one knob → 0xBFFC answer → firmware
      `platform()`. Realize as the existing **read-intercept** of PA 0xBFFC (a
      deposit-at-init is clobbered by the SRM self-decompressor). Supersedes
      `EMULATR_CPU1_ALIVE`. No split-brain (no independent host sim/silicon flag).

---

## 5. Test plan (what proves each step)

| Gate | Test | Where |
|------|------|-------|
| Harness fixed point | `determinism_equivalence`, `lock_arbiter_semantics`, `parked_agent_no_deadlock`, `driver_toggle` | `tests/schedLib/smp_harness_tests.cpp` (GREEN) |
| Step 0 | Full DOCTest suite green (after `/EHsc` + `test_float` fixes) | `tests/**` |
| Step 1 (Phase 1) | `EMULATR_DISPATCH=1` boot **byte-identical** to legacy boot; snapshot/resume bit-identical | boot trace-diff vs legacy |
| Step 2 (async) | Single real agent under `ThreadedDriver` == Sequential; clean under **TSan** | extend `smp_harness_tests.cpp` + TSan CI lane |
| Step 3 (Phase 2) | 2 agents advance deterministically; snapshot at any quantum boundary resumes bit-identically; fixed-seed re-run → identical interleaved retire trace | new multi-agent doctest |
| Step 4 (Phase 3) | **Dedicated micro-test** (do NOT rely on firmware): 2 CPUs contend on one lock_flag word for N iters → exactly one holder, total acquisitions == N, no double-acquire, no livelock | new `ll_sc_contention` doctest |
| Step 5 (Phase 4) | CPU0↔CPU1 IPI delivered + acked; ping-pong N round trips deterministic; snapshot/resume across an in-flight latched IPI | new IPI doctest |
| Step 6 (Phase 5) | SRM POST prints secondary bring-up line, reaches `>>>`; `show cpu` reports both present/available; single-socket manifest unchanged | firmware boot |
| Step 7 (Phase 6) | Deterministic re-run → identical 2-CPU trace; SMP guest (Tru64/VMS) reaches login | integration |

**Guardrails (non-negotiable):**
- `determinism_equivalence` stays green at every step. A red = the agent leaked
  shared state into its concurrent phase — that **is** the bug.
- Do not integrate the threaded path for >1 agent until the agent is green on
  Sequential AND equivalence holds.
- The Phase-3 LL/SC micro-test **must pass before any firmware SMP result is
  trusted** — livelock there presents identically to a boot hang.

---

## 6. One-paragraph summary

The SMP harness is `schedLib/SmpHarness.h` (the agent/dispatcher/clock/lock-arbiter
scaffold) + `schedLib/SmpDrivers.h` (the deterministic `SequentialDriver` ⇄
asynchronous `ThreadedDriver` bifurcation) + `schedLib/AlphaCpuAgent.{h,cpp}` (the
real-CPU adapter), validated by `tests/schedLib/smp_harness_tests.cpp` and invoked
from `systemLib/Machine.cpp::run()`. It is **built and proven bit-identical across
both paths**, driving **one** real CPU today. Finishing it is, in order: close the
P0 build/test cleanups, inject the re-entrant retire-bounded `AlphaCpuAgent` and
its per-CPU/shared-service split (Phase 1, byte-identical-boot gate), prove the
single agent on the threaded path under TSan, then expand to N CPUs (scheduler →
LL/SC interlock cliff → IPI → gated rendezvous → SMP-aware snapshot). The platform
lever (`EMULATR_PLATFORM`) lands independently whenever convenient.
</content>
</invoke>
