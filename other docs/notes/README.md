# docs/notes

Hand-written design memos, learnings from debugging sessions,
architectural decision records (ADRs), and other prose that doesn't
belong in the formal SPEC document.

## Naming conventions

- `ADR-NNNN-short-title.md` -- numbered architectural decision
  records.  One ADR per significant design decision.  Use the
  Michael Nygard ADR template: Context, Decision, Status,
  Consequences.
- `learnings/YYYY-MM-DD-topic.md` -- session learnings.  Short
  notes capturing a non-obvious fact discovered during debugging
  that future sessions will benefit from knowing.
- `proposals/YYYY-MM-DD-title.md` -- proposed changes that warrant
  discussion before being adopted.  Becomes an ADR once decided.

## What goes here vs. SPEC.md

- SPEC.md: the contract.  What the system is, what it must do.
- docs/notes/: the journey.  Why we made certain decisions, what
  we learned along the way, what alternatives we rejected and why.

If a note's content becomes load-bearing for future implementation,
it gets promoted into SPEC.md.
