<!--
============================================================================
20260604_serial_console_interrupt_design.md -- COM1 16550 + Device-Interrupt
Console Unblock: Design Notes
============================================================================
Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1

Project Architect: Timothy Peer
AI Collaboration:  Claude (Anthropic)

Commercial use prohibited without separate license.
Contact:        peert@envysys.com  |  https://envysys.com
Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
============================================================================
-->

# COM1 16550 + Device-Interrupt Console Unblock -- Design Notes

**Status:** RESOLVED 2026-06-04 (Cowork pass complete).  All [COWORK:]
placeholders are filled with verified files/lines; decisions recorded
inline as [RESOLVED ...] blocks.  Key deltas from the web draft:
4.1 is verify-only (run loop polls per step); MCR.OUT2 gate NOT armed
(validated negative); Section 6 PIC promoted from insurance to critical
path (pc264_io.c programs it; the OCW1 mask is load-bearing); TEST 1
cold-boots (PIC state predates all snapshots); no serialization change
this milestone (clean kChipsetVersion bump in follow-up).

**Goal of the work:** reach the `P00>>>` console prompt and accept typed
input. Both halves are unblocked by one wire -- an asserted COM1 UART
interrupt delivered through the Cchip device-interrupt path.

---

## Section 0: The gate this design closes

Prior open question: is the SRM console getc/putc polled or
interrupt-driven? If polled, a register-only 16550 model reaches the
prompt with no interrupt plumbing. That shortcut is now CLOSED -- the
path is interrupt-driven. Three trace facts settle it:

1. Steady-state idle never touches the UART. A full captured tick of
   the post-rwp steady state (75K retires, window 160.859B-160.860B)
   shows zero accesses to COM1 0x3F8-0x3FF. The only I/O in the tick is
   two LDBUs of the unrelated 0xFFFF0001 IIC/NIC poke and one inb(0x80)
   delay-port read. A polled getc would show periodic LSR (0x3FD) or
   RBR (0x3F8) reads here; there are none.

2. The shell blocks on a semaphore, not a loop. In the
   yyreset -> idle window, read_with_prompt (0x70448) descends through
   the file layer (0x5a650) into a kernel wait primitive (JSR 0xb0f08)
   with R16=0x3f23e00 (an input semaphore); the scheduler swaps to
   krn$_idle and the shell process never runs again in the window.
   RBR is never read (LDBU counts in-window: LSR x198, MSR x199,
   RBR x0; all LSR/MSR reads belong to the boot-time polled qprintf
   output path, which ends before yy_reset).

3. The driver ends in interrupt mode. It toggles IER 0x00 / 0x03
   ninety-eight times during the polled-output phase and ends at
   IER=0x03 (RX-data-available + THR-empty enabled) immediately before
   blocking -- a driver handing the line to its ISR.

Consequence: only an asserted UART interrupt
(ISA IRQ4 -> Cypress bridge -> DRIR<55> -> Cchip EI -> device line)
can post the input semaphore at 0x3f23e00 and wake the shell.
The register model (Increment 1) and the device-EI path (Increment 2)
are therefore both on the critical path; there is no polled-only
milestone.

---

## Section 1: The interrupt line is combinational, not edge-driven

This is the single most important correctness requirement and is NOT
deferrable. At hand-off the driver wrote IER=0x03 with THR already
empty (boot output drained). A real 16550 asserts the THR-empty
interrupt on that write (IIR -> 0x02); the TX ISR fires and drains the
software output ring where `P00>>>` is queued. If the model raises
THR-empty only on a TX-complete transition (an edge when the shift
register empties), it MISSES the enable-while-already-empty case and
the prompt stays stuck even after DRIR/EI is wired.

The UART interrupt line MUST be a level-sensitive combinational
function of (IER & LSR), re-evaluated on every event that can change
either operand:

```
// Recompute on: write IER, write THR, write FCR (FIFO reset),
//               RX enqueue, read IIR, read RBR, read LSR.
// All bit names are 16550 register fields, not CPU IPRs.

uart_int_pending =
      (IER.ERBFI & LSR.DR)        // 0x01 enable & data-ready  -> RX-avail
    | (IER.ETBEI & LSR.THRE)      // 0x02 enable & THR-empty    -> THR-empty
    | (IER.ELSI  & LSR.<errbits>) // 0x04 enable & line-status  -> DEFERRED
    | (IER.EDSSI & MSR.<delta>);  // 0x08 enable & modem-status -> DEFERRED
```

Companion requirement -- IIR priority + clear-on-read, or the ISR
storms / never re-arms:

- IIR reports the highest-priority pending source, with FIFO bits
  <7:6> = 11 set (so reads present as 0xC0 | code), consistent with
  the FIFOs-on advertisement already emitted:
    line-status   IIR code 0x06   priority 1 (highest)   [DEFERRED]
    RX-avail      IIR code 0x04   priority 2
    char-timeout  IIR code 0x0C   priority 2             [DEFERRED]
    THR-empty     IIR code 0x02   priority 3
    modem-status  IIR code 0x00   priority 4             [DEFERRED]
- Reading IIR clears a pending THR-empty source.
- Reading RBR clears RX-avail.
- Writing THR clears THR-empty (and, with the line empty + ETBEI set,
  re-asserts -- see the storm guard in Section 5).
- The ISR reads IIR to dispatch; that read MUST lower the line for the
  serviced source or the line storms. Conversely, each clearing read
  MUST re-run the combinational eval so the line re-arms for the next
  byte.

---

## Section 2: 16550 complete register surface (Increment 1)

Model the silicon: the full register set is present with correct
storage / RO / W1C and correct polled-read semantics. FIFO trigger
levels, char-timeout, line-status, and modem-status INTERRUPTS are
shallow behavior, tracked by the unwired-TODO table in Section 3 per
the CchipPhaseA resolution (4) discipline. "Complete surface, shallow
behavior" -- not "minimal surface."

Register map (COM1 base 0x3F8; DLAB in LCR<7> selects divisor latch):

```
off   DLAB=0 read   DLAB=0 write   DLAB=1
0x3F8 RBR           THR            DLL
0x3F9 IER           IER            DLM
0x3FA IIR           FCR            (IIR/FCR)
0x3FB LCR           LCR            LCR
0x3FC MCR           MCR            MCR
0x3FD LSR (RO)      --             LSR
0x3FE MSR (RO)      --             MSR
0x3FF SCR           SCR            SCR
```

Behavior depth for THIS milestone:
- RBR/THR: RX-avail driven by the injection queue (Section 4); THR
  modeled as instantaneous TX into the output ring (storm guard in
  Section 5).
- IER: full storage of bits 3:0; feeds the combinational line.
- IIR/FCR: IIR priority + clear-on-read per Section 1; FCR FIFO-enable
  and reset bits honored; trigger-level bits stored, no trigger
  effect (TODO).
- LCR: full storage (word length, stop, parity, DLAB, break).
- MCR: full storage; OUT2 is an ACTIVE delivery gate (standard PC/AT
  Southbridge wiring: the UART INTRPT pin reaches the ISA IRQ line
  through a tri-state buffer enabled by OUT2; the Cypress bridge keeps
  this). GATE THE DELIVERY, NOT IIR: OUT2 does not affect the 16550
  internal interrupt logic, so IIR still reports pending sources with
  OUT2=0 (a polling driver must still see them). AND OUT2 in only at
  the DRIR assert boundary (Section 4, Seam 1):
    isa_irq4 = uart_int_pending AND MCR.OUT2   -> DRIR<55>
  [RESOLVED 2026-06-04, VALIDATED NEGATIVE: the yyreset->idle window
  (trc 20260604-112143, 1.81M retires) contains ZERO MCR (0x3FC)
  writes -- the only control traffic is IER 98x 0x03 / 98x 0x00.  Any
  MCR setup predates the window (driver init runs before cyc 158.1B).
  Per this section's own conditional: the gate is NOT ARMED this
  milestone.  MCR is stored, the gate moves to the Section 3 TODO
  table, and the model emits a one-shot stderr log on the first MCR
  write so the next cold-boot trace settles bit 3 permanently.  Arm
  the gate in the same edit that observes OUT2 set.  AXPBox precedent:
  CSerial has no OUT2 gate at all (Serial.cpp eval_interrupts).]
- LSR: DR from queue, THRE/TEMT from the instantaneous-TX model,
  error bits stored (no error injection path yet).
- MSR: storage; no modem-delta interrupt (TODO).
- SCR: scratch, plain storage.

[RESOLVED 2026-06-04: the model EXISTS -- deviceLib/Tsunami/Uart16550.h,
header-only, guard UART_16550_H, implements IIoPortHandler; IER/FCR/
LCR/MCR/SCR/DLL/DLM storage already present, LSR/MSR/IIR computed.
This increment EXTENDS IN PLACE (no new file, no build edit): add the
combinational line, IIR priority/clear, and an internal RX FIFO
replacing the direct backend->getChar() pulls in readRBR/readLSR.]

---

## Section 3: Unwired-TODO register table (pre-stub)

Goes in the UART model header per the house rule (header table +
greppable `// TODO(unwired): ...` line comment at each shallow site;
both removed in the same edit that lands the wiring).

```
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior
// ============================================================================
// FCR.trigger  -- trigger-level bits <7:6> stored; RX-avail asserts on
//                 >=1 byte regardless of programmed trigger. Wire when a
//                 driver programs a non-default trigger level.
// IIR 0x0C     -- char-timeout interrupt not asserted; RX-avail (0x04)
//                 covers the traced driver. Wire when the FIFO timeout
//                 path is exercised.
// IIR 0x06     -- line-status interrupt (ELSI) not asserted; LSR error
//                 bits store but raise nothing. Wire when an error
//                 injection path exists.
// IIR 0x00     -- modem-status interrupt (EDSSI) not asserted; MSR
//                 stores but raises nothing. Wire when modem-control
//                 lines are exercised.
// MCR.OUT2     -- stored; delivery gate NOT armed (validated negative
//                 2026-06-04: zero MCR writes in the hand-off window;
//                 setup predates the window).  One-shot stderr log on
//                 first MCR write -- a RESEARCH SIGNAL only, not an
//                 auto-arm trigger: arming additionally requires
//                 confirming this BOARD gates delivery on OUT2 (the
//                 evidence -- no MCR writes + AXPBox no-gate precedent
//                 -- currently says it does not).  A stray OUT2=1
//                 write must not arm a gate that then suppresses
//                 delivery whenever OUT2 drops.
// ============================================================================
```

---

## Section 4: Device-interrupt delivery seam list (Increment 2)

This is net-new plumbing. It does NOT reuse the interval timer's
b_irq<2> aggregation: the timer rides i_intim_l -> b_irq<2> (EI level
22) via a direct cpu-field poke and bypasses DRIR/DIM. COM1 rides the
device path: DRIR<55> -> AND DIMn -> DIRn -> b_irq<1> (EI level 23).

Naming hazard -- two registers both called "IER," both must be open:
- UART IER (offset 0x3F9): the 16550 enable, RX+THR (0x03 observed).
- CPU HW_IER (IPR 0x010A), EIEN field: the 21264 external-interrupt
  enable. Device line is EI level 23; per the 2026-05-19 Phase D map
  (irqLevel -> IER bit via 57 - irqLevel) that is bit 34 = EIEN<1>.
Name them apart in code/comments (e.g. uartIer vs cpu.ier / EIEN).
A COM1 interrupt that sets DRIR but finds EIEN<1> masked is correctly
dropped; if the two IERs are conflated in the trace this will read as
a bug.

Seam-by-seam (each edit gets a header FILE N / FUNCTION / CHANGE block
and an inline comment at the changed line, per the house rule):

```
SEAM 1  UART -> PIC -> Cchip assert  [RESOLVED: route through the PIC]
  Uart16550 (header-only) exposes a computed intPending() accessor; it
  calls nothing.  TsunamiChipset::step() -- the existing per-step
  chipset hook -- evaluates ONCE per boundary:
    pic.setIrqInput(4, m_com1.intPending())   // + OUT2 once armed
    level = pic.outputAsserted()              // honors OCW1 mask + EOI
    level ? m_cchip.assertInterrupt(55) : m_cchip.deassertInterrupt(55)
  assert/deassert exist: TsunamiCchip.h:383 / :395 (atomic fetch_or /
  fetch_and).  Step-boundary eval IS the Section 5 storm guard.
  The PIC hop is REQUIRED, not optional -- see revised Section 6.

SEAM 2  Cchip DRIR/DIM -> b_irq<1>   [RESOLVED: already combinational]
  V4 stores no irq1 line at all: readDIR(cpuId) (TsunamiCchip.h:408)
  computes DRIR & DIM[cpu] on every call, and Machine::run polls per
  step.  No re-eval seam is needed on DRIR change or DIM write -- the
  AXPBox staleness hazard this seam guarded against does not exist in
  V4's pull model.  Add pendingIrq1(cpuId) as the level query:
    (readDIR(cpuId) & 0x00ffffffffffffff) != 0
  mirroring pendingIrq0's mask split (TsunamiCchip.h:511-512).

SEAM 3  Level poll -> canAcceptInterrupt -> divert  [RESOLVED]
  New arbitration block in Machine::run AFTER the b_irq<0> block
  (pattern at Machine.cpp:1045-1081), gated:
    !divertedThisCycle && cchip().pendingIrq1(0) && canAcceptInterrupt(23)
  staging stageInterruptDivert(m_cpu, 1ull<<34)  // EI[1], ISUM bit 34
  canAcceptInterrupt: Machine.cpp:461-509; formula 57-irqLevel at
  :498-501 maps arg 23 -> IER bit 34.  NAMING NOTE at the call site:
  the 23 is V4's bit-selector convention, NOT the HRM IPL (HRM 6.3.1
  and TsunamiCchip.h:507 put the device class at IPL 21).  Priority
  order timer > error > device falls out of block order + the
  divertedThisCycle mutex (one divert per boundary).
  EI-bit cross-check vs AXPBox: System.cpp:1783 drives irq_h(1) for
  (drir & dim & 0x00ffffffffffffff) -> eir bit 1 -> 21264 ISUM bit 34.
  SIDEBAR (separate ticket, do not fix here): the EXISTING error path
  gates canAcceptInterrupt(23) (bit 34) but stages isum 1<<33 (EI[0]);
  per AXPBox error rides irq_h(0)=bit 33, so its gate arg should be 24.
  Works today because firmware enables both bits.

SEAM 4  HW_IER storage  [VERIFIED 2026-06-04 -- no change]
  PalEntries.cpp:1389-1391 HW_IER read returns cpu.ier; :1392-1394
  HW_IER_CM composes via ierCmCompose.  EIEN<1>=bit 34 reachable.
```

[RESOLVED: DRIR/DIM has storage + atomic assertInterrupt/
deassertInterrupt + computed readDIR already wired (Phase A/B).  The
ONLY missing piece is the b_irq<1> consumer: pendingIrq1 query +
Machine arbitration block (Seams 2-3).  No TsunamiCchip TODO(unwired)
entries conflict.]

### 4.1  PAL-mode (and IPL) deferral -- latch, do not drop

Per the PALcode Design Guide Table 1-1, PALmode "Disables interrupts"
(PALcode runs as atomic sequences); HW_REI is the exit that re-enables
them. The Seam 3 acceptance gate already includes NOT palMode, but the
gate is only half the requirement. The other half is what decides
deterministic delivery versus intermittent stall:

- A device EI request asserted while the CPU is in PAL mode (or while
  IPL masks level 23) is DEFERRED, NOT DROPPED. The model must never
  clear the request as a side effect of a masked / failed acceptance.
- This is naturally lossless because the request is LEVEL-SENSITIVE,
  not edge-consumed: DRIR<55> stays set as long as the UART source is
  unserviced (int_pending holds true until the ISR reads RBR / reads
  IIR / writes THR). While deferred the ISR has not run, so the source
  is still asserted, so DRIR<55> is still set.
- Therefore the only implementation requirement is RE-EVALUATION at
  every delivery opportunity: recompute DRIR & DIM -> b_irq<1> ->
  canAcceptInterrupt(23) at each step() boundary AND specifically when
  palMode clears (HW_REI) or IPL is lowered (HW_MTPR IPL / swpipl).
  Do NOT gate re-evaluation on the assert edge alone.

This is the SAME discipline already adopted for the interval timer
(profile task #70: latch unconditionally on the cycle edge, gate only
the divert). COM1 follows it, with the refinement that the "latch" for
a device interrupt IS the level-held DRIR<55> bit -- it self-clears
when the ISR services the UART, so no separate one-shot latch is
needed.

Why it matters for THIS work: the hand-off THRE drain (TEST 1) runs in
native-mode driver code and is likely taken immediately. But the
interval timer (b_irq<2>) is interleaving, and any COM1 assert that
lands inside a timer-ISR PAL window, a CALL_PAL, or an HW_MTPR sequence
must survive to PAL exit. Edge-consume the request and you get a stall
whose presence depends on exactly when the keystroke or THRE lands
relative to PAL windows -- precisely the nondeterminism the V4 mandate
forbids.

[RESOLVED 2026-06-04: CONFIRMED poll-per-step -- 4.1 is verify-only.
Machine::run's loop body (for-loop at Machine.cpp:804) executes the
timer DELIVER poll (:963) and the b_irq<0> level poll (:1045-1047) on
EVERY step iteration.  canAcceptInterrupt returns false in palMode
(:476); the request persists (latch for timer, computed level for
error/device); the first post-HW_REI iteration's poll delivers.  No
execHwRei seam, no IPL-write seam.  The Seam 3 block inherits the
invariant by construction.  Documented as an invariant comment at the
new block.]

---

## Section 5: Deterministic RX injection queue (Increment 3)

Shape (mirrors the interval-timer tick accounting):
- A single-producer / single-consumer queue. The TCP backend thread is
  the sole producer; it enqueues raw bytes asynchronously. This is the
  ONLY place threading enters the serial path -- keep the surface to
  this one queue, nothing else crossing the boundary (minimal-Qt /
  std-first threading rule).
- The CPU side is the sole consumer and drains the queue ONLY at the
  step() boundary, never mid-instruction. On drain: push the byte to
  RBR, set LSR.DR, re-run the combinational eval (Section 1).
- Each enqueued byte is stamped with the cycle at which it becomes
  visible. For live interactive use the stamp is "next boundary." For
  replay the stamp comes from a recorded injection schedule.

Determinism trade-off to NAME in the spec (V4 mandate): host keystroke
ARRIVAL is nondeterministic, but CONSUMPTION is deterministic given a
fixed injection schedule. Replay determinism therefore requires the
injection schedule to be RECORDED and replayed, not regenerated from
live host timing.

Snapshot-anchoring convention (required, or replay-from-snapshot breaks
silently): sidecar stamps are ABSOLUTE cycle counts. A snapshot restore
re-bases the consumer's next-due pointer to the first stamp
>= restored_cycle. Without this, a replay that starts from a mid-stream
snapshot injects at the wrong cycles. This is the price of decoupling
the schedule from the snapshot (Section 9); it is cheap as long as it
is written down and tested. [RESOLVED: record/replay of the schedule is
DEFERRED to the same follow-up commit as serialization (Section 9) --
this milestone implements live injection only (stamp = next step
boundary).  The convention above (absolute stamps, sidecar beside the
snapshot, re-base on restore) is adopted now as the contract and noted
as a TODO at the queue site.]

Storm guard for instantaneous TX (also a named determinism point):
TX is modeled instantaneous, so THRE re-asserts the moment the TX ISR
returns; with ETBEI still set and the ring empty this is a same-cycle
storm. The driver normally clears ETBEI when its ring empties, so
faithful clear-on-IIR-read + faithful IER storage lets the driver's own
logic terminate it. The model-side guard: re-evaluate the computed line
ONLY at the step() boundary and deliver AT MOST ONE interrupt edge per
boundary -- the same discipline as RX injection, extended to the
TX-complete / THRE re-eval.

---

## Section 6: 8259 PIC pair -- PROMOTED to critical path (Increment 4)

[REVISED 2026-06-04 -- no longer insurance.]  The DS10 SRM programs the
Cypress PIC pair at hardware init: pc264_io.c:520-571 issues full
ICW1-ICW4 to master and slave, then OCW1 masks (master: ONLY IRQ2/
cascade enabled -- IRQ4 stays MASKED until the serial driver unmasks
it), then non-specific EOIs to both.  Two consequences:

1. The mask is LOAD-BEARING.  During the polled-output phase the driver
   toggles uartIer 0x00/0x03 per byte; with THRE always true, every
   IER=0x03 period computes uart_int_pending=1.  The PIC's masked IRQ4
   is what keeps those asserts off DRIR<55> until the driver goes
   interrupt-driven.  Without the mask the model delivers spurious
   device interrupts mid-qprintf.

2. The model needs: ICW1-4 init state machine, OCW1 mask storage,
   IRR level inputs, EOI (OCW2) handling, and the master/slave cascade.
   The OCW3 poll / IACK vector read stays promote-after-trace: capture
   the first b_irq<1> handler entry and record which ports it reads
   before dispatch (AXPBox resolves via pic_read_vector on an IACK
   read, AliM1543C.cpp:431/921 -- expect the Cypress analog).

3. PIC OUTPUT RE-EVAL INVARIANT (review addition 2026-06-04 -- the
   8259-level analogue of 4.1, and the actual unstick mechanism).
   The PIC INT output MUST be recomputed as a level function of
   (IRR & ~IMR & priority) on EVERY un-gating event -- OCW1 (IMR)
   write and EOI -- not only on an IRQ input edge.  The hand-off
   sequence is: UART THRE asserts (line already high), THEN the
   driver's last un-gating write opens the path.  An output driven
   only from the input edge saw that edge earlier, while masked, and
   never re-fires.  Edge-trigger nuance (mode pinned below): the 8259
   latches IRR on the input rising edge REGARDLESS of IMR -- the mask
   gates the output stage, not edge capture -- so a masked-while-
   asserted edge is still pending in IRR and the unmask delivers it.

4. TRIGGER MODE PINNED: ICW1 = 0x11 (cy82c693_def.h:118; bit3 LTIM=0)
   -- EDGE-TRIGGERED, cascade, ICW4 required; vector bases master 0x00
   / slave 0x08 (DICW2).  No ELCR programming in pc264_io.c init.
   TEST 2 consequence: post-EOI re-trigger requires a fresh input
   edge, which the 16550 provides naturally IFF its line genuinely
   FALLS on source clear (IIR read / RBR read) and rises on the next
   byte -- the Section 1 combinational fidelity is what makes
   steady-state RX work, not PIC-side levels.

THREE-LAYER RE-EVALUATION DISCIPLINE (one shared rule): the UART
re-evaluates (uartIer & LSR) on its register events (Section 1); the
PIC re-evaluates (IRR & ~IMR & priority) on OCW1/EOI/input change
(item 3); the CPU re-evaluates pending EI every step boundary
(4.1, verified free).  The chain unsticks only if all three re-eval
on their own un-gating event; miss any one and the stall reappears
one layer down.

The Cy82C693ISABridge.h surface today is 190 lines with NO PIC decode
(ports 0x20/0x21/0xA0/0xA1/0x4D0-1 unclaimed) -- the PIC is net-new
there, output wired per Seam 1.

SEQUENCING CONSEQUENCE (validated from the window): the yyreset->idle
window contains ZERO PIC port traffic -- all PIC programming (init +
the driver's IRQ4 unmask) happened BEFORE cyc 158.1B.  PIC state is
not in any snapshot, so a predig_rwp replay boots the PIC at reset
defaults and IRQ4 never unmasks.  TEST 1 therefore runs from COLD BOOT
(~10-15 min wall with warps, measured 2026-06-04), not from predig.

---

## Section 7: Data fidelity flags

- DRIR<55> is BOARD wiring, not 21272 HRM -- and it is now FIRMWARE-
  SOURCE-CONFIRMED, no _PROVISIONAL flag needed: pc264_io.c:533
  (initialize_hardware) executes
    DIM0 |= (1<<48) | (1<<55) | (0xE<<60)
  i.e. the DS10 SRM itself unmasks bit 55 for the ISA bridge output
  (bit 48 = a second on-board device line, note for the PCI-enum work;
  61:63 = error class).  Matches AXPBox AliM1543C (interrupt(55,...))
  and the 2026-05-30 pci_irq_table finding.
- UART IER vs CPU HW_IER: distinct registers, distinct names in code
  (Section 4). EIEN<1> = bit 34, EI level 23, per 2026-05-19 Phase D.

---

## Section 8: Test plan (two-step, sources validated independently)

The prompt and the typed echo ride the same wire but different 16550
sources; validate them separately so a regression in one cannot mask
the other.

```
TEST 1  THRE wire -> prompt drains, zero keyboard input
  Setup:  Seams 1-4 live; EIEN<1> enabled; combinational line asserts
          THR-empty on the IER=0x03 write with THR already empty.
          COLD BOOT (Section 6 sequencing consequence: PIC programming
          predates every existing snapshot and PIC state is not
          serialized; a predig replay leaves IRQ4 masked forever).
  Expect: `P00>>>` appears at the console with ZERO RX activity
          (no RBR reads, no injection). Fully guest-generated ->
          deterministic. This is the cheapest gate and isolates the
          device-EI plumbing + THRE line from the injection queue.
  Anchor: the assertion keys on the driver's FINAL uartIer=0x03 write
          (the hand-off ordering is settled: the PIC unmask happens
          pre-window, before cyc 158.1B, so the IER write is the
          LATER of the two un-gating events and is the unstick).
          The IRQ4-unmask assumption itself is verified by the PIC
          model's one-shot port logs on the cold boot.
  Check:  doctest CHECK only (exceptions disabled in V4).

TEST 2  RX-avail wire -> typed echo
  Setup:  Test 1 passing; inject one keystroke through the
          cycle-stamped queue.
  Expect: RX-avail asserts -> input semaphore at 0x3f23e00 posts ->
          shell wakes -> reads RBR -> echoes.
  Check:  doctest CHECK; assert RBR read count goes 0 -> 1 in-window
          and the echo byte reaches the output ring.

TEST 3  PAL-window deferral -> delivery on HW_REI (determinism guard)
  Setup:  Tests 1-2 passing; using the cycle-stamped queue, inject a
          keystroke stamped to a cycle known to fall inside a PAL
          window (e.g. during a timer-ISR or CALL_PAL sequence).
  Expect: DRIR<55> sets during PAL mode; the interrupt is NOT taken
          until palMode clears at the next HW_REI; then it delivers
          and the byte is read. The request is never lost.
  Check:  doctest CHECK; assert the taken-cycle equals the first
          HW_REI after the assert, and is identical across two runs
          (the deferral must be deterministic, not timing-dependent).
```

[Assertion addresses (semaphore 0x3f23e00, rwp 0x70448, yyreset
0x44518) are re-verified at test-writing time against the cold-boot
run under test -- they are heap/GCT-relative and can shift with env
contents.]

---

## Section 9: Snapshot / serialization

RESOLVED (2026-06-04). Split the state by what it is:

1. Injection queue contents + replay schedule -> SIDECAR, never in any
   snapshot. They are an input stream, not machine state; snapshotting
   them conflates "what the machine is" with "what the operator typed"
   and churns the version every time the injection mechanism changes.
   Anchoring convention in Section 5.

2. UART hardware-visible registers (RBR/THR latch, IER, IIR/FCR config,
   LCR, MCR, LSR, MSR, SCR, divisor) -> the IO/DEVICE snapshot section
   (the existing I/O structural maps), NOT CpuState. The UART is a
   device; CpuState holds CPU registers/IPRs. So kCpuStateVersion is
   likely untouched; the version that may move is the IO-map's.
   [RESOLVED: home = the chipset snapshot section, governed by
   kChipsetVersion (Snapshot.h:127, currently 1).  UART + PIC state
   serialize together there in a FOLLOW-UP commit AFTER the prompt
   milestone, as a clean kChipsetVersion=2 bump -- regen cost is one
   cold boot, ~10-15 min wall (measured 2026-06-04), so the guarded-
   padding alternative is not worth carrying.  THIS milestone touches
   no serialization at all; the known consequence is that snapshots
   taken between driver-init and the prompt are unsound for UART/PIC
   state -- they already are today, and TEST 1 cold-boots anyway.]

Padding-reuse guard (if the UART block is squeezed into existing
IO-map padding rather than appended with a version bump): a legacy
snapshot reads those bytes as zero, and zero is WRONG for LSR --
THRE=0,TEMT=0 means the model believes TX is permanently busy and the
next putc blocks forever. Reuse padding ONLY with (a) a discriminator
(key off the existing version field or spend one "uart_present" bit)
and (b) a load-time default that forces LSR.THRE|TEMT (and sane
IER/MCR) when the block reads as legacy-zero. Reusing padding on trust
is a latent wedge.

Bump-cost reframe: the `>>>` prompt snapshots that CLAUDE.md protects
do not exist yet -- reaching the prompt is the goal of this work. What
a version bump actually invalidates is the intermediate BOOT snapshots
used to fast-forward during iteration. That is a real but bounded cost
(regenerate one boot snapshot, eat the boot cycles). Weigh: clean
IO-map version bump (one regen) vs guarded padding reuse (no regen, but
carry the discriminator+default permanently). The V4 fidelity house
style leans to the clean bump unless boot-snapshot regen is painfully
slow; the guarded-padding path is legitimate if the guard is written.
[CALL MADE (see item 2 above): clean kChipsetVersion bump in the
follow-up serialization commit; measured regen cost ~10-15 min cold
boot with warps (2026-06-04 run).  Guarded padding reuse rejected --
not worth carrying the discriminator permanently.]

---

## Section 10: Sequencing summary

1. 16550 complete register surface (Section 2) + the combinational
   (IER & LSR) line with IIR priority / clear-on-read (Section 1).
   The line semantics are the unstick and are NOT deferrable.
2. Device-interrupt delivery (Section 4): DRIR<55> -> DIM -> b_irq<1>
   -> EI level 23 -> EIEN<1> gate -> divert. New plumbing. Includes
   PAL-mode / IPL deferral as latch-and-redeliver (4.1): never drop a
   request masked by palMode/IPL; re-evaluate on HW_REI.
3. Deterministic RX injection queue (Section 5).
4. 8259 PIC pair (Section 6, critical path): ICW FSM + OCW1 mask +
   IRR edge capture + EOI + cascade, output re-eval invariant; poll/
   IACK vector read promote-after-trace.
5. Tests (Section 8): prompt-drains-no-input, then typed-echo.

MILESTONE CLOSURE NOTE (review addition): reaching `P00>>>` via the
cold-boot run validates the wire, but a REUSABLE prompt snapshot
cannot be banked until the follow-up kChipsetVersion serialization
(UART + PIC state) lands -- a snapshot taken at the prompt today
restores with reset-state UART/PIC and a dead console.  The milestone
is not fully closed until that follow-up commits.

---

## Cross-references

- Chipset CSR surface + unwired-TODO discipline:
  CchipPhaseA_Design_Notes.md
- HRM-vs-AXPBox interrupt-path profile: Tsunami_HRM_vs_AXPBox_Profile.md
  (1.3 device interrupts; 1.2 timer; canAcceptInterrupt gate)
- HW_IER / EIEN gating, IRQ-to-IPL map: 20260519_boot_path_cleared.md
  (Phase D)
- HRM (Tsunami/Typhoon 21272): EC-RE2CA-TE Rev 4.0, 21 Oct 1999
  (6.3 TIGbus and Interrupts; 10.2.2.7-8 DIRn / DRIR)
- Board interrupt wiring (DRIR<55> assignment): apisrm/ref/pc264_io.c
  :533 (DIM0 bit-55 unmask) + :727 (pci_irq_table, Cypress bridge row)
  -- DS10/PC264, the platform this firmware targets (not ES45/Titan).
- 8259 init programming: apisrm/ref/pc264_io.c:520-571 (ICW1-4 master+
  slave, OCW1 masks, non-specific EOIs); defaults + trigger mode:
  apisrm/ref/cy82c693_def.h:85-135 (ICW1=0x11 edge-triggered).
