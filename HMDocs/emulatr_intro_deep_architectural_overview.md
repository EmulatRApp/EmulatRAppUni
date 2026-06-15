# Introduction — Deep Architectural Overview

ASA‑EmulatR is a full‑system, cycle‑based, high‑fidelity emulator of the DEC Alpha AXP
platform — specifically the 21264 (EV6) processor and the 21272 (Tsunami/Typhoon) core
logic as integrated in AlphaServer DS10/ES40/ES45‑class systems. It is implemented in
C++20 with a deliberately Qt‑free architectural core (Qt 6 is confined to the periphery —
UI, threading, and host glue), built with CMake under Visual Studio 2022 on a Windows host.
Its purpose is not to approximate an Alpha; it is to *be* one, faithfully enough that
unmodified DEC SRM console firmware, PALcode, and the operating systems that ran on the
metal run on EmulatR without knowing the difference.

This overview describes the architecture that makes that possible. It is written for readers
who need to reason about *why* the emulator is shaped the way it is — the execution model,
the memory system, the device fabric, the privileged boundary, and the invariants that hold
them together. The chapters that follow expand each subsystem in depth; this introduction
establishes the goals those subsystems serve and the single standard against which every one
of them is measured.

## 1. The Fidelity Standard

EmulatR is governed by one overriding standard: **it must behave like real Alpha hardware in
every architecturally visible way, even when doing so complicates the implementation.** This
is what "fidelity‑faithful" means in practice, and it is applied uniformly — not just to the
instruction set, but to *all* subsystems: memory translation, cache and coherency behavior,
the PAL boundary, interrupt and exception delivery, device and DMA semantics, and
multiprocessor ordering. A subsystem is correct only when no architecturally visible
difference from hardware can be observed through it.

The test of fidelity is black‑box and adversarial. The judges are not internal assertions but
the most demanding guests available: the SRM console and PALcode exercise the privileged
architecture directly; a booting operating system exercises the memory model, the trap
machinery, and the device fabric under real workloads; and a reference oracle (AXPBox) is
used for differential validation at the instruction and subsystem level. If any of these can
distinguish EmulatR from an AlphaServer through architecturally defined behavior, the
emulator is wrong, regardless of how convenient the deviation might be.

Fidelity here means *architectural‑semantic* accuracy, not the reproduction of a particular
chip's timing. EmulatR models Alpha's architectural semantics, not its performance
characteristics. What it does reproduce exactly is everything the architecture makes visible:
the cycle‑based run loop (one iteration corresponds to one hardware clock cycle), the precise
point at which results and exceptions become architecturally committed, the ordering
guarantees and barriers, and the privileged state transitions. Timing is a means to those
ends, not an end in itself.

## 2. Primary Goals and Objectives

EmulatR is designed around five primary goals, in strict priority order. Every design
decision is evaluated against these goals, in this order; when two goals conflict, the
higher‑priority goal wins.

1. **Architectural correctness first.** The emulator models Alpha's architectural semantics,
   not its performance characteristics. Correctness is always prioritized over speed.
2. **Deterministic and debuggable execution.** Execution must be understandable,
   reproducible, and inspectable. One iteration of the run loop corresponds to one hardware
   clock cycle, enabling deterministic replay and debugging. Correctness takes precedence
   over speculative aggressiveness.
3. **Explicit serialization.** Ordering and synchronization are never implicit. All
   serialization is driven by architectural mechanisms — the MB, WMB, EXCB, and TRAPB
   barriers, PAL entry, and exceptions — and all exceptions are delivered precisely at the
   architectural commit point, the Writeback stage.
4. **SMP as a first‑class concern.** Multiprocessor behavior is fundamental to the design,
   not layered on afterward. Cross‑CPU coordination is modeled with explicit barriers,
   reservation tracking, and IPI delivery, and every subsystem is designed with SMP
   correctness in mind from the outset.
5. **Separation of concerns.** Instruction decode, execution, memory access, coherency,
   privilege, and devices are isolated into well‑defined functional domains ("Boxes") with
   one‑way dependency flow. No domain reaches into another's internal state.

These objectives have been invariant across the entire history of the project. Four distinct
implementations have been written (see §6); each changed the *mechanism*, never the goals.

## 3. The Execution Model at a Glance

EmulatR executes through a **six‑stage in‑flight pipeline** — Fetch (IF), Decode (ID), Issue
(IS), Execute (EX), Memory (MEM), and Writeback (WB). The run loop is cycle‑based: a single
iteration advances every in‑flight instruction by one stage, mirroring one EV6 clock.
Instructions occupy `PipelineSlot`s in a ring‑buffer pipeline and carry their state forward
stage by stage until they retire. Architectural results and exceptions become visible only at
Writeback — the single, well‑defined commit point — which is what makes exceptions precise
and replay deterministic.

Within Execute, dispatch is *flat*. A decoded instruction is resolved through the grain
system to a single grain leaf that implements its semantics, with a decode cache amortizing
repeated decode of the same instruction. Execution itself is partitioned into the functional
**Boxes** — IBox (instruction/fetch/decode), EBox (integer), FBox (floating‑point), MBox
(memory), CBox (cache, coherency, control), and PalBox (the privileged architecture library
boundary) — each with a well‑defined interface and strictly one‑way dependency flow.

A deliberate consequence of this design is that the **hot execution path is shallow**: from
the run loop, through the pipeline tick, into a grain leaf, the call‑stack depth is typically
≤ 2, and rarely exceeds 4. This is not incidental. A flat path keeps a stack trace at any
point short and legible, directly serving the determinism‑and‑debuggability goal; and it
minimizes the per‑instruction scaffolding that profiling has shown — rather than the opcode
bodies themselves — to dominate runtime, directly serving performance without compromising
correctness. The grain leaves are the leaves of that call tree by construction.

> Detailed treatment: Chapter 2 (Execution Model), Chapter 3 (Pipeline Architecture),
> Chapters 4 and 14 (Functional Boxes), and Chapter 13 (AlphaPipeline Implementation).

## 4. Dependent Virtual Systems

A CPU core is not a machine. To present a bootable AlphaServer, EmulatR stands up — and
depends upon — a fabric of virtual subsystems, each held to the fidelity standard of §1:

- **The EV6 processor core**, including its internal processor registers (IPRs) and the
  privileged architecture library executed through PalBox.
- **The 21272 Tsunami/Typhoon core logic** — the Cchip (system control/address), the Dchip
  (data path), and the Pchip PCI interface(s) in the locked dual‑Pchip topology.
- **The PCI fabric**, modeled as a five‑stratum stack: S0 bus primitive → S1 physical decode
  → S2 BAR routing → S3 HBA controller → S4 device endpoint.
- **The AliM1543C southbridge** — the PCI‑to‑ISA bridge providing the TOY/RTC, legacy
  interrupt routing, and the IDE/ATAPI channel.
- **The storage subsystem** — the SCSI HBA and its `VirtualScsiDevice` endpoints, the
  IDE/ATAPI path, and removable/tape media variants.
- **Flash / NVRAM**, which backs the SRM environment store across reset.
- **The serial console (UART)**, the channel through which SRM console I/O is conducted.

EmulatR provides those virtual systems. It also *depends on* a small set of external systems
it does not reimplement: the **SRM console image and PALcode** it loads as firmware inputs;
the **Windows host** and its **Win32 VirtualAlloc** memory services (see §5); **Qt 6** at the
periphery; **spdlog** for runtime‑levelled logging; the **CMake / Visual Studio 2022**
toolchain; and the **AXPBox reference oracle** used for validation. The distinction matters:
the virtual systems above are the machine EmulatR *is*; the external systems are the
foundation it *stands on*.

> Detailed treatment: Chapter 10 and Chapter 16 (Devices, MMIO, and DMA), Chapter 20 (Boot
> Sequence, PAL, and SRM Integration), and Appendix C (Physical Address Memory Map).

## 5. Memory Management

EmulatR's memory system is two layers stacked: a host‑provided physical substrate beneath an
exactly modeled guest virtual‑memory architecture.

**Host backing.** `GuestMemory` is the shared physical‑memory abstraction; its RAM backend,
`SafeMemory` / `SparseMemoryBacking`, is an on‑demand page allocator built on the Windows
**VirtualAlloc** facility. The full guest physical address space is *reserved* up front
(`MEM_RESERVE`), but pages are *committed* (`MEM_COMMIT`) only as the guest first touches
them — so a multi‑gigabyte machine costs only the working set actually used. Host page
protection (via `VirtualAlloc`/`VirtualProtect`) is reused to carve out MMIO regions and to
back fault and watchpoint behavior. The strict `GuestMemory` ↔ `SafeMemory` separation
enforces the separation‑of‑concerns goal: device and MMIO traffic never aliases raw RAM, and
neither side reaches into the other.

**Guest architecture.** Atop that substrate, EmulatR implements the Alpha virtual‑memory
architecture precisely as specified in the **Alpha Architecture Reference Manual** — the
"ARM" (the *manual*, not the unrelated processor family). This is the architected memory
management the guest sees: the 64‑bit Alpha virtual‑address format and its field boundaries;
the PTE representation with its protection, granularity‑hint, and fault bits; software‑managed
translation realized through the SPAM TLB cache and the Ev6SiliconTLB layering; ASN management
and coherence; PAL‑mediated translation‑buffer fill and shootdown; and memory faults routed
through the `FaultDispatcher` and delivered precisely at Writeback.

The two‑layer arrangement is what lets EmulatR be simultaneously frugal and exact. The host
layer contributes lazy commit and a small resident footprint, host‑enforced page protection
reused for guest protection and instrumentation, and a clean isolation boundary; the guest
layer contributes a faithful, fully architected translation path — the same VA format, PTE
semantics, TLB behavior, and fault delivery a real EV6 exposes — with nothing about the host
backing visible to the guest.

> Detailed treatment: Chapter 5 (Memory System Architecture), Chapter 15 (Memory System
> Implementation Details), Chapter 17 (Address Translation, TLB, and PTE), and Appendices C
> (Physical Address Memory Map) and F (SPAM).

## 6. Lineage: Four Implementations, One Set of Objectives

The current emulator is the fourth implementation. The objectives of §2 were fixed from the
beginning; each version was an attempt to reach them by a different mechanism, and each taught
the lesson that shaped the next.

- **V1 — vtable‑grain dispatch.** Each instruction dispatched through a virtual call to a
  per‑instruction grain. It was architecturally correct and booted SRM, but the per‑instruction
  virtual‑call overhead capped throughput.
- **V2 — the performance proof‑of‑concept.** V2 pushed the performance strategy to its limit
  by dispatching on instruction *families* — operand‑shape signatures — rather than discrete
  named instructions. It was fast, but family classification under‑determined behavior:
  instructions sharing a signature still required per‑instruction exception and corner‑case
  handling that the flat family model could not express, leaving semantic residue.
- **V3 — a hybrid of V1 and V2.** V3 tried to combine V1's per‑instruction correctness with
  V2's flat dispatch. The coupling between the two models was too tight, and it introduced
  silent‑failure modes — the most dangerous outcome for an emulator whose entire value is
  fidelity.
- **V4 — the current design.** V4 keeps a flat dispatch path but resolves it to *faithful
  per‑instruction grain leaves*, backed by a decode cache, with performance pursued as the
  aggregation of many marginal gains rather than a single large architectural bet. It is the
  first version to satisfy all five goals at once — correctness, determinism, explicit
  serialization, SMP‑first design, and clean domain separation — without trading any of them
  away.

What carried through all four versions unchanged is the list in §2. The dispatch mechanism
evolved; the objectives did not.
