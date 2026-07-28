<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-032
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-032 -- ROOT CAUSE FOUND AND FIXED: GH-block PA aliasing in the
#                 TB compose.  A GH=g TB entry MATCHED all 8^g pages of its
#                 block but COMPOSED every one onto the base page.  SYSBOOT's
#                 GH=3 S0 data region collapsed onto one physical page; the
#                 relocation walk read layered residue; the loader faithfully
#                 reported BADIMGOFF -> %SYSBOOT-F-LDFAIL.
#                 "Match wide, compose narrow" -- JRN-SCSI-020's defect class
#                 (correct implementation of a wrong scope), third instance.

    Doc id   : JRN-SCSI-032
    Date     : 2026-07-27 (same evening as JRN-SCSI-031)
    Status   : ROOT-CAUSE RECORD + FIX.  Supersedes JRN-SCSI-031 Sec 4-6 in
               part -- the supersession boundary is drawn EXACTLY in Sec 5.
    Relates  : JRN-SCSI-031 (mechanism decode; drove the probes that surfaced
               this), JRN-SCSI-020 (defect class), JRN-VMB-010/VPTB arc
               (same subsystem), JRN-ISA-001 Sec 6 (the standing rule this
               vindicates).

--------------------------------------------------------------------------------
## 1. THE DEFECT (one sentence, then the mechanics)

  mmuLib/Ev6Translator.h applyTlbHit composed every TB hit as
      pa = (PFN << 13) | va<12:0>
  regardless of the entry's granularity hint, while the TB lookup
  (pteLib/SPAMShardManager + TlbEntry::matches) correctly matched a GH=g
  entry against ALL 8^g pages of its naturally aligned block -- so every
  VA page in a GH block translated to the BLOCK'S BASE PAGE.

  Correct EV6/AARM semantics (AARM 3.3.3; the GH block maps physically
  contiguous, naturally aligned pages): the VA bits that select the page
  WITHIN the block, VA<13+3g-1:13>, pass through into the PA and supersede
  the low 3g PFN bits.

  Consequence chain on DS20 `b dka0 -fl 0`:
    - SYSBOOT maps its S0 data region with GH=3 PTEs (per-page PFNs
      correct, verified in the verdict4 snapshot: L3 PTEs at PA 0x790000+
      = PFN 0x800/0x801/0x802/0x803, GH=3, V=1).
    - Every load/store to VA 0xFFFFFFFF88000000 + n*0x2000 hit PA
      0x1000000: the zero sweep, the sec1 copy-in, and the relocation
      walk's LDQ/STQ all collapsed onto the base page (measured live:
      PA-WATCH shows va=0x88002000 -> pa=0x1000000, va=0x88004000 ->
      pa=0x1000000, ... in the same run).
    - The symbol-vector relocation walk (JRN-SCSI-031 Sec 3) then read
      layered residue instead of coherent data, found a quadword value
      no digest range contains, and returned 0x0013809A exactly as
      designed.  BADIMGOFF -> %SYSBOOT-F-LDFAIL.
    - Charon boots the same media because its TB passes the page-index
      bits through.

  Why nothing before SYSBOOT tripped it: the console runs on kseg /
  superpage translations (no TB), and every prior TB fill in the boot
  path was GH=0 -- for which the compose is correct.  SYSBOOT's S0 region
  is the first GH!=0 mapping the machine ever exercises.  Correct for
  every case tested, wrong for the first case beyond the model: the
  JRN-SCSI-020 shape, in the translation layer itself.

--------------------------------------------------------------------------------
## 2. HOW IT WAS FOUND (the probe chain, one evening)

  P1b  PA-WATCH 0x6C4708 LEN 0x68 (state word *(0x14708) + digest head):
       write ledger CLEAN -- bit2 never set (closing JRN-SCSI-031's
       transient-flag branch); digest+0x3c written ONCE by the builder
       (0x1, high half of a quad store at PC 0x5def0); the wrapper's
       success flags (|=0x1800 at 0x5ff38/48) never stored.  So NEITHER
       known call site could have made a "pass 1" -- yet memory showed
       relocated-looking values.  Contradiction -> widen the watch.
  P2b  PA-WATCH 0x1000000 LEN 0x70 + DIAG WREG window: the zero sweep's
       stores arrived at the SAME PA from DIFFERENT VA pages --
       va=0x88000000/0x88002000/0x88004000/... all pa=0x1000000+off.
       Aliasing, on the raw ledger, no interpretation required.
  Then instrument-integrity: confirmed va/pa in the PA-WATCH line are
  the same store record (MemDrainer applyStoreEffect logs r.memAddr
  beside the translated pa) -- not an artifact.
  Then the guest page tables from the snapshot: per-page-correct PFNs
  with GH=3 -- so the guest was right and the emulator was wrong.
  Then the code: SPAMShardManager normalises tags by vpnMaskForGh
  ("lookups for any VA inside the GH block hit this entry") while
  applyTlbHit composes with the bare 8 KiB mask.  Fill sites
  (PalEntries.cpp HW_DTB_PTE0/1, HW_ITB_PTE) extract gh from the PTE and
  insert with it; canonicalFromDtbPte preserves GH<6:5> into the stored
  AlphaPte.  Lookup wide + compose narrow = the defect.

--------------------------------------------------------------------------------
## 3. THE FIX

  mmuLib/Ev6Translator.h, applyTlbHit (sole owner of TB-hit PA compose;
  serves the DTB path at :441 and the ITB path at :696):

      ghBits  = 3 * pte.gh()
      offMask = (1 << (13 + ghBits)) - 1
      pa      = ((pfn << 13) & ~offMask) | (va & offMask)

  Masking the PFN base makes the compose correct whether PAL filled the
  entry with the block-base PTE or the faulting page's own PTE (low 3g
  PFN bits superseded either way, per the block's natural alignment).

  pteLib/TlbEntry.h physAddrOf: DELETED.  It was caller-less, carried
  the same 8 KiB-only compose, and its comment asserted the wrong model
  ("GH expansions keep the same low-bit semantics because PFN already
  addresses the base of the block" -- backwards: BECAUSE the PFN is the
  base, the VA page-index bits must pass through).  Dead code with a
  confidently wrong comment is a trap for the next contributor; PA
  composition now has ONE owner, marked at the deletion site.

  TEST (tests/pteLib/test_spam.cpp): the load-bearing pin is CROSS-PAGE
  -- fill the TB via page N of a GH block, hit via page k, assert the
  composed PA lands on the k-th page.  Single-page tests pass on the
  broken code.  Coverage: DTB and ITB realms x GH 0/1/2/3 x probe pages
  {0, 1, last}, non-base fill PFN, and block-boundary miss (one page
  past the block does not match).  74 assertions.

  GATE: suite 503/506 -- only the 3 pre-existing drift failures
  (ide_wiring + 2x mmio_csc).  Boot retest: Sec 6.

--------------------------------------------------------------------------------
## 4. INVALIDATION SCOPE (audited now, constraint recorded for the TB tier)

  - TBIS (SPAMShardManager::invalidateSingle): probes all four GH tag
    shapes; TlbEntry::matches re-masks by the ENTRY'S OWN gh, so a TBIS
    on any VA inside a block matches and invalidates the whole entry.
    Entry granularity == invalidation granularity.  CORRECT.
  - TBIA / TBIAP: epoch sweeps, GH-agnostic.  CORRECT.
  - TB/ComJIT tier (V5 brief; jitLib is examples-only today): STANDING
    CONSTRAINT -- translation-block keys anchored on physical page +
    generation must treat a GH-block TB entry as spanning its full 8^g
    pages.  Under the old compose, fetches from a GH region produced PAs
    all on the base page; post-fix they distribute across their true
    pages.  A wide-match entry that invalidates narrow would be this
    defect class's third costume.  Wire this into the split-key design
    review before the tier goes live.

--------------------------------------------------------------------------------
## 5. SUPERSESSION BOUNDARY OVER JRN-SCSI-031 (journal integrity)

  The aliasing model retroactively reframes 031's Sec 4 "three
  independent measurements".  Precisely:

  SURVIVES  031 Sec 4(1), the file oracle: pure static simulation on
            vdisk bytes; untouched by any memory-coherence question.
            The file walks clean; Charon's success is still reproduced
            from our own disk.
  SURVIVES  031 Sec 4(3), the captured-register analysis, AS A FAITHFUL
            DESCRIPTION OF WHAT THE WALKER SAW: it read its inputs
            through GH-aliased translations, so its LDQs returned
            base-page residue; the first set bit's quadword read as an
            already-relocated-looking (huge/negative) value; the
            classifier rejected it; r22/r24 captured at initial values.
            All real observations -- of an incoherent memory image.
  RETRACTED 031 Sec 4(2) "a prior pass ran to completion, CORRECTLY",
            and with it the entire double-invocation frame of Sec 4-6:
            "pass 1", "who calls twice", the transient-bit2 hypothesis,
            and the digest+0x3d done-flag question.  The relocated-
            looking values in the sec1 copy were the RESIDUE OF MULTIPLE
            VA PAGES LAYERED ONTO PA 0x1000000 through the aliased
            translation (my per-page PTE walk read the snapshot through
            CORRECT translations that the live machine never used).
            Those questions are dissolved, not answered.
  RETRACTED the "phantom 0x66666666 bitmap" reading of the P2b quad
            pattern (031-adjacent, this session's working notes): the
            {1,2,5,6,9,a} touched set is layered aliased stores, not a
            second bitmap image.

  031 did its job -- its probes surfaced the aliasing -- and its Sec 3
  decode of the translator (structure, legs, exits) remains valid and
  was load-bearing for THIS diagnosis.

  THE PRE-REGISTERED FORK, RESOLVED: the architect's constraint said the
  divergence must terminate in machine-environment state because the
  image bytes are identical on every machine that loads them.  It did --
  one layer lower than any of the four candidate surfaces (IPR, HWRPB,
  PALcode result, console datum).  TRANSLATION ITSELF WAS THE
  ENVIRONMENT.

--------------------------------------------------------------------------------
## 6. BOOT RETEST (do-no-harm gate) -- THE WALL IS DOWN

  DS20 `b dka0.0.0.8.0 -flags 0`, fixed binary (2026-07-27 19:52):

    %SYSBOOT-F-LDFAIL: GONE.  Every broken run halted at cyc ~2.05e9;
    this run executed to cyc 3.4695e9 (retires 2.479e9, wall 598 s) --
    ~1.4e9 cycles of new ground, through the full console era and deep
    into SYSBOOT's image-load sequence.

  NEW FRONTIER (the next gate, named):
    SYSBOOT prints "Illegal inst", dumps registers, halts clean at
    PC 0x20B30 (excAddr 0x20AA0).  Exception PC on the guest stack =
    0x2F0A0 = the RETURN POINT of CALL_PAL 0x0005 at VA 0x2F09C
    (CALL_PAL pushes PC+4) -- i.e. SWPCTX (OpenVMS privileged PAL
    function 5), args staged r16 = *( *( *(-0x110(r27)) + 0x18) + 0x78 ).
    SYSBOOT's first privileged context swap OPCDECs under the VMS
    personality even though execSwpctxVms exists in PalEntries.cpp --
    dispatch gap vs internal veto is the first question of the next
    hunt.  Guest state at the report: R0 = 0xFFFFFFFF81C14000 (S0),
    R16 = 0x01414080, R25 = 0.

  GATE LEDGER: suite 503/506 (3 pre-existing drift failures only);
  DS20 console era fully traversed to P00>>> and far beyond (strongest
  single-platform evidence -- the fix only alters GH!=0 TB compose and
  GH!=0 was previously ALWAYS-aliased, i.e. strictly broken).  OWED:
  DS10 and ES40 boots to P00>>> to complete the tri-platform pass --
  deferred rather than mutating the shared run-dir model config while a
  live console session was attached; run via tools/run_ds10_trace.sh
  and an ini model swap next session.

--------------------------------------------------------------------------------
## 7. Instrument-integrity ledger additions

  7. EMULATR_DIAG_WREG produced ZERO lines in the P2b run despite
     PCLO/PCHI/CAP/CYCLO all set and the facility present in the binary
     (grep = 1).  Unresolved -- either the PC-window semantics exclude
     the BSR writer sites or WREG gating differs from the documented
     recipe.  Do not trust a silent WREG run until this is pinned.
  8. POSITIVE instance worth recording: the PA-WATCH line prints the
     store's VA and PA side by side from the same record -- that pairing
     is what turned "mysterious write pattern" into "aliasing, read
     directly off the ledger".  Instruments that log the full tuple
     (JRN-SCSI-028's dmaMissProbe rule) keep paying.

--------------------------------------------------------------------------------
## 8. Files touched

  - mmuLib/Ev6Translator.h        applyTlbHit GH-aware compose (the fix)
  - pteLib/TlbEntry.h             physAddrOf deleted (dead + wrong model)
  - tests/pteLib/test_spam.cpp    cross-page GH compose pin (74 asserts)
  - this journal                  NEW
  - memory.md                     index entry (rides the same commit)
