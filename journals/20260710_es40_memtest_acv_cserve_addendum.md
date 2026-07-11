<!--
EmulatR V4 -- ES40 memtest ACV: ADDENDUM to the 2026-07-10 corrected-mechanism
journal.  Elevates the CALL_PAL 0x09 (CSERVE) at 0x01B78F8 as the prime
in-window suspect via a paradox argument, defines the function-code
identification probe as the new cheapest-decisive step, accepts Cowork's
live-code corrections, and updates the fix-shape tree.  2026-07-10b.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience.  [LOCATE] = point-in-time; verify
against the live tree.  _PROVISIONAL = not yet HRM/ref-verified.
This is a HYPOTHESIS with a stated falsification path, not a conclusion.
-->

# ADDENDUM 2026-07-10b -- CSERVE-return hypothesis

Parent: 20260710_es40_memtest_acv_trace_corrected_mechanism.md.  Everything in
the parent's "What the trace PROVES" section stands.  This addendum reorders the
next move and adds one branch to the fix-shape tree.

## COWORK VERIFICATION VERDICT (2026-07-10) -- S1 DEAD, this branch closed

Verified against the live tree + trace before acting.  Result: the CSERVE
hypothesis is a STREETLIGHT lead and its actionable branch (S1) is a rejected
warp.  Do NOT implement cserve 0x66.  Details:

1. Selector correction: the addendum's "R16 = 0x3FC12000 at the CALL_PAL,
   implausible selector" is WRONG.  The trace shows R16 is reloaded to 0x66 at
   0x08C2F8 (BIS zero,#0x66,r16), one op before the BSR.  execCserve reads the
   selector from exactly there: funcCode = intReg[16] & 0xFF (PalEntries.cpp:353).
   So the selector IS R16, value 0x66 (=102 dec).

2. S1 is already litigated and rejected.  PalEntries.cpp:580-600 records that
   CSERVE 0x66 "get_time" was REMOVED 2026-07-08 as an unsourced mislabel: the
   authoritative PC264 PAL (ev6_vms_pc264_pal.mar:3911) hw_ret's with R0
   UNTOUCHED for all undefined codes, and 102 is undefined (last defined =
   MP_WORK_REQUEST 101).  EmulatR's no-op MATCHES hardware -- the missing =>R00
   in the trace is the FAITHFUL behavior, not a dropped return.  Re-implementing
   cserve 0x66 to supply R0 is exactly the warp the addendum's own do-no-harm
   section forbids.

3. The tree names our subroutine.  That same comment says get_time was added to
   "paper over an ES40 memory-test SYSFAULT at guest 0x08C2D0 (return =
   input - get_time())" -- our exact subroutine.  Our ACV IS that SYSFAULT,
   re-exposed by the faithful no-op.  get_time was a MASK (a TOY timestamp in R0
   shifted the address off the fault), never a fix.

4. Therefore S2 is confirmed by the tree itself: CSERVE is a faithful red
   herring; the defect is the UPSTREAM fill-pointer base (R0 should be 42-bit
   0x000003FF_C03EA0A1, is 32-bit 0xC03EA0A1).  Proceed to the parent's
   re-pointed A.3 (fill-pointer base birth at 0x05AFA0).  The faithful open
   question (PalEntries.cpp:594-600): the memtest's real time/base source is
   likely an internal get_timestamp bsr, NOT a cserve -- resolve from apisrm/
   Processor Support ref, do not re-add the cserve.

The sections below are the original web hypothesis, kept for history; S1 is
closed by the above.

## Accepted corrections (Cowork live-code verification overrides web analysis)

- The va==pa fill STQ at 0x05AFAC is a FAITHFUL identity DTB hit the SRM
  installed for low memory -- NOT a translation collapse.  The only translator
  identity path is the EMULATR_BOOTSTRAP_ITB_BYPASS #ifdef; the store otherwise
  reaches translateData and misses kseg normally.  The "born from a va==pa
  collapse" branch is DOWNGRADED to least-likely, per the parent.
- SPE[1] plumbing is faithful (Ev6Translator.h:158 gates on
  ((va>>41)&0x7F)==0x7E && (spe&0x2), identical to AXPBox; enable honored via
  cpu.m_spe).  Not the blocker.  Unchanged.

## The paradox that forces a non-arithmetic seam

The parent proves every arithmetic op in the chain is faithful: SUBQ (0x08C308),
LDA (0x05AFB4), and the SLL (0x05AF60) that builds R2 all produce the correct
64-bit result for their captured inputs.  Follow that to its conclusion:

  - If EmulatR faithfully executes faithful ops on given inputs, then real EV6
    running the identical instruction stream on the identical inputs produces
    the identical result: R16 = 0xFFFFFFFF_7F827F5F, and would ACV here too.
  - Real EV6 running real SRM does NOT ACV here (it boots).
  - Therefore the INPUTS must differ between EmulatR and real EV6 somewhere in
    the chain.  Since the arithmetic is faithful, that difference cannot arise
    in an ALU op -- it must arise at a NON-ARITHMETIC seam.

In the captured chain (fill loop -> SUBQ -> BIS -> probe) there is exactly one
non-arithmetic seam: the BSR at 0x08C2FC into 0x01B78F8, which is
CALL_PAL 0x09 (CSERVE) then RET.  Everything else is ALU or register copy.  So
CSERVE is the prime IN-WINDOW candidate for where EmulatR's register state
diverges from hardware.

Scope caveat (honest): this elevates CSERVE as the leading in-window seam only.
It does not exclude an out-of-window origin (parent hypothesis (b): the
fill-pointer base was already 32-bit before the capture began).  If CSERVE
clears, that branch is next.

## Why CSERVE is the prime suspect (not a footnote)

The parent records, and sets aside, that "cserve does not set R0 here; the SUBQ
therefore consumes the fill-loop pointer directly."  Re-read that as the bug,
not as expected behavior:

1. Code shape.  The subroutine at 0x08C2D0 does: BIS zero,r16,r2 (save incoming
   arg into R2), BSR -> CSERVE, then SUBQ r2,r0,r0 (R0 = R2 - R0).  A CSERVE
   immediately upstream of a SUBQ that CONSUMES R0 is the shape of code that
   expects CSERVE to RETURN its result in R0.  CSERVE functions conventionally
   return in R0.  The intended flow is very likely R0 = cserve(...); R2 - R0.
2. EmulatR's stub returned nothing.  The trace shows no =>R00 write from the
   CALL_PAL.  So the SUBQ fell through onto the STALE fill pointer still sitting
   in R0 from the fill loop.  "Consumes the fill-loop pointer directly" is the
   SYMPTOM of a dropped return value, not correct behavior.
3. It explains the parent's cleanest fact.  The 0x3FF is never born across
   633,115 instructions because the operation that was supposed to FORM it is a
   no-op.  On real hardware this CSERVE plausibly returns the pointer in its
   42-bit kseg-masked form (0x000003FF_C03EA0A1) -- exactly the value that makes
   the SUBQ borrow yield 0xFFFFFC00_7F827F5F.  The base is not lost in
   arithmetic and not truncated at an MMU seam; it is the missing return value
   of a PAL service EmulatR stubs.
4. It fits a documented weakness.  execCserve was ported from OSF and is being
   corrected; several VMS cserve function codes are currently no-ops
   (0x44/0x45/0x46/0x65 per the PALmode briefing).  A CSERVE dispatch that
   no-ops a function the SRM depends on is a known failure family, which raises
   the prior further.

## The two sub-cases the probe must distinguish

The hypothesis is only correct in one of two sub-cases; the probe decides which:

  S1: real CSERVE at this selector WRITES R0 (returns a value/transformed
      pointer).  -> EmulatR's stub is the injection seam.  Fix: implement the
      function faithfully so R0 carries what hardware returns.  This is the fix,
      and it is PAL/CSERVE work -- not MMU, not decompressor, not Stream B.
  S2: real CSERVE at this selector PRESERVES R0 (leaves it untouched).  -> then
      R0 = fill-pointer is INTENDED, CSERVE is a red herring, and the base was
      lost UPSTREAM.  Fall back to extending the trace window backward to root
      the fill-pointer base (parent Plan A.3).

## New cheapest-decisive step (ahead of the backward trace extension)

Task: identify the CSERVE function selector at 0x01B78F8 and its R0 contract.
This is one register capture plus a reference-source lookup -- no 84 GB re-walk.

Probe (gated EMULATR_BRINGUP_PROBES, fire-once, capped):
  - At the CALL_PAL 0x09 site (0x01B78F8), capture the full argument-register
    set -- R0 and R16..R21 -- since the selector register convention for cserve
    varies by PAL variant and must not be assumed.  (Note: R16 = 0x3FC12000 at
    this point is the saved incoming arg, an implausible small-selector value,
    so the selector is likely NOT R16 here; capture all to be safe.)
    [_PROVISIONAL: cserve selector register + arg layout -- confirm vs ref.]
  - Capture R0 immediately BEFORE and immediately AFTER the CALL_PAL, to see
    directly whether EmulatR's dispatch writes R0.

Cross-check (authoritative, in parallel):
  - apisrm/ref and the VMS PALcode source (ev6_vms_pc264_pal.mar, sys__cserve
    dispatch) for the identified function: does it write R0, and with what?
    Confirm the PAL personality for THIS loaded image (VMS per the PALmode
    briefing; verify against es40_decompressed.bin, do not assume).

Decision:
  - Ref says the function returns in R0 AND EmulatR's dispatch does not
    -> S1 confirmed.  Implement that cserve function faithfully; re-run;
    expect R0 = kseg-masked pointer, SUBQ -> 0xFFFFFC00_7F827F5F, SPE[1] hit,
    ACV cleared.
  - Ref says the function preserves R0 -> S2; the base is upstream.  Proceed to
    the parent's re-pointed A.3 (watch the fill-pointer base birth at 0x05AFA0).

## Fix-shape tree -- updated

Leading candidate (NEW):
  - Born from a CSERVE / PAL return the SRM expects and EmulatR no-ops
    -> implement the CSERVE function faithfully.  PAL/CSERVE seam.  Test via the
    probe above BEFORE any backward trace extension.

Remaining branches (unchanged from parent, priority after CSERVE clears):
  - Born from a memory-size / CSC read -> root is Stream B; wiring the size CSR
    (B1) makes the base correct.  (Stream B becomes upstream-causal.)
  - Born from a load of a pointer/table in the decompressed image -> image bytes
    wrong; fix the decompressor (20260519_decompressor_pal_overlap_findings).
  - Born from an IPR / HW_MFPR read EmulatR models differently -> fix that
    register's value.  (CSERVE is the CALL_PAL cousin of this branch, split out
    and elevated because it is the one non-arithmetic seam actually in-window.)
  - DOWNGRADED: born from an earlier store via a va==pa collapse -> the observed
    va==pa store is a faithful identity DTB hit (Cowork verified); least likely
    unless a probe shows the store bypassing the DTB.

## Sequencing / do-no-harm

- The probe is read-only instrumentation; no gate beyond the compile guard.
- If S1 lands a CSERVE implementation, that touches PAL dispatch -> full suite +
  DS10 + DS20 + ES40 boot-to-P00 green before commit.
- FAITHFUL rule: if S1, implement the real function's R0 contract from ref/HRM.
  Do NOT fabricate a plausible R0 value to make the SUBQ come out right -- that
  is warp-as-fix and would mask whatever the function actually computes.

## Open / _PROVISIONAL

- CSERVE selector register + arg layout for this image -- _PROVISIONAL, confirm
  vs apisrm/ref + the loaded image.
- Which cserve function code 0x01B78F8 invokes, and its documented R0 side
  effect -- the crux; resolve from ref before implementing.
- PAL personality of es40_decompressed.bin (VMS assumed) -- verify.

## References

Parent + prior: 20260710_es40_memtest_acv_trace_corrected_mechanism.md;
20260709_es40_memtest_acv_briefing.md, _analysis.md, _vptb_verdict.md;
20260710_es40_memtest_session_plan.md (mechanism corrected in parent).
Trace: D:/EmulatR/traces/20260709-211151_srm.trc (RETIRE_COMPACT); subroutine
0x08C2D0-0x08C314; CSERVE stub 0x01B78F8 (CALL_PAL 0x09; RET); probe 0x01B7DD4.
Image: tools/host_decompressor/out/es40_decompressed.bin (load base 0x8000).
EmulatR seams [LOCATE]: palBoxLib/grains/PalEntries.cpp execCserve dispatch
(CALL_PAL 0x09) + SPE derive :1711/1729; pipelineLib/MemDrainer.h:205-242
(WB commit, A.3 seam); mmuLib/Ev6Translator.h:158 (SPE1 gate), :305-306
(kseg enable honored).
Authoritative: apisrm/ref (SRM); ev6_vms_pc264_pal.mar (sys__cserve dispatch);
Alpha 21264/EV67 HRM 5.3.9 (SPE).
Memory: [[es40-srm-boot-status]], [[emulatr-es40-diag-knobs]],
[[verify-webchat-claims-vs-live-tree]].  Tasks: #6 (root cause), #10 (A.3),
#11 (Stream B), + new: CSERVE function-code identification probe.
