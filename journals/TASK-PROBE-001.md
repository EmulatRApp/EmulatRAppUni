# TASK-PROBE-001 -- Retire fault_pc+R30 stdout probe; scope boot-entry replacement

    Project    : EmulatR (Alpha AXP / EV6 21264)
    Author     : eNVy Systems, Inc.
    Raised     : 2026-07-24
    Executor   : Cowork
    Architect  : Claude
    Sign-off   : Tim (required at GATE 1 and GATE 2)
    Status     : TASK A ready to execute. TASK B blocked pending GATE 2.

ASCII-128 only. All names marked `_PROVISIONAL` are intent-named by the
architect and NOT verified against the tree.

---

## 0. Context

A compile-time diagnostic probe currently prints an instruction window to
**stdout**, headed:

    fault_pc + R30  (Tim's idea)   = 0x00000000000a6e88
        +00  pa=0x00000000000a6e88  word=0x40481131
        ... (10 words total, +00 through +36)

Its readout is complete. The ten words decode as coherent Alpha console code
(SUBL / STQ / argument marshalling into r17-r21 / BSR linking r26 / canonical
NOP / GP-relative LDA). There is no memory corruption at PA `0xa6e88`. The
probe has answered its question and is now retired under the standard
probe-retirement policy.

Three additional reasons for removal:

1. It writes to **stdout**, which is shared with the SRM console stream.
   Interactive console sessions (`SHOW DEVICE`, `BOOT DQA1`) are now in
   routine use; interleaved diagnostic text risks corrupting terminal state
   mid-session. This is a correctness risk, not cosmetic.
2. `fault_pc + R30` (a PC added to a stack pointer) has no architectural
   meaning as an address basis. Any future firing lands arbitrarily.
3. It is not aimed at the current blocker. At the halt under investigation,
   zero bootstrap instructions have retired, so the GPRs hold SRM residual
   state and describe the console rather than the bootstrap.

---

## TASK A -- Remove the probe

**Scope: removal only. No refactoring, no renaming, no reformatting of
surrounding code. Do not "improve" anything adjacent.**

### A.1 Discovery

Run, from the repo root:

    grep -rn "fault_pc" --include=*.cpp --include=*.h
    grep -rn "Tim's idea" --include=*.cpp --include=*.h
    grep -rn "fault_pc + R30" --include=*.cpp --include=*.h

Record every hit with file and line number.

**GATE 1 -- STOP AND REPORT if any of the following are true:**

- the probe spans more than one translation unit;
- the emitting code lives outside `main.cpp`;
- a helper function exists solely to serve this probe (it must be removed
  too, but only with sign-off);
- the probe is NOT wrapped in a compile-time guard (`#if defined(...)`),
  i.e. it is unconditional code;
- more than one distinct probe matches these greps.

Otherwise proceed.

### A.2 Removal

- Delete the emitting block **and** its compile-time guard.
- Delete any macro definition, CMake option, or `target_compile_definitions`
  entry that exists solely to gate this probe. Search:

      grep -rn "<GUARD_MACRO_NAME>" CMakeLists.txt cmake/ --include=*.txt --include=*.cmake

- Delete any helper local to the probe (formatters, address computation,
  hex-dump lambdas) that becomes unreferenced.
- Do **not** delete any shared hex/format utility also used elsewhere.
  Verify by grep before removing anything.

### A.3 Commit header (ADR)

Place at the top of the commit message:

    // ADR-NNN: Remove fault_pc+R30 stdout instruction window.
    //
    // Readout complete. PA 0xa6e88 decoded as coherent Alpha console code
    // (SUBL/STQ/arg marshalling/BSR/NOP/LDA); no corruption present. The
    // question the probe was raised to answer is answered.
    //
    // Removed for four reasons:
    //   (a) readout complete, per probe-retirement policy;
    //   (b) wrote to stdout, colliding with the SRM console stream now that
    //       interactive console sessions are in routine use;
    //   (c) fault_pc+R30 has no architectural meaning as an address basis;
    //   (d) not aimed at the current blocker (halt at bootstrap entry with
    //       zero bootstrap instructions retired -- GPRs hold SRM residue).
    //
    // Superseded by EMULATR_BOOT_ENTRY_TRACE (TASK-PROBE-001 TASK B),
    // pending architect sign-off.

Replace `ADR-NNN` with the next sequential ADR number in the tree.

### A.4 Acceptance criteria

1. Clean configure + build, MSVC / VS2022, no new warnings.
2. `grep -rn "fault_pc"` returns zero hits in `.cpp` / `.h`.
3. DS10, DS20, ES40 each still reach `P00>>>` in ISP mode.
4. DS20 `BOOT DQA1` still reaches the halt at `PC = 20000000`, i.e. the
   removal changed no behavior on the boot path.
5. Console output for a full boot-to-prompt session contains no diagnostic
   instruction-window text.

Criterion 4 matters: this task must be behavior-neutral. If the halt
signature changes, **stop and report** -- that would mean the probe was not
side-effect-free, which is itself a finding.

---

## TASK B -- Replacement probe (BLOCKED)

**Do not begin TASK B without sign-off at GATE 2.**

The intended replacement is `EMULATR_BOOT_ENTRY_TRACE`: a one-shot probe
firing on halt, writing to `./traces/boot_entry_YYYYMMDD_HHMMSS.txt` (house
convention -- **never stdout**). It answers a single question:

> At the halt PC, does the instruction fetch return `0x00000000`
> (`CALL_PAL HALT`)? If so, is the cause image placement or VA translation?

It has five sections: exception/MM IPR state; ITB lookup for the entry VA; an
independent software page-table walk; an instruction window read **through
the fetch path** (not a raw PA read); and a dump of the loaded image at its
reported physical base for diffing against APB extracted from the ISO.

Sections 2 and 3 deliberately duplicate. Divergence between the ITB result
and an independent walk localizes the fault to the ITBMISS fill path, which
is where the C5 regression would land.

### B.1 Reconnaissance required before any code is written

Report existence, exact signature, and side-effect status of each:

| Intent | Architect's provisional name | Exists? |
|---|---|---|
| Read IPR by enumerator | `CpuState::readIpr(Ipr)` | ? |
| ITB probe, no fill, no fault | `CpuState::itbLookup(va)` | ? |
| Software PT walk, no side effects | `Mmu::walkForDebug(va, ptbr)` | ? |
| Fetch-path peek, no side effects | `CpuState::peekInstructionForDebug(va)` | ? |
| Raw physical read, no side effects | `Memory::readPhys32ForDebug(pa)` | ? |
| Halt code accessor | `CpuState::haltCode()` | ? |
| PAL-mode flag accessor | `CpuState::palMode()` | ? |

For each: does it exist, does it have a debug/non-mutating variant, and does
calling it perturb ITB state, fault state, or the decode cache?

**This is the crux.** A probe that fills the ITB, posts a fault, or pollutes
the decode cache while measuring will change the thing it measures. Any
accessor lacking a genuinely side-effect-free path must be reported, not
worked around.

### B.2 GATE 2 -- architect + Tim sign-off

Required before implementation:

1. Recon table above, completed.
2. Confirmation of the `Ipr` enumerator names actually present in the tree
   (`EXC_ADDR`, `EXC_SUM`, `MM_STAT`, `VA`, `IPL`, `PTBR` are provisional).
3. Decision: does the probe fire only on `halt_code == 0`, or on any halt?
   Architect's recommendation is **any halt** -- broader is more useful if a
   fix changes the halt code rather than eliminating the halt.
4. Confirmation of the image base `0x5bc000` as reported by SRM
   (`base = 5bc000, image_start = 0, image_bytes = 99400`), and whether it
   is stable across boots or must be captured at runtime. If it varies,
   hardcoding it is wrong.

---

## Out of scope

Explicitly **not** part of this task. Raised separately, do not touch:

- `AlphaServer DS20 4 MHz` -- timebase defect, `kCcMultiplier` (see prior
  RSCC analysis). Real and boot-relevant, but a separate work item.
- `SROM Revision:` non-ASCII field bleed.
- `Pchip 0 Rev 1` vs `Pchip 1 Rev 4.2` inconsistency.
- `EWA0` at `FF-FF-FF-FF-FF-FF` (21143 SROM all-ones).
- `memtest` / `net: No such command`.

---

## Reporting

On completion of TASK A, report:

- grep results from A.1 (all hits, file:line);
- exact diff applied;
- build result (warnings verbatim, if any);
- boot-to-prompt confirmation for DS10 / DS20 / ES40;
- DS20 `BOOT DQA1` halt signature, verbatim, for comparison against:

      halted CPU 0
      halt code = 0
      PC = 20000000

Then **stop**. Do not proceed to TASK B.

---

## TASK A COMPLETION REPORT (Executor: Cowork/PC session, 2026-07-24)

A.1 Discovery: exactly ONE probe.  Hits (excluding out/ mirrors):
    main.cpp:779  { "fault_pc                       ", faultPc },
    main.cpp:780  { "fault_pc + R30  (Tim's idea)   ", r30PlusFault },
GATE 1: single TU (main.cpp), inside `#if EMULATR_BRINGUP_PROBES`, no
external helpers, no probe-only macro.  ONE DEVIATION REPORTED: the
guard block is SHARED with the live EMULATR_PCTRACE post-mortem dump
(JRN-VMB-016), so the `#if` + `if (sr != HaltedClean)` wrapper and the
`memory` local were KEPT; only the probe body (locals r30/faultPc/
r30Combined/r30PlusFault, Probe struct, probes[], 10-word print loop)
was excised, with a retirement comment in place.  EMULATR_BRINGUP_PROBES
is a shared CMake option (14 consumer files) -- kept per A.2.

A.4 acceptance:
  1. Clean MSVC/VS2022 build.  No NEW warnings (pre-existing C4834 at
     TsunamiChipset.h tulip setDmaAccess lambda + TsunamiChipset.cpp
     125-128 predate this change).
  2. grep "fault_pc" *.cpp/*.h = ZERO hits (retirement comment reworded
     to avoid the token).
  3. PARTIAL: DS20 reaches P00>>> (fresh binary, port 10024, scripted
     console, ISP mode).  DS10/ES40 boots DEFERRED: they need the shared
     config/Emulatr.ini model= switched, declined while Tim's interactive
     session is live on the same tree.  OWED (also standing owed from
     VMB-017 regardless of this task).
  4. STOP-AND-REPORT per the criterion-4 rule -- the signature DIFFERS
     from the doc's baseline, and the cause is ENVIRONMENTAL, not the
     removal: with the VMB-016/017 faithful stack armed (EMULATR_2D_NOOP/
     DELAYWARP/CSERVE_ROUTE=guest/DIVERT_PALSWAP), `b dqa1` proceeds PAST
     0x20000000, loads the bootstrap, and halts at
         PC = 0x20003a38, halt code 0 (HaltedClean), pal=0
     after printing `%APB-F-NOIOVEC` -- the IDENTICAL signature to
     `b dqa0` (JRN-VMB-019).  The doc's PC=20000000 baseline is the
     UNFIXED-env signature (probe removal cannot affect execution; it
     printed at post-mortem only).  NEW FINDING for the NOIOVEC track:
     the dqa1 (OpenVMS_v82.iso) boot block loads the SAME-SIZE bootstrap
     (1226 blocks, image_bytes 99400, base 5bc000) and fails identically
     -> the CD is NOT the IDE-boot unblock; see JRN-VMB-022 (no "IDE"
     keyword in APB) and JRN-SCSI-001/-002 (SCSI path, in progress).
     Console banner shows "10 MHz" this run (timebase item stays out of
     scope per Sec "Out of scope").
  5. Full boot-to-halt console session contains NO instruction-window
     text (grep "Instruction-stream probes" = 0).

A.3 ADR header prepared (next number = ADR-0002); COMMIT NOT MADE --
per house git rules the commit is Tim's call.  Header text ready:
    // ADR-0002: Remove fault_pc+R30 stdout instruction window.  (body
    // exactly as specified in A.3 above)

TASK B: NOT STARTED (GATE 2 respected).
Status: TASK A code-complete; DS10/ES40 boot confirmations owed.
