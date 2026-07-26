<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-010
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-010 -- TASK-BOOT-001 Phase 1: the L0 cause is NAMED.
#                 EMULATR_CSERVE_START_MODE was unset in the 2026-07-25
#                 evening shells; CSERVE$START (0x42) defaults to OFF and
#                 no-ops the console->APB divert -- the documented
#                 halt-0-at-0x20000000 wall.  No code regression; no
#                 emulator bug; an environment-discipline failure the
#                 tooling already existed to prevent.

    Doc id   : JRN-SCSI-010
    Date     : 2026-07-26
    Status   : PHASE-1 RECORD (TASK-BOOT-001 Sec 3.3) -- CONFIRMED.
               Acceptance MET 2026-07-25 18:02: first launcher run reached
               %APB-F-NOIOVEC (run_ds20_showdev_20260725_180201.log).
               L0 is OPEN again; ZERO emulator code changed.  See Sec 4.1.
    Relates  : TASK-BOOT-001 (the work order), JRN-SCSI-009 Sec 1.5,
               JRN-VMB-004 (the original stub-to-no-op wall), JRN-VMB-016,
               JRN-VMB-017 (locator), JRN-VMB-020 (last-good A1/A2 runs).
    New tool : tools/run_taskboot001_phase1.sh (one-paste Phase-1 launcher)
    Evidence : logs/pctrace_bootfail_20260725_172726.log,
               logs/pctrace_coldboot_20260725_17315*.logexport (x2),
               putty_console_p10023_202607251[67]*.log (repo root),
               out/build/relwithdebinfo/logs/run_ds20_a1snap_20260724_215204.log,
               palBoxLib/grains/PalEntries.cpp (case 0x42),
               tools/run_ds20_bplus.sh, tools/run_ds20_showdev.sh,
               deviceLib/Tsunami/Uart16550.h, systemLib/Machine.cpp.

--------------------------------------------------------------------------------
## 0. The named cause (one paragraph)

  CSERVE$START (CALL_PAL CSERVE func 0x42) -- the ONLY seam that performs
  the console->APB transfer (divert to the guest PAL sys__exit_console) --
  is gated by EMULATR_CSERVE_START_MODE and DEFAULTS TO OFF
  (PalEntries.cpp case 0x42 mode selector: "Default off until A is
  verified"; committed 2026-07-22, unchanged since).  Every working run of
  2026-07-24/25 was launched through tools/run_ds20_bplus.sh, which exports
  EMULATR_CSERVE_START_MODE=guest (plus the rest of the boot-path stack).
  The 2026-07-25 evening runs were launched BARE, from a fresh shell, from
  a different CWD (the repo root), without the script and without the
  export -- so case 0x42 executed its no-op branch, the divert never
  happened, PTBR was never switched, VA 0x20000000 was never fetched, and
  the console fell through to its failed-bootstrap handling: "halted CPU 0
  / halt code = 0 / PC = 20000000".  That is the EXACT wall JRN-VMB-004
  documented for a stubbed START ("stubbing this to a no-op stranded the
  entire handoff") before Option A existed.  L0 is not a regression in the
  emulator; the 07:45-vs-evening "failure window" was a SHELL change, not a
  code change.

--------------------------------------------------------------------------------
## 1. Evidence chain (all from logs already on disk; no new runs needed)

  E1  The failing diagnostic run (logs/pctrace_bootfail_20260725_172726.log,
      operator boots `b dka0.0.0.8.0 -flags 0` twice from P00>>>):
        line 961 : CSERVE entry: func=66 (0x42) START  pc=0x1ae398 ...
                   cyc=2088010843
        line 1200: CSERVE entry: func=66 (0x42) START  pc=0x1ae398 ...
                   cyc=2655814448
      and ZERO "CSERVE-START-A"/"CSERVE-START-A2" lines anywhere in the
      log.  The dispatcher REACHED case 0x42 twice; the guest-divert path
      (which unconditionally prints CSERVE-START-A2 before diverting,
      PalEntries.cpp) never executed.  s_startMode == kStartOff.  QED.
      The paired console log (putty_console_p10023_20260725172726.log)
      shows both boots ending "jumping to bootstrap code / halted CPU 0 /
      halt code = 0 / PC = 20000000".

  E2  The last-good runs prove the working configuration: run_ds20_a1snap_
      20260724_215204.log line 17020-17021:
        CSERVE-START-A:  palBase=0x8000 restore_state=0xe3a0
                         exit_console=0x13480 enc=0xd0ffebc7
        CSERVE-START-A2: mirror-axpbox p23(r23)<-0x1ae39c divert->0x13480
      (and line 31: "autoload suppressed (--no-autoload) -- genuine cold
      boot").  Same binary lineage, mode=guest active, APB ran to NOIOVEC.

  E3  The evening shell demonstrably lacked exports:
      - "TICKWARP: armed=0 (EMULATR_TICKWARP NOT set -- use export in
        bash)" in the 17:27 log head;
      - the same run AUTOLOADED snapshots/predig_oemsnap_cyc1627262918
        .axpsnap (line 28) => EMULATR_NO_AUTOLOAD unset => the 17:27
        "cold boot" was actually a RESUME of the second failed boot's
        post-state (the second contamination of 07-25);
      - logs/pctrace_coldboot_20260725_17315{9,10}.logexport: the files
        contain only the Emulatr USAGE text.  The filename fused
        "....log" with the next pasted line's "export" -- the multi-line
        paste collapsed, the exports after the redirect never executed,
        and the binary exited at arg parsing.  Two launch attempts never
        ran at all.

  E4  Launch-context proof: every PuTTY console log up to 07-25 00:48
      lives in out/build/relwithdebinfo/ (run_ds20_showdev.sh pins CWD to
      the build run dir); the 07-25 evening PuTTY logs (164543..172726)
      live at the REPO ROOT, and no run_ds20_showdev_*.log exists for
      them.  The evening runs bypassed the script chain entirely.

  E5  Partial-export corroboration: the evening failing boots DID produce
      marker snapshots (predig_oemsnap_*), and the marker-watch is gated on
      EMULATR_CONSOLE_SNAPSHOT (Uart16550.h consoleSnapshotEnabled(),
      Machine.cpp "Off unless EMULATR_CONSOLE_SNAPSHOT is set").  So the
      evening shell had SOME exports -- the JRN-SCSI-009 1.5(b) diagnostic
      knobs -- but not the run_ds20_bplus.sh BOOT stack (2D_NOOP /
      DELAYWARP / CSERVE_START_MODE / CSERVE_ROUTE / DIVERT_PALSWAP).
      The recipe the operator followed listed the diagnostic vars and
      omitted the boot stack, because the boot stack had always arrived
      silently via the launcher script.

  E6  Why PCTRACE "never armed" (the branch-(c) reading): the pctraceArm
      call sits INSIDE the kStartGuest branch of case 0x42
      (PalEntries.cpp, "EMULATR_PCTRACE: arm the forward retire-trace at
      the exit_console target").  With mode=off the arm hook is
      unreachable BY CONSTRUCTION -- "PCTRACE-DUMP armPc=0x0 captured=0"
      (17:27 log line 1253) is the disabled handler, not a changed console
      exit path.  TASK-BOOT-001 Sec 3.1's recipe, run bare, can ONLY
      produce branch (c).  Recorded here as a recipe trap.

--------------------------------------------------------------------------------
## 2. The mechanism explains every PROVEN fact of TASK-BOOT-001 Sec 1

  - APB image + page tables + HWPCB/PTBR correct in the failing snapshot:
    all CONSOLE work, done before CSERVE START, unaffected by the mode.
  - ZERO APB-executed side effects (diff = exactly the 77 image pages):
    the divert never ran, so no instruction ever executed outside the
    console's own code.
  - Halt PC = 0x20000000, REASON = 0, BIP=1 RC=0: the console wrote
    halt_pc into the per-CPU slot during boot prep; when START returned
    as a no-op the console re-entered its own flow without any
    architected halt -- reason stays at its console-init value 0
    (AARM Table 27-1), exactly JRN-SCSI-009's "un-architected re-entry".
  - Release AND RelWithDebInfo fail identically: environment, not binary.
  - Deterministic byte-identical repeats: same bare env every time.
  - Fault logs indistinguishable from the working era: the console-side
    execution IS the working era's, up to the missing divert.
  - The "07:45 binary ran APB / evening builds do not" window: the 07:45
    runs rode a shell whose session had the bplus stack; the evening
    session did not.  The binaries were never the variable.

--------------------------------------------------------------------------------
## 3. Outcome branch taken (TASK-BOOT-001 Sec 3.2)

  Branch (b)/(c) hybrid, with the seam named: CSERVE START *was* issued by
  the console (E1 kills pure (c)), the transfer never fetched at VA
  0x20000000 (kills (a)), and the console-exit glue "bug" is that the
  PalEntries case 0x42 handler was CONFIGURED OFF.  No instruction-level
  root-causing applies; there is no defective instruction.

  The Sec-1 NOT-eliminated list resolves as:
  - JRN-SCSI-008 manifest edit: EXONERATED BY MECHANISM (a manifest row
    cannot reach the CSERVE dispatch mode selector).  The 30-second
    PREEDIT A/B (both legs through the SAME launcher) is still worth one
    run as a formality once L0 is confirmed open; expect both legs to
    reach NOIOVEC.
  - "any other working-tree change between builds": moot for L0.

--------------------------------------------------------------------------------
## 4. Confirmation run (= TASK-BOOT-001 Sec 4 acceptance; operator, ~15 min)

    ./tools/run_taskboot001_phase1.sh

  (new one-paste launcher: exports NO_AUTOLOAD / PCTRACE+window / CONSOLE_
  SNAPSHOT, then delegates to run_ds20_bplus.sh -> run_ds20_showdev.sh,
  which supply the boot stack, --no-autoload, pinned CWD, and a tee'd
  timestamped run log).  At P00>>>: `b dka0.0.0.8.0 -flags 0`.

  PASS = %APB-F-NOIOVEC (APB executes to its resolver; L0 open).  Expect
  in the run log: "autoload suppressed", the bplus banner with
  EMULATR_CSERVE_START_MODE=guest, CSERVE-START-A/A2 lines, a PCTRACE
  trajectory, and DIAG-PC lines in 0x20000000..0x20099400.

  4.1 RESULT (2026-07-25 18:02, first launcher run) -- PASS.  Log:
      out/build/relwithdebinfo/run_ds20_showdev_20260725_180201.log.
        line 52   : "autoload suppressed (--no-autoload) -- genuine cold boot"
        line 34069: CSERVE-START-A: restore_state=0xe3a0 exit_console=0x13480
                    enc=0xd0ffebc7 cyc=1899266111  (identical to the 07-24
                    working-era values, E2)
        line 34070: PCTRACE-ARM pc=0x13480 N=8192
        line 34074: PCTRACE-DUMP armPc=0x13480 captured=129
        DIAG-PC   : 2000/2000 records (cap); first fetch at VA 0x20000000
                    retires enc=0xd3800000 then 0x201f0001 -- the proven APB
                    entry bytes -- after a single clean ITB-miss/refill pair.
        console   : "jumping to bootstrap code" -> "%APB-F-NOIOVEC, Failed
                    to create IOVEC"; emulator exit = HaltedClean at APB's
                    post-message CALL_PAL HALT (PC 0x20003a38, cyc 1.9159e9;
                    the JRN-VMB-020 halt-exits-process behavior -- the
                    "abort" is expected).
      TASK-BOOT-001 Sec 4 ACCEPTANCE MET; L0 OPEN; Phase 2 required no code.
      The 2-working-day box never starts: the cause was named and fixed by
      configuration in the same session.

  4.2 RE-BASELINE (2026-07-25 18:09, TASK-BOOT-001 Sec 4) -- PASS.  Log:
      out/build/relwithdebinfo/run_ds20_showdev_20260725_180914.log, old
      window PCLO=0x20095840 PCHI=0x20099000 CAP=3000000 via the launcher.
        DIAG-PC records = 7179; UNIQUE PCs = 752 -- EXACT match to the
        JRN-SCSI-004 Sec 4 reference footprint; trace ends in the resolver
        exit epilogue (LDQ restores at 0x20096e94..0x20096ea4, lda sp,
        RET enc=0x6bfa8001) with %APB-F-NOIOVEC on the console.
      Corpus continuity restored: snapshots/hold/ pre/post pair + this run
      replace the deleted reference snapshot.

  Frontier: Phase 4 -- the CRB-window L1 run (JRN-SCSI-009 Sec 2:
  PCLO=0x101aa000 PCHI=0x101ac000 CAP=20000; the callback conversation)
  -> JRN-SCSI-011, then the AXPBox comparative (runbook R4).

--------------------------------------------------------------------------------
## 5. Durable fixes so this class of loss cannot recur (proposals)

  P1  (DISCUSS-FIRST; one-line) Flip the case 0x42 default from kStartOff
      to kStartGuest in PalEntries.cpp.  The guard comment says "Default
      off until A is verified" -- A was verified 2026-07-24 (JRN-VMB-017/
      -020: locator unambiguous, APB executed to NOIOVEC).  The condition
      has been met; the default is stale.  Note the paired knobs: Option A
      also needs EMULATR_DIVERT_PALSWAP semantics -- if the default flips,
      decide whether DIVERT_PALSWAP (and CSERVE_ROUTE) flip with it or
      whether the engine defaults already cover them.  Edit shape: the
      two `return`s in the s_startMode lambda (PalEntries.cpp mode
      selector) return kStartGuest instead of kStartOff.
  P2  (already queued, TASK-BOOT-001 Sec 7) autoloadLatest -> opt-in.
      07-25 produced the SECOND contaminated session; E3 documents it.
  P3  Launch discipline: diagnostic recipes in journals should name the
      LAUNCHER (run_taskboot001_phase1.sh / run_ds20_bplus.sh), never a
      bare Emulatr.exe line, so the boot stack can never be forgotten;
      multi-line `export` pastes are a proven failure mode (E3's
      .logexport).  This journal's Sec 4 recipe follows its own rule.
  P4  Cheap tripwire (optional): when the DS20 console issues CSERVE START
      and s_startMode==kStartOff, print one loud line, e.g.
      "CSERVE-START: MODE OFF -- handoff will strand at halt_pc (export
      EMULATR_CSERVE_START_MODE=guest)".  Would have named this cause at
      16:45 on 07-25.

--------------------------------------------------------------------------------
## 6. Files touched by this phase

  - tools/run_taskboot001_phase1.sh      NEW (launcher, Sec 4)
  - journals/20260726_JRN-SCSI-010_l0_cause_named_cserve_start_mode_unset.md
                                          NEW (this record)
  No emulator code changed.  P1/P4 await approval.
