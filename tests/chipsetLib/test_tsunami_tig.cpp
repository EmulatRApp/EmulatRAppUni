// ============================================================================
// tests/chipsetLib/test_tsunami_tig.cpp
//   TsunamiTig -- per-CPU CPU-START register latch (secondary-CPU bring-up)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// VERIFIED secondary-start mechanism (apisrm pc264.c start_secondary): the
// console stages the entry address in PAL$CPU0_START_BASE[id], then writes
// TIG 0xC00028+id (-> kIpcr0..3 PAs) to kick CPU id.  These tests pin the
// CPU-START latch + query/consume API and the byte-identical UP-boot posture
// (no write -> reset state preserved).  They also CONSUME coreLib/
// Ev6Pc264PalDefs.h (cpuStartSlotOffset), tying the latched id to the impure
// entry slot the started secondary loads.
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiTig.h"
#include "coreLib/Ev6Pc264PalDefs.h"

#include <cstdint>

namespace {

// CPU-START register PAs (xtig: PA = 0x801_0000_0000 + (0xC00028+id << 6)).
constexpr uint64_t kCpuStartReg0 = 0x80130000A00ULL;  // outtig 0xC00028
constexpr uint64_t kCpuStartReg1 = 0x80130000A40ULL;  // outtig 0xC00029
constexpr uint64_t kCpuStartReg2 = 0x80130000A80ULL;  // outtig 0xC0002A
constexpr uint64_t kCpuStartReg3 = 0x80130000AC0ULL;  // outtig 0xC0002B

} // namespace

TEST_CASE("TsunamiTig -- fresh TIG holds no CPU-start request (UP-boot posture)")
{
    TsunamiTig tig;
    tig.reset();

    CHECK(tig.isAtResetState());
    CHECK(tig.pendingCpuStartMask() == 0u);
    CHECK_FALSE(tig.cpuStartRequested(0));
    CHECK_FALSE(tig.cpuStartRequested(1));
    CHECK_FALSE(tig.cpuStartRequested(2));
    CHECK_FALSE(tig.cpuStartRequested(3));
}

TEST_CASE("TsunamiTig -- CPU-START write latches a per-CPU start request")
{
    TsunamiTig tig;
    tig.reset();

    // start_secondary(1) writes value 0 to the CPU1 start reg (the WRITE is
    // the kick, not the value).
    tig.write(kCpuStartReg1, 0);

    CHECK(tig.cpuStartRequested(1));
    CHECK_FALSE(tig.cpuStartRequested(0));
    CHECK(tig.pendingCpuStartMask() == 0x2u);

    // A start write makes the snapshot-deferral guard report non-reset, so the
    // TIG would correctly demand serialization once SMP is live.
    CHECK_FALSE(tig.isAtResetState());

    // Consume/clear (as the secondary bring-up will after it releases CPU1).
    tig.clearCpuStartRequest(1);
    CHECK_FALSE(tig.cpuStartRequested(1));
    CHECK(tig.pendingCpuStartMask() == 0u);
    CHECK(tig.isAtResetState());
}

TEST_CASE("TsunamiTig -- each CPU-START register maps to its own id bit")
{
    TsunamiTig tig;
    tig.reset();

    tig.write(kCpuStartReg0, 0);
    tig.write(kCpuStartReg2, 0);
    tig.write(kCpuStartReg3, 0);

    CHECK(tig.cpuStartRequested(0));
    CHECK_FALSE(tig.cpuStartRequested(1));
    CHECK(tig.cpuStartRequested(2));
    CHECK(tig.cpuStartRequested(3));
    CHECK(tig.pendingCpuStartMask() == 0xDu);

    // Out-of-range ids are rejected, never index out of bounds.
    CHECK_FALSE(tig.cpuStartRequested(-1));
    CHECK_FALSE(tig.cpuStartRequested(4));
}

TEST_CASE("TsunamiTig -- latched id maps to the PAL impure entry slot")
{
    // The started secondary loads PAL$CPU0_START_BASE[id] = base + id*8.
    using namespace coreLib::ev6::pc264;
    CHECK(cpuStartSlotOffset(0) == kCpu0StartBase);
    CHECK(cpuStartSlotOffset(1) == kCpu1StartBase);
    CHECK(cpuStartSlotOffset(1) == kCpu0StartBase + kCpuStartStride);
    CHECK(cpuStartSlotOffset(3) == kCpu0StartBase + 3 * kCpuStartStride);
}
