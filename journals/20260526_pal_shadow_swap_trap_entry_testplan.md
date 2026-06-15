EmulatR V4 -- Test Plan: PAL Shadow-Swap Omission on Trap Entry
==============================================================
Date: 2026-05-26   Author: Tim Peer / Claude collaboration
Trace under study: D:\EmulatR\traces\20260523-205324_srm.trc (DS10 cold boot)

1. ROOT-CAUSE STATEMENT (to verify, not assume)
-----------------------------------------------
PipelineDriver::retire() delivers a fault by entering PAL mode with a
direct PC low-bit write:

    cpu.pc = coreLib::ev6::computeHwExceptionEntry(cpu.palBase, entryOffset)
           | uint64_t{1};

It does NOT route through coreLib::palModeEnter(), so the EV6 SDE shadow
swap (swapPalShadowRegs over R4-R7 / R20-R23) is SKIPPED on trap entry.
HW_REI return (execHwRei -> coreLib::setPalMode) DOES perform the swap.
Result: one unmatched swap per trap/return cycle while I_CTL[SDE] is set.

Observable consequence in the trace, store at PC 0x600d3c (R21 base, a
shadowed register), first-touch of unmapped page 0x5f0000:
    cyc 4196736  EA = 0x5f0004  (native R21, correct)   -> DTB miss
    cyc 4196758  EA = 0x0f01    (stale shadow R21, WRONG) -> DTB miss
    cyc 4196780  EA = 0x5f0004  (native R21, correct)   -> commits
Native R21 was set to 0x5f0004 at cyc 4196734; shadow copy (trace H21)
held 0x0f01 throughout. 0x0f01 is the shadow contents, NOT a truncated
PC (the H22=0x600f01 match is coincidental).

Two damage modes to confirm:
  (a) The PAL miss-handler at 0x600301 runs on the NATIVE R4-7/R20-23 set
      (swap skipped), so it scribbles PAL scratch onto the interrupted
      native code's live registers.
  (b) The replayed faulting instruction resolves its base operand from
      the wrong (swapped) register copy.
Hypothesis: (a) during the setup phase is why the descriptor region the
0x600f00 search walks reads as NULL/zero (empty table -> 14M-cycle
downward scan -> NXM at va=-4 -> HALT at 0x600501, cyc 18362889).

2. PRIMARY METHOD -- LIVE DEBUGGER (Visual Studio, manual single-step)
---------------------------------------------------------------------
Goal: watch intReg[21] vs intShadow[5] and the swap count across the
three store attempts, live, to feed back state not present in the log.

Build: relwithdebinfo (symbols + optimisation) OR debug. NOTE the
relwithdebinfo tree has been stale before (DRAINA artifact) -- rebuild
clean and confirm the run-date header in the .trc before trusting it.

Skip the ~4.19M-cycle replay: arm a predig snapshot so cold restarts
resume just before the episode (see reference_predig_snapshot):
    --snapshot-on-pc 0x600c00     (routine entry, ~cyc 4196358)
The predig_ snapshot autoloads by mtime on subsequent cold starts.
Clear stale snapshots first (MINGW64): rm -f snapshots/*.axpsnap
(do NOT use cmd 'del' under bash).

Conditional breakpoints (all gated on cycle to avoid stepping millions
of instructions). Use cpu.cycleCount in the condition expression.

 BP-1  PipelineDriver.h, retire() fault-delivery line
         cpu.pc = computeHwExceptionEntry(...) | 1;
       Condition: r.faultCode != coreLib::kNoFault
                  && cpu.cycleCount >= 4196730 && cpu.cycleCount <= 4196785
       Watch:  cpu.intReg[21], cpu.intShadow[5],
               coreLib::iCtlSdeHigh(cpu.i_ctl), cpu.inPalMode(),
               r.faultCode, cpu.mm_stat
       Expect: SDE true; on entry NO swap happens (step over the line and
               confirm intReg[21] is UNCHANGED across trap entry).

 BP-2  PalShadow.h, swapPalShadowRegs() first line
       Condition: cpu.cycleCount >= 4196730 && cpu.cycleCount <= 4196785
       Purpose: count hits per trap/return cycle.
       Expect (bug present): 0 hits during trap entry, 1 hit during the
               HW_REI return -> asymmetric. (After fix: 1 and 1.)

 BP-3  PipelineDriver.h, buildCtx() store-operand resolution
         ctx.opB = rbFromFp ? cpu.fpReg[rb] : cpu.intReg[rb];
       Condition: slot.grain.pc == 0x600d3c
                  && cpu.cycleCount >= 4196730 && cpu.cycleCount <= 4196785
       Watch:  rb, cpu.intReg[rb], ctx.opB
       DECISIVE: at the 2nd attempt (cyc ~4196758) expect rb==21 and
               cpu.intReg[21]==0x0f01 (shadow) while 0x5f0004 sits in
               intShadow[5] -- proves the registers are swapped at
               resolution time. At attempts 1 and 3 expect 0x5f0004.

 BP-4  PalEntries.cpp, execHwRei() setPalMode call
       Condition: cpu.cycleCount >= 4196730 && cpu.cycleCount <= 4196785
       Watch:  resumeInPal, cpu.inPalMode() before, intReg[21] before/after
       Expect: PAL->native transition fires the swap (intReg[21] flips).

Record this table per attempt:
    attempt | cyc      | pal at resolve | intReg[21] | intShadow[5] | EA
    1       | 4196736  | ?              | ?          | ?            | ?
    2       | 4196758  | ?              | ?          | ?            | ?
    3       | 4196780  | ?              | ?          | ?            | ?

3. SECONDARY METHOD -- GATED INSTRUMENTATION (if live-step impractical)
----------------------------------------------------------------------
Mirror the existing MEMDIAG pattern (default-0 compile gate, folds away).
Add a temporary EMULATR_SHADOWDIAG block, window cyc 4196730..4196785:
  - in retire() fault delivery: log faultCode, cycle, SDE, inPalMode,
    intReg[21], intShadow[5], plus a "swap NOT called here" marker.
  - in swapPalShadowRegs(): log cycle + a static call counter.
  - in buildCtx() for pc==0x600d3c: log rb, intReg[rb], resulting opB.
Revert the gate to 0 after (same discipline as MEMDIAG / ISADIAG).
Combine with the predig snapshot so each iteration is seconds, not
minutes. Emit to the build-tree (mounted) per reference_v4_cli_run_procedure,
not X:\traces.

4. CLEANEST VERIFICATION -- ISOLATED DOCTEST (no boot required)
--------------------------------------------------------------
Add a doctest that reproduces the swap parity in isolation. doctest
discipline: CHECK only, never REQUIRE (exceptions disabled in V4).

  - Construct CpuState: set I_CTL[SDE] (iCtlSdeHigh) high; palMode=native.
  - intReg[21] = 0x5f0004 (native);  intShadow[5] = 0x0f01 (shadow).
  - Simulate trap delivery exactly as retire() does today (set PC<0>=1
    directly, NO swap), then simulate HW_REI return (setPalMode(false)).
  - CHECK(cpu.intReg[21] == 0x5f0004);   // FAILS today -> reads 0x0f01
  - Second CHECK: a balanced trap-entry + HW_REI leaves R20-23 unchanged.
  - Optional: drive a real DTB-miss store through the pipeline and CHECK
    the EA is identical on first attempt and on replay.
This is the regression test that should accompany the eventual fix
(route trap entry through palModeEnter), and it pins the bug without the
4M-cycle boot.

5. GUEST-MEMORY ADDRESSES TO WATCH (the "debug these addresses" ask)
-------------------------------------------------------------------
Search/structure region (data watchpoints or gated load/store logs):
    0x5f0004, 0x5ff41c, 0x5ff460, 0x5ff464, 0x5ff468
Builder inputs that came back zero/NULL (find who should have set them):
    0x6020ac (alloc ptr, =0x5f0000 OK)
    0x6020c4 (count, reads 0 at cyc 4196726 -- suspect)
    0x602290 (ptr, =0x6020ac)
    VA 0x00000000 (outright NULL deref at cyc 4196797)
Correlation to chase: whether any of these zero inputs trace back to a
native R4-7 / R20-23 register that an earlier PAL handler clobbered
because the trap-entry swap was skipped. If yes, (a) and the empty table
are the same root.

6. SUCCESS CRITERIA
-------------------
Bug confirmed when: swap-count is asymmetric (0 entry / 1 return); and
buildCtx at attempt 2 resolves intReg[21]==0x0f01 (shadow) with the
native 0x5f0004 displaced into intShadow[5].
Fix validated when, after routing trap entry through palModeEnter:
  - swap count is symmetric (1/1);
  - the store takes ONE fault, EA stable at 0x5f0004 across replay;
  - the PAL miss-handler operates on the shadow set;
  - doctest #4 passes; full suite stays green (CHECK-only);
  - DS10 re-run advances past cyc 18362889 / PC 0x600501 (and ideally the
    0x600f00 search terminates because the descriptor region populates).

7. NOTE -- pre-existing temp diagnostic
---------------------------------------
execHwRei() carries an un-gated "[HW_REI XITION #N]" fprintf marked
"REMOVE BEFORE COMMIT". Leave it during this investigation (useful), but
it must come out before any commit.
