# HRM Deviations

Places where V4's chipset model deviates from the literal HRM text, or makes
a platform-wiring choice the HRM deliberately leaves open. Format: what the
HRM says, what we do, why.

## PCI INTx -> DRIR bit assignment (Ticket 3)

- **HRM:** `DRIR<63:0>` (Sec. 6.3, reg `0x300`) is 64 raw device-interrupt
  inputs; `DIRn = DRIR & DIMn`. The HRM does NOT specify which PCI INTx pin
  drives which DRIR bit -- that is board-level wiring (TIGbus / sys_int
  routing) and varies per platform (ES40 / DS20 / DS10). `DRIR<63>` is
  reserved for the error/NXM interrupt (Sec. 6.3; `MISC<NXM>`).
- **V4:** `TsunamiChipset::pciIntxToDrirBit(pchipId, intxLine) =
  32 + pchipId*4 + (intxLine & 3)`. Pchip0 INTA-D -> `DRIR[35:32]`,
  Pchip1 INTA-D -> `DRIR[39:36]`. `raisePciInterrupt` / `lowerPciInterrupt`
  use it.
- **Why:** No HRM-canonical mapping exists; this is a documented V4
  convention (matches the action-plan simplification), stays clear of the
  reserved bit 63, and is a single constant to revise if a specific
  platform's board wiring is adopted.
