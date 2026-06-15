# palBoxLib

PalBox -- privileged and PALcode executors.  CALL_PAL and the HW_*
family.  These are the entry points to PAL mode and the bridge
between architectural state and IPRs.

## Contents

- `executeCALL_PAL` and the 256+ function-code variants from
  `GrainMaster.tsv` opcode 0x00.  Personality-aliased pairs
  (e.g., HALT vs HALT_64) dispatched via the `PersonalityResolver`
  set up by codegen.
- `executeHW_MTPR` (opcode 0x1D) -- write IPR.  IPR side-effects
  (PAL_BASE update, MMU TLB invalidation, ASN context, ...) are
  applied here as part of the executor body.
- `executeHW_MFPR` (opcode 0x19) -- read IPR.
- `executeHW_LD` (opcode 0x1B) -- privileged load through GuestMemory.
- `executeHW_ST` (opcode 0x1F) -- privileged store through GuestMemory.
- `executeHW_REI` (opcode 0x1E) -- PAL exit and PC restore.

## PAL contract

- Privilege check: executors that require kernel mode validate
  `PS<CM>` and signal OPCDEC if called from user mode.  CALL_PAL
  function-codes 0x80-0xFF are unprivileged on EV6.
- IPR writes: routed through the appropriate `IprStorage_X`
  accessor in iprLib.  IPR-bit-mask normalization happens in this
  Box, not in iprLib (e.g., PAL_BASE bits[43:15] mask).
- HW_REI restores PC from the appropriate IPR (EXC_ADDR or saved
  context PC) and exits PAL mode.

## Dependencies

- coreLib
- iprLib (IPR write/read, register file for argument passing)
- memoryLib (HW_LD / HW_ST through GuestMemory only)

## Consumed by

- grainFactoryLib (function table wires opcode 0x00, 0x19, 0x1B,
  0x1D, 0x1E, 0x1F)
