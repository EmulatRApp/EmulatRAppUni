================================================================================
EmulatR V4 -- BRIEFING: STORE-WATCH to pin PAL$CPU0_START_BASE[1] (base pinning)
================================================================================
Written: 2026-06-21.  Drives a single-agent diagnostic run.  Purpose: pin the
ONE physical address that the entire agent-1 bring-up dereferences, empirically,
before any second agent or symbol consumer is wired.  Verify-before-decode.

Sequence context: TIG CPU-START step committed (TsunamiTig 0xC00028+id latch +
coreLib/Ev6Pc264PalDefs.h).  THIS is the next step.  AFTER it: take the
staging-discipline decision (dispatcher outbox / arbiter-forwards-to-LockMonitor
+ MemDrainer reservation staging), THEN wire agent1 live under ThreadedDriver.
This run is SINGLE-AGENT (the console stores; no second agent observes yet), so
it deliberately does NOT force the staging decision.

--------------------------------------------------------------------------------
0.  WHY THIS FIRST (the bedrock)
--------------------------------------------------------------------------------
start_secondary(id) [apisrm pc264.c:333] does:
    ((uint64*)PAL$CPU0_START_BASE)[id] = PAL$PAL_BASE + 1;   // stage entry addr
    mb();
    outtig(NULL, 0xC00028 + id, 0);                          // kick CPU id
The secondary then loads PAL$CPU0_START_BASE[whami] and hw_ret_stall's to it
[EV6_VMS_PC264_PAL.MAR:1040].  agent1's slot read, the parked-release target, and
every Ev6Pc264PalDefs.h offset all resolve through PAL$CPU0_START_BASE[1].  If
that PA is wrong by even one slot, every later symptom is a second-order shadow
of one bad constant -- you'd debug the second agent when the bug is the address.
Pin it once, empirically, up front.

The offsets in Ev6Pc264PalDefs.h are CANDIDATES: they resolve against the PAL
`get_base`, NOT palBase.  The console image loads verbatim at guest PA 0x0
(SrmLoader), so the candidate absolute PA is 0x100 + id*8 -> CPU1 at PA 0x108.
This run confirms or corrects that candidate.

--------------------------------------------------------------------------------
1.  PRECONDITIONS  (REVISED 2026-06-21 after grepping the gate chain)
--------------------------------------------------------------------------------
GREP FINDING (changes the run design -- do NOT pre-seed blindly):
  - cpu_enabled DEFAULTS to 0xffffffff for PC264 [ev_action.c:1169; NONVOLATILE
    but all-CPUs-on], so bit 1 is set BY DEFAULT.
  - robust_mode DEFAULTS to 0 [powerup.c:179].
  - start_secondaries() [pc264.c:321] is called from powerup.c:438 gated ONLY by
    `if (!robust_mode)` -- there is NO dualCPU() gate on the call, and the loop
    `for i<MAX_PROCESSOR_ID: if i!=primary && (cpu_enabled&(1<<i)) start_secondary
    (i, PAL$PAL_BASE+1)` [pc264.c:327-330] has NO CPU-presence check.
  => start_secondary(1) fires BY DEFAULT once powerup reaches line 438.

CORRECTION 2026-06-21 (value+kick watch run, fw_ds20_20260621-193535): the
"banner => passed start_secondaries" claim above is WRONG and is WITHDRAWN.  The
"Running on the ISP model" banner is printed by platform() in EARLY powerup;
start_secondaries is at powerup.c:438 AFTER ddb_startup(4) (full device init),
which is LATER.  Evidence:
  - No START-WATCH(kick) (TIG 0xC00028+id) through 268M; (val) stores stop at
    182M (the 0x600938 vector/SCB relocation), nothing after.
  - The 108B cold run's console (app_output_20260621150723.log) shows ONLY the
    early banner through 108,000,000,000 cycles -- NO device-probe output, so
    ddb_startup(4) and start_secondaries were NOT reached even by 108B.
  => start_secondaries is gated behind a DS20 POST-BANNER boot-progress wall
     (the "console idle" state).  cpu_enabled is NOT the blocker (not in
     ds10_flash.rom -> 0xffffffff default -> bit 1 set).  This is upstream of
     CPU1 bring-up: base-pinning cannot complete until powerup reaches
     ddb_startup(4) -> line 438.

THE REAL GATE is therefore "what is the boot doing for ~100B+ cycles after the
early banner (PC region from ~182M onward; run ended at PC=0x1adb60 / kFaultDtbMiss)
that keeps it from reaching ddb_startup(4)?"  NEXT DIAGNOSTIC = retire-PC profiler
(boot-profiler ticket): bucket retired PCs over 182M->current, find the dominant
region, decode wait-loop-vs-progress.  Base-pinning resumes only once powerup
reaches line 438.  (A short cold value/kick watch is moot until then; a warm
resume from the 107B banner snapshot will NOT catch the kick -- start_secondaries
is far ahead of 107B, not just past it.)

  P1 (DEMOTED -- NOT a gate for this run).  cns$srom_proc_mask / dualCPU()
     [pc264.c:502] only shapes the SysType/DSRDB config tree; it does NOT gate the
     start_secondaries call or the CPU1 kick.  Leave proc_mask as-is for the
     base-pinning run.  (It matters later for config-tree fidelity + whether a
     downstream rendezvous wait expects CPU1 alive -- revisit at agent1-live.)

  P2 (DEFAULT-SATISFIED -- verify, do not assume; seed only as FALLBACK).
     The bit is already set unless a persisted NVRAM cpu_enabled overrides it to a
     smaller mask.  Instrument the consume side instead of pre-seeding:
       FENCE-1 (read-confirmation, ALWAYS): load-watch / one-line log at
         ev_read("cpu_enabled") [pc264.c:327] (and hwrpb.c:688) to capture the
         EFFECTIVE value the console got.  Bit 1 set -> P2 proven, no seed needed.
       FALLBACK (b) NVRAM pre-seed cpu_enabled=3 -- ONLY if FENCE-1 shows bit 1
         cleared.  If used, fence it:
           FENCE-2 (format/checksum): confirm SRMEnvStore's flash env-block layout
             + whether the console validates a block checksum; write the value in
             that EXACT representation or it reads back absent/garbage.
           FENCE-3 (re-default ordering): verify powerup does not re-create/
             re-default cpu_enabled before pc264.c:327 reads it (FENCE-1 will show
             a clobber).
     Option (a) -- warm from a past->>> snapshot, `set cpu_enabled`, re-snapshot --
     is CIRCULAR for DS20 today (no >>> snapshot exists; >>> is downstream of the
     very start_secondaries path being instrumented).  Keep (a) only as the
     faithful end-to-end re-validation to run ONCE DS20 reaches >>>.

--------------------------------------------------------------------------------
2.  REACH-CONFIRMATION  (disambiguates a no-fire)
--------------------------------------------------------------------------------
Arm an independent positive signal that start_secondary(1) was REACHED, so a
silent watch tells you "wrong PA" not "never got there":
  - PC-watch on start_secondary entry (resolve its PA from the ghidra
    AlphaDS20_v7_3 project / console symbol table -- do NOT guess the PC), via
    the BreakpointSink paired-PC gate (traceLib/BreakpointSink.h
    setBreakOnGateOpen) or a PipelineDriver PC compare; OR
  - the console banner "starting console on CPU 1" (or equivalent) via the
    Uart16550 stderr mirror.
If reach-confirmation FIRES but the store-watch does NOT -> the store PA differs
from the watched range (wrong base/symbol).  If reach-confirmation does NOT fire
-> a precondition (P1/P2) failed; fix that first, the watch proves nothing yet.

--------------------------------------------------------------------------------
3.  THE WATCH  (range, not a single computed address)
--------------------------------------------------------------------------------
RUN PARAMETERS: cold boot (--no-autoload), NO env seeding on the first pass,
--max-cycles ~0x10000000 (~268M) so it runs past the banner / powerup
start_secondaries point (~190M).  If FENCE-1 shows cpu_enabled lacks bit 1 OR
reach-confirmation never fires, only THEN apply the P2 fallback seed and re-run.

Add an env-gated STORE-WATCH in MemDrainer::applyStoreEffect (mirror the existing
EMULATR_GCT_WATCH / PA10-STOREWATCH idiom: a gated PA compare logging cyc/pc/va/
pa/size/value).  RANGE-watch the candidate region, not one address, so a base
that is wrong by one slot is still caught:

    candidate base B = 0x0 (console image base; see Ev6Pc264PalDefs.h caveat)
    watch  [B + 0x100  ..  B + 0x100 + MAX_PROCESSOR_ID*8)   // the start-slot array
    also watch PAL$HALT_SWITCH_IN candidate (B + 0x220) opportunistically.

Capture on every hit: storing PC, faulting VA, resolved PA, store VALUE, size,
cycle, palMode.  Gate behind a new env (e.g. EMULATR_START_WATCH=1) so normal
runs stay silent.  REMOVE-BEFORE-COMMIT like the sibling temp watches.

--------------------------------------------------------------------------------
4.  TWO CONFIRMATIONS ON THE HIT
--------------------------------------------------------------------------------
A genuine start_secondary(1) store satisfies BOTH:
  C1. VALUE == (palBase | 1)  (i.e. PAL$PAL_BASE+1, the enter-in-PAL target).
      This DOUBLES as evidence for the PC<0> low-bit / enter-in-PAL invariant --
      the staged entry carries the PAL-mode bit, exactly the inPalMode()==pc<0>
      model.  Cross-check palBase against the live value at capture.
  C2. The NEXT TIG write is outtig(0xC00028 + 1) -> PA 0x801_3000_0A40 (kIpcr1).
      The store -> mb -> poke ordering from start_secondary is the correlation
      signature.  If you see the slot store but NOT the immediately-following
      0xC00029/kIpcr1 write, the mb or the TIG decode ordering is suspect.

(For CPU id != 1, the store is to base + id*8 and the poke is 0xC00028+id; CPU1
is the first/only secondary on DS20, so id==1.)

--------------------------------------------------------------------------------
5.  THE RECONCILE GATE  (verify-before-decode, closed)
--------------------------------------------------------------------------------
Compare the captured PA against coreLib/Ev6Pc264PalDefs.h cpuStartSlotOffset(1)
resolved through the confirmed base:
  MATCH    -> the symbol is validated; agent1 (and any consumer) may dereference
              PAL$CPU0_START_BASE.  Record the confirmed base in the header's
              base-resolution note (replace "candidate" with "confirmed @ <PA>,
              run <date>").
  MISMATCH -> correct the symbol / base in Ev6Pc264PalDefs.h BEFORE anything
              reads it.  Capture the real base; re-run to re-confirm.  Do not
              proceed to agent1 on an unconfirmed base.

--------------------------------------------------------------------------------
6.  OUT OF SCOPE (deliberately deferred to the next round)
--------------------------------------------------------------------------------
- Staging discipline (dispatcher outbox / LockArbiter-forwards-to-LockMonitor /
  MemDrainer reservation staging).  This run is single-agent; no second agent
  observes shared reservation state, so the LL/SC-ordering-between-two-agents
  correctness question is not forced here.  Take it deliberately, on its own,
  AFTER the base is pinned, BEFORE agent1 goes live under ThreadedDriver.
- Wiring the second agent itself.

--------------------------------------------------------------------------------
7.  DELIVERABLES OF THIS RUN
--------------------------------------------------------------------------------
1. Confirmed absolute PA of PAL$CPU0_START_BASE (and thus [1]) + the resolved
   base, with storing PC + cycle + value, captured from a reach-confirmed run.
2. C1 (value==palBase|1) and C2 (next write 0xC00029) both observed.
3. Ev6Pc264PalDefs.h base note updated: candidate -> confirmed (or corrected).
4. Green light (or correction) for agent-1 bring-up to dereference the slot.

End of briefing.
