# Chipset-event pre/post trap -- design note (2026-05-31)

Goal: in a SINGLE run, capture [previous | chipset-event | post] -- the
retired-instruction + register context immediately BEFORE a chipset interrupt
signal, the event itself (which b_irq, full Cchip CSR snapshot), and the
context AFTER (the PAL INTERRUPT-vector entry through to wherever it lands,
e.g. the 0xd954 dispatch). Logic-analyzer-style, with pre-trigger history.

## Why (immediate payoff)

The Cchip signaling side is HRM-correct (interval timer = b_irq<2> ->
MISC<ITINTR>, cleared W1C; see Ch.6.3.2). The open bug is the VMS PAL
dispatch at 0xd954 reading a ZERO handler PC (r2 = *(impure[0x170] +
impure[0x158])). See memory project_r2_dispatch_zero_handler.

A PC-gate on 0xd954 shows the dispatch but NOT how we got there. This trap
answers the question the PC-gate cannot: is 0xd954 reached directly from the
clock divert, or via a NESTED fault during sys__int_clk (clock handler touches
something that faults, and THAT fault vectors through the zero SCB slot)? The
event chain with the cause of each divert disambiguates "clock -> 0xd954" from
"clock -> ... -> nested fault -> 0xd954", which changes where the fix goes.

## Mechanism

1. Pre-trigger RING (always on): circular buffer of the last K retire records
   (K ~128). Each record = {cycle, PA/PC, encoded instr, register file
   snapshot (or delta)}. ~32 KB at K=128, fully passive -- this is "previous".
2. TRIGGER = chipset interrupt signal. Two distinct moments, log BOTH with
   cycle timestamps but ANCHOR the pre/post window on DELIVERY:
     - ASSERTION (chipset side): TsunamiChipset raises a b_irq line
       (interval timer falling edge -> MISC<ITINTR> -> b_irq<2>; or
       DIRn<55:0> -> b_irq<1>; or error -> b_irq<0>).
     - DELIVERY (CPU side): stageInterruptDivert (systemLib/Machine.cpp)
       actually diverts the CPU to the INTERRUPT vector (offset 0x680).
   Assertion and delivery can be SEPARATED by many cycles (CPU at high IPL or
   polling). Anchoring on delivery keeps "post" bounded and CPU-relevant;
   the logged assertion timestamp shows the latency.
3. EVENT snapshot (at the anchor): Cchip CSRs + CPU state (see fields below).
4. POST capture: next K retire records after the trigger -> "post".
5. FLUSH: write [ring][event][post] to one file at the trigger.

## Event snapshot fields

Cchip CSRs (base CSR_CCHIP = 0x801A0000000; read the MODEL state directly, do
not issue emulated bus reads):
  MISC  +0x080  (ITINTR<7:4> per-CPU [CPU0=bit4], DEVSUP, IPINTR, IPREQ, NXM)
  DIM0  +0x200  DIM1 +0x240      (per-CPU device interrupt masks)
  DIR0  +0x280  DIR1 +0x2C0      (per-CPU device interrupt requests = DRIR & DIMn)
  DRIR  +0x300                   (device raw interrupt request, 64 bits)
  IIC   +0x380                   (interval timer suppress counter)
  CSC   +0x000                   (IPENA = IPI enable mask)
  DIR2/3 +0x680/+0x6C0, DIM2/3 +0x600/+0x640  (Typhoon CPU2/3; capture for SMP)
  which b_irq<3:0> line(s) currently asserted to the anchored CPU.
CPU state: PC (with PAL-mode bit), PS, IPL, ISUM (EI<3:0> nibble), IER_CM,
full integer register file, and the divert CAUSE (which EI bit / vector).

## Architecture

Extend traceLib/BreakpointSink rather than add a new sink:
  - add an EVENT-TRIGGER source alongside the existing PC gates
    (s_gateOpenPc/s_gateClosePc): a trigger callback the divert path
    (and optionally the chipset assert path) invokes.
  - add the pre-trigger ring (reuse the full-complement retire record the
    sink already formats).
  - reuse canAcceptInterrupt / the divert seam (Phase B) as the hook point.
Determinism: the ring is passive; only the flush does I/O, at the trigger
(acceptable, one-shot or N-shot via a revolutions-style counter).

## Caveats

- Assertion vs delivery latency (above) -- log both, anchor on delivery.
- Multiple diverts: gate with a revolutions counter so we capture the FIRST
  clock divert (and a few after), not every poll.
- If 0xd954 turns out to be a nested fault, add the fault vector (DTBM/
  unaligned/arith) as additional trigger sources.

## Sequencing

Quick path first: PC-gate on 0xd954 (open ~0xd910, close ~0xd958) to read
r21/impure[0x158]/impure[0x170]/r4/r2. If that alone identifies the zero
field, the trap may be unnecessary for THIS bug. Build the trap if we need to
see the cause-chain or for general interrupt-path diagnostics going forward.
