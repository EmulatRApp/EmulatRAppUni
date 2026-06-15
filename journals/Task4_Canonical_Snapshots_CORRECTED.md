# Task 4 (corrected) -- Canonical snapshot entry points

Supersedes the section-5 sketch and the dual-canonical proposal. Folds
in the empirical results from the 2026-06-05 discovery runs. ASCII(128)
only. PROSE-FIRST: this is the spec for review, not yet code.

## What changed from the proposal

Three things were settled empirically, not by reasoning:

1. v2 restore is interactive AND fault-clean (UPD> answered Enter;
   console_prompt restore log showed clean lastFault, benign excAddr).
   The gating prerequisite is met.
2. The deaf `predig_console_prompt` was MISLABELED, not poisoned:
   comment='periodic', cycle=1735308432199 (mid-tick-delay band),
   post-restore PC in the 0x7bef0/0x1c6xxx delay loop. It is a
   pre-prompt periodic auto someone cp'd and renamed. RETIRE it.
3. The `>>>` anchor is `--snapshot-on-pc 0x44518` with NO floor and NO
   occurrence index. Discovery (run 2, restore + checkpoints + bounded)
   showed yyreset 0x44518 hit=1 / count=1 before the settled prompt;
   read_with_prompt 0x70448 hit=1 immediately after. One clean hit.

## Why no cycle floor (the discovery surprise)

The proposal anticipated 0x44518 might fire many times (shared getchar
path, sub-prompts) and need a cycle-floor disambiguator. It does not --
hit=1 in the discovery window. AND a cycle floor would have been
FRAGILE anyway: two restores of the identical snapshot reached yyreset
at 1735330268239 (run 1) vs 1735317095873 (run 2) -- a 13M-cycle spread
for the same logical event, from live-console RX/interrupt-injection
nondeterminism (plink connect timing). Control flow is stable (one
hit); absolute cycle is not. So PC-occurrence anchoring is robust and
cycle anchoring is not -- landing on hit=1 dodged the fragile path.
The `--snapshot-on-pc-min-cycle` enhancement is therefore NOT needed.

## The three canonical points

- **C (`>>>`, PRIMARY)** -- the entry every downstream device/OS-boot
  session launches from. Mint: cold boot with `--snapshot-on-pc 0x44518
  --snapshot-name-tag console_prompt`. First retire at yyreset writes
  the predig and disarms autos. Self-checking acceptance: restore the
  minted file and confirm it lands at the interactive prompt
  (read_with_prompt 0x70448 blocked on input) and answers a keystroke.
  If it lands at a sub-prompt instead, THEN add a floor -- but the
  cold-boot count (overnight job) is expected to confirm hit=1.
- **B (UPD>)** -- the LFU path. Reuse the validated `predig_updprompt`
  (today's interactive UPD> test). Verify via the show config / device
  / memory diff against a live post-tick run. Likely just promote it.
- **A (pre-init)** -- debugging entry into the init block (GCT/FRU,
  PCI/ISA probe). LOWER priority: init currently works end to end, so A
  is a future-init-debugging convenience. Mint when needed, same recipe
  at the console-init dispatcher entry (PC TBD by a discovery pass).

## Open dependency on the cold-boot count

hit=1 was measured from the 1.735T RESTORE, not cold boot. The mint run
is a cold boot; if a healed-flash cold boot hits 0x44518 earlier (a
console-init sub-prompt before the settled >>>), first-hit would anchor
wrong. Semantic argument says it won't (yyreset = shell prompt reset; no
LFU detour on healed flash). One overnight cold-boot run with
`EMULATR_CHECKPOINTS=yyreset:0x44518,rwp:0x70448` settles it: if the
CKPT_SUMMARY shows yyreset hit=1 from cold boot, the anchor is proven
and C can be minted with plain `--snapshot-on-pc 0x44518`.

## Sequencing (composes with task 3)

1. Overnight cold boot -> confirm yyreset hit=1 from cold origin.
2. Mint C via `--snapshot-on-pc 0x44518` on the cold boot (or once
   confirmed, on a fast-forward+forward run -- but cold-boot mint yields
   a self-contained canonical not dependent on a soon-retired auto).
3. Land task 3 (`--snapshot-dir` / cwd pinning); bank A/B/C into the
   pinned `snapshots/canonical/`.
4. Retire/rename the mislabeled `predig_console_prompt` with a one-line
   note (mislabeled periodic auto, cycle 1.735T, mid-delay -- NOT an
   at-prompt capture) so it is never re-adopted.
5. Optional convenience: `--restore-canonical {A|B|C}` selector.

## Acceptance

- Restoring C lands an interactive `>>>` in seconds; answers a keystroke.
- Restoring B lands interactive UPD>; show config/device/memory match a
  live post-tick run.
- yyreset hit=1 confirmed from cold boot (anchor is unambiguous).
- The mislabeled predig is retired and documented.
