<!--
EmulatR V4 -- ES40 memtest ACV: RESOLVED.  Root cause is a Cchip AAR ASIZ
decode-width mismatch (EmulatR writes extended 4-bit ASIZ; pc264 SRM decodes
3-bit and mis-sizes memory).  Records the confirmed root, the full arc of
retired leads, the HRM-sourced AAR/ASIZ encoding, the power-of-2 tiling-function
design, and the isExtendedAar-as-firmware-property decision.  2026-07-10.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience.  [LOCATE] = point-in-time; verify
against the live tree.  Encoding values are HRM-sourced (Tables 10-14/10-15).
-->

# ES40 memtest ACV -- RESOLVED: AAR ASIZ decode-width mismatch (2026-07-10)

## Root cause (confirmed from the trace + HRM)

The ES40 SRM memory-test ACV at PC 0x1B7DD4 is not an ALU bug, not a translation
bug, not a sparse-memory issue, and not a wrong memorySize value.  It is a Cchip
AAR ASIZ encoding-width mismatch:

  - EmulatR programs the Array Address Register with the EXTENDED 4-bit ASIZ.
    For a single 4 GiB Typhoon array it writes AAR = 0x9009, i.e. ASIZ<15:12> =
    0x9 = 1001 = 4 GB (a Typhoon-only code).
  - The pc264 SRM decodes ASIZ as 3 bits: (AAR >> 12) & 7.  On 0x9009 that is
    0x9 & 7 = 0x1 = 16 MB.  The firmware therefore believes the array is 16 MB.
  - The trace caught the exact CSR read that proves it: read of AAR0 at
    0x801.A000.0100 returning 0x9009.

Every downstream symptom we chased is the consequence of the firmware sizing
memory at 16 MB while 4 GiB is installed: the memtest builds test addresses from
a bad top-of-memory, the fill walks past where the firmware thinks memory ends,
and the verify-address subtraction wraps negative into 0xFFFFFFFF_7F827F5F -- an
address that maps nowhere.  The ACV is the last domino, not the first.

This is a chipset-fidelity gap: a prior audit made the extended ASIZ ENCODING
faithful but did not wire the CONSUMPTION path (whether the loaded firmware can
decode 4-bit ASIZ).  That was out of that audit's scope; it is the whole story
here.

## The arc of retired leads (so the record does not mislead)

Each lead below was live at some point and was retired by evidence, not opinion.
Recording them prevents a future session from re-opening a solved case from the
wrong end.

  1. VPTB propagation defect.  RETIRED.  cpu.vptb=0 is genuine machine state
     (SRM never called MTPR_VPTB; VA_CTL=0x2).  The walker was only ever reached
     because the malformed VA missed SPE[1]; nothing correct walks here.
  2. (-1)<<42 vs (-1)<<32 shift/sign-extend width slip.  RETIRED.  The trace
     shows plain faithful SUBQ/LDA/SLL; no shift builds the base.
  3. OR-merge base|PA.  RETIRED.  The BIS at 0x5B03C is a plain register copy;
     the address is formed by SUBQ, not OR.
  4. Missing 42-bit kseg base injection ("0x3FF never born").  RETIRED.  The
     "3 GiB base" is the honest difference 4 GiB - 1 GiB (SUBQ at 0x8B948 from a
     memory-top of 0x1_00000000), not a truncated kseg pointer.  No base was
     dropped.
  5. CSERVE(0x66) return no-op.  RETIRED.  The subroutine at 0x8C2D0 is address
     arithmetic (arg - R0), R0 is faithfully preserved across the CALL_PAL, and
     the result feeds a load address -- cserve is incidental, not on the fault
     path.
  6. Wrong memorySize value / va==pa translation collapse.  RETIRED.  memorySize
     is 4 GiB and correct; the va==pa fill store is a faithful identity DTB hit;
     the size the FIRMWARE derives is wrong only because it mis-decodes ASIZ.

What every retired lead had in common: EmulatR's per-instruction behavior was
faithful.  The defect was never in an instruction; it was in a chipset CSR the
firmware could not consume.

## HRM-sourced encoding (Tables 10-14 Tsunami / 10-15 Typhoon)

AAR ASIZ <15:12> (bit <15> is the Typhoon extension):

    code    size          notes
    0000    disabled
    0001    16 MB
    0010    32 MB
    0011    64 MB
    0100    128 MB
    0101    256 MB
    0110    512 MB
    0111    1 GB          top of the 3-bit range (codes 0-7)
    1000    2 GB          Typhoon only (bit <15> set)
    1001    4 GB          Typhoon only
    1010    8 GB          Typhoon only
    1011-1111  Reserved   (both chips)

AAR ADDR (base) field <34:24>, 16 MB-granular in position:
    Tsunami (10-14): <31:24> valid  -> 16 MB align, max base ~4 GB
                     (matches aar_m_addr = 0xFF000000).
    Typhoon (10-15): <34:28> valid  -> 256 MB align, max base ~32 GB
                     (<34:32> are the Typhoon-only high bits).

Two decode widths, both properties of the loaded firmware, both caps:
  - ASIZ width: pc264 reads (AAR>>12)&7 -> codes 0-7 -> max bank = 1 GB.
    A 4-bit decoder reaches code 0xA -> max bank = 8 GB.
  - Base width: Tsunami-style <31:24> (~4 GB) vs Typhoon <34:28> (~32 GB).

Note on base granularity: Typhoon <34:28> (256 MB) is coarser than Tsunami
<31:24> (16 MB).  This is non-binding for every supported geometry (all bank
bases are >= 1 GB multiples, expressible under either width) and both reach
bit 34 for the 24 GB top base of a 4x8 GB config.  The 256 MB granularity does
NOT prevent 1 GB-boundary placement (1 GB is a multiple of 256 MB).  Flagged
only because the Tsunami/Typhoon asymmetry could be a genuine spec choice or a
24->28 OCR artifact; either reading yields the same supported tilings.

## The tiling function (spec-level design)

Supported guest totals are restricted to POWERS OF TWO: 4/8/16/32 GiB.  This is
a POLICY choice (it drops 2/12/24 GiB), recorded as policy, not spec.  The HRM
forbids non-power-of-2 ARRAYS; it does not forbid a 12 GiB TOTAL as 8+4.  We
choose not to support non-power-of-2 totals; a request for one must hit a
distinct "unsupported geometry" policy gate, NOT a spec NotRepresentable.

Under the power-of-2-total policy, tiling reduces to four equal power-of-2
banks, and buddy/natural alignment falls out for free (equal power-of-2 banks
laid end to end are each aligned to their own size):

    tile(memSize, bankCount=4, asizWidth, baseWidth)
        -> [ per-bank {base, ASIZ code} ]  |  NotRepresentable
      bankSize = memSize / bankCount
      require bankSize is a power of 2
      require bankSize <= maxBank(asizWidth)     (3-bit -> 1 GB, 4-bit -> 8 GB)
      require (bankCount-1)*bankSize <= maxBase(baseWidth)
      bases = { i*bankSize | i in 0..bankCount-1 }   (each naturally aligned)
      else NotRepresentable  -- error loudly; NEVER clamp or truncate.

There is exactly ONE legal tiling per (total, width), so the earlier
greedy-vs-matched and DIMM-population tie-break questions EVAPORATE for these
geometries.

Representability by decoder (spec-derived, HRM-confirmed codes):

    total    3-bit decoder (pc264)         4-bit decoder
    4 GiB    4x1 GB (code 0111) 0/1/2/3G   4x1 GB (0111)
    8 GiB    NotRepresentable (needs 2 GB) 4x2 GB (1000)
    16 GiB   NotRepresentable              4x4 GB (1001)
    32 GiB   NotRepresentable              4x8 GB (1010)

4 GiB is the ONLY total both decoders support.  The current failing ES40 config
is tile(4 GiB, 4, 3-bit, Tsunami-base) = four 1 GB banks at 0/1G/2G/3G -- the
general function evaluated at one point, NOT a hard-coded constant.  On pc264,
8/16/32 GiB are a faithful, loud NotRepresentable -- which is exactly the answer
whose ABSENCE (a silent 16 MB read) caused this bug.

## The design decision: isExtendedAar is a firmware property, not a platform one

The extended 4-bit ASIZ is not "what an ES40 is."  It is "what a firmware that
decodes 4 bits can consume."  The ES40 hardware is Typhoon (21272) and CAN
express extended ASIZ; the loaded pc264 SRM decodes only 3 bits.  A model that
hard-codes isExtendedAar=true for the ES40 platform asserts a firmware
capability the loaded firmware does not have -- that is the bug one level above
the tiling.

Recommendation:
  - Gate the encoding width on the LOADED FIRMWARE's AAR-decode width (ASIZ and
    base), derived from firmware identity, NOT on the board name.  Feed that
    width plus memSize and the chipset bank count into the single tiling
    resolver.  Platform name feeds the inputs (bank count, which firmware
    loads); it must not branch the tiling algorithm.
  - Do NOT delete the extended path -- it is real and future 4-bit-decoding
    configs need it (8/16/32 GiB).  Gate it, do not remove it.
  - Per the single-resolver principle (model + memorySize authoritative, all
    else derived): decode width wants to be DERIVED from the firmware and
    resolved once.  If it cannot yet be cleanly derived, the honest interim is
    to tile pc264/ES40 as 4x1 GB now (unblocks boot) and file decode-width-as-
    derived-input as the model correction (task #5) -- recording the deferral
    with its reason, not pretending the interim is the general answer.

## Fix direction (held for discuss-before-code)

Faithful fix: tile the pc264 ES40 as 4x1 GB (3-bit ASIZ, code 0111) so the banks
read back mappable, and gate the extended-ASIZ path on firmware decode width.
We do NOT adopt AXPBox's expedient (it NOPs the memtest with 0xE7E00000 over the
three test routines so it never builds the address -- the known-good-but-lossy
cheat this project rejects).  We make EmulatR produce what the hardware produces,
then the memtest runs for real.

Seams to review before proposing the concrete edit (I will want these in front
of me): TsunamiCchip.h reset() tiling and computeAAR [LOCATE], and the ES40 ->
isExtendedAar decision site [LOCATE].  The edit is a resolver change (tiling
function + firmware-derived width), not a constant swap.

## Implementation landed -- interim (2026-07-10, pending build + do-no-harm)

Applied (NOT yet compiled -- MSVC build is Tim's; do-no-harm gate below stands):
  - TsunamiCchip.h: added ctor arg `bool extendedAsizDecode = false` + member
    m_extendedAsizDecode; reset() now gates the extended tier on that member
    instead of (Typhoon || Titan).  Default false -> ES40/Typhoon tiles the
    existing greedy fill to 4 x 1 GB (ASIZ 0x7), pc264-consumable.  R3 hard-stop
    reworded as the "not representable by the loaded firmware's decoder" refusal.
    AAR-block + reset() comments record encoding-vs-consumption.
  - TsunamiVariant.h:40: corrected the "ES40 -> 8GB arrays/32GB" comment.
  - TsunamiChipset.cpp:48: TODO(aar-decode-width) at the m_cchip ctor -- task #5
    passes the firmware-derived width here.
  - test_ticket02_aar_encoding.cpp: the three extended-encoding cases opt into
    the 4-bit decoder (extendedAsizDecode=true); added M3 cases pinning the
    Typhoon DEFAULT 3-bit permutation (the one the old suite skipped): 4 GB ->
    4 x 1 GB ASIZ 0x7, and firmware-formula round-trip == memSize for 1/2/4 GB.

INTERIM boundaries (deferred to task #5, recorded honestly, NOT shipped as done):
  - Tiling stays GREEDY, not the power-of-2-total / four-equal-bank policy.  It
    COINCIDES with 4 x 1 GB for the ES40 4 GiB/3-bit case; it diverges for 4-bit
    8/16 GiB (greedy 1x8 / 2x8 vs policy 4x2 / 4x4).  No consumer today.
  - extendedAsizDecode is a ctor arg defaulting false, NOT yet derived from the
    firmware image; production callers pass nothing (3-bit).  Deriving it is #5.
  - The NotRepresentable path is std::abort() (loud) but NOT unit-testable
    (exceptions disabled).  Task #5 should make it a representability predicate
    that RETURNS a status so the refusal is doctest-able across the size matrix.

Follow-up (2026-07-10, from the first do-no-harm build -- the gate did its job):
  - The build COMPILED clean (Emulatr + Emulatr_tests).  The doctest run then
    caught a real blast-radius regression: five NON-memory tests constructed
    8/32 GiB Typhoon chipsets (test_mmio_csc_roundtrip fixture 32 GB;
    test_ticket01_5/01_dispatch/04_timer 8 GB via TsunamiChipset;
    test_ticket03_interrupts 8 GB via TsunamiCchip) to exercise MMIO / dispatch
    / timer / interrupts / variant-binding -- size incidental.  With the default
    3-bit cap those now correctly hit NotRepresentable and terminated the test
    binary.  FIX: reduced those five to <=4 GiB (representable on 3-bit); the AAR
    extended cases already opt into 4-bit.  Threading extendedAsizDecode through
    TsunamiChipset (so a genuine 4-bit-firmware 32 GB config is testable, and so
    production can pass the firmware-derived width) is folded into task #5.
  - NotRepresentable refusal changed std::abort() -> std::fflush(stderr) +
    std::exit(EXIT_FAILURE) (added <cstdlib>): orderly fail-fast, no SIGABRT /
    crash dialog, per Tim's "clean shutdown + exit".  (Still terminates the
    process -- the doctest-able returned-status form remains task #5.)

## Do-no-harm gate

reset() tiling feeds EVERY platform.  Any change to reset()/computeAAR or the
isExtendedAar decision requires: full suite + DS10 + DS20 + ES40 boot-to-P00
green.  Capture DS10/DS20 AAR readback before and after -- they inherit the same
tiling mechanism, and a 3-bit assumption correct for them today could shift
under a width-derived resolver.  The standing notes already record a Typhoon-flip
regression caught only by the full suite; this is that blast radius.

## Spec confirmations pinned (no longer _PROVISIONAL)

  - ASIZ 1001=4GB, 1010=8GB, 1011-1111=Reserved -- CONFIRMED HRM Table 10-15.
  - Typhoon base valid width <34:28> -- CONFIRMED HRM Table 10-15 (non-binding
    for supported geometries, see note above).
  - Tsunami base valid width <31:24>; max bank 1 GB (code 0111) -- CONFIRMED
    HRM Table 10-14.
  - pc264 3-bit ASIZ decode ((AAR>>12)&7) reads 0x9009 as 16 MB -- CONFIRMED
    from the trace CSR read.

## Open items

  - pc264 base-decode width (Tsunami <31:24> assumed) -- confirm from the pc264
    decode path; non-binding for the 4 GiB config (3 GiB top base fits either).
  - Firmware AAR-decode-width derivation mechanism (from image identity) --
    task #5; the general-case dependency for 8/16/32 GiB.
  - Whether DS10/DS20 configs exercise any total other than what 3-bit supports
    -- verify in the do-no-harm pass.

## References

Authoritative:
  - Alpha 21272 (Tsunami/Typhoon) HRM section 10.2.2.5, Table 10-14 (Tsunami
    AAR: ASIZ 0000-0111, base <31:24>), Table 10-15 (Typhoon AAR: ASIZ adds
    1000=2GB/1001=4GB/1010=8GB, base <34:28>, <15> Typhoon extension bit);
    section 9.4 supported array sizes; AAR0-3 at 801.A000.0100/0140/0180/01C0.
Trace:
  - D:/EmulatR/traces/20260709-211151_srm.trc: AAR0 read 0x801.A000.0100 = 0x9009;
    memory-top 0x1_00000000 load (ord ~282045015, global 0x1525A0); base SUBQ
    0x8B948 (4 GiB - 1 GiB); fill walk 0x5AFB4; verify SUBQ 0x8C308; probe
    0x1B7DD4.
EmulatR seams [LOCATE]:
  - chipsetLib/TsunamiCchip.h reset() + computeAAR; the ES40 -> isExtendedAar
    decision site; platform resolver (model + memorySize -> chipset geometry).
Reference implementation (witness only, rejected expedient):
  - AXPBox: 0xE7E00000 NOP over the three memtest routines in LoadROM.
Prior journals (leads corrected here):
  - 20260709_es40_memtest_acv_briefing.md, _analysis.md, _vptb_verdict.md;
    20260710_es40_memtest_acv_trace_corrected_mechanism.md (missing-0x3FF frame
    RETIRED); 20260710_es40_memtest_acv_cserve_addendum.md (CSERVE lead RETIRED);
    20260709_es40_memtest_next_steps_and_meminit_csr.md (Stream B grounds here).
Memory to correct: [[es40-srm-boot-status]] (retire VPTB and 0x3FF framing;
  record AAR-ASIZ root); [[emulatr-es40-diag-knobs]].
Tasks: #6 (root cause -- RESOLVED), #5 (variant model / firmware decode width),
  #11 (Stream B memory-init -- grounded concretely by this).
