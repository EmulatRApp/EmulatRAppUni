// ============================================================================
// tests/chipsetLib/test_flashrom.cpp
//   FlashRom -- AMD Am29F016 command machine + factory/persistence behavior
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Exercises the mirrored AXPBox AMD state machine (C2/C3): autoselect id read,
// the erase handshake (0x80 twice -> read-array), naive program-after-erase,
// per-sector erase scope, and the factory-0xFF fallback when no backing image
// is present.  Offsets are flash byte offsets (post >>6), exactly as the
// firmware's command_sequence() addresses them (0x5555 / 0x2AAA).
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/FlashRom.h"

#include <cstdint>

namespace {

void unlock(FlashRom& f) {
    f.write(0x5555, 0xAA);
    f.write(0x2AAA, 0x55);
}

void autoselect(FlashRom& f) {
    unlock(f);
    f.write(0x5555, 0x90);
}

// Any write that is not the AA@0x5555 unlock returns the machine to read-array
// from Read/Autosel (mirrors AXPBox; the firmware reset sequence ends here too).
void resetToRead(FlashRom& f) {
    f.write(0, 0xF0);
}

void sectorErase(FlashRom& f, uint32_t off) {
    unlock(f);
    f.write(0x5555, 0x80);
    unlock(f);
    f.write(off, 0x30);
}

// Drains the two-read erase handshake so the device is back in read-array.
void drainEraseHandshake(FlashRom& f) {
    f.read(0);
    f.read(0);
}

void programByte(FlashRom& f, uint32_t off, uint8_t v) {
    unlock(f);
    f.write(0x5555, 0xA0);
    f.write(off, v);
}

} // namespace

TEST_CASE("FlashRom factory state is erased 0xFF") {
    FlashRom f;
    CHECK(f.read(0x0000) == 0xFF);
    CHECK(f.read(0x1234) == 0xFF);
    CHECK(f.read(FlashRom::kSize - 1) == 0xFF);
}

TEST_CASE("FlashRom autoselect returns AMD Am29F016 id, then read-array") {
    FlashRom f;
    autoselect(f);
    CHECK(f.read(0) == FlashRom::kManufacturerId);  // 0x01 (AMD)
    CHECK(f.read(1) == FlashRom::kDeviceId);         // 0xAD (Am29F016)
    resetToRead(f);
    CHECK(f.read(0) == 0xFF);                        // array again
}

TEST_CASE("FlashRom erase handshake returns 0x80 twice then read-array") {
    FlashRom f;
    sectorErase(f, 0x50000);                 // eerom sector
    CHECK(f.read(0) == 0x80);                // Confirm1 -> Confirm0
    CHECK(f.read(0) == 0x80);                // Confirm0 -> Read
    CHECK(f.read(0x50000) == 0xFF);          // erased data visible
}

TEST_CASE("FlashRom program after erase round-trips a byte") {
    FlashRom f;
    sectorErase(f, 0x50000);
    drainEraseHandshake(f);
    programByte(f, 0x51000, 0x42);
    CHECK(f.read(0x51000) == 0x42);
    CHECK(f.read(0x51001) == 0xFF);          // neighbor still erased
}

TEST_CASE("FlashRom sector erase clears only its own 64 KB sector") {
    FlashRom f;
    sectorErase(f, 0x50000);
    drainEraseHandshake(f);
    programByte(f, 0x50010, 0xAA);

    sectorErase(f, 0x60000);
    drainEraseHandshake(f);
    programByte(f, 0x60010, 0xBB);

    CHECK(f.read(0x50010) == 0xAA);
    CHECK(f.read(0x60010) == 0xBB);

    // Re-erase the 0x50000 sector; the 0x60000 sector must survive.
    sectorErase(f, 0x50000);
    drainEraseHandshake(f);
    CHECK(f.read(0x50010) == 0xFF);
    CHECK(f.read(0x60010) == 0xBB);
}

TEST_CASE("FlashRom missing backing file leaves factory 0xFF") {
    FlashRom f;
    f.loadRaw("definitely_nonexistent_flash_image_xyz.rom");
    CHECK(f.read(0x00000) == 0xFF);
    CHECK(f.read(0x40000) == 0xFF);
}
