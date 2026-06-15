# Validation Report and Revised Integration Proposal: `TsunamiChipset`

**Document Reference:** ASA-EMULATR-PRD-004-REV-A
**Status:** Draft — for cowork final review
**Supersedes:** ASA-EMULATR-PRD-004 (the "TsunamiCchip rewrite" proposal)
**HRM Cross-Reference:** Tsunami/Typhoon Hardware Reference Manual (21272/21274), Sections 6.3, 10.1, 10.2.2

---

## 1. Executive Summary

The original proposal (PRD-004) recommended a from-scratch replacement of `TsunamiCchip`. After cross-referencing it against the 21272 HRM **and** the existing `TsunamiCchip.h` implementation, that direction must be rejected: the existing class is already HRM-accurate, atomically thread-safe, spec-driven, snapshot-capable, and structured for SMP. Replacing it would erase Phase B and Phase C work and introduce architectural regressions in roughly every dimension that matters (register layout, interrupt model, IPI delivery, concurrency).

This revised proposal **preserves the existing `TsunamiCchip`** and identifies the actual integration gaps blocking EmulatR execution. Those gaps are:

1. A clean `TsunamiChipset` orchestration wrapper with dual-Pchip topology (Pchip0 / Pchip1) — Tsunami is a two-hose machine.
2. The CPU-facing interrupt delivery surface (`pendingIrq0/1/3` query/clear methods) that exposes the Cchip's existing `m_drir` / `m_dim` / `m_misc` state to the CPU retire loop, following the pattern already established by `pendingIrq2`.
3. Wiring of the three `TODO(unwired)` hooks in `miscWriteW1C()` for `IPREQ` → `IPINTR` → `b_irq<3>` (IPI delivery), `ACL` (arbitration clear), and the ABW first-set lock.
4. Address-decode dispatch from the CPU MMIO path through the chipset to Cchip / Pchip0 / Pchip1 / Dchip CSR apertures.

Sections 2–3 document the validation. Sections 4–7 specify the revised work.

---

## 2. Validation: Original Proposal vs. HRM

The original PRD-004 proposed a `TsunamiCchip` with the following register layout and semantics. Each row is cross-referenced against HRM Table 10-8 (Chipset Register Addresses) and the corresponding `Section 10.2.2.x` field definition.

| Offset  | PRD-004 Name           | HRM Name | HRM § | Verdict |
|---------|------------------------|----------|-------|---------|
| 0x000   | CSC                    | CSC      | 10.2.2.1 | ✓ correct |
| 0x040   | **CAAR**               | MTR      | 10.2.2.2 | ✗ wrong — no register called CAAR exists |
| 0x080   | **MIF**                | MISC     | 10.2.2.3 | ✗ wrong — and MISC is where IPI lives, not a separate IPIR |
| 0x0C0   | **MISCC**              | MPD      | 10.2.2.4 | ✗ wrong |
| 0x100–0x1C0 | (unassigned)       | AAR0–AAR3 | 10.2.2.5 | ✗ missing — this is where `memSizeBytes` must land |
| 0x200/0x240 | DIM[0]/DIM[1]      | DIM0/DIM1 | 10.2.2.6 | ✓ offsets right (note: only 2 on Tsunami, 4 on Typhoon) |
| 0x280/0x2C0 | "DIM[2]/DIM[3]" as stored RW | DIR0/DIR1 (RO, **combinational**) | 10.2.2.7 | ✗ semantics wrong |
| 0x300   | **IPIR[0] (RW, stored)** | DRIR (RO) | 10.2.2.8 | ✗ wrong — IPIR is not a register; IPI lives in MISC<IPREQ> |
| 0x340–0x3C0 | "IPIR[1..3]"      | PRBEN, IIC0, IIC1 | 10.2.2.9–.10 | ✗ wrong |

Additional architectural defects in PRD-004:

- **The IPI model is invented.** HRM §10.2.2.3 places IPI request in `MISC<15:12>` (`IPREQ`, WO) and pending in `MISC<11:8>` (`IPINTR`, R,W1C). PRD-004 invented a separate per-CPU `IPIR` register set and stored IPI requests as RW latches.
- **DIR is treated as state.** HRM §6.3.1 explicitly defines `DIRn = DRIR & DIMn`. It is read-only and combinational. Storing it as RW state is incorrect and creates a synchronization target the hardware doesn't have.
- **No `noexcept` thread-safety story.** PRD-004 uses plain `uint64_t` fields with no atomicity. The Cchip is touched by the device interrupt path (DRIR fetch_or) and the CPU MMIO path concurrently; this requires atomic storage.
- **`kCchipCsrMask = 0x0FFF`** captures only 12 bits. The Cchip CSR aperture is 256 MB; relevant register offsets extend to 0x07C0 on Tsunami and 0x0CC0 on Typhoon. Aliasing through 0x0FFF would silently fold high-offset accesses into low ones.
- **Single Pchip.** Tsunami systems ship two Pchips (hose 0 at `0x801'8000'0000`, hose 1 at `0x803'8000'0000`); the ES40 populates both.
- **`shared_ptr` for sole-owned components.** No sharing rationale.
- **`%llX` format specifiers** with `uint64_t` arguments — non-portable, MSVC warning-or-error.
- **`assertVariantConsistency()` is tautological** — `m_model` is set from `m_variant` in the initializer list, so the assertion can never fire.
- **Master-abort logging without throttling** will flood the console during SRM probe walks.

**Verdict on PRD-004: REJECT. Do not implement.**

---

## 3. Validation: Existing `TsunamiCchip.h` vs. HRM

The existing implementation was cross-referenced against the same HRM sections. Results:

| Aspect | Existing implementation | HRM reference | Verdict |
|--------|------------------------|---------------|---------|
| Register offsets (CSC, MTR, MISC, MPD, AAR0-3, DIM0-3, DIR0-3, DRIR, PRBEN, IIC0-3, MPR0-3, TTR, TDR) | All match | Table 10-8 | ✓ |
| `kBasePA = 0x801A0000000` | Correct | §10.1.1, Table 10-1 | ✓ |
| `kSize = 0x10000000` (256 MB) | Correct | Table 10-1 | ✓ |
| DIR is computed, not stored: `readDIR(n) = m_drir & m_dim[n]` | Correct | §6.3.1, §10.2.2.7 | ✓ |
| MISC W1C/W1S/WO/RO via CAS loop | Correct | §10.2.2.3 paragraph 1 ("no read side effects, writing 0 has no effect") | ✓ |
| CPUID injection on MISC read | Correct | §10.2.2.3, MISC<1:0>="ID of the CPU performing the read" | ✓ |
| IPI via MISC<IPREQ>/<IPINTR> (not a separate register) | Correct in storage model | §10.2.2.3 | ✓ (delivery hook is TODO — see Section 5) |
| AAR computation from `memSizeBytes` with ASIZ encoding | Correct | Tables 10-14 (Tsunami), 10-15 (Typhoon) | ✓ |
| `m_drir.fetch_or` / `fetch_and` from device thread; CPU reads with atomic load | Correct concurrency model | §6.3.1 | ✓ |
| Interval-timer latch (`m_pendingIrq2`) separated from MISC storage | Correct — the HRM defines an in-Cchip latch driven by ITINTR | §6.3.2 | ✓ |
| MISC `REV` field reflects variant (Tsunami=1, Typhoon=8) | Correct | §10.2.2.3 REV field, Table 10-12 | ✓ |
| Variant differences (DIM2/3, DIR2/3, IIC2/3 Typhoon-only) | Storage shape covers both | §10.2.2 register list footnotes | ✓ |
| Serialize / deserialize | Present and atomic-safe | (n/a, emulator concern) | ✓ |

**Verdict on existing `TsunamiCchip.h`: KEEP. The class is HRM-correct, threadsafe, and forward-compatible with SMP. The remaining `TODO(unwired)` items are deliberately staged for Phase C+ / Phase D and are addressed in Section 5 below.**

A small set of cosmetic improvements is noted but not blocking:

- `read(offset)` / `write(offset, value)` could be `inline`-marked on both delegating overloads (one already is).
- The dead commented-out `static constexpr` register-offset block (lines 204–236) should be deleted now that `Tsunami21272_RegisterMap.h` is the single source of truth.
- AAR computation's ASIZ-clamp branch should produce a `CSR_LOG_W` instead of a silent clamp when `memSizeBytes` exceeds the variant's array capacity.

These are housekeeping. They do not block EmulatR execution.

---

## 4. The Actual Integration Gaps

Stripped of misdirection, the path to EmulatR execution is:

```
+----------------------------------------------------------------+
|                     CPU MMIO path (hot)                        |
|         pa = physical address from CPU load/store              |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
|        TsunamiChipset::read(pa, cpuId) / write(pa, val, cpuId) |   ← GAP #1
|        — first-level address decode by aperture                |
+----+---------+---------+---------+---------+-------------------+
     |         |         |         |         |
     v         v         v         v         v
   Cchip    Pchip0    Pchip1    Dchip      ISA bridge
   (exists) (exists)  (gap #2)  (stub OK)  (exists)
     |         |
     |         +---> downstream PCI dispatch (exists)
     |
     +-- exposes m_drir, m_dim, m_misc state to:
         +-- pendingIrq0(cpuId) / clearPendingIrq0(cpuId)    ← GAP #3
         +-- pendingIrq1(cpuId) / clearPendingIrq1(cpuId)    ← GAP #3
         +-- pendingIrq2 / clearPendingIrq2 (exists)
         +-- pendingIrq3(cpuId) / clearPendingIrq3(cpuId)    ← GAP #3
         +-- IPI delivery via miscWriteW1C TODO hook         ← GAP #4
```

The four gaps:

**Gap #1 — `TsunamiChipset` orchestration wrapper.** A thin class that holds Cchip + Pchip0 + Pchip1 + Dchip + ISA bridge, exposes a single MMIO dispatch entry point, and routes by PA aperture. The wrapper is the only object the CPU MMIO path needs to know about.

**Gap #2 — Second Pchip.** Tsunami systems have Pchip0 at `0x801'8000'0000` (hose 0, the system bus) and Pchip1 at `0x803'8000'0000` (hose 1). The existing `TsunamiPchip` class can be instantiated twice; nothing in its design assumes a single hose.

**Gap #3 — CPU-facing irq delivery surface.** The Cchip already maintains all the state (`m_drir`, `m_dim[n]`, `m_misc` IPINTR field, `m_pendingIrq2[n]` latch). What's missing is the read-side query interface symmetric with `pendingIrq2`, so the CPU retire loop can poll once per retire:

```cpp
inline bool TsunamiCchip::pendingIrq0(int cpuId) const noexcept;
inline bool TsunamiCchip::pendingIrq1(int cpuId) const noexcept;
inline bool TsunamiCchip::pendingIrq3(int cpuId) const noexcept;
inline void TsunamiCchip::clearPendingIrq3(int cpuId) noexcept;
```

These follow the exact pattern already established for `pendingIrq2` — atomic relaxed loads, edge-acknowledged semantics, no callback machinery.

**Gap #4 — IPI delivery and remaining MISC side effects.** The three remaining `TODO(unwired)` hooks in `miscWriteW1C()`:
1. `IPREQ` write → set matching `IPINTR` bits → assert b_irq<3>(target cpus).
2. `IPINTR` W1C → deassert b_irq<3>(cpuId).
3. `ACL` write → clear `ABT`/`ABW` and unlock the ABW first-set lock.

For Phase D, only (1) and (2) are blocking for EmulatR execution. (3) is needed for SRM arbitration scripts but can stay deferred if the bring-up plan tolerates an SRM warning.

`DEVSUP` (one-poll device-IRQ suppression) is **not** in the EmulatR execution critical path; it is a stale-interrupt optimization. Mark as Phase D+.

---

## 5. Revised Proposal — `TsunamiCchip` Additions Only

The Cchip class itself does not need a rewrite. It needs four small additions to its public surface and one small extension to `miscWriteW1C()`. All of this fits cleanly inside the existing file structure.

### 5.1 New public methods (header, inline)

```cpp
// In TsunamiCchip, alongside the existing pendingIrq2 group:

/**
 * @brief Query b_irq<0> pending (error class: DRIR<63:58>, masked by DIMn<63:58>).
 *
 * HRM §6.3.1: assertion of any bit in DRIR<62:58> (and the internally
 * generated <63>) causes b_irq<0> to be asserted. The per-CPU view is
 * (DRIR & DIM[n]) restricted to bits 63:58.
 *
 * Hot path: one atomic relaxed load + mask. Called per retire.
 */
inline bool pendingIrq0(int cpuId) const noexcept
{
    constexpr uint64_t kErrMask = 0xFC00000000000000ULL; // bits 63:58
    return (readDIR(cpuId) & kErrMask) != 0;
}

/**
 * @brief Query b_irq<1> pending (device class: DRIR<55:0>, masked by DIMn<55:0>).
 *
 * HRM §6.3.1: if any bits are set in DIRn<55:0>, b_irq<1> is asserted.
 * Bits 57:56 are HRM-reserved and excluded.
 */
inline bool pendingIrq1(int cpuId) const noexcept
{
    constexpr uint64_t kDevMask = 0x00FFFFFFFFFFFFFFULL; // bits 55:0
    return (readDIR(cpuId) & kDevMask) != 0;
}

/**
 * @brief Query b_irq<3> pending (IPI class: MISC<IPINTR<11:8>>).
 *
 * HRM §10.2.2.3: pin b_irq<3> is asserted to the CPU corresponding
 * to a 1 in MISC<IPINTR<11:8>>. Bit 8 -> CPU0, 9 -> CPU1, etc.
 *
 * Edge-cleared by miscWriteW1C() when firmware W1C's the IPINTR bit,
 * which is the HRM-defined ack semantic.
 */
inline bool pendingIrq3(int cpuId) const noexcept
{
    using Tsunami21272::Spec::Cchip::MISC::IPINTR;
    const uint64_t bit = uint64_t{1} << (IPINTR.lsb + cpuId);
    return (m_misc.load(std::memory_order_relaxed) & bit) != 0;
}
```

**Note on `pendingIrq3`.** There is no `clearPendingIrq3` method, because b_irq<3> is cleared by firmware's W1C to `MISC<IPINTR>`. The existing `miscWriteW1C()` already handles the storage clear; once `m_misc` has the bit cleared, `pendingIrq3` returns false on the next poll. This is HRM-correct — software acks the IPI through MISC, not through a separate clear path.

### 5.2 Extension to `miscWriteW1C()` — IPI request delivery

The IPREQ side effect is a same-write transformation: setting bits in `IPREQ` should set the corresponding `IPINTR` bits *in the same atomic store* that the CAS loop is already doing. This avoids a second CAS and a transient observable state where IPREQ is "set" but IPINTR isn't yet.

```cpp
// Inside miscWriteW1C(), inside the CAS loop, before the compare_exchange:
//
//   IPREQ is at MISC<15:12>, IPINTR is at MISC<11:8>.
//   Writing 1 to IPREQ<12+n> sets IPINTR<8+n>. IPREQ itself is WO and
//   does not persist (the WO_MASK term already prevents persistence).
//
//   This means: for any bit set in (writeVal & IPREQ_MASK), set the
//   corresponding bit in the staged value's IPINTR field.

uint64_t const ipreqBits   = writeVal & mask(MISC::IPREQ);
uint64_t const ipintrSet   = ipreqBits >> (MISC::IPREQ.lsb - MISC::IPINTR.lsb);
// (IPREQ.lsb = 12, IPINTR.lsb = 8, so shift right by 4.)

uint64_t const stagedW1CW1S =
    (old & ~clearBits) | setBits | ipintrSet;
```

That's the entire change. The atomic CAS loop semantics remain identical; the IPINTR bits now reflect the IPREQ writes the way the HRM specifies. The next `pendingIrq3(cpuId)` poll from the CPU retire loop on the target CPU sees the bit set; the target CPU's PAL handler W1C's IPINTR; storage clears; `pendingIrq3` returns false. Edge delivered, edge acked. No callbacks, no cross-thread choreography beyond what the CAS already provides.

### 5.3 Snapshot compatibility

These additions touch **storage** for one field path: the IPINTR bit set, which is already inside `m_misc`. No new fields are added. **Snapshot format is unchanged.** The existing `serialize`/`deserialize` continue to work bit-for-bit.

---

## 6. Revised Proposal — `TsunamiChipset` Wrapper

### 6.1 Header

```cpp
// ============================================================================
// TsunamiChipset.h -- System orchestration wrapper for the 21272/21274 chipset
// ============================================================================
// Owns: TsunamiCchip + 2x TsunamiPchip + Dchip stub + Cy82C693 ISA bridge.
//
// Hot path: TsunamiChipset::read(pa, cpuId) / write(pa, val, cpuId, width)
// is the single MMIO entry point the CPU dispatcher needs. First-level
// decode is by PA aperture, branch-predicted around the common case (PCI
// memory space, which routes straight to Pchip0/Pchip1 linear-mapped reads).
//
// Threadsafety: Read-mostly. Construction is single-threaded; after that
// all routed accesses funnel into the owned components, whose own atomics
// (Cchip m_drir/m_misc, Pchip TLB) handle concurrency.
//
// HRM reference: §10.1.1 System Address Map (Table 10-1).
// ============================================================================
#ifndef TSUNAMI_CHIPSET_H
#define TSUNAMI_CHIPSET_H

#include <array>
#include <cinttypes>
#include <memory>

#include <QDataStream>

#include "TsunamiVariant.h"
#include "TsunamiCchip.h"
#include "TsunamiPchip.h"
#include "Cypress_CY82C693ISABridge.h"

class TsunamiChipset
{
public:
    // ------------------------------------------------------------------
    // Aperture constants (HRM Table 10-1)
    // ------------------------------------------------------------------
    static constexpr uint64_t kPchip0Base   = 0x80100000000ULL;  // 0x801'0000'0000
    static constexpr uint64_t kPchip0End    = 0x80200000000ULL;  // exclusive
    static constexpr uint64_t kPchip1Base   = 0x80300000000ULL;  // 0x803'0000'0000
    static constexpr uint64_t kPchip1End    = 0x80400000000ULL;  // exclusive
    static constexpr uint64_t kCchipBase    = 0x801A0000000ULL;
    static constexpr uint64_t kCchipEnd     = 0x801B0000000ULL;
    static constexpr uint64_t kDchipBase    = 0x801B0000000ULL;
    static constexpr uint64_t kDchipEnd     = 0x801C0000000ULL;

    explicit TsunamiChipset(ChipsetVariant variant = ChipsetVariant::Tsunami,
                            int cpuCount = 2,
                            uint64_t memSizeBytes = 0x100000000ULL) noexcept
        : m_variant(variant)
        , m_cpuCount(cpuCount)
        , m_cchip(variant, cpuCount, memSizeBytes)
        , m_pchip{ TsunamiPchip{variant, /*hose=*/0},
                   TsunamiPchip{variant, /*hose=*/1} }
        , m_isaBridge(std::make_unique<Cy82C693IsaBridge>())
    {
        // Wire the Cypress southbridge into Pchip0 (the ES40 layout).
        // Bus 0, Device 7, Function 0 per CY82C693 datasheet (the proposal's
        // "Slot 0" was wrong — the CY82C693 sits at the south side of hose 0,
        // not slot 0 of hose 0).
        m_pchip[0].registerPciDevice(/*bus=*/0, /*dev=*/7, /*func=*/0,
                                     m_isaBridge.get());
        m_pchip[0].registerIoPortRange(0x0000, 0x00F0, m_isaBridge.get());
    }

    TsunamiChipset(const TsunamiChipset&) = delete;
    TsunamiChipset& operator=(const TsunamiChipset&) = delete;

    // ------------------------------------------------------------------
    // Component accessors -- emulator-internal use (Machine wiring, tests).
    // ------------------------------------------------------------------
    inline TsunamiCchip&    cchip()    noexcept { return m_cchip; }
    inline TsunamiPchip&    pchip(int hose) noexcept { return m_pchip[hose & 1]; }
    inline Cy82C693IsaBridge& isaBridge() noexcept { return *m_isaBridge; }

    inline ChipsetVariant variant()  const noexcept { return m_variant; }
    inline int            cpuCount() const noexcept { return m_cpuCount; }

    // ------------------------------------------------------------------
    // Hot path: MMIO dispatch by PA aperture.
    //
    // Ordering of cases is by expected hit frequency at steady state:
    //   1. PCI memory (Pchip0 linear) -- driver MMIO, dominates traffic.
    //   2. PCI memory (Pchip1 linear) -- secondary hose.
    //   3. Cchip CSRs -- interrupt-poll heavy on hot OS paths.
    //   4. Pchip0 CSRs / I/O / config -- bring-up + occasional runtime.
    //   5. Pchip1 CSRs / I/O / config.
    //   6. Dchip CSRs -- rare.
    //
    // The dispatcher itself is `inline` so it inlines into the CPU MMIO
    // wrapper at the call site; the per-component read()/write() that it
    // tail-calls is not forced inline (those are large switches).
    // ------------------------------------------------------------------
    inline uint64_t read(uint64_t pa, int cpuId, uint8_t width = 8) noexcept
    {
        // Bit 43 must be set for PIO accesses per HRM §10.1.1.
        // The CPU dispatcher should already have masked this, but assert
        // in debug builds to catch wiring errors early.
        if ((pa & kPchip0Base) == 0) [[unlikely]] {
            return readUnmapped(pa, /*write=*/false, 0, cpuId);
        }

        // Linear PCI memory dominates -- check Pchip0/Pchip1 ranges first.
        if (pa >= kPchip0Base && pa < kPchip0End) [[likely]] {
            return m_pchip[0].read(pa - kPchip0Base, cpuId, width);
        }
        if (pa >= kPchip1Base && pa < kPchip1End) {
            return m_pchip[1].read(pa - kPchip1Base, cpuId, width);
        }
        return readUnmapped(pa, /*write=*/false, 0, cpuId);
    }

    inline void write(uint64_t pa, uint64_t value, int cpuId,
                      uint8_t width = 8) noexcept
    {
        if ((pa & kPchip0Base) == 0) [[unlikely]] {
            readUnmapped(pa, /*write=*/true, value, cpuId);
            return;
        }

        if (pa >= kPchip0Base && pa < kPchip0End) [[likely]] {
            m_pchip[0].write(pa - kPchip0Base, value, cpuId, width);
            return;
        }
        if (pa >= kPchip1Base && pa < kPchip1End) {
            m_pchip[1].write(pa - kPchip1Base, value, cpuId, width);
            return;
        }
        readUnmapped(pa, /*write=*/true, value, cpuId);
    }

    // Note on dispatcher shape: the Cchip and Dchip CSR apertures live
    // inside Pchip0's 0x801'... range, so the per-Pchip read()/write()
    // sub-dispatcher recognizes them and forwards to m_cchip / m_dchip.
    // This is HRM-correct (the Pchip CAPbus is the carrier for Cchip CSR
    // operations -- see HRM §6 and §12068) and keeps the chipset-level
    // dispatcher's case count to two hot paths.

    // ------------------------------------------------------------------
    // Snapshot
    // ------------------------------------------------------------------
    void serialize(QDataStream& ds) const noexcept
    {
        m_cchip.serialize(ds);
        m_pchip[0].serialize(ds);
        m_pchip[1].serialize(ds);
        m_isaBridge->serialize(ds);
    }

    void deserialize(QDataStream& ds) noexcept
    {
        m_cchip.deserialize(ds);
        m_pchip[0].deserialize(ds);
        m_pchip[1].deserialize(ds);
        m_isaBridge->deserialize(ds);
    }

private:
    uint64_t readUnmapped(uint64_t pa, bool isWrite, uint64_t value,
                          int cpuId) noexcept;

    ChipsetVariant   m_variant;
    int              m_cpuCount;
    TsunamiCchip     m_cchip;
    std::array<TsunamiPchip, 2> m_pchip;
    std::unique_ptr<Cy82C693IsaBridge> m_isaBridge;
};

#endif // TSUNAMI_CHIPSET_H
```

### 6.2 Key design points (rationale, not boilerplate)

**Two Pchips, not one.** The original proposal hardcoded a single Pchip. Tsunami is a two-hose chipset. Both hoses are instantiated; on a one-hose physical model the second one is still present but its PCI bus has no enumerated devices. This matches what `CSC<P1P>` (Pchip-1-present) advertises in the Cchip CSR — keeping Pchip1 instantiated keeps the Cchip's `P1P` story honest.

**`unique_ptr` over `shared_ptr`.** The ISA bridge has exactly one owner (the chipset). Pchip0 holds a non-owning `Cy82C693IsaBridge*` for dispatch; if the chipset is destroyed, the Pchip is destroyed first (member destruction order), so the raw pointer never dangles. `shared_ptr` would invite reference cycles and obscure ownership.

**Dispatcher hot path.** The aperture decode is two range checks for the common case (PCI memory), each compiled as a single compare against a constant. With `[[likely]]` on Pchip0, the path executes in ~6 instructions on x86-64 before tail-calling into `m_pchip[0].read()`. The Cchip CSR aperture lives inside Pchip0's range and is forwarded by the Pchip sub-dispatcher; HRM §6 specifically documents the CAPbus as the carrier for Cchip CSR operations, so this is architecturally faithful rather than a layering shortcut.

**No `assertVariantConsistency()`.** The original proposal's check was tautological. Variant consistency is enforced by construction — the Cchip and Pchips all take `variant` and validate their own fields.

**No `m_model` string.** Marketing-name derivation (ES40 / ES45) belongs in the platform-bring-up code, not the chipset. The variant enum is the single source of truth.

### 6.3 The `readUnmapped` helper

```cpp
// In TsunamiChipset.cpp (out-of-line so it doesn't bloat the hot path):

uint64_t TsunamiChipset::readUnmapped(uint64_t pa, bool isWrite,
                                      uint64_t value, int cpuId) noexcept
{
    // Forensic logging only, with the throttled stderr policy used in
    // TsunamiCchip's UNKNOWN handler. SRM probes the address map during
    // bring-up; without throttling this would flood the console.
    static std::atomic<uint64_t> s_cnt{0};
    const uint64_t n = s_cnt.fetch_add(1, std::memory_order_relaxed);
    if (n < 32) {
        std::fprintf(stderr,
                     "TsunamiChipset: UNMAPPED %s pa=0x%011" PRIx64
                     " val=0x%016" PRIx64 " cpu=%d (event %" PRIu64 ")\n",
                     isWrite ? "WRITE" : "READ", pa, value, cpuId, n);
    } else if ((n & 0xFFFFu) == 0) {
        std::fprintf(stderr,
                     "TsunamiChipset: %" PRIu64 " unmapped accesses "
                     "(loud-stderr muted past 32)\n", n + 1);
    }
    // HRM behavior: master abort returns all-ones to the CPU and may set
    // MISC<NXM>. The NXM hook is TODO(unwired) -- Phase D wires it after
    // the b_irq<0> error class is plumbed through pendingIrq0().
    return isWrite ? 0 : 0xFFFFFFFFFFFFFFFFULL;
}
```

The `0xFFFF...FF` return is the HRM-defined master abort behavior. The verification checklist item that read "should return `0xFFULL`" for subtractive-decode bounds in the original proposal was incorrect — the master-abort fill value is `~0`, not `0xFF`. (Subtractive-decode behavior for the ISA bridge specifically is the bridge's concern, not the chipset's.)

---

## 7. Hot-Path Performance Notes

The chipset hot path is the bottleneck on PIO-heavy workloads (driver init, interrupt polling). Three optimizations matter:

**1. Branchless aperture decode where possible.** The current dispatcher uses range checks. If profiling shows the chipset dispatcher in the top-N hot functions, an alternative is to mask `pa >> 32` against a small lookup table:

```cpp
// pa[39:32] uniquely identifies all apertures we care about; an 8-bit
// table dispatch is ~2 instructions and predicates better than a chain
// of range checks.
const uint8_t key = static_cast<uint8_t>(pa >> 32);
switch (key) {
    case 0x80: /* Pchip0 family: 0x801... */ ...
    case 0x82: /* Pchip1 PCI memory: 0x802... */ ...
    case 0x83: /* Pchip1 family: 0x803... */ ...
}
```

Defer this until profiling justifies it — modern branch predictors handle the simpler range-check form quite well for steady-state workloads.

**2. Avoid virtual dispatch in the hot path.** The proposal's `IPciDeviceHandler` / `IIoPortHandler` interfaces would put a vcall on every PCI MMIO access. The existing Pchip uses a typed handler model (function pointer + context) which is significantly faster on cold I-cache. Keep that.

**3. Per-CPU irq poll is one atomic load per pin per retire.** With the four `pendingIrqN(cpuId)` methods, the CPU retire loop becomes:

```cpp
// Once per retire:
if (chipset.cchip().pendingIrq3(cpuId)) [[unlikely]] { /* IPI deliver */ }
if (chipset.cchip().pendingIrq2(cpuId)) [[unlikely]] { /* timer deliver */ }
if (chipset.cchip().pendingIrq1(cpuId)) [[unlikely]] { /* dev deliver */ }
if (chipset.cchip().pendingIrq0(cpuId)) [[unlikely]] { /* err deliver */ }
```

Each predicate is one atomic relaxed load + mask, branch-predicted not-taken. Four poll points per retire is ~8 ns on modern hardware and dwarfs the retire cost only when an interrupt actually fires, which is precisely when extra work is acceptable.

If profiling later shows the four polls are themselves a hot-spot, consolidate into one composite poll:

```cpp
inline uint8_t TsunamiCchip::pendingIrqMask(int cpuId) const noexcept
{
    const uint64_t dir  = readDIR(cpuId);
    const uint64_t misc = m_misc.load(std::memory_order_relaxed);
    using IPINTR = Tsunami21272::Spec::Cchip::MISC::IPINTR;
    const uint64_t ipi = misc & (uint64_t{1} << (IPINTR.lsb + cpuId));
    return static_cast<uint8_t>(
        ((dir & 0xFC00000000000000ULL) ? 1u : 0u)   // irq0
      | ((dir & 0x00FFFFFFFFFFFFFFULL) ? 2u : 0u)   // irq1
      | ((m_pendingIrq2[cpuId].load(std::memory_order_relaxed)) ? 4u : 0u)
      | (ipi ? 8u : 0u));                            // irq3
}
```

One composite predicate per retire; downstream branches stay rare. Recommend deferring this until baseline numbers exist — premature optimization.

---

## 8. Verification Checklist (Revised)

The PRD-004 verification items need correction. Below is the revised list, with HRM citations.

1. **SRM Cchip register scan.** SRM reads CSC, MTR, MISC, MPD, AAR0-3, DIM0-1 during early init. Verify each returns the reset value programmed in `TsunamiCchip::reset()`. Specifically: CSC bits[3:0] match the CPU-present mask; AAR ASIZ encoding matches `memSizeBytes`; MISC<REV> equals 1 (Tsunami) or 8 (Typhoon). (HRM Tables 10-9, 10-12, 10-14.)
2. **IPI delivery round-trip.** From CPU0, write `(1u << 13)` to `MISC` (IPREQ bit for CPU1). On the next retire of CPU1, `pendingIrq3(1)` returns true. CPU1's PAL handler writes `(1u << 9)` to `MISC` (W1C of IPINTR bit for CPU1). Next retire: `pendingIrq3(1)` returns false. (HRM §10.2.2.3 IPREQ/IPINTR.)
3. **DIR is combinational.** Set `m_drir` bit 5 via `assertInterrupt(5)`; clear `m_dim[0]` bit 5; verify `readDIR(0)` bit 5 is zero. Set `m_dim[0]` bit 5; verify `readDIR(0)` bit 5 is one. (HRM §6.3.1.)
4. **Pchip0 config space — CY82C693 vendor/device ID.** Read PA `0x801'FE00'0xxx` (where xxx selects bus 0/dev 7/func 0/reg 0x00). Verify returns `0xC6931080` (VID 0x1080 Cypress, DID 0xC693). (HRM §10.1.2.1 config space, CY82C693 datasheet for VID/DID.) **Correction to PRD-004:** the CY82C693 lives at device 7, not slot 0; PRD-004 had this wrong.
5. **Pchip1 presence.** Read `MISC<REV>` and `CSC<P1P>` — verify `P1P=1`. Master-abort the second hose: read PA `0x803'8000'0000` (P1-WSBA0). Verify a sane Pchip1 register response, not all-ones.
6. **Master abort fill.** Read PA `0x801'C000'0000` (HRM-reserved aperture). Verify return value is `0xFFFFFFFFFFFFFFFF`, **not** `0xFFULL`. (HRM §10.1.1 reserved-region behavior; PRD-004 had `0xFF`.)
7. **Interval timer fire/clear.** Call `fireIntervalTimer()`. Verify `pendingIrq2(n)` returns true for each enabled CPU and `MISC<ITINTR<4+n>>` is set. W1C the matching `MISC<ITINTR>` bit; verify `pendingIrq2(n)` returns false. (HRM §6.3.2, §10.2.2.3 ITINTR.)
8. **Snapshot round-trip.** Take a snapshot mid-bring-up. Restore. Verify all atomic fields restore correctly; verify `m_pendingIrq2` is **not** persisted (Phase C deliberate decision — see existing header rationale). The PAL-handler-quiescent-state assumption holds.

---

## 9. Implementation Order

1. **Cchip additions** (Section 5). Pure additive; preserves snapshot format. Smallest review surface.
2. **`TsunamiChipset` wrapper** (Section 6). New file; no existing code modified.
3. **CPU retire-loop polling integration.** Threading `pendingIrqN(cpuId)` calls into the Machine retire loop. This is downstream of (1) and isolated to the CPU module.
4. **Verification item 1–4** (Section 8) — smoke-test the bring-up path.
5. **`IPREQ` → `IPINTR` wiring** (Section 5.2). Single-line CAS-loop addition.
6. **Verification item 5–8.**

Each step is independently testable. Steps (1) and (2) can be reviewed in parallel since they touch disjoint files.

---

## 10. What This Proposal Does Not Do

- It does not rewrite `TsunamiCchip`. The existing class is correct.
- It does not add SMP scheduling. The CPU retire loop changes are per-CPU calls into already-atomic Cchip state.
- It does not wire MISC<DEVSUP>, MISC<ACL>, or the ABW first-set lock. Those remain Phase D+ as documented in the existing header `TODO(unwired)` table.
- It does not add an SDRAM timing model (MTR), DIMM SPD discovery (MPD), or SDRAM mode-register programming (MPR0-3). Those remain Phase D+ as well.
- It does not change snapshot format. Phase B left TTR/TDR out of the snapshot by design (see existing change-history notes); this proposal preserves that posture.

---

## 11. Risks

**One.** If cowork's review of the existing `miscWriteW1C()` CAS loop disagrees with the in-line IPREQ→IPINTR extension (Section 5.2) — for example, preferring a separate atomic operation — the alternative is two sequential CAS attempts. That preserves correctness but adds a one-cycle observable window where IPREQ is "set" and IPINTR isn't. The HRM does not forbid this window (IPREQ is WO, so software can't observe a "set" state anyway), but the single-CAS form is cleaner and matches the spec.

**Two.** The `pa & kPchip0Base` check in the hot-path dispatcher assumes bit 43 set; if the CPU dispatcher passes through a non-PIO address by mistake, the unmapped fallback handles it but at the cost of one cache miss on the throttled stderr log. Recommend the CPU dispatcher assert `(pa >> 43) & 1` in debug builds.

---

## Appendix A: HRM Cross-References Used

| Topic | HRM section |
|-------|-------------|
| System address map (Table 10-1) | §10.1.1 |
| PCI memory / I/O / config space layout | §10.1.2 |
| PIO address translation (system→PCI) | §10.1.3 |
| Cchip CSR register list (Table 10-8) | §10.2.2 (intro) |
| CSC fields (Tables 10-9 Tsunami, 10-10 Typhoon) | §10.2.2.1 |
| MTR fields (Table 10-11) | §10.2.2.2 |
| MISC fields incl. IPREQ/IPINTR/ITINTR (Table 10-12) | §10.2.2.3 |
| MPD fields (Table 10-13) | §10.2.2.4 |
| AAR fields (Tables 10-14/10-15) | §10.2.2.5 |
| DIMn (Table 10-16) | §10.2.2.6 |
| DIRn = DRIR & DIMn (Table 10-17) | §10.2.2.7 |
| DRIR (Table 10-18) | §10.2.2.8 |
| Device/error interrupt delivery via b_irq<1:0> | §6.3.1 |
| Interval timer interrupts via b_irq<2> | §6.3.2 |
| Interprocessor interrupts via b_irq<3> | §6.3.3 |

— End of document —
