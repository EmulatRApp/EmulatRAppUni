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


// ============================================================================
// LockMonitor -- per-CPU LDx_L / STx_C reservation contract (SSOT).
// ============================================================================
// EXECUTABLE SPEC for the reservation table that backs the real CPU's LL/SC,
// ported from the schedLib::LockArbiter Phase-3 micro-tests now that the
// validated logic lives here in memoryLib (the memory boundary every writer
// crosses).  LockMonitor splits the arbiter's atomic storeCond into a pure
// query (check) plus an explicit mutation, so the tests model a store-
// conditional as: a SUCCESSFUL STx_C is a store, so it check()s then
// clearLine()s the line (consuming its own reservation AND cross-invalidating
// every other CPU on that line); a FAILED STx_C just clear()s the issuer.
// Granularity is the 64-byte EV6 cache line.

namespace {

// Model one store-conditional against the monitor.  Returns success.
bool storeCond(LockMonitor& lm, int cpu, uint64_t line)
{
    bool const ok = lm.check(cpu, line);
    if (ok) lm.clearLine(line);   // success stores -> break ALL reservations on line
    else    lm.clear(cpu);        // failure drops only the issuer's
    return ok;
}

} // namespace


TEST_CASE("LockMonitor -- LL then SC by the same CPU succeeds once")
{
    LockMonitor lm;
    constexpr uint64_t G = 0x100;

    lm.set(0, G);
    CHECK(storeCond(lm, 0, G) == true);    // reservation valid -> SC wins
    CHECK(storeCond(lm, 0, G) == false);   // consumed by the prior success
}

TEST_CASE("LockMonitor -- reservation is cache-line granular (64B)")
{
    LockMonitor lm;

    // LDx_L of one byte reserves the whole 64-byte line; an SC to a DIFFERENT
    // byte of the same line still matches.
    lm.set(0, 0x2000);
    CHECK(lm.check(0, 0x2008) == true);    // same line (0x2000..0x203F)
    CHECK(lm.check(0, 0x2040) == false);   // next line -- no match
}

TEST_CASE("LockMonitor -- a load does not clear another CPU's reservation")
{
    LockMonitor lm;
    constexpr uint64_t G = 0x4000;
    int const A = 0, B = 1;

    lm.set(A, G);   // CPU A reserves G
    lm.set(B, G);   // CPU B reserves G -- must NOT clear A's reservation

    // A stores-conditional first: succeeds; its store then breaks B's line.
    CHECK(storeCond(lm, A, G) == true);
    CHECK(storeCond(lm, B, G) == false);
}

TEST_CASE("LockMonitor -- exactly one STx_C wins a contended line")
{
    LockMonitor lm;
    constexpr uint64_t G = 0x4000;
    int const A = 0, B = 1;

    lm.set(A, G);
    lm.set(B, G);

    bool const aWon = storeCond(lm, A, G);
    bool const bWon = storeCond(lm, B, G);

    CHECK(aWon != bWon);   // exactly one winner: no double-acquire, no livelock
    CHECK(aWon == true);   // first-to-SC wins
}

TEST_CASE("LockMonitor -- a plain store breaks every OTHER CPU's reservation")
{
    LockMonitor lm;
    constexpr uint64_t G = 0x4000;
    int const A = 0, B = 1, C = 2;

    lm.set(A, G);
    lm.set(B, G);
    lm.set(C, G);

    // CPU B issues a PLAIN store to G: cross-invalidate everyone but B.
    lm.clearLine(G, /*exceptCpu*/ B);

    CHECK(lm.check(A, G) == false);   // A's reservation broken
    CHECK(lm.check(C, G) == false);   // C's reservation broken
    CHECK(lm.check(B, G) == true);    // B keeps its own (exceptCpu)
}

TEST_CASE("LockMonitor -- device/DMA store (exceptCpu=-1) breaks ALL reservations")
{
    LockMonitor lm;
    constexpr uint64_t G = 0x4000;

    lm.set(0, G);
    lm.set(1, G);

    lm.clearLine(G);   // default exceptCpu = -1: no CPU is spared

    CHECK(lm.check(0, G) == false);
    CHECK(lm.check(1, G) == false);
}
