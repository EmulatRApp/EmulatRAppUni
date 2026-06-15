# docs/generated

Code-generator outputs that are documentation rather than source.

## DO NOT EDIT these files by hand

Re-run the generator (`grainFactoryLib/codegen/`) to update.

## Files

- `InstructionMatrix.md` -- human-readable Markdown table of every
  (opcode, function, mnemonic, type, box, personality) row from
  `grainFactoryLib/GrainMaster.tsv`.  Useful for reviewing the
  instruction set coverage at a glance and as a citation target
  in design discussions.

## Why generated documentation lives in source control

So that reviewers can see, in a pull request diff, what changed in
the architectural reference when the TSV is edited.  Without the
generated doc in source control, a TSV change would have no
human-readable counterpart in the diff.
