n<!--
EmulatR V4 -- ES40 FAST-DECOMPRESS Lever + Module-Reset Re-Seed Design Briefing
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Purpose: hand-off document for the claude.ai web variant to produce the
DESIGN PLAN for the EMULATR_FAST_DECOMPRESS init lever and to ratify the
already-landed module-reset memory re-seed.  Implementation happens in Cowork
against the live tree.  ASCII(128) only.
-->

# EmulatR V4 -- ES40 FAST_DECOMPRESS Lever + Module-Reset Re-Seed Briefing

## What this document is for

The web variant's deliverable is a design plan, not generated code. Two things
are on the table:

1. RATIFY (or correct) the module-reset guest-memory re-seed that has ALREADY
   been landed in Cowork (it fixes the reboot fault at PC 0x5c0).
2. DECIDE the representation + generation strategy for an init-time
   EMULATR_FAST_DECOMPRESS lever that lets the ES40 boot skip the
   multi-billion-cycle guest SROM decompressor while remaining a faithful
   Oracle. The open question is how the accelerated path obtains the console
   entry PC without guessing a C2-decode value.

Cowork (the agent with live file access) turns the plan into diffs, verifies
line numbers, the build, and boot behavior. Treat all file excerpts and line
numbers here as a point-in-time snapshot (2026-07-14). Cowork is the source of
truth for current file state.

## Background: the two problems, one root

The ES40 silicon-mode boot has two coupled symptoms downstream of the
now-modeled TIG module reset:

- HANG: "Initializing...." stall was the unmodeled TIG-bus system reset. That
  is modeled now (co-gated reset triad -> chipset.reset() + reboot). Verified:
  triad detected, module reset fired, reboot re-entered the decompressor at
  pc=0x900000, cyc=0.
- HALT-ON-REBOOT: the reboot then cleanly faulted at PC 0x5c0 (kFaultHalt,
  HaltedClean, ~4.19M cycles). Root cause (CONFIRM-3 hazard 1): the module
  reset restored CPU + chipset CSRs but NOT guest memory, so the reboot re-ran
  the decompressor over the PREVIOUS boot's dirtied memory and faulted.

The re-seed (below) addresses HALT-ON-REBOOT. The FAST_DECOMPRESS lever is a
separate, additive acceleration that reuses the same "which loader ran" plumbing.

## Part 1 -- LANDED: module-reset guest-memory re-seed (please ratify)

On a real b_modrst_l reset the SROM re-lays the firmware image into memory
before the CPU re-runs BiSt -> SROM -> firmware. V4 was not doing the re-lay.
The fix captures WHICH loader ran at boot and re-runs that same free loader on
reset, so the reboot always decompresses/runs on a CLEAN image.

Landed seams (2026-07-14):

- systemLib/Machine.h:556-558 -- new
    enum class LoadMode : uint8_t { None, Raw, Srm, Decompressed };
    LoadMode              m_loadMode = LoadMode::None;
    std::filesystem::path m_firmwareSrcPath;
- systemLib/Machine.h:188 -- new decl
    bool reseedFirmwareForReset() noexcept;
- systemLib/Machine.cpp:988-989 -- loadSrmFirmware captures
    m_loadMode = LoadMode::Srm;  m_firmwareSrcPath = path;
- systemLib/Machine.cpp:1025-1026 -- loadDecompressedRom captures
    m_loadMode = LoadMode::Decompressed;  m_firmwareSrcPath = path;
- systemLib/Machine.cpp:1094-1117 -- new reseedFirmwareForReset():
    Srm          -> systemLib::loadSrmFirmware(mem, m_firmwareSrcPath, m_srmLoadPa)
    Decompressed -> systemLib::loadDecompressedRom(mem, m_firmwareSrcPath)
    Raw / None   -> return false (CPU-only re-entry; resetToLoadedEntry still runs)
  The returned descriptor/payload are unchanged from the initial load, so they
  are intentionally discarded; only guest memory is re-written.
- systemLib/Machine.cpp:1517-1527 -- systemTick reset block now:
    m_chipset.clearTigResetRequest();
    m_chipset.reset();          // CSR re-init (HRM Chapter-10 reset values)
    reseedFirmwareForReset();   // re-lay a CLEAN firmware image (SROM's job)
    resetToLoadedEntry();       // CPU re-enter the loaded entry
    return true;
  All still inside #if EMULATR_BRINGUP_PROBES, behind EMULATR_TIG_RESET; ES40
  module-reset only (DS10/DS20 never raise the triad).

Questions for the web variant on Part 1:

- Is re-running the FREE loader the correct re-seed, or should the reset re-lay
  from a cached in-memory copy of the original image (avoid re-reading the file
  and re-seeding flash)? Note: reseedFirmwareForReset calls the FREE
  systemLib::loadSrmFirmware, which writes memory only -- it does NOT repeat the
  Machine::loadSrmFirmware member's flash bindFlash/seedFrom. Confirm that
  memory-only re-seed is the faithful SROM behavior and that leaving flash
  untouched across a module reset is correct (real flash is nonvolatile, so
  leaving it is arguably MORE faithful).
- Does the module reset also need to re-arm m_palImageRelocated / the Step D
  one-shot? loadSrmFirmware sets m_palImageRelocated=false; the free loader does
  not touch the member. If the reboot must re-run Step D relocation, the member
  needs an explicit reset here. Please rule on this.

## Part 2 -- TO DESIGN: EMULATR_FAST_DECOMPRESS init lever

### Goal

Let the ES40 boot skip the guest SROM decompressor (multi-billion cycles, ~1hr
wall) and instead load a host-decompressed console image directly, WITHOUT
sacrificing Oracle fidelity. Default (unset) stays the faithful compressed path.

### Existing plumbing this reuses

main.cpp:257-271 already branches between three loaders:

    if (Auto && looksLikeRom)     loadDecompressedRom(path)   // pre-decompressed cache
    else if (fmt == Srm)          loadSrmFirmware(path, srmLoadPa)  // FAITHFUL default
    else                          loadFirmware(path, loadPa, startPa)

systemLib/SrmLoader.cpp:257-338 loadDecompressedRom expects an AXPBox-style
decompressed cache:

    [0x00] entryPc  (u64 LE, low bit = PALmode)
    [0x08] PAL_BASE (u64 LE)
    [0x10] console image, exactly 0x200000 bytes, copied verbatim to guest PA 0x0
    total file size == 16 + 0x200000 == 0x200010

firmwareLib (clean-room, already scaffolded; NO DEC source) provides:

    long emulatrDecompressFirmware(const unsigned char* compressed,
                                   long compressedFileSize,
                                   unsigned char* out, long outCap,
                                   unsigned* targetBaseOut);

It finds the "WimC" header, DEFLATE-inflates (Mark Adler public-domain
inflate.c) into the caller buffer, and returns decompressed length +
targetBaseOut (the WimC target load base == PAL_BASE, e.g. 0x600000).

### The blocking unknown

emulatrDecompressFirmware yields the decompressed BYTES and the PAL_BASE, but
NOT the console ENTRY PC that loadDecompressedRom needs at header offset 0x00
(and not the exact 0x200000-byte window within the decompressed output, nor the
low-bit PAL flag on the entry). On real hardware the decompressor computes the
entry and jumps to it; that value is a C2-decode quantity. Per the EmulatR data-
fidelity rule, guessing it risks silent PAL/boot corruption. We must PIN it, not
assume it.

Note: es40_v7_3.rom in the tree (out/build/cli/firmware) is NOT a decompressed
cache -- it is the 2 MB flash NVRAM backing (Alpha instructions at offset 0,
size 0x200000, no 16-byte cache header). So there is no existing AXPBox cache in
the tree to diff against.

### Three candidate strategies (web variant to choose + refine)

STRATEGY A -- Capture-based (highest fidelity, most work)
  Instrument the FAITHFUL boot to snapshot, at the exact instruction where
  control leaves the decompressor and enters the console: (a) the PC + PAL bit,
  (b) PAL_BASE, (c) the 0x200000-byte guest window that becomes the console.
  Write those into the cache header + body. The accelerated boot then reproduces
  the captured architectural state byte-for-byte. Validation: boot the cache and
  confirm identical trace to the faithful path from that PC onward. This is the
  no-guess path; the entry PC is OBSERVED, never assumed.

STRATEGY B -- Header-derived + boot-compare (faster, assumption to confirm)
  Derive the header directly: entryPc = 0x8000 (BASE_OF_DECOMPRESSED_IMAGE from
  decomp.h, ROM section) with PAL bit per the faithful entry, PAL_BASE =
  targetBaseOut, console window = the 0x200000 bytes at the image base that maps
  to PA 0. Then VALIDATE by boot-comparison against the faithful path. If the
  compare diverges, fall back to Strategy A. Risk: rests on the 0x8000 entry
  assumption until the compare confirms it.

STRATEGY C -- Decompress-into-guest-memory (no cache file)
  Skip the cache file entirely: at init, host-decompress directly into guest
  memory to reproduce the post-decompressor memory image, then set the CPU entry
  to the faithful post-decompressor PC. Same entry-PC unknown as A/B, but no
  on-disk cache format to get right, and nothing new to ship. The entry PC still
  must be captured (as in A) or derived (as in B).

### Where the lever would be wired (once the strategy is fixed)

- main.cpp:264-268 (the fmt==Srm faithful branch): if getenv
  EMULATR_FAST_DECOMPRESS is set, generate the decompressed cache (via
  firmwareLib, if missing) and call loadDecompressedRom instead of
  loadSrmFirmware. Unset -> unchanged faithful path (the Oracle default).
- CACHE PATH CONVENTION (PINNED 2026-07-14, Tim): the decompressed cache lives
  in the run dir's ./firmware directory, named <stem>.bin -- the firmware stem
  with a .bin extension replacing .exe.  So firmware/ES40_v7_3.exe ->
  firmware/ES40_v7_3.bin (co-located with the firmware, portable with the run
  dir).  main.cpp derives it as firmwarePath.parent_path() / (stem + ".bin").
- The reset re-seed (Part 1) then automatically re-lays via
  LoadMode::Decompressed on a module reset, so accelerated mode reboots cleanly
  too, with no extra work.
- CMake: add firmwareLib (compile inflate.c + decompressFirmware.c as C; link
  into the main target). Cache generation is host-side, once per firmware file.

### Faithfulness / Oracle framing (please confirm the language)

The claim we want to be able to make: with EMULATR_FAST_DECOMPRESS UNSET,
EmulatR runs the full faithful decompressor and IS the Oracle. With it SET, the
architectural state handed to the console is byte-for-byte identical to what the
faithful decompressor produces (validated by boot-comparison), so downstream
execution is identical -- the lever changes only the CYCLE COUNT spent in
decompression, which is named as an explicit determinism/timing trade-off in the
spec. Confirm this framing is sound, and specify exactly what must be equal
(memory image + PC + PAL_BASE + relevant IPRs) for the Oracle claim to hold.

### Licensing (already resolved; included for completeness)

firmwareLib ships only inflate.c (Mark Adler, public domain) + the original
eNVy WimC wrapper. The DEC-copyrighted references (ev6_huf_decom.m64,
decompress.c/decom.c) are reference-only, NOT compiled, and must be excluded
from any distribution. The compressed firmware is the USER's own file,
transformed locally, never redistributed. See firmwareLib/NOTICE.md.

## Deliverables expected from the web variant

1. Ratify or correct Part 1 (the re-seed), including the two open rulings:
   memory-only re-seed vs flash, and whether Step D / m_palImageRelocated must
   be re-armed on module reset.
2. Choose Strategy A / B / C for Part 2 and specify, seam by seam:
   - how the console entry PC + PAL bit are OBTAINED (captured vs derived),
   - the exact cache format (or the in-memory equivalent for C),
   - the main.cpp lever wiring and the getenv gate name (EMULATR_FAST_DECOMPRESS
     unless you prefer another),
   - the firmwareLib CMake integration.
3. A validation plan proving the Oracle claim: the boot-comparison method, what
   state must be bit-equal, and the acceptance criterion (accelerated boot trace
   == faithful boot trace from the console entry PC onward, both reaching P00).
4. Confirm the ES40 bifurcation holds end to end (DS10/DS20 unaffected: lever
   unset by default, and even if set, only alters init/reset on the ES40 path).

## Out of scope for this briefing

The DS10/DS20 dqa0/dqb0 show-dev enumeration gap (#32) is a separate ALi M1543C
/ M5229 native-BAR-probe track and is NOT part of this lever work.

## RESOLUTION -- web-variant rulings applied (2026-07-14)

The web variant reviewed this briefing and returned rulings; the following are
now settled, with code landed where noted.

Part 1 (re-seed) -- RATIFIED.
  - Re-seed contract adopted verbatim: "reproduce the architectural state a
    fresh SROM lay-down produces, and nothing else."  A member is re-armed in
    reseedFirmwareForReset iff a fresh lay-down would have it in its post-load
    state.
  - Memory-only re-seed vs flash: RATIFIED memory-only.  Real flash is
    nonvolatile; a module reset does not re-flash the part, so leaving flash
    untouched is MORE faithful.  The free loader (memory only) is the right
    primitive.
  - m_palImageRelocated: RULED re-arm (forced).  Left true from the first boot
    it suppresses Step D on the reboot -- a latent 0x5c0-family bug.  LANDED:
    reseedFirmwareForReset now sets it per-mode (Srm -> false, pre-relocation;
    Decompressed -> true, console already final), so the contract lives in one
    place, not the tick block.  (systemLib/Machine.cpp reseedFirmwareForReset.)
  - Contract audit (web asked "did we miss one?"): every member the member
    loader Machine::loadSrmFirmware sets was checked against the free loader.
    Result: m_palImageRelocated is the ONLY architectural-fidelity member that
    goes stale.  m_srmDescriptor/Payload/LoadPa, m_loaded*, m_loadMode,
    m_firmwareSrcPath are identical across the reset (same image); flash is
    intentionally preserved; m_snapTriggers*, m_injectInterruptFired,
    m_nextAutoSaveCycle are diagnostic/cosmetic, not architectural, and are
    left as-is.  Audit recorded in the reseedFirmwareForReset header comment.

Part 2 (lever) -- STRATEGY A selected (capture-based).
  - B is strictly dominated: its validation IS A's capture (run the faithful
    boot, observe where control leaves the decompressor), so B does not save
    the capture work and carries a corruption window.  Cut B.
  - C (decompress into guest memory, no cache file) does not remove the
    entry-PC unknown and additionally loses the cache file as a checked-in,
    diffable record of the pinned C2-decode value.  Rejected in favor of A.
  - Capture point is defined STRUCTURALLY, not as an address: the target PC of
    the decompressor's FINAL control transfer into the console, captured at the
    instant the transfer is taken, with the PAL bit read from the actual PC/PSW
    state then.  "Target of the decompressor's exit transfer," not "PC 0x8000"
    -- an observation, robust to the entry not being 0x8000.
  - Oracle criterion sharpened to a SNAPSHOT compare, not an IPR enumeration:
    snapshot both paths at the captured console entry PC; require the snapshots
    bit-identical (whatever the snapshot serializes as architectural state);
    trace-to-P00 then follows by determinism and is the confirmation, not the
    primary test.  Reuses the snapshot-completeness discipline from the TB POC
    audit -- one definition of "architecturally equal" serves both.  Do NOT
    enumerate "PC + PAL_BASE + relevant IPRs"; "relevant" is a guess and is
    exactly where a silent divergence would hide.
  - NOT yet implemented: the capture instrumentation, the cache format writer,
    the main.cpp lever wiring, and the firmwareLib CMake integration.  These
    await the go-ahead; the capture instrumentation is the first build step.

## Part 3 -- FAST_DECOMPRESS <-> TB / comJIT phase-orthogonality (invariant)

Added on the web variant's advice (the verbal point the briefing did not ask
about).  The interaction MUST be stated in the plan before the lever is wired.

Invariant: FAST_DECOMPRESS operates at LOAD/INIT time by substituting a
pre-decompressed image; it has NO execution-phase presence.  The TB / comJIT
dispatch layer operates during EXECUTION.  They are in different phases (load
vs execute) and therefore cannot bypass each other in either direction -- the
lever is not an alternative execution engine, it is an init-time substitution
of the decompressor's OUTPUT.  When unset, the decompressor executes normally
and the TB layer sees it normally; the lever's existence changes nothing about
the TB layer on the faithful path.

Measurement-intent rules (mirroring the POC provenance discipline -- a traced
run is disqualified as timing; a fast-boot run is disqualified as a
decompressor-workload measurement):
  - The decompressor loop is the richest TB workload in the boot (highest-k hot
    loop, the poster amortization case).  TB measurement / audit runs are
    therefore FAST_DECOMPRESS-UNSET by construction, measuring the real
    decompressor execution.
  - A lever-SET TB run is legal but sees the post-decompressor console stream
    instead of the decompressor; its TB statistics are NOT comparable to the
    faithful-path baseline and MUST be flagged non-comparable.
  - The two designs are orthogonal by phase and mutually exclusive by
    measurement intent.  Do not combine configurations that change the
    workload.

## IMPLEMENTATION STATUS + TWO FINDINGS (2026-07-14)

LANDED (safe, non-corrupting; pending next build):
  - firmwareLib compiled into the Emulatr target (CMakeLists: inflate.c +
    decompressFirmware.c added beside SrmLoader).
  - main.cpp EMULATR_FAST_DECOMPRESS lever in the fmt==Srm branch.  UNSET =
    faithful decompressor = Oracle (unchanged default).  SET = load
    parent/<stem>.bin via loadDecompressedRom IFF it exists and loads; else
    print a note and FALL BACK to loadSrmFirmware.  The lever cannot corrupt:
    absent/invalid cache is a no-op that reverts to the Oracle path.
  - Reset re-seed (Part 1) already re-lays via the captured loader, so an
    accelerated boot reboots cleanly through LoadMode::Decompressed.

NOT landed (gated on the fidelity decision below): auto-generation of the
cache.  The lever routes to a cache but nothing generates one yet, so with the
lever SET today the run simply falls back to faithful.

FINDING 1 -- the console entry decode ALREADY EXISTS (supersedes the briefing's
"blocking unknown").  SrmDescriptor decodes it WITHOUT running the decompressor:
finalPC is scanned from the stub's LDA R0,disp(R26) + JSR R31,(R0) pair
(kLdaPattern 0x201A0000 / kJsrToFinalPc 0x6BE04000, SrmLoader.h:128-131), and
descriptor.entryPa() = targetPalBase + finalPC is the post-decompress console
entry the faithful path ALREADY trusts for done-detection.  So Strategy A's
"capture the entry PC" is unnecessary for the ENTRY value -- it is decoded.

FINDING 2 -- but the post-decompress MEMORY LAYOUT is the real fork, and the two
existing loaders DISAGREE:
  - Faithful path: the guest decompressor writes valid PAL directly into
    [targetPalBase, ...] = 0x600000..0x60ffff (5,120 HW_ST in cyc 0..4.19M, per
    the 2026-05-18 forensic trace), entry at entryPa (~0x6005c0).  Step D is
    DETECTION-ONLY -- the relocation COPY was deleted 2026-05-18 because it was
    destroying firmware-written PAL bytes (Machine.cpp onBeforeFetch:833-862).
  - AXPBox cache path (loadDecompressedRom): console image at PA 0x0, entry
    0x8000, palBase in the header.  A DIFFERENT layout that also boots to P00.
  Consequently a firmwareLib-generated cache reproduces the AXPBox layout, NOT
  the faithful 0x600000 layout.  For the Oracle claim, "architecturally equal at
  the console entry" must be proven by the snapshot compare (both paths reach
  the same P00 state), because the two layouts are not obviously bit-identical.

DECISION NEEDED (for the web variant / Tim) before wiring cache generation:
  (a) Accept the AXPBox-format cache (console@PA0, entry 0x8000) as the
      accelerated backing, validated by snapshot-equality at P00 -- simplest,
      reuses loadDecompressedRom as-is; OR
  (b) Have the accelerated path reproduce the FAITHFUL layout (PAL@targetPalBase,
      entry entryPa) so the accelerated and faithful states match at the console
      entry, not just at P00 -- stronger Oracle equivalence, more work (the cache
      generator must place bytes at targetPalBase and set entry = entryPa).
  firmwareLib's WimC targetBaseOut (comp[WimC+0x10]) and loadSrmFirmware's
  targetPalBase (payload[sigOffset+0x10]) are anchored differently and may
  differ -- reconcile them empirically (one instrumented decompress) as part of
  whichever route is chosen.

## SNAPSHOT-MODEL PIVOT (2026-07-14, ratified by web + landed)

The layout fork (Finding 2) is resolved by NOT constructing the second realm at
all.  The faithful decompressor already produced the correct post-decompress
image at true addresses; we CAPTURE that at the pc==entryPa handoff and REPLAY
it.  This is Strategy A realized with existing machinery -- no new cache format,
no firmwareLib decompression on the hot path.

Why the snapshot system IS Strategy A:
  - Snapshot::save serializes the COMPLETE quiescent state: full CpuState (all
    regs/IPRs/pc/palBase/mode), page-sparse guest memory at true addresses,
    chipset (cchip/dchip/pchip/devices); TsunamiTig is assert-guarded (build
    fails if it would silently drop state).  V4 resolves writeback into CpuState
    within each PipelineDriver::step, so between steps the machine == CpuState +
    memory + chipset -- nothing in-flight to lose.
  - armSnapshotOnPc(pc) mints at the first retire where pc matches -- arm it at
    descriptor.entryPa() (decoded, targetPalBase+finalPC) and one faithful boot
    mints the entry snapshot.
  - autoloadLatest / systemLib::load restore it; test_snapshot_roundtrip proves
    save->load->bit-identical (serializer completeness); the schedLib
    determinism-equivalence discipline covers downstream equality.

LANDED (main.cpp, pending build) -- EMULATR_FAST_DECOMPRESS is now a 3-mode lever:
  - unset / "faithful": guest decompressor end to end (Oracle default, unchanged).
  - "capture": faithful boot + armSnapshotOnPc(entryPa) -> mints
    snapshots/predig_entry_<stem>_cyc<N>.axpsnap.  Prints entryPa.
  - "restore": load the newest entry snapshot for the stem and re-enter at
    entryPa (skips the decompressor); NON-CORRUPTING faithful fallback if
    absent/invalid/version-mismatched.  Sets restoredFromEntrySnapshot so the
    later autoloadLatest does NOT clobber the restored state.
  Driver: tools/es40_fast_decompress.sh {capture|restore|faithful}.  The old
  loadDecompressedRom/firmwareLib .bin route is RETIRED (it encoded the AXPBox
  realm); firmwareLib stays compiled but unused on this path.

VALIDATION (gates trusting restore, task #15):
  1. Serializer completeness: run test_snapshot_roundtrip (existing doctest).
  2. Entry-boundary + downstream determinism: capture the entry snapshot, then
     compare a faithful continuation from entryPa against a restore continuation
     -- e.g. a second snapshot armed at a downstream console PC, byte-compared.
     This catches the one open detail: m_palImageRelocated is likely captured
     FALSE (it flips when entryPa is fetched, the step AFTER the pc-match mint);
     on restore, descriptor.valid re-arms onBeforeFetch so it re-flips on the
     first fetch -- the equality test confirms that self-heal (or tells us to
     pre-set it).

NOT yet landed (task #17, deferred until validation is green): reset re-restore.
In restore mode a downstream TIG module reset must re-enter via snapshot
re-load, not resetToLoadedEntry (which would reset pc=0).  Needs a
LoadMode::Snapshot that re-loads the entry snapshot and SKIPS resetToLoadedEntry.
Not required for the entryPa round-trip validation (the reset is ~14B cyc
downstream), so it is sequenced after the proof.

Honest cost: snapshots are version-tied (kCpuStateVersion/kChipsetVersion), so a
rebuild invalidates the entry snapshot (clean version-mismatch -> faithful
fallback).  One faithful (slow) capture boot per build; instant restores after.

## WITHIN-RUN REVISION (2026-07-14, Tim) -- the snapshot is a per-run artifact

Corrected model: the entry snapshot is NOT a cross-run cache; it is a within-run
module-reset accelerator.  Requirements (Tim):
  - NAME: firmware/<stem>.axpsnap (e.g. firmware/es40_v7_3.axpsnap).  The name
    binds the machine state to the exact firmware it was decoded from, which is
    what guarantees the restored state matches the consumer's run (determinism).
  - LIFETIME: one run.  Minted once at pc==entryPa.  It SURVIVES exit / safe exit
    / abort (diagnostically relevant).  It is deleted + regenerated ONLY by a
    SUBSEQUENT run, at that run's mint/save phase (delete-then-save), never read
    by a later run.  So the delete is coupled to the save, not to run start/end.
  - PURPOSE: a within-run TIG module reset re-enters at entryPa via the snapshot
    instead of re-paying the multi-billion-cycle decompression.

LANDED (replaces the cross-run capture/restore split):
  - main.cpp: EMULATR_FAST_DECOMPRESS is now faithful (default) vs "snapshot"/"1".
    In snapshot mode, after the faithful loadSrmFirmware it calls
    mach.enableEntrySnapshot(firmware/<stem>.axpsnap).
  - Machine.h: enableEntrySnapshot(path) + members m_entrySnapshotMode/Minted/Path.
  - Machine.cpp systemTick: one-shot mint when m_entrySnapshotMode &&
    !minted && srmDescriptor.valid && pc==entryPa -> std::filesystem::remove
    (delete) then systemLib::save (regenerate) to the fixed path.
  - The cross-run "restore" main.cpp branch + restoredFromEntrySnapshot +
    firmwareLib .bin routing are removed from the hot path.

STILL PENDING (task #17, the consumption) + a HAZARD to design for:
  The within-run reset re-restore is NOT wired yet.  When wired, the TIG reset
  branch must restore the entryPa snapshot's MEMORY + CpuState + chipset CSRs
  and SKIP resetToLoadedEntry -- BUT it must PRESERVE the current flash/NVRAM,
  NOT revert it to the snapshot's (entryPa-era) flash.  Rationale: real flash is
  nonvolatile; a module reset does not revert it.  The LFU-update path WRITES
  flash (new firmware/env) and THEN resets; a full snapshot load would revert
  that write and re-enter LFU (loop).  So the reset consumption is
  restore-memory+cpu+chipset-from-snapshot, PRESERVE-current-flash.  This mirrors
  the faithful reseedFirmwareForReset contract (re-lay memory, leave flash) and
  must be proven by the validation (task #15) before it is trusted.

## REUSE REFINEMENT (2026-07-14, Tim) -- checksum-validated, restore-except-flash

Correction to "not consumed by any subsequent run": determinism across runs is
already guaranteed by the snapshot LOAD VALIDATOR (checksum footer +
kCpuStateVersion + kChipsetVersion + chipset VARIANT), so the snapshot does NOT
need deleting every run -- it is REUSED when it validates and regenerated only on
mismatch.  This is the "same machinery we have today" (autoload's checksum/
version path).  Payoff: a run with a valid snapshot skips the decompressor at
INIT, not just at a within-run reset.

RESTORE-EXCEPT-FLASH (Tim's ruling): the snapshot carries the 2 MB TIG flash
image (kChipsetVersion 3).  On reuse we restore memory + CpuState + chipset CSRs
but re-point flash to the CURRENT backing (bindFlash) so live `set`/`update srm`
env (persisted to <stem>.rom / ds10_flash.rom) is honored, not reverted to the
snapshot's capture-time env.  Same leave-flash contract as reseedFirmwareForReset.
This also resolves the reset-consumption flash hazard (task #17): the reset uses
the same tryRestoreEntrySnapshot.

MODEL-AGNOSTIC (DS10 / DS20 / ES40): two isolation layers.  (1) per-firmware name
firmware/<stem>.axpsnap -- each model keys its own file.  (2) systemLib::load's
chipset-VARIANT check (Tsunami vs Typhoon) + version -- an ES40 snapshot cannot
restore into a DS10/DS20 machine (clean fail -> faithful).  The lever lives in the
shared fmt==Srm path so it applies to all models uniformly; default faithful
leaves every model unchanged.  Only the TIG module-reset consumption and the
es40_fast_decompress.sh wrapper are ES40-specific.

LANDED (main.cpp + Machine, pending build):
  - Machine::tryRestoreEntrySnapshot(path): systemLib::load (validate) -> on
    success bindFlash(m_firmwareSrcPath) + m_entrySnapshotMinted=true; false on
    absent/mismatch.
  - Machine.h decl; main.cpp snapshot mode = enableEntrySnapshot + try reuse,
    restoredFromEntrySnapshot suppresses autoloadLatest.
  - systemTick mint: no unconditional delete -- save overwrites; only reached on
    the faithful path (reuse consumed the valid case).
STILL PENDING: task #17 reset consumption (call tryRestoreEntrySnapshot on the TIG
reset, skip resetToLoadedEntry) + task #15 validation (round-trip equality gates
trusting reuse and reset).

## Standing EmulatR V4 rules (apply to all implementation work)

Discuss before code; header + inline documentation on every change; TODO
discipline; best-effort deterministic architecture with named trade-offs;
ASCII(128) only; copyright header on every generated source/spec; include guards
never #pragma once; hex radix for switch/case labels; surgical Edit over
rewrites; doctest CHECK only; no toString helper; provisional IPR/SCBD values
marked _PROVISIONAL and HRM-verified before C2 decode; bounded trace windows
only; verify every file write via bash.
