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

P2-T2  STEP 2 -- split stepCycle into cpuKernel(cpu) + systemTick(now).
  cpuKernel = interrupt poll + PipelineDriver::step for one cpu, no global side
  effects.  systemTick = stop-sentinel, interval-timer fire+divert, flash flush,
  snapshot cadence+naming, IDLEWARP -- ONCE per quantum, dispatcher-level.  Move
  the carried statics (s_idleTickWarp, s_warpLog, s_cnt) to systemTick.  This is
  the structural cliff of the phase (a mis-assigned line double-fires under a 2nd
  agent).  GATE: byte-identical.

P2-T3  STEP 3 -- decouple systemNow() + add the global retire ordinal.
  Make systemNow() its own counter advancing by the PER-STEP RETIRE CYCLE DELTA
  (INVARIANT D-1a: identical to PCC, NOT per-iteration/per-quantum -- a wrong
  advance rate fails this gate).  Add the dispatcher's cross-agent monotonic
  global retire ordinal to the trace line (the deferred half of the logging
  policy; do NOT make it any CPU's cycleCount).  Confirm all L2 consumers + L2b
  inject arm track the system clock.  GATE: byte-identical.

P2-T4  STEP 4 -- CpuState ownership into AlphaCpuAgent.
  Agent owns CpuState; Machine::cpu() returns agent0's.  SET cpuSlot from the
  agent's id() (closes the STEP-1b "single agent = 0" placeholder -> real slot).
  Re-home L1 (RTC bindCycleSource @~351) + the IDLEWARP write @~1305 to the
  system clock.  Keep trace + spdlog sinks Dispatcher-level shared services.
  Snapshot must still round-trip agent0.  GATE: byte-identical + determinism_
  equivalence + snapshot round-trip green.

P2-T5  whami-cpuid RECONCILIATION (do with/after STEP 4).
  Today there are TWO slot-ish sources: the NEW CpuState.cpuSlot (numeric, used
  by tracing) and the OLD mCpuId (typed as CpuType MODEL enum, dormant -- the
  pre-existing TODO(whami-cpuid) in CpuState.h).  Unify to ONE: route PalEntries
  WHAMI (execMfprWhami / CSERVE$WHAMI @PalEntries.cpp ~542/~851) and HWRPB whami
  (HwrpbBuilder) through the real slot; retype/retire the mis-typed mCpuId.  Do
  NOT leave two divergent slot sources long-term.

P2-T6  STEP 5 -- flip default + delete legacy loop (POST-Phase-2 acceptance only).
  Per D-2: a SEPARATE one-variable commit AFTER the dispatcher path has been the
  proven-equivalent default across Phase-2 acceptance.  Flip EMULATR_DISPATCH
  default, delete the legacy Machine::run loop; the determinism harness becomes
  the sole gate.  Until then legacy STAYS the gate's reference.

DOWNSTREAM (already in memory.md deferred list; not Phase 2):
  Phase 3 (LL/SC cross-CPU interlock + LockArbiter real backing + contention
  micro-test -- THE cliff); Phase 4 (per-CPU IPI divert + P4 constants); Phase 5
  (rendezvous responder, gated on P1/P2/P3 measurements); Phase 6 (N-CPU +
  scheduler-cursor snapshot serialize).  A real 2nd CPU stays deferred until the
  Phase-2 split + P2-T4 land.
================================================================================
