# Spec — tools/mkdisk.py — raw disk-image generator (DRAFT for approval)

Status: PROPOSED (discuss-before-code).  2026-06-12.

## 1. Purpose

Generate a **blank, writable, raw flat disk image** for the emulated ATA
fixed disk (dqa0).  Two roles, one artifact:

- **Now:** the read-proof target so dqa0 exists and `READ SECTORS` returns
  LBN 0 (DQ boot-integration acceptance #3).
- **Later:** the empty **install target** that a Tru64/VMS installer
  (booted from the dqa1 ISO) writes the OS onto, after which `boot dqa0`
  is bootable.

NON-GOAL: this tool does NOT synthesize a bootable OS or a filesystem.
Bootability is installed FROM the ISO when the CD boot reaches the
installer (a downstream milestone — console currently halts at `>>>`).

## 2. Image format (must match the consumer)

- Raw flat file; **byte offset = LBA * sectorSize**.
- `sectorSize = 512` (ATA fixed disk; matches `FileBlockMedia(...,512,RW)`
  and `Cy82C693Ide::kSectorBytes`).  CD/ISO is 2048 and OUT OF SCOPE here.
- Read-WRITE: the OS installer / `FileBlockMedia(512, readOnly=false)`
  writes to it and the writes persist.
- Size sets capacity = size/512 sectors, which drives the ATA IDENTIFY
  geometry (V4 derives 16 heads x 63 sectors/track from blockCount), so
  pick a realistic size (>= a few MB; an OS install target wants GBs).

## 3. CLI

```
mkdisk.py OUTPUT --size SIZE [--sector 512]
                 [--blank | --stamp]      (default: --stamp)
                 [--label NAME]
                 [--sparse | --full]      (default: --sparse)
                 [--verify]               (re-read + check stamps, exit 0/1)
```
- `--size` accepts `K`/`M`/`G` suffixes (e.g. `4G`, `64M`); must be a
  whole multiple of `--sector`.
- `--sparse` (default): create via seek+truncate so a multi-GB image
  costs only the stamped bytes on disk; `--full` forces full allocation.
- Deterministic: same args -> byte-identical file (no timestamps/random),
  so traces/replays stay stable (project no-wall-clock rule).

## 4. Sector stamping (the verifiable part)

`--stamp` (default) writes a 32-byte header at the front of EACH sector so
a read can be proven to be the CORRECT LBN (not a stale buffer or wrong
sector); the rest of the sector is zero.

| Offset | Bytes | Field |
|--------|-------|-------|
| 0      | 12    | magic ASCII `"EMULATR-DSK"` + NUL |
| 12     | 4     | sectorSize (LE u32) |
| 16     | 8     | LBN (LE u64) |
| 24     | 8     | stamp checksum = FNV-1a of bytes 0..23 (LE u64) |
| 32     | rest  | 0x00 |

`--blank` instead writes all-zero sectors (proves a read happened, not
that the right LBN came back) -- only if a pristine zeroed target is
wanted for an installer that dislikes non-zero blocks.

With `--sparse --stamp`, only LBN 0..N stamped headers are written; the
rest of each sector is a hole (reads back zero), so the file stays small.

## 5. Self-test

`--verify OUTPUT` re-opens the image, reads a sample of LBNs (0, 1, last,
+ a few random), and checks each stamp's magic/LBN/checksum.  Exit 0 on
match, 1 on mismatch.  (A C++ round-trip doctest that reads the generated
image back through `FileBlockMedia` and asserts the LBN-0 stamp is an
OPTIONAL follow-up that also exercises the RW seam.)

## 6. Wiring into the emulator

1. Generate, e.g. `python tools/mkdisk.py dqa0.img --size 4G`.
2. Drop it in `[Storage] diskDir` (EmulatrV4.ini) or use an absolute path.
3. Set the dqa0 (ch0/u0 ata_disk) `media` in `ds10_platform.win`
   (currently `""`) to the filename; `media_kind` absent/`image` ->
   FileBlockMedia 512 RW.  On boot: `Storage: attached ATA disk '...'
   to IDE ch0 unit0`.
4. Acceptance #3: at `>>>`, `boot dqa0` (or a read) drives dq_driver
   READ SECTORS -> Cy82C693Ide -> FileBlockMedia; the N5 IDE trace shows
   LBN 0 returning its stamp.

## 7. Out of scope (this tool / this step)

- Synthesizing a bootable OS or a filesystem (comes from the ISO install).
- A valid Alpha boot block at LBN 0 (a documented checksum structure;
  add later as `--bootblock alpha` ONLY if we want SRM `boot dqa0` to pass
  the boot-block checksum before an OS exists -- not needed for #3).
- 2048-byte ISO authoring; HostOpticalMedia; any non-DS10 geometry.

## 8. Open question

Default image size for an OS install target (Tru64 UNIX V5 wants a few
GB; OpenVMS less).  Proposed default if `--size` omitted: error out (force
an explicit size) rather than guess.  CONFIRM the target OS so the doc can
suggest a sane default.
