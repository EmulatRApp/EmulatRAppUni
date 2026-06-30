<!--
EmulatR V4 -- DS20 "AlphaPC 264DP" badge: trace-proven root cause + fix
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-29.  ASCII(128) only.
-->

# DS20 -> "AlphaPC 264DP" Badge: IIC Completion-Interrupt Root Cause + Fix

**Date:** 2026-06-29
**Status:** *** SUPERSEDED -- INTERRUPT ROOT CAUSE DISPROVEN (2026-06-29, see Sec. 8). ***
The interrupt-completion theory below was falsified by direct measurement: the DS20
IIC driver never sets ENI (0x08) -- it runs POLLED. The IIC IRQ fix (IicPcf8584
interruptPending + TsunamiChipset EMULATR_IIC_IRQ_BIT) is therefore NOT the repair;
it is left in place but DEFAULT-OFF and inert. Read Sec. 8 first; Sec. 1-7 are the
(now-corrected) investigation trail, retained for the deterministic facts they pin.

---

## 1. Symptom

DS20 cold boot badges itself "AlphaPC 264DP" (SYSTYPE member 1) instead of
"AlphaServer DS20" (member 6). HWRPB scan: SYSTYPE=0x22 (DEC_TSUNAMI),
SYSVAR=0x405 -> member = (SYSVAR>>10)&0x3F = 1.

## 2. How it was proven (EmulatR as tool of record)

Two retire-trace (.trc) captures via the GuestMemory + IIC instrumentation
(see 20260629_guestmemory_diag_instrumentation.md). Trace hooks require
`-DEMULATR_TRACE_HOOKS=ON` (PipelineDriver per-commit callback is compiled out
by default).

Capture A -- armed on the HWRPB base store (PA 0x2000), 8M-instr window:
  - get_sysvar (fn @0x7f5c0, BSR @0x5c3e4) runs ~14k instrs, returns member 1;
    build_hwrpb OR's member<<10 (0x400) into base SYSVAR 0x5 -> stores 0x405.
  - The whole get_sysvar window does a filesystem/device-table walk with
    ZERO IIC bus traffic -> fopen("iic_ocp0") fails at TABLE LOOKUP, never
    reaching an open/probe. The device was never registered.

Capture B -- armed on the first IIC node-0x40 START (EMULATR_TRACE_ARM_ON_IIC=0x40),
4M-instr window (cyc ~184.6M-188.6M):
  - Entry = the IIC controller write (STB pc=0x1ade60, PA 0x801fc000080 / 0x800fff80001).
  - Window is dominated by a spin-wait + timeout countdown: hot loop
    0x6206c LDL / 0x62070 XOR / 0x62074 BEQ (poll a DRAM flag) paired with
    0x620d0 SUBL / 0x620d8 BGE (decrementing retry), while clock-interrupt PAL
    vectors (0xa3c1, 0xa4c1, 0xec91) fire ~20k times underneath.
  - The poll reads DRAM 0x3c4f0-0x3c500 (a self-referential / empty queue head),
    NOT the controller register: only ~3 controller MMIO touches in the first
    60k instrs. => the driver is INTERRUPT-DRIVEN, waiting on an ISR-posted
    semaphore, not polling the controller.

## 3. The causal chain (firmware SSOT + trace, both read-only confirmed)

apisrm/ref/iic_driver.c, iic_rw_common:
```
pb->isr_complete = 0;  pb->misr_t.sem.count = 0;
iic_write_csr((IIC_START | pb->int_flag), IIC_STATUS, fp);  // int_flag = IIC_ENI (0x08)
krn$_start_timer(&pb->misr_t, IIC_MAS_TIMEOUT);             // 2 s
status = krn$_wait(&pb->misr_t.sem);                        // wait for ISR
...
case TIMEOUT$K_SEM_VAL:
    if (pb->isr_complete == 1) break;                       // not a valid timeout
    iic_write_csr((IIC_STOP | pb->int_flag), ...);
    pb->rec_count = 0;                                      // <-- ERROR: clear count
...
status = pb->rec_count;  return(status);                   // iic_init checks this
```
ISR `iic_service` is bound to SCB vectors IIC_VECTOR1=0xa9 / IIC_VECTOR2=0xaa
(iic_def.h). On the IIC completion interrupt it reads the byte, posts misr_t.sem,
sets isr_complete=1.

EmulatR's IicPcf8584 performed the byte transfer synchronously (set S1<PIN>=0)
but NEVER raised the completion interrupt: kCtl_ENI (0x08) was commented
"interrupt enable (stored)" -- stored and ignored; no IRQ asserted anywhere.

Therefore: ISR never runs -> misr_t.sem never posted -> krn$_wait hits
TIMEOUT$K_SEM_VAL -> rec_count=0 -> iic_init `if (status != 1) continue;` skips
registering the node as iic_ocp0 -> get_sysvar's fopen("iic_ocp0") misses ->
member 1 -> SYSVAR 0x405 -> "AlphaPC 264DP".

This is an INCOMPLETE INTERFACE (missing completion interrupt), not a symptom to
patch. Uncompromised faithfulness.

## 4. Interrupt routing (21272 HRM + pc264_io.c)

- 21272 HRM "Device and Error Interrupt Delivery": TIGbus interrupts set DRIR
  bits; DRIR & DIMn -> DIRn; any DIRn<55:0> asserts CPU pin b_irq<1> (device
  class, LEVEL-sensitive, assertion level Low). <62:58> -> b_irq<0> (errors);
  <63> = internal Cchip error (NXM). MISC<DEVSUP> = passive-release suppression.
- pc264_io.c:533 unmasks DIM0 bits 48, 55, 61-63. Bit 55 = PCI0 INT from Cypress
  SIO (the 8259, already wired in EmulatR as kIsaBridgeDrirBit). 61-63 = errors.
  Bit 48 is marked "Unused" in the generic PCI DRIR table (pc264_io.c:684) but is
  DELIBERATELY UNMASKED -> it is the DS20 Cchip IIC TIG interrupt.
- PAL maps a device interrupt bit to its SCB vector via 0x800 + vector*16
  (ev6_osf_pc264_pal.mar). The C driver registered iic_service at 0xa9/0xaa.

LEADING DRIR BIT = 48 (confirm empirically; 49-52 are fallback candidates).

NOTE: there are two IIC consumers -- the POLLED PALcode path (sys__iic_read,
ev6_osf_pc264_pal.mar; writes 0xC5, no ENI; fans/temp/PSU at 0x70/0x72) and the
INTERRUPT-DRIVEN C driver (iic_driver.c; ENI set). Only the latter gates
registration; the polled path is unaffected by this fix (no ENI -> no INT level).

## 5. Fix (implemented; EmulatR-only; both headers)

1. deviceLib/Tsunami/IicPcf8584.h -- interruptPending():
   `return ((m_status & kSt_PIN) == 0) && ((m_control & kCtl_ENI) != 0);`
   Level-derived from existing state (snapshot-clean, no new mutable field).
   Reads false until the guest enables interrupt mode, so default boot is
   byte-identical and the polled PAL path is untouched.

2. chipsetLib/TsunamiChipset.h -- evalDeviceIrqs(): assert/deassert a Cchip DRIR
   bit from m_iic.interruptPending() on level change (mirrors the SIO/IRQ4 path
   into kIsaBridgeDrirBit). The bit is chosen at runtime by EMULATR_IIC_IRQ_BIT
   (device class 0..55), DEFAULT OFF (unset) = today's faithful behavior. This
   lets the exact bit be confirmed without a rebuild; once 48 is verified it
   becomes a named constant and s_lastIicLevel is promoted to a member.

Discipline: no srmapi/SSOT changes. Level-sensitive model matches HRM Table 6-9.
DEVSUP passive-release is NOT modeled yet -- a follow-up only if testing shows
stale/storming interrupts.

## 6. Verification plan (PENDING -- fill in after the run)

Native RelWithDebInfo build (both headers changed), then:
```
unset EMULATR_TRACE_ARM_PA
export EMULATR_IIC_IRQ_BIT=48
export EMULATR_TRACE_WINDOW=1
export EMULATR_RETIRE_TRACE_DIR=./traces
export EMULATR_TRACE_ARM_ON_IIC=0x40
export EMULATR_TRACE_ARM_INSTRS=4000000
export EMULATR_GMEM_WATCH=0x2058
export EMULATR_IIC_TRACE=1
export EMULATR_FLASH_ROM=ds20_flash.rom; rm -f ds20_flash.rom
unset EMULATR_PLATFORM
./run_fw.sh ds20 cold 2>&1 | tee fw_ds20_iicirq48.out
```
SUCCESS (in increasing strength):
  - iic_init's 0x40 wait no longer spins to timeout (far fewer 0x6206c loop iters);
  - GMEM-WATCH(0x2058) shows v=0x1805 (member 6) instead of 0x405;
  - HWRPB scan banner: member=6 "AlphaServer DS20".
If bit 48 does not take, sweep 49-52 via EMULATR_IIC_IRQ_BIT (no rebuild).

RESULT: bits 48-52 all -> SYSVAR 0x405 (member 1, unchanged). Then disproven outright
(Sec. 8): no DRIR bit could matter because the IIC never enters interrupt mode.

## 7. Broader hypothesis (to test next)

The same missing-completion-interrupt mechanism may explain the post-dva0 stall
and the LFU-region spin (user observation, 2026-06-29). Corroboration:
Floppy82077.h notes F3/F4/F5 document the SAME class of bug -- a recalibrate/seek
krn$_wait timing out because a completion edge (IRQ6 / 0x536 poll bit) was not
delivered. Capture B's window (cyc ~185M-189M) does NOT cover dva0/LFU (those are
hundreds of millions of cycles later, near ~1.4B at console); a separate later
arm is needed to trace them. After the IIC fix verifies, audit every
interrupt-driven device wait (FDC, IDE/ATAPI, COM, IIC runtime) for a delivered
completion interrupt.

## 8. CORRECTION 2026-06-29c -- interrupt theory DISPROVEN; IIC is POLLED

Direct measurement overturned Sec. 1-7's mechanism. Added two diag logs
(EMULATR_IIC_CTRL_TRACE in IicPcf8584::ioWrite; IIC-IRQ-ASSERT in
TsunamiChipset::evalDeviceIrqs). On a cold boot with EMULATR_IIC_IRQ_BIT=48:

  - EVERY IIC control write is 0xc5 (START) / 0xc3 (STOP) / 0xc0 (INIT) / 0x80 /
    0x00 / 0x20.  ENI (0x08) is NEVER set -- across the whole boot.  (The log was
    fixed to specifically catch ENI writes; count stayed at the first-40 cap with
    zero ENI hits.)  => the DS20 V7.3-2 IIC driver runs POLLED, not interrupt.
  - IIC-IRQ-ASSERT never printed: interruptPending() (ENI-gated) never went true,
    which is why the bit-48..52 sweep was uniformly inert -- the IRQ was never
    raised, so the DRIR bit was irrelevant.

Confirmed-true facts that survive (use these going forward):
  - get_sysvar() = `fopen("iic_ocp0","sr+") ? member 6 : fopen("iic_8574_ocp") ?
    8 : member 1` (pc264.c:636-664).  member 1 means BOTH fopens returned NULL.
  - iic_ocp0 is a static node_list entry at IIC node 0x40, IIC_LED_TYPE, test=1
    (iic_driver.c:225).  iic_init creates its inode only if the verify
    `iic_rw_common(0x40,1,buf,READ)` returns rec_count==1 (iic_driver.c:1071-1076).
  - iic_open REJECTS the open with msg_failure if `pb->mode != DDB$K_INTERRUPT`
    (iic_driver.c:672) -- so even a created inode is unopenable unless the driver
    is in interrupt mode.
  - The 0x40 verify READ physically completes in EmulatR (ACK + 1 byte v=0x00).

OPEN QUESTION (for next session -- do NOT assume, measure):
  Two candidates remain, and they need a single instruction-level look at the
  0x40 verify return + the fopen("iic_ocp0") path:
   (A) the polled verify returns rec_count != 1 -> no inode -> fopen misses; or
   (B) the inode IS created but iic_open's `pb->mode != DDB$K_INTERRUPT` gate
       rejects fopen -> NULL.
  Decisive next step: trace armed at the 0x40 verify (EMULATR_TRACE_ARM_ON_IIC=0x40,
  ARM_INSTRS ~4M) and locate (a) iic_rw_common's return value (R0) feeding the
  `if(status!=1) continue`, and (b) whether allocinode runs for "iic_ocp0".  Also
  read iic_create/iic_set_mode for pc264 to learn the intended pb->mode and why no
  ENI is emitted (is interrupt setup skipped, or is mode polled by design?).

Instrumentation left in tree (all default-off, harmless):
  IicPcf8584 interruptPending() + EMULATR_IIC_CTRL_TRACE;
  TsunamiChipset EMULATR_IIC_IRQ_BIT + IIC-IRQ-ASSERT log;
  GuestMemory EMULATR_TRACE_DISARM_PA.  Recommend keeping them (cheap, gated) until
  the polled path is understood; the IRQ-bit wiring can be removed once (B) is ruled
  out or a polled-completion fix lands.
