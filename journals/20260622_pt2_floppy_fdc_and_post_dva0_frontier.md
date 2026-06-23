# Journal -- 2026-06-22 (pt2) -- Floppy FDC fixes + post-dva0 krn$_poll frontier

Project:   EmulatR -- Alpha AXP / EV6 (21264) Emulator (V4)
Architect: Timothy Peer.  AI collaboration: Claude (Cowork) + claude.ai web.
Scope:     D:\EmulatR\EmulatRAppUniV4\Emulatr (V4 active tree).  ASCII(128) only.
Follows:   20260622_ds20_iic_base_wall_cleared.md (the IIC base milestone).

================================================================================
ARC OF THE AFTERNOON
================================================================================
After the IIC base fix cleared the post-banner wall, the DS20 cold boot ran the
full device-enumeration sequence (PCI hose 0/1, PCI-to-ISA bridge, Cypress
82C693 IDE, GCT/FRU init) and reached the SRM powerup option-firmware scan,
where it stalled on "Checking dva0.0.0.0.0 ...".  Fixed the floppy (FDC) so it
enumerates as an empty drive; the boot then advanced past the floppy into a
NEW frontier: a perpetual kernel-scheduler idle loop downstream of dva0.

================================================================================
FLOPPY (82077 FDC) -- THREE EDGES, all landed in deviceLib/Tsunami/Floppy82077.h
================================================================================
The dva0 stall was NOT the read path (#20's recalibrate fast-fail) -- it was the
82077 INTERRUPT EDGE model.  EMULATR_FDC_TRACE pinned each fault in turn:

1. RESET-COMPLETION IRQ (DOR handler).  Trace showed only DOR 0x08 (reset
   asserted) then 0x0C (released) then silence: enable_controller waits for the
   reset-completion IRQ6 the 82077 raises on /RESET deassert, which our model
   never raised.  FIX: on DOR bit2 0->1 (exit reset), set m_intPending=true.
   -> driver then ran DSR/CCR/SPECIFY/PERPENDICULAR/motor/RECALIBRATE.

2. COMMAND-BUSY DEASSERT (fifoWrite, opcode byte).  After the reset edge,
   m_intPending stayed stuck high (driver never SENSE-INTERRUPTs the reset IRQ),
   so RECALIBRATE set it 1->1 = NO EDGE; edge-triggered ISA IRQ6 latched
   nothing; ide_recalibrate_cmd's krn$_wait hung to its 5000-unit timeout x2 x
   every floppy_devtab row.  FIX: accepting a command opcode takes the
   controller BUSY -> clear m_intPending (deassert); completion re-asserts ->
   fresh 0->1 edge the PIC samples.  -> RECALIBRATE now completes via IRQ;
   SENSE INTERRUPT drains ST0=0x70; READ DATA returns ST0=0x40 (no media).

3. PATH-B RESULT-DRAIN CLEAR (fifoRead, result drained).  Per 82077 + claude-web
   FDC spec: result-phase commands (READ/READ ID/WRITE) clear their interrupt
   when the host READS the result phase, not via SENSE INTERRUPT.  FIX: clear
   m_intPending when m_resPos>=m_resLen.  (Fidelity tidy; uncommitted -- needs
   the next rebuild.)

claude-web's 82077 enumeration spec (uploads/20260622_fdc82077_floppy_
enumeration_briefing.md) gates G1/G2/G3 RESOLVED against the real trace:
  G1: NO 4x post-reset SENSE-INTERRUPT poll (SRM goes reset->SPECIFY); single
      reset-IRQ model is correct; ST0=0xC0..0xC3 NOT needed for this firmware.
  G2: all init opcodes (03/12/07/08/06/13/0A) handled by cmdShape; no stall.
  G3: super-I/O path works (FDC sees the writes; no latent write-fold bug).
  Path A/B: ours already correct (READ returns a result phase, not the draft's
  sense-int bug).  CONCLUSION: full FDC rewrite NOT needed; the targeted edges
  met the spec's goal.  Floppy now enumerates: bounded 177 FDC ops, 3 READs,
  completes ~3.18B cyc, concludes no-media.

RESIDUAL (not a bug): the dva0 probe still costs ~3B cyc -- FIRMWARE timed waits
(floppy_spinup motor delay + krn$_sleep per density/retry), counted faithfully.
Our model completes commands immediately (per spec sec 6); the delay is
guest-side.  Warp/snapshot territory, not more FDC code.

================================================================================
*** CORRECTION (same session, after the analysis below): NOT A HANG ***
================================================================================
The "post-dva0 perpetual krn$_poll loop" analyzed below was WRONG as a verdict.
With enough cycle budget the DS20 boot ADVANCED past it and reached the
INTERACTIVE "UPD>" / "update srm" LFU (Loadable Firmware Update) prompt -- a
live console.  The krn$_wait/krn$_semrelease loop was the cooperative scheduler
IDLING between the firmware's slow timed waits (floppy_spinup + krn$_sleep +
the LFU option-firmware scan), i.e. slow-but-progressing, not a deadlock.
LESSON (cf feedback_trace_silence_is_not_looping): given the floppy_spinup/
krn$_sleep evidence, "slow timed boot" should have outweighed "blocked"; the
profile's 28% in the scheduler was normal idle, not a spin.  DS10 previously
reached this same UPD> (project_session_20260604_interrupt_chain_live); from
UPD>, P00>>> is the no-Enter branch or `update srm`.  So DS20 now boots to a
live console.  The symbolication "next diagnostic" below is moot for this wall
(there was none); keep it as general tooling.  Retained below for the record.

================================================================================
NEW FRONTIER -- post-dva0 perpetual krn$_poll idle loop  [SUPERSEDED -- see correction above]
================================================================================
With the floppy enumerating, the boot advances past dva0 and lands in a
perpetual loop at native PC 0x1ad8f0 / 0x1adb60, cycling every ~262K cyc (clock
tick) out to 5.1B+ with the console parked at "Checking dva0".  DISASSEMBLY of
those PCs (decompressed_ds20_v7_3.bin, base 0x8000) identifies them as the
KERNEL COOPERATIVE SCHEDULER:
  0x1ad8b0-0x1ad8f4 = krn$_wait : CALL_PAL 0x3f (timer-yield), then manipulates
                      the wait-object at r22 (+0x14 flag, +0x20 count, +0x00 lock).
  0x1adb14-0x1adb60 = krn$_semrelease : LDL_L/STL_C spinlock on (r22), bump
                      +0x20, clear +0x14, release lock.
So 0x1ad8f0/0x1adb60 is krn$_poll idling because the FOREGROUND task is
blocked/looping and never completes.  Same scheduler PCs as the ORIGINAL
pre-PCI wall, but reached ~everything-further-along (5.1B cyc, post device scan)
-- it is the universal idle pattern, NOT a regression to square one.

LEADS CONSIDERED + STATUS:
- GCT/FRU truncation (project_gct_cyclic_link_set_hang, the DS10 root: build_
  power_hw needs IIC 0x70/0x72, else tree truncates, FRU_ROOT missing, walk
  rings): WEAK for DS20.  ds20_platform.json ALREADY declares iic_system0(0x70)/
  iic_system1(0x72)/iic_smb0(0xA2)/iic_cpu0(0xA4)/iic_rcm_nvram0(0xC0) -- same
  set as the working DS10 -- and the IIC base is now correct (0xFFF80000), so
  build_power_hw/build_fru should succeed and the tree should be complete.  The
  0x3ff2xxxx pointers in the DIVERT-REI dump are NOT a clean GCT walk: the was=
  registers jump around tick-to-tick (0x3ff23bbc / 0xaf5ac / 0x1 / 0x3bb7c),
  i.e. random foreground instants, and the GCT base is 0x3ff32000 (ABOVE those
  pointers).  Do not anchor on GCT.
- The REAL foreground hot code (profile top non-scheduler buckets) is at
  0x79xxx (10.4%), 0x62xxx (8%), 0xaxxx (7.9%), 0x7exxx (5%), 0xecxx (4.9%),
  active banner->cap.  Naming THESE is the way in.

NEXT-SESSION FIRST DIAGNOSTIC (decided):
  Identify the blocked foreground, two complementary moves:
  (a) SYMBOLICATE 0x79xxx/0x62xxx/0xaxxx/0x7exxx/0xecxx (and krn$_wait's
      caller) -- the symbolication pipeline (tasks #14-17, DS10 map) was built
      for exactly this; if cross-build correlation is too weak, port via Ghidra
      VT/BSim as planned.
  (b) BreakpointSink full-complement gated trace on the FOREGROUND work region
      (not the scheduler PCs) for a few revolutions, to see what it loops on /
      what event it krn$_waits for.  Pair with EMULATR_IIC_TRACE=1 +
      EMULATR_GCT_WATCH=1 (both already implemented) to rule GCT in/out cleanly.

================================================================================
STATE OF THE TREE (uncommitted edits this session)
================================================================================
- chipsetLib/TsunamiChipset.h: PCF8584 IIC model-conditional base table
  (DS10=0xFFFF0000, DS20/DS20E=0xFFF80000), fail-loud find-or-fail.  COMMITTED?
  no -- verify + commit Windows-side.
- deviceLib/Tsunami/Floppy82077.h: 3 edges (reset-completion IRQ, command-busy
  deassert, Path-B result-drain clear).  Last one needs a rebuild.
- pipelineLib/MemDrainer.h: EMULATR_IIC_WATCH probe (REMOVE-WITH-fix, gated).
- palBoxLib/grains/PalEntries.cpp + systemLib/Machine.cpp: SDE swap-ledger probe
  (gated, REMOVE-WITH; shadow question is off the critical path / benign).
- DS10 regression run (task #23) still OUTSTANDING -- run a DS10 cold boot to
  confirm the IIC table row 0xFFFF0000 path is unchanged before committing.

CLEANUP BEFORE COMMIT: pull EMULATR_IIC_WATCH + the SDE swap-ledger probe;
DecListingSink.h:263 m_emitEnabled back to false.

================================================================================
HEADLINE FOR THE DAY
================================================================================
One wrong constant (IIC base DS10 vs DS20) was the post-banner wall; fixing it
+ three 82077 interrupt-edge fixes took the DS20 cold boot from "spins forever
right after the banner" to "runs the entire device-enumeration + option-firmware
scan and idles in the kernel scheduler waiting on the next unmodeled event."
Enormous forward motion in one session.
