# grainFactoryLib/codegen

The function-table code generator.  A small standalone tool, written
in modern C++ (no Qt, no other dependencies), that reads the source-
of-truth TSV files and emits the dispatch table, executor declarations,
and reference matrix.

## Inputs

- `../GrainMaster.tsv` -- (Opcode, Function, Mnemonic, Description,
  Type, Box) per row.  Personality is encoded in the mnemonic via
  the `_64` suffix convention.
- `../SemanticFlags.tsv` -- (Opcode, Function, FlagBitMask) per row;
  hand-curated.  Defines which `S_*` semantic flags apply.

## Outputs (written to `../generated/`)

- `OpcodeTable.cpp` -- top-level `g_opcodeTable[64]` dispatch
  descriptors, one per primary opcode.
- `SubTables.cpp` -- per-opcode sub-tables of executor function
  pointers, sized per the format's function-code width.
- `ExecutorDecls.h` -- forward declarations of every executor
  function name referenced.  Inclusion in each Box library forces
  missing executors to be link errors rather than runtime faults.
- `SemanticFlags.cpp` -- per-grain semantic flag bits.
- `../../docs/generated/InstructionMatrix.md` -- human-readable
  reference table for documentation.

## Build wiring

Run as a CMake custom command before the main build target.
Generator outputs are checked into the source tree; reviewing the
generated diff is part of the workflow when the TSV changes.

## What this generator does NOT do

- It does NOT generate executor function bodies.  Those are
  hand-written in the appropriate Box library
  (eBoxLib, fBoxLib, mBoxLib, palBoxLib, cBoxLib).
- It does NOT generate test stubs (yet).  When the test framework is
  chosen, a per-row dispatch verification test will be added here.
