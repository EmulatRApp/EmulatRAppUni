// ============================================================================
// tests/deviceLib/test_scsi_iso.cpp -- VirtualIsoDevice (no-media ATAPI CD)
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
// ============================================================================
//
// S1 of the IDE/ATAPI scaffold.  Locks contract C2: an empty CD reports type
// 0x05 to INQUIRY but returns FAIL-FAST sense 02/3A/00 (medium not present),
// never 02/04/xx (becoming ready -- retry-eligible).  doctest CHECK only.
// ============================================================================

#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "deviceLib/scsi/VirtualIsoDevice.h"
#include "deviceLib/scsi/IBlockMedia.h"
#include "tests/deviceLib/MockBlockMedia.h"

using namespace scsi;

namespace {
// Write a synthetic ISO image of `blocks` 2048-byte blocks; block N is filled
// with byte value (N & 0xFF) so reads can be content-verified.  Returns the path
// (a temp file in CWD) or "" on failure.
inline std::string makeTestIso(uint32_t blocks)
{
    std::string const path = "test_iso_media.tmp";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return "";
    std::vector<uint8_t> blk(VirtualIsoDevice::kBlockBytes);
    for (uint32_t b = 0; b < blocks; ++b) {
        std::fill(blk.begin(), blk.end(), static_cast<uint8_t>(b & 0xFFu));
        std::fwrite(blk.data(), 1, blk.size(), f);
    }
    std::fclose(f);
    return path;
}
} // namespace

TEST_CASE("VirtualIsoDevice: INQUIRY reports a removable CD-ROM (type 0x05)")
{
    VirtualIsoDevice cd;
    uint8_t buf[36] = { 0 };
    uint8_t cdb[6]  = { ScsiOp::INQUIRY, 0, 0, 0, 36, 0 };
    ScsiCommand cmd;
    cmd.cdb = cdb; cmd.cdbLength = 6;
    cmd.dataBuffer = buf; cmd.dataBufferLength = 36;

    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::Good);
    CHECK((buf[0] & 0x1f) == 0x05);          // peripheral device type = CD/DVD
    CHECK(buf[1] == 0x80);                    // removable medium
    CHECK(cmd.dataTransferred == 36);
    CHECK(cd.deviceType() == ScsiPeripheralDeviceType::CdDvdDevice);
}

TEST_CASE("VirtualIsoDevice: TEST UNIT READY with no media -> 02/3A/00")
{
    VirtualIsoDevice cd;
    uint8_t cdb[6] = { ScsiOp::TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 6;

    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::CheckCondition);
    CHECK(cmd.senseValid);
    CHECK((cmd.senseData.bytes()[2] & 0x0f) == 0x02);   // NOT READY
    CHECK(cmd.senseData.bytes()[12] == 0x3A);           // ASC: medium not present
    CHECK(cmd.senseData.bytes()[13] == 0x00);           // ASCQ
}

TEST_CASE("VirtualIsoDevice: READ10 no media is FAIL-FAST, not retry-eligible")
{
    VirtualIsoDevice cd;
    uint8_t cdb[10] = { ScsiOp::READ10, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 10;

    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::CheckCondition);
    CHECK(cmd.senseData.bytes()[12] == 0x3A);           // medium not present
    // Critically NOT 04/01 (becoming ready), which the firmware would retry.
    // (Precomputed bool -- doctest's CHECK can't decompose a compound &&.)
    bool const becomingReady = (cmd.senseData.bytes()[12] == 0x04) &&
                               (cmd.senseData.bytes()[13] == 0x01);
    CHECK_FALSE(becomingReady);
}

TEST_CASE("VirtualIsoDevice: REQUEST SENSE returns the medium-not-present sense")
{
    VirtualIsoDevice cd;
    uint8_t tcdb[6] = { ScsiOp::TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    ScsiCommand t; t.cdb = tcdb; t.cdbLength = 6;
    cd.handleCommand(t);                                 // arm last-sense

    uint8_t buf[18] = { 0 };
    uint8_t rcdb[6] = { ScsiOp::REQUEST_SENSE, 0, 0, 0, 18, 0 };
    ScsiCommand r; r.cdb = rcdb; r.cdbLength = 6;
    r.dataBuffer = buf; r.dataBufferLength = 18;

    cd.handleCommand(r);

    CHECK(r.status == ScsiStatus::Good);                 // REQUEST SENSE succeeds
    CHECK((buf[2] & 0x0f) == 0x02);
    CHECK(buf[12] == 0x3A);
    CHECK(r.dataTransferred == 18);
}

TEST_CASE("VirtualIsoDevice: unknown opcode -> illegal request (0x20)")
{
    VirtualIsoDevice cd;
    uint8_t cdb[6] = { 0xFE, 0, 0, 0, 0, 0 };
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 6;

    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::CheckCondition);
    CHECK(cmd.senseData.bytes()[12] == 0x20);            // invalid command opcode
}

// ===== S6: media file backend (#31) =========================================

TEST_CASE("VirtualIsoDevice: loadMedia + TEST UNIT READY good, READ CAPACITY")
{
    std::string const iso = makeTestIso(4);              // 4 blocks
    REQUIRE(!iso.empty());
    VirtualIsoDevice cd;
    REQUIRE(cd.loadMedia(iso));
    CHECK(cd.hasMedia());
    CHECK(cd.blockCount() == 4u);

    uint8_t tcdb[6] = { ScsiOp::TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    ScsiCommand t; t.cdb = tcdb; t.cdbLength = 6;
    cd.handleCommand(t);
    CHECK(t.status == ScsiStatus::Good);                 // media present -> ready

    uint8_t cap[8] = { 0 };
    uint8_t ccdb[10] = { ScsiOp::READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    ScsiCommand c; c.cdb = ccdb; c.cdbLength = 10;
    c.dataBuffer = cap; c.dataBufferLength = 8;
    cd.handleCommand(c);
    CHECK(c.status == ScsiStatus::Good);
    CHECK(c.dataTransferred == 8u);
    // last LBA = blockCount-1 = 3 (big-endian); block size = 2048.
    uint32_t const lastLba = (uint32_t(cap[0])<<24)|(uint32_t(cap[1])<<16)|(uint32_t(cap[2])<<8)|cap[3];
    uint32_t const blkSz   = (uint32_t(cap[4])<<24)|(uint32_t(cap[5])<<16)|(uint32_t(cap[6])<<8)|cap[7];
    CHECK(lastLba == 3u);
    CHECK(blkSz   == 2048u);

    std::remove(iso.c_str());
}

TEST_CASE("VirtualIsoDevice: READ10 returns image content at the requested LBA")
{
    std::string const iso = makeTestIso(4);
    REQUIRE(!iso.empty());
    VirtualIsoDevice cd;
    REQUIRE(cd.loadMedia(iso));

    uint8_t buf[2048] = { 0xCC };
    // READ10 LBA=2, 1 block.
    uint8_t cdb[10] = { ScsiOp::READ10, 0, 0,0,0,2, 0, 0,1, 0 };
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 10;
    cmd.dataBuffer = buf; cmd.dataBufferLength = 2048;
    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::Good);
    CHECK(cmd.dataTransferred == 2048u);
    CHECK(buf[0]    == 0x02);                             // block 2 filled with 0x02
    CHECK(buf[2047] == 0x02);

    std::remove(iso.c_str());
}

TEST_CASE("VirtualIsoDevice: READ10 past end of medium -> 05/21/00 (LBA out of range)")
{
    std::string const iso = makeTestIso(2);              // valid LBAs 0,1
    REQUIRE(!iso.empty());
    VirtualIsoDevice cd;
    REQUIRE(cd.loadMedia(iso));

    uint8_t buf[2048] = { 0 };
    uint8_t cdb[10] = { ScsiOp::READ10, 0, 0,0,0,5, 0, 0,1, 0 };   // LBA 5 (oob)
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 10;
    cmd.dataBuffer = buf; cmd.dataBufferLength = 2048;
    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::CheckCondition);
    CHECK((cmd.senseData.bytes()[2] & 0x0f) == 0x05);    // ILLEGAL REQUEST
    CHECK(cmd.senseData.bytes()[12] == 0x21);            // LBA out of range
    CHECK(cmd.dataTransferred == 0u);

    std::remove(iso.c_str());
}

TEST_CASE("VirtualIsoDevice: READ TOC reports a single data track at LBA 0")
{
    std::string const iso = makeTestIso(4);
    REQUIRE(!iso.empty());
    VirtualIsoDevice cd;
    REQUIRE(cd.loadMedia(iso));

    uint8_t buf[20] = { 0 };
    uint8_t cdb[10] = { ScsiOp::READ_TOC, 0, 0, 0, 0, 0, 0, 0, 20, 0 };
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 10;
    cmd.dataBuffer = buf; cmd.dataBufferLength = 20;
    cd.handleCommand(cmd);

    CHECK(cmd.status == ScsiStatus::Good);
    CHECK(buf[2] == 0x01);                                // first track
    CHECK(buf[3] == 0x01);                                // last track
    CHECK(buf[5] == 0x14);                                // track 1: data track
    CHECK(buf[6] == 0x01);                                // track number 1
    // track 1 start LBA = 0
    uint32_t const t1 = (uint32_t(buf[8])<<24)|(uint32_t(buf[9])<<16)|(uint32_t(buf[10])<<8)|buf[11];
    CHECK(t1 == 0u);

    std::remove(iso.c_str());
}

TEST_CASE("VirtualIsoDevice: ejectMedia returns to no-media fail-fast")
{
    std::string const iso = makeTestIso(2);
    REQUIRE(!iso.empty());
    VirtualIsoDevice cd;
    REQUIRE(cd.loadMedia(iso));
    cd.ejectMedia();
    CHECK_FALSE(cd.hasMedia());

    uint8_t cdb[6] = { ScsiOp::TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    ScsiCommand cmd; cmd.cdb = cdb; cmd.cdbLength = 6;
    cd.handleCommand(cmd);
    CHECK(cmd.status == ScsiStatus::CheckCondition);
    CHECK(cmd.senseData.bytes()[12] == 0x3A);            // medium not present again

    std::remove(iso.c_str());
}

// ===== drive-level over the IBlockMedia seam (MockBlockMedia, no filesystem) ==

TEST_CASE("VirtualIsoDevice: READ(10) over MockBlockMedia + isPresent toggle (no-media end to end)")
{
    auto m = std::make_unique<MockBlockMedia>();
    m->cfgBlockSize = 2048; m->cfgBlockCount = 4; m->cfgReadOnly = true;
    MockBlockMedia* raw = m.get();               // keep a handle to toggle presence
    REQUIRE(m->open() == MediaStatus::Ok);

    VirtualIsoDevice cd;
    cd.setMedia(std::move(m));
    CHECK(cd.hasMedia());

    // READ10 LBA 1 -> good, block filled with 0x01 by the mock.
    uint8_t buf[2048] = { 0 };
    uint8_t r10[10] = { ScsiOp::READ10, 0, 0,0,0,1, 0, 0,1, 0 };
    ScsiCommand c; c.cdb = r10; c.cdbLength = 10; c.dataBuffer = buf; c.dataBufferLength = 2048;
    cd.handleCommand(c);
    CHECK(c.status == ScsiStatus::Good);
    CHECK(c.dataTransferred == 2048u);
    CHECK(buf[0] == 0x01);

    // Eject the disc (removable): isPresent=false -> no-media report end to end.
    raw->cfgPresent = false;
    CHECK_FALSE(cd.hasMedia());
    ScsiCommand c2; c2.cdb = r10; c2.cdbLength = 10; c2.dataBuffer = buf; c2.dataBufferLength = 2048;
    cd.handleCommand(c2);
    CHECK(c2.status == ScsiStatus::CheckCondition);
    CHECK(c2.senseData.bytes()[12] == 0x3A);     // 02/3A/00 medium not present
}

TEST_CASE("VirtualIsoDevice: MediaStatus maps to sense -- OutOfRange 0x21, IoError 0x11")
{
    // Out-of-range READ via the mock's range check.
    {
        auto m = std::make_unique<MockBlockMedia>();
        m->cfgBlockSize = 2048; m->cfgBlockCount = 2;
        REQUIRE(m->open() == MediaStatus::Ok);
        VirtualIsoDevice cd; cd.setMedia(std::move(m));

        uint8_t buf[2048] = { 0 };
        uint8_t r10[10] = { ScsiOp::READ10, 0, 0,0,0,5, 0, 0,1, 0 };   // LBA 5 (oob)
        ScsiCommand c; c.cdb = r10; c.cdbLength = 10; c.dataBuffer = buf; c.dataBufferLength = 2048;
        cd.handleCommand(c);
        CHECK(c.status == ScsiStatus::CheckCondition);
        CHECK(c.senseData.bytes()[12] == 0x21);  // LBA out of range
    }
    // Injected IoError at a valid LBA.
    {
        auto m = std::make_unique<MockBlockMedia>();
        m->cfgBlockSize = 2048; m->cfgBlockCount = 8;
        m->faultOnRead = true; m->faultLba = 3; m->faultStatus = MediaStatus::IoError;
        REQUIRE(m->open() == MediaStatus::Ok);
        VirtualIsoDevice cd; cd.setMedia(std::move(m));

        uint8_t buf[2048] = { 0 };
        uint8_t r10[10] = { ScsiOp::READ10, 0, 0,0,0,3, 0, 0,1, 0 };   // LBA 3 (fault)
        ScsiCommand c; c.cdb = r10; c.cdbLength = 10; c.dataBuffer = buf; c.dataBufferLength = 2048;
        cd.handleCommand(c);
        CHECK(c.status == ScsiStatus::CheckCondition);
        CHECK(c.senseData.bytes()[12] == 0x11);  // unrecovered read error
    }
}
