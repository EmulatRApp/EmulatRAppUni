# EV6 Specification Rev 2.0 -- V4 Audit Sheet

Spec source:  `EV6_Specification_Rev_2.0_199604.txt`
Audit baseline: 2026-05-19, scaffold + first-pass status.

This document is the canonical coverage report between the EV6
specification and EmulatR V4's implementation.  Each row maps a
specific spec requirement to a V4 implementation state.

## How to use

Rows are organised by spec chapter and section number.  Status:

```
TODO         -- spec requires it, V4 does not implement, must be
                addressed eventually
PARTIAL      -- partially implemented; specific gap noted
IMPLEMENTED  -- spec requirement met to the extent emulation needs it
NOOP         -- requirement does not apply to a software emulator
                (e.g., pin timing, clock generation, packaging)
DEFERRED     -- requirement applies but explicitly postponed to a
                later phase with documented rationale
```

When auditing a row: check the spec section, look at the V4 location,
update the status if it changed.  When closing a TODO row to
IMPLEMENTED, add a short Notes entry citing the commit or file that
made it true.

When opening a new TODO row from a freshly-discovered gap: add a
Notes entry citing the discovery context (trace, fault, etc.).

The audit lives alongside the code so future sessions can pick it up
without re-reading the spec.

### V1 as a starting-point reference (NOT authoritative)

V1 (D:\EmulatR\EmulatRAppUni) is read-only and implements much of what
the spec describes -- IPR semantics, TLB walking, VA_FORM computation,
PAL handler dispatch, etc.  V1 saves us from designing every TODO row
from scratch; it gives us a working reference shape to port from.

**But V1 has bugs and incomplete implementations.**  Several
V4-vs-V1 differences this project has already identified came from
V1 being wrong (e.g., V1's PA-0 firmware mirror that we abandoned in
the AXPBox-aligned SrmLoader rewrite).  Treat V1 as a starting
point, not a source of truth.

### Authoritative sources, in priority order

1.  **Alpha 21264/EV6 HRM** -- the spec this audit is built against.
    Section numbers in the leftmost column map to HRM sections.
    `D:\EmulatR\Processor Support\21264ev6_hrm.pdf`,
    `21264-Alpha DataSheet.pdf`,
    `Alpha 21264-EV67 Microprocessor Hardware Reference Manual.pdf`.
2.  **Alpha Architecture Reference Manual** -- ISA semantics for
    instructions, register conventions, traps.
3.  **EV6-specific PALcode SOURCE** -- the actual assembly real Alpha
    hardware was designed to execute.  Closer to ground truth than
    any emulator.  Especially:
        `D:\EmulatR\Processor Support\Palcode\palcode\palcode\src\ev6_osf_pal.mar`
        `...\src\ev6_defs.mar`
        `...\src\target_osf.mar`
        `...\src\ev6_pal_macros.mar`
        `...\src\ev6_pal_temps.mar`
    When PALcode source declares a convention (e.g.,
    `p23 = r23 call_pal linkage register` at ev6_osf_pal.mar:803),
    that IS the ground truth -- the real chip ran this code.
4.  **Tsunami/Typhoon 21272/21274 HRM** -- chipset behaviour for
    Cchip / Dchip / Pchip / CSRs / interrupts / cache coherence.
    `D:\EmulatR\Processor Support\` Tsunami chapters.
5.  **AXPBox source** -- second working implementation.  Useful for
    "how does another emulator handle this?" cross-checks.  Imperfect
    but actively boots real SRM, so cross-references against it are
    empirically grounded.  `D:\EmulatR\axpbox\src\`.

### Porting workflow

  1. Identify the audit row's TODO requirement and HRM section.
  2. Read the HRM section first to understand the spec-correct
     behaviour, edge cases, and required field layouts.
  3. Grep V1 for an existing implementation as a structural starting
     point.  Read it but do NOT copy uncritically.
  4. Cross-check against AXPBox if the behaviour involves chipset
     interaction, interrupts, or any "system" behaviour.
  5. Translate to V4 conventions: namespaces (eBox::, mBox::, etc.),
     BoxResult contract, S_* semantic flag set, AXP_HOT/AXP_FLATTEN
     attributes, doctest CHECK (not REQUIRE), include guards (not
     pragma once).
  6. Document deltas from V1 in the audit row's Notes -- where we
     went a different direction and why.
  7. Update the audit row: status -> IMPLEMENTED, V4 Location set,
     Notes records HRM section + V1 ref (if used) + any V4-specific
     decisions.

When V1 and the HRM disagree, the HRM wins.  Note the V1 bug in the
audit row's Notes for the historical record.

## Coverage summary (post-scaffold, pre-first-audit)

| Chapter | Title | Rows | Initial status profile |
| ------- | ----- | ---- | ---------------------- |
| 1       | EV6 + Alpha Architecture            |  4 | All NOOP (Digital marketing scaffold) |
| 2       | Internal Architecture               | 33 | Mixed |
| 3       | External Interface                  | 14 | Mostly NOOP (no real bus); a few IMPLEMENTED via Tsunami model |
| 4       | PALcode                             | 18 | Mostly IMPLEMENTED / PARTIAL |
| 5       | Internal Processor Registers        | 60 | Mixed; ~30% IMPLEMENTED |
| 6       | IEEE FP Conformance                 |  9 | TODO (FP arith stubs; FPCR not wired) |
| 7       | Error Detection and Handling        | 11 | All NOOP / DEFERRED |
| 8       | Initialization and Test             |  3 | TODO (reset path partial) |
| 9       | Electrical Data                     |  1 | NOOP |
| 10      | Packaging                           |  1 | NOOP |
| 11      | Appendix 1: Reset / Sleep           |  2 | TODO / NOOP |
| 12      | Appendix 2: PAL Coding Restrictions | 27 | Mixed |

Total: 183 rows.  Approximately accurate after the first-pass audit
adjusts numbers up and down.

---

# Chapter 1: EV6 and the Alpha Architecture

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 1.1     | Alpha Architectural Extensions implemented               | NOOP   | n/a         | Scaffold; emulator inherits Alpha ISA from leaf coverage. |
| 1.2     | Implementation-Specific Features identified              | NOOP   | n/a         | Documented via individual IPR rows in Ch 5. |
| 1.3     | Instruction Set Features defined as Optional             | NOOP   | n/a         | BWX, MVI etc. tracked under their own opcode entries. |
| 1.4     | Arithmetic Exceptions                                    | TODO   | --          | FP arithmetic traps not delivered; deferred with FPCR work. |

---

# Chapter 2: Internal Architecture

## 2.1 Chip Organization

| Section | Requirement                                              | Status      | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ----------- | ----------- | ----- |
| 2.1.1   | Ebox: 80-entry register file, 4-wide issue, 4 ALUs       | NOOP        | eBoxLib/    | Functional emulator; no microarchitectural fidelity. |
| 2.1.2   | Fbox: FP adder, multiplier, divider                      | PARTIAL     | fBoxLib/    | FP arithmetic leaves stub-implemented for T-format (ADDT/SUBT/MULT/DIVT/CMP*); no FP arithmetic trap support. |
| 2.1.3   | Ibox: 80-entry inflight queue, branch predictor, 2 modes | NOOP        | iBoxLib/    | V4 v1 branch prediction: 100% mispredicted (squash-and-refetch). |
| 2.1.4   | On-chip caches: 64KB Icache + 64KB Dcache                | NOOP        | n/a         | No cache modelling; direct GuestMemory. |
| 2.1.5   | Mbox: 32-entry load queue, 32-entry store queue          | NOOP        | mBoxLib/    | MemDrainer is the V4 simplification of Mbox. |
| 2.1.6   | Cbox: external interface controller                      | PARTIAL     | chipsetLib/ | Modelled via Tsunami21274 chipset; no Bcache simulation. |

## 2.2 Pipeline Organization (7-stage)

| Section | Requirement                                              | Status   | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | -------- | ------------------------ | ----- |
| 2.2.1   | Stage 0: Instruction Fetch                               | IMPLEMENTED | pipelineLib/PipelineDriver.h | Fetch via GuestMemory.read4. |
| 2.2.2   | Stage 1: Instruction Slot                                | NOOP     | --                       | V4 issues 1-wide. |
| 2.2.3   | Stage 2: Map                                             | NOOP     | --                       | No register renaming. |
| 2.2.4   | Stage 3: Issue                                           | NOOP     | --                       | In-order; no issue queues. |
| 2.2.5   | Stage 4: Register Read                                   | IMPLEMENTED | PipelineDriver.h ExecCtx setup | opA/opB resolution; fp/int regfile selection. |
| 2.2.6   | Stage 5: Execute                                         | IMPLEMENTED | leaf dispatch        | Per-leaf BoxResult production. |
| 2.2.7   | Stage 6: Dcache Access                                   | IMPLEMENTED | pipelineLib/MemDrainer.h | memAddr/memSize handling. |
| 2.2.8   | Instruction Retire                                       | IMPLEMENTED | PipelineDriver.h retire phase | WB writes commit. |
| 2.2.9   | Retire of Operates into R31/F31 (suppress side effects)  | IMPLEMENTED | MemDrainer regfile commit | kNoRegWrite sentinel. |
| 2.2.10  | Pipeline Aborts                                          | NOOP     | --                       | V4 doesn't model aborts; faults handled at WB. |

## 2.3 Memory and I/O Accesses

| Section | Requirement                                              | Status      | V4 Location           | Notes |
| ------- | -------------------------------------------------------- | ----------- | --------------------- | ----- |
| 2.3.1   | Memory Space Load Instructions                            | IMPLEMENTED | mBoxLib/grains/LoadStore.cpp | LDL/LDQ/LDQ_U/LDBU/LDWU |
| 2.3.2   | I/O Space Load Instructions (BWX semantics, byte ops)     | PARTIAL     | LoadStore.cpp         | LDBU/LDWU implemented; explicit I/O-space ordering not enforced. |
| 2.3.3   | Memory Space Store Instructions                           | IMPLEMENTED | LoadStore.cpp         | STL/STQ/STQ_U |
| 2.3.4   | I/O Space Store Instructions (BWX, atomic byte/word)      | PARTIAL     | LoadStore.cpp         | STB/STW implemented; I/O ordering not enforced. |

## 2.4 Replay Traps

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 2.4.1   | Mbox Order Traps                                          | NOOP   | --          | V4 doesn't speculate or reorder; no replay needed. |
| 2.4.2   | Other Mbox Replay Traps                                   | NOOP   | --          | Same. |

## 2.5 Software-Directed Prefetching and Loads into R31/F31

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 2.5.1   | Normal Prefetch: LDL/LDF/LDG/LDB/LDW into R31/F31         | TODO   | --          | Currently issues a real load and suppresses commit; spec wants prefetch hint with no fault delivery. |
| 2.5.2   | Prefetch with Modify Intent: LDS into R31/F31             | TODO   | --          | Same shape. |
| 2.5.3   | Prefetch, Evict Next: LDQ into R31                        | TODO   | --          | Same. |
| 2.5.4   | Prefetch, No Reuse: LDT into F31                          | TODO   | --          | Same. |

## 2.6 Special Cases

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 2.6.1   | Load Hit Speculation                                      | NOOP   | --          | No speculation in V4. |
| 2.6.2   | Floating Point Stores                                     | IMPLEMENTED | fBoxLib/grains/Float.cpp | STS/STT/STF/STG land 2026-05-19. |
| 2.6.3   | CMOV                                                      | IMPLEMENTED | eBoxLib/grains/ | All CMOVcc variants in dispatch table. |

## 2.7 Instruction Issue Rules

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 2.7.1   | Instruction Class Definitions                             | NOOP   | --          | V4 issues serially; classes are categorisation only. |
| 2.7.2   | Ebox Slotting                                             | NOOP   | --          | No slotting needed in serial issue. |
| 2.7.3   | Instruction Latencies                                     | NOOP   | --          | Functional emulator; no latency modelling. |

---

# Chapter 3: External Interface

## 3.1 Address Spaces

| Section | Requirement                                              | Status      | V4 Location          | Notes |
| ------- | -------------------------------------------------------- | ----------- | -------------------- | ----- |
| 3.1.1   | I/O Ordering and Merge Rules                              | PARTIAL     | mmuLib/Ev6Translator | Translator dispatches to MMIO hooks; merge semantics not modelled. |

## 3.2 Cache Organization and Coherence

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 3.2.1   | Cache Block States                                        | NOOP   | --          | No cache modelling. |
| 3.2.2   | Cache Block State Transitions                             | NOOP   | --          | Same. |
| 3.2.3   | System Knowledge of Bcache Contents                       | NOOP   | --          | No Bcache. |
| 3.2.4   | Dcache States + Duplicate Tags                            | NOOP   | --          | Same. |
| 3.2.5   | Memory Barrier (MB/WMB/TBfill flow)                       | TODO   | --          | MB / WMB currently no-ops; emulator is single-threaded so weak ordering is fine, but the IPR-write fence semantics may matter. |
| 3.2.6   | Load/Locked and Store/Conditional                         | IMPLEMENTED | memoryLib/LockMonitor + MemDrainer | LDL_L/LDQ_L set reservation; STL_C/STQ_C check + clear with success indicator. |

## 3.3 System Port

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 3.3.x   | System Port pins, commands, data movement, ECC, ordering, clocking (all sub-sections) | NOOP | -- | No real bus pins; V4 dispatches memory access via GuestMemory + chipset MMIO hooks. |

## 3.4 Bcache Port

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 3.4.x   | Bcache pins, banking, transactions, clocking             | NOOP   | --          | No Bcache. |

## 3.5 Interrupts

| Section | Requirement                                              | Status      | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | ----------- | ------------------------ | ----- |
| 3.5     | Interrupt delivery via irq_h[0..5]                        | PARTIAL     | systemLib/Machine.cpp    | Phase D IER gating landed; interval-timer divert wired; other IRQ sources (IPI, perf counter) untested. |

## 3.6 Pin List

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 3.6     | Pin list / package pin assignments                        | NOOP   | --          | No pin modelling. |

---

# Chapter 4: PALcode

## 4.1 Use of Alpha Implementation-Specific Opcodes

| Section | Requirement                                              | Status      | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | ----------- | ------------------------ | ----- |
| 4.1.1   | HW_LD instruction: physical/virtual/lock/quad/long variants; alignment trap suppression | IMPLEMENTED | mBoxLib::execHwLd | Bit-level type field tested in test_mmio_csc_roundtrip. |
| 4.1.2   | HW_ST instruction: physical/conditional variants          | IMPLEMENTED | mBoxLib::execHwSt | Counterpart to HW_LD; STC variant for conditional. |
| 4.1.3   | HW_RET instruction: return from PAL with STACKED or computed target; bit 0 = palMode | IMPLEMENTED | palBoxLib::execHwRei | Wave 2 added setPalMode() shadow-swap on transition. |
| 4.1.4   | HW_MFPR / HW_MTPR instructions: read/write IPRs           | IMPLEMENTED | palBoxLib HW_MFPR/HW_MTPR | Per-IPR dispatch; coverage tracked in Ch 5 table. |

## 4.2 Internal Processor Register Access Mechanisms

| Section | Requirement                                              | Status      | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | ----------- | ------------------------ | ----- |
| 4.2.1   | IPR Scoreboard Bits                                       | NOOP        | --                       | No scoreboard hardware modelled; ordering guaranteed by serial issue. |
| 4.2.2   | Hardware Structure of Explicitly Written IPRs             | NOOP        | --                       | Direct field assignment in HW_MTPR cases. |
| 4.2.3   | Hardware Structure of Implicitly Written IPRs             | PARTIAL     | --                       | Some IPRs (mm_stat, isum) written by drainer/trap path; others not. |
| 4.2.4   | IPR Access Ordering                                       | NOOP        | --                       | Serial issue. |
| 4.2.5   | IPRs and HW_RET Stalls                                    | NOOP        | --                       | No stall modelling. |

## 4.3 PAL Shadow Registers

| Section | Requirement                                              | Status      | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | ----------- | ------------------------ | ----- |
| 4.3     | PAL shadow swap of R4-R7 + R20-R23 on PAL mode transition, gated by I_CTL[SDE] | IMPLEMENTED | coreLib/PalShadow.h | Wave 2 2026-05-19; verified via trace. |

## 4.4 PALcode Emulation of FPCR

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 4.4.1   | FPCR Status Flags semantics                               | TODO   | --          | FPCR not wired; deferred until firmware exercises FP exceptions. |
| 4.4.2   | MF_FPCR                                                   | TODO   | --          | Not implemented. |
| 4.4.3   | MT_FPCR                                                   | TODO   | --          | Not implemented. |

## 4.5 PALcode Entry Points

| Section | Requirement                                              | Status      | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | ----------- | ------------------------ | ----- |
| 4.5.1   | CALL_PAL entry: palBase + 0x2000 + 64*func (priv); palBase + 0x3000 + 64*(func&0x3F) (unpriv) | IMPLEMENTED | coreLib/Ev6EntryVectors.h | computeCallPalEntry, static_assert pinned. |
| 4.5.2   | PALcode Exception Entry Points (palBase + 0x000..0x780)   | PARTIAL     | --                       | OPCDEC (palBase+0x400) tested; INTERRUPT (palBase+0x100) tested; other vectors untested.  Per-vector enumeration in subtable 4.5.2.x below. |

### 4.5.1.x -- Per-CALL_PAL-Function Dispatch (Non-Auth V1: D:\EmulatR\EmulatRAppUni\palLib_ev6\Pal_Service.h)

V1 has 85+ explicit CALL_PAL handler leaves; V4 routes most through the generic `execCallPalDispatch` (which computes the entry PC and lets firmware PAL bytes run).  Status definitions:

- **IMPLEMENTED**: V4 has a hand-written leaf overriding the generic dispatch.
- **DISPATCH-ONLY**: V4 routes to firmware via `execCallPalDispatch`.  Behaviour depends on firmware-loaded PAL bytes at palBase+entry.  This is fine for SRM where PAL code is decompressed; surfaces as a real gap if the firmware expects emulator-intrinsic handling.
- **TODO**: V4 routes to dispatch but V1 had a hand-written intrinsic that V4 should match.

#### Privileged CALL_PAL functions (func 0x00-0x3F)

| Func | Name        | V4 Status     | V4 Location                | V1 Ref (Pal_Service.h executeXXX) | Notes |
| ---- | ----------- | ------------- | -------------------------- | --------------------------------- | ----- |
| 0x00 | HALT        | IMPLEMENTED   | palBox::execHalt           | executeHALT                       | Stops the simulator cleanly.  HRM-correct. |
| 0x01 | CFLUSH      | DISPATCH-ONLY | execCallPalDispatch        | executeCFLUSH                     | Cache flush; V4 has no cache.  Could be NOOP intrinsic. |
| 0x02 | DRAINA      | DISPATCH-ONLY | execCallPalDispatch        | executeDRAINA                     | Drain abort; V4 serial issue makes it NOOP.  Could be intrinsic. |
| 0x03 | LDQP        | IMPLEMENTED   | palBox::execLdqp           | executeLDQP                       | Physical-mode quadword load. |
| 0x04 | STQP        | IMPLEMENTED   | palBox::execStqp           | executeStqp                       | Physical-mode quadword store. |
| 0x05 | SWPCTX      | IMPLEMENTED   | palBox::execSwpctxOsf/Vms  | executeSWPCTX                     | Process-context swap; OSF + VMS variants. |
| 0x06 | MFPR_ASN    | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_ASN                   | Address Space Number read; firmware-managed via PAL_TEMP. |
| 0x07 | MTPR_ASTEN  | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_ASTEN                 | AST enable write. |
| 0x08 | MTPR_ASTSR  | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_ASTSR                 | AST summary register write. |
| 0x09 | CSERVE      | IMPLEMENTED   | palBox::execCserve         | executeCSERVE                     | Console service intrinsic; CALL_PAL inline-executed (no PAL entry). |
| 0x0A | SWPPAL      | DISPATCH-ONLY | execCallPalDispatch        | executeSWPPAL                     | Swap PAL personality. |
| 0x0B | MFPR_FEN    | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_FEN                   | FP enable read. |
| 0x0C | MTPR_FEN    | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_FEN                   | FP enable write. |
| 0x0D | MTPR_IPIR   | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_IPIR                  | Interprocessor interrupt request. |
| 0x0E | MFPR_IPL    | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_IPL                   | IPL read (PALcode-managed). |
| 0x0E | MTPR_IPL    | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_IPL                   | IPL write (PALcode-managed); paired with HW_IER for our Phase D gating. |
| 0x10 | MFPR_MCES   | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_MCES                  | Machine check error summary read. |
| 0x11 | MTPR_MCES   | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_MCES                  | Machine check error summary write. |
| 0x12 | MFPR_PCBB   | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_PCBB                  | Process Control Block Base read. |
| 0x13 | MFPR_PRBR   | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_PRBR                  | Processor Block Base read. |
| 0x14 | MTPR_PRBR   | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_PRBR                  | Same write. |
| 0x15 | MFPR_PTBR   | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_PTBR                  | Page Table Base Register read. |
| 0x16 | MFPR_SCBB   | IMPLEMENTED   | palBox::execMfprScbb       | executeMFPR_SCBB                  | System Control Block Base read. |
| 0x17 | MTPR_SCBB   | IMPLEMENTED   | palBox::execMtprScbb       | executeMTPR_SCBB                  | System Control Block Base write. |
| 0x18 | MTPR_SIRR   | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_SIRR                  | Software interrupt request register write. |
| 0x19 | MFPR_SISR   | DISPATCH-ONLY | execCallPalDispatch        | (not in V1 list)                  | Software interrupt summary read. |
| 0x1A | MFPR_TBCHK  | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_TBCHK                 | TB check; firmware-managed. |
| 0x1B | MTPR_TBIA   | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_TBIA                  | TB invalidate all. |
| 0x1C | MTPR_TBIAP  | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_TBIAP                 | TB invalidate process. |
| 0x1D | MTPR_TBIS   | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_TBIS                  | TB invalidate single. |
| 0x1E | MFPR_ESP    | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_ESP                   | Executive stack pointer read. |
| 0x1F | MTPR_ESP    | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_ESP                   | ESP write. |
| 0x20 | MFPR_SSP    | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_SSP                   | Supervisor stack pointer read. |
| 0x21 | MTPR_SSP    | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_SSP                   | SSP write. |
| 0x22 | MFPR_USP    | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_USP                   | User stack pointer read. |
| 0x23 | MTPR_USP    | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_USP                   | USP write. |
| 0x24 | MTPR_TBISD  | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_TBISD                 | TB invalidate single data. |
| 0x25 | MTPR_TBISI  | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_TBISI                 | TB invalidate single instruction. |
| 0x26 | MFPR_ASTEN  | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_ASTEN                 | AST enable read. |
| 0x27 | MFPR_ASTSR  | DISPATCH-ONLY | execCallPalDispatch        | executeMFPR_ASTSR                 | AST summary read. |
| 0x29 | MFPR_VPTB   | IMPLEMENTED   | palBox::execMfprVptb       | executeMFPR_VPTB                  | Virtual page table base read. |
| 0x2A | MTPR_VPTB   | IMPLEMENTED   | palBox::execMtprVptb       | executeMTPR_VPTB                  | Virtual page table base write. |
| 0x2B | MTPR_PERFMON| DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_PERFMON               | Performance counter write. |
| 0x2E | MTPR_DATFX  | DISPATCH-ONLY | execCallPalDispatch        | executeMTPR_DATFX                 | Data alignment fixup register write. |
| 0x3F | MFPR_WHAMI  | IMPLEMENTED   | palBox::execMfprWhami      | (intrinsic, not in V1 PAL_TEMP list) | CPU identity read. |
| 0xFF | KBPT        | DISPATCH-ONLY | execCallPalDispatch        | executeKBPT                       | Kernel breakpoint. |

#### Unprivileged CALL_PAL functions (func 0x80-0xBF)

| Func | Name           | V4 Status     | V4 Location                | V1 Ref                            | Notes |
| ---- | -------------- | ------------- | -------------------------- | --------------------------------- | ----- |
| 0x80 | BPT            | IMPLEMENTED (stub) | palBox::execBpt_tru64/vms | executeBPT               | Breakpoint trap; V4 returns kFaultUnimplemented stub.  HRM: should enter PAL handler for OS debugger. |
| 0x81 | BUGCHECK       | DISPATCH-ONLY | execCallPalDispatch        | executeBUGCHK                     | OS bug check. |
| 0x82 | CHME           | IMPLEMENTED (stub) | palBox::execChme_vms     | executeCHME                       | Change Mode to Executive; VMS-only.  V4 stub. |
| 0x83 | CHMK           | IMPLEMENTED (stub) | palBox::execChmk_tru64   | executeCHMK                       | Change Mode to Kernel; OSF/Tru64.  V4 stub. |
| 0x84 | CHMS           | DISPATCH-ONLY | execCallPalDispatch        | executeCHMS                       | Change Mode to Supervisor. |
| 0x85 | CHMU           | DISPATCH-ONLY | execCallPalDispatch        | executeCHMU                       | Change Mode to User. |
| 0x86 | IMB            | DISPATCH-ONLY | execCallPalDispatch        | executeIMB                        | Instruction memory barrier; could be NOOP intrinsic. |
| 0x87 | INSQHIL        | DISPATCH-ONLY | execCallPalDispatch        | executeINSQHIL                    | Queue: insert head interlocked longword. |
| 0x88 | INSQTIL        | DISPATCH-ONLY | execCallPalDispatch        | executeINSQTIL                    | Queue ops; V1 has explicit implementations. |
| 0x89 | INSQHIQ        | DISPATCH-ONLY | execCallPalDispatch        | executeINSQHIQ                    | -- |
| 0x8A | INSQTIQ        | DISPATCH-ONLY | execCallPalDispatch        | executeINSQTIQ                    | -- |
| 0x8B | INSQUEL        | DISPATCH-ONLY | execCallPalDispatch        | executeINSQUEL                    | -- |
| 0x8C | INSQUEQ        | DISPATCH-ONLY | execCallPalDispatch        | executeINSQUEQ                    | -- |
| 0x8D | INSQUEL_D      | DISPATCH-ONLY | execCallPalDispatch        | executeINSQUEL_D                  | -- |
| 0x8E | INSQUEQ_D      | DISPATCH-ONLY | execCallPalDispatch        | executeINSQUEQ_D                  | -- |
| 0x8F | INSQHILR       | DISPATCH-ONLY | execCallPalDispatch        | executeINSQHILR                   | -- |
| 0x90 | INSQTILR       | DISPATCH-ONLY | execCallPalDispatch        | executeINSQTILR                   | -- |
| 0x91 | INSQHIQR       | DISPATCH-ONLY | execCallPalDispatch        | executeINSQHIQR                   | -- |
| 0x92 | INSQTIQR       | DISPATCH-ONLY | execCallPalDispatch        | executeINSQTIQR                   | -- |
| 0x93 | PROBER         | DISPATCH-ONLY | execCallPalDispatch        | executePROBER                     | Probe read access. |
| 0x94 | PROBEW         | DISPATCH-ONLY | execCallPalDispatch        | executePROBEW                     | Probe write access. |
| 0x95 | RD_PS          | DISPATCH-ONLY | execCallPalDispatch        | executeRD_PS                      | Read processor status. |
| 0x96 | REI / RTI      | DISPATCH-ONLY | execCallPalDispatch        | executeRTI                        | Return from interrupt (V4 has HW_REI for the hardware-form; CALL_PAL form needed for unpriv use). |
| 0x97 | REMQHIL        | DISPATCH-ONLY | execCallPalDispatch        | executeREMQHIL                    | Queue: remove head interlocked. |
| 0x98 | REMQTIL        | DISPATCH-ONLY | execCallPalDispatch        | executeREMQTIL                    | -- |
| 0x99 | REMQHIQ        | DISPATCH-ONLY | execCallPalDispatch        | executeREMQHIQ                    | -- |
| 0x9A | REMQTIQ        | DISPATCH-ONLY | execCallPalDispatch        | executeREMQTIQ                    | -- |
| 0x9B | REMQUEL        | DISPATCH-ONLY | execCallPalDispatch        | executeREMQUEL                    | -- |
| 0x9C | REMQUEQ        | DISPATCH-ONLY | execCallPalDispatch        | executeREMQUEQ                    | -- |
| 0x9D | REMQUEL_D      | DISPATCH-ONLY | execCallPalDispatch        | executeREMQUEL_D                  | -- |
| 0x9E | REMQUEQ_D      | DISPATCH-ONLY | execCallPalDispatch        | executeREMQUEQ_D                  | -- |
| 0x9F | REMQHILR       | DISPATCH-ONLY | execCallPalDispatch        | executeREMQHILR                   | -- |
| 0xA0 | REMQTILR       | DISPATCH-ONLY | execCallPalDispatch        | executeREMQTILR                   | -- |
| 0xA1 | REMHIQR        | DISPATCH-ONLY | execCallPalDispatch        | executeREMHIQR                    | -- |
| 0xA2 | REMQTIQR       | DISPATCH-ONLY | execCallPalDispatch        | executeREMQTIQR                   | -- |
| 0xA3 | SWASTEN        | DISPATCH-ONLY | execCallPalDispatch        | executeSWASTEN                    | Swap AST enable. |
| 0xA4 | WR_PS_SW       | DISPATCH-ONLY | execCallPalDispatch        | executeWR_PS_SW                   | Write processor status software bits. |
| 0xA5 | RSCC           | DISPATCH-ONLY | execCallPalDispatch        | executeRSCC                       | Read system cycle counter. |
| 0xA6 | READ_UNQ       | DISPATCH-ONLY | execCallPalDispatch        | executeREAD_UNQ                   | Read unique field. |
| 0xA7 | WRITE_UNQ      | DISPATCH-ONLY | execCallPalDispatch        | executeWRITE_UNQ                  | Write unique field. |
| 0xA8 | AMOVRR         | DISPATCH-ONLY | execCallPalDispatch        | executeAMOVRR                     | Atomic move register-to-register. |
| 0xA9 | AMOVRM         | DISPATCH-ONLY | execCallPalDispatch        | executeAMOVRM                     | Atomic move register-to-memory. |
| 0xAA | GENTRAP        | DISPATCH-ONLY | execCallPalDispatch        | executeGENTRAP                    | Generate trap. |
| 0xAE | CLRFEN         | DISPATCH-ONLY | execCallPalDispatch        | executeCLRFEN                     | Clear FEN (FP enable). |
| 0xAB | WTINT          | IMPLEMENTED   | palBox::execWtint          | executeWTINT                      | Wait for interrupt. |

## 4.6 TB Fill Flows

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 4.6.1   | DTB Fill flow (hw fill from PTE write)                    | TODO   | --          | No translation buffer modelling; PA = VA in PAL mode shortcircuit. |
| 4.6.2   | ITB Fill flow                                             | TODO   | --          | Same. |

---

# Chapter 5: Internal Processor Registers

Per-IPR audit.  V4 source-of-truth: `coreLib/HW_IPR.h` enum + handler
cases in `palBoxLib/grains/PalEntries.cpp` execHwMfpr / execHwMtpr.

## 5.1 Ebox IPRs

| Section | IPR        | Status      | V4 Field / Handler           | Notes |
| ------- | ---------- | ----------- | ---------------------------- | ----- |
| 5.1.1   | CC (cycle counter)                  | IMPLEMENTED | cpu.cycleCount + ccOffset    | HW_MFPR returns uint32; HW_MTPR sets offset. |
| 5.1.2   | CC_CTL                              | TODO        | --                           | CC enable bit not modelled. |
| 5.1.3   | VA                                  | TODO        | --                           | Mbox fault VA staging not separate from mm_stat.  V1 ref: D:\EmulatR\EmulatRAppUni\coreLib (search execMfprVa); stores fault VA distinct from mm_stat. |
| 5.1.4   | VA_FORM                             | TODO        | --                           | Computed VA-form not implemented.  V1 ref: D:\EmulatR\EmulatRAppUni\coreLib\global_RegisterMaster_hot.h::computeVAForm() (counterpart to computeIVAForm at line ~1188).  Same formula as IVA_FORM but uses VA register instead of EXC_ADDR. |
| 5.1.5   | VA_CTL                              | PARTIAL     | cpu.va_ctl + coreLib/VA_types.h::vaCtl* | Field stored on CpuState; bit accessors added 2026-05-19 (vaCtlIsVa48, vaCtlIsVa43, vaCtlIsBigEndian, vaCtlIsVaForm32, vaCtlVptb).  Bit layout HRM-verified per Section 5.1.5: bit 0=B_ENDIAN, bit 1=VA_48, bit 2=VA_FORM_32, bits [63:30]=VPTB.  Benign divergence: HRM says VA_CTL is write-only (reads UNPREDICTABLE); V4 returns stored value.  **NOT-BENIGN DIVERGENCE**: mmuLib/Ev6Translator.h::translateData treats `(va_ctl & 0x2) == 0` as "physical addressing mode" and identity-maps VA->PA in that case.  Per HRM, VA_48 controls VA FORMAT (43-bit vs 48-bit), not phys-vs-virt addressing.  This is a V4 hack that lets the firmware boot in pseudo-physical mode without TLB; it shadows the actual SPE / TLB requirement.  When firmware sets VA_48 (or any test exercises 48-bit mode without proper SPE setup), the shortcut disappears and TLB miss path is required.  Audit: replace this hack with SPE-based superpage detection + DTB walker before declaring full IPR support. |

## 5.2 Ibox IPRs

| Section | IPR        | Status      | V4 Field / Handler           | Notes |
| ------- | ---------- | ----------- | ---------------------------- | ----- |
| 5.2.1   | ITB_TAG    | TODO (deferred) | --                       | HRM 5.2.1: write-only stage for next ITB fill.  V4 silently ignores writes; works because translator shortcuts via VA_CTL hack (see row 5.1.5+ note).  When TLB walker lands, ITB_TAG becomes a staging buffer read by execHwMtpr_ITB_PTE.  V1 ref: pal_service.h HW_ITB_TAG case stores into m_iprGlobalMaster->x->itb_tag for use by the paired ITB_PTE write. |
| 5.2.2   | ITB_PTE    | TODO (deferred) | --                       | HRM 5.2.2: write triggers ITB fill; ITB_TAG + ITB_PTE -> ITB entry via round-robin replacement.  V4 silently ignores; firmware boot to 5.3M cycles never writes HW_ITB_PTE (translator's VA_CTL[VA_48]=0 shortcut keeps the firmware in physical addressing).  Risk surfaces when firmware enables 48-bit VA mode.  V1 ref: D:\EmulatR\EmulatRAppUni\palLib_ev6\pal_service.h HW_ITB_PTE case calls Ev6Translator::fromItbPteRegister + tlbInsert.  Port when TLB walker lands. |
| 5.2.3   | ITB_IAP    | TODO (deferred) | --                       | HRM 5.2.3: invalidate-all-process.  Action with no operand; clears all ITB entries with non-global process bit clear (i.e. process-private mappings).  V4 ignores; same risk window as 5.2.2. |
| 5.2.4   | ITB_IA     | TODO (deferred) | --                       | HRM 5.2.4: invalidate-all.  Clears every ITB entry.  V4 ignores; same risk window. |
| 5.2.5   | ITB_IS     | TODO (deferred) | --                       | HRM 5.2.5: invalidate-single.  Removes ITB entry matching VA (in c.opB).  V4 ignores; same risk window. |
| 5.2.6   | EXC_ADDR   | IMPLEMENTED | cpu.excAddr                  | Set by trap delivery + CALL_PAL; read for HW_REI. |
| 5.2.7   | IVA_FORM   | TODO        | --                           | Computed VA-form for I-stream faults.  V1 ref: D:\EmulatR\EmulatRAppUni\coreLib\global_RegisterMaster_hot.h::computeIVAForm() (line ~1188).  Uses exc_addr + VPTB from I_CTL + VA_48 + form_32 bits.  Three-mode formula: form_32 (19-bit VPN), va_48 (40-bit VPN sign-extended), va_43 default (30-bit VPN).  DEFERRED in practice: V4 has no ITB miss path, so IVA_FORM read returns 0 silently and no consumer notices.  Port when TLB walker lands. |
| 5.2.8   | IER_CM (combined Interrupt Enable + Current Mode) | IMPLEMENTED | palBoxLib::execHwMfpr/Mtpr | HRM 5.2.8-verified 2026-05-19: HW_IER writes IER only; HW_IER_CM writes IER + CM atomically.  **CM is at bits [4:3] of the DATA** (21264 layout; 21164 had CM at [1:0]; the diagram in HRM 5.2.8 confirms via the bit-position labels).  Mask 0x18 = bits 3 and 4.  V1 ref: D:\EmulatR\EmulatRAppUni\palLib_ev6\Pal_Service.h line ~4015-4020 uses the same mask.  Storage: cpu.ier holds IER bits (3,4 cleared); cpu.mode holds CM; HW_IER_CM read OR's mode<<3 into the returned value.  Correction note: initial implementation 2026-05-19 morning had CM at bits [1:0] which is the 21164 layout; corrected to [4:3] when V1 reference surfaced. |
| 5.2.9   | SIRR (Software Interrupt Request)   | TODO        | --                           | Silent-no-op; software interrupts not delivered. |
| 5.2.10  | ISUM (Interrupt Summary, RO)        | IMPLEMENTED | cpu.isum                     | Set by trap delivery; read returns bits. |
| 5.2.11  | HW_INT_CLR                          | NOOP        | --                           | Write-only; silent-no-op. |
| 5.2.12  | EXC_SUM (Exception Summary)         | TODO        | --                           | FP exception flags not populated. |
| 5.2.13  | PAL_BASE                            | IMPLEMENTED | cpu.palBase + coreLib/IprFields.h::palBaseSanitize | HRM 5.2.13-verified.  Bits [43:15] = PAL_BASE physical address; [63:44] and [14:0] are RAZ/MBZ.  HW_MTPR write path applies palBaseSanitize per HRM (added 2026-05-19 after audit cross-check).  CALL_PAL entry vector via coreLib/Ev6EntryVectors.h::computeCallPalEntry applies kPalBaseAlignMask (~0x7FFF) for alignment.  Reset value zero (HRM: "contents are cleared by chip reset").  Step D PAL relocation and SrmLoader reset paths assign palBase directly from firmware metadata; those values are spec-clean by construction. |
| 5.2.14  | I_CTL (Ibox Control)                | PARTIAL     | cpu.i_ctl + coreLib/IprFields.h iCtl* | HRM 5.2.14-verified.  17 field accessors in IprFields.h (kICtl* constants + iCtlSdeLow/High, iCtlCallPalUsesR23, iCtlCallPalLinkageReg, iCtlSpe, iCtlIsVa48, iCtlIsVaForm32, iCtlMchkEn, iCtlHweEnabled, iCtlTbMbEn, iCtlChipId, iCtlVptb).  Consumers: PalShadow.h migrated to iCtlSdeHigh() 2026-05-19; execCallPalDispatch consumes iCtlCallPalLinkageReg() to set R23 or R27 with linkage value (CALL_PAL linkage register bug fix landed 2026-05-19).  PALcode-source ground-truth confirmation: ev6_osf_pal.mar:803 declares `p23 = r23 call_pal linkage register` and lines 7589/7935 explicitly save/restore p23 to/from the PAL context block CNS__P23 offset.  REMAINING TODO: (1) translator should consume iCtlIsVa48()+iCtlSpe() instead of duplicate cpu.va_ctl + cpu.i_spe fields (current Ev6Translator.h:238 VA_CTL[VA_48]=0 hack documented as not-benign in row 5.1.5); (2) SDE<0> shadow set (R8-R11 + R24-R27) not implemented -- only SDE<1> handled; (3) MCHK_EN gate on machine-check delivery not wired (no MCHK delivery in V4 anyway); (4) HWE gate on PALRES instructions not enforced; (5) CHIP_ID not seeded at reset (reads return 0 = "pass 1" by coincidence); (6) VPTB high bits handling needs cross-check when TLB walker lands. |
| 5.2.15  | I_STAT                              | NOOP        | --                           | IBox status; silent-no-op. |
| 5.2.16  | IC_FLUSH                            | NOOP        | --                           | No I-cache; silent action. |
| 5.2.17  | CLR_MAP                             | NOOP        | --                           | No register map. |
| 5.2.18  | SLEEP                               | NOOP        | --                           | No sleep mode. |
| 5.2.20  | Ibox Process Context (PCTX)         | TODO        | --                           | OS context save/restore IPR; not exercised yet. |
| 5.2.21  | PCTR_CTL                            | NOOP        | --                           | Performance counter control. |

## 5.3 Mbox IPRs

| Section | IPR        | Status      | V4 Field / Handler           | Notes |
| ------- | ---------- | ----------- | ---------------------------- | ----- |
| 5.3.1   | DTB_TAG0 / DTB_TAG1                 | NOOP        | --                           | No DTB. |
| 5.3.2   | DTB_PTE0 / DTB_PTE1                 | NOOP        | --                           | Same. |
| 5.3.3   | DTB_ALTMODE                         | NOOP        | --                           | Same. |
| 5.3.4   | DTB_IAP                             | NOOP        | --                           | Same. |
| 5.3.5   | DTB_IA                              | NOOP        | --                           | Same. |
| 5.3.6   | DTB_IS0 / DTB_IS1                   | NOOP        | --                           | Same. |
| 5.3.7   | DTB_ASN0 / DTB_ASN1                 | NOOP        | --                           | Same. |
| 5.3.8   | MM_STAT                             | IMPLEMENTED | cpu.mm_stat                  | Set on translation/memory fault. |
| 5.3.9   | M_CTL                               | IMPLEMENTED | cpu.m_ctl                    | Field stored. |
| 5.3.10  | DC_CTL                              | NOOP        | --                           | No Dcache. |
| 5.3.11  | DC_STAT                             | NOOP        | --                           | No Dcache. |

## 5.4 Cbox CSRs and IPRs

Implemented via Tsunami21274 chipset modelling; the Cbox is partially
absorbed into the chipset rather than tracked as discrete CSRs on
CpuState.

| Section | IPR / CSR                           | Status   | V4 Location                  | Notes |
| ------- | ----------------------------------- | -------- | ---------------------------- | ----- |
| 5.4.1   | Cbox CSR Description (per-CSR)      | PARTIAL  | chipsetLib/TsunamiCchip.h    | Cchip MISC + IIC + CSC modelled (Phase B); other Cbox CSRs absorbed into Tsunami spec. |
| 5.4.2   | Cbox IPR Description (per-IPR)      | TODO     | --                           | Some Cbox IPRs are CPU-side scratch; not yet enumerated. |

---

# Chapter 6: IEEE Floating Point Conformance

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 6.0     | IEEE 754 binary floating-point semantics for all FP ops   | PARTIAL | fBoxLib/grains/Float.cpp | Host-native double arithmetic (no rounding-mode honour). |
| 6.0     | FP exception trap delivery                                | TODO   | --          | Not implemented; firmware boot doesn't yet hit FP traps. |
| 6.1     | FPCR field layout (DZE, INVD, etc.)                       | TODO   | --          | FPCR not stored on CpuState. |
| 6.1     | FPCR field semantics (sticky flags, dynamic rounding)     | TODO   | --          | Same. |

---

# Chapter 7: Error Detection and Handling

All entries below are NOOP for emulation -- V4's GuestMemory is
ECC-clean by construction.  When/if simulator-injected memory errors
become a feature, these become DEFERRED with an implementation note.

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 7.1     | Icache Data or Tag Parity Error                          | NOOP   | --          | No I-cache. |
| 7.2     | Dcache Tag Parity Error                                  | NOOP   | --          | No D-cache. |
| 7.3     | Dcache Data Correctable ECC Error                        | NOOP   | --          | Same. |
| 7.4     | Dcache Triplicate Tag Parity Error                       | NOOP   | --          | Same. |
| 7.5     | Bcache Tag Parity Error                                  | NOOP   | --          | No Bcache. |
| 7.6     | Bcache Data Correctable ECC Error                        | NOOP   | --          | Same. |
| 7.7     | Bcache Data Uncorrectable ECC Error                      | NOOP   | --          | Same. |
| 7.8     | Memory Data Correctable ECC Error                        | NOOP   | --          | No ECC modelling. |
| 7.9     | Memory Data Uncorrectable ECC Error                      | NOOP   | --          | Same. |
| 7.10    | System Port Read Errors                                  | NOOP   | --          | No real system port. |

---

# Chapter 8: Initialization and Test

| Section | Requirement                                              | Status   | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | -------- | ------------------------ | ----- |
| 8       | Reset sequence: CPU comes out of reset with palMode=1, palBase=0, IPL=31 (or equivalent fully-masked state) | PARTIAL | systemLib/Machine.cpp reset() | Reset sets palMode + pc + zero regs; ier=0 default per new field; no formal alignment with spec's exact reset state table yet. |
| 8       | Test pins, scan chain                                    | NOOP     | --                       | No test pin modelling. |

---

# Chapter 9: Electrical Data

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 9       | Electrical / DC / AC characteristics, clocking, power     | NOOP   | --          | Not relevant to software emulation. |

---

# Chapter 10: Packaging

| Section | Requirement                                              | Status | V4 Location | Notes |
| ------- | -------------------------------------------------------- | ------ | ----------- | ----- |
| 10      | Pinout, package dimensions                                | NOOP  | --          | Not relevant. |

---

# Chapter 11: Appendix 1 -- Reset and Sleep Mode

| Section | Requirement                                              | Status   | V4 Location              | Notes |
| ------- | -------------------------------------------------------- | -------- | ------------------------ | ----- |
| 11      | Reset sequence per appendix detail                        | PARTIAL  | Machine.cpp              | Same row as Ch 8 entry; expand once spec text is digested. |
| 11      | Sleep mode entry / exit                                   | NOOP     | --                       | No sleep modelling. |

---

# Chapter 12: Appendix 2 -- PAL Coding Restrictions

These are constraints on PALcode behaviour that the hardware enforces
or relies on.  For an emulator: each row asks "do we either obey the
constraint or check for / mask violations of it?"  Many are
non-applicable because V4 is serial / non-speculative. 

| Section | Restriction Title                                                                    | Status | V4 Location | Notes |
| ------- | ------------------------------------------------------------------------------------ | ------ | ----------- | ----- |
| 12.1    | Reset Sequence Required by Retirator and Mapper                                       | NOOP   | --          | No retirator/mapper modelling. |
| 12.2    | No Multiple Writers to IPRs in Same Scoreboard Group                                  | NOOP   | --          | No scoreboard. |
| 12.3    | (removed in spec)                                                                     | NOOP   | --          | --    |
| 12.4    | No Writers and Readers to IPRs in Same Scoreboard Group                               | NOOP   | --          | Same. |
| 12.5    | PAL shadow enables: software must NOT modify SDE from inside PAL mode                 | TODO   | --          | Detect/assert when firmware writes I_CTL with new SDE while palMode=1. |
| 12.6    | Avoid Consecutive read-modify-write-read-modify-write to IPRs in same scoreboard grp  | NOOP   | --          | Same as 12.2/12.4. |
| 12.7    | Replay trap + interrupt code sequence + STF/ITOF                                      | NOOP   | --          | No replay trap modelling. |
| 12.8    | (removed in spec)                                                                     | NOOP   | --          | --    |
| 12.9    | PALmode I-Stream address ranges                                                       | TODO   | --          | Verify Ibox fetch addressing rules in PAL mode (we currently allow any PA). |
| 12.10   | Duplicate IPR mode bits                                                               | TODO   | --          | Some IPRs duplicate bits across read/write paths; verify no inconsistencies in CpuState representation. |
| 12.11   | Ibox IPR update synchronisation                                                       | NOOP   | --          | Serial issue makes this moot. |
| 12.12   | HW_MFPR EXC_ADDR / IVA_FORM / EXC_SUM Usage                                           | TODO   | --          | Verify these reads only occur in expected contexts; document allowed/forbidden combos. |
| 12.13   | DTB FILL flow collision                                                               | NOOP   | --          | No DTB. |
| 12.14   | HW_RET                                                                                | TODO   | --          | Check HW_RET delay rules in PalEntries.cpp execHwRei. |
| 12.15   | (REMOVED in spec)                                                                     | NOOP   | --          | --    |
| 12.16   | JSR-BAD VA                                                                            | NOOP   | --          | No JSR speculation. |
| 12.17   | MTPR to DTB_TAG0/DTB_PTE0/DTB_TAG1/DTB_PTE1                                           | NOOP   | --          | No DTB. |
| 12.18   | No FP OPERATES or FP CONDITIONAL BRANCHES in same fetch block as MTPR                 | NOOP   | --          | Serial issue; one instruction at a time. |
| 12.19   | HW_RET/STALL after updating the FPCR via MT_FPCR in PALmode                           | NOOP   | --          | FPCR not yet wired; revisit when 4.4 lands. |
| 12.20   | I_CTL SBE Stream Buffer Enable guideline                                              | NOOP   | --          | No stream buffer. |
| 12.21   | HW_RET/STALL after MT ASN0/ASN1                                                       | NOOP   | --          | No ASN. |
| 12.22   | HW_RET/STALL after MT IS0/IS1                                                         | NOOP   | --          | No IS register modelling. |
| 12.23   | HW_ST IP/CONDITIONAL does not clear the lock flag                                     | TODO   | --          | Verify STC failure path leaves cpu.hasReservation alone vs clearing it. |
| 12.24   | HW_RET/STALL after MT ITB_IA, ITB_IAP, IC_FLUSH                                       | NOOP   | --          | No ITB. |
| 12.25   | MT ITB_IA after Reset                                                                 | NOOP   | --          | Same. |
| 12.26   | Conditional branches in PALcode guideline                                             | NOOP   | --          | V4 doesn't predict branches. |
| 12.27   | Reset of 'Force-Fail Lock Flag' State in PALcode                                      | TODO   | --          | Verify reset path clears reservation state in LockMonitor + cpu fields. |

---

# Change history

- 2026-05-19  scaffold (this commit).  Status profile is first-pass
  estimate before code audit.  Rows mostly TODO except where
  recently-landed work (Wave 1 / Wave 2 / Phase D) was explicit.

# Next-session audit pass

Recommended first audit pass: row-by-row through Chapter 5 (IPR
table) using `palBoxLib/grains/PalEntries.cpp` execHwMfpr and
execHwMtpr switch statements as ground-truth.  Each `case` arm there
maps to one or more rows here.  Migrate the silent-no-op blocks'
entries to NOOP (not TODO) where appropriate; promote known-
implemented IPRs to IMPLEMENTED with file:line citation.

Second pass: Chapter 12 PAL coding restrictions.  Each TODO row gets
either a concrete check (assert / log / mask) or a documented "we
trust the firmware to obey" rationale promoting it to NOOP.
