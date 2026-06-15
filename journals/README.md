# journals

Per-session debugging and design journals.  One file per session,
named `YYYY-MM-DD.md` or `journal_YYYY-MM-DD_session.md` (the
latter convention came from EmulatR; either is acceptable).

## What each journal captures

- Summary of what was attempted, decided, and fixed.
- Bugs found, their root causes, and the fixes applied.
- Followups that were identified but not addressed in this session.
- Memory updates (added or changed memory files).
- Open questions for the next session.

## Tone

Keep entries short and focused -- they are a record of decisions
and state transitions, not a full transcript.  A reader returning
to the project after a week should be able to skim recent journals
and reconstruct what changed and why.

## Pre-pivot history

Earlier journals from the EmulatR (V3) project live at
`D:\EmulatR\EmulatRAppUni\journals\`.  Notable: the
`2026-05-04.md` journal documents the BoxResult / SrmRomLoader /
BIS-grain bug chain that motivated the pivot to this V4 rewrite.
That journal is worth reading once for context but is not part of
this project's git history.

## Initial journal

`EmulatR4_proposal.txt` -- the original proposal draft that became
this project's specification.  Will be moved into this directory
from `D:\EmulatR\EmulatRAppUniV4\journals\` once Qt Creator picks
up the new directory tree.
