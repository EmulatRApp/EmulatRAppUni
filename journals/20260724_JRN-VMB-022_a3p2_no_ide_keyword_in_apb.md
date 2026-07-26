<!--
EmulatR V5 -- Session Journal JRN-VMB-022
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-022 -- NOIOVEC part 6 (A3 part 2): whole-boot scan proves the
#                resolver NEVER succeeds anywhere in the run, and the APB
#                image contains NO "IDE" protocol keyword AT ALL.  Leading
#                hypothesis: this APB vintage cannot IOVEC an IDE boot
#                device -- NOIOVEC is its CORRECT answer.  A4 (AXPBox,
#                same media) is the decisive test.

    Doc id   : JRN-VMB-022
    Date     : 2026-07-24
    Status   : ANALYSIS RECORD.  No code changed.  One instrumented run
               (DIAG-PC module-window scan) + static APB.EXE analysis.
    Relates  : JRN-VMB-021 (walk transcript), -020 (A1/A2 clean), -019/-018.
    Artifacts: logs/run_ds20_a3scan_20260724_222859.log (DIAG-PC, 6,931
               module-range retire records across the whole boot).

--------------------------------------------------------------------------------
## 0. Executive summary

 1. WHOLE-BOOT EMPIRICAL SCAN (EMULATR_DIAG_PCLO=0x20095840 PCHI=0x20099000
    CAP=3000000, cold boot -> LFU exit -> b dqa0 -> NOIOVEC halt): the
    resolver module executes in EXACTLY ONE cluster the entire run
    (cyc 1,840,936,874-1,840,952,773; 6,931 retires; 7 entry-PC retires =
    2 top-level + 2 recursive invocations + ITB-miss replays).
    The accept region 0x20097200-0x20097fff: ZERO retires, EVER.
    The success stores 0x20097e30/40/60/90: NEVER execute.
    4x CMOVEQ status births -- same as the trace of record.
    => There is NO successful invocation to diff against; the module's
    ONLY consumer in this boot is the failing IOVEC construction.

 2. THE COMPLETE PROTOCOL-KEYWORD SET OF THIS APB IS:
        "DVA ", "RAID", "SCSI", "MSCP", "FLOP"  -> type 0x11
        "MOP ", "BOOT"                          -> (other types)
    Enumerated by decoding EVERY LDAH+LDA-built 32-bit constant in the
    driver-identify region 0x2000e9d0-0x2000fa00 (all ~9 identify bodies).
    Additionally: the RAW string "IDE" occurs NOWHERE in the 628,224-byte
    image (0 hits), and no LDAH/LDA immediate pair anywhere in the code
    region can form "IDE"/"IDE "/"IDE\0" (imm sweep: 0x2045 -> 0 hits;
    0x0045 hits are arithmetic; 0x4449 hits are the high half of "RAID").
    A protocol comparison REQUIRES the constant; it does not exist.
    => This APB image cannot recognize "IDE" as a boot protocol.

 3. Identify mechanics pinned (0x2000e9d0, driver 1): a0 = the parsed
    descriptor BASE 0x2006a2b0; it reads the PROTOCOL field [a0+0x58]
    (= 0x2006a308, ZAPNOT #0xf -> 4 chars) and CMPEQs it against the
    battery; on match STQ type -> [a2] (0x2000ec44).  The descriptor's
    numeric fields at +0x144.. are -1-filled by the anchor-PDSC code at
    0x2000dbe0 (observed live).  The type 0x11 seen at 0x2006a438 in the
    halt snapshot was stored PRE-window (identify's battery PCs have zero
    retires inside the trace window; JRN-018's live observation was the
    "2-char type mnemonic" SECOND half of identify, driven by driver-data
    mnemonics, not embedded strings -- "dq"/"DQ" immediates also have 0
    hits in code).

 4. Caller contract decoded (call site 0x2000e95c-0x2000e990): the
    resolver is called with r25=4 and an OUTPUT cell at [fp+8]; on
    success the caller loads [fp+8] -> PDSC and JMPs [pdsc+8] (r25=3).
    0x158284 = "nothing resolved".  With no IDE production anywhere,
    exhaustion is the correct result for input "IDE 0 105 0 0 0 0 0".

 5. LEADING HYPOTHESIS (Occam, all evidence consistent): this APB
    (ident A13-03; driver set includes TURBOchannel-era names PMAF-*/
    PMAD-AA/CORE-IO in the method blocks at 0x63960-0x639f0) PREDATES
    IDE boot support.  The console (apisrm fd_table, faithfully
    reproduced by EmulatR) hands it "IDE 0 105 0 0 0 0 0"; APB answers
    %APB-F-NOIOVEC -- CORRECTLY.  JRN-VMB-019's "IDE boot IS supported
    by this vintage" was an ERA inference (EW5700 string), not a binary
    fact; the keyword enumeration refutes it for THIS image.
    CAVEAT kept open: the resolver walk is generic/data-driven; a
    non-keyword acceptance path cannot be 100% excluded until the walk's
    mode gate (VMB-021 Sec 0.4) is fully mapped -- but with zero "IDE"
    bytes image-wide, any accept would still have nothing to match the
    protocol against downstream (driver-name templating needs a driver).

 6. WHAT THIS MEANS FOR EMULATR: A1 clean + A2 clean + (this) => EmulatR
    is exonerated END-TO-END on NOIOVEC: console string canonical, APB
    database intact, execution faithful, and the no-match is APB's
    correct semantic answer.  The BOOT-PROGRESS options are environmental:
      (a) A4 CHECK FIRST (decisive, cheap): boot the SAME dka0.vdisk
          media on AXPBox ES40 via its IDE.  If AXPBox ALSO gets
          %APB-F-NOIOVEC -> hypothesis CONFIRMED, EmulatR fully cleared,
          pick (b)/(c)/(d).  If AXPBox boots -> hypothesis WRONG, the
          walk gate needs the deeper static map (VMB-021 Sec 3.1).
      (b) NEWER VMS MEDIA whose APB knows IDE (the attached
          OpenVMS_v82.iso's installed system, if its APB is newer --
          check ITS APB.EXE keyword set the same way: 5-minute scan).
      (c) SCSI path: model an HBA (this REVERSES JRN-VMB-018/-019's
          "SCSI not a prerequisite" -- that conclusion rested on "IDE
          boot supported"); console would emit "SCSI ..." which this
          APB DOES accept.
      (d) MSCP or MOP network boot (DE500 model, tickets NET-ADAPTER-001).

--------------------------------------------------------------------------------
## 1. New reference addresses (adds to VMB-021 Sec 2)

  parsed descriptor BASE      0x2006a2b0  (identify a0; protocol +0x58 =
                              0x2006a308; -1 numeric block +0x144..)
  identify battery            0x2000e9d0-0x2000ec4c (driver 1); repeats
                              for other drivers through 0x2000fa00
  identify type store         0x2000ec44 (STQ type -> [a2])
  keyword constants           DVA_ 0x20415644, RAID 0x44494152,
                              SCSI 0x49534353, MSCP 0x5043534d,
                              FLOP 0x504f4c46, MOP_ 0x20504f4d,
                              BOOT 0x544f4f42
  driver table                0x200712e0 -> descriptors 0x2006abc0..0x2006b2c8
                              (type codes at +8: 1,2,3,4,6,7,9,0xc,0x15,0x18)
  driver method blocks        0x20063820 (drv1), 0x20063900, 0x20063930,
                              0x20063a00 ...; TC-era names "PMAF-FS/AA/FA/
                              CA/FD/FU", "PMAD-AA", "CORE-IO" 0x63960-0x639f8
  boot root spec              heap VA 0x200a0078: counted "[SYS0.]" (the
                              0x20074ab8 "SYS" template stores build THIS,
                              not a driver name)
  anchor PDSC code            0x2000dba0 (fills descriptor -1 block @0x2000dbe0)

--------------------------------------------------------------------------------
## 2. Next steps

  1. A4 ORACLE RUN (decisive): AXPBox ES40 + the same dka0.vdisk +
     b dqa0.  One boot answers the hypothesis.  (EmulatR remains PRIMARY;
     this is corroboration of an APB-capability question, not of EmulatR.)
  2. Check the V8.2 ISO's APB: extract APB.EXE from the attached
     OpenVMS_v82.iso (or its installed tree on other media) and run the
     same keyword enumeration; if it contains "IDE", installing/booting
     that system is the direct unblock.
  3. If (1) says AXPBox boots: resume VMB-021 Sec 3 (gate static map).
  4. Update tasks_20260612_boot_pci_deploy sequencing: Track B (PCI #41)
     is UNCHANGED (needed for SYSBOOT regardless); the SCSI-HBA question
     re-enters the roadmap if (a)/(b) fail.

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
