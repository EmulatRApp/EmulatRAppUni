<!--
EmulatR V5 -- Session Journal / Audit JRN-VMB-008
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active hive (emulatrappuniv5)
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Per docs/notes/ADR-0001-source-file-headers.md (Markdown header as HTML comment).
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-008 -- VA register-family HRM audit (VA_CTL / VA_FORM / IVA_FORM / ITB-DTB TAG+PTE)

    Doc id      : JRN-VMB-008
    Status      : AUDIT COMPLETE.  VA field-decode is HRM-correct; one LATENT
                  plumbing break (VPTB stranding) that is NOT triggered by the
                  current OpenVMS boot.  Redirects the blocker to the miss-
                  SERVICING path (JRN-VMB-007 A/B fork), not VA field math.
    Date        : 2026-07-19
    Relates to  : JRN-VMB-007 (the A/B fork), 20260716_vector_dispatch_tb_region.
    Encoding    : ASCII-128.  Hex radix.

---

## 1. Authoritative HRM spec (21264/EV67), as used here

VA_CTL (Virtual Address Control, write-only, packed):
    VPTB      [63:30]   Virtual Page Table Base
    VA_FORM_32[2]       VA_FORM address-format select
    VA_48     [1]       RESET 0.  0 = 43-bit VA format; 1 = 48-bit.
                        Sign-ext ACV: VA_48=1 -> ACV if VA[63:0] != SEXT(VA[47:0]);
                        VA_48=0 -> ACV if VA[63:0] != SEXT(VA[42:0]).
    B_ENDIAN  [0]       Big-endian mode.

VA_FORM (data-stream PTE virtual address; sources VA_CTL fields):
    VA_48=0, FORM32=0:  VPTB[63:33] : VA[42:13]                    (43-bit)
    VA_48=1, FORM32=0:  VPTB[63:43] : SEXT(VA[47]) : VA[47:13]     (48-bit)
    VA_48=0, FORM32=1:  VPTB[63:30] : VA[31:13]                    (32-bit)
    (VA field lands at result[..:3]; low 3 bits zero.)

IVA_FORM (instruction-stream): identical formats, sourced from I_CTL's
    VPTB / VA_48 / VA_FORM_32.  I_CTL's VPTB is [47:30] (NOT [63:30]);
    the canonical base is reconstructed by sign-extending bit 47 into [63:48].

ITB_TAG / DTB_TAG (WO): VA[47:13].
ITB_PTE / DTB_PTE (WO): PFN[43:13]; prot URE[11]/SRE[10]/ERE[9]/KRE[8];
    GH[1:0]=[6:5]; ASM[4] (D-side adds FOR/FOW + write-enables).

## 2. Audit result -- per register (emulator vs HRM)

All PASS unless noted.  Accessors live in coreLib/VA_types.h; computeVaForm in
coreLib/IprFields.h; PTE decode in pteLib/Ev6PteFormat.h.

  VA_FORM math (IprFields.h:321-343) -- PASS all three forms:
    43-bit VPTB mask 0xFFFFFFFE00000000 = [63:33]; VPN (VA>>10)&0x1FFFFFFF8;
    48-bit VPTB mask 0xFFFFF80000000000 = [63:43]; SEXT seg 0x7C000000000 =
      [42:38]; VPN (VA>>10)&0x3FFFFFFFF8;
    32-bit VPTB mask 0xFFFFFFFFC0000000 = [63:30]; VPN (VA>>10)&0x3FFFF8.
    Switches correctly on form32 then va48 (IprFields.h:326,331).

  VA_CTL extractors (VA_types.h:133-168) -- PASS:
    B_ENDIAN=1<<0, VA_48=1<<1, VA_FORM_32=1<<2, VPTB mask 0xFFFFFFFFC0000000.
    (vaCtlVptb and vaCtlIsBigEndian are DEAD -- defined, no callers.)

  I_CTL VA-fields + IVA_FORM (IprFields.h:190-295) -- PASS:
    I_CTL VA_48 = bit 15; VA_FORM_32 = bit 16; VPTB = [47:30], reconstructed
    to [63:xx] with SEXT(47).  IVA_FORM feeds iCtlVptb(i_ctl) to computeVaForm
    (PalEntries.cpp:1484); fault VA = EXC_ADDR (:1485).  No mismatch.

  VA_CTL read/write (PalEntries.cpp:1456 read / :1943 write) -- PASS:
    write stores full raw opB (no field dropped); read returns it.  Default
    va_ctl=0 (CpuState.h:198) => VA_48=0 = HRM reset.  i_ctl=0, m_ctl=0.

  VA_48 sign-ext / ACV checker (Ev6Translator.h:125-134 isCanonicalVA) -- PASS:
    reads live va_ctl bit 1; msb = va48?47:42; canonical iff high bits all-0 or
    all-1.  Exactly the HRM SEXT(VA[47:0]) / SEXT(VA[42:0]) rule.  Called from
    translateData (:305) and the I-side (:540).

  ITB/DTB TAG + PTE (pteLib/Ev6PteFormat.h:99-135; PalEntries.cpp:1959-2021) -- PASS:
    ITB_PTE PFN=(reg>>13)&(2^31-1); DTB_PTE PFN=(reg>>32)&(2^31-1) per Fig 5-27;
    prot/GH/ASM bit positions correct.  TAG stores raw opB; VPN match is va>>13
    ([63:13]) but is safe because isCanonicalVA runs first (so [63:48]=SEXT(47)).

## 3. Ranked discrepancies

  #1 [HIGH, but LATENT -- not triggered this boot] VPTB stranding.
     MTPR_VPTB (CALL_PAL 0x2A, the OpenVMS path) writes the page-table base into
     cpu.vptb (PalEntries.cpp:863; field CpuState.h:372).  But VA_FORM reads
     cpu.va_ctl (:1511) and IVA_FORM reads cpu.i_ctl (:1484).  NOTHING copies
     cpu.vptb -> va_ctl[63:30] / i_ctl[47:30] (cpu.vptb has exactly one writer,
     one reader).  So IF the OS sets the base via MTPR_VPTB, VA_FORM masks a
     VPTB of 0 and every Dstream DTB-miss PTE VA = VPN<<3 with no base.  This is
     the self-documented "VPTB stranded" defect (probes at PalEntries.cpp:864-867
     and Ev6Translator.h:358-368).
     NOT ACTIVE HERE: EMULATR_VPTB_DIAG showed MTPR_VPTB fired ZERO times this
     boot, and the stop-state va_ctl=0x2 has VPTB[63:30]=0.  So no VPTB base is
     established by EITHER path -- the firmware is not using the VPTB walk in
     this phase at all.  Fix is still correct (HRM: VA_FORM must see the OS VPTB):
     have execMtprVptb_vms also deposit VPTB into va_ctl[63:30] and i_ctl[47:30].

  #2 [LOW, latent] TB tag stored/compared as [63:13] not masked to [47:13];
     safe only because isCanonicalVA runs first.

  #3 [LOW, doc-only, MISLEADING] Stale field comments: CpuState.h:195-197 still
     describes the REMOVED "VA_48 = physical-mode bypass" hack; :200-202 says
     I_CTL VA_48 is bit 1 (it is bit 15).  Accessors are correct; comments invite
     a wrong-bit "fix".  Worth correcting.

  #4 [VERIFY] HW_IVA_FORM index = 0x0107 with 0x0105 assigned to
     HW_ITB_PTE_TEMP_PROVISIONAL; conventional encoding puts IVA_FORM at 0x105.
     Check vs HRM Table 5-2 before clearing the _PROVISIONAL tripwire.  Does not
     affect field math if PALcode and the enum agree.

  #5 [LOW, spec gap] B_ENDIAN unimplemented (vaCtlIsBigEndian has no consumers).
     Irrelevant to PTE-address math.

## 4. Reconciliation and conclusion

The VA register field-decode is HRM-correct: computeVaForm (all 3 forms), the
VA_CTL / I_CTL extractors, the VA_48 canonical-ACV checker, and the ITB/DTB
TAG/PTE decoders all PASS.  The only functional defect (#1) requires the OS to
set the page-table base via CALL_PAL MTPR_VPTB, which this OpenVMS boot never
does (VPTB-DIAG = 0 hits), and no VPTB base is set by the VA_CTL path either
(stop va_ctl=0x2, VPTB=0).

Therefore the blocker is NOT a VA-field decode bug.  The firmware establishes
NO VPTB base in this phase -- it is NOT using the VA_FORM/VPTB page-table walk;
it runs on HAND-INSTALLED TB entries (JRN-VMB-007 Sec 2).  So a Dstream DTB miss
on a VMB working-set page reaching the VPTB-walk handler is the JRN-VMB-007 A/B
fork -- a SERVICING/vectoring or phase-predicate issue -- NOT VA arithmetic.
The audit's value is the clean elimination: VA math is exonerated; the fork
stands.

## 5. Recommended next actions

  R1 (confirm, close a probe gap).  MEMDIAG-MTPR is capped at 256 and starves on
     the DTB_TAG flood, so a LATE HW_MTPR VA_CTL write could be hidden.  Give
     VA_CTL / I_CTL / VPTB writes their OWN small cap (separate from DTB_TAG) so
     we can state definitively that NO VPTB base is ever set.  (One-line filter
     change in the existing execHwMtpr MMU-ctl probe.)

  R2 (fix #1 regardless -- HRM correctness, cheap, removes a latent corruptor).
     execMtprVptb_vms also deposits VPTB into va_ctl[63:30] and i_ctl[47:30].
     Will NOT change this boot (MTPR_VPTB unused here) but is correct and unblocks
     the later OS phase that does use it.  Also fix the #3 stale comments.

  R3 (the actual blocker).  Resolve the A/B fork (JRN-VMB-007 Sec 4-6): the
     triple-trace (EMULATR_TRACE_ARM_ON_DTBM) + the DTBVEC-DIAG probe (entry PA,
     p_misc at the gate, branch direction, per-VA "DTB_TAG ever installed").

## 6. Citations

  Emulator (D:\EmulatR\emulatrappuniv5):
    coreLib/IprFields.h:321-343       computeVaForm (3 forms)
    coreLib/IprFields.h:190-295       I_CTL VA-fields (VA_48 b15, FORM32 b16, VPTB [47:30])
    coreLib/VA_types.h:133-168        VA_CTL extractors
    coreLib/CpuState.h:198,203,207    va_ctl/i_ctl/m_ctl defaults = 0
    coreLib/CpuState.h:195-202        STALE field comments (#3)
    palBoxLib/grains/PalEntries.cpp:1456,1943  VA_CTL read/write
    palBoxLib/grains/PalEntries.cpp:1484,1511  IVA_FORM / VA_FORM MFPR
    palBoxLib/grains/PalEntries.cpp:863-867     MTPR_VPTB -> cpu.vptb (+ stranding probe)
    mmuLib/Ev6Translator.h:125-134,305,540      isCanonicalVA (ACV) + call sites
    mmuLib/Ev6Translator.h:358-368              VPTB-stranding ACVPROBE
    pteLib/Ev6PteFormat.h:99-135                ITB/DTB PTE decode
  Authoritative: 21264/EV67 HRM VA_CTL / VA_FORM / IVA_FORM / ITB_TAG-PTE
    (Figs 5-5/5-6/5-7, 5-8, 5-27; field spec transcribed Sec 1).

## 7. Standing rules

  ASCII-128; hex radix; _PROVISIONAL IPRs (HW_ITB/DTB_PTE_TEMP 0x105/0x122)
  stay tripwire-suffixed until HRM-verified; logging in CMake compile guards;
  discuss before code.
