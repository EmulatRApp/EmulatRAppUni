<!--
EmulatR V4 -- Proposal: dual canonical snapshot entry points (A pre-init, B UPD>)
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Status: PROPOSAL for Tim's consideration. Not a commitment. Tim retains
approval authority. Companion: Roadmap_Post_SRM_Prompt.md (section 5).
ASCII(128) only.
-->

# Proposal: Dual canonical snapshot entry points (A: pre-init, B: UPD>)

## Recommendation

Feasible today with the landed Level 1 snapshot machinery. This is
mostly procedure plus validation, with minimal new code. Mint two
canonical snapshots:

- A -- pre-init: lands PC and full state just before the
  "keyboard not plugged in" banner block. Debugging entry into the
  init sequence itself (GCT/FRU build, PCI/ISA probe).
- B -- UPD>: lands at the LFU prompt, past GCT/FRU and the billions of
  intervening ticks. Skips the expensive span; work downstream.

Your read is correct: the scaffold is in place. The clarification below
is only that the snapshot must (and already does) carry more than CPU
state, and the plan leans on that.

## Why this is feasible now (what the scaffold already provides)

From the landed Level 1 subsystem (systemLib/Snapshot.{h,cpp}; roundtrip
hive in tests/systemLib/test_snapshot_roundtrip.cpp):

- Captures CpuState (GPRs/FPRs, IPRs, PS, PC, lock flag, architectural
  CC offset), GuestMemory pages (incl. the GCT/FRU region at 0x3f32000,
  the config tree, and the device tables built during the probe), the
  Tsunami Cchip/Dchip/Pchip CSR storage including the atomic DIM / IIC /
  DRIR interrupt state, and the SRM firmware staging.
- cycleCount alignment is preserved across save/restore. The interval
  timer holds no internal counter -- the cycle counter IS the counter --
  and a mid-interval resume is documented as not phantom-firing. This is
  the single invariant the billions-of-ticks jump depends on, and it is
  already correct.
- A precise capture trigger exists: `--snapshot-on-pc <PC>
  --snapshot-name-tag <tag>`.
- Level 1 restart semantics are next-fetch-boundary: capture lands at a
  clean retire boundary in native console code, with no mid-pipeline
  half-state to reconcile.
- B (the UPD> state) is already banked per the roadmap status anchor;
  A is the only genuinely new capture.

## The one reframe

This is not "bypassing" the billions of ticks; it is replaying what they
produced. Those ticks were not idle -- they built the GCT/FRU structures
in RAM, walked the bus and wrote the config tree and device tables, and
advanced the timer and interrupt state. B works because the snapshot
captured that work, not because CPU registers can recompute it. CPU
state is necessary but not sufficient; the snapshot also carries memory,
chipset, and counter state, which is exactly why the resume is faithful.

## Discovery procedure for the capture points

One deterministic cold-boot discovery run finds both anchors; a second
gated run mints both snapshots. Determinism is the whole foundation: V4
reproduces the same PC stream and the same cycle counts bit-for-bit
across runs, so an anchor found in run one is exactly reachable in run
two -- the same property that predicted Step D's cycle and the divert
landing every 1,048,576 cycles.

Discovery run (one cold boot):

- Maintain a compile-gated circular buffer of the last N retired PCs and
  their cycleCount.
- Anchor on the UART THR write, not putChar in the abstract. The THR
  write maps one-to-one to a character on the wire and is unambiguous
  about "output is happening now."
- At event one (the THR write that emits the first banner character),
  dump the ring AT the event, not at end of run -- by the time event two
  is reached the buffer has rolled past event one's context. Record the
  THR-write PC, the prior-N PCs, and the cycleCount. Back up to a clean
  retire boundary; that boundary PC plus its disambiguator is anchor A.
- Keep running. At event two (the THR write that emits the UPD> prompt),
  dump the ring again. Same extraction yields anchor B.

The gate needs a disambiguator -- a bare PC is not a unique point in
execution. The THR-write PC is hit once per character (thousands of
times), so gating on the bare PC fires on the first character ever
printed, not the intended one. For A that may be tolerable if the
keyboard-not-plugged banner is genuinely the first console output; for B
it is not, since UPD> is thousands of THR writes in. Three
disambiguators, in increasing durability:

- Cycle floor: capture at the next clean boundary at or after the
  discovered cycleCount. Simplest and inherently unique (cycleCount is
  monotonic), but tied to the exact build -- any code change shifts the
  cycle.
- Occurrence index: the Nth hit of the PC. Survives small code edits,
  because the call site outlives them even when the cycle does not.
- Data predicate: fire when the THR write carries the specific byte in
  the right context. Most precise, most work.

Recommended: occurrence index as the durable recipe, with cycle floor as
the quick path for a one-shot on an unchanged build.

This is the concrete method for the open item "identify and document A's
capture PC." And because A and B are re-minted whenever a device landing
bumps kSnapshotVersion (see Risks), this is a repeatable recipe rather
than a one-off PC hunt: re-run it on each device landing, then
re-validate with the show-config / show-device diff. The ring buffer is
also worth leaving in permanently (compile-gated) as a last-N-PCs-before
-fault diagnostic aid, independent of this use.

## Completeness and correctness contract

State inventory that must be intact at A and B (all already captured;
the work is to verify each survives roundtrip at these specific points):

- CpuState: PC with its mode bit, GPRs/FPRs, IPRs, PS, lock flag, the
  architectural CC offset.
- cycleCount: restored verbatim, never reset to a low value. (The
  non-negotiable for B.)
- GuestMemory: every touched page, including GCT/FRU at 0x3f32000, the
  config tree, the device tables, and the NVRAM/env region.
- Chipset: Cchip/Dchip/Pchip CSRs plus DIM / IIC / DRIR.
- SRM staging (settled post-relocation at both points).

Verification (empirical and cheap -- preferred over reasoning about
completeness):

1. B: load it, run `show config`, `show device`, `show memory`, and diff
   the output byte-for-byte against the live post-tick run. A match means
   the snapshot captured everything those commands depend on. A
   divergence names exactly which device-state serialization is missing.
2. A: load it, resume, and confirm the init output (keyboard-not-plugged
   block -> GCT/FRU init -> UPD>) reproduces identically to a cold boot
   crossing that point.
3. Both: confirm lastFault / excAddr at capture is benign, so restore
   does not resume into a phantom fault (precedent: the predig snapshot
   that carried a stale kFaultOpcDec).
4. cycleCount: assert the restored value equals the captured value, and
   that an RPCC read just after restore is monotonic with respect to the
   pre-capture value.

## Risks and things to check (all small)

- Fault-state residual: lastFault is carried in the snapshot; verify it
  is clean at A and B.
- Host transport: the plink/PuTTY socket is host state, not guest state.
  Restore gives a fresh connection; any bytes in flight in the host
  buffer at capture time are lost. The guest UART registers and FIFO
  restore fine. Expected, not a bug.
- File-format version: these two captures should need no CpuState or
  format change; if one ever does, bump kSnapshotVersion per existing
  discipline. Existing predig_*.axpsnap remain loadable.
- File size: ~68 MB each at the current 64 MiB wholesale format. Two
  canonical files is negligible against the existing auto backlog. The
  planned sparse format would shrink them but is not required here.

## New work versus reuse

Reuse: all capture/restore machinery, `--snapshot-on-pc`, the roundtrip
hive.

New, and small:
1. Run the discovery procedure (above) to identify and document anchors
   A and B -- each a THR-write boundary PC plus its disambiguator. B may
   reuse the banked predig instead.
2. Extend the capture gate with an occurrence count and/or cycle floor:
   `--snapshot-on-pc X --occurrence N`, or `--after-cycle C`. Today
   `--snapshot-on-pc` fires on first hit; this is the only snapshot-side
   code the technique needs.
3. Name and bank the two canonical files, pinned via the section-3
   cwd / `--snapshot-dir` / `--flash-path` work so VS and bash launches
   resolve the same files.
4. Wrap the validation diffs (above) as a small repeatable check.
5. Optional convenience: a `--restore-canonical {A|B}` selector that
   composes with the task-4 restore-to-`>>>` entry point.

## Open questions for Tim

- Is the banked UPD> predig clean enough to be canonical B, or re-mint?
- Canonical naming and location (e.g. snapshots/canonical/)?
- Should A sit at the very first banner line, or a hair earlier at the
  console-init dispatcher entry, so the whole block is steppable?
- This proposal covers A and B; the roadmap also banked a `>>>` state.
  Treat that as the task-4 restore-to-prompt point -- complementary, a
  third canonical entry rather than part of this proposal.

## Acceptance criteria

- Loading B lands an interactive UPD> in seconds; show config / device /
  memory match the live post-tick run.
- Loading A resumes and reproduces the init block through to UPD>.
- The roundtrip hive covers both canonical points.
- cycleCount is verified verbatim across both.

## Standing rules

ASCII(128) only; ADR-0001 headers; doctest CHECK only; discuss before
code on anything non-trivial; surgical edits; Cowork verifies live file
state and line numbers before editing.
