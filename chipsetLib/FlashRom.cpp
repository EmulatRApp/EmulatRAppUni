// ============================================================================
// FlashRom.cpp -- AMD Am29F016 (2 MB) flash ROM on the Tsunami TIG bus
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
// State machine and identity bytes mirror AXPBox src/Flash.cpp verbatim
// (constraints C2/C3 in FlashRom.h).  Persistence is the architect's
// debounce/raw-image model (D1/D2), not AXPBox's shutdown-only statefile.
// ============================================================================

#include "FlashRom.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>   // std::getenv (TEMP flash trace, 2026-06-06)
#include <cstring>   // std::memcpy (seedFrom, 2026-06-03)

// Command-sequence addresses are flash byte offsets (post >>6), matching the
// firmware's command_sequence() writes to 0x5555 / 0x2AAA.
namespace {
    constexpr uint32_t kUnlockAddr1 = 0x5555;
    constexpr uint32_t kUnlockAddr2 = 0x2AAA;
    constexpr uint8_t  kCmdUnlock1  = 0xAA;
    constexpr uint8_t  kCmdUnlock2  = 0x55;
    constexpr uint8_t  kCmdAutosel  = 0x90;
    constexpr uint8_t  kCmdProgram  = 0xA0;
    constexpr uint8_t  kCmdErase    = 0x80;
    constexpr uint8_t  kCmdChipEra  = 0x10;
    constexpr uint8_t  kCmdSectEra  = 0x30;
    constexpr uint8_t  kEraseStatus = 0x80;  // handshake byte (C3)
    constexpr uint32_t kOffMask     = FlashRom::kSize - 1;  // 0x1FFFFF (2 MB pow2)

    // TEMP DIAGNOSTIC 2026-06-06 (task #9): env-gated trace to determine
    // whether SRM `set'/`update srm' writes actually reach FlashRom (vs
    // landing in a non-persisted region).  Silent unless EMULATR_FLASH_TRACE
    // is set; flash writes only occur on set/update so it never spams a
    // normal run.  Remove with the other TEMP diagnostics (task #6).
    inline bool flashTraceOn() noexcept
    {
        static bool const on = (std::getenv("EMULATR_FLASH_TRACE") != nullptr);
        return on;
    }
}

FlashRom::FlashRom() noexcept
    : m_data(kSize, kErased)
{
}

void FlashRom::factoryInit() noexcept
{
    std::fill(m_data.begin(), m_data.end(), kErased);
    m_mode  = Mode::Read;
    m_dirty = false;
    m_pendingMutation = false;
}

// ----------------------------------------------------------------------------
// read -- one zero-extended byte (C4).  Autoselect returns manufacturer/device
// id; the erase handshake returns 0x80 twice then drops to read-array (C3).
// ----------------------------------------------------------------------------
uint64_t FlashRom::read(uint32_t off) noexcept
{
    switch (m_mode) {
    case Mode::Autosel:
        switch (off) {
        case 0:  return kManufacturerId;  // AMD
        case 1:  return kDeviceId;         // Am29F016
        default: return 0;
        }
    case Mode::Confirm1:
        m_mode = Mode::Confirm0;
        return kEraseStatus;
    case Mode::Confirm0:
        m_mode = Mode::Read;
        return kEraseStatus;
    default:
        return m_data[off & kOffMask];
    }
}

// ----------------------------------------------------------------------------
// write -- AMD command machine.  Mirrors AXPBox CFlash::WriteMem exactly.
// Byte program is a naive overwrite (C2).
// ----------------------------------------------------------------------------
void FlashRom::write(uint32_t off, uint64_t data) noexcept
{
    uint8_t const d = static_cast<uint8_t>(data);

    // TEMP DIAGNOSTIC (task #9): log every AMD-FSM write so a set/update
    // shows the full unlock/erase/program stream -- or its absence.
    if (flashTraceOn()) {
        std::fprintf(stderr,
                     "FlashRom::write off=0x%06x data=0x%02x mode=%d\n",
                     static_cast<unsigned>(off & kOffMask),
                     static_cast<unsigned>(d),
                     static_cast<int>(m_mode));
        std::fflush(stderr);
    }

    switch (m_mode) {
    case Mode::Read:
    case Mode::Autosel:
        if (off == kUnlockAddr1 && d == kCmdUnlock1) { m_mode = Mode::Step1; return; }
        m_mode = Mode::Read;
        return;

    case Mode::Step1:
        if (off == kUnlockAddr2 && d == kCmdUnlock2) { m_mode = Mode::Step2; return; }
        m_mode = Mode::Read;
        return;

    case Mode::Step2:
        if (off != kUnlockAddr1) { m_mode = Mode::Read; return; }
        switch (d) {
        case kCmdAutosel: m_mode = Mode::Autosel; return;
        case kCmdProgram: m_mode = Mode::Program; return;
        case kCmdErase:   m_mode = Mode::Erase3;  return;
        }
        m_mode = Mode::Read;
        return;

    case Mode::Erase3:
        if (off == kUnlockAddr1 && d == kCmdUnlock1) { m_mode = Mode::Erase4; return; }
        m_mode = Mode::Read;
        return;

    case Mode::Erase4:
        if (off == kUnlockAddr2 && d == kCmdUnlock2) { m_mode = Mode::Erase5; return; }
        m_mode = Mode::Read;
        return;

    case Mode::Erase5:
        if (off == kUnlockAddr1 && d == kCmdChipEra) {
            // Chip erase: whole device to 0xFF.
            std::fill(m_data.begin(), m_data.end(), kErased);
            m_dirty = true;
            m_pendingMutation = true;
            m_mode = Mode::Confirm1;
            return;
        }
        if (d == kCmdSectEra) {
            // Sector erase: the 64 KB sector containing `off' to 0xFF.
            uint32_t const secBase = (off & kOffMask) & ~(kSectorSize - 1);
            std::fill(m_data.begin() + secBase,
                      m_data.begin() + secBase + kSectorSize, kErased);
            m_dirty = true;
            m_pendingMutation = true;
            m_mode = Mode::Confirm1;
            return;
        }
        m_mode = Mode::Read;
        return;

    case Mode::Program:
        // Naive overwrite (C2); firmware guarantees a prior sector erase.
        m_data[off & kOffMask] = d;
        m_dirty = true;
        m_pendingMutation = true;
        m_mode = Mode::Read;
        return;

    default:
        m_mode = Mode::Read;
        return;
    }
}

// ----------------------------------------------------------------------------
// Persistence (D1/D2)
// ----------------------------------------------------------------------------
void FlashRom::loadRaw(const std::string& path) noexcept
{
    m_path = path;
    factoryInit();  // 0xFF baseline; overwritten below on a good image

    if (m_path.empty()) return;

    std::FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr,
                     "FlashRom: no backing image at '%s' -- factory 0xFF init\n",
                     m_path.c_str());
        return;  // first boot: firmware will initialize and we persist later
    }

    std::fseek(f, 0, SEEK_END);
    long const len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (len != static_cast<long>(kSize)) {
        std::fprintf(stderr,
                     "FlashRom: '%s' size %ld != %u -- factory 0xFF init\n",
                     m_path.c_str(), len, kSize);
        std::fclose(f);
        return;
    }

    size_t const got = std::fread(m_data.data(), 1, kSize, f);
    std::fclose(f);
    if (got != kSize) {
        std::fprintf(stderr,
                     "FlashRom: short read on '%s' (%zu/%u) -- factory 0xFF init\n",
                     m_path.c_str(), got, kSize);
        factoryInit();
        return;
    }
    // Mode is never restored (D2): always read-array on boot.
    m_mode          = Mode::Read;
    m_dirty         = false;
    m_backingLoaded = true;   // 2026-06-03: gates the firmware-image seeding
    std::fprintf(stderr, "FlashRom: restored %u bytes from '%s'\n",
                 kSize, m_path.c_str());
}

// ----------------------------------------------------------------------------
// seedFrom -- initialize the array from the firmware ROM image (2026-06-03).
// See FlashRom.h for the full rationale (real flash ships with the SRM ROM
// content; factory 0xFF sent build_dsrdb 14 MB off the device end).  Called
// by Machine::loadSrmFirmware only when loadRaw found no backing image, so
// a persisted flash (with the firmware's own env writes) always wins.
// ----------------------------------------------------------------------------
void FlashRom::seedFrom(const uint8_t* data, size_t n) noexcept
{
    if (data == nullptr || n == 0) return;

    size_t const count = (n < static_cast<size_t>(kSize))
                       ? n
                       : static_cast<size_t>(kSize);
    std::memcpy(m_data.data(), data, count);
    // Remainder (if image < 2 MB) stays factory-erased 0xFF.

    m_mode = Mode::Read;
    // Mark dirty + pending so the debounce poll persists the seeded image
    // to the backing file; from then on loadRaw restores it on every boot.
    m_dirty           = true;
    m_pendingMutation = true;
    m_lastMutateCycle = 0;

    std::fprintf(stderr,
                 "FlashRom: seeded %zu bytes from the firmware ROM image "
                 "(no backing file; will persist on first flush)\n",
                 count);
}

// restoreImage -- adopt the array from a snapshot's serialized flash image
// (kChipsetVersion 3, 2026-06-06).  The chipset's QDataStream (de)serializer
// owns the wire I/O (FlashRom stays Qt-free, D2); this just takes the bytes.
// Mirrors a successful loadRaw: command machine resets to read-array (D2), the
// image counts as backing-loaded, and dirty stays false so restoring a stale
// snapshot never clobbers a newer ds10_flash.rom on the debounce poll -- the
// snapshot now carries env independently of the backing file.
// ----------------------------------------------------------------------------
void FlashRom::restoreImage(const uint8_t* data, size_t n) noexcept
{
    if (m_data.size() != static_cast<size_t>(kSize)) {
        m_data.assign(static_cast<size_t>(kSize), kErased);
    }
    size_t const count = (n < static_cast<size_t>(kSize))
                       ? n
                       : static_cast<size_t>(kSize);
    if (data != nullptr && count > 0) {
        std::memcpy(m_data.data(), data, count);
    }
    // Remainder (if the image was short) stays factory-erased 0xFF.
    if (count < static_cast<size_t>(kSize)) {
        std::fill(m_data.begin() + static_cast<std::ptrdiff_t>(count),
                  m_data.end(), kErased);
    }
    m_mode            = Mode::Read;   // D2: always restore in read-array mode
    m_backingLoaded   = true;
    m_dirty           = false;        // snapshot bytes match capture; do not
    m_pendingMutation = false;        // proactively rewrite the backing file
    m_lastMutateCycle = 0;
}

bool FlashRom::flushToFile() noexcept
{
    if (m_path.empty()) return false;

    std::string const tmp = m_path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "FlashRom: cannot open '%s' for write\n", tmp.c_str());
        return false;
    }
    size_t const put = std::fwrite(m_data.data(), 1, kSize, f);
    bool ok = (put == kSize);
    if (std::fflush(f) != 0) ok = false;
    std::fclose(f);
    if (!ok) {
        std::fprintf(stderr, "FlashRom: short write to '%s'\n", tmp.c_str());
        std::remove(tmp.c_str());
        return false;
    }

    // Atomic replace.  std::rename will not overwrite an existing file on
    // Windows, so remove the destination first.  The crash window between
    // remove and rename loses only the rename (the .tmp image is intact);
    // acceptable per D1's stated durability tolerance.
    std::remove(m_path.c_str());
    if (std::rename(tmp.c_str(), m_path.c_str()) != 0) {
        std::fprintf(stderr, "FlashRom: rename '%s' -> '%s' failed\n",
                     tmp.c_str(), m_path.c_str());
        return false;
    }
    // TEMP DIAGNOSTIC (task #9): confirm a flush actually hit the file.
    if (flashTraceOn()) {
        std::fprintf(stderr, "FlashRom: flushToFile wrote %u bytes -> %s\n",
                     static_cast<unsigned>(kSize), m_path.c_str());
        std::fflush(stderr);
    }
    return true;
}

void FlashRom::tryFlush(uint64_t nowCycle) noexcept
{
    if (m_pendingMutation) {
        // Activity since the last poll: extend the debounce window so a
        // multi-program burst (one SRM `set') coalesces into a single flush.
        m_lastMutateCycle = nowCycle;
        m_pendingMutation = false;
        return;
    }
    if (m_dirty && (nowCycle - m_lastMutateCycle) >= kFlushQuiescenceCycles) {
        if (flushToFile()) m_dirty = false;
    }
}

void FlashRom::forceFlush() noexcept
{
    if (m_dirty && flushToFile()) m_dirty = false;
}
