// ============================================================================
// tests/memoryLib/test_guestmemory.cpp -- doctest cases for GuestMemory
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

#include "doctest.h"

#include "memoryLib/GuestMemory.h"

#include <cstdint>
#include <ostream>

using namespace memoryLib;


TEST_CASE("GuestMemory -- default size is 64 MiB")
{
    GuestMemory mem;
    CHECK(mem.sizeBytes() == 64ULL * 1024ULL * 1024ULL);
}

TEST_CASE("GuestMemory -- zero-initialised on construction")
{
    GuestMemory mem(4096);

    uint64_t v = 0xDEAD;
    CHECK(mem.read8(0, v) == MemStatus::Ok);
    CHECK(v == 0u);
}


TEST_CASE("GuestMemory -- byte round-trip")
{
    GuestMemory mem(4096);
    CHECK(mem.write1(0x100, 0xAB) == MemStatus::Ok);

    uint8_t v = 0;
    CHECK(mem.read1(0x100, v) == MemStatus::Ok);
    CHECK(v == 0xAB);
}

TEST_CASE("GuestMemory -- word round-trip; little-endian byte layout")
{
    GuestMemory mem(4096);
    CHECK(mem.write2(0x200, 0x1234) == MemStatus::Ok);

    uint16_t w = 0;
    CHECK(mem.read2(0x200, w) == MemStatus::Ok);
    CHECK(w == 0x1234);

    // Confirm little-endian byte order at the storage level.
    uint8_t lo = 0, hi = 0;
    CHECK(mem.read1(0x200, lo) == MemStatus::Ok);
    CHECK(mem.read1(0x201, hi) == MemStatus::Ok);
    CHECK(lo == 0x34);
    CHECK(hi == 0x12);
}

TEST_CASE("GuestMemory -- longword round-trip")
{
    GuestMemory mem(4096);
    CHECK(mem.write4(0x400, 0xCAFEBABEu) == MemStatus::Ok);

    uint32_t lw = 0;
    CHECK(mem.read4(0x400, lw) == MemStatus::Ok);
    CHECK(lw == 0xCAFEBABEu);
}

TEST_CASE("GuestMemory -- quadword round-trip")
{
    GuestMemory mem(4096);
    CHECK(mem.write8(0x800, 0xDEADBEEFCAFEBABEull) == MemStatus::Ok);

    uint64_t q = 0;
    CHECK(mem.read8(0x800, q) == MemStatus::Ok);
    CHECK(q == 0xDEADBEEFCAFEBABEull);
}


TEST_CASE("GuestMemory is a dumb byte-store -- no byte-precise bound (the bus enforces DRAM range)")
{
    // Post-amputation, GuestMemory does NOT enforce the logical byte size; the
    // arbiter (TsunamiChipset::isDramAddress / sizeBytes) gates the address
    // before ever calling here -- see tests/chipsetLib/test_systembus_arbiter.cpp.
    // The only structural limit left is the page-array bound.
    GuestMemory mem(4096);   // rounds up to one 64 KiB backing page

    // Past the logical 4096-byte size but inside the backing page: just works.
    CHECK(mem.write1(4096, 0xFF) == MemStatus::Ok);
    uint8_t v = 0;
    CHECK(mem.read1(4096, v) == MemStatus::Ok);
    CHECK(v == 0xFF);

    // Beyond the page array (pidx >= pageCount): reads zero-fill (Ok), writes
    // cannot allocate (OutOfRange).
    uint8_t v2 = 0;
    CHECK(mem.read1(0x10000, v2) == MemStatus::Ok);
    CHECK(v2 == 0);
    CHECK(mem.write1(0x10000, 0xAB) == MemStatus::OutOfRange);
}


// ----------------------------------------------------------------------------
// Misaligned access -- GuestMemory delegates alignment policy upstream.
// ----------------------------------------------------------------------------
// Per the GuestMemory.h contract (and confirmed in the V2 sparse-paged
// rewrite 2026-05-14), alignment is enforced by the translator under
// CpuState::unalignTrapEnabled, not by GuestMemory itself.  A
// misaligned-but-in-range PA that reaches GuestMemory is treated as a
// normal access; std::memcpy is alignment-agnostic on x64 so the read
// or write completes at the requested byte offset.
//
// The three earlier TEST_CASEs at this position (predating the
// translator-owns-alignment design) asserted OutOfRange for misaligned
// access; they were behaviorally wrong against both the prior flat
// backing and the current sparse one.  Collapsed here into a single
// case that documents the actual contract so a future reader does not
// re-introduce the mismatch.  Alignment enforcement, when needed, is
// exercised in the translator/MMU test layer.
TEST_CASE("GuestMemory -- misaligned access at in-range PA returns Ok "
          "(alignment delegated to translator)")
{
    GuestMemory mem(4096);

    // 16-bit access at byte-aligned offset 0x101 -- one byte off the
    // natural 2-byte boundary.  No-op against the design contract;
    // GuestMemory completes the access without complaint.
    uint16_t w = 0;
    CHECK(mem.read2(0x101, w)             == MemStatus::Ok);
    CHECK(mem.write2(0x101, 0x1234)       == MemStatus::Ok);

    // 32-bit access at byte-aligned offsets 0x102 / 0x103 -- both off
    // the natural 4-byte boundary.
    uint32_t lw = 0;
    CHECK(mem.read4(0x102, lw)            == MemStatus::Ok);
    CHECK(mem.write4(0x103, 0xDEADBEEFu)  == MemStatus::Ok);

    // 64-bit access at byte-aligned offsets 0x104 / 0x107 -- both off
    // the natural 8-byte boundary.
    uint64_t q = 0;
    CHECK(mem.read8(0x104, q)             == MemStatus::Ok);
    CHECK(mem.write8(0x107, 0x1ull)       == MemStatus::Ok);
}


TEST_CASE("GuestMemory -- writes are independent across non-overlapping ranges")
{
    GuestMemory mem(4096);
    CHECK(mem.write8(0x000, 0x1111111111111111ull) == MemStatus::Ok);
    CHECK(mem.write8(0x008, 0x2222222222222222ull) == MemStatus::Ok);
    CHECK(mem.write8(0x010, 0x3333333333333333ull) == MemStatus::Ok);

    uint64_t a = 0, b = 0, c = 0;
    CHECK(mem.read8(0x000, a) == MemStatus::Ok);
    CHECK(mem.read8(0x008, b) == MemStatus::Ok);
    CHECK(mem.read8(0x010, c) == MemStatus::Ok);
    CHECK(a == 0x1111111111111111ull);
    CHECK(b == 0x2222222222222222ull);
    CHECK(c == 0x3333333333333333ull);
}
