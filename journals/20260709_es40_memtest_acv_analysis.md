<!--
EmulatR V4 -- ES40 memory-test ACV on 0xFFFFFFFF_7F827F5F: web-variant
DESIGN analysis + next-capture spec (2026-07-09).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix throughout.
Discuss-before-code stands. This is an analysis + capture spec, not code.
[LOCATE] line numbers are from the point-in-time snapshot; Cowork verifies
against the live tree before editing.
-->

# ES40 memtest ACV on 0xFFFFFFFF_7F827F5F -- root-cause analysis (2026-07-09)

## Verdict

The briefing's binary was: (A) Ev6Translator wrongly denies a mapped VA, vs
(B) 0xFFFFFFFF_7F827F5F is a wild pointer and the ACV is correct.

The answer is neither as posed. It is a third, more specific root that lies
between them:

  The PAL self-map walk is anchored at VPTB = 0.  The PTE address it forms is
  correct in every bit-field EXCEPT the virtual-page-table base, which is
  zeroed.  So the walk targets a region the SRM never mapped, finds no valid
  PTE, and delivers ACV.  The VA is legitimate and mapped by the SRM; V4 never
  reaches its PTE.

This is the already-confirmed VPTB-propagation defect, not a wild pointer and
not a leaf/protection-check bug.  The ACV is a fourth-order symptom.

Confidence: high on the fingerprint (below); the single open contingency is
whether cpu.vptb is itself zero (SRM never programmed VPTB).  The capture in
this document closes that contingency before any edit.

## Decisive evidence: the self-map fingerprint

HRM 21264 section 5.1.5, Figure 5-6 (VA_48 = 1, VA_FORM_32 = 0 -- the active
mode per va_ctl = 0x02) defines the hardware VA_FORM:

    VA_FORM[63:43] = VPTB[63:43]
    VA_FORM[42:38] = SEXT(VA[47])
    VA_FORM[37:3]  = VA[47:13]
    VA_FORM[2:0]   = 0

Compute VA_FORM for the faulting VA with VPTB = 0 and compare to the observed
kFaultDtbMissDouble VA (the self-mapped PTE address the PAL walk used):

    faulting VA          = 0xffffffff7f827f5f
    VA_FORM(VPTB = 0x0)  = 0x7ffffdfe098    (computed)
    observed 2x-miss VA  = 0x7ffffdfe098    (faults.log, PC 0x8321, pal=1)
    -> EXACT match.

Field-by-field breakdown of the observed self-map address 0x7ffffdfe098:

    [63:43] VPTB slot   = 0x0          WRONG -- should carry the SRM VPTB
    [42:38] SEXT(VA47)  = 0x1f         correct (VA[47] = 1)
    [37:3]  VA[47:13]   = 0x7fffbfc13  correct -- matches the faulting VA
    [2:0]               = 0            correct

Reading: the VA_FORM low-field arithmetic (SEXT + VA[47:13]) is already
correct.  The ONLY corrupted field is VPTB[63:43], which is zero.  That is the
exact signature of "VPTB stored in cpu.vptb but never propagated into
VA_CTL[VPTB], and VA_FORM reads VPTB from VA_CTL."

## Why not a wild pointer (rebuts option B)

- Canonical: VA[63:48] = 0xffff, VA[47] = 1 -> well-formed negative
  (system-space).  Rules out kFaultNonCanonical.
- Superpage-excluded, so a walk is architecturally required (briefing's SPE
  claims verified):
    VA[47:46] = 0b11  (SPE2 wants 0b10)  -- no
    VA[47:41] = 0x7f  (SPE1 wants 0x7e)  -- no
    VA[47:30] = 0x3fffd, not all-ones (SPE0 wants all-ones), bit31 = 0 -- no
- The faulting VAs stride +8 (0x7f827f5f, 0x7f827f67, 0x7f827f6f ...) with R0
  doubling -- a deliberate address-line test.  The SRM intends this VA as
  mapped RAM.  R16 is not garbage.

## Why not the leaf/protection check (refines option A)

Ev6Translator's canRead / protection logic is not the active fault.  The walk
never fetches the SRM's real PTE for this VA -- it reads from the VPTB=0
self-map region instead.  Whatever byte the up-level walk resolves there is not
the leaf PTE, so the "no-valid / protection-deny" verdict is a verdict about
the wrong data.  Editing the protection check or canonicalFromDtbPte would be
the wrong seam and risks a DS10/DS20 regression for no gain.

## Reconciliation with yesterday's HOOKB dump

Yesterday's probe (pte = 0x403bfc1300000001, valid=1, kre=0,
pfn = 0x403bfc13, PA bit<43> set = Pchip PCI-I/O window) is the same root seen
one level deeper.  pfn 0x403bfc13 matches the faulting VPN (VA>>13) in its low
20 bits (0xbfc13) -- a 2^-20 coincidence that is not a real leaf PTE but
VA-derived page-table structure read out of a VPTB=0 self-map.  This answers
the briefing's open question: the walk DOES resolve toward a Pchip PA, so the
"physical Pchip wall" and this "virtual system-space ACV" are the same wall
seen from two sides -- both downstream of VPTB = 0.

## Grounding in the live tree ([LOCATE] -- Cowork verify)

- PalEntries.cpp:1242-1247  execMtprVptb: `cpu.vptb = R16` only; no propagation
  into VA_CTL[VPTB] (D-side) or I_CTL (I-side).  This is the defect seam.
- PalEntries.cpp:362-363    HW_VA_FORM read case returns 0 in the snapshot.
  If the live build still stubs this, and the guest PAL reads HW_MFPR
  HW_VA_FORM to form the PTE address, that is an alternate/additional seam for
  the VPTB=0 result.  The capture disambiguates which link is broken.
- PalEntries.cpp:562-576    DTB_PTE0/1 fill via canonicalFromDtbPte(opB).
  Downstream of the walk; currently innocent (installs faithfully whatever the
  walk hands it).  Do not touch in this pass.
- HRM VA_FORM figures: Alpha_21264-EV67 HRM section 5.1.5 (Fig 5-5 VA_48=0,
  Fig 5-6 VA_48=1, Fig 5-7 VA_FORM_32=1) -- the three modes the VA_FORM fix
  must cover, VPTB field width is mode-dependent.  VA_CTL[VPTB] field bounds
  are _PROVISIONAL until read off the VA_CTL register figure.

## Next capture (cheapest-decisive-first; Cowork runs)

One gated probe, keyed on the faulting PAGE, discriminates every branch.  Key
on VA & ~0x1fff == 0xffffffff7f826000 (the 8 KiB page containing the stride),
capture the FIRST hit only.  All probes under compile guard
EMULATR_BRINGUP_PROBES, zero-cost in release, with a runtime mute knob.

S1 (static, free): confirm in source whether the guest PAL DTBM handler forms
    the PTE address by reading HW_MFPR HW_VA_FORM, or computes it from a
    PAL-managed VPTB register.  This tells us which V4 seam feeds the walk.

D1 -- VPTB provenance (most decisive).  Add a tiny diag ring in execMtprVptb
    that records {cyc, pc, R16} on every CALL_PAL MTPR_VPTB (0x2A).  At the
    first page-matched ACV, dump:
      - cpu.vptb                       (last value MTPR_VPTB stored)
      - VA_CTL full value, VA_CTL[VPTB] field, VA_48, VA_FORM_32 bits
      - the last MTPR_VPTB ring entry (or "none seen")
    Decisive read:
      cpu.vptb != 0 AND walk used VPTB=0  -> propagation defect CONFIRMED.
      cpu.vptb == 0 AND a 0x2A was seen   -> SRM set VPTB=0 (investigate SRM
                                             state / premature probe).
      cpu.vptb == 0 AND no 0x2A ever seen -> either SRM has not set VPTB yet
                                             (upstream / premature) OR V4 is
                                             not dispatching 0x2A to
                                             execMtprVptb (dispatch bug).

D2 -- VA_FORM value.  At the same hit, log the HW_VA_FORM value V4 computes /
    returns for the faulting VA, plus the recomputed VA_FORM using cpu.vptb
    (the SRM's intended base).  Show they differ and that the cpu.vptb-based
    address lands in a different (mapped) region than 0x7ffffdfe098.

D3 -- terminal PTE.  At the walk's terminal step for this VA, capture the raw
    8-byte word loaded (the candidate PTE), decode valid / KRE / KWE / FOR, and
    the PFN/PA it points to.  Confirms whether the terminal read lands in RAM
    or Pchip PA (the "same wall" question) and whether ACV came from no-valid
    or from protection.

Instrumentation seam: gate inside the V4 translate/HW_VA_FORM path where the
PTE address is formed and where kFaultAcv is selected -- NOT EMULATR_GMEM_WATCH
(the walk read is a page-table load, but the point of interest is the address
formation, which GMEM_WATCH cannot see).

## Decision tree from the capture

1. cpu.vptb != 0, walk used VPTB=0
   -> VPTB-propagation defect (primary).  Fix: propagate VPTB into VA_CTL[VPTB]
      (D-side) and I_CTL (I-side) at execMtprVptb, matching the HW_MTPR path
      the real MTPR_VPTB PAL handler performs.  Re-run; expect the self-map to
      land in the real VPT window, the PTE to be found valid + KRE, DTB fill,
      LDQ retry succeeds, memtest advances.  The VA_FORM three-mode formula fix
      is a follow-on fidelity item, sequenced AFTER VPTB is confirmed nonzero.

2. cpu.vptb == 0, no MTPR_VPTB (0x2A) ever dispatched
   -> Sub-discriminate: is 0x2A reaching execMtprVptb at all (dispatch table),
      or has the SRM simply not run its VPTB setup yet (probe is premature ->
      the real work is upstream, tracing R16 in the decompiled SRM)?  A grep of
      the CALL_PAL dispatch for 0x2A plus a boot-wide MTPR_VPTB count settles
      this.

3. cpu.vptb != 0 AND the walk actually used it (nonzero VA_FORM[63:43]),
   PTE found but KRE=0
   -> Only then is it the leaf/protection or a genuine no-access SRM mapping
      (yesterday's B/C).  Fall back to the source-PTE-vs-installed-PTE compare.
      The 2026-07-09 fingerprint makes this branch unlikely for this run.

## Fix sequencing and do-no-harm

- VPTB propagation touches the shared MMU path.  Gate any commit on the full
  suite + DS10 + DS20 + ES40 boot-to-P00 green (standing rule).
- Land VPTB propagation FIRST; the VA_FORM three-mode fix is a separate,
  later edit (its low-field arithmetic is already correct in this run, so it
  is not the active fault).
- VA_CTL[VPTB] field bounds are mode-dependent (HRM Fig 5-5/5-6/5-7); mark the
  propagation mask _PROVISIONAL until read off the VA_CTL register figure and
  HRM-verified.

## References

- logs/faults.log (2026-07-09): 51 x kFaultAcv, VA 0xffffffff7f827f5f stride;
  preceding kFaultDtbMissDouble at PC 0x8321, VA 0x7ffffdfe098.
- Alpha 21264/EV67 HRM section 5.1.5, Figures 5-5/5-6/5-7 (VA_FORM).
- PalEntries.cpp:1242-1247 (execMtprVptb), :362-363 (HW_VA_FORM read),
  :562-576 (DTB_PTE fill) -- [LOCATE], snapshot.
- Standing memory: VPTB propagation defect (confirmed); VA_FORM/VA_48 formula
  defect (confirmed, efficacy pending, "unmapped VPTE address given VPTB=0").
