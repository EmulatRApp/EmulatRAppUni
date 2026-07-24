# Changelog

All notable changes to EmulatR are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions track the
Help &amp; Manual build number (`major.minor.build`, e.g. 1.4.4) rather than
SemVer; the version lives in the tree/branch and file headers, not in filenames.

This is the granular, developer-facing history (with `JRN-*` journal references).
The curated, tester-facing summary lives in the guide's **Release Notes** topic.

## [1.4.4] - 2026-07-23

First public beta. SRM console reaches the `>>>` prompt on DS10, DS20, and ES40
(ISP/default mode). Guest-OS boot is not yet available.

### Added
- **DS20 (Tsunami) brought to SRM `>>>`** with correct *AlphaServer DS20* identity
  and badge. (20260622 announcement/status; 20260623 platform-manifest + identity)
- **ES40 (Clipper) bring-up** to the SRM console, including the COM2 console
  backend. (20260701 init; 20260708 console COM2 gate — landed/verified)
- **Platform manifest system** — runtime-loaded `<firmware>_platform.json` device
  tree (IIC + PCI), the 3-channel platform-identity contract with a boot canary,
  and the ini→JSON SSOT. (20260703 platform_ssot_ini_to_json; 20260623)
- **Platform Editor** web UI (`tools/PlatformEditor/webui`) with live validation
  (checks V-01…V-28) and format-preserving save.
- **Device models** — floppy FDC (82077), keyboard (8042) + VGA console, and
  IDE/ATAPI storage (Cypress func1 on DS10/DS20; ALi M5229 on ES40) over a common
  `IBlockMedia` seam. (20260622 floppy; 20260712 M5229; 20260718 kbd/VGA)
- **Deterministic execution substrate** — `AlphaCpuAgent` + `Dispatcher` with a
  swappable Sequential/Threaded driver; `determinism_equivalence` doctest
  (Sequential == Threaded, bit-identical). (20260619 phase1/phase2)
- **Level-1 snapshots** (CpuState + GuestMemory + chipset CSRs; auto-save /
  autoload-newest) plus an env-gated **entry snapshot** that skips the firmware
  decompressor.
- **macOS/Linux build hardening** and an HWRPB-scan instrument. (20260625)
- **Env-gated diagnostics** (relwithdebinfo/debug only, compiled out of release):
  retire-trace `.trc` firehose, BOOTTRACE marker trace, `EMULATR_DIAG_*` windows,
  `EMULATR_RSCC_DIAG`, and the WARP-ACCOUNTING summary.
- **Distribution tooling** — versioned redistributable packaging
  (`tools/make_redist.sh`), the user guide staged into `documentation/`, uniform
  PC (Git Bash) execution guards + doc headers across the `run_*.sh` scripts, and
  this changelog + the guide's Release Notes track.

### Changed
- **MMU DTB/ITB** now modeled as true 128-entry fully-associative
  (`SPAMShardManager<2,64>`), replacing an incorrect 16-way set-associative model
  that caused conflict-eviction thrash. (JRN-VMB-012)
- **Flash / NVRAM** co-located with the firmware image (`<stem>.rom`); the run/diag
  scripts now write their per-run `<name>_diag_flash.rom` under `firmware/`.
- **CSERVE 0x66** restored to its faithful no-op (a prior `get_time` at 0x66 was a
  regression and was removed). (20260711 CONFIRMED)
- Legacy `Machine::run` loop removed in favor of the agent/dispatcher path.

### Fixed
- **DS20 model badging** — IIC completion-interrupt root cause.
  (20260629, 20260630 rootcause_and_fix)
- **ES40 memtest ACV** — AAR `ASIZ` decode-width mismatch.
  (20260710 RESOLVED_aar_asiz_and_tiling)
- **ES40 post-first-tick halt** — SCB base mismatch. (20260708)
- **DS20 post-banner wall** — PCF8584 IIC base mapping cleared. (20260622)
- **GuestMemory** — page-crossing store overrun in the sparse pager.
- **GuestMemory** — added explicit `<new>` / `<cstdlib>` includes required by the
  Release configuration and the clang (macOS) build. (2026-07-23)
- **DS10** — boot-to-`>>>` regression bisected and confirmed. (20260705)
- `tools/run_srm_trace_full.sh` — repaired a truncated tail (the file was cut
  mid-statement in the working tree and in git HEAD).

### Known issues
See the guide's **Known Gaps** and **Release Notes** topics. In brief: the SRM→OS
hand-off at VA `0x20000000` does not yet fire (working hypothesis: a zero PALtemp
base register `r21` at the PAL restart — JRN-VMB-013/014/016); floating point is
IEEE-T POC only, with VAX G/F absent (gates OS install); PCI enumeration is
partial (~1 on-board device vs ~9); Ethernet (DE500/21143) is designed but not
modeled; SMP runs single-CPU; the Titan (DS25/ES45) chipset is model-only.

[1.4.4]: https://example.invalid/EmulatR/releases/tag/v1.4.4
