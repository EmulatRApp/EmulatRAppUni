# iprLib

Architectural register file and Internal Processor Registers.  This is
the only library that mutates architectural state at MEM commit.
Every other component reads from here or stages writes through
`commitPending`.

## Contents

- `IprMaster` -- per-CPU owner of register state.
- `IntRegFile` -- 32 entries, R31 hardwired zero.
- `FloatRegFile` -- 32 entries, F31 hardwired zero, FPCR shadow.
- IPR storage: `PalIPR`, `RunLoopIPR`, `HwpcbIPR`, `OsfIPR`.
- `m_pc` -- PC tracker with `redirectPC`, `advancePC`, `getPC`,
  `setPC`, `consumeRedir`.
- `commitPending(PipelineSlot&)` -- the single MEM-stage commit entry
  point.  R31 / F31 zero-write skip is performed here, in one place.

## Ownership rule

Registers are NOT modified by:
- Decode
- Fetch
- Grain construction
- Executor functions (they stage into `slot.effects`)

Registers ARE modified by:
- `commitPending` at the MEM stage
- The MMU during ASN context switch
- PAL state-restoring entry points (HW_REI), via the same staged path

## Dependencies

- coreLib

## Consumed by

- pipelineLib (PC reads, MEM commit)
- cpuLib (architectural state inspection)
- All executor Boxes (operand reads via `slot.m_iprGlobalMaster`,
  IPR reads via the same)
