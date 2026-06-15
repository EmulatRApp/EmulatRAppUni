# EOD Handoff — 2026-06-12 (pm) — IBlockMedia seam, storage media, enabled flag

Second handoff of the day (the first was the 64→1024 memory-size fix:
`20260612_EOD_handoff_memory_size_64_to_1024.md`).  This covers the
storage-media arc: dq media-backed reads (#31), the IBlockMedia seam
refactor (Phase A, #33), the OS-suffixed manifest (#35), and the
per-target `enabled` flag.  Companion: `20260612_dq_ew_driver_requirements_review.md`.

All `Emulatr_tests` runs this session: **449 cases / 5966 assertions /
0 failed** (RelWithDebInfo + Release), snapshot suite intact.

---

## 1. dq media-backed read path (#31) — found mostly built

The ATA fixed-disk read engine was already implemented (Cy82C693Ide:
attachDisk, ATA IDENTIFY 0xEC, READ SECTORS 0x20/0x21, multi-sector PIO
streaming) AND wired (Machine resolves `storage[].media` vs `[Storage]
diskDir`).  The gap was config: media fields were empty.  The genuinely
missing code was the **ATAPI READ path on the CD** — implemented in
VirtualIsoDevice: READ CAPACITY(10), READ(6/10/12), READ TOC (single
data track), TEST UNIT READY, with MediaStatus→SCSI sense mapping.
Single 2048-byte burst per command (multi-block streaming = trace-gated
#32; only add if a real boot issues >1-block ATAPI reads).

## 2. IBlockMedia seam (Phase A, #33) — APPROVED + LANDED

Byte sourcing for both the ATA disk and the ATAPI CD now routes through
an `IBlockMedia` (the drives no longer open files).  Files:

- **`deviceLib/scsi/IBlockMedia.h`** — `enum class MediaStatus { Ok,
  NoMedia, OutOfRange, IoError, ReadOnly, NotOpen }` + interface
  (`open/close/isOpen/isPresent/isReadOnly/blockSize/blockCount/read/
  write`).  Qt-free.  blockSize = device logical sector (512 disk /
  2048 CD) — must match the drive type (C2 decode value).
- **`deviceLib/scsi/FileBlockMedia.h`** — flat-image backing; serves
  BOTH 512 RW (ATA) and 2048 RO (ISO) via std::fstream/std::filesystem,
  offset = lba*blockSize.  A flat image is "present" once open.
- **`deviceLib/scsi/BlockMediaFactory.h`** — `makeBlockMedia(kind, path,
  blockSize, readOnly, err)`: `image|iso|absent → FileBlockMedia`;
  `host → nullptr + "Phase B" diag`; **unknown → FAIL CLOSED** (no
  silent fallback).  Returns an already-open medium.
- **`tests/deviceLib/MockBlockMedia.h`** — in-memory, deterministic
  fill, fault injection (NoMedia/IoError at a chosen LBA).

Drive refactor (seam edits, approved before writing):
- **VirtualIsoDevice** holds `unique_ptr<IBlockMedia>`; `setMedia()`
  injects; `loadMedia(path)` is a FileBlockMedia convenience.  doRead/
  doReadCapacity/doReadToc go through the medium; MediaStatus→sense:
  Ok→good, NoMedia/NotOpen→02/3A/00, OutOfRange→05/21/00, IoError→03/11/00.
- **Cy82C693Ide** `Disk` struct backs onto `unique_ptr<IBlockMedia>`;
  `attachMedia(ch,unit,uptr)` is the seam, `attachDisk(path)` a
  FileBlockMedia convenience; loadDiskSector reads via the medium.
- **TsunamiChipset** `setIdeDiskImage/setIdeCdImage` → `setDiskMedia/
  setCdMedia` (inject unique_ptr).
- **Machine** storage loop uses the factory (media_kind), blockSize/
  readOnly from StorageType (AtaDisk 512 RW, AtapiCdrom 2048 RO),
  fail-closed on unknown kind.
- **ScsiSenseData** gained `senseLbaOutOfRange` (05/21/00) +
  `senseUnrecoveredReadError` (03/11/00).

CAUTION (build): the unique_ptr members make Cy82C693Ide /
VirtualIsoDevice / TsunamiChipset **move-only**.  No copies existed
(verified: 449 green incl. snapshots) but watch for any future copy.

Phase B = **#34 HostOpticalMedia** (Win `CreateFileW \\.\X:` / Linux
`/dev/sr0` pread; `host:N` logical resolver).  The factory's `host`
branch is the drop-in.

## 3. OS-suffixed manifest (#35) — `.win` / `.linux`

User renamed `ds10_platform.json` → `ds10_platform.win` (Linux host =
`.linux`).  `Machine.cpp` default is now `#ifdef _WIN32` → `.win` else
`.linux` (`EMULATR_PLATFORM_CONFIG` still overrides).  `PlatformConfig::
load` parses by content, extension-agnostic.  **CMake POST_BUILD copy
fixed** to deploy the OS-suffixed file (was still copying the old
`.json` name → runtime fell back to the built-in default; first run
after the rename showed `cannot open ds10_platform.win`, second showed a
JSON parse error from a hand-edit — both now resolved).

## 4. Per-target `enabled` flag (this session)

- `StorageTarget.enabled = true` — **absent = enabled (implicit)**;
  parser `toBool(true)`.
- **validate()** skips disabled targets in the duplicate channel/unit
  check → a disabled ALTERNATE may share a slot; two *enabled* on the
  same slot is still a hard error.
- **Machine** skips `!enabled` targets (parsed, not attached).
- Doctest in `test_platform_config.cpp`: implicit-true, explicit-false,
  disabled-alt-shares-(0,1) validates, positive control (enable →
  duplicate → hard error).
- `ds10_platform.win`: `dqa1` = Tru64 ISO (enabled) + a **disabled
  host-passthrough alternate on the same (0,1) slot**.  To switch dqa1
  to the physical drive: flip ISO `enabled:false`, host `enabled:true`.
- Follow-up (optional): extend `enabled` to top-level PCI + IIC devices
  (same one-line-per-layer addition).

---

## 5. Next session

1. Rebuild + cold boot; confirm the manifest loads (no fallback warning)
   and `Storage: attached ATAPI CD 'D:\isos\tru64v5.iso' to IDE ch0
   unit1` (if the ISO exists), then `boot dqa1`.  Capture
   `EMULATR_IDE_TRACE=1` if it stalls — that decides #32 (multi-block).
2. **#34 HostOpticalMedia** (Win+Linux) when host passthrough is wanted.
3. Still-open boot blockers (unchanged, pre-existing): #26 64→1024 live
   confirm (cold boot, no stale snapshot), the 0x7bef0 software-tick
   loop (#21), dva0 scan (#17), halt-button neutralization (#27).

## 6. Process note

The D: bash mount LAGS host writes — `python`/`g++`/`cp` reading the
mount repeatedly saw truncated copies of files the Read tool showed
complete (Memory.MD, VirtualIsoDevice.h, ds10_platform.win).  Verify D:
edits with the Read tool (host view), NOT bash byte counts; for local
compile-checks, stage from known content into /tmp rather than `cp` from
the mount.
