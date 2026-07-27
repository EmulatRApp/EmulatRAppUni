<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-028
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-028 -- SESSION HANDOFF.  The DMA window and the cache-
#                 coherence questions are both CLOSED by evidence
#                 (negatives).  The I/O stack is now verified at EVERY
#                 hop.  %SYSBOOT-F-LDFAIL remains OPEN with no valid
#                 anchor -- read Sec 4 before drawing another window.

    Doc id   : JRN-SCSI-028
    Date     : 2026-07-27
    Status   : PROBE RECORD + instrumentation.  READ THIS FIRST on resume.
    Relates  : JRN-SCSI-026 (halt-10 closed -> SYSBOOT reached),
               JRN-SCSI-027 + its Sec 5b retraction addendum.

--------------------------------------------------------------------------------
## 1. Where the machine is

  DS20, `b dka0.0.0.8.0 -flags 0`, reproducible every run:

    jumping to bootstrap code
    %SYSBOOT-F-LDFAIL, unable to load SYS$PUBLIC_VECTORS.EXE,
                       status = 0013809A

  0x0013809A decodes (architect, on real VMS via f$message) as
  %LOADER-E-BADIMGOFF -- "image offset not within any image section",
  facility 0x13 = LOADER.  So the bytes ARRIVE and DO NOT PARSE.
  Ground truth: the architect boots THIS SAME dka0.vdisk under Charon
  into OpenVMS.  The image is good; the defect is ours.

## 2. The architect's DMA hypothesis -- tested, both halves NEGATIVE

  Q: "Could this be cached bytes?  AXPBox uses probe-scatter, we only
      probe.  Are these target pages not resident via a DMA window?"

  (a) SCATTER-GATHER WINDOWS -- REFUTED for this path.
      NEW probe logs every WSBA write (not the old one-shot dump).
      Across a full boot, EVERY window write has SG=0:
        WSBA0 <- 0x800000              enable=0 SG=0
        WSBA1 <- 0xffffffff80000001    enable=1 SG=0   (1GB, direct-mapped)
        WSBA2/3 <- 0                   enable=0 SG=0
      (console programs them twice, identically; VMS never reprograms
      them.)  ZERO sg=1 translations in the whole run -- and that line
      logs unconditionally, cap or no cap.  Direct-mapped translation
      is applied and correct: pci=0xbff42380 -> pa=0x3ff42380 win=1.

      BUT THE GAP IS REAL AND STAYS ON THE LEDGER: TsunamiPchip::
      translateDmaToPa returns the PCI address UNTRANSLATED when a
      matched window has WSBA<1> (SG) set -- `SG: TODO (page map walk)`.
      Any guest that DOES enable SG gets correct payloads delivered to
      WRONG physical pages, and it is invisible at the device layer
      because payload and SCRIPTS tiling both verify clean.  The 21272
      algorithm is now documented in-code beside the TODO (Sec 3).

  (b) CACHED BYTES -- STRUCTURALLY IMPOSSIBLE HERE.
      There is no data-cache model.  cBoxLib holds only
      BranchPredictor.h + grains/CacheOps.cpp, and that file's own note
      says every body is "a one-line semFlags propagation": MB, WMB,
      ECB, TRAPB, EXCB are architectural no-ops.  CPU loads go straight
      to GuestMemory; DMA writes go straight to GuestMemory::write1.
      One flat store, no coherence layer to be stale against.

## 3. Chipset provenance correction (architect)

  Titan 21274 = DS15 / DS25 / ES45.  The DS20 is Tsunami/Typhoon =
  21272.  `chipsetLib/Titan21274_CsrSpec.h` is a SIBLING part and is NOT
  the authority for the DS20 DMA path; use
  `Processor Support/tsunami_typhoon_21272_hrm.txt` (grep target).

  21272 SG algorithm, recorded for whoever implements it
  (HRM Sec 10.1.4.3 + Figure 8-4):
    - An enabled SG window maps 8KB PCI pages via a PTE array at TBA.
    - PTE<0> = Valid; PTE<22:1> = system page address <34:13>;
      PCI ad<12:0> passes through as the page offset.
    - PTE address = TBA<34:n> : ad<m:13> per the window-size table
      (1MB window -> 1KB PTE area ... 1GB -> 1MB ... 4GB = Window 3,
      DAC only).
    - PTE bits <31> and <28> OR together to form the peer-to-peer PTP
      bit (Sec 8.2.1) -- not needed for plain DMA.
    - dmaWriteBytes already chunks per 4KB page, which is the right
      scaffolding; only the map walk is missing.

## 4. THE I/O STACK IS NOW VERIFIED AT EVERY HOP -- and BADIMGOFF survives it

  | Hop | Verdict | Evidence |
  |---|---|---|
  | file -> target payload | byte-exact | 68/68 READs FNV-matched vs dka0.vdisk (tools/scsi_read_diff.py) |
  | target -> HBA buffer | clean | zero D1 padding events in the SYSBOOT window |
  | HBA -> SCRIPTS tiling | exact cover | 47/47 commands: sum(count)==xfer, dataPos monotone, addresses contiguous |
  | PCI addr -> guest PA | correct | direct-mapped window 1; SG never enabled (Sec 2a) |
  | guest memory -> CPU | no cache | flat store, no coherence layer (Sec 2b) |
  | geometry | driver's own testimony | MODE SELECT block descriptor block-length = 0x000200 |

  NOTE the tiling measurement is at the HBA seam -- those are PCI
  addresses BEFORE translation.  Sec 2a is what closes the remaining
  gap between "the SCRIPTS engine walks the buffer correctly" and "the
  bytes land in the right physical pages".  Together the two cover it.

## 5. NO VALID ANCHOR -- do not reuse 0x42790 (JRN-SCSI-027 Sec 5b)

  Retracted last session, all three by PA-WATCH rather than argument:
    - pc 0x42790 as the loader's failure return: that address takes 60
      OS-era stores (a data buffer) while a value probe saw an
      instruction retire there in a DIFFERENT run.
    - the "r18 section table" at 0x14788: it takes ZERO OS-era stores;
      all 104 are decompressor output at cyc ~6.8e6 writing instruction
      words.
    - "partial populate" from 0xC000 matching EISD[2]+16: coincidence
      in a small search space.

  RULE THAT COST FOUR MIS-AIMED WINDOWS: low memory 0x1xxxx-0x6xxxx is
  REUSED across decompressor / console / OS eras, and a post-halt
  snapshot's page tables are the CONSOLE's.  A PC is meaningless
  without the era it retired in.  Any next anchor must evidence
  EXECUTION and PROVENANCE in ONE run.

  Suggested next instruments (cheapest first):
    N1  Walk back from the console PUTS callback that actually emits
        the LDFAIL text -- tools/crb_conversation_decode.py already
        reconstructs that conversation, and the caller chain leads to
        the loader with provenance attached.
    N2  Snapshot DURING SYSBOOT execution (--snapshot-on-pc, or the
        console marker armed mid-load) so the page tables in the
        snapshot are the OS's own; then translate the known image PAs
        through the CORRECT tables.
    N3  Only then a retire window, with the ISD ground truth below.

  HOST-SIDE GROUND TRUTH (already parsed, still valid):
    image header at LBA 697408: EIHD major 3, SIZE 416, ISDOFF 296,
    ACTIVOFF 112, SYMDBGOFF 160.  Three EISDs at +296/+332/+368,
    secsize 36 each, VBNs 2176 / 138 / 74, chain terminating at 404.
    VERDICT RULE once a valid anchor exists: fields match the image but
    the compare fails -> 32-bit canonicalization lane; fields differ ->
    corruption upstream in memory.

  SURVIVING POSITIVE EVIDENCE (do not re-chase): LDL sign-extension
  produced a correct architected sign-copy (0xFFFFFFFF ->
  0xFFFFFFFFFFFFFFFF) and r2 carried a canonical 0xFFFFFFFF80000000 S0
  base.  The 32-bit canonicalization lanes are sound on that path.

## 6. Instrument-integrity ledger (FOUR instances in two sessions)

  A capped or ungated probe does not fail loudly -- it answers a
  DIFFERENT question confidently.  Fixed so far:
    1. EMULATR_VACTL_DIAG fixed 128 cap -> EMULATR_VACTL_DIAG_N.
    2. EMULATR_DIAG_WREG ignored EMULATR_DIAG_CYCLO/CYCHI entirely.
    3. cmdTrace could not show data-OUT payloads (handleCommand zeroes
       dataTransferred on entry).
    4. THIS SESSION: PCHIP-DMA xlate had a hard cap of 8, so it only
       ever showed console-era translations -> EMULATR_PCHIP_DMA_TRACE_N,
       plus sg=1 always logs regardless of cap.
  Related: cycle floors must be set BELOW every observed handoff
  (drift is ~40M cycles run to run), not threaded with a 1M margin.

## 7. State of the tree

  COMMITTED + PUSHED (origin/v5-tb at 42b19b5):
    06d6587 EXTxH aligned-Rb fix (AARM byte_loc<5:0>) -- killed NOIOVEC
    9763ff3 MTPR_VPTB stores PT__VPTB -- killed halt-10, reached SYSBOOT
    502c876 MODE SELECT(6)/(10) on the virtual disk
    5caa817 SCSI read/tiling probes + WREG cycle gate + scsi_read_diff.py
    d918e78 / 42b19b5 JRN-SCSI-027 + its anchor retraction
    08740ea run-emulatr skill (.claude/skills/run-emulatr/)

  UNCOMMITTED (this session's instrumentation, builds clean, boot-verified):
    chipsetLib/TsunamiPchip.h -- PCHIP-WSBA write probe (two-tier guard
      + no-op fallback so release still compiles), tunable
      EMULATR_PCHIP_DMA_TRACE_N, unconditional sg=1 logging, and the
      21272 SG algorithm documented beside the TODO.
    (tools/make_redist.sh was already modified at session start -- NOT
     ours, leave it.)

  RUN RECIPE (pin the run dir; a second built config silently steals
  the launcher's newest-exe auto-pick):
    RUN_DIR_OVERRIDE=out/build/relwithdebinfo \
    EMULATR_PCHIP_WIN_PROBE=1 EMULATR_PCHIP_DMA_TRACE_N=40 \
    bash tools/run_taskboot001_phase1.sh
  Then type `b dka0.0.0.8.0 -flags 0` at P00>>> (or use
  tools/srm_console_driver.py if no human is at the console).
  Sort run logs by NAME, not mtime -- the build re-mirrors old logs.
