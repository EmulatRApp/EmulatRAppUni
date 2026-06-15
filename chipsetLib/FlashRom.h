// ============================================================================
// FlashRom.h -- AMD Am29F016 (2 MB) flash ROM on the Tsunami TIG bus
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// The SRM firmware reaches the console-init path only after from_init() ->
// init_flash_rom() succeeds.  init_flash_rom sends the AMD autoselect unlock
// (AA@0x5555 / 55@0x2AAA / 90@0x5555) and reads manufacturer ID 0x01 at flash
// offset 0; with no flash device the read is 0xFF, init_flash_rom returns
// failure, from_init returns 1 ("00000001 exit status for from_init"), and the
// eerom inode is never created ("file open failed for eerom").  Modeling this
// device clears that single root cause.
//
// Identity, command sequences, sector map and factory-erased state are
// confirmed against the AMD Am29F016B datasheet (Pub# 21444 Rev B, Table 5
// Command Definitions + Table 2 Sector Address Table); the state machine is
// mirrored from AXPBox src/Flash.cpp, which the datasheet validates.
//
// CONSTRAINTS (verified against AXPBox src/Flash.cpp, mirrored deliberately):
//   C1  Flash holds configuration/NVRAM only, NOT firmware.  The SRM image
//       stays on the decompressor path; instruction fetch never routes here.
//   C2  Byte program is a naive overwrite (m_data[off] = data).  No 1->0 AND
//       masking -- correct because firmware always erases the sector first.
//   C3  Only completion signaling is the erase handshake (read returns 0x80
//       twice, then back to read-array).  No DQ6/DQ7 toggle-bit polling.
//       0x80 = DQ7 set, DQ6 clear: a single returned value that reads as
//       "done" under BOTH datasheet completion methods -- Data# Polling
//       (DQ7 true) and Toggle Bit (DQ6 stable across two reads) -- which is
//       why this minimal handshake satisfies the firmware without modeling
//       the toggle bits.  Byte program drops straight to read-array, so a
//       post-program status read returns the final array datum (DQ7 already
//       matches the programmed value = "done").
//   C4  Access width / dsize is ignored: one byte per shifted address.  Reads
//       return the single backing byte zero-extended.  The >>6 PA stride (done
//       by the caller) makes each flash byte its own aligned address.
//
// PERSISTENCE (architect decisions D1/D2):
//   D1  dirty-flag + coalesce-on-quiescence: flush only after a debounce
//       window W with no flash mutation, so one SRM `set' (one sector erase +
//       N byte programs) coalesces into ONE atomic flush; never flush mid
//       transaction (no all-0xFF sector persisted).  Clean-shutdown forceFlush
//       is the backstop.
//   D2  backing file is a RAW 2 MB image (inspectable; no AXPBox statefile
//       wrapper).  Size mismatch -> factory 0xFF init.  The command-machine
//       mode is never serialized; the device always boots in read-array mode.
//
// The device is opaque bytes: any header / magic / checksum in the eerom
// region is firmware-authored CONTENT and is deliberately NOT modeled here.
// ============================================================================

#ifndef TSUNAMI_FLASHROM_H
#define TSUNAMI_FLASHROM_H

#include <cstdint>
#include <string>
#include <vector>

class FlashRom
{
public:
    // Geometry / identity (AMD Am29F016, PC264/DS10 flash).
    static constexpr uint32_t kSize           = 0x200000;  // 2 MB
    static constexpr uint32_t kSectorSize     = 0x10000;   // 64 KB
    static constexpr uint8_t  kManufacturerId = 0x01;      // AMD
    static constexpr uint8_t  kDeviceId       = 0xAD;      // Am29F016
    static constexpr uint8_t  kErased         = 0xFF;

    // Debounce window (cycles).  Chosen >> the intra-`set' byte-program gap so
    // a whole programming burst coalesces into one flush, while env writes
    // (rare, off the hot path) still persist promptly after the burst ends.
    static constexpr uint64_t kFlushQuiescenceCycles = 8000000ULL;

    FlashRom() noexcept;

    // ---- Bus access -------------------------------------------------------
    // `off' is the flash byte offset = (pa - TIG_flash_base) >> 6, computed by
    // the chipset.  width is intentionally absent (C4): one byte per address.
    [[nodiscard]] uint64_t read(uint32_t off) noexcept;
    void                   write(uint32_t off, uint64_t data) noexcept;

    // ---- Persistence (D1/D2) ---------------------------------------------
    // loadRaw: bind the backing file and load it; size != kSize (or missing)
    // leaves the array at factory 0xFF.  Always boots in read-array mode.
    void loadRaw(const std::string& path) noexcept;
    // tryFlush: debounced quiescence flush; call every chipset step with the
    // monotonic cycle accumulator.
    void tryFlush(uint64_t nowCycle) noexcept;
    // forceFlush: unconditional flush of pending changes (clean-shutdown).
    void forceFlush() noexcept;

    // backingLoaded: true iff loadRaw restored a full image from the
    // backing file (false on first boot / missing / wrong-size image).
    [[nodiscard]] bool backingLoaded() const noexcept { return m_backingLoaded; }

    // seedFrom (2026-06-03): initialize the flash array from a firmware
    // ROM image.  On real hardware the 2 MB TIG flash CONTAINS the
    // shipped SRM ROM -- env blocks, DSRDB, SROM config -- not erased
    // 0xFF (AXPBox seeds its CFlash from rom.srm the same way).  A
    // factory-0xFF flash made build_dsrdb follow an all-ones header
    // 14 MB past the device end (the 2026-06-03 fread / build_hwrpb SRM
    // crash dump).  Copies min(n, kSize) bytes; the remainder stays
    // erased.  Marks the array dirty so the debounce poll persists the
    // seeded image to the backing file -- subsequent boots then restore
    // it via loadRaw with any firmware env writes layered on top.
    void seedFrom(const uint8_t* data, size_t n) noexcept;

    // ---- Snapshot support (kChipsetVersion 3, 2026-06-06) ----------------
    // SRM environment variables live in this flash (real DS10 stores env in
    // the TIG flash), so a faithful machine snapshot must carry the 2 MB
    // image or restored env is lost.  FlashRom stays Qt-free (D2 design): the
    // chipset's QDataStream (de)serializer owns the wire I/O and moves the
    // array through these two.  image() is the read side; restoreImage()
    // adopts bytes on load, mirroring a successful loadRaw (mode=Read,
    // backingLoaded=true, dirty=false -- a stale snapshot never rewrites a
    // newer backing file).
    [[nodiscard]] const std::vector<uint8_t>& image() const noexcept { return m_data; }
    void restoreImage(const uint8_t* data, size_t n) noexcept;

private:
    // AMD command state machine (mirrors AXPBox CFlash mode set exactly).
    enum class Mode : uint8_t {
        Read, Step1, Step2, Autosel, Program,
        Erase3, Erase4, Erase5, Confirm0, Confirm1
    };

    void factoryInit() noexcept;
    bool flushToFile() noexcept;   // atomic temp-write + rename

    std::vector<uint8_t> m_data;   // heap-backed so 2 MB does not inflate the
                                   // chipset object (mirrors PAL-scratch pattern)
    Mode        m_mode            = Mode::Read;
    bool        m_dirty           = false;  // unflushed mutations exist
    bool        m_pendingMutation = false;  // a mutation occurred since last poll
    bool        m_backingLoaded   = false;  // loadRaw restored a full image (2026-06-03)
    uint64_t    m_lastMutateCycle = 0;
    std::string m_path;                     // backing file; empty = no persistence
};

#endif // TSUNAMI_FLASHROM_H
