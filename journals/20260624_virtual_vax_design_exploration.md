<!--
EmulatR V4 -- Virtual VAX Design Exploration (journal)
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Purpose: capture an exploratory design discussion about a hypothetical
virtual VAX platform built in the EmulatR style, plus production-host
performance levers, for a later discussion. NOT a committed track and
NOT a Cowork hand-off. ASCII(128) only.
-->

# Virtual VAX design exploration -- captured 2026-06-24

Exploratory discussion, not a committed second target.  No code, no
Cowork action.  This journal exists so the thread can be picked up cold
later without re-deriving the architecture mapping.  Three topics were
covered, in order: (1) the VAX platform / model landscape, (2) what a
virtual VAX built like EmulatR would share with and differ from the
current tree, (3) the performance levers if such a VAX were a production
migration target on a tuned Linux host.

## The fork that frames everything

The single decision that gates every other answer: is a virtual VAX a
fidelity-first instrument (like EmulatR -- cycle-accurate, verify-before-
decode, per-step cost paid deliberately) or a speed-first migration
target (functional accuracy, JIT, run-as-fast-as-the-host-allows)?  The
two builds want nearly opposite answers.  EmulatR pays the software-TLB
walk, the BoxResult / MEM-drain commit, and no-decode-caching ON PURPOSE
in exchange for fidelity.  A production virtual VAX attacks exactly those
structures because, for migration, cycle-accuracy is pure tax.

DECISION (deferred): pick the end of that spectrum before any virtual-VAX
work starts.  Most of this journal's content branches on it.

## Topic 1 -- platform / model landscape

"VAX virtualization" has two unrelated senses.  Architectural: VAX =
Virtual Address eXtension, 32-bit demand-paged virtual memory (P0 / P1 /
S0 / S1).  No hardware hypervisor ever existed on VAX; soft-partitioning
(Galaxy) was an Alpha / OpenVMS feature, not VAX.  Modern usage: full-
system emulation on x86 -- Stromasys Charon-VAX, AVTware / EmuVM vtVAX
(commercial), SIMH (open source), plus NuVAX and others.  These boot
unmodified OpenVMS / Ultrix and can join real or emulated clusters.

Model spread that was named (4100 / 7640 / 7650 / 7660) is TWO machine
classes, not points on one line:

```
VAX 4000-100   KA52 module, single NVAX (~72 MHz class), uniprocessor.
               Q22-bus + DSSI storage, SCSI, SGEC Ethernet, SSC, QUART.
               BA42-style deskside.  Macrocoded console in flash ROM.
               -> the "DS10 of VAX": tractable first target.

VAX 7000-6x0   KA7AA module, NVAX+ at 90.91 MHz, 4 MB B-cache, two LEVI
               gate arrays.  LSB (Laser System Bus), 128-bit SMP back-
               plane.  Trailing digit = CPU count:
                 7640 = 4 CPUs, 7650 = 5, 7660 = 6.
               IOP node bridges to XMI / VAXBI / Futurebus+.  Up to
               3.5 GB usable (32-bit VAX cap).  Gbus macrocoded console.
               -> the SMP escalation (DS20-and-beyond analog).
```

Notes carried for later: the trailing-digit = CPU-count convention is
identical on the VAX 6000-6x0 (NVAX, XMI bus) -- 6640 / 6650 / 6660 are a
real and easily-confused family; the "7" prefix disambiguates to LSB.
The VAX 7000 is the shared-chassis ancestor of the DEC 7000 / 10000 AXP
(same LSB, same LEVI, CPU-swap upgrade to 21064) -- interesting only as
the "what if EmulatR went SMP-Alpha" branch; it shares nothing with the
DS10 / DS20 Tsunami / Typhoon PCI platform.

Clusters: a VMScluster (renamed VAXcluster once Alpha joined) presents N
nodes as one storage / security / management domain with a cluster-wide
distributed lock manager (DLM) and a votes / quorum connection manager.
A single-node cluster is a legitimate boot mode (VAXCLUSTER +
EXPECTED_VOTES), not a contradiction.  Multinode is defined by the
interconnect: 4000-100 realistically does DSSI or NI (Ethernet); 7000 can
anchor CI (star coupler + HSC), DSSI, or NI / FDDI.  The DLM / quorum
logic is identical regardless of box; only the wire changes.

## Topic 2 -- virtual VAX vs EmulatR (architecture port)

Grounded against the live V4 structures: six-stage driver (IF reads 4
bytes, DE dispatches on the 32-bit encoded word, EX returns BoxResult,
MEM drains, WB advances PC and delivers faults to palBase + entryFor-
Fault); the "no leaf mutates CpuState directly -- writes flow through
BoxResult applied at MEM" contract; fpReg as raw 64-bit patterns punned
to IEEE single / T-format; MemDrainer owning LL/SC.

TRANSFERS WHOLESALE (the chassis is platform-agnostic):
  - Machine / CpuState / GuestMemory / device separation.
  - Deterministic single-threaded step() loop, staged retire skeleton.
  - Snapshot / restore with kCpuStateVersion bumps.
  - Trace instruments: width-trace, store-watch, load-watch, PC-gated
    snapshot mint.
  - doctest harness, ADR-0001, compile-guard-vs-runtime-gate logging,
    Qt-at-named-seams.
  - Methodology: verify-before-decode, _PROVISIONAL, empirical-trace-
    before-fix, hard-stop, boot-to-prompt-by-blocker campaign.

GROUND-UP REWRITE -- the front end (the headline difference):
  EmulatR IF reads exactly 4 bytes; DE is a table lookup on that word.
  VAX is variable-length CISC: 1-byte opcode (FD / FF escape for 2-byte),
  then 0..6 operand specifiers, each a mode+register byte that may pull
  1/2/4/8-byte displacement or immediate, indexed mode adding a second
  specifier byte.  Instructions run 1..~50 bytes.  IF cannot read a fixed
  width; DE becomes a sequential operand-specifier walker that fetches as
  it goes and can fault mid-instruction.  grain.encoded -> one execFn does
  not survive; need a decoded-instruction representation with a variable
  operand vector.

FIVE CONTRACTS THAT FLIP:
  1. Atomic side-effects break two ways.
     (a) Autoincrement / autodecrement / immediate / absolute modes mutate
         GPRs DURING operand evaluation, before EX -- the decoder becomes a
         register writer, violating the BoxResult-at-MEM contract.
     (b) FPD (first-part-done): string, packed-decimal, and EDITPC
         instructions are interruptible and restartable mid-execution,
         with partial state in registers and FPD set in PSL.  An
         instruction can be half-applied, trapped, resumed.  This is the
         deepest assumption violation in the whole port: "instruction =
         one atomic BoxResult at MEM" must become "instruction may emit
         incremental, restartable architectural state."
  2. PC is a general register (R15).  EV6's clean cpu.pc vs grain.pc
     split collapses: PC participates in addressing modes (PC-relative is
     autoincrement on R15; immediate / absolute autoincrement PC) and can
     be a destination.  Decoder and PC-advance entangle.
  3. Condition codes + mode live in PSL.  VAX sets N / Z / V / C on nearly
     every op -> a CC side-channel threaded through EX for almost the whole
     ISA, plus IPL, current / previous mode, IS, FPD, T, arith-trap
     enables.  Mode is PSL<current_mode> (K/E/S/U) -- so pc<0> PALmode
     canonicalization has NO analog; there is no PALmode and no PALcode.
  4. MMU + exception dispatch are hardware-architected, not PAL-soft.
     VAX defines the page-table walk in hardware (two-level, 512-byte
     pages, region split, architectural PTE format) -> translator becomes
     the prescribed MM algorithm, not a soft-TLB shim.  Exceptions /
     interrupts dispatch through the SCB (hardware-walked vector table,
     32 IPLs) in the core, not a loadable image.  Upside: no PAL tree to
     reverse-engineer.  Cost: implement the architecturally-microcoded
     complex instructions directly -- string ops, INSQUE / REMQUE and
     interlocked queue variants, CALLS / CALLG (full procedure-call
     standard), POLY, CRC, EDITPC (its own mini-interpreter).  SIMH is the
     behavioral witness the way AXPBox is now.
  5. FP changes the oracle.  fpReg punning targets IEEE single / T-format
     (Berkeley SoftFloat world).  VAX float is F / G / D / H -- DEC formats,
     not IEEE, word-swapped layout, different bias / normalization, H is
     128-bit.  Reference SoftFloat does not cover them.  Alpha retained
     VAX-compat F / G, so fpBoxLib could share that path, but D and H are
     VAX-only; packed decimal is a separate behavioral surface entirely.

Cycle-accuracy caveat: EV6 gives a documented OoO pipeline (replay traps,
stWait table) to model against.  NVAX is an in-order macropipeline --
structurally simpler -- but VAX timing is data-dependent (string /
decimal microcode loops scale with operand length), so cycle-accuracy is
fuzzier and harder to source than the EV6 HRM tables.  Expect a VAX build
to start functionally-accurate and chase cycles only where a workload
demands it (roughly the current v1 pipeline posture).

Target ordering: build core once on 4000-100 (uniprocessor NVAX, shared
CPU generation with the 7000's NVAX+).  Uniprocessor collapses the
reservation problem -- BBSSI / BBCCI, ADAWI, interlocked queue ops are
locally atomic, no cross-CPU clearLine, so LockMonitor mostly idles.  The
7000 is the SMP escalation where interlocks + NVAX cache-ownership
coherence go live; LockMonitor's shape (single reservation home, line
invalidation) is right, but the primitive is VAX interlock + ownership-
bit, not LDx_L / STx_C.

## Topic 3 -- performance levers (speed-first / production migration)

Framing: silicon -> modern host does NOT create a perf problem; the host
is far faster, so the win is essentially free.  The work is (a) not
squandering host headroom and (b) not adding latency the silicon lacked.
Levers sort into those two buckets.

CPU (mostly ruled out for throughput -- confirmed):
  - Dominant residual cost is the SOFTWARE MMU, not decode.  512-byte
    pages + CISC multi-memory-ref-per-instruction means many translations
    per instruction.  Software-TLB design (size, associativity, caching
    VA straight to a host pointer vs re-walking) is the bigger CPU lever.
  - Decode is the second lever; answer to "heavy-handed each step" is
    never decode the same PC twice.  Charon = dynamic binary translation;
    vtVAX = optimized interpretation + decoded-instruction caching.  VAX
    code is overwhelmingly static, so the specifier-walk amortizes to ~0.
  - FP-format conversion (F/G/D/H, H painful) matters only for FP-heavy
    work.  SMP interlock cost (map to host atomics) matters only for
    multi-VCPU guests with contended locks; uniprocessor -> near-free.

THE IDLE LOOP (most-forgotten lever):
  - A naive emulator burns 100% of a host core running VMS's null
    process.  Idle-pattern detection that yields the host thread until the
    next timer / IO interrupt is the single biggest consolidation-density
    and power win.  Invisible to throughput benchmarks; shows up only when
    packing many VAXen per host.  Both production emulators do it; a
    from-scratch build must design for it explicitly.

I/O (critical -- where headroom most easily leaks):
  - Storage backend must be ASYNC.  Synchronous blocking per MSCP / SCSI
    command serializes everything; io_uring lets the emulated controller
    present a deep queue with many requests outstanding.  (This is where a
    tuned Linux host earns its keep.)
  - Container hygiene: local NVMe over network storage, host-block-
    aligned, pre-allocated not sparse, deliberate write-back vs write-
    through (VMS / RMS journaling expects ordering -- do not trade
    integrity for speed silently).
  - Present the guest a DEEPER MSCP credit window than the silicon
    offered -> VMS pipelines more I/O than iron allowed.
  - Interrupt-delivery latency gates latency-bound IOPS: completion ->
    inject interrupt -> VMS ISR depends on how fast the CPU thread notices
    a pending interrupt.  Coarse polling adds latency the disk lacks;
    cross-thread signaling (I/O thread -> CPU thread) tightens it.

NI / cluster (matters -- but metric is LATENCY, not width, for clusters):
  - Emulated DEQNA / DELQA / SGEC / DEMNA were 10 Mbit; over modern host
    networking the wire stops being the limit and the emulator packet path
    becomes it.  NIC levers: host bridge type (TAP/bridge vs SR-IOV
    passthrough vs vhost / DPDK), zero-copy, batching, interrupt
    moderation on the virtual ring.
  - For a VMScluster, bandwidth is almost never the constraint -- the DLM
    is.  DLM rides SCS messages whose latency / rate dominate any lock-
    contended app (RMS file sharing, Rdb, hot resources).
  - HIGHEST-LEVERAGE cluster move: short-circuit SCS when nodes are co-
    located.  Two emulated nodes on one host can exchange SCS over host
    shared memory / loopback at microsecond latency instead of a physical
    wire round-trip -- a cluster-lock win with NO hardware analog.  Across
    hosts, low-latency interconnect (RDMA / RoCE) for SCS beats raw
    Ethernet bandwidth.  Emulated shared disk (shared container / emulated
    DSSI) handles storage; coordination still goes over SCS, so that is
    where the optimization budget goes.

CLUSTER REGRESSION RISK (the one way emulation can be SLOWER than iron):
  - Connection-manager heartbeats and quorum timers are tight; emulation
    is acutely sensitive to host scheduling jitter.  A preempted emulator
    thread = cold translation cache + cold TLB + possibly-missed heartbeat
    -> spurious cluster state transitions or hangs.
  - Mitigations are stability features, not micro-opts: core isolation
    (isolcpus, nohz_full, IRQ affinity steering NIC / NVMe interrupts off
    pinned guest cores), one physical core per VCPU thread, NUMA-local
    guest RAM + I/O threads, hugepages backing guest memory, performance
    governor.  A PREEMPT_RT / determinism-tuned host matters here far more
    for interrupt latency + heartbeat sanity than for raw throughput.
  - This is the real value of the "minimal kernel" instinct: scheduling
    determinism + a clean I/O path, not microkernel elegance per se.

## Open questions to revisit

OPEN-1  Fidelity-first vs speed-first (see top).  Gates everything.
OPEN-2  If a virtual VAX ever shares the V4 tree: how much of the core
        abstraction to keep VAX-agnostic NOW so a later fork is cheaper.
        Candidate seams: GuestMemory translation interface, the device
        register-decode pattern, the trace / snapshot infrastructure.
        Risk of over-abstracting against a target that may never land.
OPEN-3  Is this a research / verification instrument (EmulatR-like) or a
        deployment target?  Different answers to OPEN-1 and OPEN-2.
OPEN-4  If pursued: 4000-100 first (uniprocessor, shared NVAX core,
        LockMonitor idle) confirmed as the tractable entry point.

## Reference anchors

- VAX 7000 / 10000: KA7AA / NVAX+ 90.91 MHz, LSB 128-bit, x0 = CPU count,
  shared chassis with DEC 7000 / 10000 AXP.
- VAX 4000-100: KA52 / single NVAX, Q22-bus + DSSI, macrocoded console.
- VMScluster: DLM over SCS, votes / quorum, single-node mode legitimate.
- Emulator prior art: Charon-VAX (DBT), vtVAX (interp + decode cache),
  SIMH (instruction-level), VAX MP (SMP MicroVAX 3900 that never existed
  -- the expedience pole EmulatR is built to refuse).
- EmulatR contracts touched: six-stage driver, BoxResult-at-MEM, fpReg
  IEEE punning, MemDrainer LL/SC, palBase trap delivery, pc<0> PALmode.
