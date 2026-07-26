<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-004
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-004 -- P1/P2 live gate PASSED; P3 NOIOVEC retest: SCSI boots to
#                 the SAME failure with a BYTE-IDENTICAL resolver footprint --
#                 the "APB predates IDE boot" hypothesis is REFUTED as the
#                 root cause; the 0xf3-tail gate is PROTOCOL-INDEPENDENT.

    Doc id   : JRN-SCSI-004
    Date     : 2026-07-25
    Status   : ANALYSIS RECORD + live-gate acceptance.  No code changed.
    Relates  : JRN-SCSI-003 (implementation), JRN-SCSI-001/-002 (design/seams),
               JRN-VMB-019/-020/-021/-022 (NOIOVEC investigation).
    Artifacts: scratchpad p3diag_scsi_diagpc.log (7,179 DIAG-PC records),
               p3_scsi_console_transcript.log, scsi_gate_console_transcript.log,
               snapshots/auto_halt_1785006537_2027331327.axpsnap (SCSI-run
               halt state), diagpc_footprint.py / p3_snapshot_strings.py
               (scratchpad; copy to tools/ if wanted durable).

--------------------------------------------------------------------------------
## 1. Live gate (JRN-SCSI-003 P1/P2 acceptance): PASSED

  Binary: relink of 2026-07-25 (00:45 in-source; 12:04 mirrored via
  tools/build_emulatr.sh relwithdebinfo -- the 00:29 run-dir mirror PREDATED
  the last TsunamiChipset.h edit; rebuilt before running).
  Runbook: VMB-020 Sec 3 scripted console (srm_a1_driver.py, showdev mode),
  EMULATR_NO_PUTTY=1, port 10024, full faithful env stack.

  `show config`: "bus 0, slot 8 -- pka -- NCR 53C810".
  `show dev`   : pka0.7.0.8.0 (SCSI Bus ID 7) + dka0.0.0.8.0 .. dka600.6.0.8.0
                 (7 disks, "EMULATR VIRTUAL DISK") -- the console's own pke
                 driver enumerated the bus through the SCRIPTS engine.
  Manifest at run time attached dka0.vdisk (VMS system disk) at id 0 plus
  dka1..dka6 .img at ids 1-6 (the Q3 media swap was already in the manifest).
  Boot-to-prompt ~3 min wall.  No SCSI/PCI trace anomalies observed.

--------------------------------------------------------------------------------
## 2. P3 `b dka0`: boot READS run clean; NOIOVEC persists

  (boot dka0.0.0.8.0 -flags 0) -> "block 0 ... valid boot block" ->
  "reading 1226 blocks from dka0.0.0.8.0" (all through Ncr53C810 SCRIPTS +
  the S1 DMA seam, ~1 s wall) -> base = 5bc000 -> APB runs ->
  %APB-F-NOIOVEC, Failed to create IOVEC -> HALT @0x20003a38, HaltedClean,
  cyc 2,027,331,327.  Same terminal signature as the dqa0/IDE boots.

--------------------------------------------------------------------------------
## 3. Post-mortem (auto_halt_1785006537 snapshot, p3_snapshot_strings.py)

  1. GETENV DELIVERED THE SCSI STRING: "SCSI 0 8 0 0 0 0 0" present at the
     parse-ctx window 0x2006aab8 (IDE run of record: "IDE 0 105 0 0 0 0 0"
     at the same address).  Console side is CORRECT and canonical.
  2. Descriptor window 0x2006a308: ident field = "SCSI    " (8-char pad),
     same shape as IDE run's "IDE     ".  The 0x11 dword at 0x2006a438 is
     present in BOTH runs (NOT an accept marker -- likely static/type field
     independent of walk outcome).
  3. ONE dword differs between the runs in the descriptor window:
     [0x2006a394] = 4 (IDE run) vs 0 (SCSI run).  Meaning unknown; the
     resolver-window code did NOT write it (footprints identical, Sec 4)
     so it is written elsewhere or input-derived.  OPEN OBSERVATION.
  4. Stack window 0x200dfxxx is NOT 1-1 mapped in the snapshot flat view
     (reads as noise via base+offset) -- stack post-mortems need the page
     table at 0x3ff04000, not the image-region identity map.

--------------------------------------------------------------------------------
## 4. DIAG-PC footprint: SCSI vs IDE = IDENTICAL (the decisive result)

  Method: EMULATR_DIAG_PCLO=0x20095840 PCHI=0x20099000 CAP=3000000 (same as
  the VMB-022 scan), cold boot -> LFU exit -> b dka0 -> halt.  Compared with
  logs/run_ds20_a3scan_20260724_222859.log via diagpc_footprint.py.

                              IDE (VMB-022)   SCSI (this run)
    records                       6,931           7,179 (+248 replays)
    clusters (>1M cyc)                1               1
    unique PCs                      752             752   PC-set diff: EMPTY
    module entry 0x20095840           7               7
    op dispatch 0x20095e30-e60      235             235
    0xf3 tail 0x20096d14-e44        248             248
    BNE exit @0x20096e04              4               4
    exit land @0x20096e44             4               4
    CMOVEQ birth @0x20096e58          4               4
    ACCEPT 0x20097200-2fff            0               0
    success stores 0x20097e30+        0               0
    recursion bsr 0x200964fc          3               3
    scanner 0x20096f80               31              31
    final retire                0x20096eb0      0x20096eb0

  The walk over "SCSI 0 8 0 0 0 0 0" retires the EXACT same instruction set,
  in the same structure, exiting at the same 0xf3-tail BNE, as the walk over
  "IDE 0 105 0 0 0 0 0".  The protocol keyword changes NOTHING.

--------------------------------------------------------------------------------
## 5. Consequences

  1. REFUTED as root cause: "this APB (A13-03) predates IDE boot; NOIOVEC is
     its correct answer to an IDE topology string" (VMB-022 leading
     hypothesis).  SCSI IS in the image's protocol set, the console delivered
     a canonical SCSI string, and the resolver STILL never reaches the accept
     region.  The keyword table is not even consulted differently.
  2. CONFIRMED: VMB-021 finding 4 is the real bottleneck -- the 0xf3-class
     entry gate (0x20096d14-0x20096e44) chooses "inline probe then EXIT"
     instead of "call the PDSC handler", driven by token flag bits (10/11/
     12/14), the MODE argument (R7=1), and/or sub-request r25=4 -- all
     PROTOCOL-INDEPENDENT.  The walk behaves like a validate/probe pass that
     never executes; either APB expects a different mode for the execute
     pass, or something upstream that should trigger the execute pass is
     missing/failing silently.
  3. The SCSI HBA is EXONERATED for P3 purposes: reads, DMA, interrupts, and
     the console pke driver all behaved; APB itself loaded off the SCSI disk.
     SCSI-stack work is DONE for this frontier (P4 debts remain).

--------------------------------------------------------------------------------
## 6. Next (sharpened)

  1. A4 (NOW THE TOP PROBE, cheap + decisive): boot this SAME dka0.vdisk on
     AXPBox (Sym53C810).  AXPBox also-NOIOVEC -> the media/APB combination
     is broken or needs newer media -> V8.2 path.  AXPBox boots -> EmulatR
     has an environment gap (env var set, HWRPB field, config tree) that
     flips the resolver's mode -- diff the AXPBox boot environment.
  2. STATIC (A3 part 3, VMB-021 Sec 3.1): disassemble 0x20096d14-0x20096e44,
     name the exact branch chain and which token bit / mode value / key
     field selects the PDSC-call continuation; then read one number-PDSC.
  3. V8.2 media (user is separately testing `b dqa1`): enumerate the V8.2
     ISO's APB keyword set; a newer APB may drive the resolver in execute
     mode or bypass it.
  4. Housekeeping: DS10/ES40 regression to `>>>`; P4 debts (D1 MA/residue,
     D2 disconnect/reselect, G-G snapshot participation, test drift, C4834).

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
