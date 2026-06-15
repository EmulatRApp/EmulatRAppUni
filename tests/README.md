# tests

Test harness and per-component tests.  The test framework is TBD
(Catch2, GoogleTest, doctest, or a minimal custom one); the choice
will be made during the "frank discussion about technology" before
implementation begins.

## Subdirectories (planned)

- `coreLib/` -- type-shape and POD-layout tests (`static_assert`s on
  `sizeof(InstructionGrain)`, etc.)
- `pipelineLib/` -- stage transition, ring rotation, flush correctness
- `eBoxLib/` -- per-instruction executor tests for integer ops
- `fBoxLib/` -- per-instruction executor tests for FP ops
- `mBoxLib/` -- memory-op executor tests, alignment edge cases
- `palBoxLib/` -- PAL-side executor tests, IPR side-effect verification
- `cBoxLib/` -- branch and jump executor tests, branch predictor
- `generated/` -- codegen-emitted dispatch verification.  One test
  per `GrainMaster.tsv` row that synthesizes rawBits, runs the
  dispatch lookup, and asserts the resulting grain has the expected
  mnemonic and Box.
- `integration/` -- end-to-end scenarios.  Boots from a known SRM
  ROM image and compares the trace against a golden file.

## Test harness shape (proposed)

A test that exercises one executor needs:

- A mock `PipelineSlot` with operands populated.
- A mock `IprMaster` with the integer / float register file and
  IPRs initialized to a known state.
- A mock `GuestMemory` for executors that touch memory.
- A way to capture `slot.effects` after the executor runs and
  assert on its fields.

This is a small enough harness that it can be hand-written without a
heavy framework.  Final framework decision deferred.

## Dependencies

- All libraries being tested
- Test framework (TBD)
