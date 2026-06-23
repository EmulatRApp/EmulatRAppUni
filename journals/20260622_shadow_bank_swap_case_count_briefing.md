<!--
EmulatR V4 -- BRIEFING: PALshadow bank swap -- case-count decision
Project: EmulatR (Alpha 21264 / EV6-EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic (claude.ai web).
Written 2026-06-22.  Design sign-off document; Cowork implements against the
live tree after resolving the [LOCATE]/[CONFIRM] items.  ASCII(128) only.
Per ADR-0001 this header stands in for the source-file attribution block.
-->

================================================================================
BRIEFING -- PALshadow bank swap: how many cases to support
================================================================================

PURPOSE
  Button the shadow-register topic by fixing the swap's structure, not by
  enumerating cases.  Standing question: "SDE<1>, PalMode<1>; each one or both
  = 3 cases?"  Answer below: NO -- one predicate, two input sources, zero
  special cases.  This is a harden-by-construction refactor: the log analysis
  (run 20260622-140754) shows the swap is already producing architecturally
  correct results (leave-swap fires; interrupted program resumes in the normal
  bank, proven by next-tick was=0x0).  The DIVERT-REI mismatches are a checker
  bank-alignment artifact, addressed in section 5, NOT live corruption.  The
  boot hang is a separate device gap (section 6), tracked elsewhere.

--------------------------------------------------------------------------------
0.  THE MODEL  (one boolean function, not two independent switches)
--------------------------------------------------------------------------------
EV6 exposes the shadow copies of R4-R7 and R20-R23 iff BOTH conditions hold:

    shadow_visible  ==  ( palMode  AND  I_CTL[SDE<1>] )

I_CTL[SDE<1>] is the PALshadow-enable bit.  [CONFIRM exact field position
against EV6 HRM I_CTL -- referenced here as SDE<1> per the architect's
notation.]  The hardware does not copy data; there are two physical register
files and this conjunction selects which one the register number decodes to.
A "swap" in V4 is the act of re-pointing the eight register slots when the
selected bank changes.

KEY FACT: the selector is a single boolean FUNCTION of two inputs.  The bank
must change on every EDGE of that function -- every time
( palMode AND SDE<1> ) flips truth value -- regardless of WHICH input moved.

--------------------------------------------------------------------------------
1.  THE COUNT  (this is the decision)
--------------------------------------------------------------------------------
Truth table of the selector:

    palMode   SDE<1>   selector   bank
       0        0        false    normal
       0        1        false    normal
       1        0        false    normal
       1        1        TRUE     shadow      <-- the only shadow state

DECISION -- support exactly ONE case, computed, not enumerated:

    SWAP  iff  selector_before  !=  selector_after

where selector = (palMode AND SDE<1>) sampled before and after any mutation of
either input.

Do NOT implement the "3 cases" (SDE-alone / palMode-alone / both):
  - "SDE changes alone" and "palMode changes alone" are not different ACTIONS.
    The action is identical (flip the bank); only the FIRE condition differs,
    and the fire condition is the same edge predicate for both.  Two hand-
    written predicates that each try to predict the edge are two chances to
    slip parity.  That is precisely the bug class we were hunting.
  - "Both change simultaneously" is degenerate.  In this design palMode and
    SDE<1> mutate at disjoint instruction sites (palMode at PAL enter/leave;
    SDE<1> only at HW_I_CTL writes), so a simultaneous flip does not occur in
    normal flow.  Even if it did, the edge predicate computes the correct
    result from before/after of the conjunction -- no separate case needed.

So: 1 swap rule.  2 input sources (where to evaluate it).  0 "both" case.
The "3" framing is the enumeration to avoid.

--------------------------------------------------------------------------------
2.  THE PREDICATE  (single source of truth for the swap)
--------------------------------------------------------------------------------
Introduce one function that every input-mutation site routes through.  Shape
(names/placement [LOCATE] against the live PalShadow / CpuState seam):

    // Call AROUND any change to palMode or SDE<1>.  Caller captures the
    // selector before mutating, mutates, then passes new state in.
    inline void reconcileShadowBank(CpuState& cpu,
                                    bool palBefore, bool sdeBefore,
                                    bool palAfter,  bool sdeAfter)
    {
        const bool selBefore = palBefore && sdeBefore;   // SDE<1> already
        const bool selAfter  = palAfter  && sdeAfter;    // reduced to bool
        if (selBefore != selAfter)
            swapPalShadowRegs(cpu);   // existing physical-bank flip
    }

Rules for Cowork:
  - swapPalShadowRegs stays the ONLY place the eight slots are re-pointed.
  - No site computes its own "should I swap" condition.  Every site supplies
    before/after and lets the predicate decide.  This makes parity structural:
    each call is independently correct, so any sequence of calls composes.
  - SDE<1> is reduced to a bool at the seam (mask + shift, [CONFIRM] bit pos),
    so the predicate never re-derives the field encoding.

--------------------------------------------------------------------------------
3.  CALL SITES  (where to invoke the one predicate -- sites, not cases)
--------------------------------------------------------------------------------
These are the locations where an input can move.  All route through section 2.
Line numbers are point-in-time; Cowork [LOCATE]s current positions.

  S1. PAL enter -- palMode 0->1 :
        - fault divert            [LOCATE PipelineDriver.h, ~1245]
        - interrupt/clock divert  [LOCATE Machine.cpp, ~208]
        - CALL_PAL entry          [LOCATE PalEntries.cpp, ~1158]
  S2. PAL leave -- palMode 1->0 :
        - HW_REI                  [LOCATE PalEntries.cpp setPalMode path]
  S3. SDE<1> change while in PAL :
        - HW_I_CTL write handler  [LOCATE PalEntries.cpp, ~1668]

Audit target: confirm S1/S2 currently swap "only if SDE<1> already set" and S3
"only if palMode set."  After this refactor those conditionals DISAPPEAR -- they
are subsumed by the before/after predicate.  Verify no site retains an
independent swap call after consolidation (grep swapPalShadowRegs; every hit
must be inside reconcileShadowBank).

--------------------------------------------------------------------------------
4.  WORKED EXAMPLE -- the VMS clock tick  (why parity is now automatic)
--------------------------------------------------------------------------------
The clock ISR toggles SDE<1> twice inside PALmode.  With the single predicate,
one tick produces four reconcile calls, each independently correct:

  enter  : palMode 0->1, SDE<1>=1  -> sel 0->1 -> SWAP   (normal -> shadow)
  zap    : SDE<1> 1->0, palMode=1  -> sel 1->0 -> SWAP   (shadow -> normal:
                                                          expose saved frame)
  restore: SDE<1> 0->1, palMode=1  -> sel 0->1 -> SWAP   (normal -> shadow)
  leave  : palMode 1->0, SDE<1>=1  -> sel 1->0 -> SWAP   (shadow -> normal)

Net parity over the tick is even by construction; no hand-counting.  A plain
CALL_PAL that never touches SDE<1> produces exactly two calls (enter, leave) and
the same even parity.  This is the entire reason to collapse to one predicate.

--------------------------------------------------------------------------------
5.  CHECKER FIX  (separate from the swap; stops the false DIVERT-REI flags)
--------------------------------------------------------------------------------
The DIVERT-REI MISMATCH lines (R05/R06/R20/R23 at savedPc=0x1adb60) are the
checker snapshotting the SHADOW bank at the REI capture point while it
snapshotted the NORMAL bank at divert.  Evidence: the "now" values are frozen
PAL scratch (R05 now=0x1670d0, R23 now=0x1f, R20 now=0x3c4c8) across all 983
hits, and "was" recovers to 0x0 on the next tick -> the program bank is intact.

ACTION: align both DIVERT-REI snapshots to the SAME bank (the interrupted
program's normal bank).  Capture "was" and "now" through the same bank-resolved
accessor, taken at the same architectural point relative to the swap.  Add, as a
one-shot diagnostic, a selected-bank tag + a running swap-parity counter logged
at both capture points; a correct tick shows even parity and bank(REI)==
bank(divert).  Expectation after this fix: the R05/R06/R20/R23 mismatches at
0x1adb60 vanish.  [Note: R02/R03 mismatches at 0x1ad614/0x1ad75c/0x1ad8f0 are a
DISTINCT, non-shadow signature -- r2/r3 are not banked -- and are out of scope
here; track separately as possible CALL_PAL caller-scratch comparison error.]

--------------------------------------------------------------------------------
6.  ADJACENT FINDING -- UNHANDLED OUTER WRITE 0xfff80000  (SEPARATE work item)
--------------------------------------------------------------------------------
Recorded here because the same run surfaced it and the shadow topic cannot be
read correctly without it.  It is NOT part of the swap change -- do not touch it
in this pass -- but it is the actual boot blocker, so it gets its own briefing
seeded from the evidence below.  Conflating the two is the failure to avoid:
the shadow swap is merely SPOTLIGHTED by this spin (the interval timer keeps
catching the same stuck loop), so fixing the device removes the spotlight, and
fixing the swap does nothing for the hang.

  WHAT THE LOG SHOWS (run 20260622-140754, --max-cycles 0x10000000):
    - The console is SPINNING in the routine at 0x1ad6xx-0x1adbxx.  983 of ~1000
      timer diverts save PC=0x1adb60; the rest cluster at 0x1ad614 / 0x1ad75c /
      0x1ad8f0 -- all the same routine.  Span: cyc ~184,187,451 to end of run
      (~268M).  ~84M cycles of pure spin, i.e. it never exits before the cap.
    - Interleaved with every divert cycle, a fixed device write sequence:
        TsunamiPchip: UNHANDLED OUTER WRITE offset=0x0000fff80000 value=0x41 w=1
        TsunamiPchip: UNHANDLED OUTER WRITE offset=0x0000fff80001 value=0xc5 w=1
        TsunamiPchip: UNHANDLED OUTER WRITE offset=0x0000fff80001 value=0x80 w=1
        TsunamiPchip: UNHANDLED OUTER WRITE offset=0x0000fff80001 value=0xc0 w=1
      Width=1 byte writes, repeating, event counter advancing monotonically
      (10,11,12 ... 31 ...) -- the loop re-emitting the same poke train each pass.
    - Shape: 0xfff80000 = index/address port, 0xfff80001 = data port.  Pattern
      "write 0x41 to index, then 0xc5/0x80/0xc0 to data" is an index/data
      register pair the Pchip outer decode does not claim, so the loop's read-
      back never satisfies its exit condition -> infinite spin.
    - Corroborating: CSERVE func=0x46 (IIC_WRITE) is logged "reserved / no-op"
      in the same window (pc=0x1ad938, palBase=0x8000).

  WORKING HYPOTHESIS (for the device briefing to confirm, not assume):
    0xfff80000/1 is the IIC / SMBus / RMC management-controller register pair.
    This ties to the DS20 system-recognition path: pc264.c get_sysvar() probes
    iic_rcm_temp / iic_8574_ocp to distinguish DS20 vs DS20E vs DP264, and that
    probe rides the IIC controller.  With the controller unmodeled, the console
    poll-waits on a status/ready bit that never comes.  [CONFIRM] the 0xfff80000
    decode and the polled bit against the Tsunami HRM outer-I/O map and the
    0x1ad6xx routine disassembly before wiring anything.

  WHY IT IS OUT OF SCOPE HERE:
    - r5/r6/r20/r23 read 0x0 at "was", so the spin loop does NOT use the
      shadowed registers as live state -> the shadow mismatch is not what stalls
      the loop.  Independent causes; independent fixes.
    - The swap change must land verifiable on its own (parity correctness +
      checker fix), without a device dependency clouding the result.

  HANDOFF: spin a dedicated briefing -- decode 0xfff80000/1, identify the polled
  ready/status bit the 0x1ad6xx loop waits on, and decide implement-vs-satisfy-
  stub.  That is the change that actually clears the boot.  Do NOT attempt it in
  the shadow pass.

--------------------------------------------------------------------------------
7.  DELIVERABLES
--------------------------------------------------------------------------------
1. One reconcileShadowBank predicate; swapPalShadowRegs called ONLY from it.
2. S1/S2/S3 sites converted to before/after calls; per-site swap conditionals
   removed; grep confirms no independent swap call survives.
3. SDE<1> reduced to bool at the seam with [CONFIRM]ed field position.
4. DIVERT-REI checker snapshots bank-aligned; one-shot bank+parity diagnostic
   added behind an env gate; re-run shows the 0x1adb60 mismatches gone.
5. No new cases.  The implementation contains the word "case" zero times for
   this logic.

End of briefing.
