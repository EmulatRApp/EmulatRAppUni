// ============================================================================
// tests/deviceLib/test_block_media.cpp -- IBlockMedia: FileBlockMedia + Mock
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
// Phase A media-layer coverage (approved 2026-06-12): FileBlockMedia round-trip
// + read-only enforcement, and MockBlockMedia status contract incl. fault
// injection.  Drive-level tests (ATAPI READ(10) against MockBlockMedia) follow
// the drive refactor.
// ============================================================================

#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "deviceLib/scsi/FileBlockMedia.h"
#include "deviceLib/scsi/BlockMediaFactory.h"
#include "tests/deviceLib/MockBlockMedia.h"

using namespace scsi;

namespace {
// Create a flat image of `blocks` x `bs` bytes, each block filled with `fill`.
inline std::string makeFlatImage(const char* name, uint32_t bs, uint32_t blocks, uint8_t fill)
{
    std::FILE* f = std::fopen(name, "wb");
    if (!f) return "";
    std::vector<uint8_t> b(bs, fill);
    for (uint32_t i = 0; i < blocks; ++i) std::fwrite(b.data(), 1, bs, f);
    std::fclose(f);
    return name;
}
} // namespace

TEST_CASE("FileBlockMedia: 512-byte RW round-trip (write then read back)")
{
    std::string const img = makeFlatImage("fbm_rw.img", 512, 8, 0x00);
    REQUIRE(!img.empty());

    FileBlockMedia m(img, 512, /*readOnly=*/false);
    REQUIRE(m.open() == MediaStatus::Ok);
    CHECK(m.isOpen());
    CHECK(m.blockSize() == 512u);
    CHECK(m.blockCount() == 8u);
    CHECK_FALSE(m.isReadOnly());

    std::vector<uint8_t> wr(512, 0xA5);
    CHECK(m.write(3, 1, wr.data()) == MediaStatus::Ok);

    std::vector<uint8_t> rd(512, 0x00);
    CHECK(m.read(3, 1, rd.data()) == MediaStatus::Ok);
    CHECK(rd == wr);                                  // exact round-trip

    // out-of-range read past the last block
    CHECK(m.read(8, 1, rd.data()) == MediaStatus::OutOfRange);

    m.close();
    CHECK_FALSE(m.isOpen());
    std::remove(img.c_str());
}

TEST_CASE("FileBlockMedia: 2048-byte ISO is read-only (write -> ReadOnly)")
{
    std::string const img = makeFlatImage("fbm_iso.img", 2048, 4, 0x11);
    REQUIRE(!img.empty());

    FileBlockMedia m(img, 2048, /*readOnly=*/true);
    REQUIRE(m.open() == MediaStatus::Ok);
    CHECK(m.blockSize() == 2048u);
    CHECK(m.blockCount() == 4u);
    CHECK(m.isReadOnly());

    std::vector<uint8_t> rd(2048, 0);
    CHECK(m.read(2, 1, rd.data()) == MediaStatus::Ok);
    CHECK(rd[0] == 0x11);
    CHECK(rd[2047] == 0x11);

    std::vector<uint8_t> wr(2048, 0xFF);
    CHECK(m.write(0, 1, wr.data()) == MediaStatus::ReadOnly);

    m.close();
    std::remove(img.c_str());
}

TEST_CASE("FileBlockMedia: open() of a missing file -> NotOpen")
{
    FileBlockMedia m("does_not_exist_fbm.img", 2048, true);
    CHECK(m.open() == MediaStatus::NotOpen);
    CHECK_FALSE(m.isOpen());
    std::vector<uint8_t> rd(2048, 0);
    CHECK(m.read(0, 1, rd.data()) == MediaStatus::NotOpen);
}

TEST_CASE("FileBlockMedia: create_if_missing makes a blank writable image of the right size")
{
    const char* path = "fbm_created.img";
    std::remove(path);                                   // ensure absent
    // 8 sectors x 512 = 4096 bytes, writable, createBytes set.
    FileBlockMedia m(path, 512, /*readOnly=*/false, /*createBytes=*/4096);
    REQUIRE(m.open() == MediaStatus::Ok);                // created on open
    CHECK(m.blockCount() == 8u);

    std::vector<uint8_t> rd(512, 0xFF);
    CHECK(m.read(0, 1, rd.data()) == MediaStatus::Ok);
    bool allZero = true; for (uint8_t b : rd) if (b) allZero = false;
    CHECK(allZero);                                      // blank target reads zeros

    // round-trip a write to prove it is writable
    std::vector<uint8_t> wr(512, 0x5A), back(512, 0);
    CHECK(m.write(7, 1, wr.data()) == MediaStatus::Ok);
    CHECK(m.read(7, 1, back.data()) == MediaStatus::Ok);
    CHECK(back == wr);
    m.close();
    std::remove(path);
}

TEST_CASE("FileBlockMedia: create_if_missing never creates for read-only media")
{
    const char* path = "fbm_ro_nocreate.img";
    std::remove(path);
    FileBlockMedia m(path, 2048, /*readOnly=*/true, /*createBytes=*/4096);
    CHECK(m.open() == MediaStatus::NotOpen);             // RO: must NOT create
    std::error_code ec;
    CHECK_FALSE(std::filesystem::exists(path, ec));
}

TEST_CASE("FileBlockMedia: create_if_missing never overwrites an existing file")
{
    std::string const img = makeFlatImage("fbm_existing.img", 512, 4, 0xAB);
    REQUIRE(!img.empty());
    // createBytes for a DIFFERENT size: existing file must be left as-is (4 blocks).
    FileBlockMedia m(img, 512, /*readOnly=*/false, /*createBytes=*/512*64);
    REQUIRE(m.open() == MediaStatus::Ok);
    CHECK(m.blockCount() == 4u);                         // untouched
    std::vector<uint8_t> rd(512, 0);
    CHECK(m.read(0, 1, rd.data()) == MediaStatus::Ok);
    CHECK(rd[0] == 0xAB);
    m.close();
    std::remove(img.c_str());
}

TEST_CASE("MockBlockMedia: status contract -- Ok / OutOfRange / NoMedia / ReadOnly")
{
    MockBlockMedia m;
    m.cfgBlockSize = 2048; m.cfgBlockCount = 4; m.cfgReadOnly = true;
    REQUIRE(m.open() == MediaStatus::Ok);
    CHECK(m.blockCount() == 4u);

    std::vector<uint8_t> buf(2048, 0);
    CHECK(m.read(2, 1, buf.data()) == MediaStatus::Ok);
    CHECK(buf[0] == 0x02);                            // block 2 filled with 0x02

    CHECK(m.read(4, 1, buf.data()) == MediaStatus::OutOfRange);   // past end
    CHECK(m.write(0, 1, buf.data()) == MediaStatus::ReadOnly);    // RO write

    m.cfgPresent = false;                             // disc ejected
    CHECK(m.read(0, 1, buf.data()) == MediaStatus::NoMedia);
    CHECK(m.blockCount() == 0u);
}

TEST_CASE("MockBlockMedia: fault injection returns the chosen status at the LBA")
{
    MockBlockMedia m;
    m.cfgBlockSize = 2048; m.cfgBlockCount = 8;
    m.faultOnRead = true; m.faultLba = 5; m.faultStatus = MediaStatus::IoError;
    REQUIRE(m.open() == MediaStatus::Ok);

    std::vector<uint8_t> buf(2048, 0);
    CHECK(m.read(0, 1, buf.data()) == MediaStatus::Ok);          // clean LBA
    CHECK(m.read(5, 1, buf.data()) == MediaStatus::IoError);     // injected fault
    CHECK(m.read(4, 2, buf.data()) == MediaStatus::IoError);     // span covers LBA 5
}

// ===== media_kind factory (deliverable #5): fail-closed, no silent fallback ===

TEST_CASE("makeBlockMedia: image/iso/absent open a file; host + unknown fail closed")
{
    std::string const img = makeFlatImage("fbm_factory.img", 2048, 3, 0x07);
    REQUIRE(!img.empty());

    std::string err;
    // absent kind -> file
    auto a = makeBlockMedia("", img, 2048, /*readOnly=*/true, /*createBytes=*/0, err);
    CHECK(a != nullptr);
    CHECK(err.empty());
    CHECK(a->blockCount() == 3u);

    // explicit "iso" -> file
    err.clear();
    auto i = makeBlockMedia("iso", img, 2048, true, /*createBytes=*/0, err);
    CHECK(i != nullptr);

    // "host" -> not built yet (Phase B), no medium, clear diagnostic
    err.clear();
    auto h = makeBlockMedia("host", "host:0", 2048, true, /*createBytes=*/0, err);
    CHECK(h == nullptr);
    CHECK(err.find("Phase B") != std::string::npos);

    // unknown kind -> FAIL CLOSED (never silently a file)
    err.clear();
    auto u = makeBlockMedia("nonsense", img, 2048, true, /*createBytes=*/0, err);
    CHECK(u == nullptr);
    CHECK(!err.empty());

    // missing file for a file kind -> nullptr + error
    err.clear();
    auto miss = makeBlockMedia("image", "does_not_exist_factory.img", 2048, true, /*createBytes=*/0, err);
    CHECK(miss == nullptr);
    CHECK(!err.empty());

    std::remove(img.c_str());
}
