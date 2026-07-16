<!--
EmulatR V4 -- ES40 FAST_DECOMPRESS Lever + Module-Reset Re-Seed -- DESIGN PLAN
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree.
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Purpose: web-variant DESIGN PLAN in response to the 2026-07-14 briefing.
Implementation happens in Cowork against the live tree; Cowork is the source of
truth for current file state and line numbers. ASCII(128) only.
-->

# EmulatR V4 -- FAST_DECOMPRESS Design Plan (response to 2026-07-14 briefing)

## Executive shape

The lever is NOT "load decompressed bytes and jump." It is SNAPSHOT-RESTORE.
The faithful decompressor is a deterministic function of a fixed input; its
output at the console entry is a fixed, reproducible total machine state. Mint
that state once (faithful boot), persist it, and on subsequent silicon boots
RESTORE it instead of recomputing it. The billions of cycles eliminated are
cycles that deterministically recompute a constant -- so the trade-off is cycle
count ONLY, with provably zero architectural cost. This plan cannot hold any
other shape and remain an Oracle.

Cache artifact naming (per validated platform):  firmware.snap
  e.g.  ES40_v7_3.snap ,  DS20.snap ,  DS10_v7_3.snap
Each .snap is the complete machine state captured at pc = entryPa (the commit
boundary where control first reaches the console).

--------------------------------------------------------------------------------

## Part 1 -- LANDED: module-reset guest-memory re-seed -- RATIFIED

The re-seed is correct in principle: on b_modrst_l the SROM re-lays firmware
before the CPU re-runs BiSt -> SROM -> firmware, and the 0x5c0 reboot fault was
the decompressor running over the previous boot's dirtied memory. The shape is
right. Two rulings requested:

### Ruling 1 -- memory-only re-seed vs flash:  MEMORY-ONLY IS CORRECT (and more faithful)

Real flash is nonvolatile; a module reset does NOT re-flash the part. Leaving
flash untouched is what hardware does. Re-running the MEMBER loadSrmFirmware
(with bindFlash / seedFrom) would model a flash re-seed hardware never performs.
The FREE systemLib::loadSrmFirmware, writing memory only, is the faithful
primitive. RATIFIED.
  CONFIRM (Cowork): nothing downstream reads flash state that only the member
  loader would have re-established. If flash is genuinely stable across the
  reset, memory-only is exactly right.

### Ruling 2 -- m_palImageRelocated / Step D:  RE-ARM IT (forced)

On a real module reset the CPU re-runs the ENTIRE SROM -> firmware sequence,
including PAL relocation. The reboot re-enters the decompressor at 0x900000 and
re-runs the whole path. If m_palImageRelocated stays true from the first boot,
the Step D one-shot is suppressed on the reboot and relocation SILENTLY does not
happen -- a latent second bug of the 0x5c0 family (reboot executing against
state a fresh path would have rebuilt but did not). The member loader sets
m_palImageRelocated=false; the free loader does not touch it. Therefore
reseedFirmwareForReset() MUST explicitly set m_palImageRelocated=false before
resetToLoadedEntry(). Put it in the re-seed, not the tick block.

### The re-seed CONTRACT (resolves both rulings and prevents the next one)

  "Reproduce the architectural state a fresh SROM lay-down produces, and nothing
   else."
    Memory                -> YES (SROM re-lays it).
    PAL relocation one-shot-> YES, reset it (fresh lay-down has not relocated).
    Flash                 -> NO (SROM does not re-flash nonvolatile parts).
  Any member the initial Machine::loadSrmFirmware touches gets reset by the
  re-seed IFF a fresh SROM lay-down would have it in the pre-relocation state.
  ACTION (Cowork): audit EVERY member the initial loader initializes against
  this contract. m_palImageRelocated is unlikely to be the only stale one; each
  member the free loader leaves stale is a potential 0x5c0-family bug. The
  contract turns "did we miss one?" into a checklist.

--------------------------------------------------------------------------------

## Part 2 -- TO DESIGN: FAST_DECOMPRESS as SNAPSHOT-AT-ENTRY capture/restore

### 2.1 Why NOT Strategy B or C -- the address-realm collision

Forensic (briefing 838-846): the faithful decompressor writes PAL directly to
targetPalBase (0x600000+), entry at entryPa (~0x6005c0). The AXPBox
loadDecompressedRom cache puts the console at PA 0, entry 0x8000. These are TWO
DIFFERENT post-decompress LAYOUTS -- two address realms.

  - Realm 1 (faithful / DEC): executable console high (~0x600000), jump to
    entryPa. Ground truth -- it is what the guest firmware itself computed.
  - Realm 2 (AXPBox cache): console at PA 0, entry 0x8000. A DIFFERENT firmware's
    convention, an artifact of AXPBox's decompressor, NOT DEC SRM's.

Strategy B ("entryPc = 0x8000, confirm by compare") does not confirm an
assumption -- it ENCODES THE WRONG REALM. 0x8000 is a Realm-2 value; the DEC
path jumps to entryPa. And to run the compare that would "confirm" B, you must
observe where control actually leaves the decompressor -- which IS the Strategy-A
capture. B is strictly dominated: its validation is A, and it ships a
wrong-realm cache in the meantime. REJECT B.

Strategy C ("decompress into guest memory, no cache file") still carries the
same entry-PC unknown AND loses the checked-in, diffable artifact that pins the
observed value. REJECT C as the primary path.

DECISION: Strategy A, redefined as SNAPSHOT-AT-ENTRY. It OBSERVES the layout
(and the whole machine) rather than ASSERTING it. There is only one realm in a
snapshot -- the real one -- so a snapshot CANNOT collide realms.

### 2.2 The decisive correction -- do NOT decode-and-relocate

The lever must NOT reproduce the decompressor's decode/relocate logic. That is
exactly the C2-decode guessing the data-fidelity rule forbids: re-deriving
targetPalBase / entryPa / placement outside the firmware that owns them is
silent-corruption risk. The faithful decompressor ALREADY decoded and relocated
correctly, in the guest. The lever CAPTURES that output and REPLAYS it. Capture
Realm 1 as-is; never construct Realm 2.

### 2.3 pc = entryPa is only an INDEX into required state -- capture the whole machine

entryPa alone is necessary but radically insufficient. The console at entryPa is
not self-contained: its first instructions depend on registers, IPRs, PAL-mode
context, and memory that the decompressor established over billions of cycles. A
restore that sets pc = entryPa but leaves registers/IPRs at power-on defaults
jumps the console into garbage -- a silently broken boot that looks loaded.

Therefore the .snap artifact is the COMPLETE MACHINE STATE at entryPa, of which
pc = entryPa is one field. This is the SAME object as the TB-audit snapshot.

### 2.4 "Full" is defined -- it is the existing snapshot format

Do not invent a bespoke cache format. "Full" = whatever the existing snapshot
save/restore system serializes, because that system was built to capture ENOUGH
STATE TO RESUME EXECUTION IDENTICALLY -- which is precisely this requirement
(resume the console identically whether reached by decompressor or by lever).
Reuse it:

  ES40_v7_3.snap = a snapshot (memory regions at true addresses + R0..R31 +
  F0..F31 + PC + PAL_BASE + all IPRs + PAL-mode / whatever else the snapshot
  serializes), captured at the entryPa commit boundary.

The snapshot encodes the OBSERVED machine, not a layout convention -- which is
why it dodges the realm collision. loadDecompressedRom's Realm-2 format is NOT
reused; a snapshot-restore path supersedes it for the levered boot.

### 2.5 The capture point -- exact commit boundary

Capture at the architectural commit boundary of the entry: the decompressor's
FINAL control-transfer has RETIRED, PC now equals entryPa, and NOTHING at
entryPa has executed yet. Not mid-transfer, not after the console's first
instruction. Same "let the in-flight instruction reach commit, then snapshot"
discipline as the run-termination protocol. entryPa is OBSERVED as the target of
that final transfer, with its actual PAL bit -- never derived, never 0x8000.

### 2.6 Determinism -- why one capture is valid forever

The decompressor is a deterministic function of (compressed image, start state).
Same firmware -> same post-decompress total state -> same entryPa snapshot,
every boot. The .snap is not a lucky run; it is a deterministic function's fixed
output. This is what licenses mint-once / restore-forever, and what makes the
eliminated cycles pure recomputation of a constant.

--------------------------------------------------------------------------------

## Part 3 -- FAST_DECOMPRESS vs TB / comJIT -- PHASE-ORTHOGONAL (cannot bypass)

The lever operates at LOAD/INIT phase (it substitutes / restores a machine
state). The TB / comJIT design operates at EXECUTION phase (it dispatches
decoded blocks as instructions run). Different phases -> neither can bypass the
other; "bypass" is not even expressible between them.

  - Lever UNSET: the decompressor runs faithfully and the TB layer sees it
    exactly as any other code. The lever's existence changes nothing about TB
    operation on the faithful path. (The decompressor loop is the TB layer's
    RICHEST workload -- highest-k hot loop -- so TB MEASUREMENT always runs
    lever-UNSET, against the real decompressor.)
  - Lever SET: the TB layer, if active, sees the POST-decompressor console
    stream instead of the decompressor. That is legal (console code is TB-able)
    but a DIFFERENT workload; its TB statistics are NOT comparable to the
    faithful-path baseline. Flag any lever-set TB run as non-comparable.

INVARIANT: FAST_DECOMPRESS is a load-phase state substitution with no
execution-phase presence; it structurally cannot bypass TB/comJIT. TB
measurement is lever-unset by construction. (Same provenance discipline as
"a traced run is disqualified as timing.")

--------------------------------------------------------------------------------

## Part 4 -- The two-lever, two-mode boot flow

Two capture EVENTS per validated platform, at different times:

  GENERATION (once per firmware, the expensive boot):
    Run the faithful decompressor. At the entryPa commit boundary, take a
    complete machine snapshot. Persist it as firmware.snap. This MINTS the
    artifact; full billions of cycles, done once.

  CONSUMPTION (every subsequent silicon boot, the cheap boot):
    Restore firmware.snap instead of running the decompressor. Microseconds,
    not an hour. The decompressor never runs.

Boot flow:

  VIRTUAL mode (Oracle, no lever):
    load devices -> faithful init -> enter SRM. Unchanged.

  SILICON mode, lever ENGAGED:
    load device STRUCTURE (objects must exist to be restored into) ->
    RESTORE the complete firmware.snap for this platform ->
    enter the console directly from restored state.
    Do NOT re-run the initialization the snapshot embodies -- the restore IS
    that initialization's result. (See the ordering decision below.)

### 4.1 THE ONE DECISION COWORK MUST CONFIRM -- does the snapshot capture chipset/CSR state?

"Restart initialization" after restore is an ORDERING HAZARD of the 0x5c0
family: re-running init over a machine the snapshot already put into post-init
state can double-initialize or clobber a CSR/IPR the snapshot set. Resolution
depends on ONE fact:

  OPTION 1 (STRONGLY PREFERRED) -- snapshot is the WHOLE machine (CPU + IPRs +
    memory + chipset/CSR state). On silicon boot: restore, and SKIP the
    initialization the snapshot embodies. Init is REPLACED by the restore, not
    "restarted." Cleanest, and faithful because the faithful state at entryPa is
    a TOTAL machine state -- reproduce it as a unit.

  OPTION 2 (fallback) -- snapshot is CPU + memory + IPRs only. Run chipset init
    FIRST, then restore a snapshot that deliberately does NOT cover the CSRs init
    just set. Requires DISJOINT ownership (init owns chipset CSRs; snapshot owns
    CPU/memory/IPRs) with a proven non-overlapping seam. Riskier: any state both
    touch is a collision.

  DECIDING FACT: does the existing snapshot serialize chipset/CSR state, or only
  CPU-side state? If whole-machine -> Option 1, flow is "restore replaces init."
  If CPU-side only -> Option 2, prove the seam disjoint. Everything else follows
  from this. LEAN: Option 1 -- restoring a total state as a unit avoids the
  two-halves ordering hazard entirely.

--------------------------------------------------------------------------------

## Part 5 -- Validation plan (the Oracle claim, and the completeness proof)

The SAME snapshot machinery both validates the Oracle claim AND proves the
snapshot is complete enough for the lever to be faithful. Two stages:

  STAGE 1 -- ENTRY-SNAPSHOT EQUALITY (round-trip completeness):
    Faithful path reaches entryPa with total state S. Restore S into a fresh
    machine; snapshot that at entryPa; memcmp. Bit-identical => the snapshot
    round-trips completely (nothing captured is lost on restore).

  STAGE 2 -- TRACE-TO-P00 EQUALITY (behavioral completeness):
    Run BOTH paths forward from entryPa. Traces identical to P00 => no
    depended-upon state was OMITTED from the snapshot. If a snapshot-omitted IPR
    mattered, Stage 1 could pass while Stage 2 diverges downstream -- which flags
    the incompleteness precisely.

  ACCEPTANCE: Stage 1 bit-equal AND Stage 2 trace-equal to P00. Then the .snap
  is complete FOR THIS FIRMWARE's console path and the Oracle claim holds:
  with the lever UNSET EmulatR runs the full faithful decompressor and IS the
  Oracle; with it SET the state handed to the console is byte-for-byte identical,
  so downstream execution is identical -- the lever changes only CYCLE COUNT, a
  named determinism/timing trade-off.

WHAT MUST BE EQUAL: the COMPLETE snapshot image (not an enumerated "relevant
IPRs" guess). Snapshot both paths at entryPa and memcmp; if the snapshot is
complete, bit-identical snapshots PROVE the Oracle claim with no hand-wave. This
also, by construction, catches any address-realm confusion -- a wrong-realm
restore diverges at the entry boundary, before a single console instruction runs.

SNAPSHOT-COMPLETENESS OBLIGATION (now safety-critical, not academic): confirm
that "complete enough to restore V4" (what save/restore needed) == "complete
enough to define entryPa equivalence" (what the lever needs). Any state
save/restore tolerated omitting, that the console depends on, is a silent
FAST_DECOMPRESS corruption. Stage 1 + Stage 2 together are the proof.

--------------------------------------------------------------------------------

## Part 6 -- Wiring (once Part 4.1 is decided)

- main.cpp fmt==Srm faithful branch: if getenv EMULATR_FAST_DECOMPRESS is set,
  and firmware.snap exists for this platform, RESTORE it (snapshot path) instead
  of loadSrmFirmware; if the .snap is missing, MINT it (faithful boot + capture
  at entryPa + persist), then proceed. Unset -> unchanged faithful path (Oracle
  default).
- Gate name: EMULATR_FAST_DECOMPRESS (retain).
- Reset re-seed (Part 1): on a module reset in levered mode, the clean path is a
  snapshot RESTORE (LoadMode gains a Snapshot mode, or the Decompressed re-seed
  is replaced by snapshot-restore for the levered ES40). Cowork to reconcile
  reseedFirmwareForReset() with the snapshot path so a levered reboot also
  arrives at entryPa state cleanly -- this composes with Part 1's contract.
- firmwareLib: still needed only to GENERATE (host-decompress) if a future
  strategy mints without a full faithful boot; for snapshot-at-entry the mint IS
  the faithful boot, so firmwareLib is not on the restore path. Keep the CMake
  integration (inflate.c + decompressFirmware.c as C) for generation tooling.

--------------------------------------------------------------------------------

## Part 7 -- ES40 bifurcation (confirmed)

DS10/DS20 unaffected: the lever is unset by default (Oracle path), and the
silicon module-reset triad that drives the re-seed / restore is ES40-only
(DS10/DS20 never raise it). Even with the lever SET, it alters only init/reset
on the ES40 path. Each validated platform owns its own firmware.snap:
ES40_v7_3.snap, DS20.snap, DS10_v7_3.snap -- the same validated set that bounds
the firmware-agnostic scope elsewhere, for the same reason: a .snap can be
minted only for a firmware with a trusted faithful decompressor run.

--------------------------------------------------------------------------------

## Deliverable summary

1. Part 1 re-seed RATIFIED: memory-only correct (more faithful); re-arm
   m_palImageRelocated=false in the re-seed; adopt the re-seed CONTRACT and audit
   all initial-loader members against it.
2. Part 2 strategy: A, redefined as SNAPSHOT-AT-ENTRY. B and C rejected on the
   address-realm collision, not merely the entry-PC unknown. Do NOT
   decode-and-relocate; CAPTURE the faithful output. Artifact = firmware.snap =
   complete machine state at pc=entryPa, using the EXISTING snapshot format.
3. Part 3: FAST_DECOMPRESS and TB/comJIT are PHASE-ORTHOGONAL; the lever cannot
   bypass the execution-phase TB layer. TB measurement is lever-unset.
4. Part 4: two-lever boot flow (generation mints .snap; consumption restores it).
   ONE decision for Cowork: does the snapshot capture chipset/CSR state ->
   Option 1 (restore replaces init, PREFERRED) vs Option 2 (init-then-overlay,
   prove disjoint seam).
5. Part 5: Oracle proof = Stage 1 entry-snapshot bit-equality + Stage 2
   trace-to-P00 equality. Equal set = the COMPLETE snapshot image. This proof is
   also the realm-collision detector and the completeness proof.
6. ES40 bifurcation holds end to end.

## Standing rules honored

Discuss before code; this is a PLAN, Cowork produces diffs. ASCII(128) only;
provisional values marked _PROVISIONAL and HRM-verified before C2 decode
(entryPa / targetPalBase are OBSERVED, not provisional, once captured); bounded
trace windows; verify every file write via bash. Cowork is source of truth for
current line numbers.
