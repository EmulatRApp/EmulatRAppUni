<!--
EmulatR V4 -- VA_FORM 43/48-bit Segment Decode Fidelity Briefing
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree.
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Purpose: hand-off document for Cowork to close the VA_FORM composition
defect that drives the kFaultDtbMissDouble storm at DTBM_SINGLE+0x20
(PC 0x8321, VA 0x6c845000).  Deliverable is a DESIGN PLAN; Cowork turns
it into diffs, verifies exact file paths / line numbers, and runs the
gating capture BEFORE landing the edit.
Cowork is the source of truth for current file state; every path and
line below is to be confirmed against the live tree.
If committed as a tracked file, prepend the canonical ADR-0001 header
from docs/notes/templates/header_cpp.txt (this HTML comment is the
web-handoff header only).
ASCII(128) only.
-->

# VA_FORM 43/48-bit Segment Decode -- Fix Design Plan

## 1. What core found (confirmed against the HRM)

Two defects in the VA_FORM formatting helper (core inspected the
function that takes only `form32`; confirm exact home -- `VA_types.h`
defines the `vaCtl*` accessors and is the likely site, plus its two
call sites in the translate path):

Bug 1 -- no VA_48 branch.  The helper handles only the `form32` axis;
there is no 48-bit path.  `VA_types.h` documents VA_CTL[1] = VA_48 and
defines `vaCtlIsVa48()`, but neither call site passes it.  In 48-bit
mode the VPN field must be VA<47:13> at VA_FORM[37:3]; the helper
always places VA<42:13> at [32:3], truncating VA<47:43> and omitting
the SEXT(VA<47>) segment at [42:38].

Bug 2 -- VPTB / VPN field overlap (the storm cause).  Per HRM 5.1.5
Figure 5-5 the 43-bit form is:

    VA_FORM = VPTB<63:33> : VA<42:13> : 0<2:0>

The helper masks VPTB with 0xFFFFFFFFC0000000 (bits [63:30]) while
the VPN field occupies [32:3].  Bits [32:30] are therefore contributed
by BOTH operands.  The [63:30] mask is the VA_CTL *register* field
extent for VPTB -- correct as a field extractor, wrong as the VA_FORM
composition mask, which must be narrowed per mode.  The current helper
is a hybrid: form32 VPTB mask ([63:30]) combined with 43-bit VPN
placement ([32:3]).  Neither mode is fully correct.

Active-path assessment: for VA 0x6c845000 the VPN field is
(0x6c845000 >> 13) << 3 = 0x36422 << 3 = 0x1B2110, which reaches only
bit 20.  The 43-bit and 48-bit VPN slices are numerically identical for
this VA because VA<47:33> are zero.  So Bug 1 is LATENT here; Bug 2 is
the ACTIVE cause: if firmware VPTB has any of bits [32:30] set, the OR
displaces the PTE address by up to 0x1C0000000 onto an unmapped page,
producing the unresolvable double miss and the storm.

## 2. Authoritative HRM composition (all three modes)

Source: HRM 5.1.5, Figures 5-5 / 5-6 / 5-7.  VPTB is held in the
VA_CTL register at [63:30]; only a mode-dependent prefix feeds VA_FORM.
The VPTB prefix and the VPN field tile with no overlap in every mode --
the overlap only appears when the VPTB mask is too wide.

    Mode (VA_48, VA_FORM_32)  VA_FORM layout
    ------------------------  -------------------------------------------
    (0, 0)  43-bit default    VPTB<63:33> : VA<42:13>            : 0<2:0>
    (1, 0)  48-bit            VPTB<63:43> : SEXT(VA<47>)<42:38>
                                          : VA<47:13><37:3>      : 0<2:0>
    (0, 1)  32-bit form       VPTB<63:30> : 0<29:22> : VA<31:13><21:3>
                                                                 : 0<2:0>

Exact constants (all HRM-derived; hex radix):

    VPTB prefix masks
      43-bit : 0xFFFFFFFE00000000   (bits [63:33])
      48-bit : 0xFFFFF80000000000   (bits [63:43])
      32-bit : 0xFFFFFFFFC0000000   (bits [63:30])

    VPN field (index << 3), and resulting field extent
      43-bit : ((va >> 13) & 0x3FFFFFFF)  << 3   -> [32:3]  mask 0x1FFFFFFF8
      48-bit : ((va >> 13) & 0x7FFFFFFFF) << 3   -> [37:3]  mask 0x3FFFFFFFF8
      32-bit : ((va >> 13) & 0x7FFFF)     << 3   -> [21:3]  mask 0x3FFFF8

    48-bit SEXT(VA<47>) segment, bits [42:38]
      ((va >> 47) & 0x1) ? 0x7C000000000 : 0x0

Undefined combo: (VA_48=1 AND VA_FORM_32=1) is not an architected VA_FORM
figure.  Per the project's hard-stop-over-silent-degradation rule, treat
it as a loud failure (assert / kFaultHalt with a diagnostic), not a
silent fall-through into either branch.

## 3. Gating diagnostic -- run BEFORE any edit (empirical-trace-before-fix)

The mask fix is only the storm fix if firmware VPTB has bits [32:30]
set.  Add a bounded, compile-gated capture (CMake compile guard, not a
runtime-only path) at the DTBM_SINGLE LD_VPTE.  Cap at ~32 hits then
disarm -- this is a storm; an ungated dump wedges the sandbox.  Record
per hit:

  1. Live VPTB value as read by the helper (the raw operand it masks).
  2. VA_CTL[VA_48] (bit 1) and VA_CTL[VA_FORM_32] (bit 2).
  3. Which double-miss vector the trap dispatched through
     (palBase+0x100 = DTBM_DOUBLE_3 => 43-bit; palBase+0x180 =
     DTBM_DOUBLE_4 => 48-bit).  This is an independent confirm of the
     VA_48 state.
  4. cpu.va (must read 0x6c845000 -- the ORIGINAL faulting VA; HRM
     5.1.3 / 5.3.8: VA and MM_STAT are NOT written on an LD_VPTE miss,
     so if cpu.va shows the VPTE address instead, that is a separate
     preservation bug in the double-miss delivery path).
  5. The helper's current VA_FORM output vs. the HRM-correct value.

Discrimination table:

    VPTB operand      -> conclusion
    ----------------     ---------------------------------------------
    0x0               -> WRONG SOURCE, not a mask bug.  The helper is
                         reading cpu.vptb (CALL_PAL MTPR_VPTB copy,
                         zero at boot) instead of VA_CTL<63:30> as
                         written by firmware HW_MTPR VA_CTL.  Fix the
                         source wiring first; the mask fix is moot.
    nonzero, [32:30]  -> BUG 2 CONFIRMED.  Mask fix (Section 4) clears
      set                the storm.
    nonzero, [32:30]  -> mask change is a no-op here.  Re-examine: is
      clear              VA_48 set (Bug 1 on the active path after all)?
                         Is the DTB fill in the double handler failing
                         to install?  Do not land the mask fix as "the"
                         storm fix on this branch.

Only after this capture identifies the branch do we land the
corresponding change.

## 4. Fix design

### 4.1 Signature and dispatch

Change the helper to take both mode bits, sourced from VA_CTL via the
existing `vaCtlIsVa48()` / form32 accessors -- do not add a second
independent copy of these bits.  Proposed shape (place / rename per the
live signature; hand-written leaf uses the `auto X() -> Y` form per the
codegen differentiator convention):

    auto vaFormAddress(uint64_t va, uint64_t vaCtl) noexcept -> uint64_t
    {
        bool const form32 = vaCtlIsForm32(vaCtl);   // VA_CTL<2>
        bool const va48   = vaCtlIsVa48(vaCtl);     // VA_CTL<1>

        if (form32 && va48) {
            // Unarchitected combo -- hard stop, do not guess.
            // (assert in debug; kFaultHalt + diagnostic in release)
        }

        if (form32) {                               // (0,1) 32-bit form
            return (vaCtl & 0xFFFFFFFFC0000000ULL)
                 | (((va >> 13) & 0x7FFFFULL) << 3);
        }
        if (va48) {                                 // (1,0) 48-bit
            uint64_t const sext =
                ((va >> 47) & 0x1ULL) ? 0x7C000000000ULL : 0x0ULL;
            return (vaCtl & 0xFFFFF80000000000ULL)
                 | sext
                 | (((va >> 13) & 0x7FFFFFFFFULL) << 3);
        }
        // (0,0) 43-bit default -- the boot path.
        return (vaCtl & 0xFFFFFFFE00000000ULL)
             | (((va >> 13) & 0x3FFFFFFFULL) << 3);
    }

Passing `vaCtl` whole (rather than a pre-masked vptb) removes the
field-extent-vs-composition confusion that produced Bug 2: the single
correct mask lives inside the helper, per mode.

### 4.2 Call sites

Both call sites core identified currently pass only form32.  Change
each to pass the live VA_CTL register value (or the mode bits derived
from it), and confirm both read the SAME VA_CTL that firmware programs
via HW_MTPR VA_CTL -- not a stale mirror.

### 4.3 VPTB source confirm (folds in the open item)

This closes the earlier open question of whether VA_FORM reads
VA_CTL<63:30> (hardware, HW_MTPR VA_CTL) or cpu.vptb (CALL_PAL
MTPR_VPTB).  The Section 3 capture answers it directly; if the operand
is zero, the source is the bug and Section 4.1's mask work is deferred
until the source is corrected.

## 5. PROVISIONAL / TODO discipline

- 43-bit branch: HRM-confirmed AND trace-confirmable now (storm clears).
  Not provisional once the Section 3 capture shows [32:30] set.
- 48-bit and 32-bit branches: HRM-confirmed (Figures 5-6 / 5-7) but not
  yet exercised by a real trace.  These are not "guessed" values, so
  the `_PROVISIONAL` suffix (reserved for unverified scbd/offset/bit
  guesses) does not apply; instead tag them so their unexercised status
  is greppable:
    - file header TODO table row:
      `TODO(vaform-48-trace) : 48-bit / form32 VA_FORM branches HRM-derived, not yet exercised by a VA_48 boot trace`
    - call/branch site:
      `// TODO(vaform-48-trace): HRM Fig 5-6/5-7, no execution-trace confirmation yet`
  Remove both in the same edit that lands a VA_48-exercising trace.

## 6. Test / assertion plan (doctest CHECK only; exceptions disabled)

Field-disjointness invariant (guards against a future re-introduction
of the overlap): for each mode, assert (vptb_prefix_mask &
vpn_field_mask) == 0 and (in 48-bit) that the SEXT segment mask is
disjoint from both.

    CHECK((0xFFFFFFFE00000000ULL & 0x1FFFFFFF8ULL)  == 0x0ULL); // 43
    CHECK((0xFFFFF80000000000ULL & 0x3FFFFFFFF8ULL) == 0x0ULL); // 48 vpn
    CHECK((0xFFFFF80000000000ULL & 0x7C000000000ULL)== 0x0ULL); // 48 sext
    CHECK((0x3FFFFFFFF8ULL       & 0x7C000000000ULL)== 0x0ULL); // 48 both
    CHECK((0xFFFFFFFFC0000000ULL & 0x3FFFF8ULL)     == 0x0ULL); // 32

Golden vectors:

  43-bit regression (the storm).  Stress VPTB with bits [32:30] set to
  prove the mask discriminates:
    va    = 0x6c845000
    vaCtl = 0xFFFFFFFFC0000002  (VPTB<63:30> all ones; VA_48=1? no --
            keep bit1=0, bit2=0; the trailing 2 shown only illustrates
            VA_48 placement -- use 0xFFFFFFFFC0000000 for the pure
            43-bit case)
    For vaCtl = 0xFFFFFFFFC0000000 (43-bit, VPTB[32:30] set):
      expected VA_FORM = 0xFFFFFFFE001B2110
      old/buggy output = 0xFFFFFFFFC01B2110   (differs by 0x1C0000000)
    CHECK(vaFormAddress(0x6c845000ULL, 0xFFFFFFFFC0000000ULL)
          == 0xFFFFFFFE001B2110ULL);

  43-bit with the ACTUAL firmware VPTB captured in Section 3: compute
  the golden value by hand from the figure and add a CHECK, so the fix
  is pinned to the real boot state, not only the synthetic stress case.

  48-bit VPN placement + SEXT (TODO(vaform-48-trace)-tagged): pick a VA
  with VA<47> set to exercise the sign-extension segment, hand-compute
  the golden VA_FORM from Figure 5-6, and CHECK it.

  32-bit form: VA with bits above 31 set, confirm they are dropped
  (VPN masks to VA<31:13>), CHECK against Figure 5-7.

Boot-level: after landing, the DTBM_SINGLE storm at VA 0x6c845000 must
clear -- the LD_VPTE resolves to a mapped VPTE page, the single-miss
handler installs the final PTE (HW_MTPR DTB_TAG/DTB_PTE), and the
original access at 0x6c845000 retires.  Confirm forward progress past
0x8321 in a bounded post-fix capture.

## 7. Convention checklist for the landing edit

- Surgical Edit of the existing helper + its two call sites; no
  whole-file rewrite.  V0/V1/V2 and Processor Support remain read-only.
- Header block on each touched file in the FILE N / FUNCTION / CHANGE
  style, plus an inline comment at each changed line referencing it.
- ASCII(128) only.  Hex radix on all constants (already hex above).
- doctest CHECK only.  If a new test file is added, include guard
  `#ifndef <DIR>_<FILE>_H` form, never `#pragma once`.
- Compile-gated diagnostic (Section 3) behind a CMake compile guard,
  with the runtime env gate (if any) kept distinct from the compile
  guard; expands to `((void)0)` in Release.
- Qt not involved here; this is a std-only arithmetic path.

## 8. Back to the architect / open confirms

1. Section 3 capture result (VPTB operand + VA_48 + double-miss vector)
   -- this selects the branch and is the go/no-go for the mask fix.
2. Exact path/line of the VA_FORM helper and its two call sites.
3. Confirm the VPTB operand is VA_CTL<63:30> as programmed by firmware
   HW_MTPR VA_CTL, not the cpu.vptb CALL_PAL mirror.
4. Confirm cpu.va still reads 0x6c845000 during the storm (no LD_VPTE
   VA/MM_STAT clobber in the double-miss delivery path).

## 9. TRACE RESULTS AND CORRECTIONS (2026-07-03, post-capture)

The Section 3 gating capture was run (mac-diag, EMULATR_BRINGUP_PROBES,
storm-window gate cycleCount >= 250M, cap 64, probe at the HW_VA_FORM /
HW_IVA_FORM read in PalEntries.cpp).  It inverted two predictions.  The
fix has since been landed (computeVaForm three-form, coreLib/IprFields.h;
both call sites in PalEntries.cpp pass raw va_ctl / iCtlVptb(i_ctl) +
va48; regression tests/coreLib/test_iprfields.cpp).

Captured (all 64 hits identical on the control bits):

    va_ctl = 0x02  ==>  VA_48 = 1, VA_FORM_32 = 0, VPTB<63:30> = 0
    pc     = 0x8305 (the VA_FORM read inside the miss handler)
    representative:  va = 0x0000_0801_03fb_2000  (48-bit kseg, VA<47>=0)
        buggy VA_FORM = 0x40FEC8      (43-bit mask, bit 33 dropped)
        HRM-correct   = 0x2_0040_FEC8 (48-bit VA<47:13>->[37:3])

Corrections to Sections 1-3:

- BUG 1 IS THE ACTIVE CAUSE, not Bug 2.  Section 1 predicted Bug 2 (VPTB
  overlap) active and Bug 1 latent, on the assumption the boot ran 43-bit
  with a nonzero VPTB.  The SRM actually runs VA_48=1, so the 43-bit VPN
  mask [32:3] truncated VA<47:33> of every self-map VPTE address.  For the
  captured VAs bit 33 is the lost bit.

- BUG 2 DID NOT FIRE.  VPTB = 0, so there were no bits [32:30] to leak;
  the overlap is real per the HRM but inactive on this boot.  The fix's
  per-form VPTB masks close it anyway (latent-correct).

- Discrimination table (Section 3) needs a correction.  It read "VPTB
  operand 0x0 -> WRONG SOURCE (helper reading cpu.vptb, not VA_CTL)".
  That is NOT what happened: the helper reads va_ctl, and va_ctl itself
  is 0x02 -- i.e. firmware's last HW_MTPR VA_CTL genuinely wrote VA_48=1
  with VPTB=0 (HW_MTPR VA_CTL stores unmasked: `c.cpu->va_ctl = c.opB`).
  So VPTB=0 here is a REAL programmed state, not a source-wiring bug.

- VPTB=0 SUFFICIENCY -- explicit post-fix gate (do not assume).  Even with
  a correct VA_FORM, VPTB=0 yields VPTE address = 0 | VA_FORM_low, a low
  address that may not be where the page tables live.  Re-arm the SAME
  64-capture window after the fix:
    * corrected VPTE loads a valid PTE and the walk converges -> storm
      clears -> VPTB=0 was the intended state.  DONE.
    * still storms on a now-correctly-computed-but-unmapped VPTE -> VPTB
      SEEDING is a SECOND defect stacked behind this one (see below).

- SECOND DEFECT (latent, deferred review): CALL_PAL MTPR_VPTB is
  intercepted as a C++ intrinsic (execMtprVptb: `c.cpu->vptb = R16` and
  nothing else).  On real EV6 the MTPR_VPTB PALcode propagates VPTB into
  VA_CTL<63:30> (D-side) and I_CTL (I-side) via HW_MTPR.  V4 does the
  storage half (cpu.vptb) and drops the propagation half, so any OS/SRM
  VPTB set through MTPR_VPTB is stranded in cpu.vptb where VA_FORM never
  looks.  If the sufficiency gate shows an unmapped-VPTE storm, THIS is
  the next fix (propagate MTPR_VPTB into va_ctl/i_ctl, or have VA_FORM
  fall back to cpu.vptb when va_ctl VPTB is zero -- decide by fidelity).

- SEXT(VA<47>) segment: trace-confirmed for VA<47>=0 only (all captured
  VAs have bit 47 clear, so SEXT contributes 0 and the two-field and
  three-field results coincide).  The VA<47>=1 half is guarded by doctest
  vector V2 (predicted, 0x7E20040FEC8), NOT yet by a live trace -- keep
  the TODO(vaform-48-trace) note for the SEXT half only; the VPN-width
  half of the 48-bit branch is now trace-confirmed.

- Cross-platform (DS10/DS20) re-attribution.  DS silence is NOT the Bug-2
  [32:30] story (Bug 2 never fired).  DS is quiet because it either never
  runs VA_48 mode at >>> or never drives a self-map VPTE load in 48-bit
  mode.  The DS comparison probe should key on va_ctl[1] (VA_48), not on
  the VPTB bits: 43-bit => the fix is latent-correct there; 48-bit-but-
  untested => the fix is active and DS was quietly lucky.
