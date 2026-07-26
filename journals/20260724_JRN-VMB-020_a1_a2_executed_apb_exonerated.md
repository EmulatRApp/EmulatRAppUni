<!--
EmulatR V5 -- Session Journal JRN-VMB-020
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-020 -- NOIOVEC part 4: probes A1 + A2 EXECUTED, both CLEAN.
#                APB's database is intact and EmulatR's execution of the
#                failing window is instruction-for-instruction faithful.
#                The search exhausts legitimately.  Frontier -> A3/A4.

    Doc id   : JRN-VMB-020
    Date     : 2026-07-24
    Status   : PROBES COMPLETE.  No code changed (runtime env + offline
               analysis only).  Two NEW reusable instruments built (Sec 4).
    Relates  : JRN-VMB-019 (executes its Sec 2 A1 and A2), JRN-VMB-018 (+P2),
               JRN-VMB-017.
    Method   : (1) scripted RAW-TCP console client (no PuTTY) drives the full
               boot dialogue unattended; (2) auto_halt Level-1 snapshot at
               the NOIOVEC halt supplies the entire guest memory for the A1
               diff; (3) an independent AARM-semantics replay oracle audits
               every retire in the trace of record for A2.

--------------------------------------------------------------------------------
## 0. Executive summary

 1. A1 (state-vs-image diff): IDENTICAL.  Diffed the ENTIRE 0x99400-byte APB
    image in guest memory (PA 0x5bc000, captured at the halt) against
    APB.EXE;1 file bytes: 30 divergent ranges, 499 bytes TOTAL, every one a
    legitimate runtime-written data cell (parsed descriptor, parse context,
    driver-index, heap pointers).  The A1 decision window 0x2006a0c0-0x2006a200
    (state/jump tables), the 0xf8 token stream 0x20099218-0x20099330 (incl.
    the [0x2009921e] chain link), the [0x200652e8] literal, and the message
    table are ALL byte-identical to the image.  NO corruption, NO mis-seeding.

 2. A2 (CPU-execution audit): CLEAN, and executed MECHANICALLY over the whole
    window rather than by 20-30 instruction spot checks.  An independent
    Python AARM oracle replayed the trace of record (20260724-185950_srm.trc):
    register file reconstructed from the trace's own =>R commits, each of the
    11,620 APB-range retires re-decoded from the VERIFIED image bytes and its
    result recomputed.  Score: 6,086 register results, 835 store values,
    2,873 effective addresses, 1,100 conditional-branch directions, and
    byte-accurate load-vs-store memory consistency -- ZERO divergences.
    The suspect leaves specifically: LDQ_U x1362, EXTBL x207, EXTLL/EXTLH
    x142/x142, CMPULE x238, CMOVEQ x129, ZAPNOT x149, INSBL x75, EXTWL x71,
    SRA/SRL/SLL x287, S4ADDQ (in ADDQ family x373) -- all correct.  The
    EXTxH/INSxH/MSKxH shift-count-64 edge (Rbv<2:0>=0; AARM says result 0 /
    mask identity, a naive C++ `<<(64-0)` gives mod-64) occurred 313 times:
    every one matched the AARM value.  Also ZERO memory inconsistencies:
    every quad loaded in the window equals the bytes stored by prior traced
    stores.

 3. CONSEQUENCE: JRN-VMB-019's remaining Track-A suspects are now A3
    (descriptor provenance -- the query record built BEFORE the failing
    window) and A4 (AXPBox oracle diff).  The static grammar walk, fed the
    canonical string, correctly finds no match: either the query descriptor
    is mis-built earlier in APB's flow (from console data APB consumed
    before this point), or this APB genuinely requires a console-side
    element (e.g. a runtime handler registration or config-tree node) that
    EmulatR's console does not yet provide -- which only the A4 known-good
    execution can reveal cheaply.

 4. RECORD CORRECTION: after %APB-F-NOIOVEC, APB executes HALT @0x20003a38
    and EmulatR STOPS (Stop reason: HaltedClean, process exit) -- it does
    NOT return to the SRM console.  JRN-VMB-017/memory.md's "clean return to
    console" was wrong (the 18:59:50 PuTTY log also ends at NOIOVEC with no
    subsequent prompt).  Post-halt SRM examine is therefore unavailable;
    the auto_halt snapshot is the right instrument (and better: full memory).

--------------------------------------------------------------------------------
## 1. A1 detail (snapshot diff)

  Capture : boot run with the faithful stack (2D_NOOP/DELAYWARP/CSERVE guest
            route/DIVERT_PALSWAP) + autosnapshot ON; at the NOIOVEC halt the
            save-on-halt path (Machine.cpp:1258) wrote
            snapshots/auto_halt_1784955322_1842256525.axpsnap (4.3 GB, flat
            guest memory at payload0=0x3df4).  KEEP this file: it is the
            complete failure-state memory image for any further A3 spelunking.
  Base    : APB load base PA = 0x5bc000 (parsed live from the console's
            "base = 5bc000, image_start = 0, image_bytes = 99400" line);
            PA = base + (VA - 0x20000000); file_offset = VA - 0x1FFFFE00.
  Verdicts (windows of interest):
      A_state_tables   0x2006a0c0-0x2006a200 : IDENTICAL
      C_token_stream   0x20099218-0x20099330 : IDENTICAL
      D_literal        0x200652e8            : IDENTICAL
      message_table    0x2006bf00-0x2006c320 : IDENTICAL
      parsed_descriptor 0x2006a308-0x2006a4a0: DIVERGENT (runtime, expected)
      parse_ctx        0x2006aa5c-0x2006aacb : DIVERGENT (runtime, expected)
  Runtime cells worth recording (A3 fodder; all values at halt):
      0x2006a308  "IDE     "        (protocol name, space-padded)
      0x2006a394  0x04
      0x2006a3f4-0x2006a408  -1 sentinels
      0x2006a430  -1 ; 0x2006a438 = 0x11   (driver-identify stores, JRN-018 f.4)
      0x2006a8b0  0x32c ; +8 = 1     (image had -1 statically)
      0x2006a8d0  ptr pair 0x20063820 (driver block) / 0x2006a8d8
      0x2006a930  "IDE     "
      key record  0x2006aa68 = long 0x2, 0x2006aa6c = long 0x13 (ident),
                  0x2006aa70 = long 0x200dfd40 (-> "IDE 0 105 0 0 0 0 0"),
                  0x2006aa74 = long 0x3
      0x2006aab8  "IDE 0 105 0 0 0 0 0" (the complete 19-char string, again
                  confirming JRN-VMB-019 E3)
      0x20071230-0x200712cc  MM-ish cells: 0x40100000, 0x40000000,
                  0xfffff800_00000000, 0xd, heap page ptrs 0x200a0000/
                  0x200a2000/0x2009e000/0x200a6000 (past image end -- APB heap)
      0x20072a90  0xcccccccccccccccd (div-by-10 reciprocal; numeric parse)
      0x2008e4e8  ptrs 0x20090458/0x20090568 (matcher-family PDSCs);
      0x2008f690/0x2008f7b0 = 0x2000135c (stored return/callback VA)
  Full report: scratchpad a1_diff_report.txt (session artifact; regenerate
  with a1_snapshot_diff.py against the kept snapshot).

--------------------------------------------------------------------------------
## 2. A2 detail (replay oracle)

  Input   : traces/20260724-185950_srm.trc (261,612 retires, the JRN-019
            trace of record).  Trace grammar: RET ord/cpu/rpcc/pc/mnem/pal/
            exc [=>Rdd=v] [ld|st<sz> va pa v] [sde=n] [H..].
  Model   : commit trace =>R values as ground truth AFTER checking; loads
            take the traced v as input; EV6 SDE honored (pal=1 writes to
            R4-7/R20-23 with sde!=0 go to the shadow bank, NOT the native
            replay file); byte-accurate store history for load checking.
  Checker-model corrections made en route (NOT EmulatR bugs, recorded so the
  next reader does not re-trip):
    - trace v= on stores logs the RAW register, not the size-masked datum;
    - AMASK returning 0 for lit 1 is CORRECT on EV6 (BWX implemented);
    - without the SDE shadow rule the replay register file is polluted by
      DTB-miss PAL handler writes (e.g. native R6 0x2006aa6c vs shadow
      0x5c2101) -- 276 phantom mismatches, all vanished with the rule.
  Result  : 0 value, 0 store, 0 EA, 0 branch-direction, 0 memory
            inconsistencies over the full window.  Instruction coverage top:
            BIS 1790, LDQ 1427, LDQ_U 1362, LDA 849, STQ 608, BNE 483,
            ADDQ 373, XOR 328, BEQ 311, ADDL 246, CMPULE 238, EXTBL 207...
  Checker : scratchpad a2_replay_check.py (session artifact).  Reusable for
            ANY window of ANY retire trace -- this is a general differential
            oracle for EmulatR-vs-AARM integer semantics and worth promoting
            into tools/ if it earns its keep again.

--------------------------------------------------------------------------------
## 3. How the run was driven (unattended; repeatable)

  1. Build: tools/build_emulatr.sh relwithdebinfo (binary 2026-07-24 21:35).
  2. Launch (Git Bash, from out/build/relwithdebinfo):
       EMULATR_2D_NOOP=1 EMULATR_DELAYWARP=1 EMULATR_CSERVE_START_MODE=guest \
       EMULATR_CSERVE_AUDIT=1 EMULATR_CSERVE_ROUTE=1 EMULATR_DIVERT_PALSWAP=1 \
       EMULATR_HALT_DIAG=1 EMULATR_CONSOLE_MIRROR=1 EMULATR_CONSOLE_PORT=10023 \
       EMULATR_NO_PUTTY=1 \
       ./Emulatr.exe --firmware firmware/ds20_v7_3.exe --no-autoload \
         --max-cycles 999000000000
     NOTE --autosnapshot off OMITTED deliberately: save-on-halt is the capture.
     NOTE EMULATR_NO_PUTTY=1 is REQUIRED for scripted driving: Machine.cpp:312
     hardcodes autoLaunchPutty=true (the ini's autoLaunchPutty=false is
     overridden), PuTTY grabs the single-client console and the script's
     connection is rejected.  run_ds20_showdev.sh line ~171 unsets the var,
     so scripted runs must launch the exe directly (as above), not via the
     wrapper.  (Possible cleanup: make Machine.cpp honor the ini value.)
  3. Console dialogue (RAW TCP localhost:10023), observed sequence + timing
     (whole boot-to-halt ~11 min wall):
       "...standard console update:"  -> send <CR>
       "UPD>"                         -> send "exit"   (never "u srm")
       "P00>>>"                       -> "show bootdef_dev" (persisted dqa0)
       "P00>>>"                       -> "b dqa0"
       "base = 5bc000, ..."           -> capture load base
       "%APB-F-NOIOVEC"               -> emulator halts + exits; snapshot lands
     Driver: scratchpad srm_a1_driver.py (pattern-waits, never sleeps blind).

--------------------------------------------------------------------------------
## 4. Instruments built (reusable)

  - srm_a1_driver.py  : unattended SRM console walker (RAW TCP, pattern-wait,
                        transcript).  Generalizes to any scripted boot probe.
  - a2_replay_check.py: AARM integer-semantics differential oracle over any
                        EmulatR retire trace (SDE-aware, byte-accurate mem).
  - a1_snapshot_diff.py: whole-image guest-memory-vs-file differ keyed off
                        the auto_halt snapshot flat payload.
  All in the session scratchpad; copy into tools/ if wanted durable.

--------------------------------------------------------------------------------
## 5. Next steps (updated Track A)

  A3. Descriptor provenance (NOW THE FRONT): window the "determine boot
      device type" phase (the descriptor build that writes 0x2006a308/
      0x2006a430 and the key record 0x2006aa5x) with EMULATR_DIAG_PCLO/PCHI
      and verify WHAT populates the fields the 0xf8 walk consumes -- then
      decode, from the static side (JRN-018 P2 stream maps), what a MATCHING
      registration would need to look like.  The kept snapshot supplies every
      data value; the missing piece is which code path SHOULD have registered
      or matched the request that instead exhausts.
  A4. AXPBox oracle (cheap decisive split): boot the SAME media on AXPBox
      ES40, window module entry 0x95840 (image offsets transfer), and diff
      the 0xf8 walk against ours.  If AXPBox's walk SUCCEEDS, the divergent
      input field names the console-side gap directly.  (EmulatR PRIMARY;
      AXPBox corroboration only.)
  A5. (unchanged) EIHD symbol decode nice-to-have.
  Track B (PCI #41, B1 BAR rebind + B2 IDSEL boundary) proceeds in parallel;
  required for SYSBOOT regardless of the NOIOVEC verdict.

  Housekeeping still owed: CSERVE entry-ledger throttle; DS10/ES40
  regression pass to >>>; DIVERT_PALSWAP promotion decision; the
  Machine.cpp:312 autoLaunchPutty ini-honor cleanup (new, this session).

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
