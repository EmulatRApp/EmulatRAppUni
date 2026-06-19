# ES40 Enablement: ALi M1543C South-Bridge Interface

**Date:** 2026-06-17
**Author:** Claude (Anthropic), with Project Architect Timothy Peer
**Status:** ALi model built + compile/run verified standalone; wiring landed
(needs an MSVC/Qt build to confirm no DS10 regression).
**For H&M incorporation:** chipset / south-bridge chapter, beside the Cypress.

---

## 1. Goal

Make the ES40 SRM firmware (`firmware/es40_v7_3.exe`) runnable in EmulatR for
the halt-button generalization test. ES40 is **Tsunami (21272)** silicon, so
no new chipset is needed -- but its south bridge is the **ALi M1543C**, not the
Cypress CY82C693 used by the DS10/DS20 (PC264). The missing piece was the ALi
bridge interface + a way to select it per platform.

## 2. What was built

### `chipsetLib/AliM1543C.h` (new)
Mirrors the `Cy82C693IsaBridge` contract exactly: implements
`IPciDeviceHandler` + `IIoPortHandler`, 256-byte config space.

- Function 0 = PCI-to-ISA bridge: **vendor 0x10B9, device 0x1533,
  class 0x060100, header type 0x80** (multifunction).
- `AliPciFunctionStub` companions: **M5229 IDE 0x5229**, M5237 USB 0x5237,
  **M7101 PMU/SMBus 0x7101** -- give the SRM's multifunction probe a valid
  header instead of a master abort.
- Config writes store through with RO-protected vendor/device; named ALi
  bridge/IRQ-routing registers (idx 0x40-0x4B) documented from the datasheet.

Ported from `Processor Support/ALi M1543_Datasheet.{pdf,txt}` (indexed in
REFERENCE_INDEX §3.1). **Verified with g++**: identity reads back
0x1533_10B9, PIRQ store-through, RO protection, IDE companion 0x5229_10B9.

### Model-gated south-bridge selection
`TsunamiChipset::wireDevices()` now picks the bridge by model:
`isAliPlatform(model)` (ES40/ES45/DS25) -> `m_ali`, else `m_cypress`
(DS10/DS20, the default). Added `AliM1543C m_ali` member + include. The rest
of `wireDevices()` -- the ISA devices at fixed ports (8259 pair, 8254, RTC,
UARTs, KBC) -- is bridge-agnostic and reused unchanged.

### Model thread (`Machine.cpp`)
The chipset is now constructed from the ini model string via the existing
model-string ctor:
`m_chipset(m_settings.system.model, m_settings.system.cpuCount, memSize)`.
This populates the chipset's `m_model` (the gate needs it) and uses the ini
cpuCount. **DS10 -> Tsunami, cpuCount 1: byte-identical to the prior
hardcoded line.**

## 3. FAITHFUL vs STUBBED

**Faithful:** ALi func0 identity + class + multifunction header; companion
function identities; config-space store-through with RO IDs; the south-bridge
selection (DS10/DS20 untouched).

**Stubbed (TODO):** `ali-irq-route` -- the PIRQ steering (idx 0x48-0x4B) and
IDE INTAJ level/edge (0x44) have real routing effects the SRM relies on; today
they store through with no field effect. Companion-function internals (actual
IDE/USB/PMU behavior) are not modeled -- the existing ATA port models carry
dqa for now. ISA pass-through is a float/sink (the real ISA devices are wired
separately).

## 4. Build / run

1. **Build DS10 first** and smoke-test to confirm no regression (the
   `wireDevices()` + `Machine.cpp` ctor changes touch the shared path; DS10
   must still reach `>>>`).
2. ES40 run -- `config/EmulatrV4.ini`:
   `[System] model=ES40, cpuCount=1` ; `[ROM] firmwareImage=firmware/es40_v7_3.exe`.
   The model switch loads `es40_platform.win`; the gate wires the ALi.
3. Watch the COM1 banner + the `HALTPROBE`/smir trace.

## 5. Expected outcomes / known risks the run will expose

- **South-bridge init divergence:** only the ALi *identity* is faithful so far;
  the ES40 SRM's `initialize_hardware` programs ALi config/IRQ-routing
  registers whose field effects are stubbed. First likely divergence point.
- **Pchip1 / dual-hose:** ES40 is dual-Pchip; EmulatR mirrors Pchip1 as
  all-ones. ES40 enumerates hose 1.
- **System identity:** the HWRPB/SROM path is DS10/PC264-tuned; ES40 may read a
  DS10 system-type and branch oddly.
- **Halt source:** DS10/PC264 reads halt via TIG smir (modeled). ES40 has an
  OCP/RMC; if its SRM reads halt there instead, the message persists for a NEW
  reason -- itself the useful finding. If it reads the same smir, the existing
  fix carries over and the message should be gone.

## 6. References
- `Processor Support/ALi M1543_Datasheet.{pdf,txt}` sec 4 (config registers).
- `chipsetLib/AliM1543C.h`, `Cypress_CY82C693ISABridge.h`, `IDeviceHandlers.h`.
- `es40_platform.json` / `.win`; `config/EmulatrV4.ini`.
- LFU option-card firmware: `firmware/<platform>_diskN/` (REFERENCE_INDEX §3.1).
- Prior: `journals/20260613_halt_switch_tig_register.md` (smir),
  `journals/20260616_titan_21274_interface.md` (taxonomy).
