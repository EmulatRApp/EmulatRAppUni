================================================================================
EmulatR V4 -- SMP levers disambiguation + HRM MP-boot wiring map
================================================================================
Written: 2026-06-20.  Purpose: (1) pin the three "levers" so the vocabulary
stops drifting across sessions, and (2) turn Alpha AARM Section 27.8.1.1
(Multiprocessor Considerations) into an explicit Phase-3/4/5 checklist with the
two new items the HRM surfaces (per-CPU console comm area RXRDY/TXRDY, and the
PE-bit / BB_WATCH coupling).  NO CODE -- this records understanding only.
Authoritative design context: 20260618_smp_secondary_cpu_bringup_design.md,
20260619_alphacpuagent_phase2_ownership_lift_design.md, the Phase-2 task ledger
(20260619_phase2_task_ledger.md), and memory.md deferred list.

--------------------------------------------------------------------------------
1.  THE THREE LEVERS (none of them, alone, boots a second CPU)
--------------------------------------------------------------------------------

LEVER A -- EMULATR_DISPATCH  (execution-path select)
  Unset = legacy Machine::run per-cycle loop.  This is the DEFAULT and remains
  the gate's oracle until Phase 2 STEP 5 (P2-T6).
  Set   = dispatcher driving one AlphaCpuAgent.
  It selects HOW the single CPU is stepped.  It does NOT create a second CPU.

LEVER B -- IExecutionDriver swap  (SequentialDriver <-> ThreadedDriver)
  This is the "deterministic-async interface."  SequentialDriver is the
  deterministic oracle; ThreadedDriver is std::barrier-synced.  The harness test
  determinism_equivalence proves Sequential == Threaded, bit-identical.  This is
  the MECHANISM by which an async/threaded CPU still steps deterministically.
  Today only the harness mock / single real agent sits behind it.

LEVER C -- EMULATR_PLATFORM  (firmware platform identity; MISNAMED BY LINEAGE)
  Unset or =isp : intercept the read of PA 0xBFFC, return 0xCAFEBEEF, so the
                  firmware resolves ISP_MODEL (pre-silicon simulator) and
                  reaches >>>.  (Read-intercept, NOT a deposit -- offset 0x3FFC
                  is overwritten by the self-decompressor.)
  =silicon      : no intercept -> firmware resolves REAL_HW.
  CRITICAL CLARIFICATION: this lever replaced the old EMULATR_CPU1_ALIVE hack,
  so it LOOKS SMP-related.  IT IS NOT.  The 0xBFFC poll was once mistaken for a
  CPU1 rendezvous; 06-18 root-cause proved it is the apisrm pc264.c platform()
  ISP-model detection flag.  EMULATR_PLATFORM does not bind, start, or advance a
  secondary processor.  Reading "PLATFORM lever landed" as "CPU1 is reachable"
  is the misconception to kill.

NET: all three levers together still yield exactly ONE CPU, steppable
deterministically, on a firmware that believes it is on a simulator.

--------------------------------------------------------------------------------
2.  WHY WE ARE NOT READY TO BOOT CPU1 (structural, not just risky)
--------------------------------------------------------------------------------

- CpuState is UNBOUND from the agent.  The agent REFERENCES CpuState through the
  bound Machine; it does not OWN it.  There is exactly one register file, so
  there is no second CpuState for a CPU1 to inhabit.  Per-agent ownership is
  P2-T4; we are at P2-T1.
- stepCycle still fuses per-CPU work with system-level side effects.  Until
  P2-T2 splits it into cpuKernel(cpu) + systemTick(now), a second agent would
  re-fire the interval timer, flash flush, and snapshot cadence (double-fire).
- LL/SC cross-CPU interlock (Phase 3) does not exist.  The HRM primary-selection
  mutex IS an LDQ_L/STQ_C contention case; without it, primary selection cannot
  even be modeled correctly.
- HWRPB secondary-consumption path (Phase 5) is unbuilt: no per-CPU console comm
  area / RXRDY polling, no PE bit, no BB_WATCH ownership model.

--------------------------------------------------------------------------------
3.  AARM Section 27.8.1.1 -> V4 PHASE CHECKLIST
--------------------------------------------------------------------------------
HRM verbatim anchors (Alpha Architecture Reference Manual, System Bootstrapping,
27.8.1.1 Multiprocessor Considerations; embedded-console case 27.8.1):

  "selection of the primary processor occurs before any access to main memory
   by any of the processors.  At system cold start, each of the processors will
   be executing in console I/O mode.  The necessary memory for console execution
   must be independent of main memory..."
  "The selection of the console primary requires one or more hardware registers
   ... One possible example is a mutex contained in a single-bit register
   accessed only with LDQ_L/STQ_C instructions."
  "console secondaries must not access main memory [until notified].  The
   console primary is responsible for building the HWRPB ... must be able to
   signal one or more of the secondaries by additional hardware register(s)."
  "On warm bootstrap or restart, a secondary processor must locate its per-CPU
   slot in the HWRPB and poll its RXRDY bit."
  "the console primary identifies itself by comparing its WHAMI register
   contents with the Primary CPU ID value stored in the HWRPB."
  "The Primary-Eligible (PE) bit in the per-CPU slot ... indicates ... whether
   the CPU has access to a BB_WATCH."

CHECKLIST (status as of 2026-06-20):

  [Phase 3] Primary-selection mutex via LDQ_L/STQ_C, resolved BEFORE any
            main-memory access.  STATUS: NOT BUILT.  This is the LL/SC
            cross-CPU interlock cliff; it is exactly the non-negotiable
            contention micro-test in the design.  LockArbiter needs real
            backing; per-CPU lock_flag/lock_physical_address; any store by any
            agent (STx / STx_C / DMA) clears other CPUs' lock_flag on that
            granule.  Never yield MID-INSTRUCTION, but other agents MUST be able
            to interleave at the LL/SC boundary to detect contention.

  [Phase 5] Console-memory independence invariant: secondaries must not touch
            main memory until the primary signals a valid HWRPB.  STATUS: NOT
            MODELED.  Needs an enforced "secondary pre-rendezvous main-memory
            access = error/parked" guard.

  [Phase 5 + HWRPB ext] Per-CPU console comm area with RXRDY/TXRDY that the
            secondary polls.  STATUS: ABSENT -- NEW WIRING.  HwrpbBuilder.cpp
            currently writes the per-CPU SLOT array (state flags
            present/available/bootstrap/restart_cap/pal_valid/pal_loaded,
            pal_rev/var, cpu_type/var/rev, logout) sized by cpu_count, but NO
            RX/TX console comm buffers or RXRDY/TXRDY ready bits.  Add the
            per-CPU comm area + ready-bit semantics so a parked secondary has
            something faithful to spin on.

  [Phase 4] Primary -> secondary signal via additional hardware register(s).
            STATUS: PARTIAL.  Cchip IPREQ->IPINTR->b_irq<3> delivery landed
            2026-06-18, BUT TsunamiTig ipcr is STORAGE-ONLY (no delivery) ->
            documented SMP-secondary stall.  Reconcile: which register the
            console secondary actually waits on, and make that path deliver.
            Re-confirm IPI EI-bit / IPL constants against the VMS console PAL
            (the 06-18 IPI work used the OSF PC264 table; the console image runs
            the VMS personality).

  [P2-T5]   WHAMI compared to Primary CPU ID in HWRPB.  STATUS: TWO DIVERGENT
            SLOT SOURCES -- the new CpuState.cpuSlot (numeric, used by tracing)
            vs the mis-typed mCpuId (CpuType MODEL enum, dormant TODO).  Unify
            to ONE real slot and route WHAMI (execMfprWhami / CSERVE$WHAMI) and
            HWRPB whami through it; retype/retire mCpuId.

  [NEW, fold into Phase 5] PE (Primary-Eligible) bit per per-CPU slot, coupled
            to BB_WATCH access.  STATUS: NEITHER MODELED.  The primary must own
            the BB_WATCH; the PE bit advertises eligibility.  Add to the
            per-CPU slot modeling alongside the comm area.

--------------------------------------------------------------------------------
4.  DECISION / SEQUENCE (unchanged by this analysis)
--------------------------------------------------------------------------------
- Immediate next step is STILL P2-T1 (DecListingSink cpuId emit + deliberate
  one-time golden/AXPBox re-baseline).  It is a prerequisite that does not
  presuppose a second CPU; last session's handoff already opened there.
- CPU1 stays PARKED behind: Phase 2 (CpuState ownership, P2-T2 split then P2-T4
  ownership) -> Phase 3 (LL/SC interlock) -> Phase 4 (deliver the secondary
  signal, not just store it) -> Phase 5 (HWRPB secondary consumption: comm area
  + RXRDY + PE/BB_WATCH + memory-independence invariant).
- The two genuinely-new items this HRM pass adds to the backlog: the per-CPU
  console comm area (RX/TX + RXRDY/TXRDY) and the PE-bit / BB_WATCH coupling --
  both folded into Phase 5 HWRPB-slot modeling, NOT new phases.

--------------------------------------------------------------------------------
5.  TRACE FORMAT vs BIT-CORRECTNESS  (resolves the P2-T1 DEC-listing question)
--------------------------------------------------------------------------------
Bit-correctness lives in the VALUES (cycle, pc, the encoded instruction word,
register writes, memory effects), NOT in how a line is rendered.  The textual
format (DEC-ASM listing vs key=value machine line) is a presentation choice and
is orthogonal to correctness.  Verified against the actual tooling:

- The Phase-1 gate (tools/phase1_dispatch_gate.sh) is a TEXTUAL `diff -u` of two
  normalized host logs (legacy vs EMULATR_DISPATCH=1), not a field-extracting
  comparator.  Consequence: format is FREE for correctness as long as it is
  CONSISTENT across the two compared sides.  The cpuId tag cancels because BOTH
  legacy and dispatch emit it -- so the dispatch gate needs no re-baseline.
- We do NOT diff against raw DEC SRM listings from the manuals.  The "DEC
  listing channel" is merely a human-readable rendering; the machine channel and
  the RET channel carry the same values in key=value form.  The earlier
  "preserve raw-DEC-diffability" reservation was therefore WRONG and is
  WITHDRAWN: standardizing on the compact DEC-ASM style costs nothing in
  correctness and is cleaner.

TOOLING COUPLING + RENAME (must ship inside the P2-T1 change):
  AGREED RENAME (Tim, 2026-06-20): rename the trace cycle label `cyc=` -> `rpcc=`
  -- it IS the per-CPU PCC and `rpcc` says so explicitly (this also pre-states
  the D-1a invariant that this column is PCC, NOT the system clock).  Rename the
  INS/REG/FRG/HEARTBEAT `cycle=` labels and the DEC-listing cycle column to match
  for one consistent name; re-mint the golden once.  The P2-T3 global retire
  ordinal stays a SEPARATE field -- do NOT reuse `rpcc=` for it.
  New RET head: `RET [cpu=<n> ]rpcc=<n> pc=<hex16> <mnem> pal=<0|1> exc=<hex16> ...`
  tools/analyze_retire_trace.py parses RET lines with a POSITIONAL regex
  `RET\s+cyc=(\d+)\s+pc=([0-9a-fA-F]+)\s+(\S+)`; BOTH the new optional `cpu=`
  field AND the `cyc=`->`rpcc=` rename BREAK it.  Update to:
  `RET\s+(?:cpu=\d+\s+)?rpcc=(\d+)\s+pc=([0-9a-fA-F]+)\s+(\S+)`.  Audit any
  other positional consumer of RET / INS / DEC-listing lines for the same break.

DECISIONS / DIRECTION:
- Free to make the compact DEC-ASM listing the canonical human/diff channel;
  cpuId tag goes wherever, it is additive metadata.
- The verbose key=value machine channel and the DEC listing duplicate each other
  for no correctness reason (ergonomics: script-parse vs human-read).  Optional
  consolidation to ONE compact, parseable, tagged channel is a separate
  discuss-before-code design item.
- PRINCIPLED END-STATE (makes format provably irrelevant AND the AXPBox
  cross-check robust, since AXPBox emits its OWN format and that comparison
  already needs field projection): a field-extracting SEMANTIC comparator that
  parses each trace to canonical (pc, instr, reg-writes, mem-effects) tuples and
  diffs those.  The textual gate is a pragmatic shortcut valid only because both
  sides are the same emulator and format.

--------------------------------------------------------------------------------
6.  HRM SCOPE -- AARM Section 27.4 SYSTEM BOOTSTRAPPING (the spine)
--------------------------------------------------------------------------------
Section 3's 27.8.1.1 (MP Considerations) is a downstream IMPLEMENTATION detail of
the real spine, which is Section 27.4.  Intro (verbatim sense): the console
locates, loads, and transfers control to a primary bootstrap; it defines console
responsibilities + the initial state seen by system software for BOTH MP and UP;
it covers cold bootstrap (full hardware init) and warm bootstrap (partial init).
A bootstrap occurs from powerfail recovery, a processor halt, or an INITIALIZE or
BOOT console command (state transitions: Section 27.1.1).

27.4.1 COLD-BOOTSTRAP SEQUENCE (the 10 steps) mapped to EmulatR V4 state:
  1.  System initialization ............ chipset/CPU reset .............. DONE
  2.  Size memory ...................... AAR / memorySize (64->1024 fix) . DONE
  3.  Test sufficient memory ........... memory_test=none today ......... SKIPPED/STUB
  4.  Load PALcode ..................... SrmLoader + PAL relocation ..... DONE
  5.  Build valid HWRPB ................ HwrpbBuilder (SRM self-builds at
                                         cold boot; ours = OS handoff) .. BUILT
  6.  Build Memory Cluster Descriptors . MEMDSC ......................... AUDIT NEEDED
  7.  Init bootstrap page tables + map
      initial regions .................. CSERVE Region 0/1/2/3 .......... DONE
  8.  Locate + load primary bootstrap
      image ............................ dqa0 / VMB boot path ........... FRONTIER (not done)
  9.  Initialize processor state on ALL
      processors ....................... SMP secondary init lives HERE .. NOT DONE (Phase 5)
  10. Transfer control to primary
      bootstrap image .................. OS handoff ..................... NOT DONE

  Steps 1-7 are substantially what reached >>>.  Steps 8-10 are the OS-boot
  frontier.  STEP 9 is the architectural home of the secondary-CPU work in
  Section 3 -- the HRM lists "all processors" as a cold-boot step, so CPU1 init
  is part of the bootstrap CONTRACT, not a bolt-on.  The Section-3 27.8.1.1
  checklist (primary-selection mutex, RXRDY comm area, signaling, PE/BB_WATCH)
  is the detailed wiring of step 9.

COLD vs WARM + the four triggers, mapped to EmulatR work:
  - cold  = full HW init (the 10 steps).  Forced cold when BOOT_RESET="ON".
  - warm  = partial HW init.  Architectural analog of SNAPSHOT RESTORE.
  - triggers:
      powerfail recovery .... warm-restart analog -> snapshot/restore work
      processor halt ........ the halt-button / TIG smir work
      INITIALIZE (AUTO_ACTION=BOOT) and BOOT command ... env-var path
                              (AUTO_ACTION / BOOT_RESET / bootdef_dev)

--------------------------------------------------------------------------------
7.  TBD -- MAP THE PRIMARY-CPU HWRPB (prerequisite for SMP hooks)
--------------------------------------------------------------------------------
Status: OPEN investigation (Tim 2026-06-20).  Phase-5 secondary consumption (the
Section-3 / AARM 27.8.1.1 list -- per-CPU PCS slot, RXRDY poll, primary signal)
needs the secondary to LOCATE the HWRPB the primary built.  Before we can wire
those hooks we must identify WHERE the live HWRPB is and its extent/layout.

Two HWRPBs exist in play, do NOT conflate:
  - V4 HwrpbBuilder (deviceLib/HwrpbBuilder.cpp) builds an HWRPB for the OS
    HANDOFF (per-CPU SLOT array sized by cpu_count; state flags, pal/cpu rev,
    logout).  Authored, but see PalEntries swpctx note: still pending DEPLOYMENT
    of the built buffer into guest memory at boot (Machine orchestrator memcpy).
  - The SRM firmware SELF-BUILDS its own HWRPB during primary-CPU cold boot
    (AARM 27.4 step 5).  This is the one a real secondary would consume.

PROPOSED MECHANISM (Tim): profile the address space the SRM consumes while it
builds the HWRPB during primary-CPU initialization.
  - Capture: a STORE-WATCH / PA-write profiler over the primary-CPU cold-boot
    window (reuse the boot-profiler ticket / RetireProfiler / region-map tooling;
    a PA-write heatmap of the init window, not a whole-file trace).
  - IDENTIFY the HWRPB by its SELF-DESCRIBING header rather than by size/guesswork
    (consistent with the region-map "match by self-describing magic" principle):
    the HWRPB begins with its own physical address (self-referential validation),
    a revision, and a checksum at a known offset, followed by the offset table to
    CTB / CRB / MEMDSC / DSRDB / the per-CPU SLOT array.  Match that header in the
    captured write set to pin the base; walk the offset table to map the extent.
  - OUTPUT: HWRPB base PA + layout (esp. per-CPU SLOT array base/stride and the
    RXRDY/comm-area offsets) -> feeds the Phase-5 secondary-consumption hooks and
    closes the swpctx "deploy HWRPB to guest memory" gap.
SEQUENCING: groundwork for Phase 5; does NOT block the Phase-2 clock work
(P2-T3a/b -> T4).  Pin the address map first, then wire hooks against real offsets
(never provisional -- per the IPR/SCBD discipline: provisional OK for storage,
never for decode).

End of note.
