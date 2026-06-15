# coreLib

POD types and shared definitions used across the emulator.  No runtime
logic beyond inline accessors; no ownership of pipeline or memory state.
This is the foundation library every other lib depends on.

## Contents

- `PipelineSlot` -- the per-cycle data carrier (one slot per pipeline
  ring position).  See SPEC_execution_model section 2.
- `InstructionGrain` -- the POD grain.  Holds opcode, function-code,
  operand fields, semantics flags, and the `execute_fn` function
  pointer.  No vtable.  See SPEC_execution_model section 3.
- `EffectSet` -- structured side-effect record produced by an executor
  at EX, consumed by MEM (commit) and WB (trace).  See section 6.5.
- `BypassEntry` / `BypassNetwork` -- forwarding network record types.
  See section 5.5.
- Shared enums: `DispatchKind`, `ExecBox`, `PCReason`, `GrainFormat`,
  `TrapCode`, `PalFunction`.
- Attribute macros: `AXP_HOT`, `AXP_ALWAYS_INLINE`.

## Dependencies

- Standard library only.  No project dependencies.  No Qt.

## Consumed by

- Every other library in the project.

## Conventions

- All headers in this lib should compile standalone with `<cstdint>`
  alone (or `<cstddef>` for `size_t`).
- No `.cpp` files unless absolutely required (POD types and inline
  helpers should be header-only).
- ASCII(128) source only.
