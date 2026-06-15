// ============================================================================
// tests/systemLib/test_firmwareloader.cpp -- doctest cases for FirmwareLoader
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// Cases:
//   - Load a tiny binary file at PA 0; bytes match buffer; startPc /
//     palMode line up with PAL-bit convention.
//   - PAL-bit set on startPa -> palMode true, startPc has low bit
//     cleared.
//   - File not found -> ok = false, error populated.
//   - Image too large for memory -> ok = false, error populated.
//
// ============================================================================

#include "doctest.h"

#include "memoryLib/GuestMemory.h"
#include "systemLib/FirmwareLoader.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>


using memoryLib::GuestMemory;
using memoryLib::MemStatus;
using systemLib::loadRawBinary;
using systemLib::LoadResult;


namespace {

// Write a small payload to a temp file and return the path.  Caller
// is responsible for filesystem cleanup -- doctest does not provide a
// per-test fixture, so each test removes its own temp file at the
// end.
std::filesystem::path makeTempBinary(std::string const& tag,
                                     std::vector<uint8_t> const& bytes)
{
    auto path = std::filesystem::temp_directory_path() /
                ("emulatr_test_" + tag + ".bin");
    std::ofstream out{path, std::ios::binary};
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path;
}

} // anonymous namespace


// =============================================================================
// Happy path
// =============================================================================

TEST_CASE("FirmwareLoader -- load 8 bytes at PA 0; contents match")
{
    GuestMemory mem{4096};

    std::vector<uint8_t> const payload = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04
    };
    auto const path = makeTempBinary("happy", payload);

    LoadResult const r = loadRawBinary(mem, path, 0x0, 0x100);

    CHECK(r.ok);
    CHECK(r.error.empty());
    CHECK(r.bytesLoaded == 8u);
    CHECK(r.startPc == 0x100u);
    CHECK_FALSE(r.palMode);

    for (uint64_t i = 0; i < payload.size(); ++i) {
        uint8_t got = 0;
        CHECK(mem.read1(i, got) == MemStatus::Ok);
        // Widen to int to dodge an MSVC + doctest 2.5.0 stringify path
        // that does pointer + pointer arithmetic on unsigned char pairs
        // when an assertion fails.
        CHECK(static_cast<int>(got) == static_cast<int>(payload[i]));
    }

    std::filesystem::remove(path);
}


// =============================================================================
// PAL-bit handling
// =============================================================================

TEST_CASE("FirmwareLoader -- PAL bit on startPa sets palMode")
{
    GuestMemory mem{4096};
    auto const path = makeTempBinary("palbit", {0x55, 0xAA});

    // startPa = 0x8001 -> startPc = 0x8000, palMode = true
    LoadResult const r = loadRawBinary(mem, path, 0x0, 0x8001);

    CHECK(r.ok);
    CHECK(r.startPc == 0x8000u);
    CHECK(r.palMode);

    std::filesystem::remove(path);
}


TEST_CASE("FirmwareLoader -- low bit clear means palMode false")
{
    GuestMemory mem{4096};
    auto const path = makeTempBinary("nopalbit", {0x11, 0x22});

    LoadResult const r = loadRawBinary(mem, path, 0x0, 0x8000);

    CHECK(r.ok);
    CHECK(r.startPc == 0x8000u);
    CHECK_FALSE(r.palMode);

    std::filesystem::remove(path);
}


// =============================================================================
// Failure modes
// =============================================================================

TEST_CASE("FirmwareLoader -- file not found")
{
    GuestMemory mem{4096};
    auto const path = std::filesystem::temp_directory_path() /
                      "emulatr_does_not_exist.bin";
    // Belt and braces: ensure it really does not exist.
    std::filesystem::remove(path);

    LoadResult const r = loadRawBinary(mem, path, 0x0, 0x0);

    CHECK_FALSE(r.ok);
    // r.error is non-empty and contains "not found".  Compare via the
    // emptiness check + a presence test that does not bring static
    // npos into the doctest stringify path.
    CHECK_FALSE(r.error.empty());
    CHECK(r.error.find("not found") < r.error.size());
    CHECK(r.bytesLoaded == 0u);
}


TEST_CASE("FirmwareLoader -- image larger than guest memory")
{
    // 256-byte memory, 300-byte payload -> reject.
    GuestMemory mem{256};
    std::vector<uint8_t> payload(300, 0xAB);
    auto const path = makeTempBinary("toolarge", payload);

    LoadResult const r = loadRawBinary(mem, path, 0x0, 0x0);

    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(r.error.find("exceeds") < r.error.size());

    std::filesystem::remove(path);
}


TEST_CASE("FirmwareLoader -- non-zero loadPa places image at offset")
{
    GuestMemory mem{4096};
    std::vector<uint8_t> const payload = {0xC0, 0xDE};
    auto const path = makeTempBinary("offset", payload);

    LoadResult const r = loadRawBinary(mem, path, 0x100, 0x100);
    CHECK(r.ok);

    // Bytes should land at PA 0x100, 0x101 -- not at PA 0.
    uint8_t got = 0;
    CHECK(mem.read1(0x100, got) == MemStatus::Ok);
    CHECK(got == 0xC0);
    CHECK(mem.read1(0x101, got) == MemStatus::Ok);
    CHECK(got == 0xDE);

    // PA 0 should still be zero (no overflow into low memory).
    CHECK(mem.read1(0x0, got) == MemStatus::Ok);
    CHECK(got == 0x00);

    std::filesystem::remove(path);
}
