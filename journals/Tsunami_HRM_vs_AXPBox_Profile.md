# Tsunami / Typhoon 21272 — HRM ↔ AXPBox Implementation Profile

Project: EmulatR — Alpha AXP / EV6 Architecture Emulator (V4)
Author: Timothy Peer · AI collaboration: Claude (Anthropic)
Companion to: AXPBox-vs-V4 topology audit (task #60), EV6 Spec audit.
Status: first cut (interrupts + memory complete; Pchip/Dchip to expand).

## Purpose

Profile the Tsunami/Typhoon 21272 chipset as the HRM **specifies** it vs how
AXPBox **implements** it, register-by-register and mechanism-by-mechanism, so
V4's `TsunamiChipset` controller interface (task #71) is designed from the spec
plus a clear-eyed view of where AXPBox cut corners — rather than copying
AXPBox's expedient hacks or inheriting its gaps.

Confidence marks: **[HRM]** verbatim/derived from the 21272 HRM;
**[AXP]** observed in AXPBox source (authoritative reference *implementation*,
not spec); **[?]** inferred / needs HRM confirmation before binding V4 dispatch.

Sources: `tsunami_typhoon_21272_hrm.txt` Ch.6 (interrupts) + Ch.10 (CSRs);
AXPBox `src/System.cpp` (`cchip_csr_read`/`cchip_csr_write`/`interrupt`),
`src/AlphaCPU.cpp` + `.hpp` (`irq_h`, timer fire, `check_int`/`check_timers`).

---

## 1. Interrupt subsystem (boot-critical path)

### 1.1 The two models side by side

| Layer | HRM | AXPBox |
|-------|-----|--------|
| CPU request reg | `b_irq<5:0>` pins → EI[5:0], latched until source deasserts **[HRM]** | `state.eir` (6 bits), set by `irq_h`, latched **[AXP]** |
| CPU enable reg | `HW_IER` EIEN<5:0> = arch bits 38:33 (IPL 30/23/22/21/20/19) **[HRM]** | `state.eien` (low 6 bits) **[AXP]** |
| Delivery gate | take interrupt when enabled & requested & IPL/PALmode permit **[HRM]** | `check_int`: `(eien & eir) && !(pc&1)` → `GO_PAL(INTERRUPT)` **[AXP]** |
| Chipset status | Cchip `MISC<ITINTR>`, `DRIR`, `DIMn` **[HRM]** | `state.cchip.misc/drir/dim[]` **[AXP]** |

EV6 line map (both): EI[0]→IPL30, EI[1]→IPL23 (device), **EI[2]→IPL22
(interval timer)**, EI[3]→IPL21 (IPI), EI[4]→IPL20 (perf0), EI[5]→IPL19 (perf1).
Cchip drives `b_irq<3:0>` (error/special, device, timer, IPI); EI[4]/EI[5] are
CPU-internal performance counters, not chipset-driven.

### 1.2 Interval timer — `b_irq<2>` (the lane V4 is stuck on)

HRM §6.3.2 **[HRM]**: external pin `i_intim_l` asserts → Cchip sets
`MISC<ITINTR>` for the CPU(s) and asserts `b_irq<2>`; the pin **stays asserted
(latched)** until the CPU writes a 1 to its `MISC<ITINTR>` bit (W1C). `IICn` can
suppress the timer for n cycles. The interval timer **bypasses DIM/DRIR** — it
is not a device interrupt and is not maskable at the Cchip; its only mask is the
CPU's `EIEN<2>`.

AXPBox **[AXP]**:
- Fire (CPU loop, `AlphaCPU.cpp:392`): every `ins_per_timer_int = cpu_hz/1024`
  instructions (1024 Hz, CPU 0 only), `cc_large > next_timer_int` →
  `cSystem->interrupt(-1, true)`.
- `interrupt(-1)` (`System.cpp:1763`): `misc |= 0xf0` **and** for each CPU
  `irq_h(2,true,0)` — **unconditional latch**, no eien/IPL/PALmode check, delay 0
  ("timer interrupt is immediate").
- Clear (`csr_write` MISC, `System.cpp:1404`): firmware W1C of `MISC<ITINTR>`
  → clears bit **and** `irq_h(2,false,0)` deasserts `eir<2>`.
- Deliver: handled downstream in `check_int` via `eien & eir & !PALmode`.

**V4 status / gap:** V4 has the clear path (`miscWriteW1C` ITINTR deassert,
task #10) and the delivery gate (`canAcceptInterrupt(22)` checks IER bit 35 =
EIEN<2> — storage verified correct). **BUG (task #70):** V4's
`fireIntervalTimer()` (the `MISC<ITINTR>`/`pendingIrq2` latch) sits *inside* the
`canAcceptInterrupt` gate (`Machine.cpp:694-698`), so ticks are **dropped while
masked** — a firmware that polls `MISC<ITINTR>` never sees it set → infinite
wait. Fix: latch unconditionally on the cycle edge; gate only the divert.

### 1.3 Device interrupts — `b_irq<1:0>`

HRM **[HRM]**: device asserts → Cchip latches in `DRIR` (raw). `DIMn` (per-CPU
device interrupt mask) selects which raw requests reach `b_irq<1>` (low 56 bits)
/ `b_irq<0>` (top bits). `DIRn` (RO) = `DRIR & DIMn` = the masked/effective
request.

AXPBox **[AXP]** (`interrupt(number>=0)` + re-eval block, `System.cpp:1769-1791`):
`assert` → `drir |= 1<<number`; then per CPU: `drir & dim[i] & 0x00ffffffffffffff`
→ `irq_h(1,true,100)` (device IRQs **delayed 100 clocks**), and
`& 0xfc00000000000000` → `irq_h(0,true,100)`. `csr_write` DIM (0x200/240/600/640)
stores `dim[]` (note: AXPBox stores DIM but does **not** re-run the eval there —
[?] re-eval happens on the next `interrupt()` call, a subtle ordering shortcut).

**V4 status / gap:** V4 models **none** of this — no `drir`, no `dim`-masking,
no device-interrupt assert path, no per-line delay, no MMIO→`interrupt()` hookup.
V4's only request line is `pendingIrq2` (the timer). #71 scope.

### 1.4 `irq_h` propagation-delay model

AXPBox `irq_h(number,assert,delay)` (`AlphaCPU.hpp:571`) **[AXP]**: `active =
eir bit set || irq_h_timer[number]`; assert-when-inactive with `delay>0` loads
`irq_h_timer[number]=delay` (counts down in `check_timers`, sets `eir` on expiry),
`delay==0` sets `eir` immediately. Models chipset→core latency (device 100,
timer 0) and debounces re-assertion. **[?]** Not in the HRM as a fixed clock
count — it's an AXPBox realism shortcut; V4 needs it only when device IRQs land.

---

## 2. Memory & configuration

### 2.1 Memory size is READ from a CSR, not probed  ← key finding

AXPBox `cchip_csr_read` case `0x100` (`System.cpp:1344`) **[AXP]**:
`return (iNumMemoryBits - 23) << 12;` — the firmware **reads** memory size from
this CSR; cases `0x140/0x180/0x1c0` return 0 ("all memory in a single array").
So AXPBox firmware never probes-until-fault to size RAM.

**V4 status / gap (high priority, #71):** V4's cl67 cold boot machine-checked at
`mm_stat=0x3fffffc` = exactly the 64 MiB top of `--mem` default, and *cleared*
when `--mem` was raised to 1 GiB. Strongly implies V4 does **not** tie this size
CSR to the configured `memSize`, so the firmware tested/touched memory past the
actual backing. Implementing CSR `0x100` to report `memSize` (encoded
`(log2(bytes)-23)<<12`) is the correct fix — more faithful than delivering an
MCHK for a probe that shouldn't happen. **[?]** Confirm the exact encoding and
which offset (`0x100` is the Cchip memory-presence/MPD-class register) against
HRM Ch.10 before binding.

### 2.2 Non-existent-memory error path (NXM)

HRM **[HRM]** (~Ch.6 / 10.2.2): an access to non-existent memory makes the Cchip
set `MISC<NXM>` and `DRIR<63>`, latch `MISC<NXS>` (the failing source), and raise
an **error interrupt** through the device-interrupt path — not a direct CPU
machine check. Cleared by W1C of `MISC<NXM>`.

**V4 status / gap:** V4 raised a raw `kFaultMachineCheck` for the OOB access
(task #72). The architecturally correct mechanism routes through the Cchip
(NXM → DRIR<63> → interrupt), i.e. #72 is really "NXM error-interrupt delivery
via the Cchip," a #71 sub-item — not a standalone CPU trap. Moot for the current
boot once memory size is correct, but required for faithful error modeling.

### 2.3 Other config CSRs

`CSC` (0x000) system config — clock ratios, CPU/array presence; firmware reads
it to learn topology **[HRM]**; AXPBox stores/masks it **[AXP]**. `MPRx`
(memory programming, WO), `MCTL`, `TTR`/`TDR` (TIGbus timing), `PWR` (power mgmt)
— **to profile.**

---

## 3. MMIO / DMA — Pchip (to expand)

Pchip CSRs (`pchip_csr_read`/`write`, `System.cpp:1227+`) **[AXP]**:
`WSBAn`/`WSMn`/`TBAn` (PCI DMA window base/mask/translated-base = PCI↔system
address translation), `PCTL`, `PERROR`/`PERRMASK`, `TLBIV`/`TLBIA` (Pchip DMA
scatter-gather TLB invalidate). This is the MMIO/DMA interface; **V4 models
essentially none of it** (no Pchip window translation, no PCI device→memory
path). Expand with per-register HRM §10.2.5 vs AXPBox behavior.

---

## 4. Dchip (to expand)

`DSC`/`DSC2` (system config, RO), `STR` (system timing), `DREV` (revision).
Mostly RO identity/config the firmware reads. Profile against HRM §10.2.4.

---

## 5. AXPBox global shortcuts (catalog)

- **ROM speed patches** (`LoadROM`, `System.cpp:1655-1662`) **[AXP]**: NOPs the
  cl67 memory-test routines (`0x8bb78/0x8bc0c/0x8bc94`, `BEQ r31,+0`) and a few
  spin loops. AXPBox reaches `>>>` partly by *skipping the memory test* V4 runs.
- **Decompression completion** = `get_clean_pc() < 0x200000` **[AXP]**: cl67/ES40
  specific (decompress lands low). Runs away on es45 (relocates to 0x600000).
- **Single memory array**: CSR `0x100` reports one array; `0x140/180/1c0` = 0.
- **icache default off**, **800 MHz default**, function-call (not per-instruction)
  trace, IDB build-time gated.
- **DIM stored without immediate re-eval** in `csr_write` (re-eval deferred to
  next `interrupt()`).

These are *expedients to reach a usable console fast*, not spec behavior. V4
should prefer the HRM mechanism (real memory-size CSR, real interrupt latch/
delivery) and treat AXPBox shortcuts as "known-good but lossy" reference points.

---

## 6. Implications for V4 `TsunamiChipset` (#71)

1. **Memory-size CSR ↔ `memSize`** (§2.1) — wire CSR `0x100` to report the
   configured guest RAM so the firmware sizes correctly; likely removes the need
   for MCHK-on-probe entirely. **Highest leverage.**
2. **Interrupt latch vs deliver** (§1.2, task #70) — latch `MISC<ITINTR>`/
   `pendingIrq2` unconditionally on the timer edge; gate only the divert.
3. **Device-interrupt path** (§1.3) — model `DRIR`/`DIMn`/`DIRn` + the
   `drir & dim → b_irq<1:0>` eval + MMIO→`interrupt()` hookup. Scope: minimal
   single-line vs full 6-line `eir`.
4. **NXM error path** (§2.2) — route non-existent-memory accesses through
   `MISC<NXM>`/`DRIR<63>` → error interrupt, not a raw CPU MCHK.
5. **Pchip DMA windows** (§3) — when PCI/MMIO devices are wired.

Sequencing: #70 (latch) is the quick win and first concrete piece of #71;
the memory-size CSR (item 1) is the next-highest leverage and directly explains
the cl67 64 MiB wall.

---

## 7. V4 Implementation Plan (design only — no code until reviewed)

Goal: build the HRM-authoritative chipset by completing V4's existing
`TsunamiChipset` and merging AXPBox's *integration semantics* (not its
structure) into V4's layering. **HRM = authority for what each register does;
AXPBox = proven reference for how the pieces couple.**

### 7.1 Architecture decision (locked)

Keep V4's layering: `Machine` owns `m_chipset` and `m_cpu`; the run loop
mediates. **The chipset never writes `CpuState` directly** — it exposes request
state; `Machine` reads it and decides delivery. Do NOT copy AXPBox's `CSystem`
god-class (state.cchip + direct `acCPUs[i]->irq_h()` reach-in). One-way data
flow: chipset → latched request state; Machine → deliver + (on handler entry)
W1C acknowledgement back into the chipset.

### 7.2 Interface contract (chipset ⇄ Machine)

Chipset (`TsunamiCchip`) exposes:
- Full CSR read/write with HRM-correct side effects (§1-2 tables): MISC W1S/W1C,
  DIM store + interrupt re-eval, DRIR latch, IICn suppression.
- `memorySize()` → CSR `0x100` reports `memSize` (§2.1).
- Request raise: `fireIntervalTimer()` (latch MISC<ITINTR> + EI[2] request,
  UNCONDITIONAL); `raiseDeviceInterrupt(n)`/`lower...` (set/clear DRIR bit,
  re-eval DRIR & DIM → EI[1]/EI[0]).
- Request query: `pendingInterrupts()` → the latched EI bitmask (the `eir`
  equivalent) for Machine to arbitrate.
- W1C acknowledge: MISC<ITINTR>/IPI clear deasserts the matching request line
  (extend existing `miscWriteW1C`).

Machine (run loop / delivery) owns:
- `canAcceptInterrupt(line)` = IER EIEN bit set **AND** IPL permits **AND NOT
  palMode** **AND** relocation done. (Add the missing PALmode gate — proven
  needed by the in-PALmode injection halt.)
- Per step: chipset latches the timer unconditionally; then if
  `(pendingInterrupts() & enabledLines)` and `canAcceptInterrupt` →
  `stageInterruptDivert` to the correct vector; latch persists until W1C.

### 7.3 Request-model decision (recommended)

Implement a **6-bit EI request latch (eir-equivalent)** now — cheap and
future-proof — driving delivery off `(eir & eien)` per HRM, even though only
EI[2] (timer) and EI[1] (device) are sourced initially. Avoids a second rework
when SMP/IPI (EI[3]) and device IRQs land. `pendingIrq2` becomes EI[2] of this
bitmask.

### 7.4 Interrupt vector mapping (HRM-confirm first) [?]

The stale injection used `palBase + 0x100` = DTBM_DOUBLE_3 — **wrong** for a
hardware interrupt. Determine the correct EV6 INTERRUPT PAL entry offset from
`coreLib/Ev6EntryVectors.h` / `EV6_DEFS.MAR` and route delivery there. Bind only
after HRM/defs confirmation (_PROVISIONAL until then).

### 7.5 NXM error path (replaces #72 raw-MCHK)

On out-of-range physical access: set `MISC<NXM>` + `DRIR<63>`, latch
`MISC<NXS>`, raise the error interrupt via the device path — not a raw
`kFaultMachineCheck`. Likely moot once the memory-size CSR is correct, but it is
the faithful mechanism.

### 7.6 TIGbus / DPR / TOY (demand-driven)

Verify-then-implement, NVRAM first: env-var store with sane defaults
(`auto_action = HALT` so the console lands cleanly at `>>>`); TOY returns a
valid host-derived time; flash window only if the console re-reads flash. Grep
the cl67 trace for accesses into these regions before building each.

### 7.7 Slice order (each independently buildable + cl67-validated)

1. **Timer latch + PALmode gate** (#70) — restructure `Machine` run loop. Validate: timer poll/wait no longer hangs.
2. **Memory-size CSR `0x100 ↔ memSize`** — Validate: correct size at smaller `--mem`; no probe-fault.
3. **6-bit EI request latch + device-interrupt (DRIR/DIM/DIRn) path + correct INTERRUPT vector.** Validate: device IRQ delivers.
4. **NXM error path** (replaces raw MCHK, #72).
5. **NVRAM env vars → TOY → flash** (demand-driven).

### 7.8 V4 conventions to honor

Include guards (not `#pragma once`); no `toString` clashes (use `<type>Name` +
`operator<<`); doctest `CHECK` only (no `REQUIRE`); ASCII(128) only;
documentation header + line on each touched file; surgical `Edit` over rewrite;
discuss-before-code / summarize diff before applying; `_PROVISIONAL` on any
guessed scbd/offset/bit until HRM-verified.

### 7.9 Open [?] items to HRM-confirm before binding dispatch

- INTERRUPT PAL entry offset (NOT `0x100`).
- CSR `0x100` memory-size encoding/offset.
- `IICn` semantics + any device-IRQ delivery delay.
- Exact `MISC` bit layout (ITINTR/IPINTR/IPREQ/NXM/NXS) vs HRM §10.2.2.3 —
  partially derived from AXPBox W1S/W1C masks; verify before binding.
