// ============================================================================
// systemLib/Snapshot.h -- Level 1 (boot-safe) state snapshot save/restore
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// Level 1 (boot-safe) snapshot of the architecturally + memory-visible
// state of one Machine.  Captures:
//
//   - CpuState (full POD: int regs, fp regs, pc, all IPRs, palTemps,
//     mode shadows, reservation, halt/fault flags, cycle counters).
//   - GuestMemory backing buffer (the full configured byte array;
//     firmware bytes loaded at boot are included automatically).
//   - TsunamiChipset register storage (Cchip + Dchip + Pchip CSR
//     fields; variant / model / cpuCount captured for cross-check).
//   - SRM firmware staging (descriptor, payload bytes, load PA, the
//     relocation one-shot flag, and the captured entry PC / palMode).
//
// Does NOT capture, by design:
//
//   - Pipeline microarchitectural state (in-flight slots, store queues,
//     branch-predictor history).  Level 1 restart semantics are "next
//     fetch boundary," which V4 already provides because PipelineDriver
//     holds no carry-state between step() calls beyond what is in
//     CpuState + GuestMemory.
//   - MmioRegistry function-pointer table.  Reconstructed by Machine
//     construction unconditionally.
//   - TsunamiPchip device registries (PCI devices, I/O port handlers).
//     Same reasoning -- function pointers, rebuilt by device wiring.
//   - TLB / page-walker caches.  Do not exist in V4 v1.
//   - Trace ring / wall-clock anchor.  Instrumentation, not state.
//
// File format (all little-endian; QDataStream Qt 6.0):
//
//   header  : magic[8] = "EMULATR1"
//             format_version  (uint32) bumped on layout change
//             cpu_state_version (uint32) bumped on CpuState shape
//             chipset_version (uint32) bumped on chipset shape
//             timestamp_unix  (uint64) capture wall-clock seconds
//             cycle_at_capture (uint64) emulator cycleCount at save
//             ccOffset_at_capture (uint64) architectural CC at save
//             comment[256]    (zero-padded ASCII tag, e.g. "auto-halt")
//   cpu     : raw CpuState bytes (POD; aligned to original layout)
//   memory  : size (uint64) + raw bytes (size bytes)
//   chipset : variant (uint32) + model (qstring) + cpuCount (int32) +
//             memSize (uint64) + Cchip serialize + Dchip serialize +
//             Pchip serialize
//   srm     : valid (bool) + sigOffset (uint64) + payloadSize (uint64)
//             + palBase (uint64) + finalPC (uint64) + jsrOffset (uint64)
//             + loadPa (uint64) + palImageRelocated (bool)
//             + loadedStartPc (uint64) + loadedPalMode (bool)
//             + payloadByteCount (uint64) + payloadBytes
//   footer  : checksum64 over all preceding bytes (uint64)
//
// Round-trip contract:
//
//   save(machine, path)              -- write Level 1 snapshot.
//   load(machine, path)              -- restore into a fresh Machine.
//   autoloadLatest(machine, dir)     -- find newest *.axpsnap in dir
//                                       and load it.  Returns the path
//                                       loaded, or empty string when
//                                       no file was found (cold boot).
//
// The roundtrip test (tests/systemLib/test_snapshot_roundtrip.cpp)
// captures a Machine, restores it into a fresh Machine, runs both
// for N steps, and compares CpuState + memory hash.  Bit-identical
// is the contract; any drift indicates an unsnapshotted state field.
//
// ============================================================================

#ifndef SYSTEMLIB_SNAPSHOT_H
#define SYSTEMLIB_SNAPSHOT_H

#include <cstdint>
#include <filesystem>
#include <string>

class QString;

namespace systemLib {

class Machine;


// ---------------------------------------------------------------------------
// Format constants.  Bump versions on shape changes; old files become
// invalid (loadSnapshot returns failure with a clear error message).
// ---------------------------------------------------------------------------
inline constexpr char     kSnapshotMagic[8]      = { 'E','M','U','L','A','T','R','1' };
inline constexpr uint32_t kFormatVersion         = 1;
// kCpuStateVersion history:
//   v1 -> v2 (2026-05-11): unalignTrapEnabled bool added.
//   v2 -> v3 (2026-05-12): CBoxState struct added (writeMany +
//       errorReg + dataReg + shftCtrl), modeling the Cbox CSR / IPR
//       shadow per HRM section 5.4.  Replaces the earlier single
//       cBoxCsr uint64 idea before any v3 snapshots were written.
//   v3 -> v4 (2026-05-13): isum uint64 added after intrFlag,
//       backing HW_MFPR(HW_ISUM) for the OSF/1 PAL INTERRUPT vector
//       decode.  CpuState is serialized as a raw POD blob, so the
//       new field shifts every byte after intrFlag -- pre-v4
//       snapshots (including any predig_ autoload captures) are
//       layout-incompatible and rejected at load.
//   v4 -> v5 (2026-05-20): software-managed TLB (C1-C4) added to
//       CpuState -- itbMgr/dtbMgr (SPAMShardManager<16,8>, ~28 KB),
//       the va (HW_VA) field, and the ITB/DTB IPR staging fields
//       (itbTag, dtbTag0/1, dtbAsn0/1, itbPteTemp, dtbPteTemp).  Raw
//       POD blob grew and shifted; all pre-v5 snapshots are
//       layout-incompatible and MUST be rejected (managers are
//       trivially copyable post-atomics-strip, so the memcpy is
//       valid -- only the layout changed).
//   v5 -> v6 (2026-05-21): deprecated the standalone `bool palMode`;
//       PALmode is now carried in PC<0> (single source of truth).
//       Removing the bool field shrinks/realigns the raw POD blob, so
//       all pre-v6 snapshots are layout-incompatible and rejected.
// Snapshots captured before this point cannot be loaded; cold-boot
// to re-establish a halt snapshot post-change.
// Chipset stream versions:
//   v1 -> v2 (2026-06-05): interrupt-chain device state appended after
//       the Pchip block -- COM1/COM2 Uart16550 (registers, THRE latch,
//       RX FIFO), Pic8259Pair (both units incl. ICW init FSM, ELCR,
//       edge-detector cache), TsunamiChipset::m_lastPicLevel, and the
//       Cchip TTR/TDR pair deferred at Phase B.  Pre-v2 snapshots
//       restored these chips at reset defaults, leaving the interrupt
//       chain dead (deaf-console, 2026-06-05); they are rejected.
//   v2 -> v3 (2026-06-06): FlashRom 2 MB image appended after
//       m_lastPicLevel in serializeDevices.  SRM environment variables
//       live in the TIG flash, so a snapshot that omitted it restored
//       stale/empty env.  Length-prefixed raw bytes; restore mirrors
//       loadRaw (read-array mode, dirty=false).  Pre-v3 snapshots are
//       rejected (the appended bytes shift the SRM-staging block that
//       follows, so the stream is not backward-readable).
//   v3 -> v4 (2026-06-06): IicPcf8584 FRU EEPROM bank image appended after
//       the flash image (set sys_serial_num / buildfru write the FRU JEDEC
//       EEPROMs, so the snapshot must carry them).  Pre-v4 rejected.
//   v4 -> v5 (2026-06-07): manifest-driven IIC content.  The fixed FRU bank
//       is replaced by a count-prefixed list of the configured devices'
//       256-byte images (FRU + NVRAM mutable content) in bus order.  Device
//       IDENTITY is re-applied from the platform manifest before restore;
//       only mutable content travels.  Pre-v5 snapshots rejected (the IIC
//       block layout changed; not backward-readable).
inline constexpr uint32_t kCpuStateVersion       = 7;  // v7: + CpuState.fpcr
inline constexpr uint32_t kChipsetVersion        = 5;  // v5: manifest-driven IIC content
inline constexpr char     kSnapshotExtension[]   = ".axpsnap";
inline constexpr char     kSnapshotDirDefault[]  = "snapshots";

// Periodic auto-save policy (Machine::run consults these).
// 2026-06-04: 10M -> 1B cycles.  Tickwarp/RSCC warp raised effective
// throughput to ~40M cyc/s wall; at 10M the 69 MB periodic save fired
// ~4x/second and dominated run I/O (observed in the 20260603-211444
// run tail).  1B ~= one save per ~25 s wall at warped speed.
inline constexpr uint64_t kAutoSavePeriodCycles  = 1000ULL * 1000ULL * 1000ULL;
inline constexpr int      kAutoSaveKeepCount     = 5;


// ---------------------------------------------------------------------------
// SnapshotResult -- return value from save / load / autoload.
// ---------------------------------------------------------------------------
struct SnapshotResult
{
    bool        success      = false;
    std::string path;                // resolved path written or read
    uint64_t    bytesWritten = 0;    // save only; 0 on load
    uint64_t    bytesRead    = 0;    // load only; 0 on save
    uint64_t    cycleAtCapture = 0;  // load: cycle recorded in file
    std::string errorMessage;        // empty on success
};


// ---------------------------------------------------------------------------
// save / load / autoload entry points.
// ---------------------------------------------------------------------------
// save / load take a Machine by reference.  The Machine must already
// be constructed (memory size, chipset variant, device wiring done);
// snapshot only overwrites the state-bearing fields, never the
// construction parameters.  A variant / memory-size mismatch between
// the snapshot file and the live Machine is a hard failure with a
// descriptive errorMessage.
SnapshotResult save(Machine& machine,
                    std::filesystem::path const& path,
                    char const*                  comment = "manual") noexcept;

SnapshotResult load(Machine&                      machine,
                    std::filesystem::path const&  path) noexcept;

// autoloadLatest scans dir for files matching "*.axpsnap" and loads
// the one with the newest mtime.  Returns the SnapshotResult for the
// load attempt; success == false and an empty path indicate "no file
// found -- cold boot."  Diagnostic logging is the caller's job.
SnapshotResult autoloadLatest(Machine&                     machine,
                              std::filesystem::path const& dir =
                                  kSnapshotDirDefault) noexcept;

// pruneOldSnapshots removes files in dir matching "auto_*.axpsnap"
// beyond the keepCount newest.  Used by the periodic auto-save path
// to bound disk usage.  Files outside the "auto_" prefix (manual /
// named captures) are never touched.
void pruneOldSnapshots(std::filesystem::path const& dir,
    int                          keepCount) noexcept;

} // namespace systemLib

#endif // SYSTEMLIB_SNAPSHOT_H