# System Bus Refactor -- Architecture Contract v1.1 + Diagnostic Addendum + Plan

Status: APPROVED FOR IMPLEMENTATION (2026-05-22)
Supersedes: all earlier I/O-stack / CPU-memory-interface direction (the
"keep GuestMemory's readN / insert-decode-above / staged minimal-NXM-wiring"
proposals are RETIRED). This document is the definitive specification.

--------------------------------------------------------------------------------
0. WHY (diagnosis basis -- the bug this refactor fixes)
--------------------------------------------------------------------------------
SRM boot spins in an MCHK loop. Decoded chain:
  - Faulting instruction: HW_LD R4,0(R4) (ALT, QW, virtual) at PAL PC 0x8590
    (instr word 0x6c845000, opcode 0x1B). At fault R4 = 0x2000000000 (128 GB).
  - 0x2000000000 is in the Tsunami DRAM window (< 8 TB) but beyond the 1 GB
    GuestMemory backs -> GuestMemory returns OutOfRange -> kFaultBusError (11)
    -> delivered as MCHK.
  - sys__cbox (apisrm ev6_osf_pc264_pal.mar:3521) is the MCHK HANDLER's
    "get cbox error chain" routine, not the cause.
  - This is the SRM memory-sizing probe: it reads ascending PAs until a bus
    error to find RAM size, expecting a RECOVERABLE machine check.
  - It loops because the bus error is raised INSIDE GuestMemory and the chipset
    is never told (sub-8 TB accesses never reach the Cchip), so the error
    registers (Cchip MISC.NXM, Cbox ERROR_REG) read all zeros and the handler
    cannot characterize the fault. errorReg == 0 verified live; reportNxm is a
    stub with zero call sites; there is no chipset->CPU machine-check signal.

Root cause: error-reporting feedback path is missing. On real hardware the Cchip
(memory controller) decodes the PA, and an unclaimed PA is the Cchip's NXM to
report. V4 inverted this: GuestMemory acts as the memory-map arbiter. Fix =
restore the Cchip-decodes topology.

--------------------------------------------------------------------------------
1. OVERVIEW AND GOALS
--------------------------------------------------------------------------------
Define the strict boundaries between the emulated EV6 CPU core, the system bus
(Tsunami Chipset), and backing physical memory. Primary mandate: complete
decoupling of CPU execution logic from physical memory routing.

--------------------------------------------------------------------------------
2. COMPONENT RESPONSIBILITIES AND DEMARCATION
--------------------------------------------------------------------------------
2.1 CPU Core (ExecCtx / Pipeline / MemDrainer)
  - IGNORANCE: zero knowledge of topology, memory maps, PCI, or RAM limits.
  - OUTPUT: emits 44-bit Physical Addresses for Load, Store, and Fetch.
  - INTERNAL ROUTING: exclusively handles PALcode scratchpad access
    (PA >= 0xFFFFFF00000). These transactions MUST NOT escape to the bus.
  - TRAP HANDLING: responds to the MCHK signal by vectoring to the PALcode
    (VMS/OSF) Machine Check handler.

2.2 System Bus Arbiter (ISystemBus / TsunamiChipset)
  - OMNISCIENCE: the absolute authority on physical address decoding.
  - ROUTING: evaluates every incoming PA against AAR0-3 (RAM) and the fixed
    I/O windows (MMIO).
  - COHERENCE: owns the LL/SC LockMonitor; observes all writes to invalidate
    active Load-Locked reservations.
  - ERROR GENERATION: sole generator of NXM (Non-Existent Memory) faults. On an
    unclaimed PA it MUST update chipset syndrome registers (Cchip::MISC) and
    assert the MCHK signal to the CPU. MCHK assertion also drives the
    diagnostic dump (section 5) to d:/traces/TsunamiBus.trc.
  - GATEKEEPER: the chipset is the sole authority over when GuestMemory is
    touched. PA inside configured DRAM bounds -> route down to GuestMemory.
    PA outside -> block the access and raise NXM MCHK.

2.3 Physical Memory (GuestMemory)
  - PASSIVITY: reduced to a "Dumb Byte Store."
  - NO ROUTING: zero MMIO hooks, no address range checks, no fault generation.
  - TRUST: assumes any PA passed to read/write has already been validated as
    legal DRAM by the bus arbiter.
  - OWNS: physical allocation / lifecycle of the sparse 64 KiB pages
    (ensurePage, m_zeroSentinel). These mechanics are correct and unchanged;
    the demotion is purely amputation of routing logic.

--------------------------------------------------------------------------------
3. TOPOLOGICAL FLOW
--------------------------------------------------------------------------------
  [ CPU CORE ]
       | (Valid PA + Width)
       v
  [ ISystemBus Interface ] ---> (LL/SC invalidation check)
       |
       +-- decode: PA in AAR bounds  ----> [ GuestMemory (RAM) ]
       +-- decode: PA in Tsunami I/O ----> [ TsunamiPchip (PCI/MMIO) ]
       +-- decode: unclaimed PA      ----> [ NXM / MCHK latch ] --> interrupts CPU
                                                    |
                                                    +--> TsunamiBus.trc dump

--------------------------------------------------------------------------------
4. C++ INTERFACE CONTRACT (coreLib/ISystemBus)
--------------------------------------------------------------------------------
The CPU interfaces with the system solely through an abstract interface, in a
LOW layer (coreLib/memoryLib), so the CPU never depends on chipsetLib
concretely (avoids a pipelineLib->chipsetLib cycle; keeps a fake bus injectable
in tests).

  struct BusResult {
      MemStatus status;  // Ok, or hardware fault state (e.g. BusError)
      uint64_t  data;    // payload for reads
  };

  class ISystemBus {
  public:
      virtual ~ISystemBus() = default;
      virtual BusResult read (uint64_t pa, uint8_t width) = 0;
      virtual BusResult write(uint64_t pa, uint64_t value, uint8_t width) = 0;
      // Instruction fetch (bypasses LL/SC checks; handles cache-line fills).
      virtual BusResult fetch(uint64_t pa, uint8_t width) = 0;
  };

TsunamiChipset implements ISystemBus. The CPU-side translation point:
MemDrainer maps BusResult.status == BusError -> kFaultBusError -> MCHK via the
existing Ev6EntryVectors path. (Confirm fetch-fault mapping: an unmapped fetch
is an I-stream machine check / IACV per the handler -- verify, see section 7.)

--------------------------------------------------------------------------------
5. ERROR REPORTING + DIAGNOSTIC MANDATES (Ticket 10 / NXM fallthrough)
--------------------------------------------------------------------------------
No component shall silently drop or fake a return value for an unmapped
read/write outside explicit PCI/ISA bridging rules.

On an unmappable PA the TsunamiChipset MUST, in this ORDER:
  1. Capture context (see record below).
  2. Write + flush the TsunamiBus.trc record (BEFORE any debug guard, so a
     JIT-debugger kill never loses the record -- mirrors BreakpointSink).
  3. Optionally fire the debug guard (__debugbreak) if diagnostic mode is on.
  4. Set Cchip::MISC.NXM = 1 and Cchip::MISC.NXM_SRC = faulting CPU ID.
  5. Return BusResult{status = BusError}; the CPU translates this into the
     hardware Machine Check exception.
  6. PALcode MCHK handler then reads Cchip::MISC, processes HW_REI, resumes.
It MUST NOT throw a C++ exception.

5.1 Diagnostic trigger contract (d:/traces/TsunamiBus.trc)
Per-fault structured record. Reuse the existing sink pattern (DecListingSink /
BreakpointSink): lazy file-open with root-probe + create_directories +
EMULATR_RETIRE_TRACE_DIR resolution; self-describing ASCII-128 header; sync
flush per record. Gate the whole sink behind a CLI / per-subsystem flag so
production runs do not pay for it.

Record fields:
  - Timestamp: relative CPU cycle count.
  - Access type: READ / WRITE / FETCH.
  - Faulting PA (44-bit).
  - Syndrome: Cchip::MISC before AND after the NXM latch.
  - Backtrace: current execution PC and excAddr (return address).

THROTTLE HARD. The sizing probe produces a burst of NXM faults; a full dump per
fault (and especially a per-fault multi-cycle buffer) re-creates the multi-GB
trace footgun. Match existing policy: first N loud, then per-distinct-PA or
every-Nth. The per-fault structured record is the high-value artifact; DEFER
the "last 1024 cycles of bus activity" ring as heavier scope unless a small
bounded ring is cheap.

5.2 Fault-concern surface (eventual; implement NXM-DRAM row first, keep the
record struct general enough to extend)
  - DRAM NXM         : PA outside AAR0-3.        Log PA, CPU ID, PC; MISC syndrome.
  - I/O master abort : unmapped PCI/ISA (>=8TB). Log Bus/Dev/Func or offset; PERROR.
  - LL/SC coherence  : STQ_C fail, monitor clear. Log CPU ID, PA; track ClearLine.
  - PAL scratchpad   : bad 0xFFFFFF... access.   Log CPU ID, width (microcode bug).
  - Alignment        : unaligned access.         Log faulting PC, unaligned PA.
  - Access violation : kernel/user protection.   Log protection key, context.

5.3 Reading the trace (the live diagnostic)
  - Working : an NXM record immediately followed by a MISC read and HW_REI.
  - Looping : the same NXM PA repeated tight-cycle -> handler read an unexpected
    syndrome, failed to clear, or misinterpreted excAddr on resume.

--------------------------------------------------------------------------------
6. ACCEPTANCE CRITERIA
--------------------------------------------------------------------------------
  - grep -R "MMIO" GuestMemory.h -> 0 results.
  - PipelineDriver depends only on ISystemBus, not TsunamiChipset.
  - SRM sizes memory by reading PA 0x2000000000, catching the MCHK, reading
    Cchip::MISC, and continuing execution (boot advances past the loop).
  - TsunamiBus.trc shows NXM -> MISC read -> HW_REI (the "working" pattern).

--------------------------------------------------------------------------------
7. GATING PREREQUISITE (resolve BEFORE writing the NXM path)
--------------------------------------------------------------------------------
Acceptance section 6 passes only if we set exactly what the handler reads and
resume the way it expects. UNVERIFIED, and it dictates the syndrome bits and
the MCHK resume semantics:
  (a) Does the OSF MCHK handler read only MISC.NXM, or also NXM_SRC and/or the
      Cbox ERROR_REG chain (via sys__cbox)? If the latter, a logger/repair that
      only touches MISC is incomplete.
  (b) Does MCHK delivery resume at the NEXT instruction, or retry the faulting
      HW_LD? Retry alone loops forever even with MISC.NXM set -- this is the
      EXC_ADDR / HW_REI-past-the-load question.
Source of truth: apisrm memconfig_pc264.c + the OSF MCHK handler in
ev6_osf_pc264_pal.mar; cross-checked against AXPBox (boots the same ROM, so its
bus/NXM/resume path is the reference).

--------------------------------------------------------------------------------
8. IMPLEMENTATION CHECKLIST
--------------------------------------------------------------------------------
  0. Commit current state (trace instrumentation) as baseline.
  1. Verify the MCHK contract (section 7) -- read-only, gates the design.
  2. Define coreLib/ISystemBus + BusResult.
  3. Retype the spine: ExecCtx.memory -> ISystemBus*; MemDrainer + PipelineDriver
     params -> ISystemBus&. Convert the ~27 readN/writeN sites to
     bus.read/write/fetch(pa,width) (the per-width switch collapses into width).
  4. TsunamiChipset implements read/write/fetch: AAR/DRAM -> GuestMemory delegate;
     I/O window -> Pchip; fallthrough -> reportNxm (set MISC.NXM + NXM_SRC) +
     TsunamiBus.trc dump + BusResult{BusError}.
  5. GuestMemory amputation: remove m_mmioReadHook/m_mmioWriteHook/attachMmioHooks;
     remove the pa < m_size bounds checks; move kPalScratchBase/m_palScratch out;
     no OutOfRange/fault returns. Keep sparse paging + m_zeroSentinel.
  6. Rehome PAL scratch into the CPU exec context (decide: ExecCtx vs CpuState vs
     a small CpuScratch). CPU services PA >= 0xFFFFFF00000 before the bus call.
  7. Rehome LockMonitor to the bus; reconcile MemDrainer LL/SC (per-CPU
     reservation flag stays in CpuState; cross-line invalidate-on-write -> bus).
  8. Wire fetch through the bus; reconcile the existing IFetchOverride (SRM-stub
     fetch) seam.
  9. TsunamiBusTraceSink per section 5 (NXM-DRAM row first; gated; throttled).
 10. CHECK-only tests: NXM at 0x2000000000 sets MISC.NXM + raises MCHK; DRAM hit
     delegates; LL/SC invalidation via bus; grep MMIO GuestMemory.h -> 0.

--------------------------------------------------------------------------------
9. OPEN CLARIFICATIONS
--------------------------------------------------------------------------------
  - AAR at probe time: are AAR0-3 firmware-programmed before the sizing loop, or
    do we decode against a boot-default DRAM bound (the configured GuestMemory
    size) until they are written? For acceptance, decoding 0x2000000000 as NXM
    works as long as it is outside the configured bound; confirm the pre-AAR
    source-of-truth.
  - fetch() fault class: an unmapped fetch should map to the I-stream machine
    check / IACV the handler expects, not necessarily the data MCHK path.

--------------------------------------------------------------------------------
END
--------------------------------------------------------------------------------
