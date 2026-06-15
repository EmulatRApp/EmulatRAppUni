<!--
EmulatR V4 -- Cowork hand-off: post-prompt hygiene + milestone lock
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Companion: Roadmap_Post_SRM_Prompt.md (2026-06-05).
Purpose: instructional hand-off for Cowork. Land the section-0 hygiene
batch plus the boot-to-prompt regression lock. Cross-stream device and
OS-boot ordering (roadmap sections 1-6) is NOT in scope here; it is
pending Tim's disk-vs-network first-boot decision. ASCII(128) only.
-->

# EmulatR V4 -- Cowork hand-off (post `>>>`): hygiene + milestone lock

## What this is, and what it is not

In scope: the dependency-free, already-decided batch from roadmap
section 0, plus two milestone-locking items -- the restore-to-prompt
entry point pulled forward from section 5, and a boot regression test
that the roadmap does not yet have. None of this needs the open
cross-stream decision resolved, and all of it is do-before-new-work.

Out of scope, do NOT start: G4 PCI enumeration (section 2), the
G5/G6/G7/G8 device models (section 3), the OS-boot arc (section 4),
SRM ENV persistence (section 1), and the boot profiler (section 6).
These wait on Tim's first-boot route decision and, for G4, on a captured
config-access transcript. Also do NOT flip the halt-button / AUTO_ACTION
input bit yet: there is no boot device, so AUTO_ACTION would only loop.
Leave the banner reading "Halt Button is IN."

Source of truth: Cowork holds live file state. Treat every path, symbol,
and structure named here as a point-in-time reference carried over from
the roadmap, not as verified current state. Confirm names and locations
in the tree before editing. Line numbers are deliberately omitted.

## Workflow for this batch (standing rules apply)

- Discuss before code on the non-trivial items, flagged [PROSE FIRST]:
  propose the concrete edit shape as prose -- the files, the change, and
  the acceptance check -- and wait for Tim's approval before writing.
- ASCII(128) only in all file content. ADR-0001 header block on any new
  source/header, plus an inline comment at each changed line referencing
  it. Include guards, never #pragma once. doctest CHECK only. Prefer
  surgical edits over rewrites.
- After each task: verify the build is green and that a cold boot still
  reaches `>>>`. Report that result before moving to the next task.

## Tasks (dependency order)

### 1. Land the three uncommitted changesets [git, do first]
Establish a green committed baseline before anything else moves. The
three Windows-side changesets per the roadmap:
1. LDQP / STQP / VPTB `_vms` fix plus the fBox stub repair.
2. snapshot kChipsetVersion 2.
3. boot profiler.
Confirm the changeset boundaries are unambiguous (ask Tim if any file
straddles two), verify the tree builds green, then land as three
separate commits in the project message style. Do not bundle them.

### 2. Codegen orphan guard (genGrains.py) [PROSE FIRST]
Make genGrains.py fail, or warn loudly (Tim's call), when a
handwritten.tsv entry matches no derived leaf name. This closes the
silent-stub class that already bit SCBB, LDQP/STQP, and VPTB. ~10 lines.
Propose: where in the generator the cross-check belongs, fail vs warn,
and the message format. Acceptance: a deliberately orphaned tsv entry
trips the guard; a clean tsv runs unchanged.

### 3. Flash + snapshot CWD pinning [PROSE FIRST]
Active foot-gun. `ds10_flash.rom` and `snapshots/` resolve against the
launcher CWD, so a VS launch (build dir) and a bash launch (project dir)
diverge: the updated flash is in the project dir, the build-dir copy is
stale, and a VS launch boots back into LFU. Add explicit path control in
the trace-dir pattern -- `--flash-path` and `--snapshot-dir` CLI, and/or
`EMULATR_FLASH_PATH` / `EMULATR_SNAPSHOT_DIR` env. Propose: CLI vs env vs
both, precedence order, and default-resolution behavior. Acceptance:
both launch styles resolve the same flash and snapshot dir. Document the
new knobs.

### 4. Canonical restore-to-`>>>` snapshot entry point [PROSE FIRST]
Pulled forward from section 5 because it is the iteration multiplier for
every later device and OS-boot task -- restore in seconds rather than a
full cold boot. Define the canonical predig snapshot that restores
directly to an interactive `>>>`, and the documented launch path to it.
Propose: which banked v2 snapshot becomes canonical, how it is selected,
and how it composes with the task-3 cwd pinning. Acceptance: one
documented command brings up an interactive prompt from the snapshot.

### 5. Boot-to-`>>>` regression test [PROSE FIRST] [milestone lock]
The gap: nothing currently asserts that a cold boot still reaches the
prompt, and every device added later perturbs the I/O path and can
silently regress it. Add a regression test (doctest CHECK) that asserts
a cold boot reaches an interactive `>>>`. Land this BEFORE the diagnostic
removal in task 6 and before any future device work, so it protects
them. Propose: detection method (console-output signature vs snapshot
state compare), a runtime budget, and where it lives in the test tree.

### 6. TEMP diagnostic removal
Now protected by task 5. Strip the temporary probes per the roadmap:
PICTRACE; UARTBP #9 / #10 / #9d / #10d; the DIVERT-REI ledger; the
C970 / FCLOSE / FPIP watches; and the EMULATR_MEMDIAG-gated
PipelineDriver lexer probe. Enumerate the exact removal sites first,
remove, then confirm build-green and that the task-5 regression still
passes. Mechanical but wide -- land it as one reviewable changeset.

### 7. Console transport defaults + line-doubling check
Re-default SRMConsoleDevice::Config to the canonical transport: port
10023, echo off (the guest echoes), IAC guard on. Document the defaults.
While here, test one hypothesis before treating it as separate work: the
`show device` line-doubling (also seen at UPD>) may share a root cause
with the echo default -- a terminal echo / CRLF handling issue in the
transport, not a per-command bug. Check whether correcting the echo
default clears the doubling.

## Report back
For tasks 2, 3, 4, 5: return the prose proposal and wait for approval
before editing. For tasks 1, 6, 7: proceed, and report build-green plus
a boot-to-`>>>` confirmation after each. Flag anything found in the tree
that contradicts the roadmap's stated current state.

## Held for the next hand-off (blocked on Tim's decision)
G4 PCI enumeration plus BAR machinery is the common prerequisite for both
G5 (tulip NIC) and G6 (SCSI), so it is the first real workstream once the
first-boot route is set. The pending decision -- boot the first OS from
disk (port V1 scsiCoreLib, or possibly an IDE/dqa path via the Cypress
82C693) or from network (tulip plus MOP/BOOTP/TFTP) -- fixes whether G6
or G5 lands on the critical path. A shared device-interrupt-delivery
(DRIR/DIM) path and a deterministic DMA/transfer seam should be factored
once, before the first DMA-capable device, rather than built twice. The
G4 enumeration spec is the next artifact to draft once the route is
chosen.
