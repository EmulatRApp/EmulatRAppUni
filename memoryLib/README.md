# memoryLib

Guest memory and instruction-fetch view.  Single-source-of-truth physical
memory model with a separate IBox view for instruction fetch (ROM image
during boot, GuestMemory after).  No write aliasing between physical
address regions.

## Contents

- `GuestMemory` -- DRAM model, the single source of truth for data
  reads and writes.  No shadow banks, no aliasing logic.
- `IBoxView` -- instruction-fetch view.  Backed by an immutable ROM
  snapshot during boot; switches to GuestMemory after the boot loader's
  `done:` exit.
- `RomBackingStore` -- the immutable boot-image snapshot.
- `MMU` / `TLB` -- virtual-to-physical translation, ASN handling.
- `MMIO` -- memory-mapped I/O routing.
- Read primitives: `read64`, `read32`, `readInst32`.
- Write primitives: `write64`, `write32`, `writeBlock`.

## Memory contract (see SPEC proposal section 2-6)

1. Physical memory is single-sourced.  GuestMemory is authoritative.
2. The 0x000000, 0x600000, and 0x900000 regions are distinct unless
   the MMU explicitly maps them.
3. No cross-region shadow writes ever.
4. IBox fetch view differs from data view, but both must agree on
   architectural correctness.
5. HW_LD / HW_ST always operate on GuestMemory, never on the ROM
   backing store.

## Dependencies

- coreLib

## Consumed by

- pipelineLib (instruction fetch path)
- mBoxLib (data load and store)
- palBoxLib (HW_LD, HW_ST)
