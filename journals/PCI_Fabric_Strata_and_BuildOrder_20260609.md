# PCI Fabric Strata, V4 Cross-Reference, and Locked Build Order
# EmulatR V4 -- 2026-06-09
# Author: Claude (Anthropic), with T. Peer (project architect)
# Status: DESIGN. No code rides with this doc (house rule: discuss before code).
# Companion to: Device_Enumeration_Scaffold_Spec_20260607.md (manifest + loader,
#   landed 2026-06-09), GCT_FRU_Support_Spec_20260607.md.
#
# Locked decisions (architect, 2026-06-09):
#   S-SCOPE   SRM-boot-faithful, NOT OS-native-driver-faithful. Build a clean
#             virtual HBA that SRM can enumerate / BAR-assign / boot through. The
#             register-faithful HBA (a real adapter's IOCB ring, for a booted OS
#             native driver) is a known FUTURE Stratum-3 variant behind the submit
#             seam -- NOT attempted until (a) a kernel boots, (b) the target OS is
#             chosen, (c) a real driver register trace is in hand. Consistent with
#             the CRB/console-callback boot path + HWRPB analysis (boot only needs
#             SRM to enumerate + assign + boot; OS-native is a separate surface).
#   S-SEAM    Stratum-3 command interface is submit(command-block) -> completion,
#             NOT "guest wrote magic register, read data back synchronously."
#             Completion is inline-invoked in fork-1 (== today's sync IDE path);
#             the threaded fork-2 (QThreadPool) swaps in behind the SAME seam.
#   S-DETERM  The completion event mutates guest-visible state (status reg, and
#             later IRQ) on the EMULATION timeline (scheduled N emulated cycles
#             out), regardless of which thread did the I/O labor. Banked IN the
#             seam definition so both forks are forced through the same rule.

================================================================================
## 0. Summary

A faithful PCI fabric that supports multiple HBAs is three separable problems
that are easy to fuse: ENUMERATION (how the guest discovers HBAs), ROUTING (how
an access reaches the right HBA), and MESSAGING/COMPLETION (how work and
interrupts flow back). The 30-disk-fabric goal (5 HBAs x 6 disks) stresses a
different layer than the single-CD case did.

Cross-reference headline (2026-06-09): V4 is AHEAD exactly where expensive
fidelity normally lives -- Stratum 1's real Alpha sparse/dense translation +
config decode already exist (most emulators fake this). The gap is CONCENTRATED
and prerequisite-ordered at Stratum 2's BAR->range binding, which the legacy-IDE
shortcut (fixed ports 0x1F0) let the single-CD path skip. That is the good kind
of gap: structural, isolated, not smeared across layers.

================================================================================
## 1. The strata (top to bottom)

STRATUM 0 -- the access primitive.
  Is: the shape of one in-flight bus transaction: (space, address, size, data,
  isWrite), space in {Config, IO, Mem}. A value type, not an interface. Carries
  NO identity (no device name, no "dk", no driver). If anything richer rides this
  envelope, a higher concern has leaked downward -- that is the invariant that
  keeps the whole stack honest.
  Up-contract: emitted by the CPU load/store path; on read returns data (plus a
  completion signal once async exists).
  Down-contract: none; it is the currency that travels between all lower strata
  (possibly narrowed -- e.g. once decoded to config it carries (bus,dev,func,
  offset) instead of a raw address, but it is still just an envelope).

STRATUM 1 -- physical decode (the Cchip/Pchip split).
  Is: the address classifier + translator. Decides space + window; for PCI windows
  performs the Alpha sparse/dense + config swizzle so a CPU-side address becomes a
  true PCI coordinate. An Alpha CPU address in a Pchip window is NOT the PCI
  address -- it has been through sparse/dense translation and, for config, the
  swizzle.
  Owns: the Tsunami window map (which CPU ranges are RAM=Cchip, which are each
  Pchip config/IO/mem windows) + the translation arithmetic.
  Down-contract: hands a translated envelope to exactly one of: memory (RAM), or a
  specific Pchip ("Config cycle for (b,d,f,offset)" or "IO/Mem at PCI-address X").
  Sole boundary between "CPU view of memory" and "PCI view of itself"; everything
  below thinks in PCI coordinates. This is the stratum the 30-disk case forces to
  be real (genuine config-cycle generation) where single-CD leaned on legacy IDE.

STRATUM 2 -- the PCI bus object (enumeration + routing registry).
  Two maps plus a config mechanism:
   - Enumeration map (bus,dev,func) -> PciFunction*. Answers config walks; absent
     slots return all-ones; header per HBA must be real enough for SRM's probe
     (correct class code, sane header type, PROGRAMMABLE BARs).
   - Range map: IO/Mem interval -> PciFunction*. Routes NORMAL (non-config)
     accesses. Populated DYNAMICALLY by the guest's BAR writes, NOT by static
     wiring. The manifest says "an HBA exists at dev 5"; the GUEST decides where
     its registers live by programming the BAR, and the range map updates when it
     does.
  BAR sizing/assignment handshake: guest writes all-ones to a BAR, reads back the
  size mask, then assigns a base and writes it. The single biggest design
  consequence of multi-HBA: the routing table is DYNAMIC, driven by guest config
  writes, not static wiring. Generic SCSI HBAs are native-mode PCI (no legacy
  compat ports), so they live wherever the BAR says.

STRATUM 3 -- the HBA (controller) model.
  One PCI function, three sub-parts: config header (PCI identity + BARs); the
  register/command interface (CSRs the guest pokes -- the virtual HBA's command
  model, design freedom here); and a device map keyed on the bus's native axis
  ((target,lun) for SCSI, (channel,unit) for IDE). The HBA is the translation
  point from bus addressing to a device object, and the interrupt source (its PCI
  function's line). Five HBAs = five Stratum-3 instances, each a distinct function
  with its own map and its own interrupt -- which is why a completion can be
  attributed to one HBA deterministically.
  Down-contract: submit(command) to the resolved device (Stratum 4) with a
  completion callback; on completion (scheduled on the emulated timeline) set the
  status register and (regime 3 only) raise the interrupt.

STRATUM 4 -- the device surface.
  The addressing-agnostic leaf: VirtualScsiDevice::handleCommand/submit, media-
  backed. Owns the device property bag (type, media path, geometry/block size,
  model string) + CDB execution (INQUIRY, READ CAPACITY, READ(10), REQUEST SENSE).
  The 02/3A NOT-READY/MEDIUM-NOT-PRESENT answer is a Stratum-4 answer. Knows
  nothing of its (target,lun) or which HBA owns it. This is why ONE device class
  serves IDE, SCSI, and FC unchanged -- all topology lives above it.

================================================================================
## 2. V4 cross-reference (2026-06-09, grounded in code)

S0 access primitive   PARTIAL. Seam is mmioRead(pa,width,cpuId)/mmioWrite(...) via
  ISystemBus (TsunamiChipset : memoryLib::ISystemBus). Space is IMPLICIT in the
  decoded pa; return is SYNCHRONOUS, no completion token. Works, but it is method
  params not a value envelope, and the sync-return signature is what blocks the
  Stratum-3 submit->completion seam.

S1 physical decode    HAVE (STRONG). TsunamiChipset::mmioRead routes Cchip/Dchip/
  Pchip. TsunamiPchip has real sparse AND dense I/O + mem windows with
  SparseSpace::decodePciAddr/decodeByteLane/decodeXferLen; config window present.
  The genuine Alpha translation already exists -- where we are ahead. (Open: confirm
  config swizzle type0/type1 for a multi-function walk.)

S2 PCI bus            PARTIAL -- THE KEY GAP. Have: registerPciDevice(bus,dev,func)
  BDF map; empty slot floats 0xFFFFFFFF; synthesizePciConfig already computes a
  barMask (the size-probe readback). Missing: registerIoPortRange/
  registerPciMemRange are FIXED-RANGE claimants -- the decode range is wired at
  ATTACH time and never moves. No BAR-write -> range rebind. (See Sec 3, step 3 --
  this is smaller than "implement BAR assignment".)

S3 HBA model          PARTIAL. Cy82C693Ide: config header (pciConfigRead) ok,
  taskfile CSR command model ok, (channel,unit) map (attachDevice) ok, interrupt
  source NONE (polled -- correct for SRM). SCSI HBA: NONE (Symbios is a config stub
  only -- no command model, no (target,lun) map). The submit->completion seam: NOT
  built (handleCommand is inline/synchronous).

S4 device surface     HAVE. VirtualScsiDevice / VirtualIsoDevice handleCommand, CDB
  exec, the 02/3A sense. Media file backend STUBBED (m_hasMedia branch, no image
  read). On the critical path for a real boot (see Sec 3, step 5).

Six-point faithfulness checklist:
  1 config-space walking            HAVE (enum map + all-ones float + class code).
  2 BAR sizing/assignment           DESIGNED in synthesis (barMask), NOT WIRED to a
                                     live device.
  3 dynamic range->function rebind  MISSING. The structural addition multi-HBA
                                     needs; the legacy-IDE shortcut skipped it.
  4 per-function interrupt routing  PARTIAL (DRIR/evalDeviceIrqs path exists; no
                                     per-PCI-function INTx routing). NOT needed for
                                     the SRM polled path -- defer.
  5 interrupt ack / status semantics MISSING the HBA completion-STATUS register.
                                     NEEDED in fork-1 (polled drivers read status in
                                     a loop; polled != statusless). Needed SOONER
                                     than (4).
  6 determinism on completion        Banked as a design constraint (S-DETERM); not
                                     built. Attaches to step-1's completion seam.

================================================================================
## 3. Refinements that resize the work

R1. The Stratum-2 gap is SMALLER than "implement BAR assignment". The size-
    reporting half (barMask) already exists. What is missing is the WRITE half and
    its consequence: a config write to a BAR offset must (a) store the guest's
    assigned base, (b) on the all-ones probe write, return the precomputed barMask
    instead of the base, (c) on the real base write, REBIND the range map
    (unregister old interval, register new). The change is to make the decode
    range OWNED BY THE BAR REGISTER'S STATE rather than by the attach call. It is a
    wiring change at one seam, not a new subsystem.

R2. Do we even need DYNAMIC rebind to boot? Point (3) is load-bearing for
    multi-HBA in general, but interrogate whether SRM-on-DS10 REASSIGNS BARs or
    accepts firmware-assigned ones. If SRM programs the BAR ONCE during its probe
    and never moves it, then SRM-boot-faithful needs BAR WRITE-ACCEPTANCE (store
    base, rebind once), NOT full dynamic rebind-on-every-write. The general version
    is correct and wanted eventually for regime 3, but building it when SRM writes-
    once is gold-plating the exact layer we want lean. DECIDE FROM A TRACE (step 2),
    do not build blind.

R3. Stratum 0 envelope + completion token: fix FIRST, and the load-bearing half is
    the COMPLETION TOKEN, not the value type. As long as the bus contract is "read
    returns data inline," every HBA inherits sync-return -- and the SCSI HBA we are
    about to build would bake sync-return into its command path (the retrofit
    trap). Make the access a value type in the same pass since the signature is
    being touched anyway. SEQUENCING: this MUST land before the SCSI HBA model.

R4. Stratum-3 interrupt source: LEAVE IT (polled). SRM dk/dq drivers poll; the
    CRB-callback window polls (SRM driving); a booting kernel never needs the IDE
    or SCSI completion interrupt to fire. Build the SCSI HBA with the submit->
    completion seam where completion just sets the STATUS REGISTER and returns (no
    IRQ raise) -- same spirit as the synchronous IDE path. The IRQ raise is a
    one-line addition at that seam when regime 3 arrives. BUT the completion-status
    register (point 5) MUST exist in fork-1: the polled driver reads it to learn
    the command finished. Defer the interrupt, NOT the status register.

================================================================================
## 4. Locked build order (2026-06-09)

1. STRATUM 0 envelope + completion token. Value type for the access; submit->
   completion seam where completion is inline-invoked today (S-SEAM). Bank
   S-DETERM here. FIRST -- it is the contract every later piece inherits.

2. TRACE SRM's BAR writes (cheap diagnostic, like the dqa0 trace). Decides whether
   step 3 needs write-once-acceptance or full dynamic rebind (R2). ALSO capture
   whether SRM even probes the Symbios config stub today: does it walk the SCSI
   function's config header and try to size its BARs, or skip it because the
   class/header is not convincing yet? That tells us whether step 4's "config stub"
   must first become a real enumeration-valid header before SRM will attempt BAR
   assignment on it. FIRST thing to look at in the trace.

3. STRATUM 2 BAR->range binding. Sized by step 2. Wire the BAR write through to the
   (already-computed) mask and to a range rebind. Load-bearing either way; only its
   generality (write-once vs dynamic) is in question.

4. STRATUM 3 SCSI HBA. Config header + CSR command model + (target,lun) map +
   completion-STATUS register, built on the step-1 seam. NO IRQ raise yet (R4).

5. STRATUM 4 media backend. Replace the m_hasMedia stub with a real image-file
   backing so the 30-disk attach has something behind each (target,lun). On the
   critical path: a 30-disk fabric with a stubbed media branch boots 30 NOT-READY
   devices -- proves the fabric, NOT the boot.

================================================================================
## 5. Open questions to resolve from the step-2 trace

Q1. Does SRM-on-DS10 write each BAR ONCE (accept firmware/own-probe assignment) or
    re-assign/move it? -> sizes step 3 (write-once-acceptance vs dynamic rebind).
Q2. Does SRM probe the Symbios SCSI config stub at all today (walk its header, try
    to size BARs), or skip it? -> decides whether step 4 needs a real enumeration-
    valid header BEFORE BAR assignment will be attempted. FIRST look.
Q3. (Step 1 incidental) Confirm the config swizzle handles type0 vs type1 cycles
    for a multi-function walk across the five HBA device numbers.

================================================================================
## 6. Task mapping

  Step 1 -> new "Stratum-0 access envelope + submit/completion seam" (front of the
           async work; task #8 fork-2 QThreadPool builds on this seam).
  Step 2 -> new "Trace SRM BAR writes + Symbios config probe" (diagnostic).
  Step 3 -> the P4 PCI consumer / Stratum-2 BAR->range binding (relates task #7).
  Step 4 -> new "Stratum-3 SCSI HBA model".
  Step 5 -> new "Stratum-4 media file backend".
  Self-describing device registry (task #9) = Stratum-2 topology knowledge +
  Stratum-3 per-function/(target,lun) descriptors; manifest = declarative source.

================================================================================
## 7. The submit/completion seam: thread-boundary contract (banked at step 1)

QThreadPool is a fork-2 implementation detail, but FIVE of its consequences are
fork-1 DESIGN CONSTRAINTS that must be baked into the step-1 seam or they force
the cross-layer rewrite we are designing out. Decided 2026-06-09.

C-VALUE (the gating contract -- decided now). The command-block and the
completion-result are SELF-CONTAINED VALUE TYPES that own their data, with NO live
pointers back into guest-visible state. A worker receives a COPY of the inputs it
needs (CDB, LBA, byte count, a handle to the backing HOST file -- never a pointer
to guest memory or the HBA data register), produces a RESULT VALUE (bytes read,
status, sense), and hands it back to be APPLIED ON THE EMULATION THREAD at the
scheduled cycle. The worker touches the host ISO/image file and nothing else.
  CONSEQUENCE for steps 3-4: VirtualScsiDevice::handleCommand must RETURN a result
  value, NOT write into a caller-provided buffer. The current ScsiCommand carries
  dataBuffer = &HBA pio buffer (device fills guest-visible HBA state in place) --
  that is the synchronous fork-1 pattern and is exactly the buffer-fill that breaks
  under threading. Build the new SCSI HBA + the seam value-out from the start; the
  existing IDE buffer-fill is legacy fork-1 that migrates to the seam later, NOT a
  template to copy.

C-WORKQ (Qt stays out of the core). Offload behind a CORE-OWNED abstraction
(IWorkQueue, std::function-based; backing pool swappable -- std or QThreadPool),
NOT a direct QThreadPool include in the core. Option A over Option B, and not on
purity grounds: the abstraction is WHERE the determinism rule is enforced (results
come back as scheduled events). Binding directly to QThreadPool tempts Qt
signal/slot completion delivery, which lands results on whatever thread the
connection type picks -- the next trap.

C-DELIVERY (Qt's event loop is NOT the timeline). The emulation thread runs a
tight CPU loop, not a QEventLoop, so a queued finished() signal either never
delivers, or forces a Qt event loop inside the CPU loop (perf + determinism
disaster), or delivers on the wrong thread. CORRECT shape: the worker pushes its
result onto a CORE-OWNED completion queue (lock-free or briefly-locked); the
emulation loop DRAINS that queue at a deterministic point and schedules each
result's apply-event at issue_cycle + N. Delivery is the timeline's job, not Qt's.
The pool runs labor only; the handoff back is our own queue + scheduler (owned by
IWorkQueue).

C-LATENCY (N is a model input, not free). N = emulated cycles from issue to
completion. DETERMINISTIC and replay-stable: same N for the same command under the
same conditions, derived from EMULATED state (command type, byte count -> a
modeled service time), NEVER from how long the host actually took (wall-clock N
diverges snapshots/replays). N small/zero for the boot milestone (tolerated by the
polled path); a realistic N is the knob that finally exercises the interrupt-
driven path (regime 3 DMA latency).

C-SNAPSHOT (in-flight commands are state). A snapshot taken mid-command must not
drop the pending completion or restore hangs waiting on a completion that never
comes. DISCIPLINE (a) BANKED: snapshot capture forces a completion-queue DRAIN to
a boundary before serializing -- fits the existing capture-side flush() drain,
simplest. (Discipline (b): serialize the pending command + its scheduled
completion cycle and re-arm on restore -- more faithful, more surface; defer.)
This COUPLES the async seam to the snapshot subsystem; know it before threading
lands, not as a restore hang.

Net: bank C-VALUE, C-WORKQ, C-DELIVERY, C-LATENCY, C-SNAPSHOT in the step-1 seam.
The QThreadPool swap is then just "replace the inline invoke with a pool submit"
behind IWorkQueue. Skip them now and the pool brings the cross-layer rewrite.

================================================================================
## 8. Architect memo refinements (2026-06-09)

Codified off the locked design; these REFINE Sections 1-7.

M1 S-DETERM rationale. The determinism rule's rationale is now REPLAY/TRACE
determinism; the SNAPSHOT rationale is RETIRED. The rule stands unchanged:
completion mutates guest-visible state on the emulated timeline, scheduled N
cycles out, never wall-clock.

M2 Snapshots are SRM-BOOT-ONLY and PRESUPPOSE inline completion; NOT extended to
running-OS / in-flight-async state. CONSEQUENCE: at the boot milestone there is no
in-flight async at snapshot time, so C-SNAPSHOT (Sec 7) RELAXES to "snapshots
presuppose inline completion"; the capture-drain coupling + discipline (b) are NOT
needed for boot. If extending snapshots to OS/async state is ever proposed it
RESURRECTS the full async<->snapshot coupling -- FLAG it, do not silently build.

M3 No guest driver-class modeling. Do NOT model dq/dk/dv/dr/ew/fg -- those are SRM
console-name conventions the firmware owns. Key every registry on the PHYSICAL
attachment axis, never the console-name prefix: IDE=(channel,unit),
SCSI=(target,lun), FC=native tuple. ONE device map PER CONTROLLER. No global device
pool, no per-prefix dictionaries. (Refines the Stratum-2/3 registry, task #9.)

M4 30-disk manifest storage[] schema (5 HBAs x 6 disks). Each storage entry carries
an EXPLICIT (target,lun) for SCSI (the (channel,unit) landed 2026-06-09 is the IDE
axis). validate() must reject: duplicate (target,lun) within a controller,
target == hba_id, out-of-range target/lun, and missing media file when media is
non-empty. (Extends the storage validation already landed.)

M5 Primary-source cross-check (PRD-004 lesson). Every register/decode/address claim
in the bring-back proposal is validated against the Tsunami/Typhoon HRM and the
21264A refs BEFORE acceptance. Standing requirement on the Section-7 deliverable.
