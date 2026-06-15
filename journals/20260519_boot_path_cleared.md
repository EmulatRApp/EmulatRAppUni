# Boot path cleared -- Wave 1 + Wave 2 + Phase D landed 2026-05-19

Single-day milestone.  ES45 v7.3 firmware now runs past 20M cycles in
native SRM Console code without halting.  Three architectural fixes
landed in sequence after a deep diagnostic morning.

This journal is the source-of-truth for what's true at end of day.
Tim should drop a condensed version into auto-memory as
`project_boot_path_cleared_20260519.md`, replacing or superseding the
earlier entries that pointed at the wrong diagnoses
(`project_decompressor_pal_overlap_confirmed.md` interpretation was
half-right; the "spin loop" was the OPCDEC handler at palBase+0x400,
not a decompressor).

## End-of-day boot status

```
cycles      = 20,000,000  (MaxCyclesExceeded; no halt)
PC          = 0x600924    (native SRM Console code)
palMode     = false
lastFault   = kNoFault
i_ctl       = 0x1080      (SDE[1] set, IC_EN set)
palBase     = 0x600000
```

Firmware is happily in its SRM main loop, processing data, evolving
registers run-over-run.  Previously was halting at cyc 5,242,886.

## What landed today

### Wave 1: FP load/store family

Eight new opcodes (0x20..0x27): LDF / LDG / LDS / LDT / STF / STG /
STS / STT.

-   TSV: 8 new rows in `grainFactoryLib/GrainMasterV4.tsv` with
    appropriate flags (`S_MemFormat | S_ReadsRb | S_WritesRa |
    S_ReadsInt | S_WritesFp | S_Load` for loads; corresponding store
    rows; VAX-format loads/stores carry `S_VaxFp`).
-   Eight hand-written leaves in `fBoxLib/grains/Float.cpp` packing
    `memAddr / memSize / regWriteIsFp / memData` per the V4 BoxResult
    contract.  Loads delegate format conversion to the drainer; stores
    apply inverse conversion in-leaf.
-   New `fBoxLib/grains/FpFormat.h` with four conversion-pair helpers
    (S, T, F, G memory <-> 64-bit register) ported verbatim from V1's
    `convertS_FloatingToRegister` family.
-   `pipelineLib/MemDrainer.h` `signExtendLoadFill` renamed to
    `formatLoadValue` with a `semFlags` parameter; dispatches on
    `S_VaxFp | memSize` to call the correct `convertX_FloatingToRegister`
    helper at load time.
-   `handwritten.tsv` extended with the 8 new symbols so codegen skips
    them.  Codegen re-ran clean: 156 hand-written leaves skipped (up
    from 148).

Direct cause for needing this wave: SRM PAL handler at palBase+0x2A40
(MFPR_VPTB dispatch target) executes `STS F20, -1629(R26)` as its
second instruction (FP spill before scratch use).  V4 was emitting
OPCDEC.

### Wave 1 follow-up: Mem-format Rb regfile fix

`pipelineLib/PipelineDriver.h` had a latent bug: ExecCtx setup used
`useFp = S_ReadsFp` for BOTH opA and opB.  For STS / STT / STF / STG
where Ra is FP but Rb is the integer EA base, this routed opB through
fpReg (= 0) producing VAs that were `disp` alone instead of
`R[Rb] + disp`.

Fix: gate Rb on `(useFp && !isMemFormat)`.  Mem-format always reads
Rb from intReg regardless of FP involvement on Ra.

### Wave 2: PAL shadow registers (R4-R7 + R20-R23)

EV6 swaps 8 registers (R4-R7 + R20-R23) with PAL shadow copies on
every transition into/out of PAL mode, gated by `I_CTL[SDE]` (bit 7).
Source: Alpha 21264/EV6 HRM Section 6.6.  Trace shows i_ctl=0x1080
on cold boot, so SDE is enabled.

-   `coreLib/CpuState.h` gains `intShadow[8]` (R4-R7 -> shadow[0..3],
    R20-R23 -> shadow[4..7]).
-   New `coreLib/PalShadow.h` with `kSdeBit` constant + helpers
    `swapPalShadowRegs`, `sdeEnabled`, `palModeEnter`, `palModeLeave`,
    `setPalMode`.
-   Three transition sites patched:
    -   `Machine.cpp::stageInterruptDivert` (interval-timer + injection)
    -   `palBoxLib::execCallPalDispatch` (CALL_PAL retire)
    -   `palBoxLib::execHwRei` (HW_REI exit; uses `setPalMode` since
        target depends on resume PC's low bit)
-   Reset path left direct (intReg and intShadow both zero, no swap
    meaningful).

Wave 2 is foundationally correct for any PAL handler touching those 8
registers; not the direct cause of today's specific HALT (R17 was the
gating reg and it's not in the EV6 swap set).

### Phase D: HW_IER tracking + IER-bit gating in canAcceptInterrupt

The actual cause of the cyc 5.24M HALT: interval timer was firing
before the firmware had enabled it.  AXPBox's cross-check
(`System.cpp::interrupt` + `AlphaCPU.cpp` line 469) gates interrupt
taking on `(state.eien & state.eir)` -- per-source enable register
must explicitly authorize the source.  Alpha 21264 equivalent is
HW_IER (IPR `0x010A`).  Firmware writes via HW_MTPR HW_IER enable
specific sources after handler infrastructure is online.

-   `CpuState::ier` field added (uint64_t, reset 0 = all masked).
-   `palBoxLib::execHwMfpr` HW_IER and HW_IER_CM read return `cpu.ier`.
-   `palBoxLib::execHwMtpr` HW_IER and HW_IER_CM store `c.opB` into
    `cpu.ier`.  Both removed from the prior silent-no-op blocks.
-   `Machine::canAcceptInterrupt(irqLevel)` extended: maps `irqLevel`
    in [19..30] onto IER bits 33..38 via `(57 - irqLevel)`, gates on
    `(cpu.ier & bit) != 0`.  Falls through to "accepted" for IRQ
    levels outside the chipset EI range (software interrupts at IPL
    1..14 etc. -- their own gates will land in a follow-up phase).
-   IRQ-to-IPL map matches Alpha 21264 HRM Section 5.4 (errors=30,
    device=23, interval timer=22, IPI=21, PMC0=20, PMC1=19).

### Diagnostic facilities (independent of the boot fixes)

Two new compile-time-gated subsystems landed under their own CMake
options, both ON by default for non-Release builds:

-   `coreLib/LogSubsystem.h` + `.cpp` -- per-subsystem on/off + throttle
    + optional file sink for 8 diagnostic streams (Cbox, Unalign,
    IntervalTimer, Snapshot, PalRelocation, ChipsetCsr, StepD, Misc).
    Macros `LOG_SUBSYS` and `LOG_SUBSYS_THROTTLED` collapse to
    `((void)0)` when `EMULATR_DIAGNOSTIC_LOGGING=0`.  CLI flags
    `--log-disable`, `--log-only`, `--log-verbose`, `--log-file`.
    Call-site refactor of existing log sites still pending (task #31).
-   `traceLib/PaDump.h` + `.cpp` -- physical-address disassembly dump.
    CLI flag `--dump-disasm <pa>:<count>`.  Fires after firmware load
    and snapshot autoload but before run.  Used today to confirm the
    STS bytes at 0x602a44, the byte layout at the CALL_PAL handler at
    0x600100, the OPCDEC handler at 0x600400.
-   Two CMake options: `EMULATR_DIAGNOSTIC_LOGGING` and
    `EMULATR_PA_DUMP`, mirroring the existing `EMULATR_CHIPSET_DIAG`
    pattern (ON for Debug + RelWithDebInfo, forced OFF in Release).
-   Operator-facing reference doc at
    `tools/diagnostics_usage.md` (8 sections covering CLI shape,
    compile-time gates, troubleshooting, two-step snapshot-then-dump
    workflow).

### Misdiagnosis corrections from the morning

Three earlier-today hypotheses turned out wrong; the journals capture
the corrected pictures:

-   `journals/20260519_decompressor_pal_overlap_findings.md` --
    initially we thought the loop at 0x6003ec was a pass-2 decompressor
    overwriting its own code.  Correct: it's the OPCDEC handler at
    palBase+0x400 entered because the firmware OPCDEC'd on
    unimplemented STS.
-   `journals/20260519_opcdec_handler_loop_finding.md` -- corrected
    interpretation: spin loop = OPCDEC handler running with wrong
    register state because of (a) missing STS opcode and (b) missing
    PAL shadow swap.  Wave 1 + Wave 2 + Phase D all needed.
-   Earlier memory `project_decompressor_pal_overlap_confirmed.md`
    was directionally pointing at task #20 / #24 which we now
    understand as misdiagnosis -- the bytes at 0x6003ec ARE the
    OPCDEC handler, not a decompressor.  R0/R1/R2 carrying "stale"
    values is a CONSEQUENCE of missing shadow swap, not a
    decompressor bug.

### Open / next-session items

-   Trace from this 20M-cycle run captures cold-boot -> SRM main loop
    transition; would be useful to identify when firmware writes
    HW_IER and which bits.  Once we know which bits firmware enables
    first, we can validate that the IER gating bit assignment
    `(57 - irqLevel)` matches firmware expectations.
-   Eventually firmware will enable interval timer (HW_MTPR HW_IER
    with bit 35).  At that point interval-timer divert fires
    legitimately and we transition into normal interrupt-driven SRM
    operation.
-   Task #31 (call-site refactor onto LogSubsystem) still pending.
    Six call sites: UnalignedEventLog, CboxEventLog, three chipsetLib
    CSR_LOG macros, Machine interval-timer SUPPRESSED throttle,
    Machine Step D detect log, Snapshot save/load DEBUG prints.
-   Other FP opcodes (FP arithmetic) and BWX byte/word loads/stores
    haven't been needed yet but may surface later.

## File-by-file summary (for the git commit)

```
NEW
    coreLib/PalShadow.h
    coreLib/LogSubsystem.h
    coreLib/LogSubsystem.cpp
    traceLib/PaDump.h
    traceLib/PaDump.cpp
    fBoxLib/grains/FpFormat.h
    tools/diagnostics_usage.md
    journals/20260519_decompressor_pal_overlap_findings.md
    journals/20260519_opcdec_handler_loop_finding.md
    journals/20260519_boot_path_cleared.md

MODIFIED -- core data
    coreLib/CpuState.h               (intShadow[8] + ier field)
    grainFactoryLib/GrainMasterV4.tsv (+8 FP load/store rows)
    grainFactoryLib/codegen/handwritten.tsv (+8 fBox::exec*)

MODIFIED -- pipeline / drain
    pipelineLib/PipelineDriver.h     (Mem-format Rb regfile fix)
    pipelineLib/MemDrainer.h         (formatLoadValue, S_VaxFp dispatch)

MODIFIED -- leaves
    fBoxLib/grains/Float.cpp         (+8 FP load/store leaves)

MODIFIED -- PAL dispatch + transitions
    palBoxLib/grains/PalEntries.cpp  (palModeEnter/Leave at three
                                      sites; HW_IER read/write; +PalShadow
                                      include)
    systemLib/Machine.cpp            (stageInterruptDivert uses
                                      palModeEnter; canAcceptInterrupt
                                      extended with IER gate; +PalShadow
                                      include)

MODIFIED -- CLI + main
    systemLib/AppOptions.h           (+ dumpDisasmPa/Count, log* fields)
    systemLib/AppOptions.cpp         (5 new flags + help text)
    main.cpp                         (LogSubsystem flag application,
                                      DUMP_DISASM wiring)

MODIFIED -- build
    CMakeLists.txt                   (2 new options, source-list adds for
                                      LogSubsystem / PaDump / PalShadow;
                                      test-target mirrors)

REGENERATED -- codegen output (from updated TSV + handwritten.tsv)
    grainFactoryLib/generated/SemanticFlagsEnum.h
    grainFactoryLib/generated/DispatchKinds.h
    grainFactoryLib/generated/GrainsForward.h
    grainFactoryLib/generated/DispatchTables.cpp
    grainFactoryLib/generated/GrainStubs.cpp
```

## Auto-memory entries to drop

Suggested:

`project_boot_path_cleared_20260519.md` -- condensed summary of this
journal.  Replace prior `project_decompressor_pal_overlap_confirmed.md`
which had the wrong interpretation.

`reference_log_subsystem_pa_dump.md` -- pointer to
`tools/diagnostics_usage.md` for operator-facing reference.

`feedback_axpbox_cross_check.md` -- AXPBox's
`(state.eien & state.eir)` gate is the canonical reference for
interrupt arbitration policy.  Future Phase-X interrupt work should
consult AXPBox `System.cpp::interrupt` + `AlphaCPU.cpp::DoClock`.
