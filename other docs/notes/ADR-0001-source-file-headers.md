# ADR-0001: Source File Header / Copyright Block

## Status

Accepted -- 2026-05-05

## Context

Every C++ source and header file in EmulatR opens with a standard
copyright block.  This serves three purposes:

1. **Legal.**  Asserts copyright and license terms for downstream
   users and forks.
2. **Attribution.**  Records the project architect and the AI
   collaboration credits for transparency about authorship.
3. **Discoverability.**  Identifies each file by name, provides
   contact information for the project, and points at the public
   documentation site.

V3 (`D:\EmulatR\EmulatRAppUni`) used a similar pattern; this ADR
canonicalizes the V4 form and fixes the small inconsistencies that
crept into V3 (varying box-comment widths, occasional missing
attribution lines).

## Decision

Every new C++ source and header file under
`D:\EmulatR\EmulatRAppUniV4\Emulatr\` opens with the canonical block
below, with the per-file `{FILENAME}` and `{DESCRIPTION}` fields
filled in.

### Canonical block (C++ source and header)

```
// ============================================================================
// {FILENAME} -- {ONE-LINE DESCRIPTION}
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
```

### Field rules

- `{FILENAME}` is the file's own basename, including extension.
  Example: `PipelineSlot.h`, `executeBIS.cpp`, `OpcodeTable.cpp`.
- `{ONE-LINE DESCRIPTION}` is at most 60 characters and describes the
  file's purpose.  For executor `.cpp` files the description names
  the executor and its mnemonic.  Example: `executeBIS -- integer
  logical OR executor`.  For type headers the description names the
  type or interface.  Example: `PipelineSlot.h -- per-cycle pipeline
  data carrier`.
- The two `// ============================================================================`
  rule lines are exactly 80 characters wide (78 dashes plus `// `).
- All content is ASCII(128).  No em-dashes, no smart quotes, no
  Unicode arrows, no box-drawing glyphs.  Substitute `--` for
  em-dashes.  See SPEC_execution_model.md section 0 (ASCII source
  rule).

### Non-C++ files

For non-C++ files the same content is expressed in the file's native
comment syntax:

- **CMakeLists.txt / .cmake:** `#` line comments matching the same
  layout.
- **Markdown (.md):** comment block via `<!-- -->` is optional;
  Markdown files in `docs/`, `journals/`, and the SPEC family
  typically use a top-of-file H1 plus a "**Status:**" line instead
  of a copyright block.  Copyright is implied by the `LICENSE` file
  at the project root.
- **TSV, JSON, plain text:** no header.  Authoritative content only.
  License is asserted at the project root.

### AI Collaboration line

The "AI Collaboration" line names AI assistants that materially
contributed to file authorship.

V3 listed both Claude (Anthropic) and ChatGPT (OpenAI).  V4 lists
**Claude (Anthropic)** as the AI collaborator as of 2026-05-05.  If
other AI tools contribute substantively to a file later, the project
may either:

1. Add the additional tool to the canonical block (project-wide
   change, requires updating this ADR), or
2. Add the additional tool to the "AI Collaboration" line of the
   specific files where the contribution landed (per-file noting).

Option 1 is preferred when an AI tool joins as a regular collaborator;
option 2 is appropriate for one-off assists.

### V3 carryover header addendum

When a file is brought forward from V3 (`D:\EmulatR\EmulatRAppUni\`)
under section 10 of `SPEC_execution_model.md`, the canonical block is
followed by one of two carryover lines, immediately before the
description / change-log block:

- For straight reuse:
  `// EmulatR V3 reuse, no semantic change -- source: <V3-relative-path>`
- For carryover with V4 tightening:
  `// EmulatR V3 carryover, V4-tightened -- source: <V3-relative-path>`

This makes V3 origins traceable when debugging V4.

## Consequences

### Positive

- Every file is traceable to its project, license, and authorship.
- Reviewers can scan the header to confirm a new file complies with
  the convention before reading the body.
- The AI Collaboration line is honest about how the code was
  produced and gives a recognized credit.
- The V3 carryover addendum makes file genealogy visible.

### Negative

- The header adds 13-15 lines to every file.  Non-issue at this
  project's file-count scale.
- Copyright year updates require a project-wide search-and-replace
  at year boundaries.  One-line operation; tolerable.
- The 80-column rule line is fragile to copy-paste reformatting in
  some editors.  Establish editor settings (or a pre-commit hook) to
  preserve the layout.

## Reference template (paste-ready)

A paste-ready version of the canonical block lives at
`docs/notes/templates/header_cpp.txt` for new file creation.  The
template uses literal `{FILENAME}` and `{DESCRIPTION}` placeholders
that the developer replaces.  Keep that template in sync with this
ADR; if the canonical block changes, update the template in the
same commit and bump this ADR's "Accepted" date.
