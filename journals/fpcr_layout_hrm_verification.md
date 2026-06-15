<!--
EmulatR V4 -- FPCR Layout HRM Verification (de-PROVISIONAL reference)
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-10
Purpose: HRM-verified FPCR bit layout + a checklist Cowork applies against
the live coreLib/alpha_fpcr_core.h to remove its PROVISIONAL marker and
unblock the FpExc -> FPCR fold and the trap decision (shouldRaiseFPTrap /
getExceptionSummary). Sources are project reference docs (cited inline by
name). ASCII(128) only.
-->

# EmulatR V4 -- FPCR Layout HRM Verification

## 0. What this is

The FPCR bit layout was carried as PROVISIONAL in `alpha_fpcr_core.h`,
which is why `shouldRaiseFPTrap` / `getExceptionSummary` were deferred and
why the `FpExc -> FPCR` fold is not yet bound. This document is the
verification: the layout cross-checked against three sources, the EV6
deviations called out, and a constants checklist Cowork applies to the live
header. Once the header matches this, the PROVISIONAL marker comes off and
the fold can be written.

Sources (all in the project tree):
- `alpha_arch_ref.txt` -- common-architecture FPCR, Table 4-11 / Figure 4-1.
- Alpha 21264/EV67 HRM -- 21264-specific FPCR, Table 2-14 / Figure 2-11;
  Exception Summary Register, Table 5-9 / Figure 5-20.
- `EV6_Specification_Rev_2.0_199604.txt` -- section 6.1 FPCR.

All three agree on bit positions. The one substantive divergence is an EV6
implementation-subset detail (DNOD), called out in section 3.

## 1. Verified FPCR bit layout (architectural)

    Bit     Field   Type   Meaning when set
    63      SUM     RW     Summary. = OR(FPCR<57:52>). Not directly written;
                           changes indirectly when 57:52 change.
    62      INED    RW     Inexact Disable.
    61      UNFD    RW     Underflow Disable.
    60      UNDZ    RW     Underflow to Zero.
    59:58   DYN     RW     Dynamic Rounding Mode (see section 2).
    57      IOV     RW     Integer Overflow (CVTGQ/CVTTQ/CVTQL).
    56      INE     RW     Inexact Result.
    55      UNF     RW     Underflow.
    54      OVF     RW     Overflow.
    53      DZE     RW     Division by Zero.
    52      INV     RW     Invalid Operation.
    51      OVFD    RW     Overflow Disable.
    50      DZED    RW     Division by Zero Disable.
    49      INVD    RW     Invalid Operation Disable.
    48      DNZ     RW     Denormal Operands to Zero.
    47      DNOD    RW     Denormal Operand Exception Disable.
                           *** NOT implemented on EV6 -- see section 3. ***
    46:0    --      --     RAZ / IGN (reserved). On EV6: 47:0 reserved.

The six exception bits are <57:52> in the order IOV, INE, UNF, OVF, DZE,
INV. Per the arch ref, these match in purpose and order the exception bits
in the arithmetic-trap exception-summary quadword (section 5).

## 2. DYN rounding encoding (constants-critical)

    DYN<59:58>   IEEE rounding mode
    00           Chopped (round toward zero)
    01           Minus infinity
    10           Normal (round to nearest, ties to even)
    11           Plus infinity

This encoding is what the constants in `alpha_fpcr_core.h` must match
exactly, because `deriveLocalFpcr` writes `local |= (rm << DYN_RM_SHIFT)`.

## 3. EV6 / 21264 deviations (the fidelity findings)

### 3.1 DNOD (bit 47) is NOT implemented on EV6
The EV67 HRM Table 2-14 states the reserved field is <47:0> and footnotes:
architecture FPCR bit 47 (DNOD) is not implemented by the 21264/EV67.
Consequence for EmulatR (an EV6 target):
- Treat bit 47 as reserved (RAZ/IGN). Do NOT define a honored DNOD bit and
  do NOT let any denormal-operand path consult it.
- Denormal-operand handling on EV6 is governed by DNZ (bit 48) plus the /S
  qualifier only. With DNZ set, a denormal operand is treated as a signed
  zero (no INV trap for that reason); DNOD plays no role.
Mark any DNOD reference in `alpha_fpcr_core.h` for removal.

### 3.2 EV6 cannot generate IEEE-compliant denormal results
The EV67 HRM (UNFD/UNDZ description) states the hardware cannot produce
IEEE denormal results, and routes underflow as:

    UNFD  UNDZ   Result
    0     X      Underflow trap.
    1     0      Trap to supply a possible denormal result.
    1     1      Underflow trap suppressed; destination = true zero (+0.0).

Design decision for EmulatR (DECIDE / `[CONFIRM]`): the SoftFloat backend
CAN produce correct denormals. Two faithful options:
  (a) Model EV6 behavior: on underflow with traps not fully suppressed,
      route to the software-completion path (this is what /S exists for)
      rather than silently returning a denormal.
  (b) Produce the correct denormal directly (more correct than the silicon)
      and DOCUMENT the deviation from EV6 hardware behavior.
This is a policy-layer choice, not a layout fact. Decide before binding the
underflow path; name the trade-off in the spec per house rule.

## 4. Constants checklist for alpha_fpcr_core.h

Cowork verifies the live header holds exactly these (claims verified against
live source; flag if a line has drifted):

- `DYN_RM_SHIFT` == 58.
- `DYN_RM_MASK`  == (0x3ULL << 58)  (bits 59:58).
- Rounding encodings, EXACT values (a digit error silently corrupts
  rounding):
      RM_CHOPPED   == 0
      RM_MINUS_INF == 1
      RM_NORMAL    == 2
      RM_PLUS_INF  == 3
- `EXC_MASK` == bits 57:52 == (0x3FULL << 52). These are the six sticky
  exception bits `deriveLocalFpcr` clears per op.
- SUM (bit 63): confirm how the header models it. If SUM is a stored bit,
  it must be recomputed as OR(57:52) on any change (so clearing EXC_MASK
  also yields SUM==0); if SUM is computed on read, it is not stored. Either
  is fine; pick one and be consistent.
- DNOD: confirm NO honored DNOD bit exists (section 3.1). Bit 47 reserved.

Note on `deriveLocalFpcr`: the local-FPCR exception clear (`local &=
~EXC_MASK`) isolates the DYN field for the op; the op's ACTUAL exception
flags come from SoftFloat's `softfloat_exceptionFlags` (cleared before each
call) and are folded into the architectural FPCR ABOVE the backend. Do not
confuse the local-FPCR clear with SoftFloat's flag word -- they are separate
mechanisms. The local FPCR exists mainly to carry the correct DYN bits when
the op uses /D.

## 5. Exception semantics that govern the fold

From the arch ref, binding rules for the FpExc -> FPCR fold:
- Sticky bits <57:52> are set INDEPENDENT of trapping mode. Even when a
  trap is disabled for a condition, the condition is still recorded in FPCR.
  So: always OR the raw FpExc bits into FPCR<57:52>; the trap-enable
  decision is separate and does not gate the sticky record.
- Hardware only transitions these bits 0 -> 1. They are cleared ONLY when
  software writes zero via MT_FPCR. The fold must never clear a sticky bit
  as a side effect of an op.
- IOV (bit 57) is set only by integer-producing conversions (CVTTQ/CVTGQ/
  CVTQL) on overflow -- map FpExc.iov there, not for ordinary arithmetic.
- It is UNPREDICTABLE whether VAX-only operates set the FPCR exception bits;
  CVTQL (both subsets) and all IEEE operates do set them. Do not rely on VAX
  operates setting them.

## 6. Companion: Exception Summary Register (for deferred trap delivery)

Not part of the FPCR, but the structure the arithmetic-trap path populates,
captured here so the deferred `getExceptionSummary` work has the verified
layout. From the EV67 HRM Table 5-9 / Figure 5-20:

    Bits     Field            Meaning
    63:48    SEXT(SET_IOV)    Sign-extension of bit 47.
    47       SET_IOV          PALcode should set FPCR[IOV].
    46       SET_INE          PALcode should set FPCR[INE].
    45       SET_UNF          PALcode should set FPCR[UNF].
    44       SET_OVF          PALcode should set FPCR[OVF].
    43       SET_DZE          PALcode should set FPCR[DZE].
    42       SET_INV          PALcode should set FPCR[INV].
    41       PC_OVFL          EXC_ADDR sign-extension issue (48-bit mode).
    ...
    13       BAD_IVA          Bad Istream VA.
    12:8     REG[4:0]         Dest/source register of the trapping instr.
    7:0      arithmetic-trap parameter low byte (see below).

Arithmetic-trap parameter low byte <7:0> (architectural order; the figure
labels bit 3 "FOV", read as OVF):
    0  SWC   software completion (the /S qualifier was present)
    1  INV
    2  DZE
    3  OVF
    4  UNF
    5  INE
    6  IOV
    7  INT   (integer overflow enable / INT)
`[CONFIRM]` the exact low-byte bit positions against the arch ref EXC_SUM
description when trap delivery is actually wired -- they are captured here
for reference, not yet bound.

## 7. MT_FPCR / MF_FPCR ordering (for when those grains are wired)

Not a layout item, but a correctness note so it is not lost:
- Architecturally, an EXCB must bracket FPCR access (EXCB before and after
  MT_FPCR/MF_FPCR) to synchronize with overlapping FP instructions.
- EV6 spec note 12.19: in PALmode, use HW_RET/STALL after a nontrapping
  MT_FPCR to get the minimum (4-cycle) latency before the first FLOP that
  uses the updated FPCR.
In V4's deterministic, in-order-ish model the synchronization is largely
moot, but MT_FPCR's effect must be ordered at the correct stage so a later
op sees the new DYN/enable bits. Honor this when those grains land.

## 8. What this unblocks / what remains deferred

Unblocked once the section 4 checklist passes:
- Remove the PROVISIONAL marker from `alpha_fpcr_core.h`.
- Write the `FpExc -> FPCR<57:52>` sticky fold (section 5 rules).

Still deferred (separate work, depends on trap delivery):
- `shouldRaiseFPTrap` (trap-enable policy: which raw flag, under which
  qualifier/disable bit, becomes an actual arithmetic-trap delivery vs a
  sticky-only record).
- `getExceptionSummary` (populate the section 6 structure).
- The denormal-result design decision (section 3.2).

## 9. Deliverables expected from Cowork

1. Apply the section 4 checklist to the live `alpha_fpcr_core.h`; report any
   constant that does not match, with file + line.
2. Remove any honored DNOD bit (section 3.1); bit 47 reserved.
3. Once the checklist passes, remove the PROVISIONAL marker and propose the
   `FpExc -> FPCR` sticky-fold edit (discuss-before-code) per section 5.
4. Record the section 3.2 denormal decision as a flagged design item.

---

## Standing EmulatR V4 rules (apply to all implementation work)

### Workflow
- Discuss before code. Non-trivial changes are proposed first as prose with
  file paths, line numbers, and the concrete edit shape; wait for approval
  before editing. Exception: trivial typo/formatting fixes the user pointed
  out.
- Documentation at header and source line. Every source change updates a
  header block ("FILE N: ... FUNCTION: ... CHANGE: ..." style) and leaves an
  inline comment at the changed line. No anonymous changes.
- TODO discipline. Incomplete-on-purpose code documented at the file header
  (named TODO table) and the call site (// TODO(<tag>): <summary>); greppable
  tags removed in the same edit that lands the wiring.
- Best-effort deterministic architecture. Name any determinism trade-off in
  the spec.

### File / source conventions
- ASCII(128) only in all file content -- no smart quotes, em dashes, Unicode
  arrows, or box-drawing glyphs.
- Copyright/attribution header on every generated source/header (Markdown
  specs as an HTML comment) per docs/notes/ADR-0001-source-file-headers.md.
- Include guards, never #pragma once; pattern #ifndef <DIR>_<FILE>_H.
- Hex radix for all switch/case dispatch labels; convert dec->hex by value
  (16 -> 0x10), never digit substitution.
- Prefer surgical Edit over whole-file rewrites; V0/V1/V2 and Processor
  Support are read-only.

### C++ / build specifics
- doctest: CHECK only, never REQUIRE (exceptions disabled in V4).
- Never name an enum's printable helper toString (doctest ADL clash) -- use
  <typeName>Name(T) plus operator<<.
- Hand-written leaves use auto X() -> Y; codegen emits direct Y X(); change
  genGrains.py, not generated output.

### Data fidelity
- Provisional FPCR-layout / IPR values OK for C1 storage but never for C2
  decode -- mark _PROVISIONAL and HRM-verify before any dispatch, decode, or
  FPCR fold matches them. (This document is that verification for the FPCR
  layout.)

### Trace / debug discipline
- Multi-GB traces: bounded tails / gated windows only, never whole-file grep.
- Verify every file write via bash (wc -l / grep); prefer heredoc for large
  writes.

### Collaboration
- claude.ai web for analysis/design, Cowork for the edits. Claude web
  provides the instructional design; Cowork implements against the live tree.
