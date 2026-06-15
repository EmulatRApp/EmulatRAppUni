<!--
EmulatR V4 -- Spec: overnight cold-boot mint run (autosnapshot off + cold boot)
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Status: PROSE-FIRST. Spec for review, not yet code. Supersedes the
autosnapshot + prune knobs draft. Composes with Task 3 (--snapshot-dir)
and Task 4 (canonical snapshots; C anchor 0x44518). ASCII(128) only.
-->

# Spec: overnight cold-boot mint run -- one snapshot at the target PC

## What changed, and why it shrank

The earlier draft speced an autosnapshot cadence knob plus a prune /
retention ring, to keep periodic autosaves bounded across a trillion-
cycle grind. That solved the wrong problem. The overnight run does not
want periodic autos at all -- it wants exactly one snapshot, minted at
the target PC. With periodic autos turned off there is no disk cliff and
nothing to prune. The spec collapses to two switches plus one safety,
and the prune/retention machinery moves to backlog cleanup.

## The cliff this avoids (motivation)

Current behavior: autosave every 10M cycles, keep-all (prune opt-in, not
wired). Cold boot to yyreset is ~1,735,000M cycles, so 10M cadence gives
~173,500 autosaves at ~68 MB each -- roughly 11.8 PB. The disk fills
inside the first one to two percent of the boot, hours before the
prompt. Turning periodic autos OFF for the mint run removes this
entirely; no cadence or prune tuning is required to unblock Task 4.

(The 219 autos / ~15 GB already on disk came from the short May debug
runs of a few hundred M cycles each, not from a full boot -- which is
why keep-all has not bitten yet. See backlog cleanup below.)

## The two CLI switches (the whole new surface)

Both expose behavior that already exists internally; this is wiring, not
new mechanism.

1. `--autosnapshot off`
   Master disable for periodic autosaves. Backs the existing
   `Machine::setAutoSnapshotEnabled(false)` path used by the test
   fixture. With this set, the run produces no `auto_*` files; the only
   capture is the named one from `--snapshot-on-pc`. Default remains on
   for ordinary debug runs.

2. `--no-autoload`
   Suppress autoload-newest at startup so the run is a genuine cold
   boot. This is the one piece that is NOT optional: autoload-newest
   will otherwise pick up a banked `*.axpsnap` and silently turn the
   "cold boot" into a restore -- exactly the origin mismatch Task 4
   flagged for the hit-count, since yyreset hit=1 must be proven from
   COLD origin, not from the 1.735T restore. Equivalent workaround:
   point `--snapshot-dir` (Task 3) at an empty scratch directory.

Env equivalents (`EMULATR_AUTOSNAP=off`, `EMULATR_NO_AUTOLOAD=1`)
optional, same pattern as the trace dir.

## The mint itself (already exists)

`--snapshot-on-pc 0x44518 --snapshot-name-tag console_prompt` writes one
named file on first retire at yyreset and disarms further capture. Named
files are not autos, so they are never subject to prune regardless of
the autosnapshot setting; with autos off it is the only file the run
produces.

At-PC, not just-before: yyreset (0x44518) is the prompt-reset entry, not
an output-emission point, so capturing AT first retire there is correct.
"Just before" matters only when the target PC emits something you must
capture ahead of so the resume reproduces it (the THR-write logic for
the banner anchor). yyreset emits nothing; at-PC lands at the top of the
prompt setup, and the acceptance check below confirms the spot.

## The run still has two jobs -- keep the checkpoints and the bound

The mint gives you C. But Task 4's open dependency is proving yyreset is
hit=1 from cold origin, and that needs the `CKPT_SUMMARY`, which only
emits when the run exits. So the run must stay checkpoint-armed and
bounded:

- `EMULATR_CHECKPOINTS=yyreset:0x44518,rwp:0x70448` -- the cold-origin
  hit count.
- `--max-cycles <~1,800,000M>` -- a bit past the expected yyreset cycle
  so the run exits and emits the summary.

One bounded, autoload-suppressed, autos-off cold run does both: mints C
at first 0x44518, continues to the cap, exits, and the summary confirms
the hit count from cold. If it returns yyreset hit=1, the anchor is
proven and C is self-contained.

## The overnight invocation

    EMULATR_CHECKPOINTS=yyreset:0x44518,rwp:0x70448 \
      Emulatr.exe --firmware firmware/ds10_v7_3.exe \
      --no-autoload \
      --autosnapshot off \
      --snapshot-on-pc 0x44518 --snapshot-name-tag console_prompt \
      --max-cycles 1800000000000 \
      > traces/coldmint_<timestamp>.log 2>&1

(Adjust `--max-cycles` once the live `_break.trc` first-hit cycle for
yyreset narrows the prompt landing. The value above is a safe upper
bound a hair over the observed ~1.735T.)

## Acceptance

- The run completes without writing any `auto_*` files (autos off) and
  without filling disk.
- It is a genuine cold boot: cycleCount starts at 0, not a restore.
- `CKPT_SUMMARY` reports yyreset hit=1 from cold origin (anchor proven).
- It mints `console_prompt`; restoring that file lands an interactive
  `>>>` (blocked at read_with_prompt 0x70448) and answers a keystroke.

## Demoted to backlog cleanup (not a blocker for anything)

The autosnapshot cadence knob, the retention ring, and the prune policy
are now a someday-cleanup for the 219-auto / ~15 GB backlog, not a
prerequisite. When that cleanup is taken up, the one rule that must hold
is the prune-safety invariant: a prune may delete ONLY
`auto_<ts>_<cyc>.axpsnap` files in the autos directory, and must NEVER
touch `auto_halt_*`, any `--snapshot-name-tag` / `predig_*` file, or
anything under `snapshots/canonical/` (prune must not recurse into the
canonical subdir). A prune that reaches a canonical or the halt capture
is catastrophic; the cleanup's test must assert those survive. Scope
prune by filename allowlist, top-level only, in the autos dir set by
Task 3's `--snapshot-dir`.

## Standing rules

ASCII(128) only; ADR-0001 headers; doctest CHECK only; prose-first on
anything non-trivial; surgical edits; Cowork verifies live file state
and line numbers before editing.
