<!--
EmulatR V4 -- ES40 memory-test ACV on 0xFFFFFFFF_7F827F5F: VPTB verdict,
authoritative-source audit, and EmulatR implementation audit (2026-07-09).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: record the confirmed root-cause state of the ES40 SRM memory-test
access violation, the live capture (ACVPROBE Hook B + VPTB-DIAG) that settled
it, the 21264A / 21272 HRM text that grounds it, and the exact EmulatR seams
audited against those sources.  ASCII(128).  Hex radix throughout.  Line
numbers are as-audited 2026-07-09; the live tree is the source of truth.
Discuss-before-code stands.
-->

# ES40 memtest ACV -- VPTB verdict + source/implementation audit (2026-07-09)

## TL;DR

The ES40 SRM memory test wedges on a repeating access violation at PC 0x1b7dd4
(a `LDQ R0,0(R16)` memory-probe primitive) for VA 0xFFFFFFFF_7F827F5F.  Live
capture proves the D-stream page-table walk forms its PTE-fetch address with
the Virtual Page Table Base (VPTB) equal to zero, so it reads self-map
structure the SRM never mapped, finds no valid leaf, and delivers ACV.

The VPTB is genuinely zero -- NOT lost by V4.  Per the 21264A spec VPTB lives
only in VA_CTL[63:30] (write-only; no standalone register); V4 stores VA_CTL
faithfully; the SRM wrote VA_CTL = 0x2 (VA_48=1, VPTB=0) and never invoked
CALL_PAL MTPR_VPTB.  So the machine has 48-bit VA mode enabled but no page-
table base at the time of the probe.  V4's translator, VA_FORM, VA_CTL, and
superpage handling all match the 21264A.  The defect is upstream of the
translator: R16 is very likely a malformed kernel/kseg pointer (base
0xFFFFFFFF_ where a kseg probe would use 0xFFFFFC00_), or the SRM establishes
this mapping by a route not yet captured.

## Symptom and fault chain

Source of record: logs/faults.log (coreLib::logFaultEvent, retire path
pipelineLib/PipelineDriver.h:1175; the caller filters kFaultDtbMiss/ItbMiss
only, so ACV and DtbMissDouble land).

- 51 x kFaultAcv (7) at PC 0x1b7dd4, enc 0xa4100000 = LDQ R0,0(R16), pal=0
  (native), VA walking 0xFFFFFFFF_7F827F5F +8 while R0 doubles (an address-line
  test).
- 4138 x kFaultDtbMissDouble (14) -- memtest paging churn, handled.
- The row immediately before the first ACV is the PAL double-miss handler at
  PC 0x8321 (pal=1) fetching the PTE at VA 0x7ffffdfe098.

Chain:

    LDQ R0,0(R16), R16=0xFFFFFFFF_7F827F5F  (native, memtest probe)
      -> DTB miss -> PAL walk reads VA_FORM -> PTE-fetch VA 0x7ffffdfe098
         -> PTE page itself misses (kFaultDtbMissDouble)
      -> walk resolves to no-valid -> kFaultAcv delivered to the LDQ
      -> guest prints "access violation fault"

## The decompiled instruction (SRM ROM)

From tools/host_decompressor/out/es40_decompressed.bin (runtime = fileoffset +
0x8000; LDQ at file 0x1afdd4 -> runtime 0x1b7dd4).  PC 0x1b7dd4 is a leaf in a
table of memory-access PRIMITIVES, each "<access>; TRAPB; RET":

    0x1b7d80 LDBU r0,0(r16); TRAPB; RET   probe-read byte
    0x1b7d8c LDWU r0,0(r16); TRAPB; RET   probe-read word
    0x1b7dd4 LDQ  r0,0(r16); TRAPB; RET   probe-read quadword   <-- faulting
    0x1b7de0/df8 block copy;   0x1b7e20/e2c probe-write byte/word

So the console PROBES memory at R16, TRAPB forcing the fault synchronous.  R16
is passed down through r27 procedure-value dispatch (callers at 0x5b058 /
0x5a6b0); its construction is deeper in the decompiled SRM (Ghidra
decompiled_src), not raw disassembly.

## The self-map fingerprint (proof the walk used VPTB=0)

21264A section 5.1.5 VA_FORM, 48-bit form (VA_48=1, VA_FORM_32=0; active per
va_ctl=0x2):

    VA_FORM[63:43] = VPTB[63:43]
    VA_FORM[42:38] = SEXT(VA[47])
    VA_FORM[37:3]  = VA[47:13]
    VA_FORM[2:0]   = 0

Computed for VA 0xFFFFFFFF_7F827F5F with VPTB = 0:

    SEXT(VA[47]=1)<42:38> = 0x1f << 38 = 0x7C000000000
    VA[47:13]<37:3>       = 0x7fffbfc13 << 3 = 0x003FFFDFE098
    VPTB<63:43>           = 0
    -------------------------------------------------------
    VA_FORM(VPTB=0)       = 0x7FFFFDFE098

Observed PAL double-miss fetch VA (faults.log, PC 0x8321) = 0x7ffffdfe098.
EXACT match.  Only the VPTB[63:43] field is wrong (zero); the SEXT and VPN
fields are correct.  This is the first live trace of the VA[47]=1 branch
(coreLib/IprFields.h:316-318 notes it was doctest-only until now), and it
validated on the nose.

## Live capture: ACVPROBE Hook B + VPTB-DIAG (2026-07-09, out/build/cli)

ACVPROBE Hook B (added at the D-stream TLB-hit seam, mmuLib/Ev6Translator.h,
under EMULATR_BRINGUP_PROBES), every hit identical:

    va=ffffffff7f827f5f mode=0(Kernel) res=ACV pte=403bfc1300000001
    valid=1 kre=0 for=0 pfn=403bfc13 vptb=0000000000000000 vactl=0000000000000002

VPTB-DIAG (execMtprVptb, logs every CALL_PAL MTPR_VPTB write): count = 0.

Reads:
- vptb (cpu.vptb) = 0 AND no MTPR_VPTB (0x2A) ever dispatched boot-wide.
- vactl = 0x2 -> VA_48=1, VPTB[63:30] = 0.
- The "PTE" 0x403bfc1300000001 is NOT a real leaf: pfn 0x403bfc13 shares its
  low 20 bits (0xbfc13) with VA>>13 -- it is VA-derived self-map page-table
  structure read out of the VPTB=0 region.  pfn<<13 = PA 0x807.7F82.6000,
  which is reserved space above Pchip1 (see HRM map below), not a real target.

## Authoritative sources

### 21264A Specifications Rev 1.1 (1999-05)

VA_CTL (section 5.1.4, Figure 5-4, Table 5-3):
- VA_CTL is WRITE-ONLY; it controls how the faulting VA is formatted when read
  via VA_FORM.
- VPTB[63:30]  extent [63:30]  type WO  "Virtual Page Table Base."
- VA_FORM_32 [2] WO,0 ; VA_48 [1] WO,0 ; B_ENDIAN [0] WO,0.
- VA_48: when set, 48-bit VA format; the sign-extension checker raises ACV if
  VA[63:0] != SEXT(VA[47:0]).  (Our VA satisfies this -- VA[47]=1 and bits
  63:48 = 0xFFFF -- so the ACV is NOT from the checker.)

There is NO standalone VPTB register.  CALL_PAL MTPR_VPTB is a PAL FUNCTION
that HW_MTPRs the base into VA_CTL (D-side) and I_CTL (I-side).

Superpage SPE[2:0] (section 5.3.9):
- SPE[2]: superpage when VA[47:46]=2; VA[43:13]->PA[43:13], VA[45:44] ignored.
- SPE[1]: superpage when VA[47:41]=0x7E; VA[40:13]->PA[40:13], PA[43:41]=
  SEXT(PA[40]).  (This is the kernel physical-memory window; base 0xFFFFFC00...)
- SPE[0]: superpage when VA[47:30]=0x3FFFE; VA[29:13]->PA[29:13], PA[43:30]=0.
- Superpage accesses are kernel-only; non-kernel references result in ACV.

Our VA 0xFFFFFFFF_7F827F5F: VA[47:46]=11 (not 2), VA[47:41]=0x7F (not 0x7E),
bit31=0 (VA[47:30] not all-ones).  It matches NO superpage window, so a
page-table walk is architecturally required -- V4 routes it correctly.

### 21272 (Tsunami/Typhoon) HRM -- Table 10-1 System Address Map (<43:0>)

    000.0000.0000-000.FFFF.FFFF  System memory (4GB, cacheable)
    800.0000.0000-800.FFFF.FFFF  Pchip0 PCI memory
    801.0000.0000-801.3FFF.FFFF  TIGbus
    801.8/A/B000.0000            Pchip0 / Cchip / Dchip CSRs
    801.FC00.0000-801.FDFF.FFFF  Pchip0 PCI I/O
    802.xxxx / 803.xxxx          Pchip1 PCI memory / CSRs / I/O
    804+                         (undecoded -- reserved)

The HOOKB pfn 0x403bfc13 -> PA 0x807.7F82.6000 is above Pchip1, in reserved
space -- consistent with it being VPTB=0 garbage, not a real mapping.
(Pchip DMA windows WSBA/WSM/TBA, HRM 10.2.5, are the PCI-facing side the SRM
programs in PCI-init; they are not the CPU-side decode and are not this bug.)

## EmulatR implementation audit (verified live 2026-07-09)

D-stream translate and the ACV origin -- mmuLib/Ev6Translator.h:
- applyTlbHit (:212-232): DataRead does `if (pte.faultOnRead()) FaultOnRead;
  if (!pte.canRead(mode)) AccessViolation;`.  For our hit, KRE=0 ->
  !canRead(Kernel) -> AccessViolation.  Correct per the (garbage) PTE.
- translateData TLB-hit seam (~:319-322): where ACVPROBE Hook B wraps the
  applyTlbHit return.  Hook B extended to print vptb=cpu.vptb, vactl=cpu.va_ctl.
- tryKsegTranslate (~:145-178): SPE[2] `((va>>46)&3)==2`, SPE[1]
  `((va>>41)&0x7F)==0x7E`, SPE[0] `((va>>30)&0x3FFFF)==0x3FFFE`, kernel-only
  ACV -- matches 21264A section 5.3.9 exactly.
- isCanonicalVA / NonCanonical (:106-108, :286): our VA is canonical, not
  kFaultNonCanonical.

VA_FORM formation -- coreLib/IprFields.h:
- computeVaForm (:321-343): three forms (Figs 5-5/5-6/5-7).  48-bit branch
  masks `vptb & 0xFFFFF80000000000` (bits 63:43) : SEXT : VPN.  Matches spec.
- Header (:297-320): "vptb is passed in its full register position (va_ctl for
  the D-side, iCtlVptb(i_ctl) for the I-side)".
- VA_CTL bit accessors: coreLib/VA_types.h (vaCtlIsVa48, vaCtlIsVaForm32,
  vaCtlVptb -- VA_CTL[63:30]).

IPR read/write of the base -- palBoxLib/grains/PalEntries.cpp:
- HW_VA_FORM read (:1408-1412): `computeVaForm(c.cpu->va_ctl, c.cpu->va,
  vaCtlIsVaForm32(va_ctl), vaCtlIsVa48(va_ctl))`.  The walk's base = va_ctl.
- HW_VA_CTL read (:1354): returns c.cpu->va_ctl.
- HW_VA_CTL write (:1810): `c.cpu->va_ctl = c.opB;` -- FULL value, NO mask.
  So VA_CTL[VPTB] is preserved on the store; va_ctl=0x2 is exactly the SRM
  write.
- execMtprVptb_vms (:784): `c.cpu->vptb = c.cpu->intReg[16];` only -- no push
  to va_ctl.  Its own comment (:787-790) names the gap.  cpu.vptb has NO
  hardware counterpart per the spec; nothing correct consumes it.
- EMULATR_VPTB_DIAG probe (:792-803): logs MTPR_VPTB writes vs va_ctl.
- MEMDIAG-VAFORM probe (:1413-1431, under EMULATR_MEMDIAG): logs HW_VA_FORM
  reads with the computed value.
- MEMDIAG-MTPR probe (:1629-1676, under EMULATR_MEMDIAG): logs every MMU-ctl
  IPR write (I_CTL, M_CTL, VA_CTL, ITB_TAG/PTE, DTB_TAG/PTE) with a vptbHint
  (VA_CTL[VPTB]=bits 63:30, I_CTL[VPTB]=bits 47:30).  This is the un-run probe
  that answers "does the SROM ever set VPTB, or hand-install TB entries?".

Dispatch -- grainFactoryLib/generated/DispatchTables.cpp:7924: CALL_PAL
MTPR_VPTB (func 0x2A) -> palBox::execMtprVptb_vms.  So 0x2A IS wired; the
boot-wide count of 0 means the SRM never calls it (not a dispatch bug).

Prior V4 note corroborating -- pipelineLib/MemDrainer.h:359-363: "VA_FORM ...
derived from ... VA_CTL[VPTB] for the page-table walk.  Previously unset, so
VA_FORM computed 0, the walk loaded an invalid PTE from [0], and re-faulted
forever (2026-05-27 SROM 0x8301-0x8321 page-walk spin)."  Same failure family.

Fault codes -- coreLib/BoxResult.h:104-135: kFaultAcv=7, kFaultDtbMissDouble=
14, kFaultNonCanonical=12.  DtbMissDouble raised at MemDrainer.h:340-344.
PTE accessors -- pteLib/AlphaPte.h: raw, isValid, bitKRE, faultOnRead, pfn.
IPR id -- coreLib/HW_IPR.h:241: HW_VA_CTL = 0x01C4.

## Verdict

1. V4's VPTB / VA_FORM / VA_CTL / superpage modeling matches the 21264A.  The
   ACV is V4 behaving faithfully given the machine state.
2. VPTB is genuinely 0.  The SRM wrote VA_CTL=0x2 (VA_48=1, VPTB=0), never
   called MTPR_VPTB, and V4 stored the write in full.  Not a V4 loss, not a
   propagation strand (cpu.vptb=0 too), not a VA_CTL mask.
3. The earlier "propagate cpu.vptb -> va_ctl[VPTB]" fix is moot twice over:
   cpu.vptb models no real register, and MTPR_VPTB is never called.
4. The VA is not superpage-shaped (spec-confirmed), so it legitimately needs a
   VPTB the machine does not have.  The low 32 bits (0x7F827F5F, a plausible
   RAM PA ~2.1GB) with base 0xFFFFFFFF_ (vs kseg 0xFFFFFC00_) are the signature
   of a malformed kernel/kseg pointer.

## Open branches and next capture

- B1: Does the SRM establish translation for this VA by an uncaptured route --
  setting VPTB via I_CTL/M_CTL, or hand-installing DTB entries?  RESOLVE with
  the MEMDIAG-MTPR probe (widen its guard to compile under EMULATR_BRINGUP_
  PROBES; capped 256; grep MEMDIAG-MTPR before the ACV).
- B2 (now more likely): Is R16 a botched kseg/superpage address (should be
  0xFFFFFC00 | PA)?  RESOLVE by tracing R16 construction in the decompiled SRM
  (Ghidra decompiled_src) -- the r27-dispatched callers above 0x5b058.  If so,
  the defect is upstream address construction and the ACV is correct.

Do-no-harm: any MMU-path edit is gated on the full suite + DS10 + DS20 + ES40
boot-to-P00 (standing rule).  The VA_FORM three-mode formula is already correct
this run (fingerprint matched) and is not the active fault.

## References

Authoritative:
- 21264A Specifications Rev 1.1 (1999-05): section 5.1.4 VA_CTL (Fig 5-4,
  Table 5-3, VPTB[63:30] WO, VA_48, VA_FORM_32); section 5.1.5 VA_FORM (Figs
  5-5/5-6/5-7); section 5.3.9 SPE[2:0] superpage.
- 21272 (Tsunami/Typhoon) HRM: Table 10-1 System Address Map; section 10.2.5
  WSBA/WSM/TBA Pchip DMA windows.

EmulatR implementation (verified live 2026-07-09):
- mmuLib/Ev6Translator.h: applyTlbHit :212-232; translateData TLB-hit + Hook B
  ~:319-356; tryKsegTranslate ~:145-178; isCanonicalVA :106-108.
- coreLib/IprFields.h: computeVaForm :321-343 (+ header :297-320).
- coreLib/VA_types.h: VA_CTL accessors; coreLib/CpuState.h:198 (va_ctl),
  :372 (vptb, "zero at boot"); coreLib/HW_IPR.h:241 (HW_VA_CTL=0x01C4);
  coreLib/BoxResult.h:104-135 (fault codes).
- palBoxLib/grains/PalEntries.cpp: execMtprVptb_vms :784 (+ VPTB-DIAG :792-803);
  HW_VA_CTL read :1354 / write :1810; HW_VA_FORM read :1408-1412; MEMDIAG-VAFORM
  :1413-1431; MEMDIAG-MTPR :1629-1676.
- grainFactoryLib/generated/DispatchTables.cpp:7924 (MTPR_VPTB dispatch).
- pipelineLib/MemDrainer.h:340-344, :359-363; pipelineLib/PipelineDriver.h:1175.
- pteLib/AlphaPte.h (PTE accessors).

Capture artifacts:
- logs/faults.log (2026-07-09): 51 x kFaultAcv, VA 0xffffffff7f827f5f stride;
  preceding kFaultDtbMissDouble PC 0x8321 VA 0x7ffffdfe098.
- out/build/cli/es40_hookb.log: ACVPROBE HOOKB (valid=1 kre=0 vptb=0 vactl=0x2),
  VPTB-DIAG count 0.
- putty_console_p10023_20260709152242.log: 51 guest "access violation" dumps.
- tools/host_decompressor/out/es40_decompressed.bin: PC 0x1b7dd4 probe table.

Toolchain (this session):
- tools/env.sh (source: cmake+ninja+cl via vcvars temp-bat, QTDIR),
  tools/build_diag.sh (CLI build, EMULATR_BRINGUP_PROBES=ON, EMULATR_IRQDIAG=OFF),
  tools/run_es40_hookb.sh (run + EMULATR_VPTB_DIAG=1 + grep HOOKB/VPTB-DIAG),
  tools/diag_msvc.sh (vcvars diagnostic).

Related memory: [[es40-srm-boot-status]], [[emulatr-es40-diag-knobs]],
[[deliver-bash-as-scripts]], [[verify-webchat-claims-vs-live-tree]].
Prior journals: 20260709_es40_memtest_acv_briefing.md (hand-off input),
20260709_es40_memtest_acv_analysis.md (web-variant analysis).
