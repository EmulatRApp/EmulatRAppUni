# Snapshots -- Design Notes

**Status (Level 1):** IMPLEMENTED 2026-05-11.  Source files
`systemLib/Snapshot.h` and `systemLib/Snapshot.cpp`; test hive at
`tests/systemLib/test_snapshot_roundtrip.cpp`.  Auto-save on halt and
every 10M cycles; auto-load newest `*.axpsnap` at startup.

**Status (Level 2 -- cycle-accurate):** Still deferred.  Level 2 would
add pipeline microarchitectural state (in-flight slots, store queues,
forwarding lookup, branch predictor history).  V4's pipeline drains
fully between `step()` calls so Level 1's "next fetch boundary"
semantics cover everything we have observed; revisit only when a
debugging task actually requires sub-cycle replay fidelity.

**Original captured:** 2026-05-09, during PA->Tsunami bring-up session.
PALcode init completed (cycle ~4.19M, transition to `pal=0` at PC
`0x600810`); SRM Console now executing kernel-mode init in the
byte-copy loop at `0x600920..0x600948`.  Original deferral rationale:
snapshots only pay back per restore once we know what state to
capture.

**Why Level 1 landed early (2026-05-11):** the chase from yesterday's
MTPR fix through today's HW_CC two-counter split to the UNALIGN
R20-zero pattern at PC `0xdbcc` took ~5 hours of wall clock to reach a
breakpoint at the 178M-cycle mark of a single run.  The remaining
work to `>>>` is estimated at many weeks of similar bug-by-bug
forward progress; per-iteration restore time is the bottleneck.
Level 1 turns each iteration from minutes-of-boot into milliseconds.
The original design memo's "wait until `>>>`" rationale was correct
for the case where post-`>>>` is the only interesting state to
capture; once iteration speed on pre-`>>>` debug became the dominant
cost, the calculus inverted.

**Why snapshots matter:** at observed ~20K cps and SRM's many byte-copy
loops, getting from cold-boot to `>>>` takes minutes per run.  Snapshot
restore drops that to milliseconds.  Every iteration on a downstream
bug (OS boot, device driver init, etc.) compounds -- a 30-minute boot
time makes debugging impractical.

---

## What to snapshot

### Per-CPU architectural state

`coreLib::CpuState` — full struct, including:
- `intReg[32]`, `fpReg[32]`, `pc`, `palMode`, `mode`
- IPRs: `ptbr`, `asn`, `vptb`, `palBase`, `scbb`, `pcbb`, `cycleCount`,
  `intrFlag`, `mm_stat`, `excAddr`, `i_ctl`, `m_ctl`, `mm_stat`,
  `va_ctl`, `i_spe`, `m_spe`
- HWPCB shadows: `ksp`, `esp`, `ssp`, `usp`, `fen`, `asten_sr`
- PAL temps: `palTemp[24]`
- LDx_L/STx_C reservation: `reservedCacheLine`, `hasReservation`
- Lifecycle: `halted`, `lastFaultCode`

POD struct → trivially serializable. Single `memcpy` to a versioned
header + the struct bytes.

### Guest physical memory

`memoryLib::GuestMemory::m_storage` (bytes) + `m_size`. This is the
biggest single piece (V4 default 64 MiB; ES45 with full RAM up to 32 GB).

Possible compression strategies:
- **None** — fastest restore, largest file.
- **Page-granular sparse** — track which pages have been touched
  (high bit of a per-page byte? a side bitmap?), skip zero pages.
  Saves substantial space for a fresh-from-init image.
- **zstd / lz4** — generic compression, smaller file at cost of
  restore time. Best if snapshots are persisted to disk and shared.

Recommend: page-granular sparse + optional zstd wrapper. Sparse alone
should drop a fresh-init 32 GB image to ~50 MB.

### Chipset state

`TsunamiChipset` instance:
- `m_variant`, `m_model`, `m_cpuCount` (immutable; from config)
- `m_cchip` — Cchip register file: CSC, MTR, MISC, MPD, AAR0..3,
  Cchip Monitor Counters, etc.
- `m_dchip` — Dchip register file: DSC, DSC2, DREV, etc.
- `m_pchip0`, `m_pchip1` — Pchip register files: PCTL, all PCI
  config space writes, BAR assignments, sparse-mem/IO state.

Each sub-component should expose `serialize(buffer)` / `deserialize(buffer)`
methods. For v1 of snapshot support, capturing the raw POD register
arrays is sufficient — no PCI device state to worry about until devices
get implemented.

### MmioRegistry state

`pipelineLib::MmioRegistry::m_ranges` — vector of `(startPa, endPa,
ctx, readFn, writeFn, name)` tuples.

**Cannot serialize directly.** Function pointers are process-bound. On
restore, the registry must be **rebuilt from scratch** by re-running the
registration code (which the rebuilt `Machine::Machine` already does).
Snapshot needs to capture *what* was registered, not the function
pointer addresses. Range list (start/end/name) is informational only;
the registration is rebuilt.

### Firmware-staging state

- `Machine::m_srmDescriptor` (palBase, finalPC, sigOffset, payloadSize)
- `Machine::m_srmPayload` (the immutable byte vector)
- `Machine::m_palImageRelocated` (one-shot flag)
- `Machine::m_loadedStartPc`, `m_loadedPalMode`

Most of this is recoverable by re-running `loadSrmFirmware` with the
same .exe path. Snapshots could either re-load or capture the payload
inline. Capturing inline is faster restore but inflates snapshot size
by the .exe size (~1 MB).

### What NOT to snapshot

- **Trace ring** (DecListingSink::m_lookback): instrumentation, not
  state. Restore should start with empty ring.
- **Wall-clock anchor** (m_startTime): start fresh on restore so cps
  computation is meaningful from the restore point forward.
- **Configuration** (EmulatorSettings, GrainMaster.tsv-derived dispatch
  tables, SemanticFlagsEnum): immutable; never changes; not part of
  per-run state.
- **Function pointers anywhere**: never. Process-bound, must be
  reconstructed from the same source code on restore.

---

## Architectural constraints

### Self-referential pointers

`Machine` is **non-movable** because `m_memory.attachMmioHooks(&m_mmio,
...)` stores a self-referential pointer. The same constraint applies
to snapshot restore: the snapshot loader **cannot** deserialize-into-
place the bytes of a `Machine`. Instead:

1. Construct a fresh `Machine` (which auto-wires the hooks).
2. Overwrite `m_cpu` from the snapshot blob.
3. Overwrite `m_memory.m_storage` from the snapshot blob.
4. Restore chipset register state via `chipset.deserialize(...)`.
5. Re-run `loadSrmFirmware` (or restore `m_srmPayload` directly).
6. Set `m_palImageRelocated`, `m_loadedStartPc`, `m_loadedPalMode`.

The MmioRegistry and the GuestMemory hooks **are reconstructed by
`Machine::Machine`** in step 1, pointing at the new `Machine`'s
in-place members. No re-attachment needed; just don't move pointers
across.

### Versioning

The snapshot format must include a version field. CpuState shape will
evolve (we already added 7 fields in Step 4 today). Field-by-field
extensibility (named-field serialization) is more robust than blob
versioning, but more verbose. Pick one early and stick with it.

Suggested format header:

```
struct SnapshotHeader {
    char     magic[8];            // "EMULATR1"
    uint32_t format_version;      // bump on layout change
    uint32_t cpu_state_version;   // bump when CpuState gains fields
    uint32_t chipset_version;     // bump when TsunamiChipset gains fields
    uint64_t timestamp_unix;      // when capture was taken
    uint64_t cycle_at_capture;    // emulated cycle count at capture
    uint64_t guest_mem_size;
    uint64_t guest_mem_offset;    // offset into file
    uint64_t guest_mem_compressed_size;
    char     comment[256];        // human-readable label
    // Followed by: cpu_state, chipset_state, guest_memory blobs.
};
```

---

## Trigger points

When does a snapshot get captured?

**Manual** (recommended for v1): A console command (`save snap.bin`) or
a CLI flag (`--snapshot-on-halt=path` / `--snapshot-at-cycle=N path`).
Predictable, debuggable, no surprises.

**On reaching `>>>`** (the canonical "good state" capture): A
PC-watchpoint fires when the CPU first executes the SRM banner or the
prompt-emit code. Snapshot is auto-saved to a known-good file. Useful
because every run goes through the same boot sequence; capturing once
saves all future restore-times.

**On HALT** (post-mortem): If the CPU halts due to a fault, dump a
snapshot for offline analysis. Combined with the trace ring (which is
NOT snapshotted), this gives full pre-halt context.

**Periodic** (insurance): Every N cycles, save a rolling snapshot.
Useful for very-long-running workloads where a crash N cycles in
shouldn't lose all progress.

Recommend: implement manual first; add `>>>` auto-capture when the
banner-detection PC is known.

---

## Restoration semantics

A `Machine::loadSnapshot(path)` call should:

1. Validate the header (magic, versions match expected ranges).
2. Construct fresh internal state (CpuState defaulted, GuestMemory
   sized from header, chipset constructed with right variant, hooks
   attached).
3. Deserialize CpuState (full struct overwrite).
4. Deserialize GuestMemory bytes (decompress + write into m_storage).
5. Deserialize chipset registers (per-component).
6. Restore SrmDescriptor + SrmPayload (or re-load .exe).
7. Set the post-restore PC (already in CpuState.pc); ready to run().

Restored state should be **bit-identical** to the live state at capture
time, modulo the explicitly-not-snapshotted items (trace ring,
wall-clock anchor).

A roundtrip test: capture snapshot, restore into fresh `Machine`,
run for N cycles, compare CpuState + memory hash to a control run from
the same starting point. Should be byte-identical. If not, an
unsnapshotted state field exists and is leaking determinism.

---

## Implementation order (when work begins)

1. **CpuState serialization** -- small, testable, foundational.
   `DONE 2026-05-11`.  Serialized as raw POD bytes (CpuState is
   uint64_t / uint8_t / bool throughout; no padding holes observed
   on MSVC x64).  `format_version + cpu_state_version` in the header
   guards against future shape drift.
2. **GuestMemory serialization** -- compression strategy decision
   happens here; affects file size and restore speed.  `DONE 2026-05-11`.
   Implemented uncompressed in v1 (fastest restore, largest file).
   `data()` / `data() const` accessors added to `GuestMemory.h`
   bypass MMIO routing for snapshot capture and restore.  Sparse and
   zstd compression deferred to v2 if file sizes become painful.
3. **Chipset serialization** -- per-component, additive.
   `DONE 2026-05-11`.  Inline `serialize(QDataStream&) const` and
   `deserialize(QDataStream&)` on Cchip, Dchip, Pchip.  Cchip handles
   atomic DIM/IIC/DRIR via relaxed load/store on the snapshot path.
   Pchip explicitly skips `m_pciDevices` and `m_ioPorts` (function
   pointers reconstructed by Machine construction).
4. **`Machine::saveSnapshot` / `loadSnapshot`** -- orchestration; uses
   the above three.  `DONE 2026-05-11` as free functions
   `systemLib::save` / `systemLib::load` rather than members, to keep
   Machine's API surface narrow; Machine exposes `restoreSrmStaging`
   as the matching push-back hook on the load path.
5. **Roundtrip test** -- capture + restore + run-and-compare; catches
   any unsnapshotted state.  `DONE 2026-05-11`.  Hive at
   `tests/systemLib/test_snapshot_roundtrip.cpp`: 8 TEST_CASEs
   covering CpuState, GuestMemory (1 MiB PRNG fill), chipset CSRs,
   SRM staging, bad-magic rejection, memory-size mismatch rejection,
   autoload-newest selection, and prune-keeps-N-newest.  Doctest
   `CHECK` only per project convention.
6. **Trigger plumbing** -- console command, CLI flag, auto-on-`>>>`.
   `DONE 2026-05-11` for auto triggers (save-on-halt always, periodic
   every 10M cycles configurable via `kAutoSavePeriodCycles`,
   autoload-newest at startup via `main.cpp`).  Console command and
   PC-watchpoint-on-`>>>` still pending; the auto path is sufficient
   for the current debug workflow.
7. **Documentation** -- README section + sample workflow.  This
   journal entry plus the `Memory.MD` headline serves as the v1
   docs; a `systemLib/README.md` block can be added once the
   workflow has settled.

`Estimated total at design time: 2-3 focused sessions.  Actual: one
session on 2026-05-11 (Level 1 boot-safe scope only; no compression,
no console-command trigger, no PC-watchpoint capture).`

---

## Level 1 implementation summary (2026-05-11)

What landed in code:

| Layer | Files | Notes |
| ----- | ----- | ----- |
| Public API | `systemLib/Snapshot.h` | `save`, `load`, `autoloadLatest`, `pruneOldSnapshots`; format constants; `SnapshotResult` |
| Implementation | `systemLib/Snapshot.cpp` | QDataStream-based; FNV-1a 64-bit footer checksum |
| CpuState | (no change) | Serialized as raw POD bytes; CpuState already had `cycleCount` + `ccOffset` split from today's HW_CC fix |
| GuestMemory | `memoryLib/GuestMemory.h` | Added `data()` / `data() const` for snapshot bypass of MMIO routing |
| Cchip | `chipsetLib/TsunamiCchip.h` | `serialize` + `deserialize`; relaxed atomic load/store on DIM/IIC/DRIR; QDataStream include |
| Dchip | `chipsetLib/TsunamiDchip.h` | `serialize` + `deserialize`; QDataStream include |
| Pchip | `chipsetLib/TsunamiPchip.h` | `serialize` + `deserialize` over CSR storage; skips function-pointer registries; QDataStream include |
| Machine | `systemLib/Machine.h`, `Machine.cpp` | `restoreSrmStaging` hook; `srmLoadPa() / palImageRelocated() / loadedStartPc() / loadedPalMode()` accessors; `setSnapshotDir`, `setAutoSnapshotEnabled`; `run()` rewritten to step-loop with periodic-save and save-on-halt hooks |
| Startup | `main.cpp` | `autoloadLatest` call after `resetToLoadedEntry`, before `run()` |
| Tests | `tests/systemLib/test_snapshot_roundtrip.cpp` | 8 TEST_CASEs; mandatory per the V1-had-no-tests gap |
| Build | `CMakeLists.txt` | New sources added to both Emulatr and Emulatr_tests target source lists |

What is NOT yet captured (intentional deferrals):

- **Pipeline microarchitectural state** (Level 2).  V4's pipeline
  fully drains between `step()` calls, so Level 1 restart is at the
  next fetch boundary -- correct for all current debugging needs.
- **MmioRegistry / GuestMemory MMIO hook function pointers.**
  Process-bound; reconstructed by Machine construction.
- **TsunamiPchip::m_pciDevices / m_ioPorts.**  Function-pointer
  registries; same reasoning.
- **TLB / page-walker caches.**  Do not exist in V4 v1.
- **Trace ring / wall-clock anchor.**  Instrumentation, not state.
- **Compression** (zstd / sparse).  Uncompressed for v1; revisit if
  on-disk size becomes a problem.
- **Console command** (`save snap.bin` at `>>>` prompt).  Pending
  the console infrastructure itself.
- **PC-watchpoint capture at `>>>` banner.**  Pending knowledge of
  the banner PC; will be added once SRM gets that far.
- **Firmware hash in header for cross-build compatibility check.**
  V1 had this commented out; deferred until snapshot files start
  travelling between builds.

Auto-save policy:

- Save on halt: always (any `kFault*` including `kFaultHalt`).
  Filename `auto_halt_<unix_ts>_<cycle>.axpsnap`; not pruned (halt
  captures are diagnostically valuable; user removes manually).
- Periodic save: every `kAutoSavePeriodCycles` (default 10M).
  Filename `auto_<unix_ts>_<cycle>.axpsnap`; pruned to newest
  `kAutoSaveKeepCount` (default 5) by `pruneOldSnapshots`.
- Manual / named captures (any filename without the `auto_` prefix)
  are NEVER pruned.
- Disable both via `Machine::setAutoSnapshotEnabled(false)` -- used
  by the roundtrip test hive to avoid disk traffic during unit tests.

Autoload policy:

- At startup, `main.cpp` calls `autoloadLatest(machine, "snapshots")`
  immediately after `resetToLoadedEntry()`.  The directory scan
  picks the newest `*.axpsnap` by mtime.  On hit, the snapshot
  state fully replaces the freshly-loaded firmware state.  On miss,
  the cold-boot path proceeds unchanged.
