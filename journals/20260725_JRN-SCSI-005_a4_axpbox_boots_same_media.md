<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-005
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-005 -- A4 EXECUTED: AXPBox boots the SAME dka0.vdisk into
#                 OpenVMS V8.3 -- NOIOVEC is an EMULATR ENVIRONMENT GAP;
#                 media, APB image, and the SCSI stack are ALL exonerated.

    Doc id   : JRN-SCSI-005
    Date     : 2026-07-25
    Status   : ANALYSIS RECORD.  No EmulatR code changed.
    Relates  : JRN-SCSI-004 (P3 retest + identical-footprint), JRN-VMB-019/
               -020/-021/-022 (NOIOVEC track), JRN-SCSI-001/-002/-003.
    Artifacts: scratchpad a4b_axpbox_console_transcript.log (full show
               config/show dev/boot transcript), axpbox_t2/ (runnable
               harness), srm_telnet_driver.py (telnet-safe console driver),
               gate_f3tail.bin + snap_extract.py (A3p3 material).

--------------------------------------------------------------------------------
## 1. The A4 result (decisive)

  Setup: AXPBox ES40 (cl67 SRM V7.3-1, Feb 27 2007), Sym53C810 at PCI 0/3,
  EmulatR's dka0.vdisk mounted READ-ONLY as disk0.0.  Console scripted over
  telnet 21264.  `show dev`: dka0.0.0.3.0 (RZ58), pka0.7.0.3.0.

  `b dka0` -> "reading 1226 blocks" -> "image_start = 0, image_bytes =
  99400(627712)" (BYTE-COUNT-IDENTICAL APB) -> "jumping to bootstrap code"
  -> **"OpenVMS (TM) Alpha Operating System, Version V8.3"** -> date/time
  prompt (TOY unset; read-only system disk).  NO NOIOVEC.

  The disk is a V8.3 system disk.  Its APB executes its resolver to ACCEPT
  on AXPBox with a SCSI topology; on EmulatR (DS20) the same APB image runs
  the identical probe-only walk for BOTH "IDE 0 105 0 0 0 0 0" and
  "SCSI 0 8 0 0 0 0 0" and dies %APB-F-NOIOVEC (JRN-SCSI-004).

--------------------------------------------------------------------------------
## 2. What this kills / what survives

  KILLED: (a) media/APB vintage theory ("APB predates IDE/SCSI boot") --
  the image boots V8.3 elsewhere; (b) any residual SCSI-stack suspicion --
  EmulatR's HBA + pke enumeration + APB load were already clean; (c) the
  V8.2-media-swap as the required unblock (it may still be useful, but is
  no longer the hypothesis).

  SURVIVES (now THE hypothesis): EMULATR ENVIRONMENT GAP -- an input APB
  derives its resolver MODE (VMB-021: R7=1 probe-only vs execute) from
  differs between the two boots.  Candidate inputs, ranked:
    C1. Console env vars via CSERVE CALLBACK GETENV (booted_dev/boot_dev
        formats, boot_osflags, bootdef_dev...).  DS20 vs ES40 console
        builds may format these differently.
    C2. HWRPB content (systype/sysvar, CTB, MEMDSC, per-CPU slots, GCT/FRU
        pointer).  Note AXPBox GCT/FRU at 1c8000 vs EmulatR DS20 layout.
    C3. Machine/console identity: A4 ran an ES40 console; EmulatR runs are
        DS20.  A DS20-vs-ES40 confound exists (see Sec 4.1).
    C4. Bootstrap load base: AXPBox base=0x200000 (256 MB), EmulatR
        base=0x5bc000 (4 GiB) -- memory-size-derived; A2's clean replay
        makes a base-sensitivity bug unlikely but it is an observable diff.

--------------------------------------------------------------------------------
## 3. A3p3 progress (the 0xf3-tail gate, from the SCSI-run halt snapshot)

  Snapshot disassembly (VA-faithful; APB.EXE;1 file offsets are NOT linear
  vs VA -- a ~0x2c-class skew varies by region; use snap_extract.py) of
  0x20096d00-0x20096e7f names the exit chain:

    0x20096d30  LDQ  r20,0x18(fp)          ; token word
    0x20096d34  SRL/BLBS bit 10            ; bit10 SET -> probe-only path
      CLEAR -> update key record (INSWL/MSKWL byte surgery), BR back to
               the walk loop (continue matching)
      SET   -> 0x20096dc0: scanner(r17=0 whitespace probe) then
               scanner(r17=4 digit scan) [BSRs to 0x20096f80 family],
               store scan result into key record,
    0x20096e04  BNE r0 -> status epilogue  ; scan result nonzero = mismatch
    0x20096e44  LDL r1,0x28(fp)            ; result slot
    0x20096e4c  ZAPNOT r1 -> r0
    0x20096e50-54  SLL #0x24 / SRL #0x27   ; r1 = result bits 27:3
    0x20096e58  CMOVEQ r1,r17,r0           ; bits27:3==0 -> r0=0x158284
    then frame epilogue / return.

  So: token bit 10 marks probe-only entries; the no-match sentinel is
  planted when the result slot was never filled.  The execute/accept
  region (0x20097200+, success stores 0x20097e30+) is reached only via
  entries whose bit 10 is CLEAR ON THE PATH THAT MATCHES -- the open
  question is what selects those entries: the MODE argument (r7) at the
  top-level call (ctx 0x2000e5d0, JRN-VMB-021 Sec 3.3) is the lever.
  NEXT STATIC STEP: disassemble the CALLER at 0x2000e5xx to find where r7
  (mode) is loaded from -- if it derives from an env var / HWRPB field /
  descriptor flag, that names the EmulatR-side gap directly.

--------------------------------------------------------------------------------
## 4. Probe plan (sharpened by A4)

  4.1 CONFOUND CONTROL (cheap): boot the SAME dka0.vdisk on EMULATR ES40
      (es40_v7_3 + the SCSI manifest entry ported to es40_v7_3_platform
      .json).  EmulatR-ES40 boots -> the gap is DS20-console-specific
      (env var/format); still-NOIOVEC -> diff EmulatR-ES40 vs AXPBox-ES40
      (same console build!) -- a nearly-controlled experiment.
  4.2 STATIC (running): caller disasm for the r7 mode origin (Sec 3).
  4.3 ENV DIFF: capture AXPBox's full `show *` env at its >>> and diff
      against EmulatR's DS20/ES40 env; prime suspects boot_osflags,
      bootdef_dev, boot_dev, booted_dev formats.

--------------------------------------------------------------------------------
## 5. AXPBox harness notes (operational, keep)

  - WORKING binary: axpbox/test/rom/axpbox.exe (2026-05-20 build).  The
    axpbox_ptemp_build Release AND the stock 1.1.2 x64-Debug builds BOTH
    mis-execute the guest on this host (decompressor never converges /
    Unknown-opcode spam) -- MSVC-2022-vs-2008-codebase suspicion; do NOT
    use them as oracles.  cl67srmrom.exe is GOOD (byte-identical to the
    architect's fresh download; prologue matches ES40_V6_2.EXE and
    es40_v7_3.exe after its 0x240 LFU header).
  - test/rom/decompressed.rom is the VALID cl67 decompressed image (do not
    confuse with EmulatR-firmware experiments); pairing it with the May-20
    binary skips decompression (~3 min to P00>>>).
  - AXPBox serial: telnet (NUL padding + IAC negotiation) -> use
    srm_telnet_driver.py; raw srm_a1_driver.py stalls on pattern-match.
    BOTH serial ports must be connected; a console client disconnect
    KILLS the emulator process.
  - dka0.vdisk stayed READ-ONLY throughout (media protected).

--------------------------------------------------------------------------------
## 6. ADDENDUM (same day): probe 4.1 EXECUTED -- BLOCKED by pre-existing #32;
##    the blocking gap itself narrows the hunt

  Setup: es40_v7_3_platform.json gained pka_53c810 at slot 3 (AXPBox-
  mirroring; pin 2 retained from proven DS20 wiring; INTx unrouted on ES40
  -- warning logged, driver polls) with dka0.vdisk at SCSI id 0; the IDE
  dqa0 media EMPTIED (no dual attach); ini model=ES40 for the run (RESTORED
  to DS20 after).  Cold boot -> P00>>>.

  RESULT: `show config` DOES list "3  NCR 53C810  pka0.0.0.3.0" (config
  probe fine), but `show dev` lists ONLY dva0 and `b dka0` -> "device dka0
  is invalid".  EMULATR_SCSI_TRACE shows ZERO CSR activity: the console
  never ran its driver-init/bus-scan phase.  Phase diff vs AXPBox-ES40
  (same console build, V7.3-1):

    AXPBox ES40:  entering idle loop -> serial-number note ->
                  "Partition 0, Memory base ..." ->
                  "initializing GCT/FRU at 1c8000" ->
                  "Initializing pka dqa dqb"        <- drivers scan buses
    EmulatR ES40: entering idle loop -> (STOPS; none of the above)
    EmulatR DS20: serial-number note ->
                  "initializing GCT/FRU at 3ff32000" (NO "Partition" line)
                  -> enumeration DOES work (dka0..dka600 in show dev)

  This is the KNOWN #32 ES40 gap (run_es40_showdev.sh header: ES40 lists
  only dva0; dqa absent), NOT a SCSI regression.  The clean ES40-vs-ES40
  comparison is BLOCKED until #32 is root-caused.  Note also pka0.0.0.3.0
  (host id 0) vs pka0.7.x.x.x elsewhere -- with no driver init, the SCID
  was never programmed; cosmetic consequence, not a cause.

  NARROWING VALUE: the phase EmulatR-ES40 skips is partition/GCT/driver
  init.  VMB-019 exonerated GCT via "zero GCT reads in the FAILING WINDOW"
  -- that does NOT cover an EARLY GCT/config-tree read caching a flag that
  later selects the resolver's probe-only mode.  The DS20 console builds
  its GCT at 0x3ff32000 but prints no "Partition" line (ES40-vs-DS20
  console difference, or a real omission -- unresolved).  GCT CONTENT
  (EmulatR DS20 vs AXPBox ES40) is back on the suspect list for the mode
  decision.

  NEXT (reordered):
    N1. DYNAMIC mode provenance on DS20 (primary oracle, existing
        instruments): EMULATR_DIAG_WREG=18 (+CYCLO/CYCHI around the
        resolver cluster) to catch who computes the mode argument, then
        backtrace its data source (env var cell? GCT-derived flag?
        descriptor field?).
    N2. EMULATR_PA_WATCH on the DS20 GCT region (0x3ff32000+) across the
        WHOLE APB run -- did APB read the config tree early?
    N3. #32 root cause (ES40 partition/GCT/driver-init skip) -- unblocks
        the controlled comparison AND is owed housekeeping anyway.

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle (AXPBox = secondary,
supportive; this A4 use -- corroborating a gap EmulatR exposes -- is the
sanctioned pattern).
