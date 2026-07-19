<!--
EmulatR V4/V5 -- Session Journal JRN-VMB-013
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-013 -- The next wall after the DTB fix: VMB/PAL boot-transfer never reaches 0x20000000.  BOOTTRACE tooling + leads.

    Doc id      : JRN-VMB-013
    Status      : OPEN.  DTB-thrash CLEARED (JRN-VMB-012).  Next wall localized
                  to the VMB/PALcode boot handoff: the transfer to the system-
                  software entry 0x20000000 never executes; the VMB reports
                  "halted CPU 0 / halt code 0 / PC = 20000000" and re-prompts.
                  This is a PALcode/firmware issue, NOT translation and NOT the
                  console-output loop (that is normal).  Needs a focused session.
    Date        : 2026-07-19
    Model       : claude-opus-4-8, macOS native build, console driven over socket.
    Relates to  : JRN-VMB-012 (DTB fix), JRN-VMB-005/006 (UART/IRQ storm).
    Encoding    : ASCII-128.  Hex radix.

---

## 1. What is confirmed

  - boot dqa0 loads the VMB (1226 blocks, base=5bc000), reaches the handoff, and
    the CPU never executes at 0x20000000: ITBPROBE (keyed 0x20000000) fires 0x,
    boot0 checkpoint never hits.  The "halted CPU 0 / PC=20000000" is GUEST SRM
    output (a transfer-failure diagnostic), NOT an emulator kFaultHalt.
  - The console-output loop I first chased (0x1ade60 STB->COM1 THR 0x3f8;
    0x1addc0 LDL<-COM1 LSR/MSR/IIR 0x3fd/0x3fe/0x3fa) is NORMAL serial output of
    the boot messages -- ~62 chars, matches the visible text.  NOT the bug.
    (Pre-DTB-fix it re-ran 36,921x because the thrash re-drove it; post-fix it is
    normal.)

## 2. The actual failure region (BOOTTRACE, PAL mode)

Captured with the new BOOTTRACE probe (Sec 4).  In the window from the bootstrap
marker to the halt, the PAL layer runs REPEATING handlers:

  - 0xd280-0xd2c0: loads immediates (R5=0xff01, R4=0x130000ff01, ...), issues a
    burst of HW_MTPR (enc 0x77e6xxxx / 0x77e4xxxx / 0x77ffxxxx -- opcode 0x1d),
    then HW_REI at 0xd2c0 (enc 0x7bf78000).  This routine repeats 5+ times in the
    tail -- a CALL_PAL handler re-invoked.
  - 0x8318-0x8324: HW_LD (physical, opcode 0x1b, enc 0x6c845000) from PA 0x98 ->
    R4 = 0.  A physical load of low memory returning ZERO.  Candidate: PA 0x98
    should hold a non-zero pointer/flag the PAL consumes.
  - 0x117c0-0x117d4 (PAL): a bit-scan -- R4 = 1<<R20 doubling (0x10000000 ->
    0x20000000 -> 0x40000000 -> ... up past bit 45), R20 = 0x1c,0x1d,0x1e...,
    testing R6 = R5 & R4 (AND, enc 0x44a40006) and branching.  Scans mask R5 for
    a set bit.  NOTE: the "0x20000000" that shows here is just 1<<29 mid-scan, a
    RED HERRING -- not the transfer target.

All pal transitions present (1502 pal=1 lines, 204 CALL_PAL/HW_REI in a 9000-line
window).  No fault (fault=0 throughout).  So the CPU is not faulting -- the PAL/
VMB logic decides not to (or cannot) transfer to 0x20000000.

## 3. Leads for the focused session (ranked)

  L1  HW_LD from PA 0x98 = 0 (pc 0x8320).  Trace what writes (or should write)
      PA 0x98 earlier in boot; if the PAL expects a non-zero value there (a
      restart block field, a HWRPB pointer, a per-CPU slot), a 0 could steer the
      transfer decision.  Cross-check reference boot.c / the HWRPB/HWPCB layout.
  L2  The bit-scan on R5 (pc 0x117c0).  Capture R5's value (the mask being
      scanned) -- if it lacks the expected set bit (e.g. a memory-size or
      address-space-width encoding the emulator supplies wrong), the scan yields
      a wrong shift/index that mis-drives the transfer.
  L3  The repeating 0xd280 PAL handler (HW_MTPR burst + HW_REI).  Identify which
      IPRs it writes and which CALL_PAL re-invokes it; a handler that keeps
      re-entering without progress is the "halt code 0" precursor.
  L4  Where the guest decides to print "halted CPU 0 / PC=20000000" -- set an
      EMULATR_DIAG PC window on the SRM console halt-report routine and read the
      GPRs at that point (which check failed, what HALT_PC/halt_code it read from
      the HWPCB).

## 4. Tooling added this session (KEEP -- env-gated, zero-cost off)

  pipelineLib/PipelineDriver.h (retire() diagnostic block):
    BOOTTRACE -- logs every retired instruction (cyc/pc/enc/pal/fault/memAddr and
    the destination reg write =>R/F<idx>=<val>) once the UART bootstrap marker
    latches BreakpointSink::s_forceOpen (set via EMULATR_TRACE_ON_BOOTSTRAP).
    Gated on env EMULATR_BOOTTRACE; capped at EMULATR_DIAG_CAP.  Added
    #include "traceLib/BreakpointSink.h".  IMPORTANT: gate is s_forceOpen (the
    VMB->bootstrap marker), NOT BreakpointSink::armed() -- armed() is true from
    cyc 0 via the default paired-PC gate and would capture SROM init instead.

  Recipe (macOS native, console over socket): drive `\r`->LFU->`exit`->auto
  memtest->P00>>>->`boot dqa0`; env:
    EMULATR_TRACE_ON_BOOTSTRAP=1 EMULATR_BOOTTRACE=1 EMULATR_DIAG_CAP=<n>
    (optionally EMULATR_DIAG_PCLO/PCHI/CYCLO/CYCHI for a narrower window)

## 5. Recommendation

Pick up with L1 (PA 0x98 provenance) + L4 (the halt-report GPRs) -- together they
should reveal the exact failed check.  This is PALcode/firmware reverse
engineering; budget a focused session, not a grep.  The DTB fix (JRN-VMB-012)
stands as the shipped win; this wall is a separate, deeper defect.

## 6. Standing rules

  ASCII-128; hex; surgical Edit; probes in EMULATR_BRINGUP_PROBES / env guards;
  discuss before code (P-0).
