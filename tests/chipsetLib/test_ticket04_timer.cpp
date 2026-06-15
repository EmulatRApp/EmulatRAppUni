// ============================================================================
// tests/chipsetLib/test_ticket04_timer.cpp
//   Ticket 4 -- interval timer: step() drives the Cchip b_irq<2> latch
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
// step() fires the Cchip interval timer (per-CPU b_irq<2> latch) on each
// crossing of a 2^kCchipTimerBit cycle boundary -- robust to any cycle
// delta, including a large chunk that steps over a boundary (the bug the
// prior exact-landing test had).  Rate is the profile-derived mask
// interval (CchipIntervalTimer.h / Tsunami21272_CsrSpec.h).
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_CsrSpec.h"
#include "chipsetLib/fixtures/CycleInjector.h"

#include <cstdint>

using namespace Tsunami21272;

static const uint64_t kInterval = uint64_t{1} << Spec::kCchipTimerBit;

TEST_CASE("step() fires the interval timer after one full interval")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    CHECK(cs.cchip().pendingIrq2(0) == false);
    cs.step(kInterval);
    CHECK(cs.cchip().pendingIrq2(0) == true);
}

TEST_CASE("step() short of an interval does not fire")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.step(kInterval - 1);
    CHECK(cs.cchip().pendingIrq2(0) == false);
}

TEST_CASE("step() fires when a single large delta crosses a boundary (no skip)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.step(kInterval - 5);          // just short -- no crossing
    CHECK(cs.cchip().pendingIrq2(0) == false);
    cs.step(10);                     // steps OVER the boundary mid-chunk
    CHECK(cs.cchip().pendingIrq2(0) == true);
}

TEST_CASE("step() fires again on the next interval after a clear")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.step(kInterval);
    CHECK(cs.cchip().pendingIrq2(0) == true);
    cs.cchip().clearPendingIrq2(0);
    CHECK(cs.cchip().pendingIrq2(0) == false);
    cs.step(kInterval);              // second interval boundary
    CHECK(cs.cchip().pendingIrq2(0) == true);
}

TEST_CASE("step() fires for all enabled CPUs (4-CPU ES45)")
{
    TsunamiChipset cs(ChipsetVariant::Typhoon, 4, 8ULL << 30);
    cs.step(kInterval);
    CHECK(cs.cchip().pendingIrq2(0) == true);
    CHECK(cs.cchip().pendingIrq2(1) == true);
    CHECK(cs.cchip().pendingIrq2(2) == true);
    CHECK(cs.cchip().pendingIrq2(3) == true);
}

TEST_CASE("CycleInjector reaches an interval via overshooting chunks")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::runCycles(cs, kInterval, 4096);
    CHECK(cs.cchip().pendingIrq2(0) == true);
}
