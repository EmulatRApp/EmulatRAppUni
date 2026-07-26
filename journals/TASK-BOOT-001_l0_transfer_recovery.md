<!--
EmulatR V5 -- Work Order TASK-BOOT-001
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# TASK-BOOT-001 -- Recover the console->APB handoff (L0), then reopen the
#                  NOIOVEC investigation (L1).  Time-boxed; decision gate to
#                  an AXPBox-informed clean-room rebuild if the box is blown.

    Doc id   : TASK-BOOT-001
    Date     : 2026-07-26
    Status   : WORK ORDER (agreed Peer/Claude 2026-07-26).
    Reading  : JRN-SCSI-009 (layered causal model -- READ FIRST), -008, -007,
               -006, -005, -004; JRN-VMB-016 (PcTrace origin), -021, -022.
    Time-box : 2 working days AFTER the L0 cause is named (Phase 1 names it).
               Blown box -> Phase 5 decision gate, not more digging.

--------------------------------------------------------------------------------
## 0. Problem statement (one paragraph)

  Secondary boot (SYSBOOT) has never been reached.  Two independent closed
  gates: L0 -- since the evening builds of 2026-07-25, the console->APB
  transfer fails on a DS20 cold boot ("halted CPU 0 / halt code = 0 /
  PC = 20000000" immediately after "jumping to bootstrap code"); L1 -- before
  that, APB always died in its device resolver (%APB-F-NOIOVEC).  The same
  dka0.vdisk boots OpenVMS V8.3 on AXPBox ES40, so media and APB image are
  exonerated.  L0 is the current blocker and this task's target.

## 1. What is already PROVEN -- do not re-derive (JRN-SCSI-009 Sec 1)

  In the FAILING post-boot snapshot (snapshots/hold/predig_oemsnap_
  cyc1041483773.axpsnap; pre-boot baseline cyc836003331; second failing
  boot cyc1627262918 is byte-identical in the image region = deterministic):
    - APB image correct at PA 0x5bc000 (entry d3800000 / 201f0001 / ...)
    - page tables correct: VA 0x20000000 -> PA 0x5bc000 [V,KRE,KWE];
      Region 0/2/3 per AARM 27.4.1.3; PTBR (HWPCB) = PFN 0x1ff82
    - per-CPU slot: BIP=1 RC=0, HALT PC=0x20000000, PS=0x1f00 (IPL31 kernel),
      halt R25/R26/R27 = 0, REASON = 0 (= un-architected console re-entry;
      an executed CALL_PAL HALT would read 5)
    - ZERO APB-executed side effects: pre->post diff = exactly the 77
      image-load pages (0x5bc000..0x654000); stack pages byte-identical junk
    - fault logs indistinguishable from the working era (double-miss traffic
      identical in kind; the one kFaultUnimplemented byte-identical at
      cyc 182468822 in both) -- fault path exonerated
  Exonerated as causes: build config (Release AND RelWithDebInfo fail
  identically), chipset header edits (present in the last-good 07:45 binary),
  media/APB (AXPBox), env vars (booted_dev/booted_osflags verified written,
  JRN-SCSI-007/006 gates pass).
  NOT yet eliminated: the JRN-SCSI-008 manifest edit (A/B staged, Sec 3.3),
  and any other working-tree change between the 07:45 and evening builds.

## 2. Conventions and hard constraints

  - ASCII(128) only in all files; hex radix; journal style per existing docs.
    Next journal id: JRN-SCSI-010.  New tools -> tools/ (durable, headered).
  - Logs -> ./logs, traces -> ./traces of the RUN dir, timestamped
    purpose_YYYYMMDD_HHMMSS.ext.  No loose artifacts in the run-dir root.
  - EVERY diagnostic launch: EMULATR_NO_AUTOLOAD=1 (autoloadLatest resuming a
    stale snapshot contaminated two sessions on 07-25; snapshots/hold/ is the
    quarantine -- do not move files back to snapshots/).
  - Use `export` in Git Bash for EMULATR_* vars (prefix-env has failed
    silently before; the TICKWARP banner documents it).
  - axpbox/ + axpbox-1.1.2/ sources are GPL: READ-ONLY REFERENCE.  No code
    may be copied into EmulatR (proprietary license).  Behavioral specs only.
  - Do not edit ds20_v7_3_platform.json (source or run-dir copies) until
    Phase 1 data exists; the PREEDIT A/B depends on the current pair.
  - The V4 tree (EmulatRAppUniV4) is frozen context; work in emulatrappuniv5.

## 3. Phase 1 -- name the L0 cause (half a day)

  3.1 THE RUN (operator does the console typing; script everything else):
        export EMULATR_NO_AUTOLOAD=1
        export EMULATR_PCTRACE=1 EMULATR_PCTRACE_N=8192
        export EMULATR_DIAG_PCLO=0x20000000 EMULATR_DIAG_PCHI=0x20099400
        export EMULATR_DIAG_CAP=2000
        ./out/build/relwithdebinfo/Emulatr.exe \
            2> logs/pctrace_coldboot_$(date +%Y%m%d_%H%M%S).log
      Require "autoload suppressed" in the log head.  Cold boot to P00>>>,
      `b dka0.0.0.8.0 -flags 0`, halt, exit.
      PCTRACE (coreLib/PcTrace.h, JRN-VMB-016) arms at the CSERVE-START
      handoff (PalEntries case 0x42), records retires, latches the first PC
      below 0x200000 (the bail back into console) and dumps trajectory +
      arm-time PTBR check vs PFN 0x1ff82.

  3.2 OUTCOME BRANCHES:
      (a) DIAG-PC lines present / PCTRACE dumps a trajectory
          -> code RAN; the bail entry names the instruction.  Decode with
             tools/snap_va_disasm.py (VA-faithful; delta VA-0x20000000+
             0x5bc000) and root-cause that instruction's emulation.
      (b) PCTRACE arms but records nothing / zero DIAG-PC lines
          -> the transfer never fetched at VA 0x20000000.  The bug is in the
             console-exit glue: PalEntries case 0x42 (CSERVE START),
             HW_REI transition handling (the "[HW_REI XITION]" path), PAL->
             native mode switch, or ITB behavior at the first mapped fetch.
             Instrument/inspect THAT seam next, not the guest.
      (c) PCTRACE never arms
          -> CSERVE START itself never issued: the console took a different
             exit path than the facility expects.  Compare against the
             JRN-VMB-016 era; check whether the arm hook still matches the
             current PalEntries dispatch.
      Also run the 30-second manifest A/B once (Sec 1 NOT-eliminated):
        EMULATR_PLATFORM_CONFIG=out/build/relwithdebinfo/
        ds20_v7_3_platform.PREEDIT.json  (2 IDE rows) vs current (1 row).
      If PREEDIT boots to NOIOVEC, the manifest edit is the trigger -- find
      out WHY (a media-less storage row should be inert) before reverting.

  3.3 Write JRN-SCSI-010 with the branch taken and the named cause.

## 4. Phase 2 -- fix L0 (the 2-day box starts when 3.3 names the cause)

  - Fix in place; smallest change that restores the transfer.  The failure
    window is hours wide (07:45 binary ran APB; evening builds do not), so
    expect a small recent cause, not architecture.
  - ACCEPTANCE: cold DS20 boot of dka0.0.0.8.0 -flags 0 reaches
    %APB-F-NOIOVEC (yes -- NOIOVEC is the SUCCESS criterion for this task;
    it proves APB executes to its resolver again = L0 open).
  - Re-baseline: one DIAG-PC run with the OLD window (PCLO=0x20095840
    PCHI=0x20099000, CAP=3000000) and confirm the resolver footprint matches
    JRN-SCSI-004 Sec 4 (752 unique PCs, 0xf3-tail exit).  Corpus continuity
    matters: the old reference snapshot was deleted; tonight's hold/ pair +
    this re-baseline replace it.

## 5. Phase 3 -- decision gate (only if the box blows)

  If L0 is not fixed within 2 working days of being named:
  - STOP patching.  Write an AXPBox-informed CLEAN-ROOM spec for the failing
    subsystem only (behavior, not code; GPL constraint per Sec 2): what
    AXPBox does at the same seam, as observable behavior + AARM citations.
  - Scope estimate + risk note, then hand back for a go/no-go.
  Deliverable: SPEC_<subsystem>_axpbox_informed.md + a one-page summary.

## 6. Phase 4 -- reopen L1 (after acceptance; separate effort)

  The CRB-window run the NOIOVEC track never had:
    export EMULATR_DIAG_PCLO=0x101aa000 EMULATR_DIAG_PCHI=0x101ac000
    export EMULATR_DIAG_CAP=20000
  Every console callback passes the CRB dispatch entry VA 0x101aac60 with
  the routine code in R16 (0x22 = get_env; apisrm cb_table).  Capture the
  full callback conversation of the failing boot; then obtain the same from
  AXPBox (its tracing or an instrumented run) and diff.  First divergent
  answer names the L1 gap.  Verify CRB per-run with tools/dump_crb.py (the
  block is rebuilt each boot).  File as JRN-SCSI-011.

## 7. Queued small items (do opportunistically, never before Phase 1)

  - snapshotWatch recall-proofing (Uart16550.h): strip CSI escapes or watch
    the TX echo with an ends-with match, so up-arrow recall fires the marker.
  - autoloadLatest default: propose opt-in (flag/ini) -- silent resume has
    contaminated diagnostic sessions twice.
  - Promote scratchpad drivers into tools/: srm_a1_driver.py,
    srm_telnet_driver.py, diagpc_footprint.py, p3_snapshot_strings.py.
  - srm_conformance: add SIZE column per JRN-SCSI-007; re-rate env deltas
    against the FIRMWARE path only (subsystem split, JRN-SCSI-007 Sec 1).
  - INQUIRY identity (JRN-SCSI-008 Sec 4): schema + VirtualDiskDevice
    vendor/product/revision fields; test dka0 as DEC RZ-series AFTER L1
    baseline runs exist.  Then dqb (Sec 5.1); dkb deferred.
  - CC_FREQ = 100 MHz in HWRPB vs real ~500/667 (banner MHz garbage):
    audit before any L2/kernel work; VMS calibrates from it.

## 8. Tool + address quick reference (all tools in tools/, self-testing)

    snap_va_disasm.py   VA-faithful image disasm from a snapshot
                        (--find-bsr <va> scans call sites)
    snap_ptwalk.py      3-level page-table walk (default AARM region anchors)
    find_bootstrap_image.py  image-at-base check + signature sweep
    dump_crb.py         HWRPB/CRB dump -> callback entry + DIAG window line
    env_size_audit.py   size-aware EV audit of console transcripts
    alpha_disasm.py     decoder used by all of the above

    APB image        VA 0x20000000..0x20099400 = PA 0x5bc000.. (delta
                     PA = VA - 0x20000000 + 0x5bc000)
    resolver entry   VA 0x20095840; callers 0x200016a4 / 0x2000e328
                     (AI=3 -> mode r19 defaults 1; JRN-SCSI-006)
    0xf3-tail gate   VA 0x20096d14..0x20096e44
    HWRPB / CRB      PA 0x2000 / +0x7e0; callback dispatch VA 0x101aac60
                     (console VA = PA + 0xfffe000)
    page table       PA 0x3ff04000 (PTBR PFN 0x1ff82)
    per-CPU slot     PA 0x2180 (+0x80 flags, +0xF0 halt PC, +0x118 reason)
    snapshots        snapshots/hold/ (pre 836M / post-0 1041M / post-01 1627M)
