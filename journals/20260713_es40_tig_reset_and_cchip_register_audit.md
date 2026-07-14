<!--
EmulatR V4 -- ES40 "Initializing...." hang root cause (unmodeled TIG reset),
Cchip CSR address-map audit vs the Tsunami/Typhoon HRM, and the micro-delay
warp.  Context-preservation journal.  ASCII(128) only; hex radix.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
-->

# ES40 TIG-reset hang + Cchip register audit (2026-07-13)

## 0. One-paragraph summary

The ES40 silicon-mode "Initializing...." hang (seen after an LFU `exit`, and on
the console_restart path generally) is an UNMODELED TIG-BUS SYSTEM RESET, not a
timing, warp, interrupt-corruption, or IDE problem -- all of those were
eliminated with instrumentation. The firmware issues the hardware reset via the
RMC/TIG bus, then spins at a deliberate `BR .` (0x12c270) expecting the box to
reboot; EmulatR treats the TIG writes as passive/no-op, so nothing resets and it
dead-loops. Separately, an HRM cross-check (prompted by Tim) found the entire
Cchip CSR address map at and above offset 0x0500 is MISASSIGNED in EmulatR. This
journal records both, the reset design, and the open trigger-bit question.

## 1. The hang -- what was ELIMINATED (instrumented, not guessed)

Probes landed behind EMULATR_RSCC_DIAG / EMULATR_FLASH_TRACE / EMULATR_TIG_TRACE
and a new EMULATR_UDELAYWARP (all gated, inert by default):

- WARP: refuted. warpTot=0 on every RSCCDIAG-DELAY row; the no-warp run hangs
  identically. (Consistent: the cycle-time calibration runs before the IDLEWARP
  window ever fires.)
- INTERRUPT REGISTER CORRUPTION: refuted. The exact-pair DIVERT-REI ledger (LIFO,
  full 32-reg snapshot, sdeDiv/sdeRei) showed the "mismatches" are all LEGITIMATE
  CONTEXT SWITCHES -- 100% of the flagged ids also change R30(sp)+R29(gp)
  together (the scheduler switching processes at a shared wait PC). Within one
  process context, registers are conserved. SDE/shadow machinery is clean.
- THE DELAY PRIMITIVE (0x6a4f8-0x6a520): works. Prologue computes deadline
  r3 = start_RSCC + N once; loop exits when current(r4) > deadline. iter0 has a
  fixed small deadline it reaches and exits.
- "5 MHz" reading: cosmetic. deviceLib/BadgeMhzGauge.h effMhzNow() = cycleCount /
  wall_seconds / 1e6 (emulation throughput), rewritten into the banner bytes;
  NEVER written to guest state. Not a functional calibration. The functional
  time base is HWRPB cycle_count_freq (Hwrpb.h:453) / ToyRtc kDefaultCyclesPerSec.

## 2. The hang -- ROOT CAUSE (static disasm of decompressed_es40_v7_3.bin)

LFU `exit` -> lfu_system_reset (LFU.C:722) -> PC264 else branch -> "Initializing
...." -> krn$_sleep(2000) -> console_restart. The image's reset routine at
0x12c1xx-0x12c26c issues a SEQUENCE of outtig() writes then `BR .`:

  0x6b850 = xtig  : PA = (0x801<<32) | (offset<<6)   (TIG bus 0x801_3000_0000)
  0x6b8e0 = outtig wrapper
  0x12c270: c3ffffff = BR r31,0x12c270   (deliberate branch-to-self dead-loop)

Full write sequence (offset -> TIG PA, data), in order:
  0xC00020..23 -> +0x800..+0x8C0   data 0
  0xC00028..2C -> +0xA00..+0xB00   data 0   (IPCR/int-ctl region, zeroed)
  0xC0001C/1D  -> +0x700/+0x740    data 0
  0xC00019     -> +0x640           data 0
  0xC0000C     -> +0x300           data 0
  0xC0000A     -> +0x280           data 0x4   <-- ARM   (non-zero)
  0xC0000F     -> +0x3C0 (kHaltCpu0) data 0   (CLEARED, not WHAMI-set)
  0xC00018     -> +0x600           data 0x30  <-- COMMIT (non-zero, last)
  then BR .

A minimal sibling reset at 0x8c94c does only the triad {+0x280=0x4, +0x3C0=0,
+0x600=0x30, BR .}. Two independent routines converging on that triad => it is
the reset primitive. EmulatR's TsunamiTig::write (chipsetLib/TsunamiTig.h:135)
no-ops these (kHaltCpu0 passive store; +0x280/+0x600 fall to missDefault), so the
reset never happens and the CPU dead-loops (or, when the console_restart delay is
a big 0x6a4f8 micro_delay, spins there first at ra=0x6a50c). LIVE CONFIRM
2026-07-13: at the hang, PCSAMPLE ra sat in xtig/outtig (0x6b890/0x6b8cc/0x6b900)
and the reset routine (0x12cxxx) -- the console is issuing the reset writes right
now and waiting for a reset EmulatR never delivers.

## 3. CONFIRM gates (hybrid: web-variant adjudicates, Cowork disassembles)

- CONFIRM-1 SEMANTICS = RESET (not halt), sourced. sys__int_hlt
  (EV6_VMS_PC264_PAL.MAR:902-908) arms a software halt by SETTING 1<<WHAMI in TIG
  0x3C0; the reset routine writes 0x3C0=0 (clears). Reset vector = palBase+0
  (0x8000) = `BR r26,0x13e80` (sys__reset trampoline).
- CONFIRM-2 DO-NO-HARM, CLOSED STRUCTURALLY. Decompressed DS10 AND DS20 images
  have ZERO hits on the arm (0xC0000A / LDA r17,10) and ZERO on the whole
  TIG-write idiom (LDAH r17,192 = 0x263f00c0). They soft-re-init in software and
  never touch the TIG halt/reset regs. The write-handler change is ES40-EXCLUSIVE
  by absence. Regression gate still mandatory: suite + DS10 + DS20 -> P00.
- CONFIRM-3 hazards:
  1. resetToLoadedEntry (Machine.cpp:1051) re-inits m_cpu ONLY. The chipset init
     is in the ctor path (~Machine.cpp:481). A faithful reboot must re-init the
     chipset CSRs to their HRM reset values (see sec 4).
  2. LFU re-entry: CONFIRMED REAL. Even a restored/u-srm'd flash auto-enters the
     "Checking option firmware -> standard console update" (LFU) flow on PC264
     powerup and NEVER reaches P00 (auto_action defaults to HALT, so this is NOT
     auto_action). Trigger is NOT flash content, NOT the TURBO LFU0 memory scan
     (KERNEL.C:1547 is #if TURBO, and 'LFU0' is absent from the image). Trigger
     STILL UNPINNED -- likely boot_osflags / an NVRAM/entry-process condition. So
     a modeled reset would reboot -> re-auto-LFU -> loop, unless this is pinned.
     THIS is the common blocker for BOTH goals (reaching P00 for show dev, and
     escaping the reset loop).

## 4. Reset ACTION -- HRM 12.1.1 (authoritative)

Tsunami/Typhoon HRM Chapter 12.1.1 (tsunami_typhoon_21272_hrm.txt:20728):
system reset is driven by the module reset pin b_modrst_l (external, from the
motherboard/RMC), NOT a Tsunami CSR the CPU writes. The firmware's TIG writes are
RMC commands that assert module reset. On reset: the Cchip holds clock-forward
resets and tristates the TIGbus; at the DEASSERTING edge all chips latch config
from the TIGbus pull-ups into CSRs (Table 12-1: SysDC delays, CPU CFP presets,
base config), and "the other CSR bits are initialized as indicated in the tables
in Chapter 10." Then CPU re-runs BiSt -> SROM load -> firmware from flash.

=> Faithful EmulatR reset = re-init chipset CSRs to their Chapter-10 reset values
   + resetToLoadedEntry() (re-enter the SROM/decompressor). Both pieces exist;
   they need wiring to the trigger. The Tsunami HRM gives the ACTION; it does NOT
   give the exact RMC/TIG trigger register+bit (that is RMC/motherboard, in the
   ES40 Service Guide App C/D/E -- see sec 6, OPEN).

## 5. Cchip CSR address-map AUDIT (vs HRM Chapter 10) -- CLEAN (correction)

RESULT: the LIVE Cchip register map is CORRECT and matches the HRM. No defect.

The authoritative offsets live in chipsetLib/Tsunami21272_RegisterMap.h
(namespace Tsunami21272::Spec::Cchip), which TsunamiCchip.h:201 includes and the
read/write switch resolves via `using namespace Tsunami21272`. Verified against
HRM Chapter 10:
  TTR 0x0580, TDR 0x05C0, DIM2 0x0600, DIM3 0x0640, DIR2 0x0680, DIR3 0x06C0,
  IIC2 0x0700, IIC3 0x0740, PWR 0x0780 -- all CORRECT (RSVD_14 = 0x0500 marked
  Reserved, matching the HRM). m_ttr/m_tdr storage + reset values already present
  (TsunamiCchip.h:423-424, HRM 10.2.2.14/15).

CORRECTION OF A 2026-07-13 MIS-AUDIT: an earlier pass in this session reported
the map as broken (DIM2=0x0500, etc.). That reading was against a COMMENTED-OUT
dead constant block in TsunamiCchip.h (lines ~215-247, inside /* ... */) that is
NOT compiled and does NOT drive the decode. The dead block carries the stale
wrong values (kDIM2=0x0500 ...) and is the sole reason for the false alarm. It
should be DELETED to prevent future mis-audits. That is the only Cchip change
warranted here; the functional decode needs no correction.

Variant note (faithfulness nicety, not a bug): the Typhoon-only registers
(DIM2/3, DIR2/3, IIC2/3, PWR, CMONCTLA/B) are decoded unconditionally. On Tsunami
(DS10/DS20) those offsets are reserved, but DS10/DS20 firmware never accesses
them (1-2 CPUs; CPU2/3 regs unused), so it is inert. Variant-gating them
(reserved on Tsunami) would be strictly more faithful but is low priority and NOT
tied to the hang.

CMONCTLA/B (0x0C00/0x0C40) and PWR (0x0780) presence in the decode: to be
confirmed separately, but they are Typhoon-only monitor/power regs, not in the
hang path.

Net: the register map is NOT the issue. Focus returns to the reset (sec 4/6) and
the auto-LFU (sec 3 hazard 2).

## 6. OPEN -- the reset TRIGGER bit (needs the Service Guide / doc-hive audit)

The exact RMC/TIG register+bit that commands module reset is NOT in the Tsunami
HRM (reset is external b_modrst_l). Candidate authorities to audit in
/Processor Support: the SRMConsole doc hive, the ES40 Service Guide RMC/DPR
appendix (es240sva App C/D/E), the ALi M1543C datasheet, RMC firmware docs. The
empirical trigger is the co-gated triad (ARM +0x280=0x4 then COMMIT +0x600=0x30);
per web-variant condition #3 the final decode must be on the reset-commit BIT
within 0x30 (bits 4,5 -- identify which), not the literal value. Until confirmed,
the prototype decodes on the literal co-gated triad behind EMULATR_BRINGUP_PROBES
(_PROVISIONAL). Isolating the exact bit may take a few instrumented executions
with EMULATR_TIG_TRACE (extended to log the data value) watching what the console
polls/expects after the writes.

## 7. UDELAYWARP -- LANDED 2026-07-13 (boot-speed only)

pipelineLib/PipelineDriver.h: sibling to the 0x7c314 TICKWARP. At PC 0x6a514
(EMULATR_UDELAYWARP), reads current RSCC (r4) + deadline (r3); if r3-r4 >
threshold (2^16), advances cycleCount to the deadline (+ warpCycles), no memory
write (avoids the quarantined RSCCWARP 0x3c970 rewrite). Self-limiting +
thresholded. VERIFIED firing: `UDELAYWARP cyc=...->...  delta=356974` (constant
fixed-N boot delays collapsed). Correctly does NOT fire inside the reset spin
(those delays are sub-threshold). Boot-speed win; does NOT fix the reset hang or
the auto-LFU.

## 8. Next steps (order)

1. THIS JOURNAL (done -- context preserved before edits).
2. Audit the /Processor Support doc hive for the RMC/TIG reset trigger register
   (sec 6).
3. Land the Cchip CSR map correction (sec 5), variant-gated (Typhoon-only regs
   gated; TTR/TDR both-variant); regression gate suite + DS10 + DS20 -> P00.
4. Draft + land the HRM-faithful ES40 reset PROTOTYPE (sec 4): trigger = co-gated
   triad (flag-gated _PROVISIONAL) -> action = Chapter-10 CSR re-init +
   resetToLoadedEntry.  Its first job is diagnostic: does the reboot reach P00 or
   loop on the auto-LFU (CONFIRM-3 hazard 2)?
5. Pin the auto-LFU trigger (CONFIRM-3 hazard 2) -- the common blocker for both
   the dqa0/dqb0 show-dev goal and escaping the reset loop.

## 9. Reset prototype -- LANDED + FIRST RUN (2026-07-13, run 211929)

LANDED (gated EMULATR_BRINGUP_PROBES + EMULATR_TIG_RESET, inert unless armed):
- chipsetLib/TsunamiTig.h: co-gated triad detection in write() (kResetArm
  0x80130000280 bit2, kResetCommit 0x80130000600 bits4/5); m_resetArmed/
  m_resetRequested + resetRequested()/clearResetRequest(); reset() clears them.
- chipsetLib/TsunamiChipset.h: tigResetRequested()/clearTigResetRequest().
- systemLib/Machine.cpp systemTick(): polls + applies module reset
  (m_chipset.reset() + resetToLoadedEntry()) at a clean per-tick boundary.
- chipsetLib/TsunamiCchip.h: deleted the dead kDIM2=0x0500 block (sec 5).
- pipelineLib/PipelineDriver.h: EMULATR_UDELAYWARP (sec 7, prior).

FIRST RUN RESULT: the reset did NOT fire, because the console never reaches the
triad WRITE -- it is stuck EARLIER. New blocker found: the reset/shutdown path
REPEATEDLY READS TIG ctrl +0x100 (pa=0x80130000100 = intig(0xC00004)); EmulatR
returns 0 (unmodeled -> missDefault), so the poll never satisfies and it loops
(last-300 PCSAMPLE ra dominated by 0x6a50c delay + 0x6b890/0x6b900 xtig/outtig +
0x85xxx). So there is a HANDSHAKE before the triad: the console polls TIG ctrl
+0x100 for a status (RMC-ready / reset-ack) and waits. UDELAYWARP does NOT help
(status poll, not a timed delay). Run knobs were IDLEWARP=1 + TIG_TRACE=1;
UDELAYWARP was NOT armed (0 fires); TIG_RESET triad-seen absent (never reached).

NEXT (the pre-triad handshake): pin what TIG ctrl +0x100 (intig 0xC00004) should
return so the reset-path poll completes -> reaches the triad -> our reset fires.
Options: disassemble the +0x100-reading routine for the compare it waits on, or
add caller-PC + expected-value to the TIG read trace. Same shape as the modeled
DPR/RMC handshakes ([[dpr-rmc-hrm-source]]: 0xDA=0xAA tig-load, 0xD9 i2c-done).
Candidate sources: es240sva App C/D/E TIG/RMC map, ES45/Titan manuals.

Related memory: [[lfu-reset-silicon-reinit-and-flash-recovery]],
[[es40-ide-enum-gap]], [[es40-srm-boot-status]], [[dpr-rmc-hrm-source]],
[[m1543c-southbridge-hrm]].
