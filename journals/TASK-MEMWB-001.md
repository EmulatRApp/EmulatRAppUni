# TASK-MEMWB-001: Load writeback vs. descriptor corruption at IO_ROUTINES+641B0

Status: OPEN -- investigation only, no source edits without sign-off
Platform: ES40, OpenVMS Alpha V8.3
Source evidence: SDA crash dump `sda_boot01.txt` (SYSDUMP.DMP, DEV20)

---

## 1. Objective

Determine which of two mutually exclusive causes produced a bad branch target
in R26 at `IO_ROUTINES+641B0`:

* **H1 (pipeline)** -- the `LDQ` at `IO_ROUTINES+64194` read the correct value
  from guest memory but failed to write it back to R26, leaving a stale
  register. This is a MEM-stage writeback defect in EmulatR.
* **H2 (upstream corruption)** -- the `LDQ` performed correctly and guest
  memory genuinely holds the bad value, meaning the procedure descriptor was
  built wrong at some earlier point. The fault site is a victim, not the cause.

These require different work. Do not begin either until the gate in section 4
is resolved.

---

## 2. Established evidence

All of the following is read directly from the dump. Do not re-derive it.

### 2.1 The exception

Signal array at `FFFFFFFF.83657950`:

```
CHF64$L_SIG_ARGS   00002604.00000005    5 arguments
CHF64$Q_SIG_NAME   00000000.0000000C    SS$_ACCVIO
CHF64$Q_SIG_ARG1   00000000.00010000    reason mask
                   FFFFFFFF.7FFF0DC8    VA
                   FFFFFFFF.7FFF0DC8    PC
                   30000000.00001F00    PS
```

VA equals PC, so this is an instruction-fetch access violation: the machine
transferred control to `FFFFFFFF.7FFF0DC8` and faulted attempting to fetch
there. Reason mask bits 0-2 are clear, indicating a not-valid PTE on a read
access, not a protection violation.

PS decodes to kernel mode at IPL 31. That is the entire explanation for
"Exception while above ASTDEL" -- the faulting code was legitimately at high
IPL. INVEXCEPTN is the wrapper VMS must produce for any exception taken there.
The SIRR/AST work (PE-4/PE-5) is NOT implicated in this crash and should not be
pulled into this investigation.

### 2.2 The control transfer

```
IO_ROUTINES+64194:  LDQ   R26,#X0008(R0)
IO_ROUTINES+64198:  ADDL  R31,R17,R18
IO_ROUTINES+6419C:  STQ   R17,#X0018(FP)
IO_ROUTINES+641A0:  ADDL  R31,R5,R17
IO_ROUTINES+641A4:  ADDL  R31,R4,R16
IO_ROUTINES+641A8:  BIS   R31,#X03,R25
IO_ROUTINES+641AC:  BIS   R31,R0,R27
IO_ROUTINES+641B0:  JSR   R26,(R26)
IO_ROUTINES+641B4:  LDL   R23,(R5)        <- SAVR26 confirms this is the return addr
```

Standard Alpha bound-procedure call. R0 holds the procedure descriptor; R26
receives the code address from descriptor+8; JSR branches to R26 and saves
PC+4 into R26.

`SAVR26 = FFFFFFFF.82CCC1B4` = `IO_ROUTINES+641B4`. The JSR itself computed its
return address correctly. The defect is confined to the value R26 held *before*
the branch.

`SAVR27 = FFFFFFFF.818DF1F8` establishes R0, since `BIS R31,R0,R27` at +641AC
is a register copy. The descriptor is therefore at `FFFFFFFF.818DF1F8` and the
code-address field being read is at `FFFFFFFF.818DF200`.

`MAP FFFFFFFF.818DF1F8` places this in IO_ROUTINES nonpaged read/write, base
`FFFFFFFF.818D4C00`, offset `0005E5F8`.

### 2.3 Why this points at the load path specifically

Every EX-stage result between the LDQ and the JSR landed correctly. Verified
against the saved register block:

| Instruction              | Expected                    | Saved value          | OK |
|--------------------------|-----------------------------|----------------------|----|
| `ADDL R31,R5,R17`        | sign-ext of R5 = 82CCC064   | `FFFFFFFF.82CCC064`  | Y  |
| `ADDL R31,R4,R16`        | sign-ext of R4 = 81C1D680   | `FFFFFFFF.81C1D680`  | Y  |
| `BIS  R31,#X03,R25`      | 3                           | `00000000.00000003`  | Y  |
| `BIS  R31,R0,R27`        | R0                          | `FFFFFFFF.818DF1F8`  | Y  |

ALU writeback is working. The single wrong register is the one sourced from a
memory load. That asymmetry is what makes H1 credible.

It also constrains H1: the defect cannot be a blanket "MEM stage never runs."
`LDL R0,(R6)` at +64164 and `LDQ_U R28,#X0004(R6)` at +6416C both produced
plausible values in the same basic block. Any H1 mechanism must explain why
some loads in the same block commit and one does not.

Caveat to carry forward: if R0 is itself stale but coincidentally plausible,
the LDQ read the wrong address entirely and both hypotheses are mis-framed.
Section 4 addresses this.

---

## 3. Non-goals

Explicitly out of scope for this task:

* PE-4 / PE-5 (SIRR backing store, AST composition). Not implicated -- see 2.1.
* PE-2 / PE-3 (PCTX model, SWPCTX ASN install). Already landed; the rerun
  reproduced identically, which is a null result on those fixes.
* FBOX leaf coverage. The fault log contains zero OPCDEC across the OS window.
* Unaligned access. `unalignTrapEnabled` is false by deliberate bring-up
  setting.
* Any source edit. This task ends at a written finding and a recommendation.

---

## 4. Gate -- resolve before any code work

Two SDA commands against the existing dump. Both are cheap and decide the
branch.

```
SDA> EXAMINE FFFFFFFF.818DF1F8;20
SDA> EXAMINE FFFFFFFF.82CCC060;10
```

**First command** reads the descriptor. Look at the quadword at
`FFFFFFFF.818DF200`.

* Contains a plausible code address (expect `FFFFFFFF.82CCxxxx` or similar
  in-image value) -> **H1 confirmed.** Guest memory was correct; the load
  failed to deliver it. Proceed to section 5.
* Contains `FFFFFFFF.7FFF0DC8` -> **H2 confirmed.** The load worked. The
  descriptor was corrupted earlier. Proceed to section 6.
* Contains something else entirely, or the descriptor does not look like a
  descriptor -> R0 is suspect. Stop and report; neither branch applies.

**Second command** validates R0's provenance. `INTSTK$Q_R6 = FFFFFFFF.82CCC060`
and the loop at +64160 walks R6 with `LDL R0,(R6)` as a data pointer, but that
address is in the same range as the executing code. The EXTBL/INSBL/STQ_U
sequence at +6416C..+6418C implies 8-byte table entries of the form
{4-byte pointer, flag byte at +4}. Confirm the memory there matches that shape.
If it disassembles as instructions instead, R6 is wrong and the whole walk is
operating on garbage -- report that as a distinct finding.

Record both results in the finding before proceeding.

---

## 5. Branch H1 -- load writeback defect

Scope: the MEM/WB path for integer loads in the V5 pipeline.

Investigate, in order:

1. **Locate the writeback commit for load results.** Trace how a load result
   moves from MEM into the architectural register file. Identify the
   `commitPending` path (or equivalent) and every condition that can skip,
   defer, or discard it.

2. **Enumerate skip conditions.** For each early-return, guard, or predicate
   on that path, determine what guest-visible condition triggers it. The
   working question is: what is true of `LDQ R26,#X0008(R0)` that is not true
   of `LDL R0,(R6)` or `LDQ_U R28,#X0004(R6)` in the same block?

   Candidates worth checking explicitly, without prejudice to others found:
   * Interaction with the preceding `STQ_U` at +6418C -- store-to-load
     ordering, write buffer drain, or a pending-store hazard that suppresses
     the following load's commit.
   * The backward branch at +64190 (`BLBS R27`) -- whether a not-taken branch
     resolution leaves state that affects the next instruction's writeback.
   * Register-specific handling of R26, given its special role in JSR. If
     anything special-cases R26 as a return-address register, that is a prime
     suspect.
   * Size-specific paths: LDQ vs LDL vs LDQ_U may take different code.

3. **Construct a minimal reproducer** before touching pipeline source. Target
   an instruction sequence in the existing test harness that mirrors the shape:
   store-unaligned, conditional backward branch, quadword load into R26,
   indirect JSR. If the suite reproduces a dropped writeback, the defect is
   isolated without needing a full OS boot.

4. **Report, do not fix.** Produce the finding with the suspected code path
   named and the reproducer attached. Sign-off gate applies before any edit.

Instrumentation note: if a probe is needed, it must be compile-gated with an
explicit retirement policy per house convention, and any run output goes to
`./logs` or `./traces` under the run directory with a
`purpose_YYYYMMDD_HHMMSS.ext` stem.

---

## 6. Branch H2 -- descriptor built wrong

Scope: the write path that populated `FFFFFFFF.818DF200`.

The bad value `FFFFFFFF.7FFF0DC8` is not random. Characterize it before
searching:

* It is a canonical 43-bit Alpha VA (bits 63-43 all equal bit 42), so it is not
  a malformed-address signature. It lands in 64-bit system space just below
  S0/S1.
* SDA labels it `CTL$GQ_SSI_DATA+008D0`. Treat that label as a coincidental
  nearest-symbol match, not as meaning, since there is no current process and
  P1 space is not mapped.
* The low longword `7FFF0DC8` has bit 31 clear. A correct 32-to-64 sign
  extension of that value yields `00000000.7FFF0DC8`, NOT the observed value.
  Whatever produced the upper `FFFFFFFF` did not do so by sign-extending bit 31.
  Determine whether any EmulatR path can produce a high longword of all-ones
  paired with a low longword whose bit 31 is clear -- a merge of two sources, a
  partial write, or a sign extension keyed off the wrong bit position.

Then work backward: identify which VMS routine writes descriptor+8 for this
image, and instrument the guest write to that physical address to catch the
moment it is set. A watchpoint on the PA underlying `FFFFFFFF.818DF200` is the
cheapest instrument if the emulator supports one.

---

## 7. Secondary findings (log, do not chase)

Two observations from the dump that are not on the critical path but should be
recorded so they are not rediscovered:

1. **PS reserved bits.** `INTSTK$Q_PS = 30000000.00001F00` has bits 60-61 set.
   A second stack slot at `FFFFFFFF.83657738` holds `18000000.00001F00` --
   identical low longword, upper differing by exactly one left shift. Reserved
   PS bits should be zero. Confirm whether EmulatR's PS composition is the
   source before assuming VMS wrote them.

2. **HWRPB CPU count.** The bugcheck banner reports `Supported CPU count:
   00000002` while `Processor-Count=4` is configured. VMS derives that from the
   HWRPB, so either the CPU slot array is built for 2 or the supported-count
   field is not populated from the configured value. Unrelated to this crash;
   will matter at SMP bring-up.

---

## 8. Deliverable

A written finding containing:

* The two SDA EXAMINE results from section 4, verbatim.
* Which branch was taken and why.
* The named code path under suspicion, with file and function.
* A reproducer if one was constructed, or a statement that none was found.
* Explicit statement of what was ruled out.

No source edits land under this task. Fix work is a separate task opened
against the finding.

---

## 9. FINDING (2026-07-28, Cowork) -- gate resolved, H1 split

### 9.1 Gate results (section 4), verbatim

```
SDA> EXAMINE FFFFFFFF.818DF1F8;20
FFFFFFFF 81808B48 FFFFFFFF 8180A708 FFFFFFFF 801151C0 00000200 00003008

SDA> EXAMINE FFFFFFFF.82CCC060;10
00000000 82CCCB60 00000000 82CCCAE0 00000000 82CCCAA0 00000002 818DF1F8
```

SDA prints high address left; the ASCII column confirms the order.  Laid
out ascending:

```
818DF1F8: 00000200.00003008     descriptor flags/signature
818DF200: FFFFFFFF.801151C0     ENTRY POINT  <- what the LDQ should deliver
818DF208: FFFFFFFF.8180A708
818DF210: FFFFFFFF.81808B48

82CCC060: 818DF1F8  +064: 00000002    entry 0: pointer + flag byte 0x02
82CCC068: 82CCCAA0  +06C: 00000000
82CCC070: 82CCCAE0  +074: 00000000
82CCC078: 82CCCB60  +07C: 00000000
```

`MAP FFFFFFFF.801151C0` -> IO_ROUTINES, nonpaged READ-ONLY, offset 371C0.

Gate verdict: descriptor holds a plausible in-image code address ->
**H1 branch** per section 4.  The table is the predicted {4-byte pointer,
flag byte at +4} shape, R6 walks real data, R0's provenance is sound.
Entries 1-3 still have clear flags, so the loop made ONE pass and fell
through -- the LDQ executed exactly once.

### 9.2 The H1 label is retired; the hypothesis splits

TASK-MEMWB-001 bundled an OUTCOME (stale R26) with a MECHANISM (MEM-stage
writeback defect).  Measurement has separated them:

  **H1a -- MEM writeback defect.  RULED OUT BY MEASUREMENT.**
  **H1b -- post-retire clobber of R26.  OPEN.  Now the leading branch.**

### 9.3 Evidence retiring H1a

Instrumented run 20260728_194417, parameterized LOAD-WATCH armed on the
faulting LDQ (`EMULATR_LOAD_WATCH_PC=0xFFFFFFFF82CCC194`).  Fatal
execution:

```
LOAD-WATCH cyc=2086442480 pc=0xffffffff82ccc194 sz=8
           va=0xffffffff818df200 pa=0x00000000010df200
           raw=0xffffffff801151c0 status=0
```

Every field is what a healthy load must produce: VA correct, PA correct
(PFN 0x86F<<13 | 0x1200 = 0x10DF200), data correct, bus status Ok.
`EMULATR_XLATE` over the same page independently reports
`path=TLBHIT pfn=0x808` -- the GH=3 block base, composing correctly.

Path from that log point to the register file, by inspection:
  - `execLdq` sets regWriteIdx = raIndex(g) (= 26), memSize = 8,
    regWriteIsFp = false
  - `formatLoadValue` integer size-8 arm is `return raw;` (passthrough)
  - nothing between the log point and the commit sets faultCode (only
    formatLoadValue and the isLocked branch; isLocked is false for LDQ)
  - `MemDrainer::drain` therefore reaches `intReg[26] = raw`

So the load delivered the right value to the right register.  The
writeback is not the defect.

### 9.4 What H1b must now explain

R26 was correct at LDQ retire and wrong six instructions later at
`JSR R26,(R26)`.  None of the intervening instructions writes R26
architecturally:

```
+64198 ADDL R31,R17,R18    +6419C STQ  R17,#X0018(FP)
+641A0 ADDL R31,R5,R17     +641A4 ADDL R31,R4,R16
+641A8 BIS  R31,#X03,R25   +641AC BIS  R31,R0,R27
```

Leading candidate: an interrupt or exception taken inside that window,
with R26 not preserved across PAL entry/exit.  `STQ R17,#X0018(FP)` at
+6419C is the obvious fault site (a store, hence a distinct translation
and permission path from the loads that succeeded).

Discriminating test, no new code required -- a DIAG-PC window over the
eight instructions, read for CYCLE DISCONTINUITY between consecutive
in-window retires.  Contiguous cycles = nothing intervened (and H1b
fails too, forcing a re-examination of the JSR's own operand read);
a gap = PAL ran, and register preservation across PAL entry is the
defect.

### 9.5 Ruled out (do not re-walk)

  - Guest memory corruption (H2).  Descriptor, table and pointer are all
    intact in the dump.
  - Translation / GH compose / TB.  Measured PA is exactly correct on the
    failing load; XLATE reports a clean TLBHIT.  The GH=3-vs-GH=0
    asymmetry between the failing and succeeding loads is a COINCIDENCE,
    not a cause.  (An earlier Cowork argument that ruled out aliasing via
    page-offset comparison was itself invalid -- that test only holds for
    GH=0 -- but the direct PA measurement supersedes it either way.)
  - Bus error / kFaultBusError `basePc += 4` skip.  No BusError was ever
    logged in either run; the only fault class present is
    kFaultDtbMissDouble, and all 32 UNHANDLED events are writes to one
    known PCI offset.
  - MEM-stage writeback (H1a), per 9.3.
  - PE-4/PE-5 SIRR/AST.  IPL 31 is legitimate for early EXE$INIT; it
    explains the INVEXCEPTN wrapper, not the exception.
  - ASN churn.  Census on the same run: ONE ASN ever installed (0x00),
    and M2 (ASN-attributable misses) = 0 of 144,626 classified.  97% of
    misses are same-ASN refills (M3 = 140,189 over 1,835 distinct VPNs
    -- ~86 refills per page), i.e. TB thrash from invalidation/capacity,
    unrelated to addressing.

### 9.6 Instrumentation added (diagnostic only, no behavioural change)

Committed 0ab573c before this finding: parameterized LOAD-WATCH in
`MemDrainer::applyLoadEffect` (env gates EMULATR_LOAD_WATCH_PC / _PA /
_CYCLO / _CYCHI / _CAP), superseding a 2026-05-30 scaffold that hardcoded
a cycle window and sat under `EMULATR_MEMDIAG` (#define'd 0, so it had
never compiled).  Also `pteLib/AsnCensus.h`.  Both compile-gated under
EMULATR_BRINGUP_PROBES and inert unless armed.

Same commit repaired `systemLib/Machine.cpp:1164`, where a VS2022 session
attached to the running emulator had silently deleted two characters from
`pipelineLib::PipelineDriver::step`.  Caught only because it broke the
build; full working-tree diff reviewed, no other stray edits.

---

## 10. FINDING UPDATE (2026-07-28 evening) -- H1a AND H1b both ruled out

### 10.1 R26 measured across the fatal sequence

`EMULATR_DIAG_SHOWREG=26` added to the DIAG-PC line (retire() runs AFTER
MemDrainer::drain, so the value on an instruction's own line is its
POST-COMMIT state).  Run 20260728_201335, final pass:

```
cyc         pc          insn              r26 after
2080153487  82ccc190    BLBS              ffffffff82ccc1b4   (stale, prior JSR link)
2080153488  82ccc194    LDQ R26,8(R0)     ffffffff801151c0   <- COMMIT WORKED
2080153489  82ccc198    ADDL              ffffffff801151c0
2080153490  82ccc19c    STQ R17,18(FP)    ffffffff801151c0
2080153491  82ccc1a0    ADDL              ffffffff801151c0
2080153492  82ccc1a4    ADDL              ffffffff801151c0
2080153493  82ccc1a8    BIS               ffffffff801151c0
2080153494  82ccc1ac    BIS               ffffffff801151c0
2080153495  82ccc1b0    JSR R26,(R26)     ffffffff82ccc1b4   (its own link write, correct)
```

Paired LOAD-WATCH at cyc 2080153488: va=818df200 pa=0x10df200
raw=ffffffff801151c0 status=0.

**H1a (MEM writeback defect) -- RULED OUT BY MEASUREMENT.**  The load
committed the correct value to the correct register.

**H1b (post-retire clobber) -- RULED OUT BY MEASUREMENT.**  R26 held
ffffffff801151c0 unchanged across all six intervening instructions, and
the cycles are contiguous (487..495, one per instruction, every line
fault=0 pal=0) so no interrupt, exception or PALcode ran in the window.

Jump path verified by inspection as well: buildCtx resolves Rb=26 from
the encoding (0x6b5a4000 -> opcode 0x1A, Ra=26, Rb=26, func=01=JSR);
execJsr computes `divertTarget = (opB & ~3) | (pc & 1)`; retire does
`cpu.pc = r.divertTarget`.  Every link is correct.

### 10.2 THE FRAMING ERROR (recorded so it is not repeated)

This investigation treated `FFFFFFFF.7FFF0DC8` as THE JSR'S TARGET.  It
is not.  It is **the PC at which the ACCVIO was taken** -- the two are
the same thing ONLY IF the JSR branched directly into the fault.  The
DIAG-PC window stopped at +641B4, so there was NO visibility after the
JSR fired, and the assumption was never tested.

Evidence the assumption is wrong:
  - The same JSR succeeded TWICE in the immediately preceding loop
    iterations, returning to +641B4 after 25 and 58 cycles.  The callee
    demonstrably runs.
  - On the fatal pass no return line appears -- consistent with the
    callee faulting, not with the jump failing.
  - `801151C0` and `7FFF0DC8` have no bit-level relationship.  Repeated
    attempts to derive one from the other failed because THEY WERE NEVER
    THE SAME VALUE: one is the call target, the other is wherever the
    callee subsequently went.

Corrected model: the JSR most likely lands correctly at
`IO_ROUTINES+371C0` (= 801151C0, nonpaged read-only, confirmed by MAP),
the callee executes, and something inside it transfers to 7FFF0DC8.

### 10.3 Test in flight

DIAG-PC window moved to the CALLEE (`0xFFFFFFFF80115000` ..
`0xFFFFFFFF80116000`, CYCLO=0, CAP=5000).  Lines present -> the JSR
worked and the defect is inside the callee.  Silence -> the jump really
did go astray and JmpClass dispatch needs a much harder look.

### 10.4 Operational lessons from this session

  1. **Never gate an instrument on an absolute cycle number across
     runs.**  Cycle counts VARY run to run with the console path taken
     (observed 2.080e9, 2.086e9, 2.938e9 for the same fault, depending
     on whether the SRM took the firmware-update branch).  Gate on PC,
     which is stable, and use cycles only WITHIN a single run's log.
  2. **Determinism is otherwise solid** -- four consecutive reproductions
     of bugcheck 000001CC with identical register state and identical
     instruction sequence.
  3. **A dump cannot answer a wrong-PA question.**  A selective dump
     captures MAPPED VIRTUAL memory; a read of a valid-but-wrong
     physical address returns data the dump never contains.  Bus-level
     instrumentation is the only instrument for that class.
  4. **Two emulators, two disk images.**  EmulatR runs dka0.vdisk,
     Charon analyses a COPY (dka0_t.vdisk).  Keep it that way -- a
     shared image would let a live VMS and an emulator run corrupt it.

### 10.5 Instruments available for reuse (all env-armed, probes only)

```
EMULATR_LOAD_WATCH_PC / _PA / _CYCLO / _CYCHI / _CAP   bus-level load observation
                                                       (va, pa, raw, bus status)
EMULATR_DIAG_PCLO / _PCHI / _CYCLO / _CYCHI / _CAP     retire window
EMULATR_DIAG_SHOWREG=<0..31>                           append a live GPR to each line
EMULATR_XLATE + _VALO / _VAHI / _CYC                   translation classification, VA<->PA
EMULATR_ASN_CENSUS                                     ASN allocation + miss attribution
EMULATR_VALUE_GATE / _FLOOR, EMULATR_PC_GATE           lookback ring dump triggers
```

Caution recorded: the ring-dump gates are ONE-SHOT and share a single
`s_fired` latch, so a common value (0x1CC hit a loop counter) burns the
trigger before the interesting event.  Prefer PC gates or full 64-bit
values.
