# grainFactoryLib/generated

Code-generator outputs.  These files are machine-generated from
`../GrainMaster.tsv` and `../SemanticFlags.tsv` by the tool in
`../codegen/`.

## DO NOT EDIT these files by hand

Changes to files in this directory will be overwritten on the next
codegen run.  To change a generated file, edit the input TSV and
re-run the generator (see `../codegen/README.md`).

## Files

- `OpcodeTable.cpp` -- top-level `g_opcodeTable[64]` dispatch
  descriptors.
- `SubTables.cpp` -- per-opcode sub-tables of executor function
  pointers.
- `ExecutorDecls.h` -- forward declarations of every executor
  function referenced by the table.
- `SemanticFlags.cpp` -- per-grain semantic flag bits.

## Why generated files are checked in

Reviewing the diff of a regenerated file is part of how we catch
unintended dispatch changes when the TSV is edited.  If the
generated file weren't checked in, a TSV edit would silently change
the build with no diff to review.
