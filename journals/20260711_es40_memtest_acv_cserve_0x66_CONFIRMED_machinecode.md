<!--
EmulatR V4 -- ES40 memtest ACV surviving fault: CONFIRMED by machine-code
disassembly.  get_time (guest 0x8C2D0) = arg - cserve(0x66); EmulatR no-ops
CSERVE 0x66, so a stale R0 survives the subtraction and becomes a wild address
that is dereferenced -> ACV.  Records the disassembly evidence, reconciles the
lead that flipped, states explicitly why ECC is diagnostic context and NOT part
of the fix, and pins the remaining design question.  2026-07-11.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience.  [LOCATE] = point-in-time; verify
against the live tree.
-->

# ES40 memtest ACV -- CONFIRMED root: CSERVE 0x66 no-op (machine code) (2026-07-11)

## Verdict

get_time is the gap.  Disassembly of decompressed_es40_v7_3.bin (load base
0x8000) proves the subroutine at guest 0x8C2D0 computes `arg - cserve(0x66)`.
EmulatR no-ops CSERVE 0x66 (faithful to the GENERIC dispatch, wrong for this
console), so R0 is not overwritten with a time value; the stale R0 (the walk
pointer left by the prior fill loop) survives into the subtraction, producing
the wild VA 0xFFFFFFFF7F827F5F, which is then dereferenced at 0x1B7DD4 -> ACV.

This is machine-code evidence, not trace inference.  It confirms the ORIGINAL
CSERVE 0x66 root cause and supersedes the later "cserve incidental /
address-arithmetic / memory-size math" readings, which read the VALUES correctly
but missed that a no-op'd call was the intended writer of R0.

## The machine-code evidence

get_time subroutine, guest 0x8C2D0 (decoded from the instruction words):

    0x8C2F0  BIS   r31, r16, r2      ; r2 = arg (0x3FC12000 in the fault case)
    0x8C2F8  BIS   r31, #0x66, r16   ; r16 = 0x66  (CSERVE selector)
    0x8C2FC  BSR   r26, 0x1B78F8     ; -> 0x1B78F8: CALL_PAL 0x9 (CSERVE); RET
    0x8C300  BIS   r31, r29, r30     ; does NOT write r0
    0x8C304  LDQ   r26, 8(r29)       ; does NOT write r0
    0x8C308  SUBQ  r2, r0, r0        ; r0 = arg - r0   <-- consumes cserve's r0
    0x8C318  RET

- 0x1B78F8 = `CALL_PAL 0x9` = CSERVE, sub-dispatched on r16 = 0x66.  Confirmed.
- Between the CSERVE call (0x8C2FC) and the SUBQ (0x8C308), nothing writes r0.
- Therefore the SUBQ subtrahend IS CSERVE 0x66's output.  CSERVE functions
  return in R0; this is a textbook call-then-use-result.  The subroutine is
  `return arg - cserve(0x66)`.

Downstream (also from the image):
- get_time's result flows to an address: 0x5B03C `BIS r0 -> r16`, into 0x611B0
  (saves it in r3, re-passes as r16), eventually to the probe.
- The probe leaf 0x1B7DD4 is a bare `LDQ r0, 0(r16)` with NO mask.  So "add a
  PA/32-bit mask in the helper" is not the fix; the helper faithfully
  dereferences whatever address it is handed.

## Reconciliation -- why the lead flipped, why this is dispositive

- The value observations across the chase were all correct: the subtrahend is
  0xC03EA0A1, which is genuinely the stale walk pointer.
- What the value-only (dst-write) traces could NOT show is that CSERVE 0x66 was
  supposed to OVERWRITE that stale R0 with a time.  Missing that, the mechanism
  was read as "R0 is the walk pointer, cserve incidental."  The disassembly
  shows the selector load (#0x66) and the CALL_PAL 0x9 immediately before the
  SUBQ -- the call is not incidental; it is the intended writer of R0.
- The AAR ASIZ fix (companion journal ..._RESOLVED_aar_asiz_and_tiling.md)
  remains valid and valuable: it corrected memory sizing (16 MB -> 4 GiB) and
  cleared the 0x60222C fault.  It did NOT address this 0x1B7DD4 ACV, which is
  why the same VA survived the size fix unchanged -- the VA is size-independent
  (it is arg - stale_R0), which is the tell that it was never memory-size math.

## Why ECC is diagnostic context, NOT part of the fix

This is recorded explicitly to stop a future session from over-connecting ECC.

ECC connects to the DIAGNOSIS only:
- Main-memory ECC on a Tsunami/Typhoon ES40 is a HARDWARE (silicon) function of
  the chipset memory data path -- generated/checked/corrected on every DRAM
  access (SECDED).  The SRM firmware does not compute ECC; it configures the
  controller, INITIALIZES memory by writing every location so valid ECC exists
  (that is the fill loop, STQ -1 walking +8), STRESSES the path, and reads the
  hardware error-status CSRs.
- The routine's own fault-model note -- "stress the memory path, not detect
  address shorts" -- is what let us rule OUT the hypothesis that the wild
  address was a DELIBERATE probe expecting a recoverable fault.  It is not.  The
  wild address is unintended: a bug.

ECC does NOT connect to the FIX:
- The fix is CSERVE 0x66 returning a proper value in R0.  It touches nothing
  ECC-related.
- EmulatR models no ECC and does not need to.  Once CSERVE 0x66 returns a sane
  time, the memtest timing works, the stress loop runs, and because EmulatR's
  memory produces no ECC errors, the memtest passes cleanly.
- get_time is merely CALLED BY a memtest that happens to be an ECC exercise.
  Fixing get_time is orthogonal to ECC.  ECC eliminated a suspect, then left.

## The fix (task #12) -- and its one guardrail

Direction: implement CSERVE 0x66 so it returns a usable value in R0.  Then
get_time = arg - time is sane, the wild VA is never formed, and this ACV clears.

Guardrail (the reason this is not a blind one-liner): the value must satisfy
BOTH consumers of a 0x66 return:
  (a) memtest:  arg - time is a sane, non-negative quantity at 0x8C308;
  (b) SCB path: it must not re-shift base = R0 + 0x28000 (the 2026-07-08
      regression, when a 0x66 return that was too large -- a BCD TOY value --
      shifted the SCB base and halted at PC 0).

OPEN (needs source, do not guess):
  1. The SEMANTICS of CSERVE 0x66 for THIS console.  It is undefined in the
     generic pc264 .mar table (stops at 0x65 / MP_WORK_REQUEST), so it is
     console-firmware-private.  Its correct return (cycle count? interval tick?
     elapsed value? scale/units?) must be sourced from the console handler, not
     inferred.  Likely a time primitive given the get_time idiom and the
     interval-clock/TOY interrupt seam, but that is inference until confirmed.
  2. Whether the SCB-base consumer and the memtest get_time are the SAME 0x66
     call site or two.  Decides whether one return value can serve both, or
     whether context distinguishes them.  Find the SCB-setup 0x66 call site and
     compare.

Because the value is time-scaled and already caused one slow-feedback
regression, the VALUE DESIGN is a web-chat task (units + both consumers) and
Cowork lands the edit once the value and its consumers are pinned.  The
MECHANISM is now closed; only the value is open.

## PAL HANDLER DISASSEMBLED (Cowork, 2026-07-11) -- closes OPEN item #1 (semantics)

The image's OWN PAL implements CSERVE 0x66; the "undefined / console-private /
faithful no-op" reading (mine, 2026-07-10, from ev6_vms_pc264_pal.mar which stops
at 0x65) was the WRONG PAL variant.  Machine-code proof from decompressed_es40_v7_3
(base 0x8000):
  cserve dispatch (sys__cserve) at guest 0x13384..0x133f4 chains
    cmpeq r16,#0x10..0x15, #0x3e, #0x40..0x45, #0x65, and #0x66 (0x133f4);
  the 0x66 case bne-targets the handler at 0x139e8:
    0x139e8  HW_MFPR R0, 0x1010    ; read IPR 0x1010
    0x139ec  SRL     R0, #0x15, R0 ; >> 21
    0x139f0  SLL     R0, #0x15, R0 ; << 21  (clear low 21 bits, ~2M granularity)
    0x139f4  HW_RET
CORRECTION 2026-07-11 (newer HRM, 21264ev67_hrm.txt): the "IPR 0x1010 = a time/
cycle counter" reading is WRONG.  The HW_MFPR index field is NOT 16-bit; per HRM
Figure 6-4 it is INDEX[15:8] + SCBD_MASK[7:0].  So 0x1010 -> IPR index 0x10, scbd
mask 0x10.  Calibrated against the trace's EXC_ADDR read at PAL 0x8300 (index16
0x0600 -> index 0x06 = EXC_ADDR).  The Ibox IPR table gives index 0x10 = PAL_BASE.

So CSERVE 0x66 = HW_MFPR PAL_BASE, then clear the low 21 bits: R0 = palBase & ~0x1FFFFF
(2 MB-aligned PAL base).  It is a PAL-BASE query for relocation / base-relative
address math, NOT elapsed time.  (PAL_BASE is the most-read MFPR in the PAL, 218x --
a base register, not a clock.)  This CONFIRMS the mechanism (get_time subroutine =
arg - cserve(0x66); EmulatR's no-op leaves the stale walk pointer in R0) while
correcting the SEMANTICS from "time" to "PAL base".

FIX (execCserve 0x66): R0 = cpu.palBase >> 21 << 21.  EmulatR already models it
(HW_IPR.h HW_PAL_BASE=0x110; CpuState.h:425 uint64 palBase; HW_MFPR HW_PAL_BASE
returns it) -- a faithful one-liner reusing existing infra.  DETERMINISTIC (palBase
is fixed once seeded), so no scale/units concern -- fits V4 better than a clock.
Because there is ONE 0x66 handler, both consumers share the contract: SCB path
base = R0 + 0x28000 = palBase_aligned + 0x28000 lands correctly, and the memtest's
arg - palBase_aligned is a valid address.  The 2026-07-08 regression returned a BCD
TOY (wrong source) instead of palBase -- that shifted the SCB base.  No open value
question remains: return palBase masked.

## RESULT (2026-07-11 16:20 cold run, fix landed) -- ACV CLEARED, boot advanced

Run traces/20260711-162023_es40_console.out (NOTRACE, MAXCYC 0x12000000):
  - The 0x1b7dd4 ACV is GONE: 0 hits on pc=0x1b7dd4 / VA 0xffffffff7f827f5f.
  - palBase = 0x8000 at runtime, so cserve 0x66 = 0x8000 & ~0x1FFFFF = 0; the
    memtest get_time = arg - 0 = arg (a valid address) -> the march completes.
  - Console progressed FAR past the old blocker:
      Memory size 4096 MB -> testing memory -> starting drivers -> entering idle
      loop -> initializing GCT/FRU at 3fc30000.
  - Fault landscape clean: all faults are <= cyc 250M (kFaultDtbMissDouble x63 =
    normal memtest paging churn, handled) plus the one known kFaultUnimplemented
    at pc=0x13f45 (op 0x1d HW_MTPR, pre-existing anomaly).  NOTHING after 282M.
  - Stop = MAXCYC cap (301,989,888), NOT a halt -- boot ran out of cycle budget
    mid GCT/FRU init, still making forward progress.

NEXT FRONTIER: raise MAXCYC (>= 0x40000000) to chase P00>>>; the new work is GCT/FRU
init and beyond, not the memtest.  Do-no-harm gate (doctest suite + DS10/DS20 P00)
still to confirm.  Fidelity note: palBase=0x8000 -> masked 0 is self-consistent with
where EmulatR runs the PAL; if a future layout puts palBase >= 2MB, cserve 0x66 will
return palBase_aligned as the handler specifies (no code change needed).

## Status

- Mechanism: CLOSED -- CSERVE 0x66 IS a real handler (PAL_BASE masked), NOT a no-op;
  machine-code confirmed from the image's own PAL dispatch (2026-07-11).
- Fix: LANDED + VALIDATED -- execCserve 0x66 = palBase>>21<<21; the ES40 memtest ACV
  is cleared and boot advances to GCT/FRU init (2026-07-11 16:20 run).
- Task #12 (make 0x66 a faithful get_time): OPEN on the VALUE only -- source the
  0x66 semantics + confirm the SCB consumer, then design the value, then edit,
  then a full cold run.

## Do-no-harm gate

The fix touches the PAL CSERVE dispatch (execCserve).  Gate any commit: full
suite + DS10 + DS20 + ES40 boot-to-P00 green.  Faithful rule: implement the real
0x66 return contract from the console source; do NOT fabricate a value tuned to
make the SUBQ come out right -- that risks re-baking the SCB regression.

## Artifacts / references

- Image: decompressed_es40_v7_3.bin (load base 0x8000; 0x3F6000 bytes).
  Disassembled seams: get_time 0x8C2D0-0x8C318; CSERVE wrapper 0x1B78F8
  (CALL_PAL 0x9); consumer 0x611B0; probe leaf 0x1B7DD4 (bare LDQ, no mask);
  result-to-address 0x5B03C.
- EmulatR seam [LOCATE]: palBoxLib/grains/PalEntries.cpp execCserve dispatch;
  CSERVE 0x66 falls to the default no-op (R0 untouched).
- Console source needed: the pc264 console handler for CSERVE 0x66 (past the
  generic ev6_vms_pc264_pal.mar / ev6_pc264_pal_defs.mar table).
- Prior journals reconciled here: 20260710_es40_memtest_acv_cserve_0x66_
  get_time_rootcause.md (CONFIRMED here by machine code);
  ..._trace_corrected_mechanism.md and ..._RESOLVED_aar_asiz_and_tiling.md
  (their 0x8C308 attribution superseded; AAR fix itself remains valid);
  20260708_es40_scb_base_mismatch_root.md (the SCB guardrail).
- Memory to correct: [[es40-srm-boot-status]] -- record CSERVE 0x66 as the
  confirmed surviving-fault root; note ECC is diagnostic-only, not the fix.
- Tasks: #6 (AAR, CLOSED), #12 (0x66 value design, OPEN).
