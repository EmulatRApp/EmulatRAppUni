# cBoxLib

CBox -- control-flow executors.  Branches and indirect jumps.
Resolves target, writes link register if applicable, calls
`m_pc->redirectPC` for taken or mispredicted branches, and feeds
the branch predictor update at WB.

## Contents

- Unconditional branches: `executeBR`, `executeBSR` (opcode 0x30,
  0x34).  Both write Ra = next-PC (link).
- Integer conditional branches (opcode 0x38-0x3F): `executeBLBC`,
  `executeBEQ`, `executeBLT`, `executeBLE`, `executeBNE`,
  `executeBGE`, `executeBGT`, `executeBLBS`.
- Float conditional branches (opcode 0x31-0x33, 0x35-0x37):
  `executeFBEQ`, `executeFBLT`, `executeFBLE`, `executeFBNE`,
  `executeFBGE`, `executeFBGT`.
- Indirect jumps (opcode 0x1A, function 0-3): `executeJMP`,
  `executeJSR`, `executeRET`, `executeJSR_COROUTINE`.
- Branch predictor: history table, prediction lookup, update path.

## Executor contract

- Compute target.  For branches, target = PC + 4 + 4 * sign_extend(disp).
  For jumps, target = Rb & ~3 (alignment).
- If branch is taken: set `slot.effects.branchTaken = true`,
  `slot.effects.branchTarget = target`, call
  `slot.m_pc->redirectPC(target)`.
- For BSR / JSR / JSR_COROUTINE / JMP / RET: write Ra =
  buildReturnAddress(slot.di.pc) (PC+4 with PAL bit OR'd in if in
  PAL mode).  Stage via `slot.effects.intReg / intValue` so MEM
  applies it (skipping R31 uniformly).
- Update branch predictor with resolved direction and target.
- Mispredict detection: compare resolved (taken, target) vs predicted;
  set `slot.mispredict` and trigger selective flush of younger slots.

## Dependencies

- coreLib
- iprLib (PC redirect, link register write)

## Consumed by

- grainFactoryLib (function table wires opcode 0x1A, 0x30-0x3F)
