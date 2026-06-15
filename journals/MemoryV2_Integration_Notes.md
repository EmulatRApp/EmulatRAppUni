<!--
============================================================================
MemoryV2_Integration_Notes.md -- sparse paged GuestMemory integration
============================================================================
Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1

Project Architect: Timothy Peer
AI Collaboration:  Claude (Anthropic)

Commercial use prohibited without separate license.
Contact:        peert@envysys.com  |  https://envysys.com
Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
============================================================================
-->

# GuestMemory V2 -- Sparse Paged Backing Integration Notes

**Status:** LANDED 2026-05-14.  Companion to the V2 POC GuestMemory
(`uploads/guest_memory_v2.h`) and the standalone SparseMemoryBacking
preview.  Tim's framing this session: "we have created a hair-ball
already... adapt to the best memory model available to us now."  The
five integration decisions reached this session land per the
Resolutions block below.

**Predecessor context:** the Cchip Phase A work (`CchipPhaseA_Design_Notes.md`)
established the diagnostic-rich house style and the unwired-TODO
discipline.  This memory rewrite reuses both -- per-CSR-access diag
on the chipset, TODO markers on deprecated CpuState reservation
fields and the MmioRegistry class.

## What landed

The V4 `memoryLib::GuestMemory` flat backing
(`std::unique_ptr<uint8_t[]>`) has been replaced with a sparse paged
design that brings four capabilities the flat backing lacked:

- **Sparse 64 KiB pages.**  RAM pages are allocated lazily on first
  touch via `ensurePage()` with a CAS install.  The page table is a
  calloc'd array of `uint8_t*` slots, one per 64 KiB page of declared
  capacity.  Untouched slots stay nullptr; reads return zero via a
  pre-allocated zero sentinel page (branch-free hot path).  Resident
  set tracks what the guest actually wrote, not what was declared.

- **Platform-selected page allocator.**  CMake option
  `EMULATR_USE_OS_PAGES` (default ON) selects between `VirtualAlloc`
  on Win32, `mmap MAP_ANONYMOUS` on Posix, and a portable `calloc`
  fallback.  All three guarantee zero-filled pages; the access path
  is identical regardless of which is in use.  Switching is one
  CMake flag and a clean rebuild.

- **Built-in LockMonitor.**  Per-CPU cache-line reservation tracking
  for LDQ_L / STQ_C lives inside `GuestMemory::lockMonitor()` -- a
  `memoryLib::LockMonitor` instance.  Every store calls
  `m_locks.clearLine(pa)` so cross-CPU reservation invalidation is
  wired for the SMP-eventual case.

- **PAL scratch region.**  Flat 1 MiB calloc-allocated region at the
  top of 44-bit PA space (`0xFFFFFF00000` to `0xFFFFFFFFFFF`),
  routed inside read*/write* before the MMIO hook.  Reserved for
  PALcode temporary use; allocated eagerly to avoid first-touch
  cost in the PALcode hot path.

The public API is preserved verbatim: `read1/2/4/8(pa, out&) ->
MemStatus`, `write1/2/4/8(pa, value) -> MemStatus`, `sizeBytes()`,
`attachMmioHooks(ctx, readFn, writeFn)`.  Existing pipelineLib,
mmuLib, deviceLib, traceLib callers compile unchanged.

What changed at the API surface:

- `data()` -- **REMOVED.**  There is no contiguous backing buffer to
  point at.  Snapshot.cpp migrated to `forEachPage(callback)` for
  save (which preserves the wholesale-bytes file format) and
  `ensurePage(pidx)` for load.  Test_snapshot_roundtrip migrated to
  `writeBlock`/`readBlock` for bulk byte fill.

- `attachMmioHooks` signature -- **changed.**  Old hook returned
  `bool` with an out-parameter for the read value; new hook returns
  `uint64_t` directly for read and `void` for write.  Matches the
  TsunamiChipset static handler shape exactly so Machine attaches
  the chipset without an indirection layer.

- New: `forEachPage(callback)`, `pageCount()`, `allocatedPages()`,
  `residentBytes()`, `isDirty(pidx)`, `clearDirty()`,
  `writeBlock(pa, src, len)`, `readBlock(pa, dst, len)`,
  `lockMonitor()`.

## Resolutions captured this session

These are the answers to the five questions raised in the
"POC v2 vs SparseMemoryBacking vs V4 current" review:

**(1) Keep V4's MemStatus API at the public layer.**  Public methods
on GuestMemory keep the `MemStatus` contract.  POC v2's value-return
core lives privately in `read{8,16,32,64}raw` / `write{8,16,32,64}raw`;
the public API wraps them and decides MemStatus based on which
region the PA landed in.

**(2) Retire MmioRegistry.**  No longer wired into the production
path.  Machine attaches `TsunamiChipset::mmioRead/Write` directly to
GuestMemory's hooks via a thin offset-translator adapter
(`machineMmioRead/Write` in Machine.cpp).  MmioRegistry class is
marked `TODO(deprecated)` in its header.  test_mmio_csc_roundtrip
continues to exercise the registry in isolation pending migration to
a direct-attach test pattern.

**(3) Isolated PAL scratch.**  Separate flat 1 MiB calloc region at
top of 44-bit PA space; routed inside read*/write* before MMIO
fallback.  First-touch cost (1 MiB calloc at construction) accepted
to avoid latency in PAL hot paths.  TODO(unwired): wire PAL temp
accessors when a PALcode path actually targets this region; for now
the region is allocated but otherwise unused.

**(4) LockMonitor in GuestMemory.**  Member `m_locks` of type
`memoryLib::LockMonitor` carries per-CPU reservation state.
CpuState::reservedCacheLine and CpuState::hasReservation marked
`TODO(deprecated)`; migration of MemDrainer / BreakpointSink /
CpuStateDump call sites is a follow-on edit (mechanical -- route
the read/write through GuestMemory::lockMonitor()).  Snapshot path
still serialises CpuState as POD bytes including those fields, so
old snapshots remain loadable; sparse-format upgrade will drop them.

**(5) Backward-compatible interface, future-flexible underneath.**
Default capacity stays 64 MiB pending sparse-format snapshot.
Sparse allocation removes the construction cost so bumping later
(256 MiB, 1 GiB, variant max 4 GiB Tsunami / 32 GiB Typhoon) is
trivial -- one constant change.  Per Tim's framing, the design
posture supports the eventual 32 GiB Typhoon configuration without
re-architecting the memory subsystem.

## Files touched

**New:**

- `memoryLib/GuestMemory.h` -- full rewrite of the backing class.
  Replaces the previous flat unique_ptr design; preserves the
  public API surface.
- `memoryLib/GuestMemory.cpp` -- new TU.  Platform page allocator
  (mmap/VirtualAlloc/calloc), constructor/destructor lifecycle,
  raw value-return accessors, ensurePage CAS, bulk block helpers.

**Modified:**

- `CMakeLists.txt` -- added `memoryLib/GuestMemory.cpp` to
  `EMULATR_SOURCES` and `EMULATR_TEST_SOURCES`.  Added
  `option(EMULATR_USE_OS_PAGES "..." ON)` + per-target
  `target_compile_definitions` mirroring the EMULATR_CHIPSET_DIAG
  pattern.
- `systemLib/Machine.cpp` -- replaced MmioRegistry-bridging adapters
  with direct chipset-attach adapters.  Removed the registry's
  `registerRange` call from the constructor.  `m_mmio` member
  retained with a `(void) m_mmio` placeholder and a `TODO(deprecated)`
  comment.
- `systemLib/Snapshot.cpp` -- replaced `mem.data()` wholesale
  read/write with `forEachPage` save and `ensurePage` load.
  Snapshot file format unchanged; existing predig_*.axpsnap files
  remain loadable.
- `pipelineLib/MmioRegistry.h` -- header-block `TODO(deprecated)`
  marker, recording the retirement and pointing at the replacement.
- `coreLib/CpuState.h` -- `reservedCacheLine` / `hasReservation`
  fields marked `TODO(deprecated)` with migration target
  (`GuestMemory::lockMonitor()`).
- `tests/chipsetLib/test_mmio_csc_roundtrip.cpp` -- adapter
  signatures updated to match new MmioReadHook/MmioWriteHook.
- `tests/systemLib/test_snapshot_roundtrip.cpp` -- byte-fill and
  byte-compare paths migrated from direct `data()` access to
  `writeBlock`/`readBlock`.

## Sparse-format snapshot -- the follow-on

The current Snapshot.cpp preserves the wholesale-bytes file format
(write `memSize` bytes total via per-page iteration).  This keeps
existing predig_*.axpsnap files loadable, but does not yet reduce
snapshot file size when only a small fraction of pages have been
touched.

The follow-on upgrade (separate session):

1. Bump snapshot file version (`kSnapshotVersion` constant).
2. New format: page-count header, page-presence bitmap, then a run
   of `[pageIdx (uint32), 64KiB-bytes]` records for each touched
   page.
3. Save path: iterate dirty/allocated pages, write only those.
4. Load path: read the bitmap, ensurePage for each allocated index,
   read its bytes.
5. Backward-compatibility shim: detect the old version on load and
   route through the old wholesale-bytes path so predig_*.axpsnap
   from before this change still works.

Once sparse-format lands, the default `memSize` can be bumped to
the chipset variant maximum (4 GiB Tsunami, 32 GiB Typhoon) without
snapshot file blow-up.

## Deprecation cleanup tail

Mechanical follow-on edits, can be batched into a single later
session:

- Remove `CpuState::reservedCacheLine` and
  `CpuState::hasReservation`.  Migrate MemDrainer, BreakpointSink,
  CpuStateDump, test_memdrainer to read/write
  `GuestMemory::lockMonitor()` instead.  Bump snapshot file version
  (CpuState size changes) -- combine with sparse-format upgrade.
- Migrate test_mmio_csc_roundtrip from the MmioRegistry registry
  abstraction to a direct chipset-attach test, then delete
  `pipelineLib/MmioRegistry.h` / `.cpp` and remove from CMake.
- Bump default `memSize` after sparse-format snapshot lands.

## Regression chase 2026-05-17 -- MMIO hook bool-return restored

**What broke.**  The initial V2 implementation changed the MmioReadHook
signature from V1's bool-returning `bool(*)(void*, PAType, uint8_t,
uint64_t&)` to a value-returning `uint64_t(*)(void*, uint64_t,
uint8_t)`.  This lost the "did the hook actually service this PA"
signal.  Public read*/write* methods consequently treated any PA
that fell through to the MMIO path as serviced (`Ok`) regardless of
whether the chipset actually claimed it.

**Why it bit us.**  The OSF/1 PAL idle-wait pattern (documented in
`project_idle_wait_interrupt_hypothesis.md`) deliberately writes and
reads from intentionally-bad PAs (e.g., `0x47ff0403e400036d` in kseg
form, or `0x4897c006xxx` after the OS-PAL takeover at cycle ~178M).
V1 returned `OutOfRange` for these; the pipeline produced
`kFaultBusError` -> MCHK trap delivery, and the firmware's MCHK
handler ran the yield-loop body.  Under the buggy V2, the same PAs
returned 0 silently and the MCHK trap never fired -- firmware state
corrupted silently.  The diagnostic signature was a flood of
"Machine: OOR MMIO WRITE pa=0x4897c006xxx" stderr lines during the
2026-05-17 cold-boot verification probe.

**Fix.**  Hook signatures reverted to V1's bool-return shape (read
takes an out-param).  Public API in GuestMemory.h does explicit
three-way dispatch: RAM, PAL scratch, or hook-serviced.  If none
match, `MemStatus::OutOfRange` is returned.  `machineMmioRead/Write`
in Machine.cpp does the Tsunami-window range check and returns false
for outside-window PAs.  test_mmio_csc_roundtrip adapter flipped
back to bool-return.

**Lesson.**  The "did service" signal is load-bearing for any
emulator that models the bus-error trap delivery.  Future hook
contracts must preserve it.  Documented in the MmioReadHook/
MmioWriteHook header comments as the V1 contract.

## References

- POC source: `uploads/guest_memory_v2.h` (this session)
- SparseMemoryBacking preview: `uploads/SparseMemoryBacking.h`
- Phase A scaffolding: `CchipPhaseA_Design_Notes.md`
- HRM PA layout: Tsunami/Typhoon 21272 HRM EC-RE2CA-TE Rev 4.0
- Snapshot format: `Snapshots_Design_Notes.md`
