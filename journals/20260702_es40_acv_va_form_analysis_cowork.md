================================================================================
ES40 kFaultAcv LOOP @ PC 0x1b7xxx -- VA-FORM / KSEG-SUPERPAGE ANALYSIS
Read-only advisory from Cowork (PC) -> Mac Claude Code.  ASCII(128) only.
Date: 2026-07-02.  NOT committed by Cowork (Mac owns git for now).
================================================================================

1. STATUS / CONTEXT
-------------------
- The sparse-page-crossing SIGSEGV in GuestMemory write2/4/8 is FIXED
  (commit 46151a0).  ES40 now advances past pc=0x5afac.
- New blocker: a kFaultAcv (Access Control Violation) loop at PC 0x1b7xxx
  -- the SAME region as the earlier CSERVE 0x66 caller (0x1b78f8).
- Working hypothesis (Tim): a kseg / superpage access, possibly in the
  Windows-NT-style 43-bit VA form, mishandled by Ev6Translator.

2. ARCHITECTURE FACTS (Alpha Architecture Reference)
----------------------------------------------------
- Min VA size is 43 bits; implementations are 43- or 48-bit (sec 2.1).
- VA_CTL bit 1 selects VA form: set => 48-bit, clear => 43-bit.
  * 48-bit: VA<63:48> = SEXT(VA<47>).
  * 43-bit: VA<63:43> = SEXT(VA<42>).
- Superpage / kseg is KERNEL-ONLY on EV6.
- Page tables are reached EITHER physically via PTBR, OR virtually via a
  self-map linear region (VPTB) [ch. 11.8.1 / 11.8.2].  The virtual self-map
  walk is the guest PALcode's job, not EmulatR's translator.
- A translation denial is ACV (protection) vs TNV (valid bit clear) [11.9].
- Alpha Windows NT ran a 43-bit VA layout distinct from OSF/VMS -- this is
  why the NT angle matters: if this code path runs 43-bit, the superpage
  decode must use the 43-bit field positions.

3. CODE FINDINGS -- mmuLib/Ev6Translator.h (read-only)
------------------------------------------------------
(a) isCanonicalVA (line ~104): CORRECT.  It reads va_ctl & 0x2 and picks
    msb = 47 (48-bit) or 42 (43-bit).  So the canonical-VA check honors the
    active VA form.

(b) tryKsegTranslate (line ~133): *** PRIME SUSPECT ***
    Signature: tryKsegTranslate(va, mode, spe, pa_out) -- it does NOT take
    va_ctl.  It decodes the three superpage regions with HARDCODED 48-bit
    field positions, unconditionally:
       SPE[2]  (va >> 46) & 0x3   == 0x2       (VA<47:46> == 0b10)
       SPE[1]  (va >> 41) & 0x7F  == 0x7E      (VA<47:41> == 0b1111110)
       SPE[0]  (va >> 30) & 0x3FFFF== 0x3FFFE  (VA<47:30> == 0x3FFFE)
    Kernel gate: nonKernel (mode != Kernel) on a kseg-shaped VA returns
    AccessViolation immediately; otherwise a non-matching VA returns NotKseg
    (falls through to the TLB / page-walk path).
    => If the CPU is in 43-bit VA mode, a kseg/superpage VA will NOT match
       these 48-bit patterns -> NotKseg -> page walk -> (likely) ACV/TNV.
    => If the CPU IS in kernel-privileged native SRM but EmulatR reports
       mode != Kernel, a 48-bit kseg VA -> immediate ACV.

(c) applyTlbHit (line ~210): on a TLB hit, ACV is raised when
    !pte.canRead/canWrite(mode); FaultOnRead/Write when the PTE FOx bit is
    set.  So a genuine protection-deny PTE also yields ACV here.

(d) NO hardware page-table walk / VPTB self-map is done in this translator.
    On a TLB miss that is not kseg/palmode, the guest PAL runs its fill
    routine.  So a kFaultAcv LOOP = the guest PAL keeps retrying an access
    the translator keeps denying -- the condition is never cleared.

4. CANDIDATE MECHANISMS (ranked)
--------------------------------
[1] 43-bit VA-form superpage miss (fits the NT angle).  Firmware runs
    VA_CTL 43-bit; tryKsegTranslate only knows 48-bit field positions ->
    kseg VA not recognized -> page walk -> ACV -> PAL retry -> loop.
[2] Mode mis-tracking.  Native SRM runs privileged (palMode=0, CM=kernel).
    If cpu.mode != Kernel at the fault, every kseg-shaped VA -> immediate
    ACV in tryKsegTranslate.
[3] SPE bits not captured.  If the guest set I_CTL/M_CTL[SPE] via MTPR but
    EmulatR did not record m_spe/i_spe, all three (spe & N) tests fail ->
    kseg VA -> page walk -> ACV.
[4] Genuine PTE protection deny (applyTlbHit).  A real mapping whose PTE
    denies the access -- e.g., a region the no-op'd CSERVE 0x66 was meant to
    map/unprotect (0x66 args were R17=0xC0000000/0x40000000, ~512K count).

5. DECISIVE TRIAGE -- capture AT the faulting instruction
---------------------------------------------------------
One fault's worth of state disambiguates all four:
  - faulting VA (the DATA address, not the PC 0x1b7xxx).
  - VA_CTL: is bit1 set?  => 48-bit vs 43-bit form.  (Candidate [1] hinges
    entirely on this.)
  - cpu.mode / PS<CM>: is it Kernel?  (Candidate [2].)
  - m_spe / i_spe value at the fault.  (Candidate [3].)
  - fault subtype: ACV vs TNV (confirm it is ACV, not TNV).
  - Is the VA kseg-shaped?  In 48-bit: VA<47:46>==0b10, or the 0x3FFFE /
    0x7E patterns.  In 43-bit: the analogous low-shifted patterns.
  - Does the faulting VA fall in 0xC0000000 / 0x40000000 (the CSERVE 0x66
    region)?  If yes, lean candidate [4].

Read as:
  * VA_CTL=43-bit AND VA is kseg-shaped-for-43-bit  -> candidate [1] (fix in
    tryKsegTranslate: add the 43-bit field decode, gated on va_ctl).
  * mode != Kernel on a 48-bit kseg VA              -> candidate [2] (fix the
    mode plumbing so native SRM is Kernel).
  * m_spe/i_spe == 0 but firmware wrote I/M_CTL[SPE]-> candidate [3] (capture
    the MTPR into m_spe/i_spe).
  * normal VA in 0xC0000000 region, PTE denies      -> candidate [4] (points
    back to CSERVE 0x66 region setup, not the translator).

6. LIKELY FIX PER OUTCOME
-------------------------
- [1] tryKsegTranslate must take va_ctl and, in 43-bit mode, match the
  43-bit superpage field positions (shift the VA<47:xx> tests down to the
  43-bit-relative bits per the 21264 HRM I_CTL/M_CTL SPE spec).  This is the
  most likely and the cleanest single fix if VA_CTL says 43-bit.
- [2] Ensure cpu.mode is derived from PS<CM> and is Kernel during native
  SRM; check where mode is set on PAL/native transitions.
- [3] Ensure MTPR to I_CTL/M_CTL updates i_spe/m_spe (and that the SPE field
  extraction matches the CSR layout).
- [4] Not a translator bug -- implement the CSERVE 0x66 region effect (or
  the mapping the firmware expects) so the region has a valid PTE.

7. REFERENCES
-------------
- mmuLib/Ev6Translator.h : isCanonicalVA (~104), tryKsegTranslate (~133),
  applyTlbHit (~210).
- Alpha Arch Ref: sec 2.1 (VA size), ch 11.6/11.8/11.9 (PTE, page-table
  access physical vs self-map, ACV vs TNV), Appendix F (Windows NT).
- 21264 HRM: I_CTL / M_CTL SPE superpage field definitions per VA form.
- Prior: journals/20260702_es40_boot_blocker_analysis.md (SIGSEGV/write8),
  and the CSERVE 0x66 decode (undefined in $cserve_def; symptom not service).

NEXT ACTION FOR MAC: capture the six triage values at the first ACV, then
pick the lane above.  Cowork (PC) can dig further read-only on request (e.g.
disassemble the 0x1b78f8 caller, or trace where cpu.mode / i_spe / m_spe /
va_ctl are set) -- just ask.
================================================================================
