# Checkpoints — 2026-06-20

## 04:00 — Phase 2 work durably tasked; STEP 1b struct+population applied but UNCOMMITTED
- **Working on:** Closing out the Phase 2 (AlphaCpuAgent ownership-lift) planning boundary and making the deferred work survive across sessions. Active session ("Next steps") ended at a clean stopping point with the user signing off; no new code beyond the STEP 1b intermediate.
- **Done since last checkpoint:**
  - **Phase 2 task ledger written** — `Emulatr/journals/20260619_phase2_task_ledger.md`, the authoritative enumerated list, in dependency order: **P2-T1** finish STEP 1b (DecListingSink cpuId emit + golden/AXPBox re-baseline — the held mechanical pass); **P2-T2** `stepCycle` split; **P2-T3** clock decouple + global retire ordinal; **P2-T4** CpuState ownership + `cpuSlot = id()`; **P2-T5** `whami-cpuid` reconciliation (unify new `cpuSlot` with dormant mis-typed `mCpuId`/WHAMI); **P2-T6** post-acceptance flip+delete.
  - Ledger **indexed in `memory.md`** journal table with a "read FIRST when resuming Phase 2" note; six items mirrored into the session task list (P2-T1 … P2-T6) for in-session visibility.
  - **STEP 1b struct+population applied** (compilable, boot-identical intermediate): `CpuState.cpuSlot` added; `LookbackEntry.cpuId` capture wired; snapshot bumped to **v8**; `freezeRecord` signature changed. cpuId is **captured, not yet emitted** (emit + re-baseline deferred to P2-T1). Touched: `coreLib/CpuState.h`, `systemLib/Snapshot.h`, `traceLib/DecListingSink.{h,cpp}`.
- **Open / next:**
  1. **Commit the clean intermediate** — STEP 1b struct+population is applied but **uncommitted**. Recommended: client-side MSVC build to confirm the `freezeRecord` signature + `cpuSlot`/v8 bump compile, then commit on its own (`git add` CpuState.h, Snapshot.h, DecListingSink.{h,cpp}, the two phase2 journals, memory.md) so the tree is clean for next session.
  2. **Then start P2-T1 fresh** — wire the DecListingSink cpuId emit and do the golden/AXPBox re-baseline deliberately.
- **Watch-outs:**
  - **P2-T1's risk is the re-baseline, not the string edits.** It modifies the verification oracle itself; a rushed re-baseline silently weakens the gate for everything downstream in Phase 2 and still passes — the "coincidentally-right, untested" failure class. Do it fresh, not tired. (User explicitly chose to stop before it.)
  - STEP 1b intermediate is **unbuilt** in this session (sandbox can't build; MSVC is client-side). Build before relying on it.
  - D: mount can serve stale cached copies — validate writes via the Read (host) view, not bash byte counts.

## 15:06 — SMP readiness review: three-lever disambiguation + HRM §27.8.1.1 mapping (no code)
- **Working on:** Active session ("Readiness check") is a pre-coding readiness review for booting CPU1. User pushed back on starting secondary-CPU boot, citing an unbound `CpuState` and missing wiring per the attached Alpha HRM §27.8.1.1 (primary selection before any main-memory access; console must run from dedicated RAM/ROM/cache independent of main memory). Outcome: confirmed CPU1 is **not** bootable yet; immediate next coding step is unchanged (P2-T1).
- **Done since last checkpoint:**
  - **Three "levers" disambiguated** so they stop being conflated across sessions: (1) `EMULATR_DISPATCH` = execution-path select (legacy `Machine::run` oracle ↔ `AlphaCpuAgent` dispatcher; one agent either way, NOT a 2nd CPU); (2) `IExecutionDriver` swap (`SequentialDriver`↔`ThreadedDriver`, `std::barrier`-synced) = the deterministic-async mechanism, proven bit-identical by `determinism_equivalence`, but only a mock/single agent behind it today; (3) `EMULATR_PLATFORM` = misnamed-by-lineage (replaced `EMULATR_CPU1_ALIVE`), intercepts PA `0xBFFC` read → returns `0xCAFEBEEF` so firmware resolves `ISP_MODEL` and reaches `>>>` (`=silicon` removes intercept → `REAL_HW`). 06-18 root-cause proved `0xBFFC` is the ISP-model detection flag, **not** a CPU1 rendezvous. None of the three boots a secondary.
  - **Confirmed `CpuState` is unbound** — agent references `CpuState` via the bound `Machine`; does not own it. One register file exists; per-agent ownership is **P2-T4** and we're only at P2-T1, so a 2nd `CpuState` for CPU1 is structurally absent. P2-T2 (`stepCycle` → `cpuKernel`/`systemTick` split) is the prerequisite preventing system side-effects (interval timer, flash flush, snapshot cadence) from double-firing under a 2nd agent.
  - **HRM §27.8.1.1 mapped to V4 phases** (via `HwrpbBuilder.cpp` audit): per-CPU slot array w/ state flags + pal_rev/cpu_type already built; **missing** for a secondary to consume — primary selection via shared mutex/`LDQ_L`/`STQ_C` before main-mem access (**Phase 3** LL/SC interlock, "the cliff"); console-from-independent-memory invariant (**Phase 5**); per-CPU **console comm area RX/TX + RXRDY/TXRDY** that the secondary spins on (absent — new wiring); IPI primary→secondary (**Phase 4**; Cchip IPREQ→IPINTR→b_irq<3> landed 06-18 but TIG `ipcr` is storage-only, no delivery); **WHAMI** vs HWRPB primary id (**P2-T5**; two divergent slot sources `CpuState.cpuSlot` vs mis-typed `mCpuId` to unify); **PE** (Primary-Eligible) bit + **BB_WATCH** (not yet ticketed).
- **Open / next:**
  1. Immediate coding step **unchanged: P2-T1** (DecListingSink cpuId emit + deliberate golden/AXPBox re-baseline), still gated on committing the STEP 1b struct+population intermediate first.
  2. User considering a short **no-code journal** to pin (a) the three-lever disambiguation and (b) the §27.8.1.1 → Phase-3/4/5 checklist, folding the new **RXRDY/TXRDY comm area** and **PE-bit/BB_WATCH** items into Phase 5's HWRPB-slot modeling. Awaiting user confirm to draft it.
- **Watch-outs:**
  - Kill the misconception that any "lever landing" means CPU1 is reachable — CPU1 stays parked behind Phase 2 (ownership) → Phase 3 (LL/SC) → Phase 5 (HWRPB secondary consumption + comm area).
  - Two divergent CPU-id sources (`CpuState.cpuSlot` vs dormant mis-typed `mCpuId`/WHAMI) must be unified at P2-T5 before any secondary trusts its slot.
  - Still no committed build of the STEP 1b intermediate (see 04:00 entry) — that precondition for P2-T1 remains open.

## 21:06 — HwpcbContext raw-write fix landed (per-process PCC → ccOffset); timebase insulated from swpctx
- **Working on:** Same "Readiness check" session moved from review into code: decoupling the per-process cycle counter from the system timebase so context switches can't perturb raw `cycleCount`. This is the substance of P2-T3 (clock decouple) being de-risked ahead of P2-T1.
- **Done since last checkpoint:**
  - **HwpcbContext raw-write fix applied** (two `Edit`s). Both sides now route the per-process PCC through `ccOffset`, mirroring `HW_CC` MTPR/MFPR: **load** `ccOffset = src.cc − cycleCount`; **store** `dst.cc = cycleCount + ccOffset`. Raw `cycleCount` (the system timebase) is no longer written by context switches.
  - **Verified** ASCII-clean + both sides ccOffset-based (bash check + Read of the swpctx leaf).
  - **swpctx inherits the fix for free** — `execSwpctxVms` already uses these helpers and reads/writes `cc` at **HWPCB+0x40**, so it writes `ccOffset`, not raw `cycleCount`. It therefore **drops out of the T3a cycleCount-write enumeration entirely**, shrinking T3a's set to the in-`cpuKernel` retire sites (swept by the delta) + IDLEWARP + snapshot/reset mirror.
  - **Recorded:** T3a enumeration update in the Phase-2 ledger (now closed *and* shrunk), and the **HWRPB-mapping TBD** in the SMP journal (STORE-WATCH/PA-write profiler over primary cold boot → identify HWRPB by self-describing header: self-PA + checksum + offset table, not by size → map per-CPU SLOT/RXRDY offsets for the Phase-5 secondary hooks).
- **Open / next (user's fork, awaiting choice):**
  1. **Build + test + commit** the accumulated work (P2-T1 STEP 1b intermediate, P2-T2, port hardening, HwpcbContext) for a clean green baseline — *recommended first*.
  2. **Implement T3a** (clock decouple) — enumeration now closed and provably complete, ready to start.
  3. **Run HWRPB profiling** — needs a profiled primary-CPU boot capture, then analyze here.
- **Watch-outs:**
  - HwpcbContext fix is **applied but unbuilt/uncommitted** — sandbox can't build (MSVC is client-side). Build before relying on it; commit to land it on the proven baseline.
  - The STEP 1b intermediate is still uncommitted (carried from 04:00/15:06) — HwpcbContext now stacks on top of it, so the commit should cover both.
  - D: mount can serve stale cached copies — validate writes via the Read (host) view, not bash byte counts.

## 23:06 — P2-T5 in progress: WHAMI→cpuSlot reroute + clean mCpuId removal (kCpuStateVersion 8→9)
- **Working on:** Same "Readiness check" session, now executing **P2-T5** (whami-cpuid reconciliation). Chose option **(a) clean struct**: unify on `CpuState.cpuSlot`, fully delete the dormant mis-typed `mCpuId`, and bump the snapshot version.
- **Done since last checkpoint:**
  - **Snapshot serializer correction** — confirmed `Snapshot.cpp:122` writes the entire `CpuState` as a raw POD blob (`ds.writeRawData(&cpu, sizeof(cpu))`); the field-by-field `ds << cpu.cycleCount` above it is a redundant header copy. So `cpuSlot` already round-trips (it lives inside the blob). **Corrects the earlier T4 ledger note** that claimed `cpuSlot` was unserialized → Phase-5 gap; that claim was wrong (grep missed it because the blob is a `memcpy`, not a named field). T4 note being fixed.
  - **mCpuId removed from the struct** — Tim deleted the three lines in `coreLib/CpuState.h` himself (`mCpuId` field + `cpuId()` + `setCpuId()` accessors); field is gone. Because the snapshot blob is `sizeof(cpu)`, removing the field is a layout change → **`kCpuStateVersion` bump 8→9** (same mechanism STEP 1b used adding `cpuSlot`). Old snapshots version-gated/rejected; round-trip test still passes; boot gate unaffected (byte-identical).
- **Open / next:**
  1. **Finish the WHAMI reroute** — point both sites at `cpuSlot`: `CSERVE$WHAMI` (`PalEntries.cpp:548`) and `MFPR_WHAMI` (`execMfprWhami`, ~PalEntries.cpp:871) change `r.regWriteValue = 0;` → `= c.cpu->cpuSlot;` (and un-`maybe_unused` the `ExecCtx`). `cpuSlot = 0` for agent0 → byte-identical. (Mid-edit when checkpointed.)
  2. Clean the now-stale comments around the removed `mCpuId` block in `CpuState.h`/`Snapshot.h`; finish the T4 ledger-note fix.
  3. Build (client-side MSVC) + commit — this stacks on the still-uncommitted STEP 1b + HwpcbContext intermediates.
- **Watch-outs:**
  - HWRPB `whami` (`HwrpbBuilder.cpp:251`) is `primary_cpu_id` from build `spec`, **not** from `CpuState` — leave it; it's OS-handoff config, already 0, not a decode path.
  - File churn mid-edit: Tim edited `CpuState.h` by hand while the session was editing it → an Edit missed and required a re-Read. Re-read regions before re-editing to avoid clobbering manual deletions.
  - Accumulated uncommitted stack is now three deep (STEP 1b → HwpcbContext → T5). A single build/commit should cover all; tree is not green until then.
  - D: mount can serve stale cached copies — validate writes via the Read (host) view, not bash byte counts.

## 01:06 — Phase 3 red-first: failing LockArbiter LL/SC contract micro-test landed
- **Working on:** Same "Readiness check" session crossed into **Phase 3** (LL/SC interlock — "the cliff"). Wrote the executable spec for per-CPU reservation semantics as a direct `LockArbiter` contract test, deliberately landed **red first** before touching the arbiter itself.
- **Done since last checkpoint:**
  - **Three `Phase3 LockArbiter` doctest cases appended** to the LockArbiter test file (host Grep confirms them at lines **206 / 222 / 238**; `AgentId = int`, `LockArbiter` directly constructible, doctest style). Confirmed via host Grep — the bash 186-line count was the stale-mount phantom again, Edit succeeded on host.
  - Expected build result is **exactly two new failures** against the current one-holder arbiter: `a load does not clear another CPU's reservation` and `exactly one STx_C wins a contended granule` (a 2nd CPU's `loadLocked` overwrites the single holder, so the 1st CPU's `STx_C` wrongly fails). The third case — `a plain store breaks every OTHER CPU's reservation` — **passes** as a co-invariant guarding against over-correction. That red confirms the spec catches the bug; nothing else should change.
  - Design traced for the green step (held separate on purpose): refine `LockArbiter` from one-holder-per-granule to **per-CPU reservations** — `m_reservation: AgentId → granule`; `loadLocked` sets only that agent's reservation; both `storeCond` (on success) and `store` clear *every* agent whose reservation names that granule. Traced safe for `determinism_equivalence` (both drivers share the one arbiter resolved in `syncPhase`, stays bit-identical).
- **Open / next:**
  1. **Build to see the red** (client-side MSVC) — confirm exactly the two expected new failures, third case green.
  2. On "go", **land the `LockArbiter` per-CPU reservation refinement** to turn it green — the actual Phase-3 interlock core.
- **Watch-outs:**
  - Refinement was held as a separate step so the build shows clean red first, and so any perturbation of an *existing* grant-count assertion is caught in isolation rather than tangled with the new tests.
  - `MockCpuAgent` already does the two-quantum LL/SC and `syncPhase` drains effects in agent order — the one-holder bug is directly expressible as this `LockArbiter` contract test.
  - Still no committed/built baseline — the uncommitted stack (STEP 1b → HwpcbContext → P2-T5) remains open beneath this Phase-3 test; tree not green until built+committed.
  - D: mount can serve stale cached copies — validate writes via the Read (host) view, not bash byte counts.
