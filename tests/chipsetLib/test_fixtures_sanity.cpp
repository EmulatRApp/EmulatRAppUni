// ============================================================================
// tests/chipsetLib/test_fixtures_sanity.cpp
//   Ticket 0 -- sanity exercises for the reusable chipset test fixtures
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
// Proves each Ticket 0 fixture compiles and its API behaves as its header
// claims.  These are not chipset tests -- they pin the fixtures the later
// tickets depend on, so a fixture change that breaks the contract fails
// here rather than deep inside a Ticket 7 / 9 suite.
//
// Per V4 doctest convention: CHECK only, never REQUIRE (exceptions are
// disabled in the build; REQUIRE fails at compile time).
// ============================================================================

#include "doctest.h"

#include "chipsetLib/fixtures/FakePciDevice.h"
#include "chipsetLib/fixtures/FakeIoPortDevice.h"
#include "chipsetLib/fixtures/FakeCpu.h"
#include "chipsetLib/fixtures/DeterministicMemory.h"

#include <cstdint>

using namespace chipsetTests;

// ============================================================================
// FakePciDevice
// ============================================================================

TEST_CASE("FakePciDevice returns programmed config and records the read")
{
    FakePciDevice dev;
    dev.configRegs[0x00] = 0xC6931080u;   // Cypress vendor:device, LE order

    CHECK(dev.pciConfigRead(0x00, 4) == 0xC6931080u);
    CHECK(dev.configReads.size() == 1);
    CHECK(dev.configReads[0].reg == 0x00);
    CHECK(dev.configReads[0].width == 4);
}

TEST_CASE("FakePciDevice masks a narrow read to its width")
{
    FakePciDevice dev;
    dev.configRegs[0x00] = 0xC6931080u;

    // 16-bit read of the vendor ID returns only the low halfword.
    CHECK(dev.pciConfigRead(0x00, 2) == 0x1080u);
}

TEST_CASE("FakePciDevice records writes and reset")
{
    FakePciDevice dev;
    dev.pciConfigWrite(0x04, 0x0007u, 2);

    CHECK(dev.configWrites.size() == 1);
    CHECK(dev.configWrites[0].reg == 0x04);
    CHECK(dev.configRegs[0x04] == 0x0007u);

    CHECK(dev.wasReset == false);
    dev.reset();
    CHECK(dev.wasReset == true);
}

// ============================================================================
// FakeIoPortDevice
// ============================================================================

TEST_CASE("FakeIoPortDevice returns programmed port value and records the read")
{
    FakeIoPortDevice io;
    io.responses[0x3FD] = 0x60;   // UART LSR: THRE + TEMT

    CHECK(io.ioRead(0x3FD, 1) == 0x60);
    CHECK(io.reads.size() == 1);
    CHECK(io.reads[0].port == 0x3FD);
}

TEST_CASE("FakeIoPortDevice records the written byte and port")
{
    FakeIoPortDevice io;
    io.ioWrite(0x3F8, 'A', 1);

    CHECK(io.writes.size() == 1);
    CHECK(io.writes[0].port == 0x3F8);
    CHECK(io.writes[0].value == static_cast<uint64_t>('A'));
}

// ============================================================================
// FakeCpu
// ============================================================================

TEST_CASE("FakeCpu carries its id")
{
    FakeCpu cpu(2);
    CHECK(cpu.cpuId == 2);
    CHECK(cpu.lock_flag == false);
}

TEST_CASE("FakeCpu reservation survives a write to a different cache line")
{
    FakeCpu cpu(0);
    cpu.takeReservation(0x10000);
    cpu.snoopWrite(0x20000, 8);     // different 64-byte line
    CHECK(cpu.lock_flag == true);
}

TEST_CASE("FakeCpu reservation clears on a write to the same cache line")
{
    FakeCpu cpu(0);
    cpu.takeReservation(0x10000);
    cpu.snoopWrite(0x10020, 8);     // same 64-byte line as 0x10000
    CHECK(cpu.lock_flag == false);
}

// ============================================================================
// DeterministicMemory
// ============================================================================

TEST_CASE("DeterministicMemory round-trips a quadword")
{
    DeterministicMemory mem(1u << 20);
    uint64_t const v = 0xDEADBEEFCAFEBABEULL;
    mem.write(0x1000, &v, 8);

    uint64_t r = 0;
    mem.read(0x1000, &r, 8);
    CHECK(r == v);
}

TEST_CASE("DeterministicMemory ignores an out-of-range access")
{
    DeterministicMemory mem(0x100);
    uint64_t r = 0x1234;
    mem.read(0x1000, &r, 8);    // past the end -- left untouched
    CHECK(r == 0x1234);
}
