<!--
EmulatR V5 -- Session Journal JRN-VMB-007
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active hive (emulatrappuniv5)
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Per docs/notes/ADR-0001-source-file-headers.md (Markdown header as HTML comment).
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-007 -- VMB-phase DTB-miss: the 1-to-1 vs walk fork (open question) + vectoring probe spec

    Doc id      : JRN-VMB-007
    Status      : OPEN -- root cause localized to the VMB-phase DTB-miss
                  servicing; the A/B fork below is the decisive open question.
    Date        : 2026-07-19
    Model       : claude-opus-4-8 (Cowork), device bridge to tim-hpz640,
                  hive D:\EmulatR\emulatrappuniv5.
    Relates to  : JRN-VMB-001 (itbmiss frontier), JRN-VMB-006 (keyboard/VGA),
                  20260716_vector_dispatch_tb_region_design.
    Encoding    : ASCII-128.  Hex radix.

---

## 1. What landed this session (committed to the live hive)

  - Keyboard (8042) + VGA text-console interfaces implemented and VERIFIED
    (JRN-VMB-006): Pchip registers 0x03B0-0x03DF and 0x000B8000-0x000BFFFF;
    zero UNHANDLED OUTER writes at 0xB8000; IRQ1 defined and quiescent.
  - CSERVE 0x46 (IIC_WRITE) implemented in PalEntries.cpp (execCserve) --
    decodes R17 [7:0]=slave/[15:8]=word/[23:16]=data/[31:24]=data-flag and
    replays START + slave (+word/+data) + STOP against the emulated PCF8584
    at the resolved superpage PA 0x800FFF80000 (kBasePA 0x800.0000.0000 +
    the registerPciMemRange offset 0xFFF80000; S0 at +0, S1 at +1), sampling
    S1 (LRB) for R0.  Contract: ev6_vms_pc264_pal.mar sys__iic_write (:5208).
  - Instrumentation (all EMULATR_BRINGUP_PROBES-gated, env-selected):
      PTBR-DIAG   (PalEntries.cpp execSwpctxVms + execHwMtpr PAL_TEMP arm)
      IIC-DIAG    (PalEntries.cpp execCserve 0x46; EMULATR_IIC_DIAG)
      DTBMISS-DIAG(PipelineDriver.h fault block; EMULATR_DTBMISS_DIAG)

## 2. Threads RULED OUT this session (with the data)

  - PTBR write path is NOT the bug.  EV6 has no hardware PTBR IPR; the process
    PTBR is set only via SWPCTX (CpuState.ptbr, execSwpctxVms).  PTBR-DIAG:
    SWPCTX fired ZERO times before the loop.  So ptbr=0 is because the phase
    never reaches context setup, not a dropped MTPR.
  - VPTB / VA_CTL is NOT programmed in this phase.  The unconditional
    MEMDIAG-MTPR probe (PalEntries.cpp execHwMtpr) logged 256 MMU-ctl writes:
    63x DTB_TAG0 / 63x DTB_TAG1 / 62x DTB_PTE0 / 62x DTB_PTE1 + 2x ITB_TAG/PTE
    + 2x I_CTL, and ZERO HW_VA_CTL (0x01c4).  So the VMB runs on HAND-INSTALLED
    TB entries (at pc 0x600xxx), with NO page tables, NO VPTB, NO PTBR -- by
    design for this pre-handoff phase.
  - swppal is NOT on the path.  OpenVMS does not support PALcode switching
    (AARM Table 27-2 is the Tru64/Linux path); the machine boots directly on
    the VMS PAL (palBase 0x8000).  The OpenVMS handoff is the manual
    write_ipr(KSP/PTBR/VPTB) + HW_REI at boot.c:1467-1470 -- which we never
    reach (boot0 / VA 0x20000000 NOT-REACHED).
  - The 0x2_00xx_xxxx faulting VAs are a RED HERRING: they are the VA_FORM
    (VPTB) virtual-PTE-lookup addresses the DTB-miss handler computes with
    VA_CTL unset (va_ctl=0x2), i.e. the SECOND (double) miss, not the target.
  - CSERVE 0x46 (IIC_WRITE) is NOT the blocker.  Now implemented and routing-
    confirmed (0 UNHANDLED at 0xFFF80000; S1 readback 0x00 = device ACK, not a
    RAM echo of 0xC5), but the 18 calls occur at cyc ~183.6M -- INSIDE the loop,
    after the 182M handoff -- and the boot is unchanged.  The console ignores
    the CSERVE return (pc264_io.c:712), so it was a symptom in the loop.

## 3. Root cause localized -- the VMB-phase DTB miss

The decisive read is DTBMISS-DIAG: the FIRST-level (single) miss VA of each
loop iteration.  Every one is a LOW address in the VMB's own working set,
swept one 8 KB page at a time at VMB pc 0x600920 / 0x601240 / 0x6009a4:

    va = 0x5e0000, 0x5e2000, 0x5e4000, 0x5e6000 ... 0x604000 ... 0x616000
    va = 0x8000, 0xa000, 0xc000, 0xe000, 0x10000 ... 0x18000
    (plus 0x5fffd0, 0x5f0004, 0x5f2008, 0x600230, 0x602270)

None are 0x2_00xx_xxxx.  So the machine faults on the VMB's real pages, not on
anything exotic.  Causal chain:

    VMB touches page (e.g. 0x5e4000)  -> DTB miss (single)
      -> PAL DTB-miss handler runs the VA_FORM/VPTB virtual-PTE lookup
      -> VA_CTL/VPTB unset -> the PTE virtual address is garbage (0x2_00xx_xxxx)
      -> that PTE load itself misses -> DTB miss DOUBLE (pc 0x8321)
      -> cannot fill the entry -> VMB retries the same access -> LOOP.

Stop: MaxCyclesExceeded at PC 0x1ad770, fault=5 kFaultDtbMiss; handoff
(0x1ade60) re-entered 36,921x; boot0 NOT-REACHED.

The VMS PAL DOES contain hand-fill TB routines (ev6_vms_pc264_pal.mar:2296,
2592, 3499: hw_mtpr DTB_TAG0/1 + DTB_PTE0/1 after a PTE load), and the VMB
hand-installs entries early (Sec 2).  So the machinery to fill exists; the
question is why THESE misses do not take a fill/1-to-1 path and instead fall
into the VPTB walk that cannot resolve.

## 4. THE OPEN QUESTION -- the A/B fork

"Why isn't the 1-to-1 (or hand-fill) path taken for these VMB misses?" has two
structurally different answers, in different code:

  (A) DATA-FIDELITY.  The PAL reaches the walk-vs-1-to-1 gate and evaluates it
      false.  The per-CPU / PAL state the branch reads (p_misc-class -- the
      1-to-1 predicate; cf. the sign-bit test at ev6_vms_pc264_pal.mar:3399
      "skip walk for 1to1", and the same register's console-mode use at :998,
      :1031) is not holding the value the VMB expects for this phase.  The
      firmware's own predicate says "walk," and it walks correctly given a bad
      input.  Fix is in whatever SEEDS that state before this phase -- an
      IPR/PAL-shadow the emulator is not setting.  This is the _PROVISIONAL-IPR
      hazard class in the house rules: a value fine for storage but wrong at
      the decode that consumes it.

  (B) VECTORING.  The PAL never reaches that gate -- the emulator vectors the
      miss to a PA that bypasses it.  The DTB-miss entry PA the translator /
      PAL-dispatch computes is not the address whose handler contains the gate,
      or the entry offset lands mid-handler past the gate.  EV6 places entries
      at fixed PAL_BASE offsets (the .mar uses ". = ^xNNNN"; e.g. :513
      ". = ^x0D00").  Fix is in the miss-entry PA computation (the emulator
      fault->entry-offset map + PAL_BASE), not in any predicate.

These are mutually exclusive and the fix lives in a different file for each:
(A) = the IPR/shadow seed path; (B) = the fault-entry vectoring.

## 5. NEXT READ -- the triple-trace

Arm the existing decoded retire-window on the DTB double miss and read the
handler executing, so we watch the three stages of ONE miss end to end:

    (1) fault delivery + the entry PA the emulator jumps to,
    (2) the handler running up to the walk-vs-1-to-1 gate + the branch taken,
    (3) the walk (VA_FORM -> garbage PTE VA -> second miss) OR the fill.

Mechanism (already in-tree, PipelineDriver.h ~:1261, no new code):

    EMULATR_TRACE_ARM_ON_DTBM=<instrs>   # one-shot forward window on 1st DTBM
                                         # double; dumps PAL DTBM walk + retry.

Run bounded (a few hundred instrs is enough to span entry..gate..walk across
the miss + its double + the retry -- the "triple").  Reading it decides the
fork directly: if the trace shows execution reaching the gate and branching to
the walk, it is (A); if it never reaches the gate address, it is (B).

## 6. SPEC -- the DTB-miss vectoring probe (to build)

A purpose-built, EMULATR_BRINGUP_PROBES-gated, env-selected
(EMULATR_DTBVEC_DIAG) probe at the DTB-miss delivery in PipelineDriver.h,
alongside the existing DTBMISS-DIAG.  For each single kFaultDtbMiss it logs
FOUR fields, capped:

  1. ENTRY PA.  The PA the emulator vectors the miss to = cpu.palBase +
     entryOffset(faultCode) (the fault->entry map; entryForFault /
     Ev6EntryVectors.h).  Decides (B): compare against the architected EV6
     DTB-miss single/double entry offset that begins the handler holding the
     gate.  Equal => gate is reachable (not B); unequal or mid-block => (B).

  2. p_misc AT THE GATE.  The value of the per-CPU/PAL-shadow register the
     1-to-1 predicate reads (resolve the exact register from the .mar defs:
     the "p_misc" alias -- a PAL scratch/shadow; see :3399/:998/:1031 for its
     sign-bit semantics).  Log it at vectoring time.  Decides (A): is the bit
     that selects 1-to-1 vs walk holding the phase-correct value?

  3. BRANCH DIRECTION.  The direction the gate would take given (2): "1to1"
     (sign set -> skip walk) vs "walk" (VA_FORM/VPTB).  Derived from the
     p_misc sign bit; log it decoded so the log reads the decision directly.

  4. PER-VA "DTB_TAG EVER INSTALLED".  For the faulting VA, had the VMB
     previously hand-installed a DTB_TAG covering it (HW_MTPR DTB_TAG0/1)?
     Maintain a small set/ring of installed tags, populated in execHwMtpr's
     HW_DTB_TAG0 / HW_DTB_TAG1 case (PalEntries.cpp), and test membership of
     (va >> pageShift) here.  Distinguishes RESIDENT-THEN-EVICTED (an emulator
     DTB capacity/eviction mismatch -- real HW kept it, we dropped it) from
     NEVER-INSTALLED (the VMB relies on walk / 1-to-1 for it, so the gate/seed
     is the issue).

Output shape (one line per capped miss):

    DTBVEC-DIAG cyc=.. va=0x.. entryPA=0x.. p_misc=0x.. branch=<1to1|walk>
                tagInstalled=<0|1>

Implementation dependencies to resolve at build time:
  - p_misc register assignment: find "p_misc = p<N>" in the apisrm pal defs
    (ev6_pc264_pal_defs / the p-register aliases) and map p<N> to the emulator
    PAL-shadow field (CpuState).  If p_misc is a packed register (it carries
    MCES in byte 2 -- :1503/:1515/:1605), mask to the 1-to-1 field/sign bit.
  - entryOffset(faultCode): confirm the emulator's DTB-single vs DTB-double
    entry offsets against the .mar ". = ^xNNNN" layout and the architected EV6
    PAL entry points; that comparison IS the (B) test.

Together the triple-trace (Sec 5) and this probe resolve A vs B in one run:
the trace shows whether the gate is reached; the probe quantifies the entry PA,
the predicate value, and whether each missing page was ever resident.

## 7. Evidence / artifacts

  - out/build/relwithdebinfo/logs/kbd_vga_boot_ds20_20260718_174321.log
      IIC-DIAG (18x, slave 0x4e, s1=0x00, r0=0, cyc ~183.6M);
      DTBMISS-DIAG (VMB working-set VAs, Sec 3);
      CKPT_SUMMARY (handoff REACHED 36,921x; boot0 NOT-REACHED);
      Stop reason MaxCyclesExceeded PC=0x1ad770 fault=5.
  - Prior run 20260718_171413.log: MEMDIAG-MTPR (0 VA_CTL; hand-filled DTB/ITB).

## 8. Source citations

  V5 hive (D:\EmulatR\emulatrappuniv5):
    palBoxLib/grains/PalEntries.cpp   execCserve 0x46 (IIC_WRITE); execSwpctxVms;
                                      execHwMtpr PAL_TEMP + MMU-ctl probe
    pipelineLib/PipelineDriver.h      fault block; DTBMISS-DIAG; entryForFault;
                                      EMULATR_TRACE_ARM_ON_DTBM (~:1261)
    coreLib/HW_IPR.h                  HW_DTB_TAG0/1, HW_VA_CTL, PAL_TEMP indices
    mmuLib/Ev6Translator.h            tryKsegTranslate (SPE), translateData
    chipsetLib/TsunamiPchip.h         kBasePA=0x800.0000.0000; registerPciMemRange
    (to build) DTBVEC-DIAG probe      PipelineDriver.h + execHwMtpr tag set

  Authoritative (Processor Support; read-only):
    apisrm/ref/ev6_vms_pc264_pal.mar  sys__iic_write (:5208); TB-fill routines
                                      (:2296,:2592,:3499); p_misc gate (:3399),
                                      console-mode use (:998,:1031); ". = ^xNNNN"
                                      entry layout (:513)
    apisrm/ref/iic_driver.c / srmconsole/PC264_IO.C:1339  iic_write_csr
    PalcodeBitsavers/srmconsole/5.8/SRC/PC264_IO.C:712    cserve(IIC_WRITE,...)
    alpha_arch_ref.txt 27.4.1.3 (Regions 0-3), Table 27-2 (swppal, Tru64/Linux)

## 9. Standing rules in force

  ASCII-128 only; include guards not #pragma once; hex dispatch labels; doctest
  CHECK only; surgical Edit over whole-file rewrites; V0/V1/V2, Processor
  Support, axpbox read-only; logging in CMake compile guards
  (EMULATR_BRINGUP_PROBES); provisional IPR values marked _PROVISIONAL and
  HRM-verified before any HW_MFPR/MTPR decode binds them; multi-GB traces
  bounded-window only (the triple-trace is a bounded EMULATR_TRACE_ARM_ON_DTBM
  window, not a whole-run capture); discuss before code.
