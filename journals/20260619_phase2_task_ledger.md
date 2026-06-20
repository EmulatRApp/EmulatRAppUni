================================================================================
EmulatR V4 -- Phase 2 (CpuState ownership lift) TASK LEDGER
================================================================================
Written: 2026-06-19.  Durable cross-session to-do list for the AlphaCpuAgent
Phase-2 work.  Authoritative design: 20260619_alphacpuagent_phase2_ownership_lift_
design.md (+ DECISIONS D-1/D-1a/D-2).  Logging policy: 20260619_phase2_logging_
policy_addendum.md.  Each task is independently gated by tools/phase1_dispatch_
gate.sh (byte-identical vs the legacy loop, which STAYS the oracle until STEP 5).

DONE (this session, 2026-06-19):
  - STEP 1a: Machine::systemNow() indirection (RTC L1 seam marked; interval timer
    L2 @1297/1321; flash @1329; snapshot cadence+naming; IDLEWARP write seam
    marked; m_injectInterruptCycle L2b @1587 routed).  GATE PASSED + COMMITTED.
  - STEP 1b (partial): CpuState.cpuSlot field + snapshot v7->v8 + LookbackEntry.
    cpuId + freezeRecord population (sole compiler-enforced path).  Sink EMIT and
    re-baseline NOT done -- see P2-T1.

--------------------------------------------------------------------------------
OPEN TASKS (in dependency order)
--------------------------------------------------------------------------------

P2-T1  FINISH STEP 1b -- emit the cpuId tag + one-time re-baseline.
  WHERE: traceLib/DecListingSink.cpp -- emitRetireCompact (RET line ~400),
    emitCommit (machine "INS cycle=" ~514 AND the DEC human-readable listing
    line), the format-doc header comment (~288); optionally REG/FRG/HEARTBEAT/
    PAL_ENTRY/PAL_EXIT.  Source the value from the entry/postCommitCpu already in
    hand (LookbackEntry.cpuId is populated; dump paths use the frozen entry).
  ACCEPTANCE (from the logging addendum STEP-1b contract): cpuId on EVERY trace
    line; single shared stream; single agent emits cpu0; refresh the STORED
    golden + AXPBox references ONCE in the same commit (the tag changes trace
    text); phase1_dispatch_gate.sh PASS (needs NO re-baseline -- tag is on both
    sides, cancels); dry-run a synthetic cpu1 line to prove format stable.
  NOTE: NOT the global retire ordinal -- that is P2-T3.  Keep the existing cycle
    column as per-CPU PCC; do not promote it.
  RENAME (Tim, 2026-06-20): rename the trace `cyc=` label -> `rpcc=` (per-CPU PCC;
    synonymous in our usage); rename INS/REG/FRG/HEARTBEAT `cycle=` + the DEC
    cycle column to match; re-mint golden once.  P2-T3's global ordinal stays a
    SEPARATE field, never `rpcc=`.
  TOOLING: the `cpu=` add + `cyc=`->`rpcc=` rename BREAK tools/analyze_retire_
    trace.py (positional regex).  Update to
    `RET\s+(?:cpu=\d+\s+)?rpcc=(\d+)\s+pc=...`; audit other positional RET/INS
    consumers for the same break.

P2-T2  STEP 2 -- split stepCycle into cpuKernel(cpu) + systemTick(now).
  cpuKernel = interrupt poll + PipelineDriver::step for one cpu, no global side
  effects.  systemTick = stop-sentinel, interval-timer fire+divert, flash flush,
  snapshot cadence+naming, IDLEWARP -- ONCE per quantum, dispatcher-level.  Move
  the carried statics (s_idleTickWarp, s_warpLog, s_cnt) to systemTick.  This is
  the structural cliff of the phase (a mis-assigned line double-fires under a 2nd
  agent).  GATE: byte-identical.
  APPLIED 2026-06-20 (UNBUILT -- client build + gate pending): Machine.{h,cpp}.
    cpuKernel(CpuState&) = step() only (the one per-CPU action); systemTick(uint64_t
    i) = sentinel + snapshots + evalDeviceIrqs + IDLEWARP + interval-timer
    FIRE/DELIVER + b_irq diverts + synthetic inject; stepCycle(i) = { if
    (!cpuKernel(m_cpu)) return false; return systemTick(i); }.  Split BY CURRENT
    OWNERSHIP (Tim's deciding principle: relocate by how each line reads TODAY, not
    anticipated SMP ownership, since the gate proves single-agent equivalence only,
    not split-correctness).  SEAMS LEFT IN systemTick + flagged in code: DELIVER
    (reads pendingIrq2(0) = hardcoded CPU0) and snapshot-on-PC (reads m_cpu.pc) ->
    revisit at STEP 4.  The three process-global statics moved with their system
    blocks.  Only behavioral reorder: stop-sentinel poll now AFTER step (no-op when
    the sentinel is absent, so the boot gate is unaffected).  DEVIATION FROM DESIGN:
    kept bool/uint64_t signatures, did NOT introduce StepStatus/Tick (minimize
    surface; revisit at STEP 3 if systemNow() decoupling needs the richer types).

P2-T3  STEP 3 -- decouple systemNow() + add the global retire ordinal.
  Make systemNow() its own counter advancing by the PER-STEP RETIRE CYCLE DELTA
  (INVARIANT D-1a: identical to PCC, NOT per-iteration/per-quantum -- a wrong
  advance rate fails this gate).  Add the dispatcher's cross-agent monotonic
  global retire ordinal to the trace line (the deferred half of the logging
  policy; do NOT make it any CPU's cycleCount).  Confirm all L2 consumers + L2b
  inject arm track the system clock.  GATE: byte-identical.
  SPLIT (Tim 2026-06-20): T3a = clock decouple (gate-critical, no re-mint) FIRST,
    prove byte-identical, THEN T3b = global retire ordinal (re-mint).  Rationale:
    a re-mint riding on an unproven clock LAUNDERS a delta-accounting miss into the
    new golden, after which the bug passes every future gate.  T3b's re-mint must
    happen against an already-proven-byte-identical clock.
  cycleCount-WRITE ENUMERATION (exhaustive grep, 2026-06-20) -- the set m_systemClock
    must mirror (RAW cycleCount, pre-ccOffset/pre-kCcMultiplier; HW_CC writes ccOffset
    NOT raw cycleCount, so guest PCC writes can't desync the timebase):
      * in cpuKernel (swept in aggregate by the stepCycle before/after delta):
        PipelineDriver.h 178/207/746 (++), 318/355/421 (+= time advance).
      * Machine.cpp:1328 IDLEWARP jump -- explicit mirror at site (system-clock-
        primary form, PCC tracks it, so STEP 4 needn't revisit).
      * snapshot LOAD + reset (resetToLoadedEntry, ctor) -- m_systemClock =
        m_cpu.cycleCount after the CpuState lands (Machine-level, off cpuKernel).
    RESOLVED FINDING: HwpcbContext.h (swpctx) formerly wrote raw cycleCount = src.cc
    (off boot-gate path -> dormant-arm hole).  FIXED 2026-06-20 to route the per-
    process PCC through ccOffset (load: ccOffset = src.cc - cycleCount; store:
    dst.cc = cycleCount + ccOffset), mirroring HW_CC MTPR/MFPR.  swpctx therefore
    no longer writes raw cycleCount and is NO LONGER a mirror site -- the timebase
    is insulated from context switches.
  T3a APPLIED 2026-06-20 (UNBUILT -- client build + gate pending): Machine.{h,cpp}.
    New member m_systemClock{0}; systemNow() returns it (was m_cpu.cycleCount).
    stepCycle advances it by the RAW per-step PCC delta (pccBefore vs post-cpuKernel
    cycleCount), halt or not.  IDLEWARP rewritten system-clock-primary (m_systemClock
    = (systemNow()|mask)+1; m_cpu.cycleCount tracks).  Resync m_systemClock =
    m_cpu.cycleCount in resetToLoadedEntry + restoreSrmStaging (the dormant-arm
    reset/autoload path the boot gate never hits -- handled by enumeration, not gate).
    Single agent => m_systemClock == m_cpu.cycleCount at every read -> byte-identical.
    NOTE: bash mount served a STALE-TRUNCATED phantom of Machine.cpp during verify;
    host Read confirmed the file intact (the recurring D: mount hazard).
    NEXT: T3b = global retire ordinal (separate commit, re-mint AFTER T3a gate green).
  T3b APPLIED 2026-06-20 (UNBUILT -- client build + gate + one-time re-mint pending):
    traceLib/DecListingSink.{h,cpp}.  New member m_retireOrdinal, incremented once per
    onCommit (every retire, traced or not), stamped into LookbackEntry.ordinal via
    freezeRecord (sole-path, like cpuId).  Emitted as the LEADING `ord=` field (DEC
    listing `o<n>` column) on EVERY line: RET/INS/DEC/REG/FRG/HEARTBEAT/PAL_ENTRY/
    PAL_EXIT/RUN_END; format-doc headers updated.  Source = the single coalesced sink's
    per-retire counter, which IS the dispatcher global retire order under the single-
    sink design (NOT cycleCount, NOT rpcc).  analyze_retire_trace.py regex tolerates
    optional ord=; test_declistingsink pins updated (INS/REG ord=0, o0 column).
    Dispatch gate stays byte-identical (gate runs without --trace -> ord absent from the
    host log).  RE-MINT the stored golden + AXPBox refs ONCE in this commit (trace text
    changed) -- only now, AFTER T3a's gate proved the clock byte-identical, so the
    re-mint cannot launder a clock bug.  Phase-2 NEXT: T4 = CpuState ownership into
    AlphaCpuAgent (the real CPU1 prerequisite), then T5 whami reconcile, T6 flip+delete.

P2-T4  STEP 4 -- CpuState ownership into AlphaCpuAgent.
  Agent owns CpuState; Machine::cpu() returns agent0's.  SET cpuSlot from the
  agent's id() (closes the STEP-1b "single agent = 0" placeholder -> real slot).
  Re-home L1 (RTC bindCycleSource @~351) + the IDLEWARP write @~1305 to the
  system clock.  Keep trace + spdlog sinks Dispatcher-level shared services.
  Snapshot must still round-trip agent0.  GATE: byte-identical + determinism_
  equivalence + snapshot round-trip green.
  APPLIED 2026-06-20 (UNBUILT -- client build + 3-way gate pending): AlphaCpuAgent
    now OWNS a CpuState (m_cpuState; cpu() accessor; m_cpuState.cpuSlot = cpuId in
    ctor; resetForRun() resets ONLY m_cycleIndex/m_stopped).  Machine owns a
    persistent by-value m_agent0; m_cpu is now a CpuState& ALIAS bound to
    m_agent0.cpu() in the sole ctor (init list m_settings, m_agent0(*this,0),
    m_cpu(m_agent0.cpu())) -- so ~all m_cpu.<field> sites compile UNCHANGED.
    Dispatch path reuses m_agent0 (resetForRun + addAgent) instead of a transient
    local.  SAFE: Machine non-copy/non-move-CONSTRUCTIBLE (all 4 deleted) -> the
    reference never reseats; m_agent0 address-stable -> bindCycleSource's raw
    &m_cpu.cycleCount + the L1 RTC seam stay valid with NO change (T3a already
    routed systemNow()/IDLEWARP off cycleCount).  AlphaCpuAgent.cpp runnable()/
    step() UNCHANGED (m_machine->cpu() == agent0's m_cpuState via the alias).
    cpuSlot = agent0.id() = 0 -> byte-identical (cpu= tag stays 0).  Single Machine
    ctor (default args) so the reference inits everywhere; no other AlphaCpuAgent
    construction; harness determinism uses MockCpuAgent (untouched).
  CORRECTION (T5, 2026-06-20): the T4 "cpuSlot not serialized" claim was WRONG.
    Snapshot.cpp:122 writes the WHOLE CpuState as a raw POD blob (writeRawData(&cpu,
    sizeof(cpu))); the field-by-field `ds << cpu.cycleCount` above it is just a
    redundant header copy.  So cpuSlot IS serialized and round-trips.  There is NO
    cpuSlot-specific gap; the real Phase-6 item is serializing ALL agents' CpuStates
    (one POD blob per agent), already the deferred N-CPU snapshot work.
  NEXT: T5 = whami-cpuid reconciliation (route WHAMI / CSERVE$WHAMI + HWRPB whami
    through the real cpuSlot; retype/retire the mis-typed mCpuId).  Then T6 flip+delete.

P2-T5  whami-cpuid RECONCILIATION (do with/after STEP 4).
  Today there are TWO slot-ish sources: the NEW CpuState.cpuSlot (numeric, used
  by tracing) and the OLD mCpuId (typed as CpuType MODEL enum, dormant -- the
  pre-existing TODO(whami-cpuid) in CpuState.h).  Unify to ONE: route PalEntries
  WHAMI (execMfprWhami / CSERVE$WHAMI @PalEntries.cpp ~542/~851) and HWRPB whami
  (HwrpbBuilder) through the real slot; retype/retire the mis-typed mCpuId.  Do
  NOT leave two divergent slot sources long-term.
  APPLIED 2026-06-20 (UNBUILT -- client build + gate pending): chose CLEAN removal
    (option a, Tim).  Both PAL WHAMI sites now read c.cpu->cpuSlot: CSERVE$WHAMI
    (PalEntries.cpp:549) and execMfprWhami (:872, un-maybe_unused'd c).  Single
    agent => cpuSlot 0 -> byte-identical to the prior hardcoded 0.  REMOVED the
    dormant mis-typed mCpuId/cpuId()/setCpuId() from CpuState.h (no production
    caller; AlphaCpuAgent::cpuId() is a separate, unrelated method).  POD-blob
    layout shrank -> kCpuStateVersion 8 -> 9 (Snapshot.h, + history entry); pre-v9
    snapshots rejected at load (round-trip test still passes -- same layout in-run).
    HWRPB whami left as-is: primary_cpu_id is build-time spec config (HwrpbBuilder
    .cpp:251), not a CpuState decode path, already 0 for one CPU.  cpuSlot is now
    the SINGLE "which CPU" source (trace cpu= tag + both WHAMI reads).
  NEXT: T6 = flip EMULATR_DISPATCH default-on + delete the legacy Machine::run loop
    (D-2 -- its OWN one-variable commit after Phase-2 acceptance; determinism harness
    becomes the sole gate).  Then Phase 2 is CLOSED; Phase 3 (LL/SC interlock) begins.

P2-T6  STEP 5 -- flip default + delete legacy loop (POST-Phase-2 acceptance only).
  Per D-2: a SEPARATE one-variable commit AFTER the dispatcher path has been the
  proven-equivalent default across Phase-2 acceptance.  Flip EMULATR_DISPATCH
  default, delete the legacy Machine::run loop; the determinism harness becomes
  the sole gate.  Until then legacy STAYS the gate's reference.
  APPLIED 2026-06-20 (UNBUILT -- client build + determinism harness pending):
    Machine::run now has ONLY the dispatcher path (one AlphaCpuAgent = agent0 under
    SequentialDriver, calling the shared stepCycle).  Deleted the s_useDispatcher /
    EMULATR_DISPATCH env gate AND the legacy direct for-loop.  GATE RETIRED:
    phase1_dispatch_gate.sh has no legacy oracle to diff against (it would now
    compare the dispatcher path to itself -- vacuous PASS), so retire/repurpose it;
    determinism_equivalence (schedLib) is the sole acceptance gate going forward.
    Cleared because Phase-2 acceptance was met (T1..T5 green + gate byte-identical).
  ===========================================================================
  PHASE 2 CLOSED 2026-06-20.  T1..T6 landed: trace cpuId/rpcc + global ordinal,
  stepCycle split, system-clock decouple, CpuState ownership in AlphaCpuAgent,
  whami->cpuSlot unification, legacy loop deleted.  The dispatcher/agent path is
  THE path; CpuState is agent-owned; clock + sinks are dispatcher-level shared.
  NEXT = PHASE 3: LL/SC cross-CPU interlock (THE cliff) -- per-CPU lock_flag/
  lock_physical_address, any store clears other CPUs' flag on that granule,
  LockArbiter real backing, a non-negotiable contention micro-test, never yield
  MID-INSTRUCTION but interleave at the LL/SC boundary.  See memory.md deferred
  list + journals/20260618_smp_secondary_cpu_bringup_design.md.
  ===========================================================================

DOWNSTREAM (already in memory.md deferred list; not Phase 2):
  Phase 3 (LL/SC cross-CPU interlock + LockArbiter real backing + contention
  micro-test -- THE cliff); Phase 4 (per-CPU IPI divert + P4 constants); Phase 5
  (rendezvous responder, gated on P1/P2/P3 measurements); Phase 6 (N-CPU +
  scheduler-cursor snapshot serialize).  A real 2nd CPU stays deferred until the
  Phase-2 split + P2-T4 land.
================================================================================
