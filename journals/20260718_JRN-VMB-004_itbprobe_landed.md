<!--
EmulatR V4 -- Session Journal JRN-VMB-004
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
ASCII(128) only.
-->

# Session Journal -- ITBPROBE landed; the DS20 run's precondition failure confirmed and cleared

    Doc id      : JRN-VMB-004
    Status      : ACTIVE -- source landed, rebuild + capture pending on Tim.
    Date        : 2026-07-18
    Relates to  : JRN-VMB-003 (the 0x20000000 map + ITB stale-entry hypothesis;
                  Sec 6 discriminator table, Sec 9 precondition CAVEAT).
    Encoding    : ASCII-128.  Hex radix.

---

## 1. Log audit of the DS20 run (the question: "did we instrument any details")

The completed DS20 run's captures were read (bounded tails only, per the trace
rule).  Result: the ONLY probe output that reached any log is ACVPROBE HOOKA,
fired exactly 40 times (its cap) in the probe_vmb_itbmiss_*.log captures.  That
is the Dstream/DTB miss probe from the memtest-ACV task (the 0xEFEFEFEF fill
dumps + "SRM Console: Stopped" 17:26 are that context, not the VMB handoff).

    ITBPROBE lines in the logs : 0
    ITBPROBE ARMED line        : absent
    Anything captured at 0x20000000 : none

This is exactly the JRN-VMB-003 Sec 9 precondition failure.  Root causes found
in the live tree this session:

  1. The hit-path ITBPROBE was in NO source tree.  The only file containing the
     string "ITBPROBE" was the JRN-VMB-003 journal.  The delivered 705-line
     probe copy had never been dropped in.
  2. The active-hive translator emulatrappuniv5/mmuLib/Ev6Translator.h was
     TRUNCATED (511 lines, body of translateInstruction absent, no include-guard
     #endif) -- it would not compile.
  3. The running exe (out/build/relwithdebinfo/Emulatr.exe, RelWithDebInfo/
     Emulatr.exe) is the stale 2026-07-16 03:03 build -- predates any probe.

No run against that state can emit the 0x20000000 ITB discriminator.

## 2. Fix landed this session

Restored the active-hive translator from the intact 564-line good copy
(emulatrappuniv5_/mmuLib/Ev6Translator.h == EmulatRAppUniV4 copy, byte-identical)
and added the Step 1b hit-path AND miss-path ITBPROBE at the ITB lookup return
in translateInstruction.

    File written : D:\EmulatR\emulatrappuniv5\mmuLib\Ev6Translator.h
    Size         : 31764 bytes / 654 lines, verified on-disk (staged back).
    Encoding     : ASCII-128 (CRLF), braces balanced 53/53, ends at #endif.
    Gate         : EMULATR_BRINGUP_PROBES (CMakeCache already BOOL=ON).
    Key          : EMULATR_ITBPROBE_VA, default 0x20000000, PC<0>-masked; cap 16.
    Emits        : one-shot "ITBPROBE ARMED va= cap=16", then
                   "ITBPROBE HIT n= cyc= pal= va= mode= pte= valid= pfn= foe=
                    res= pa= ZEROPFN=" on the keyed hit, and
                   "ITBPROBE MISS n= cyc= pal= va= mode= asn=" on a keyed miss
                   (a ZERO miss count across the boot is the no-miss proof).
    Docs         : header CHANGE block (FILE 1 / FUNCTION / CHANGE 2026-07-18)
                   + inline comments at each changed line.  Observe-only,
                   zero-cost when the flag is off.  P-1 respected -- no walker.

## 3. Next steps (Tim)

  1. Clear the EMULATR_STOP sentinel in the run dir if honored as a stop flag.
  2. Full rebuild in VS2022 (header change -> all TUs including it recompile);
     the exe MUST be newer than 2026-07-18.  EMULATR_BRINGUP_PROBES stays ON.
  3. Sanity gate: confirm the "ITBPROBE ARMED" line appears before trusting the
     run.  No ARMED line == wrong exe again.
  4. Run DS20 cell A (snap_vmb_capture.sh SRC=dqa0, then SRC=dqa1), redirect
     stderr to logs/probe_vmb_itbmiss_YYYYMMDD_HHMMSS.log.

## 4. Reading the result (JRN-VMB-003 Sec 6 decision table)

    pte=...2DE...1101, pfn=2de, ZEROPFN=0  -> correct fill; halt is elsewhere.
    valid=1, pfn=0 (ZEROPFN=1)             -> wrongly-filled entry; chase the
                                              HW_MTPR ITB_PTE site that installed
                                              PFN 0 for tag 0x20000000.
    valid=0 treated as a HIT               -> H-A: stale entry survived the boot
                                              context swap; fix is in the swpctx /
                                              TB-invalidate path or the ITB lookup,
                                              NEVER a C++ page-table walker (P-1).
