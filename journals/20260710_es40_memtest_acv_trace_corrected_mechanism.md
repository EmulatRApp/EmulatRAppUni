<!--
EmulatR V4 -- ES40 memtest ACV: corrected construction mechanism from the
overnight RETIRE_COMPACT trace (2026-07-09 21:11:51 run).  Overturns the
OR-merge base|PA hypothesis and the shift-slip lead; records what the trace
proves and what remains open.  2026-07-10.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience.  [LOCATE] = point-in-time; verify
against the live tree.  _PROVISIONAL = not yet HRM-verified.
-->

# ES40 memtest ACV -- corrected mechanism from the overnight trace (2026-07-10)

## ROOT CAUSE (2026-07-10d) -- Cchip AAR ASIZ encoding is not SRM-consumable

FOUND, closed loop, verified from trace + apisrm source.  The powerup memtest
geometry comes from the Tsunami Cchip Array Address Registers (AAR0-3), read by
the pc264 SRM via memconfig_pc264.c get_array_size/get_array_base.  EmulatR
encodes them in a layout this firmware cannot decode:

  EmulatR (ES40 = Typhoon variant, isExtendedAar=true, 4 GiB):
    reset() tiles 4 GiB as ONE array (maxPerArray=8 GiB) ->
    computeAAR(base=0, 4 GiB, isTyphoon) -> ASIZ nibble 0x9 (extended 4-bit
    code for 4 GB) -> AAR0 = 0x9009 ; AAR1-3 disabled (0).

  Trace confirms the SRM reads exactly this over the CSR path:
    AAR0 @ PA 0x801A0000100 = 0x9009  (kBasePA 0x801A0000000 + 0x100)
    AAR1/2/3 @ 0x140/0x180/0x1C0 = 0

  pc264 SRM decode (memconfig_pc264.c:99,139 + tsunami.h:256):
    get_array_size = (0x9009 >> 12) & 7  = 0x1 -> 2^(1+3) MB = 16 MB   (!!)
    get_array_base = 0x9009 & aar_m_addr(0xFF000000) = 0
    => firmware sees ONE 16 MB array; EmulatR's own decodeAAR (&0xF) sees 4 GiB.

The 4th ASIZ bit falls outside the SRM's 3-bit `& 7`.  The pc264 SRM only groks
the base-21272 AAR layout: 3-bit ASIZ (codes 0-7, max 1 GiB/array) and an 8-bit
base mask [31:24].  So the extended 4-bit ASIZ (0x8/0x9/0xA = 2/4/8 GB, added
for Typhoon/Titan) is UNCONSUMABLE by this firmware.  The SROM-reported total is
4 GiB while the AAR-derived chunk map is 16 MB; that inconsistency drives
collect_memory_chunks + the march to a geometry whose verify address wraps to
0xFFFFFFFF_7F827F5F -> ACV.

WHY IT HID: the prior Tsunami/Typhoon fidelity session made the AAR ENCODING
faithful (extended ASIZ added, doctests pin the encoding in isolation) but never
validated the CONSUMPTION path -- that the actual pc264 SRM can read it back.
Wiring-so-it-can-be-consumed was out of that audit-fix scope.

FIX DIRECTION (discuss-before-code; chipset-fidelity change): the ES40 running
pc264 firmware must present AAR in the layout that firmware consumes -- 3-bit
ASIZ, <=1 GiB/array.  4 GiB tiles as 4x1 GiB arrays (each ASIZ=0x7; bases
0/0x40000000/0x80000000/0xC0000000), which get_array_size/base read back as four
1 GiB banks = 4 GiB and a valid, mappable chunk map.  The isExtendedAar path is
wrong for the pc264 ES40 (extended AAR only fits a firmware that decodes 4-bit
ASIZ).  Ties to the variant-classification muddle (task #5) and IS the Stream B
memory-init root (task #11), now concrete.  Seam: TsunamiCchip.h reset()
(:297) + computeAAR (:1495) + the ES40 variant->isExtendedAar decision.
Everything below is the diagnosis trail that led here.

## One-paragraph verdict

The overnight decoded trace (D:/EmulatR/traces/20260709-211151_srm.trc, ~84.6
GB, RETIRE_COMPACT) captures the birth of the malformed probe address.  It is
NOT built as an OR-merge of a kseg base with a PA (the prior hypothesis).  It
is built by a plain 64-bit SUBQ from a running fill/probe pointer (R0) that is
missing bits [41:32] = 0x3FF.  EmulatR computes every instruction in the chain
correctly; the 42-bit kseg base value that would carry that 0x3FF is NEVER
constructed in any register across the whole capture window.  This reframes the
defect from "a mangled shift" to "a 42-bit kseg/superpage base that is never
injected".  It is consistent with, and sharper than, the 2026-07-09b Hook B
finding: the faulting VA is page-table-walked into Pchip PCI-I/O space with no
kernel read only because it was malformed in the first place -- it should have
been a kseg superpage VA that never reaches the walker.

## REVERSAL (2026-07-10c) -- the base is NOT missing bits; arithmetic is faithful

DEEPER TRACE DIG retires this journal's own central thesis.  The "missing
42-bit kseg base injection" framing (below, and in the Superpage section) is
WRONG.  Rooting the fill-pointer base to its birth shows every op is faithful:

    mem_size = 0x0000000100000000 (4 GiB)   [LDQ from global 0x1525A0, ord ~282045015]
    base     = mem_size - 0x40000000 (1 GiB) = 0xC0000000 (3 GiB)   [SUBQ 0x8B948, faithful]
    R0       = base + (+8 march walk)        = 0xC03EA0A1
    probe    = R2(0x3FC12000) - R0(0xC03EA0A1) = 0xFFFFFFFF_7F827F5F [SUBQ 0x8C308, faithful]

There is NO truncation, NO dropped sign-extension, NO missing 0x3FF.  0xC0000000
is the honest difference 4 GiB - 1 GiB; the probe is the honest negative wrap of
small(R2, ~1 GiB) - large(R0, ~3 GiB).  Both SUBQ inputs are legitimately built:
R2 = r18<<13 (small page-count shift, correctly 32-bit); mem_size = a loaded
config value.  This is the THIRD dead lead (after OR-merge and shift-slip), and
the most seductive because the wrap LOOKED like a base drop.  DO NOT touch the
address math -- the trace proved it faithful.

Two trace facts that further constrain the real cause:
  - 0x1B7DD4 is a GENERIC "LDQ r0,0(r16)" helper, NOT a dedicated memtest probe.
    In the same window it services Pchip PCI-I/O reads (va=0x801A0000100,
    v=0x9009, identity sde=2) far more than memory accesses.  The ACV is this
    helper invoked with a bad R16, not a purpose-built probe.
  - The 0xFFFF.. fill pattern stored to C03E_xxxx is NEVER read back from a
    C03E address in the window.  So the fill store and the faulting read are
    NOT a matched store/verify pair at one location (the verify addr is COMPUTED
    from the fill pointer via R2 - R0, ~1 GiB away).  A simple "march
    store/verify, two framings of the same address" model is not supported.

OPEN (needs the reference, now confirmed on disk at Processor Support/
PalcodeBitsavers/apisrm + firmware/pc264srm.rom): what does the routine at
guest 0x8B694 / 0x5A6B0 / 0x5B058 INTEND -- a physical-framed march, a
kseg-framed march, a table/enumeration walk, or a boundary probe that expects a
fault?  The trace cannot answer it because every instruction is already
faithful; the intended addressing model must come from the source.  Everything
below predates this reversal and is kept for history.

## The birth chain (verified from the trace + es40_decompressed.bin)

First ACV at cyc ~282,090,581 (ord 282,090,3xx), probe PC 0x1B7DD4, faulting
VA 0xFFFFFFFF_7F827F5F.  Working backward, the malformed value is assembled by:

    0x05AFB4  LDA   r0,8(r0)     ; R0 walks a fill pointer, +8 stride
                                 ; ...c03ea099 -> c03ea0a1  (bits[41:32]=0)
    0x05AFAC  STQ   r16,0(r0)    ; fill: *(R0) = 0xFFFFFFFF_FFFFFFFF
                                 ; trace: va==pa==0x0000_0000_c03ea0a1, NO fault
    0x08C308  SUBQ  r2,r0,r0     ; R0 = R2 - R0
                                 ;    = 0x3fc12000 - 0xc03ea0a1
                                 ;    = 0xFFFFFFFF_7F827F5F   (ord 282090292)
    0x05B03C  BIS   zero,r0,r16  ; R16 = R0  (plain register copy, ord 282090299)
    0x01B7DD4 LDQ   r0,0(r16)    ; probe -> DTB miss -> walk -> ACV

Supporting context in the same window:
  - R2 = 0x3fc12000 is the incoming R16 to the subroutine at 0x08C2D0
    (0x08C2F0: BIS zero,r16,r2), itself produced at 0x05AF60 by
    SLL r18,#0xd,r16 (r18 ~ 0x1FE09; a legitimately 32-bit result).
  - The BSR at 0x08C2FC targets 0x01B78F8, which is CALL_PAL 0x09 (CSERVE)
    then RET.  The trace shows this writes NO =>R00 -- cserve does not set R0
    here.  The SUBQ therefore consumes the fill-loop pointer directly.

## What the trace PROVES (not inferred)

1. EmulatR's arithmetic is faithful.  SUBQ r2,r0,r0 and LDA r0,8(r0) both
   produce the correct 64-bit result for their captured inputs.  No
   instruction-semantics divergence is present in this chain.

2. The corrupt R0 = 0x00000000_C03EA0A1 is missing bits [41:32] = 0x3FF.
   For the probe to land in kseg (0xFFFFFC00_7F827F5F -> PA 0x7F827F5F, top of
   ~2.1 GiB installed RAM), R0 would have to be 0x000003FF_C03EA0A1 and then
   SUBQ yields:
       0x3fc12000 - 0x000003FF_C03EA0A1 = 0xFFFFFC00_7F827F5F   (valid kseg)
   XOR(correct probe, corrupt probe) = 0x000003FF_00000000 = bits [41:32].

3. The 0x3FF is never born.  Across the full capture window (170 MB, 633,115
   retired instructions, cyc 281,756,952 .. 282,390,067) NO register ever holds
   a 0x000003FF_ high longword.  Every high longword is 0x00000000 (bare 32-bit)
   or 0xFFFFFFFF (full 32-bit sign-extend), plus byte-fill patterns
   (10101010, 02020202, 0080000d/e).  The kseg base bits are dropped at
   injection, not lost mid-arithmetic.

4. The fill stores to the truncated pointer SUCCEED.  0x05AFAC writes the
   0xFFFF.. test pattern to va==pa==0xC03E_xxxx (~3.2 GiB), sde=2, no fault --
   past the ~2.1 GiB installed top with no non-existent-memory (NXM) response.
   This is the Stream B hole surfacing in the same trace.

## What this OVERTURNS

  - OR-merge base|PA (BIS) hypothesis: WRONG.  The BIS at 0x05B03C is a plain
    copy (BIS zero,r0,r16), not an OR of base with offset.  The address is
    formed by SUBQ, not by OR.  (Corrects 20260709_es40_memtest_acv_vptb_
    verdict.md and 20260710_es40_memtest_session_plan.md.)
  - The "(-1)<<42 vs (-1)<<32 width slip / SLL-SRA shift amount or LDAH width"
    lead: NOT SUPPORTED.  No shift builds the base; it is subtractive kseg
    formation and the 0x3FF is never in a register to be shifted.

## What still HOLDS (unchanged)

  - PA payload 0x7F827F5F is intact on every probe; only the high longword is
    wrong (2026-07-09 mechanism note).
  - VPTB is genuinely 0 (never set); the walker path is reached only because
    the malformed VA[47:41]=0x7F misses SPE[1] (needs 0x7E) -- the kseg
    superpage the correct VA would have hit (20260709_..._vptb_verdict.md).
  - Hook B (2026-07-09b): the installed DTB PTE for 0xFFFFFFFF_7F827F5F is
    valid-but-no-kernel-read into Pchip PCI-I/O.  Reframed: that walk is a
    SYMPTOM of the malformed VA, not the root -- the correct kseg VA never
    walks.

## Refined hypothesis (to test next)

The running fill/probe pointer R0 should carry a 42-bit-masked kseg/superpage
base (bits [41:32] = 0x3FF, i.e. the low half of the kseg region).  EmulatR
keeps it 32-bit (zero high longword).  The 0x3FF is injected by neither the
SUBQ, the LDA, nor R2 (all legitimately 32-bit in this window), so the loss is
either:
  (a) an EmulatR address-generation / superpage / PAL seam that truncates the
      base to 32 bits when it should be 42-bit sign-extended-and-masked
      (V4 leaf bug), or
  (b) the running pointer's original base -- set BEFORE this capture window --
      was already 32-bit, and the chain merely propagates it.
The window evidence (0x3FF never in-register across 633K instructions) favors
(a): an injection seam, not a value lost during arithmetic.  Not yet a
conclusion.

## Next step (recommended; discuss-before-code)

Plan A.3 instrumented value-birth watch (task #10) at pipelineLib/
MemDrainer.h:205-242, gated EMULATR_BRINGUP_PROBES, fire-once/capped.  The
static trace cannot localize the injection because the 0x3FF never lands in a
register to grep for; the watch must fire at the address-generation site
itself.  Predicates: P1 backstop (result == 0xFFFFFFFF_7F827F5F); P2 target
(an address-context result whose high longword is 0x00000000 where a kseg base
was expected, i.e. the pointer that later feeds 0x08C308's SUBQ).  Alternative
first move: extend the trace window backward to root the R0 chain to its first
block base and confirm it is 32-bit from birth.

## Superpage smoking gun (web design, arithmetic + live-code VERIFIED)

The intended probe rides the SPE[1] kseg superpage directly; the memtest never
needed VPTB or a page walk at all:

    correct 0xFFFFFC00_7F827F5F -> VA[47:41]=0x7E -> SPE[1] -> PA 0x7F82_xxxx
                                   (~2 GiB, installed RAM), NO walk, NO VPTB
    corrupt 0xFFFFFFFF_7F827F5F -> VA[47:41]=0x7F -> misses every SPE window
                                   -> walker -> VPTB=0 -> ACV

So the VPTB=0 thread was a red herring induced by the missing base bits: nothing
correct was ever supposed to walk here.  The delta is exactly bits [41:32]
(0x000 intended vs 0x3FF actual).  Fix the VALUE, not the window.

LIVE-CODE VERIFICATION (Cowork, ground truth vs the web analysis):
  - mmuLib/Ev6Translator.h:158 gates SPE[1] on
    ((va >> 41) & 0x7F) == 0x7E && (spe & 0x2) -- faithful to HRM 5.3.9 and
    identical to AXPBox src/AlphaCPU.cpp:1444.  Correct 0x7E maps; corrupt 0x7F
    does not, independent of the enable bit.
  - Ev6Translator.h:305-306 calls tryKsegTranslate(va, cpu.mode, cpu.m_spe, ..)
    -- the SPE-enable IS honored, sourced from the M_CTL-derived cpu.m_spe.
    So "is SPE[1] enabled" is a runtime-value question, not a plumbing gap; the
    plumbing is faithful.  Even with SPE[1] enabled the 0x7F base will not map,
    so SPE state is NOT the current blocker (confirms web).
  - The va==pa fill STQ at 0x05AFAC is a NORMAL store: it reaches translateData,
    misses kseg (0xC03E.. != 0x7E), and is not the physical bypass (that is
    HW_ST/S_PhysAddr, handled in MemDrainer upstream; the only translator
    identity path is EMULATR_BOOTSTRAP_ITB_BYPASS, an #ifdef reset-page debug
    hack over kBootstrapVaLo..Hi).  So va==pa is almost certainly a faithful
    identity DTB entry the SRM installed for low memory -- NOT a translation
    collapse.  This DOWNGRADES "born from a va==pa collapse" as a root
    candidate unless a probe shows the store bypassing the DTB.

## Re-pointed A.3 (watch the fill-pointer BASE, not R16)

R16 is three faithful ops downstream of the defect (fill base -> SUBQ 0x08C308
-> BIS 0x05B03C -> R16).  Point the value-birth watch at the fill-pointer BASE
-- the value R0 holds ENTERING the loop at 0x05AFA0 -- and confirm it is 32-bit
(high longword 0x00000000) where a 42-bit kseg-masked form is required.  Then
trace ONE more level to the base's own birth and classify per the fix-shape
tree below.  (Predicates unchanged: P1 backstop result==0xFFFFFFFF_7F827F5F;
P2 target = the fill-base pointer whose high longword is bare where a kseg base
was due.)

## Fix-shape decision tree (follows the base-birth seam; web design)

The concrete diff depends on WHERE the fill-pointer base is born bare -- exactly
what A.3 must reveal:
  - Born from a memory-size / CSC read  -> root is Stream B; wiring the size CSR
    (B1) makes the base correct.  That is the fix.  (Stream B becomes
    upstream-CAUSAL, not a parallel symptom.)
  - Born from a load of a pointer/table in the decompressed image -> image bytes
    are wrong; fix the decompressor (ties to 20260519_decompressor_pal_overlap_
    findings), not the MMU.
  - Born from an IPR / HW_MFPR read EmulatR models differently -> fix that
    register's value.
  - Born from an earlier store that landed via a va==pa path -> fix that
    translation seam to carry the 42-bit form.  (Per the verification above,
    least likely: the observed va==pa store is a faithful identity DTB hit.)

Secondary faithful check (independent of the base fix): confirm cpu.m_spe & 0x2
is actually set at the memtest, matching what the SRM programmed into I_CTL/
M_CTL.  If the SRM enabled SPE[1] and EmulatR is not honoring it, that is a
separate faithful fix (honor the firmware-set state) needed for the corrected
address to map -- but it is NOT today's blocker.

## References

Trace: D:/EmulatR/traces/20260709-211151_srm.trc (RETIRE_COMPACT via
DecListingSink; EMULATR_RETIRE_TRACE_DIR).  Window ords 282,089,xxx ..
282,090,3xx; first ACV cyc ~282,090,581.
Image: tools/host_decompressor/out/es40_decompressed.bin (load base 0x8000);
PCs 0x05AF60-0x05B048 (fill loop + SUBQ caller 0x08C2D0), 0x08C2D0-0x08C314
(SUBQ subroutine), 0x01B78F8 (CALL_PAL 0x09 CSERVE stub), 0x01B7DD4 (probe).
EmulatR seams (as audited): pipelineLib/MemDrainer.h:205-242 (WB commit, A.3
seam); mmuLib/Ev6Translator.h applyTlbHit :212-232, tryKsegTranslate :145-178;
palBoxLib/grains/PalEntries.cpp SPE derive :1711/1729.
Prior journals: 20260709_es40_memtest_acv_briefing.md, _analysis.md,
_vptb_verdict.md; 20260710_es40_memtest_session_plan.md (mechanism corrected
here).  Memory: [[es40-srm-boot-status]], [[emulatr-es40-diag-knobs]],
[[verify-webchat-claims-vs-live-tree]], [[deliver-bash-as-scripts]].
Tasks: #6 (root cause), #10 (A.3 watch), #11 (Stream B).
