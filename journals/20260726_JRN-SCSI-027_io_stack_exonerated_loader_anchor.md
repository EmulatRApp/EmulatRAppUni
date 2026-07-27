<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-027
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-027 -- THE I/O STACK IS EXONERATED BY EVIDENCE, LAYER BY
#                 LAYER.  %SYSBOOT-F-LDFAIL = %LOADER-E-BADIMGOFF:
#                 correct bytes, correctly placed, WRONG OFFSET
#                 computed from them.  Loader anchor: pc 0x42790.

    Doc id   : JRN-SCSI-027
    Date     : 2026-07-26
    Status   : PROBE RECORD + instrument fixes.  MODE SELECT landed
               separately (commit 502c876).
    Relates  : JRN-SCSI-026 (halt-10 closed -> SYSBOOT reached).
    Ground truth: the architect booted Charon DS20 from THIS SAME
               dka0.vdisk into OpenVMS (DCL prompt, f$message run).
               The image is provably good; any corruption is OURS.

--------------------------------------------------------------------------------
## 1. The status decoded (architect, on real VMS)

    $ write sys$output f$message("%X0013809A")
    %LOADER-E-BADIMGOFF, image offset not within any image section

  Facility 0x13 = LOADER (the executive image loader inside SYSBOOT) --
  NOT a device/driver facility as JRN-SCSI-026 Sec 7 guessed; that
  evidence line is CORRECTED here.  BADIMGOFF is specific: the loader
  GOT the bytes, parsed the image header and section table, and found
  an offset landing in no section.  "The data is internally
  inconsistent", not "the device refused a command".

## 2. The I/O stack, verified layer by layer (the session's real yield)

  | Layer | Verdict | Evidence |
  |---|---|---|
  | Backing file | 512-byte image, exact | 4290600960 B = 8,380,080 x 512, no remainder |
  | INQUIRY identity | type 0x00 direct-access | no CD 4:1 conversion invoked |
  | READ CAPACITY | last-LBA + bs 512 | `blockCount-1` (no off-by-one) from m_media->blockSize() |
  | MODE SENSE bd | agrees | same blockSize() source, cannot diverge |
  | MODE SELECT bd | **driver's own testimony** | captured parameter list: hdr bd_len=8, block length = 0x000200 |
  | Target payload | **byte-exact** | 68/68 READs FNV-matched vs dka0.vdisk at their LBAs |
  | Target->HBA buffer | clean | ZERO D1 padding events in the SYSBOOT window (all 63 were console-era) |
  | HBA->guest RAM | **exact tiling** | 47/47 commands: sum(count)==xfer, dataPos monotone, guest PAs contiguous |
  | Delivery mode | direct PA poke | m_dmaWrite writes a guest PHYSICAL address; no bus-master window translation in this model (scoped gap, now evidenced) |

  Every device-side suspect is RETIRED: geometry, block math, the
  READ(6)/READ(10) split, allocation-length padding leaking into READ
  data-in, per-command cap truncation, and multi-MOVE pointer advance.
  This is the first time this device model has been verified by
  evidence rather than source-reading.
  Tool: NEW tools/scsi_read_diff.py (re-reads each traced LBA from the
  image, recomputes the same FNV, names the divergence shape).

## 3. MODE SELECT was a real gap -- but not the wall

  SYSBOOT issues MODE SELECT(6) twice: `cdb=[15 10 00 00 0C 00]` --
  PF bit set, parameter list length 0x0C = 12 = 4-byte header + 8-byte
  block descriptor, ZERO mode pages.  The device answered ILLEGAL
  REQUEST (invalid opcode) and SYSBOOT abandoned the load.  Implemented
  probe-driven (commit 502c876): validate the list, reject only a block
  length differing from the media's, accept pages that change nothing.
  LDFAIL persists after the fix -- reads DID complete, so the command
  layer was never the wall.

## 4. Instrument defects found + fixed (all three cost a boot each)

  A capped or ungated probe does not fail loudly -- it answers a
  DIFFERENT question confidently.  Three instances today:

  1. EMULATR_VACTL_DIAG: fixed 128 cap filled during console init
     (~cyc 1.17e9), a BILLION cycles before the OS era it was aimed at.
     FIX: EMULATR_VACTL_DIAG_N (JRN-SCSI-025).
  2. EMULATR_DIAG_WREG ignored EMULATR_DIAG_CYCLO/CYCHI entirely -- the
     cycle window gated only the DIAG-PC path, despite the facility's
     own comment describing it as the way to pin a wide PC window to a
     cycle span.  Every register probe was implicitly console-era-only;
     a past "the write never happens" conclusion drawn from one would
     have been an artifact.  FIX: same gate, same defaults, on the WREG
     arm (pipelineLib/PipelineDriver.h).
  3. cmdTrace could not show data-OUT payloads: handleCommand() zeroes
     cmd.dataTransferred on entry (it is the target's data-IN output),
     so MODE SELECT's parameter list logged as "-".  FIX: fall back to
     the delivered buffer -- which is what produced Sec 2's block
     descriptor testimony.

  METHODOLOGICAL (no code fix): a POST-HALT snapshot's page tables are
  the CONSOLE's, not the OS's.  Deriving an OS-era VA from one is
  invalid -- the same physical page is mapped at a different VA (or not
  at all) once the console regains control.  Cost two mis-aimed windows,
  compounded once by a page-base arithmetic error (PFN 0x388 covers PA
  0x710000-0x711FFF; its base is 0x710000, not 0x711000).  For OS-era
  code the anchor must be a VALUE, a physical address, or a snapshot
  taken DURING execution.

## 5. The loader anchor (where the next session starts)

  Anchoring on the VALUE -- assumption-free, since 0x0013809A is
  invariant while addresses are not -- with the whole PC range open and
  the cycle floor tied to this run's OS handoff (CSERVE-START-A2):

    EMULATR_DIAG_PCLO=0x0 EMULATR_DIAG_PCHI=0xffffffff \
    EMULATR_DIAG_CYCLO=<handoff+5e6> EMULATR_DIAG_CAP=50000 \
    EMULATR_DIAG_WREG=0 EMULATR_DIAG_WMIN=0x13809a

    DIAG-WR: cyc=1895362359 pc=0x42790 enc=0xa41bfff0  R0<=0x13809a
    DIAG-WR: cyc=1895362372 pc=0x5ff0c enc=0x47e40400  R0<=0x13809a
    DIAG-WR: cyc=1895362387 pc=0x5e0bc enc=0x47e50400  R0<=0x13809a
    DIAG-WR: cyc=1895362455 pc=0xd150  enc=0x44000400  R0<=0x13809a

  pc=0x42790 `LDQ r0,-0x10(r27)` is the ORIGIN: the status is LOADED
  from an r27-relative linkage cell, not built by LDAH/LDA.  (The
  LDAH #0x14 / LDA #-0x7f66 pair found statically at PA 0x7116ac is a
  DIFFERENT site that never retires on this path -- which is exactly
  why the wider net was needed.)  The three later PCs are register
  copies propagating it up the return chain.

  NEXT: retire window upstream of 0x42790 to catch the caller's
  section-descriptor arithmetic -- the LDL loads of ISD fields, the
  ADDL/S4ADDL canonicalization, and the CMPULT bounds compares.  Diff
  the loaded values against the host-side parse of the image header at
  LBA 697408 (EIHD major 3, ISDOFF 296; EISDs at +296/+332/+368 with
  VBNs 2176/138/74, chain terminating at 404 vs EIHD$L_SIZE 416).
  VERDICT RULE: fields match the image but the compare fails -> the
  32-bit canonicalization lane (LDL sign-extension / ADDL) -- EXTxH's
  genre, and the AARM-pseudocode-per-instruction audit applies.  Fields
  do NOT match -> corruption upstream of the arithmetic, and the hunt
  moves back into memory.

## 6. Files touched

  - deviceLib/Tsunami/Ncr53C810.h   trace: LBA/cnt + FNV payload
                                    checksum + head/tail + full CDB;
                                    data-OUT payload fallback; NEW
                                    N810-MOVE tiling probe (two-tier:
                                    EMULATR_BRINGUP_PROBES compile
                                    guard + EMULATR_SCSI_MOVE_PROBE env
                                    key, the ITBPROBE shape)
  - pipelineLib/PipelineDriver.h    WREG cycle-gate fix (Sec 4.2)
  - tools/scsi_read_diff.py         NEW  LBA byte-diff vs the image
  - tests/palBoxLib/test_palentries.cpp  scoped-enum CHECK fix
  - this journal                    NEW

  HOUSEKEEPING FILED (in-code, deliberately not done here): this file's
  older traceOn()/trace()/cmdTrace() are runtime-gated only and predate
  the compile-guard convention.  Bringing them into conformance is a
  separate sweep -- mixing it into a diagnostics landing would muddy
  both.  Also owed: verify the two-tier guard excludes cleanly in a
  scratch build configured -DEMULATR_BRINGUP_PROBES=OFF (this tree has
  it ON, so it can demonstrate presence but not exclusion).
