<!--
============================================================================
SPEC_memory_realms_and_scaffold.md -- Memory realms, post-JSR scaffold
============================================================================
Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1

Project Architect: Timothy Peer
AI Collaboration:  Claude (Anthropic)

Commercial use prohibited without separate license.
Contact:        peert@envysys.com  |  https://envysys.com
Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
============================================================================
-->

# EmulatR -- Memory Realms and Post-JSR Scaffold (Draft)

**Status:** Draft, open for discussion
**Companion to:** `SPEC_execution_model.md`
**Date:** 2026-05-05

---

## 0. Purpose and scope

This document captures the memory address-realm map, the post-JSR
initialization touchpoints we anticipate needing, and the scaffold
dependencies (fault dispatch, CALL_PAL routing, HW_MTPR side effects)
required for execution to progress from the JSR-to-PALcode-entry
boundary into early PALcode initialization.

The execution-model spec (`SPEC_execution_model.md`) is silent on
these concerns deliberately -- they are different concerns from
pipeline mechanics.  This document records what we know, what we do
not know yet, and what we explicitly defer past v1.

**Framing:** EmulatR v1 is a best-effort deterministic single-CPU
single-thread simulation.  Every scaffold piece in this document is
designed to preserve that determinism: no race-prone state, no
nondeterministic timing, predictable side-effect ordering.

**Status tags used in this document:**

| Tag | Meaning |
|-----|---------|
| `v1-stub` | v1 ships a placeholder.  Returns canned values, no real semantics.  Sufficient for boot to progress without crashing.  Full implementation post-v1. |
| `v1-full` | v1 ships the full functional implementation. |
| `deferred` | Out of scope for v1.  Documented here so the design exists, but no code lands. |
| `tbd` | Decision not yet made; flagged for discussion. |

---

## 1. Address realm map

Physical address regions and their meanings, as inferred from V3's
boot trace, the proposal text (`journals/EmulatR4_proposal.txt`
sections 2-4), and the ES40 / ES45 architecture references.  Where
the meaning is inferred rather than directly attestable from the
hardware reference manual, the row is marked `(inferred)`.

### 1.1 Realm table

| PA range | Name | Meaning | Source of truth |
|----------|------|---------|------------------|
| `0x000000 - 0x100000` | DRAM low | Standard DRAM, low 1 MiB.  PAL mirror region after PALcode is relocated.  GuestMemory authoritative. | ES45 memory map |
| `0x600000 - 0x700000` | PAL base region | PALcode lives here after SRM relocation.  IPR `PAL_BASE` points at `0x600000`.  PAL trap dispatch lands at `0x600000 + offset`. | V3 trace, Alpha HRM |
| `0x6005C0` | PAL entry (initial) | First instruction of PALcode entry stub at boot.  JSR target from SRM init. | V3 trace (inferred) |
| `0x900000 - 0xA00000` | Boot ROM staging | Decompressed SRM ROM image lives here during boot.  Source data for the firmware-relocation copy loop.  NOT mutated during execution. | V3 trace (inferred) |
| `0x900004` | Boot stub entry | First PC after CPU reset.  Entry to the SRM bootstrap stub. | V3 trace |
| `0x9003EC - 0x900410` | Copy loop region | The decompression / relocation copy loop body.  R2 = source pointer, R0 = destination pointer, R1 = byte count. | V3 trace (inferred) |
| `0x90041C` | SRM-to-PAL JSR | The JSR that transfers control from SRM init to PALcode at 0x6005C0.  Form: `JSR R31, (R0)` with R0 set up by preceding LDA. | V3 trace |
| HWRPB region | HW Restart Param Block | Tsunami / chipset hardware restart parameter block.  Address TBD; populated by SRM init, consumed by PALcode and OS handoff. | Alpha HRM |
| Tsunami CSRs | Chipset registers | MMIO region for the 21272 (Tsunami / Typhoon) chipset.  Typically PA >= 0x80100000000.  PCHIP, IO, hose registers. | 21272 HRM |

### 1.2 Aliasing rule (carried from proposal section 6)

Memory is **single-sourced**: GuestMemory is authoritative.  The
0x000000, 0x600000, and 0x900000 regions are distinct unless the
MMU explicitly maps them.  No code path performs cross-region
shadow writes.  This was a V3 hypothesis we explicitly disproved
and the v4 design forbids it (anti-pattern 8.6 in execution-model
spec).

The IBox fetch view is distinct from the data view.  During boot
the IBox view reads from the ROM backing image (immutable);
HW_LD / HW_ST always read from / write to GuestMemory.  Once the
SRM loader's `done:` predicate fires, the IBox view switches to
GuestMemory and the ROM backing image becomes inert.

### 1.3 Open questions

- The exact PA boundaries of the boot ROM staging region (0x900000
  vs some other base) -- inferred from V3, not directly cited.
  `tbd-need-hrm-cite`.
- HWRPB address.  V3 boots far enough to populate but the trace
  doesn't directly show a HWRPB read.  `tbd`.
- Tsunami CSR base address(es).  V3 may not have exercised these
  during the SRM-to-PAL handoff.  `tbd-need-hrm-cite`.

---

## 2. Post-JSR initialization touchpoints

These are the operations we anticipate needing once execution
crosses the JSR boundary at PC = 0x6005C0 and starts running real
PALcode.  Each is tagged with its v1 scope.

### 2.1 PALcode entry stub (PC 0x6005C0 - 0x6005FF)

The first 64 bytes of PALcode are the entry stub.  V3 traced this
sequence:

| PC | Instruction | Purpose |
|----|-------------|---------|
| 0x6005C0 | `BIS R31, R26, R30` | R30 = R26 = 0x600000 (PAL base) |
| 0x6005C4 | `BR R26, 0x6005E0` | R26 = 0x6005C8 link, redirect |
| 0x6005E0 | `HW_MTPR R31, IPR=0x10` | Pipeline barrier (write 0 to PAL_BASE) |
| 0x6005E4-EC | `BIS R31, R31, R31` x3 | Pipeline drain NOPs |
| 0x6005F0 | `BR R7, 0x6005F4` | R7 = next PC (with PAL bit) |
| 0x6005F4-FC | ZAP / SLL / SRL R7 | Wipe R7 to zero |
| 0x600600+ | HW_LD chain | Read descriptor table (R27, R16, R0) |

**Status:** `v1-full`.  This is exactly the path V3 chased and we
expect v4 to retire correctly per the execution-model spec.

### 2.2 HWRPB initialization

The Hardware Restart Parameter Block is populated by SRM init and
consumed by PALcode and the OS.  Contains: CPU id, CPU model, system
type, console firmware revision, memory map, page table base, etc.

**v1 needs:** enough HWRPB content for early PALcode reads to find
the values they expect.  Probably canned constants populated at
GuestMemory load time rather than computed dynamically.

**Status:** `v1-stub`.  Document the field set the early PALcode
actually reads; populate those fields with canned values that look
plausible to an ES40/ES45 PALcode.  Real HWRPB construction lands
post-v1.

### 2.3 Tsunami / Typhoon chipset bridge

The 21272 chipset bridges the EV6 CPU to PCI / IO / memory.  Early
PALcode may read CSRs to determine system configuration (number of
DRAM banks, PCHIP presence, etc.).

**v1 needs:** stubbed CSR reads that return canned values matching
a plausible ES45 configuration.  No real PCI / IO emulation in v1.

**Status:** `v1-stub`.  Real Tsunami emulation is `deferred` past
v1.

### 2.4 IPR initialization

Several IPRs are written during early PALcode init:

- `PAL_BASE` -- already written (cleared) by the entry stub at
  0x6005E0.  Subsequent PALcode reads it back via HW_MFPR and
  reconstructs the value (V3 cycles 18-22 in the post-handoff
  trace).
- `PCBB` -- Process Control Block Base.  Points at HWPCB.
- `PTBR` -- Page Table Base Register.  Required before TLB
  operations are meaningful.
- `ASN` -- Address Space Number.  Set on context establishment.
- `MCES` -- Machine Check Error Summary.  Cleared at boot.
- `SCBB` -- System Control Block Base.  Points at exception
  vector table.
- `IPL` -- Interrupt Priority Level.  Set high during init.
- `ASTEN` / `ASTSR` -- AST enable / summary.  OpenVMS-specific.

**Status:** `v1-full` for the IPR storage (PalIPR struct in
iprLib already supports these).  The HW_MTPR side effects beyond
"write the field" are `v1-stub`: e.g., writing PAL_BASE updates
the field but does not also flush the I-cache (the I-cache is not
modeled in v1).

---

## 3. Scaffold dependencies

What v4 must provide so post-JSR execution does not crash on the
first non-trivial instruction.

### 3.1 Fault dispatch path

Memory access faults, alignment faults, OPCDEC, FLOATING POINT
DISABLED, and similar precise traps need a fault dispatch path that:

1. Captures the trapping PC into IPR_EXC_ADDR.
2. Switches the CPU to PAL mode.
3. Sets PC to the appropriate trap vector (PAL_BASE + offset).
4. Saves any required state (PS, FPCR shadow) into IPRs the PAL
   handler will read.
5. Discards the trapping slot from the pipeline (pipeline flush).

The function-table dispatch (execution-model spec section 4)
includes per-grain trap codes; the executor sets
`slot.boxResult.setFault(code, pc, va)`; MEM detects the fault flag
and routes through this dispatch instead of normal commit.

**Status:** `v1-full` for the dispatch path.  Specific fault
handlers (the PALcode that runs at the trap vector) are `v1-stub`
because PALcode itself is what we are trying to emulate; v1 needs
just enough to not crash on the most common faults.

### 3.2 CALL_PAL routing

CALL_PAL (opcode 0x00, function code in low 8 bits) enters PAL
mode and dispatches to the appropriate handler.  V4 dispatches
through the function-table per the execution-model spec; this
section adds the runtime entry/exit semantics.

CALL_PAL handler entry:
1. Save current PS into IPR EXC_PS (or equivalent).
2. Set CPU to PAL mode (PS<CM> = 0).
3. Compute trap vector: PAL_BASE + 0x2000 + (function * 64) for
   privileged, PAL_BASE + 0x3000 + ((function & 0x7F) * 64) for
   unprivileged.  (Exact offsets per ES45 PAL spec; verify against
   HRM.)
4. Set PC to the trap vector address.
5. Continue execution from there as ordinary PAL instructions.

CALL_PAL handler exit (HW_REI):
1. Restore PS from EXC_PS.
2. Set PC to the saved exception address (IPR EXC_ADDR).
3. Exit PAL mode.

Personality matters here: CALL_PAL function codes 0x00-0x3F are
privileged on EV6; 0x80-0xBF are unprivileged user-callable.  The
`_64` suffix convention from execution-model spec section 4.4
selects the Tru64 variant when both exist.

**Status:** `v1-full` for the routing infrastructure.  The PALcode
that lives at each vector is `v1-stub` for early PALcode (a
return-immediately stub) and `v1-full` for the handful of CALL_PAL
codes that early SRM init actually invokes.

### 3.3 HW_MTPR side effects

Writing certain IPRs via HW_MTPR has side effects beyond the
field write:

- `PAL_BASE` write -- I-cache flush in real hardware.  v1 does
  not model the I-cache, so the side effect is a no-op.  Note this
  in the executor body.
- `ITB_TAG` / `ITB_PTE` write -- inserts into the instruction TLB.
  v1: routes into the SPAM manager (or v1-stub TLB equivalent --
  see section 4).
- `DTB_TAG` / `DTB_PTE` write -- inserts into the data TLB.
  Same as above.
- `ASN` write -- changes the active ASN, which invalidates non-ASM
  TLB entries.  v1: epoch bump in the TLB manager (SPAM does this
  via `invalidateNonASM`).
- `IPIR` write -- IPI to other CPUs.  v1: no-op (single-CPU).
- `MCES` write -- clears machine check summary.  Just a field
  write.

**Status:** `v1-full` for IPR field writes.  Side effects are
a mix: I-cache and IPI are no-op in v1; TLB-related side effects
need the TLB manager (section 4 below).

### 3.4 HW_REI semantics

Return from PAL mode.  PC restored from saved exception address
IPR; PS restored; PAL mode exited.  Already noted in 3.2 above.

**Status:** `v1-full`.  The executor lives in PalBox.

### 3.5 Pipeline-level scaffold

When a fault or PAL trap fires at EX:

- The slot is allowed to retire to MEM and WB normally with its
  fault flag set.  MEM applies the architectural state changes
  associated with trap entry (PC, PS, IPR saves) and then sets
  m_pc to the trap vector for the next IF.  WB traces the trap.
- Younger slots in IF / DE / GR are squashed by the same flush
  that handles a misprediction (selective flush, never reaches
  MEM).
- Older slots in MEM and WB are NEVER squashed.  This is the
  spec invariant in execution-model section 1.2 rule 5.

**Status:** `v1-full` for the pipeline scaffold; the spec already
encodes the invariants.

---

## 4. SPAM / pteLib carryover decision

V3's `D:\EmulatR\EmulatRAppUni\pteLib\` provides a complete TLB and
PTE management subsystem.  Key files:

- `alpha_spam_manager.h` -- 4D shard manager
  (cpu x realm x sizeClass x bucketIndex).  Full Alpha TLB
  instruction coverage: TBIA, TBIS, TBISD, TBISI, TBCHK, TBIAP.
  Per-CPU epoch tracking for fast invalidation.  Cross-CPU IPI
  path.  GH coverage bitmap optimization.
- `alpha_spam_bucket.h` -- the bucket primitive used by the
  shard manager.  Configurable associativity, replacement policy
  (default SRRIP).
- `alpha_spam_types.h` -- SPAMTag, SPAMEntry, Realm, ASNType.
- `SPAMEpoch_inl.h` -- per-CPU epoch table for lazy invalidation.
- `alpha_pte_core.h` / `AlphaPTE_Core.h` -- low-level PTE access.
- `alpha_pte_traits.h` / `Ev6PTETraits.h` -- EV6-specific PTE
  layout.
- `Ev6SiliconTLB.h` / `global_Ev6TLB_Singleton.h` -- silicon-level
  TLB types and a per-CPU singleton wrapper.
- `buildCanonicalTLBTag_inl.h` -- tag construction helper.
- `calculateEffectiveAddress.h` -- VA-to-PA translation entry.
- `ev6Translation_struct.h` -- EV6-specific VA-to-PA translation
  result structure.  Returned by the lookup path.
- `TLB_Helpers_inl.h` / `TLBShootdownTypes.h` -- helper inlines and
  shootdown message types.

### 4.1 Project target informs the decision

The EmulatR target is **4-CPU ES45**: four AlphaServer EV6 cores
sharing memory and a Tsunami / Typhoon chipset.  At boot time only
**CPU 0 runs SRM** while CPUs 1-3 are quiescent; the additional
CPUs come online later via PAL machinery (SWPCTX / IPI / start-up
broadcasts).

This shapes the SPAM decision: the multi-CPU machinery in V3's SPAM
(per-CPU epoch tables, cross-CPU IPI broadcast for TBIAP, ASN
sharing across cores) is **load-bearing** for the project target,
even though v1 only exercises CPU 0.  Removing it for v1 and
re-introducing it for v2 risks subtle correctness drift; carrying it
forward keeps the multi-CPU semantics in place from day one and
v1 simply does not exercise the inactive paths.

The single-thread v1 decision (execution-model spec section 9.7)
is unchanged: one OS thread drives the simulation.  The four
simulated CPUs are scheduled round-robin on that one thread.  The
no-lock-lazy concurrency primitives in SPAM are inert under
single-thread scheduling but become live when v2 may parallelize
the per-CPU pipelines.

### 4.2 Decision (2026-05-05)

**Carry forward the existing V3 SPAM model wholesale**, including:

- 4D shard structure (cpu x realm x sizeClass x bucketIndex).
- Per-CPU epoch tables for non-ASM lazy invalidation.
- Cross-CPU IPI paths for TBIAP / TBIS broadcast.
- 4-way set-associative buckets with SRRIP replacement.
- GH coverage bitmap optimization.
- Full Alpha TLB instruction coverage (TBIA, TBIS, TBISD,
  TBISI, TBCHK, TBIAP).

Plus the dependent files listed in section 4 above:
`alpha_pte_core.h` / `AlphaPTE_Core.h`, `alpha_pte_traits.h` /
`Ev6PTETraits.h`, `Ev6SiliconTLB.h` /
`global_Ev6TLB_Singleton.h`, `buildCanonicalTLBTag_inl.h`,
`calculateEffectiveAddress.h`, `ev6Translation_struct.h`,
`TLB_Helpers_inl.h`, `TLBShootdownTypes.h`.

Land path: the carryover lives in v4's `pteLib/` (same directory
name as V3, since we are preserving the V3 design wholesale).
Each file gets the `// EmulatR V3 reuse, no semantic change` header
addendum per ADR-0001 section 10.4.  Files that need light tightening
to fit V4 conventions (header comment style, ASCII-128 normalization,
naming alignment with the BoxResult / function-table dispatch) get
the `// EmulatR V3 carryover, V4-tightened` addendum instead.

### 4.3 v1 exercise scope

v1 boots CPU 0 from reset through SRM init to early PALcode.  The
SPAM/TLB code paths v1 actually exercises:

- TLB insert via HW_MTPR (`ITB_TAG`/`ITB_PTE`, `DTB_TAG`/`DTB_PTE`).
- TLB lookup on every IF fetch (when MMU is on -- early SRM may run
  in physical-mapping mode where lookup short-circuits).
- TLB invalidation via HW_MTPR (`TBIA`, `TBIS`, etc.) as PALcode
  init clears state.

SPAM code paths v1 does NOT exercise (but v1 still carries):

- Cross-CPU TBIAP / TBISI / TBISD broadcast paths (no other CPUs
  active).
- `invalidateTLBsByASN_AllCPUs` (single-CPU run).
- IPI dispatch (no other CPUs to receive).

The non-exercised paths must still compile and link, and their
single-CPU equivalents must function correctly.  v1 testing covers
them via single-CPU unit tests against the SPAM API (e.g., a test
that calls `invalidateTLBsByASN_AllCPUs` on a one-CPU system and
verifies it degrades to the local invalidate cleanly).

### 4.4 Status

**Decided** -- 2026-05-05.  Carry forward the V3 SPAM model
wholesale.  Implementation lands when v4 reaches the post-JSR
boundary and TLB instructions begin firing.

---

## 5. Anticipated v1 deferral list

Items explicitly out of v1 scope.  Documented so when we reach the
post-JSR boundary in v4 implementation we have a decision record
rather than ad-hoc choices.

### 5.1 OS handoff

The transition from PALcode init to OpenVMS or Tru64 boot.  v1
goal: boot SRM through to early PALcode init.  OS handoff and the
machinery it requires (full TLB, full IPR set, exception delivery
to OS-supplied handlers, console driver init) is `deferred`.

### 5.2 Multi-CPU bring-up

Multi-CPU scheduling (round-robin or otherwise), bringing CPUs 1-3
online via SWPCTX / start-up IPI, per-CPU pipeline parallelism in a
later v2, MP synchronization barriers exposed to OS code -- all
`deferred`.  v1 boots CPU 0 only; CPUs 1-3 are present in the
simulator but quiescent.

Note: the SPAM / TLB machinery to *support* multi-CPU is carried
forward in v1 (section 4.2 above) -- per-CPU epoch tables, IPI
broadcast paths, ASN sharing across cores -- but the multi-CPU
*bring-up* is deferred.  The non-CPU-0 paths exist and compile;
they are simply not exercised in v1.

### 5.3 Full Tsunami / Typhoon emulation

PCHIP register emulation, IO bridge MMIO, PCI device emulation,
console serial port -- all `deferred`.  v1 stubs CSR reads with
canned values.

### 5.4 Cache modeling

I-cache, D-cache, B-cache (board cache), prefetch hints -- all
`deferred`.  v1 treats memory as flat with no caching effects.
HW_MTPR side effects that flush caches in real hardware are
no-ops in v1.

### 5.5 Performance counter modeling

EV6 has performance counters readable via PMU IPRs.  v1: stubbed,
return zeros.  `deferred`.

### 5.6 Full snapshot save / restore

Designed in execution-model spec section 11; implemented post-v1.
v1 ships API stubs.  `deferred`.

### 5.7 OS-specific features

OpenVMS AST delivery, Tru64 syscall dispatch, condition handlers,
process scheduling -- all `deferred`.  v1 is bare-metal SRM-to-PAL.

---

## 6. Acceptance criteria for this section

This document is considered settled when:

1. The realm map (section 1) is reviewed against the ES45 HRM and
   the `tbd-need-hrm-cite` rows are either confirmed or annotated.
2. The post-JSR touchpoints (section 2) have agreed v1 scope tags.
3. The scaffold dependencies (section 3) are reviewed and the
   `v1-full` items are confirmed as v1 scope.
4. ~~The SPAM / pteLib carryover decision (section 4) is made.~~
   **Decided 2026-05-05:** carry forward the V3 SPAM model
   wholesale, including multi-CPU machinery, since the project
   target is 4-CPU ES45.
5. The deferral list (section 5) is reviewed; any item we want to
   pull into v1 scope is moved.

Once the realm-map citations are confirmed and items 1-3 / 5 are
reviewed, the document moves from "Draft" to "Settled" and v4
implementation work past the JSR boundary can begin against it.
