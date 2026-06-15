# eBoxLib

EBox -- integer ALU executors.  Implements the operate-format
(opcode 0x10 INTL, 0x11 INTL, 0x12 INTSL, 0x13 INTM) instructions.
Each executor is a free function with the canonical signature:

```
void executeXxx(PipelineSlot& slot) noexcept;
```

## Contents

Executor functions wired by the function table to opcode 0x10-0x13:

- Logical: BIS, BIC, ORNOT, AND, ANDNOT, EQV, XOR
- Add / sub: ADDL, ADDQ, SUBL, SUBQ, ADDL_V, ADDQ_V, SUBL_V, SUBQ_V
- Compare: CMPEQ, CMPLT, CMPLE, CMPULT, CMPULE,
  CMPBGE
- Shift: SLL, SRL, SRA
- Byte / word manipulation: ZAP, ZAPNOT, EXTBL, EXTWL, EXTLL, EXTQL,
  EXTWH, EXTLH, EXTQH, INSBL, INSWL, INSLL, INSQL, INSWH, INSLH,
  INSQH, MSKBL, MSKWL, MSKLL, MSKQL, MSKWH, MSKLH, MSKQH
- Conditional move: CMOVEQ, CMOVNE, CMOVLT, CMOVLE, CMOVGT, CMOVGE,
  CMOVLBC, CMOVLBS
- Multiply: MULL, MULQ, UMULH, MULL_V, MULQ_V

## Executor contract

- Read operands from `slot.ra_value`, `slot.rb_value`,
  `slot.literal_value`.
- Compute result.
- Write `slot.effects.intReg` and `slot.effects.intValue`.
  The MEM-stage `commitPending` applies the write (skipping R31).
- No direct register file writes, ever.
- No memory access.
- No exception throws (`noexcept` enforced).

## Dependencies

- coreLib (PipelineSlot, EffectSet)
- iprLib (read register operands; no IPR access in this Box)

## Consumed by

- grainFactoryLib (function table wires opcode 0x10-0x13 entries
  to these executors via codegen-emitted `ExecutorDecls.h`)
