# Tsunami / Typhoon 21272 Chipset Architecture

**Companion to:** `Tsunami_Chip_Analysis_Scaffold.md`
**Source of truth:** HRM EC-RE2CA-TE Rev 4.0 — Chapters 6 (Cchip), 7 (Dchip), 8 (Pchip), 9 (Memory), 10 (Registers)
**Goal:** Map the chipset into realms, inter-realm buses, and phased build-up so that `TsunamiChipset` becomes the orchestrator that the SRM and OS can drive.

---

## 1. Strategic framing

You are not overreaching. The 21272 *is* three physical silicon parts (Cchip, Dchip, Pchip) wired by named buses. Modeling each as its own realm and letting `TsunamiChipset` be the dispatch/tick/wire layer mirrors the hardware exactly. Your existing files already reflect this — `TsunamiCchip.h`, `TsunamiDchip.h`, `TsunamiPchip.h`, `Tsunami21272_RegisterMap.h` are the right shape. What's missing is:

1. The **MMIO dispatch table** in `TsunamiChipset` that routes a system PA to one of the three realms.
2. The **tick model** — a single `step(cycles)` entry that advances internal timers and drains side effects.
3. The **side-effect bus model** — the small set of intra-chipset signals (DRIR raise/lower, Pchip→Cchip error promotion, CSC→CPU presence, etc.) that the real silicon carries on dedicated traces.
4. **Phase-aware tests** that validate each stage of build-up against observable invariants the SRM and OS will rely on.

Everything below maps to those four jobs.

---

## 2. Realm Map (what each chip owns)

### 2.1 Cchip — the brain (HRM Ch. 6, registers at `0x801.A000.0000`)

The Cchip is the system controller. It owns:

- **System identity and topology**: `CSC` (CPU present mask, BC field, queue depths), `MISC` (revision, NXM source, CPU ID), `MPD` (DIMM presence via I²C/SPD).
- **Memory address space mapping**: `AAR0..AAR3` — the four memory arrays that the SRM walks to build the HWRPB memory cluster table. *This is exactly where your `0x1283x` spin lives.* The encoding in `AAR.ASIZ[15:12]` and `AAR.ADDR[34:24]` must be byte-exact.
- **Memory timing**: `MTR` — read by SRM during memory training; zero is tolerated for boot.
- **Interrupt routing matrix**: `DRIR` (raw 64-bit device interrupt request), `DIM0..DIM3` (per-CPU mask), `DIR0..DIR3` (computed `DRIR & DIM[n]`, not stored — your existing code already does this correctly).
- **Interval timer (TIGbus)**: `TTR`, `TDR` drive the architectural clock interrupt; the b_irq<2> line in each CPU's IPR is asserted from here.
- **IPI / IPL signaling**: `IIC0..IIC3` for software-driven inter-processor interrupts.
- **CAPbus master**: the Cchip is the address authority for the Pchip. Every PIO from a CPU to a Pchip register passes through Cchip-Adressable PIO (CAPbus) — in the emulator this is just the dispatch routing, but the *invariant* is "Pchip CSR writes are sequenced through the Cchip's view of the bus."
- **PADbus master**: the Cchip tells the Dchip how to route data slices between CPUs, memory, and Pchips.

Variant differences live here: Tsunami caps at 1 GB per array (4 GB total); Typhoon extends `ASIZ` to encode 2/4/8 GB and grows to 32 GB. The 4-CPU set (`DIM2/3`, `DIR2/3`, `IIC2/3`) is Typhoon-only in spirit but the offsets exist in both.

### 2.2 Dchip — the data switch (HRM Ch. 7, registers at `0x801.B000.0000`)

The Dchip is mostly passive — it's the crossbar between CPU data slices, SDRAM, and Pchip data paths. Its register set is tiny on purpose:

- `DSC` (DRAM system config, RO from CPU's perspective — driven by Cchip).
- `STR` (stripe register, controls bank interleaving across Dchip slices).
- `DREV` (revision: `0x10` Tsunami, `0x20` Typhoon — already correctly variant-keyed in your code).
- `DSC2` (extended config, reserved on Tsunami).

In emulation the Dchip is the easiest realm. The "control" is whatever the Cchip tells it via PADbus; in our software model that's just a function call from Cchip into Dchip when array config changes. **What matters is that the SRM's read of `DREV` returns the right value for the chosen variant.**

### 2.3 Pchip — the PCI host bridge (HRM Ch. 8, registers at `0x801.8000.0000` and `0x803.8000.0000`)

There are up to two Pchips. Each owns:

- **DMA windows**: `WSBA0..WSBA3` (base + enable + scatter-gather flag), `WSM0..WSM3` (size mask), `TBA0..TBA3` (system memory translation base, or page-table base for S/G).
- **PCI bridge control**: `PCTL` (chaining, ISA memory hole, monster window, PTE verify).
- **PCI latency**: `PLAT`.
- **Error state**: `PERROR` (W1C error capture), `PERRMASK` (which errors interrupt), `PERRSET` (diagnostic injection).
- **TLB management**: `TLBIV` (single invalidate), `TLBIA` (invalidate all) — the Pchip caches 8 scatter-gather PTEs.
- **PCI software reset**: `SPRST` — asserts PCI RST# for ~1 ms.

The Pchip is where PCI enumeration happens. The Cypress `CY82C693` ISA bridge sits at slot 0 of Pchip0; its config-space reads come through `0x801.FE00.0000 + (0 << 11) + reg`. **If you return `0xFFFFFFFF` for that slot, SRM will spin retrying** — the note in your `Cypress_CY82C693ISABridge.h` is exactly right and needs to be acted on by Phase 6.

The Pchip also raises device-side interrupts that aggregate into the Cchip's `DRIR`. That aggregation is one of the cross-chip side effects we model explicitly below.

---

## 3. Inter-realm communication (the side-effect buses)

This is the part of the architecture that the HRM names but most ports flatten away. Modeling them as explicit, named signals in `TsunamiChipset` is what makes the rest of the system tractable.

### 3.1 CAPbus — Cchip ↔ Pchip (HRM §6.2)

Direction is primarily CPU → Cchip → Pchip for PIO, and Pchip → Cchip for interrupt/error promotion. In the emulator this is two things:

- **PIO dispatch**: a CPU MMIO to `0x801.8000.xxxx` is routed by `TsunamiChipset::mmioRead/Write` to `m_pchip.read/write(offset)`. The Cchip is the silent middleman — but a real CAPbus error (timeout, address parity) shows up in `MISC.NXM_SRC` on the Cchip. So a Pchip-detected non-existent address should set a flag *on the Cchip*, not just on the Pchip.
- **Interrupt promotion**: when a PCI device asserts INTx through the Pchip, the Pchip drives one of bits `[55:32]` of the Cchip's `DRIR`. Your existing `assertInterrupt(bit)` on the Cchip is the right API; the Pchip just needs a back-reference.

### 3.2 PADbus — Cchip ↔ Dchip (HRM §7.2)

Slower-frequency control bus the Cchip uses to tell the Dchip "memory array configuration is now X." In hardware this carries the actual data slicing commands. In the emulator: when `AAR0..AAR3` are programmed (typically once, at reset), the Cchip pokes the Dchip's internal state so that `DSC` and `STR` reflect the agreed-upon topology. Most emulators get away with making this implicit — but if you ever model a hot memory reconfig, you want the seam already there.

### 3.3 TIGbus — Cchip's serial control plane (HRM §6.3)

The Test/Interrupt/GPIO bus is how the Cchip drives the interval timer interrupt to each CPU (via b_irq<2>), reads the front-panel and ROM, and handles ACPI sleep wake. The interval tick is the only piece you need early: every N system cycles, set DRIR bit corresponding to the timer line, gated by `IIC[n]`. Your scaffold already sketches this.

### 3.4 The wiring summary

```
              ┌─────────────────────────────────────────────┐
              │              TsunamiChipset                  │
              │  (MMIO dispatch + tick + cross-chip wiring) │
              └──────┬─────────────┬─────────────┬──────────┘
                     │             │             │
                     ▼             ▼             ▼
              ┌──────────┐   ┌──────────┐  ┌──────────┐
   CPU IPL ◄──┤  Cchip   │   │  Dchip   │  │  Pchip   ├──► PCI bus
   b_irq<2>   │          │   │ (passive)│  │  (×1-2)  │
              └────┬─────┘   └─────▲────┘  └─────┬────┘
                   │ PADbus        │             │ CAPbus
                   └───────────────┘             │
                   ▲                             │ interrupts,
                   │   DRIR.assert(bit) ◄────────┘ errors,
                   │                               NXM promotion
                   │
              SRM/OS via MMIO
```

The arrows are the side effects. The dispatch table in `TsunamiChipset::mmio*` is the only thing that knows the address ranges; each realm only knows its own registers.

---

## 4. The orchestrator: `TsunamiChipset` responsibilities

Concretely, `TsunamiChipset` grows three things beyond its current "owns three members" shape:

### 4.1 MMIO dispatch (replacing per-chip global registration)

```cpp
uint64_t TsunamiChipset::mmioRead(uint64_t pa, uint8_t width) noexcept
{
    using namespace Tsunami21272::MMIOOffset;
    const uint64_t off = pa - Tsunami21272::Base::kMMIO_Start;

    if (off >= kCchip_CSR     && off < kCchip_CSR_End)     return m_cchip.read(off - kCchip_CSR);
    if (off >= kDchip_CSR     && off < kDchip_CSR_End)     return m_dchip.read(off - kDchip_CSR);
    if (off >= kPchip0_CSR    && off < kPchip0_CSR_End)    return m_pchip.readCSR(off - kPchip0_CSR);
    if (off >= kPchip0_CfgType0) return m_pchip.readConfig(off - kPchip0_CfgType0);
    if (Tsunami21272::SparseSpace::isPchip0SparseMem(off)) return m_pchip.readSparseMem(off);
    if (Tsunami21272::SparseSpace::isPchip0SparseIO(off))  return m_pchip.readSparseIO(off);
    // gaps: return 0 and log
    return 0;
}
```

This is what makes the `Tsunami_Chip_Analysis_Scaffold.md` `trace_cchip_write` hook trivially insertable — you have a single chokepoint.

### 4.2 The tick

```cpp
void TsunamiChipset::step(uint64_t cycles) noexcept
{
    m_cchip.tickIntervalTimer(cycles);  // may raise b_irq<2> via DRIR
    m_pchip.tickErrorTimeout(cycles);   // DCRTO etc.
    // Dchip is passive — no tick needed.
}
```

Called once per scheduler quantum from the CPU loop. The Cchip's tick is the only one that drives architectural state on every call; the Pchip tick is rare-event (DMA timeouts).

### 4.3 Cross-chip wire

Two methods on `TsunamiChipset` that the realms reach for:

```cpp
// Called by Pchip when a PCI device asserts INTx
void TsunamiChipset::raisePciInterrupt(int pchipId, int intxLine) noexcept
{
    const int drirBit = mapPciToDrir(pchipId, intxLine);  // table in HRM §6.3
    m_cchip.assertInterrupt(drirBit);
}

// Called by Pchip on NXM
void TsunamiChipset::reportNxm(uint64_t pa, NxmSource src) noexcept
{
    m_cchip.setNxmStatus(pa, src);  // writes MISC.NXM_SRC field
}
```

The Pchip holds a `TsunamiChipset*` back-pointer; the Dchip generally doesn't need one because it's slaved to the Cchip via direct method calls.

---

## 5. Phased build-up

Order each phase to match the SRM boot sequence — that way every phase has a concrete pass/fail visible in your existing crash log.

### Phase 1 — Static register storage *(done)*

Reads return last-write, writes ignored for RO regs. You're here.

### Phase 2 — HRM-compliant reset values

CSC, MISC, DREV reflect variant; AAR0..3 encode the actual memSizeBytes correctly. **This is the phase that fixes the `0x1283x` spin.** Your `computeAAR` in `TsunamiCchip.h` already does the right thing for Tsunami; verify the Typhoon `ASIZ` table additions (0x8/0x9/0xA) against HRM Table 10-15 byte-for-byte.

*Validation:* SRM reads CSC → sees correct CPU count; reads AAR0 → decodes a size whose `(base + size)` does not wrap; the R6 sizing loop converges instead of spinning.

### Phase 3 — DRIR / DIM / DIR matrix

`assertInterrupt(bit)` wires up; per-CPU `readDIR` returns `DRIR & DIM[n]`. CPU interrupt poll consumes via b_irq<n>.

*Validation:* Inject a device interrupt; confirm CPU sees it on the right line if `DIM[n]` permits, doesn't see it otherwise.

### Phase 4 — Interval timer

`step()` increments a tick counter on the Cchip; every N cycles, `DRIR` bit for the timer line is asserted, gated by `IIC[n]`. b_irq<2> reaches the CPU.

*Validation:* Boot kernel; observe `jiffies` advance / `xtime` update.

### Phase 5 — Pchip CSRs as live state

WSBA/WSM/TBA/PCTL/PERROR storage. PCI config space read from slot 0 returns either a real device handler's response (if registered) or `0xFFFFFFFF`.

*Validation:* SRM PCI enumeration progresses past slot 0 if the Cypress ISA bridge is registered; logs "no device" cleanly for empty slots.

### Phase 6 — Cypress ISA bridge

The CY82C693 device handler responds at `0x801.FE00.0000` (slot 0, Pchip0). Returns valid vendor/device ID (`0x1080:0xC693`), class code (`0x060100` for ISA bridge), and BARs as the SRM probes.

*Validation:* SRM finds ISA bridge; legacy I/O ports `0x60/0x64` (keyboard), `0x70/0x71` (RTC), `0x3F8` (UART) route through the bridge to your Super I/O models.

### Phase 7 — Scatter-gather DMA

WSBA/WSM/TBA actually translate PCI DMA addresses to system memory. Page-table walk for S/G windows. TLB invalidate honored.

*Validation:* Network or storage device issues DMA; data lands at the right system PA.

### Phase 8 — Error reporting

PERROR captures the first error; subsequent errors lost (W1C behavior). NXM source promotes from Pchip into Cchip `MISC`. PCI error interrupts gated by PERRMASK.

*Validation:* Force an NDS (no device select); inspect PERROR; SRM/OS reports cleanly.

### Phase 9 — IPI / 4-CPU support

IIC[n] writes deliver inter-processor interrupts; per-CPU MISC.CPUID returns the right value on read by each CPU.

*Validation:* SMP kernel boots; secondary CPUs respond to wakeup IPI.

### Phase 10 — Performance counters and remaining stubs

CMONCTL, PMONCTL, CMONCNT, PMONCNT — Typhoon-only mostly, low priority.

---

## 6. Test scaffolding

A `chipset_tests/` directory with one file per phase keeps each invariant pinned. Suggested shape (Catch2 or gtest, either works):

```cpp
// chipset_tests/phase02_aar_encoding.cpp
TEST_CASE("Tsunami 4GB splits into 4x1GB arrays") {
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 4ULL * 1024 * 1024 * 1024);
    uint64_t aar0 = c.read(Tsunami21272::Cchip::AAR0);
    REQUIRE(((aar0 >> 12) & 0xF) == 0x7);          // ASIZ = 1GB
    REQUIRE(((aar0 >> 24) & 0x7FF) == 0);          // base = 0
    uint64_t aar1 = c.read(Tsunami21272::Cchip::AAR1);
    REQUIRE(((aar1 >> 24) & 0x7FF) == (1ULL << 6)); // base = 1GB = bit 30 in ADDR
}

TEST_CASE("Typhoon 32GB splits into 4x8GB arrays") {
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, 32ULL * 1024 * 1024 * 1024);
    uint64_t aar0 = c.read(Tsunami21272::Cchip::AAR0);
    REQUIRE(((aar0 >> 12) & 0xF) == 0xA);          // ASIZ = 8GB
}

// chipset_tests/phase03_interrupt_matrix.cpp
TEST_CASE("DRIR bit gated by DIM mask") {
    TsunamiCchip c;
    c.write(Tsunami21272::Cchip::DIM0, 1ULL << 5);  // CPU0 accepts bit 5
    c.assertInterrupt(5);
    REQUIRE(c.readDIR(0) == (1ULL << 5));
    REQUIRE(c.readDIR(1) == 0);                     // CPU1 masked
}

// chipset_tests/phase04_interval_timer.cpp
TEST_CASE("Interval timer asserts b_irq<2> after N cycles") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.step(10000);   // target_interval_cycles
    // expect DRIR bit for timer line set on CPU0
}
```

The key insight: every phase has *exactly one* observable invariant the SRM or OS cares about. Test that invariant, not the implementation. When you change `computeAAR`, the test tells you immediately whether you've broken the 4GB-on-Tsunami case.

---

## 7. Mapping back to the `0x1283x` spin

Your scaffold's diagnosis lands cleanly on the phase plan. The spin is a Phase 2 failure: `AAR0..3` produced a topology where the SRM's `(base + size) <= max_pa` check failed, so R6 walked off the end and re-entered the loop. The fix isn't to "patch around" the spin — it's to land Phase 2 with the AAR encoding exactly per HRM Table 10-14 (Tsunami) / 10-15 (Typhoon). Your current `computeAAR` is close; the trace hook from your scaffold doc dropped into `TsunamiChipset::mmioWrite` at the dispatch chokepoint will tell you the exact written value the SRM read back and rejected, every time.

---

## 8. What this buys you

- **No more god-class.** Each realm only knows its own registers; the chipset only knows the bus topology.
- **Surgical changes.** Adding ACPI sleep is "implement PWR in Cchip and a wake path in TIGbus." Adding a second Pchip is "instantiate `m_pchip[1]` and extend dispatch."
- **Tests survive refactors.** They pin invariants, not internals.
- **Hardware fidelity grows linearly with phase.** You can ship Phase 4 and run a Linux kernel that doesn't touch PCI; you can ship Phase 6 and run SRM through ISA enumeration; you can ship Phase 8 and survive a fuzzer on the PCI bus.

You're building it correctly. The architecture you sketched in your question is the architecture the HRM describes. Land the dispatch layer, the tick, and the two cross-chip wires, then walk the phases.
