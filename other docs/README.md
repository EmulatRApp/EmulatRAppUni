# docs

Project documentation.  Hand-written design memos, learnings,
architectural decision records, and generated reference material.

## Subdirectories

- `generated/` -- codegen-emitted reference docs (e.g.,
  `InstructionMatrix.md`).  Do not edit by hand.
- `notes/` -- hand-written design memos, debugging learnings,
  architectural decision records (ADRs).

## Top-level SPEC files (at project root, not in this directory)

The canonical specification lives at the project root, not under
`docs/`:

- `SPEC.md` -- merged top-level spec (TBD, currently split into
  multiple `SPEC_*.md` files).
- `SPEC_execution_model.md` -- execution model section (drafted).
- `SPEC_project_layout.md` -- this directory tree, as a reference
  document (TBD if needed).

The reason SPEC files are at the root rather than in `docs/`: they
are load-bearing project documentation, referenced by every code
review and design discussion.  Keeping them at the root makes them
discoverable; `docs/` is for ancillary material.
