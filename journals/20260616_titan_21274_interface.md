# Titan (DECchip 21274) Chipset Interface — Design & Implementation Journal

**Date:** 2026-06-16
**Author:** Claude (Anthropic), with Project Architect Timothy Peer
**Status:** Scaffold landed; CSR spec + new-silicon Pchip compile-and-run verified.
**Intended audience:** future EmulatR sessions; **flagged for incorporation into
the H&M documentation** (chipset architecture chapter, adjacent to the Tsunami
section).

---

## 1. Why this exists

EmulatR's working chipset is the 21272 (Tsunami/Typhoon), targeting the DS10.
The ES45 / DS25 / DS15 firmware images use a **different** core-logic chipset.
This journal records the identification of that chipset, the authoritative
register interface, and an adjacent implementation that reuses the Tsunami
model where the silicon is shared and adds only the genuinely-new parts.

This was prompted by profiling the v7.3 firmware images and the found files
`palcode-ds10.rom` / `pc264srm.sys` (see `Analysis/firmware_v7_3_profile/`),
which sorted the platforms by chipset.

---

## 2. Taxonomy — the correction

| Chipset | Part | Members | Notes |
|---------|------|---------|-------|
| Tsunami | **21272** | DS10, DS20, ES40 | EmulatR's working model |
| Typhoon | **21272** (high-bandwidth, dual-Dchip *variant*) | ES40-class | NOT a separate part |
| **Titan** | **21274** | **DS15, DS25, ES45** | separate chipset; this journal |

**Bug corrected (2026-06-16):** `chipsetLib/TsunamiVariant.h` previously
labelled the `Typhoon` enum as "21274" and mapped DS25/ES45 to it. Those are
really **Titan (21274)**. `variantFromModel()` now maps DS15/DS25/ES45 →
`ChipsetVariant::Titan`; `Typhoon` is retained and re-documented as the 21272
high-bandwidth variant. `tests/chipsetLib/test_ticket01_5_variant_binding.cpp`
updated to match.

---

## 3. The interface — what Titan actually is

**Authoritative source:** Linux `arch/alpha/include/asm/core_titan.h` +
`arch/alpha/kernel/core_titan.c` (derived from the *Titan Chipset Engineering
Specification Rev 0.12, 13 Jul 1999*), cross-checked against the **ES45 Service
Guide EK-ES450-SV Appendix D** (field-level MISC/DIRn/PERROR/SERROR/AGPERROR +
physical addresses). Both are now in `Processor Support` and indexed in
`REFERENCE_INDEX.md` §3.1.

### 3.1 Shared with the 21272 (so: reused, not re-implemented)

Titan shares the **top-level PA map** with the 21272:

| Block | PA |
|-------|----|
| Cchip | `0x801_A000_0000` |
| Dchip | `0x801_B000_0800` (note `+0x800` vs 21272's `…0000`) |
| PA-chip 0 | `0x801_8000_0000` |
| PA-chip 1 | `0x803_8000_0000` |
| TIG space | `0x801_0000_0000`, byte at `(offset<<6)` |
| Hose *h* MEM/IO/CONF | `0x800_0000_0000 + (h<<33) + {0, 0x1FC00000, 0x1FE00000}` |

The **Cchip register offsets are 21272-compatible**, and the project's 21272
Cchip already carries the 4-CPU DIM/DIR layout. Verified against Appendix D:
MISC `+0x80`; per-CPU DIR0-3 at `+0x280 / +0x2C0 / +0x680 / +0x6C0`. The
**TIG/halt path is identical** (same `(offset<<6)` mechanism, same SMIR/halt
registers). Therefore Titan **reuses `TsunamiCchip`, `TsunamiDchip`,
`TsunamiTig` unchanged**.

### 3.2 New Titan silicon (so: implemented fresh)

1. **Dual-port PA-chip.** Each of the two PA-chips has a **G-port (PCI)** at
   port-offset `+0x000` and an **A-port (AGP)** at `+0x1000`. Each port has 4
   DMA windows (`WSBA[4]/WSM[4]/TBA[4]`), a port-control register
   (`GPCTL`/`APCTL` at `+0x300`), and a port-specific error block. This is
   the structural difference from the 21272's single-port Pchip.
2. **Richer error set.** G-port: `SERROR/GPERROR` (+EN/+SET), `GTLBIV/GTLBIA`,
   `SCTL`. A-port: `AGPERROR/APERROR` (+EN/+SET), `AGPLASTWR`, `ATLBIV/ATLBIA`.
   Replaces the 21272 single `PERROR`.
3. **AGP.** `APCTL` carries AGP rate/SBA/enable/present bits (`<63:52>`);
   hose 2 (pachip0.A) / hose 3 (pachip1.A) are AGP.
4. **4 hoses.** 0=pachip0.G, 1=pachip1.G, 2=pachip0.A, 3=pachip1.A. pachip1
   presence is advertised in **Cchip CSC bit 14**.
5. **Default window programming** (kernel installs, SRM-compatible): W0 = SG
   8 MB @ 8 MB (ISA); W1 = direct 1 GB @ 2 GB; W2 = SG 1 GB @ 3 GB; W3 = SG
   only (disabled). Monster Window enabled in PCTL.

---

## 4. Files landed (chipsetLib/)

| File | Role | Verification |
|------|------|--------------|
| `Titan21274_CsrSpec.h` | Authoritative constants: base PAs, Cchip/Dchip/Port offsets, PCTL/WSBA/error masks, hose map, MCHK SCB vectors. Pure `<cstdint>`. | **static_assert + g++ ✓** (`dir(2)==0x680`, `kPachip1`, `conf(0)`) |
| `TitanPchip.h` | The new silicon: `TitanPachipPort` (G/A), `TitanPachip` (g+a), `TitanPchip` (two PA-chips, CSC<14> presence, absolute-PA CSR decode, reset window programming). | **g++ compile + run ✓** (reset windows + dual-port CSR routing) |
| `TitanChipset.h` | Top-level scaffold: composes reused `TsunamiCchip/Dchip/Tig` + `TitanPchip`; MMIO CSR routing for the four CSR blocks; 4-hose INTx→DRIR mapping. | Logic mirrors verified Tsunami pattern; depends on Qt/project sub-chips (not locally compilable) |
| `TsunamiVariant.h` | Taxonomy corrected; `ChipsetVariant::Titan` + `kTitanInfo` + mapping. | Read-verified on disk |

Also: three headers added to `CMakeLists.txt`; the binding test updated.

---

## 5. FAITHFUL vs STUBBED (the honest map)

**Faithful (verified against the kernel header / Appendix D):**
- All Titan PA / register offsets and base addresses.
- Cchip MISC/DIR/DIM interrupt + IPI register layout (via 21272 reuse, offsets
  confirmed identical).
- TIG / SMIR / per-CPU halt registers — **the halt-button path** — identical to
  Tsunami (same offsets, reused class).
- Dual-port PA-chip CSR register file, window registers, PCTL/AGP/error masks.
- Reset-time PCI→memory window programming.

**Stubbed / TODO (carries explicit markers in code):**
- `TODO(titan-pci-route)` — per-hose PCI dense/sparse **MEM, IO, Type-0/1
  config-cycle** routing. Identical to the 21272; intended to delegate to the
  existing `TsunamiPchip` routing per hose rather than be re-written.
- `TODO(titan-isystembus)` — `TitanChipset` is not yet an `ISystemBus`
  (read/write/fetch arbiter) and does not yet own `GuestMemory` or the device
  layer (ISA bridge, UARTs, IDE, RTC, IIC, PIC, flash). The device/platform
  layer is PC264-derived and **identical** to Tsunami; the intended final form
  mirrors `TsunamiChipset::wireDevices()` 1:1.
- `TODO(titan-csc)` — reflect pachip1 presence into Cchip CSC<14> (needs a CSC
  setter on the reused Cchip).
- `TODO-verify` — Titan DREV / MISC.REV reset values (placeholders `0x22`/`0x0A`)
  pending the 21274 Eng Spec numbers; firmware reads these loosely.

---

## 6. Build / test handoff (authored without a local MSVC/Qt compile)

1. Build the project. Header-only additions; if MSVC `-Wswitch` flags the new
   `ChipsetVariant::Titan` in any **Tsunami** CSR-spec switch lacking a Titan
   arm, add a Titan arm (the default is otherwise taken — harmless for the
   Tsunami path, which never builds a Titan instance).
2. Run `test_ticket01_5_variant_binding` — expectations updated for the
   corrected taxonomy.
3. To actually instantiate Titan: `systemLib/Machine.cpp:317` currently
   hardcodes `m_chipset(ChipsetVariant::Tsunami, 1, memSize)` as a
   `TsunamiChipset`. A Titan boot path needs a selection seam there
   (branch on `variantFromModel(model)`), and the `TODO(titan-isystembus)`
   completion so `TitanChipset` presents the same `ISystemBus` + device
   surface `Machine` expects. **DS10 / Tsunami boot is untouched.**

---

## 7. Relevance to the current milestone

- The **halt-button phenomenon** (`journals/20260613_halt_switch_tig_register.md`)
  is chipset-invariant: Titan's SMIR / per-CPU halt registers are at the same
  offsets as Tsunami and use the reused `TsunamiTig`. A Titan profile will
  reproduce the same halt-button behavior — and the same `ipcr` storage-only
  SMP-IPI gap — so testing the new firmware does not change that surface.
- Most imminent firmware testing (DS20 / ES40 / PC264) runs on the **existing**
  Tsunami model; only ES45 / DS25 / DS15 need Titan.
- Titan extends SMP on the same HWRPB/IPI protocol; the 4-CPU DIR2/DIR3 path is
  already present in the reused Cchip.

---

## 8. References

- `Processor Support/REFERENCE_INDEX.md` §3.1 (Titan entry + taxonomy caveat).
- `Processor Support/Titan Chipset - EK-ES450-SV.A01.{pdf,txt}` Appendix D.
- Linux `arch/alpha/{include/asm/core_titan.h, kernel/core_titan.c}`.
- `Analysis/firmware_v7_3_profile/FIRMWARE_PROFILE_2026-06-16.md` (platform sort).
- `chipsetLib/Titan21274_CsrSpec.h`, `TitanPchip.h`, `TitanChipset.h`.
