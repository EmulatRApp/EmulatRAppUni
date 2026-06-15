# fBoxLib

FBox -- floating-point executors.  Implements opcode 0x14 (ITFP /
float-misc), 0x15 (FLTV / VAX float), 0x16 (FLTI / IEEE float), 0x17
(FLTL / float-misc).

## Contents

- IEEE single (S) and double (T) ops: ADDS, ADDT, MULS, MULT, DIVS,
  DIVT, SUBS, SUBT, square root family
- VAX float (F, G): ADDF, ADDG, MULF, MULG, DIVF, DIVG, SUBF, SUBG
- Conversions: CVTQS, CVTQT, CVTTS, CVTGQ, CVTGF, CVTGD, CVTQF,
  CVTDG, CVTQG, CVTBQ
- Compares: CMPGEQ, CMPGLT, CMPGLE, CMPTUN, CMPTEQ, CMPTLT, CMPTLE
- Float register move / sign manipulation: CPYS, CPYSN, CPYSE,
  MT_FPCR, MF_FPCR
- Float-misc (opcode 0x17): conditional move, FNOP variants

## Executor contract

Same shape as eBoxLib executors, but operands are read from the
float register file.  FPCR shadow is consulted for trap-mode
selection (S/U suffix variants).

## Dependencies

- coreLib
- iprLib (FloatRegFile, FPCR shadow)

## Consumed by

- grainFactoryLib (function table wires opcode 0x14-0x17)
