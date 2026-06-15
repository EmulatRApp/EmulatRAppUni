# mBoxLib

MBox -- memory-format executors.  Loads, stores, address arithmetic,
load-locked / store-conditional, prefetch.  Always operates on
GuestMemory through the standard load/store path; never reads from
the IBox view or ROM backing store.

## Contents

Executor functions for memory-format opcodes:

- Loads (opcode 0x28-0x2D): LDQ, LDL, LDQ_U, LDF, LDG, LDS, LDT
- Stores (opcode 0x24-0x27, 0x2C-0x2F): STQ, STL, STQ_U, STF, STG,
  STS, STT
- Address arithmetic (opcode 0x08-0x09): LDA, LDAH
- LL/SC (opcode 0x2A-0x2B, 0x2E-0x2F): LDQ_L, LDL_L, STQ_C, STL_C
- Prefetch hints (opcode 0x18 misc): FETCH, FETCH_M, ECB

## Executor contract

- Loads: read memory via `slot.m_mBox->load(va, width)`, populate
  `slot.effects.intReg / intValue` for the destination register.
- Stores: stage `slot.effects.memVA / memData / memWidth` and let the
  MEM stage commit the write.  Reservation tracking for STQ_C / STL_C
  consults the reservation manager.
- LDA / LDAH: pure address arithmetic; no memory access.
- Prefetch: no architectural state change; hint to MMU/cache subsystem.

## Dependencies

- coreLib
- iprLib (operand reads)
- memoryLib (GuestMemory load/store)

## Consumed by

- grainFactoryLib (function table wires memory-format opcodes
  0x08, 0x09, 0x18, 0x20-0x2F)
