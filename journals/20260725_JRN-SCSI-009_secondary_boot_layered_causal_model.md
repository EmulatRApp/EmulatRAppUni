<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-009
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix (AARM table offsets quoted decimal where the
manual does).
-->

# JRN-SCSI-009 -- WHY SECONDARY BOOT NEVER STARTS: the layered causal model.
#                 Two independent broken gates upstream of SYSBOOT; the good
#                 run's bootstrap address space verified PTE-by-PTE against
#                 AARM 27.4.1.3; "halt code = 0" decoded from the AARM as an
#                 UN-ARCHITECTED console re-entry; the failing run is
#                 snapshot-able from the console via the RX marker.

    Doc id   : JRN-SCSI-009
    Date     : 2026-07-25
    Status   : ANALYSIS RECORD + tooling.  No emulator code changed.
    Relates  : JRN-SCSI-004..008 (the NOIOVEC track), JRN-VMB-019..022.
    Sources  : Alpha Architecture Reference Manual (uploaded 2026-07-25),
               Part III ch 26/27: 27.2 (Table 27-1), 27.4.1.3 (bootstrap
               address space), 27.4.2.1 (HWRPB validation), 27.4.3.6 (BIP/RC),
               Table 26-4 (per-CPU slot, halt fields at +240/+280 decimal).
               Operator transcripts 2026-07-25 (P00>>> baseline + post-boot
               SHOW boot*), PuTTY log 2026-07-17 (M1543C era).
    New tool : tools/snap_ptwalk.py (3-level walk of the console page tables)

--------------------------------------------------------------------------------
## 0. Definition -- what "secondary boot" requires

  OpenVMS boot chain: console -> APB (primary bootstrap, the 1226-block read)
  -> SYSBOOT.EXE (SECONDARY bootstrap) -> kernel.  APB reaches SYSBOOT only
  after it BINDS A BOOT DRIVER -- the IOVEC.  %APB-F-NOIOVEC is APB reporting
  that binding failed; the entry-halt never even starts APB.  Secondary boot
  is therefore blocked by whichever of two INDEPENDENT gates is closed:

      L0  console -> APB handoff        (currently CLOSED -- the regression)
      L1  APB driver bind (IOVEC)       (CLOSED in every run that reached it)
      L2  SYSBOOT load/execute          (never yet reached; no evidence)

  The same media passes L0/L1/L2 on AXPBox ES40 to the V8.3 banner
  (JRN-SCSI-005), so the media and the APB image are good.

--------------------------------------------------------------------------------
## 1. L0 -- the console->APB handoff, graded against the AARM

  1.1 What the spec requires (AARM 27.4.1.3): the primary bootstrap runs in a
      VIRTUAL environment the console must build --
        Region 0  VA 0x10000000: HWRPB + callback routines, HWRPB at the base
        Region 1  VA 0x20000000: APB image + 3 pages (stack + guards)
        Region 2  VA 0x40000000: page-table space (L2 self-maps as an L3)
        Region 3  VPTB 0x2'00000000 (8 GiB, 8 KiB pages; L1[1] self-map)

  1.2 The GOOD run (NOIOVEC snapshot, cyc 2,027,331,327) satisfies ALL of it
      (tools/snap_ptwalk.py, PTBR 0x3ff04000):
        VA 0x20000000 -> L1[0]/L2[0x40]/L3[0] all V,KRE,KWE -> PA 0x5bc000,
                         first quadword 0x201f0001'd3800000 = the APB entry
        VA 0x20096000 -> PA 0x652000 (resolver pages mapped)
        VA 0x2009a000 -> PA 0x656000 [V,ASM] (bootstrap stack page)
        VA 0x10000000 -> PA 0x2000; first quadword 0x2000 = HWRPB self-PA
                         (AARM 27.4.2.1 validation rule 1)
        VA 0x101aa000 -> PA 0x1ac000 (CRB callback code; delta 0xfffe000)
        L1[1] self-map -> PFN 0x1ff82 = PTBR  (Region 3 wired)
      So when EmulatR last produced NOIOVEC, the L0 environment was CORRECT
      per spec.  L0 was open; the boot died at L1.

  1.3 The FAILING runs (both configs, 2026-07-25 evening):
        "jumping to bootstrap code / halted CPU 0 / halt code = 0 /
         PC = 20000000" -- identical for -flags 0 and -flags 0,1.
      AARM decode of that signature:
        - Reason-for-halt 0 = "Bootstrap, processor start, or powerfail
          restart" and is the value SET AT CONSOLE INITIALIZATION
          (Table 26-4 +280; Table 27-1).  Every ARCHITECTED halt writes a
          nonzero code (HALT instruction = 5, kstack = 2, mcheck = 7...).
        - Therefore the CPU re-entered console I/O mode through a path that
          never recorded a reason: an UN-ARCHITECTED return.  With BIP set
          and RC clear that is a FAILED BOOTSTRAP (27.4.3.6) and the console
          drops to the prompt -- exactly the transcript.
        - Operator observation (2026-07-25): "the bootstrap code bifurcated
          to a PC that initiated a system reset/init" -- i.e. execution DID
          begin and jumped into the reset flow.  Consistent with reason 0.
      Two mechanisms fit and are UNDISCRIMINATED as of this writing:
        H-A  the first fetch at VA 0x20000000 fails (image bytes not at PA
             base, or Region 1 PTEs broken in the NEW runs) and EmulatR's
             PAL/console glue records PC without a reason;
        H-B  APB's early code executes and branches into a reset/init path
             (the operator's observed bifurcation).
      Note H-A's earlier "zeros -> CALL_PAL 0" variant predicts halt code 5
      ("HALT instruction in kernel mode"), NOT 0 -- the transcript argues
      AGAINST the executed-HALT form of it.

  1.4 Variables between the last-good binary (07:45) and the failing ones:
        - chipset header edits (06:15 / 07:43): ALREADY IN the 07:45 binary
          by mtime -- exonerated (2-minute margin noted).
        - build config: CLOSED as a cause -- Release AND RelWithDebInfo now
          fail identically.
        - the JRN-SCSI-008 manifest edit (cypress_ide empty row removed):
          UNTESTED -- the PREEDIT A/B (EMULATR_PLATFORM_CONFIG=
          ds20_v7_3_platform.PREEDIT.json) is staged and pending.
        - anything else in the working tree between builds: unaudited.

  1.5 THE L0 DISCRIMINATORS (no code changes, one run each):
      (a) DIAG-PC over Region 1 during the failing boot:
            EMULATR_DIAG_PCLO=0x20000000 EMULATR_DIAG_PCHI=0x20099400
          Empty trace -> H-A (fetch never retired).  Non-empty -> H-B, and
          the last PCs name the bifurcation instruction.
      (b) A FAILING-RUN SNAPSHOT, no rebuild needed: run with
          EMULATR_CONSOLE_SNAPSHOT set, and AFTER the failed boot type the
          marker line at P00>>> (default "set oem_string snapshot",
          Uart16550.h RX marker-watch).  Then, on that snapshot:
            tools/find_bootstrap_image.py  (image at PA base? where else?)
            tools/snap_ptwalk.py           (Region 1 PTEs valid?)
            per-CPU slot at HWRPB+0x180: +0xF0 HALT PC, +0x118 REASON,
            +0x80 state flags (BIP/RC)   [offsets 240/280/128 decimal]
      (c) the PREEDIT manifest A/B (staged in JRN-SCSI-008 Sec 1).

--------------------------------------------------------------------------------
## 2. L1 -- APB's IOVEC bind (the historical NOIOVEC, fully mapped)

  What five journals established, in one paragraph: APB's device resolver is
  a bytecode VM whose walk over the console topology string retires a
  BYTE-IDENTICAL footprint for IDE and SCSI (752 PCs, empty diff;
  JRN-SCSI-004).  The resolver's MODE is argument 4 (r19, per the AARM
  calling standard r16-r21 = a0-a5; r7 is a volatile temporary and the old
  "R7 = mode" reading is dead), defaults to 1, and is 1 EVERYWHERE --
  including AXPBox's SUCCESSFUL boot (JRN-SCSI-006 Sec 8.1).  The env inputs
  it gates on are HEALTHY: post-boot booted_dev = "dka0.0.0.8.0",
  booted_osflags = "0" (operator capture 2026-07-25; gates 1 and 2 pass;
  JRN-SCSI-007's lost-ev_write branch refuted).  What separates accept from
  probe-exit is therefore DATA returned by / stored around the CRB callbacks
  -- and the callbacks have NEVER been in any instrument window (every
  capture began at 0x20095840, inside the resolver).

  THE L1 DISCRIMINATOR (once L0 reopens):
      EMULATR_DIAG_PCLO=0x101aa000 EMULATR_DIAG_PCHI=0x101ac000
  -- the CRB dispatch entry (VA 0x101aac60, tools/dump_crb.py) through which
  EVERY callback passes with the routine code in R16 (0x22 = get_env).  One
  run logs the full callback conversation APB has with the console; diff
  that conversation against AXPBox and the first divergent answer names the
  EmulatR-side gap.  (Optionally combine with the caller window
  0x20001600-0x20099400 from JRN-SCSI-006 Sec 8.4 if record volume allows.)

--------------------------------------------------------------------------------
## 3. L2 -- SYSBOOT: no evidence yet

  Nothing is known about L2 in EmulatR because no run has passed L1.  Plan
  nothing for it until the L1 conversation diff exists.

--------------------------------------------------------------------------------
## 4. Fidelity registry feeding these layers (secondary findings, dated)

  - HWRPB CC_FREQ = 100,000,000 (100 MHz) vs real DS20 ~500/667 MHz parts;
    banner MHz is wildly unstable across runs ("3 MHz" 07-17, "6 MHz" 07-25,
    "617 MHz" golden DS10) -- the console derives it from timing it performs
    against the cycle counter.  VMS calibrates timekeeping from CC_FREQ.
    Not implicated in L0/L1 today; WILL matter by L2/kernel.
  - IT_FREQ 0x400000 (4.19 MHz "interval timer") -- verify against spec
    (real systems ~1.2 KHz ticks derive differently); unaudited.
  - Console self-tests call missing commands ("memtest: No such command",
    "exer: No such command", 07-17 and 07-25 logs) and report
    "System Temperature is 0 degrees C" -- console-script/sensor fidelity
    gaps, benign for boot.
  - 2026-07-17 log (M1543C era): the ALi bridge enumerated BOTH dqa AND dqb
    -- the two-channel behaviour EXISTED in EmulatR; the Cypress 82C693
    model regressed it to one channel (JRN-SCSI-008 Sec 3 precedent).
  - sys_serial_num unset; SERIALNUM zeros in HWRPB.  Cosmetic today.

--------------------------------------------------------------------------------
## 5. Ordered runbook (supersedes the per-journal next-steps)

  R1  Failing boot + marker snapshot + Region-1 DIAG-PC (Sec 1.5 a+b in ONE
      run).  Decides H-A vs H-B and yields the failing-run post-mortem set.
  R2  PREEDIT manifest A/B (Sec 1.5 c) if R1 implicates configuration.
  R3  Once NOIOVEC is back: the CRB-window run (Sec 2) -- the callback
      conversation.  This is the run the whole L1 investigation has been
      missing.
  R4  AXPBox comparative: same CRB-window concept via its tracing, or at
      minimum its `show *` env dump, to diff the conversation.
  R5  Only after L1 opens: INQUIRY identity variation (JRN-SCSI-008 Sec 4),
      then dqb (Sec 5.1), per the sequencing argument.
