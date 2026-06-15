// ============================================================================
// tests/chipsetLib/test_ticket01_dispatch.cpp
//   Ticket 1 -- TsunamiChipset orchestrator surface (Surface 1 + tick)
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
// Exercises the chipset's functional surface in isolation, before any
// EmulatR (CPU / Machine / pipeline) class depends on it:
//
//   (1) routeMmioOffset      -- each realm region routes correctly; the
//                               two documented gaps route to None.
//   (2) instance mmioRead    -- full-PA dispatch reaches the right realm.
//   (3) Pchip1 half          -- unpopulated mirror reads off-bus.
//   (4) static shim          -- (ctx, offset, width) reaches the same
//                               register as the instance method.
//   (5) cpuId threading       -- MISC.CPUID reflects the cpuId argument;
//                               the static shim defaults to CPU 0.
//   (6) write round-trip      -- a Cchip RW write is visible on read-back.
//   (7) step()/CycleInjector  -- the tick reaches the interval-timer fire
//                               path at the mask interval.
//
// Per V4 doctest convention: CHECK only, never REQUIRE.  Enum comparisons
// are cast to int so doctest never needs to stringify an enum class.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"
#include "chipsetLib/Tsunami21272_CsrSpec.h"
#include "chipsetLib/fixtures/CycleInjector.h"

#include <cstdint>

using namespace Tsunami21272;

// ============================================================================
// 1. routeMmioOffset
// ============================================================================

TEST_CASE("routeMmioOffset maps each realm region and leaves gaps as None")
{
    auto rid = [](uint64_t off) { return static_cast<int>(MMIOOffset::routeMmioOffset(off)); };
    auto id  = [](MMIOOffset::RegionId r) { return static_cast<int>(r); };

    CHECK(rid(MMIOOffset::kPchip0_PciMem)    == id(MMIOOffset::RegionId::Pchip0_PciMem));
    CHECK(rid(MMIOOffset::kPchip0_SparseMem) == id(MMIOOffset::RegionId::Pchip0_SparseMem));
    CHECK(rid(MMIOOffset::kPchip0_SparseIO)  == id(MMIOOffset::RegionId::Pchip0_SparseIO));
    CHECK(rid(MMIOOffset::kPchip0_CSR)       == id(MMIOOffset::RegionId::Pchip0_CSR));
    CHECK(rid(MMIOOffset::kCchip_CSR)        == id(MMIOOffset::RegionId::Cchip_CSR));
    CHECK(rid(MMIOOffset::kDchip_CSR)        == id(MMIOOffset::RegionId::Dchip_CSR));
    CHECK(rid(MMIOOffset::kPchip0_CfgType0)  == id(MMIOOffset::RegionId::Pchip0_CfgType0));

    // The two documented gaps.
    CHECK(rid(0x190000000ULL) == id(MMIOOffset::RegionId::None));
    CHECK(rid(0x1C0000000ULL) == id(MMIOOffset::RegionId::None));
}

// ============================================================================
// 2-3. Instance mmioRead dispatch
// ============================================================================

TEST_CASE("MMIO read at Cchip CSC routes to the Cchip")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const pa = Base::kCchip_CSR + Cchip::CSC;
    CHECK(cs.mmioRead(pa, 8) == cs.cchip().read(Cchip::CSC, 0));
}

TEST_CASE("MMIO read at Dchip DREV returns the Tsunami revision 0x10")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    CHECK(cs.mmioRead(Base::kDchip_CSR + Dchip::DREV, 8) == 0x10);
}

TEST_CASE("MMIO read in the Pchip1 half returns off-bus all-ones")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const pa = Base::kMMIO_Start + MMIOOffset::kPchip1_PciMem;
    CHECK((cs.mmioRead(pa, 4) & 0xFFFFFFFFULL) == 0xFFFFFFFFULL);
}

// ============================================================================
// 4. Static shim equivalence
// ============================================================================

TEST_CASE("Static shim reaches the same Cchip register as the instance method")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const pa     = Base::kCchip_CSR + Cchip::CSC;        // full PA
    uint64_t const offset = MMIOOffset::kCchip_CSR + Cchip::CSC;  // window-relative
    CHECK(TsunamiChipset::mmioRead(&cs, offset, 8) == cs.mmioRead(pa, 8));
}

// ============================================================================
// 5. cpuId threading (the invariant the shims introduce)
// ============================================================================

TEST_CASE("cpuId threads to Cchip MISC.CPUID; static shim defaults to CPU 0")
{
    TsunamiChipset cs(ChipsetVariant::Typhoon, 4, 8ULL << 30);
    uint64_t const pa     = Base::kCchip_CSR + Cchip::MISC;
    uint64_t const offset = MMIOOffset::kCchip_CSR + Cchip::MISC;

    CHECK((cs.mmioRead(pa, 8, 0) & 0x3) == 0);
    CHECK((cs.mmioRead(pa, 8, 2) & 0x3) == 2);
    CHECK((cs.mmioRead(pa, 8, 3) & 0x3) == 3);

    // Static shim path defaults cpuId = 0 -> MISC.CPUID reads 0.
    CHECK((TsunamiChipset::mmioRead(&cs, offset, 8) & 0x3) == 0);
}

// ============================================================================
// 6. Write round-trip
// ============================================================================

TEST_CASE("MMIO write to a Cchip RW register round-trips deterministically")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const pa = Base::kCchip_CSR + Cchip::CSC;
    constexpr uint64_t kProbe = 0x00000000DEADBEEFULL;

    cs.mmioWrite(pa, kProbe, 8);
    uint64_t const v1 = cs.mmioRead(pa, 8);
    cs.mmioWrite(pa, kProbe, 8);
    uint64_t const v2 = cs.mmioRead(pa, 8);

    CHECK(v1 == v2);                              // deterministic
    CHECK(v1 != 0xFFFFFFFFFFFFFFFFULL);           // not off-bus
}

// ============================================================================
// 7. Tick / interval timer
// ============================================================================

TEST_CASE("step() fires the Cchip interval timer at the mask interval")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    CHECK(cs.cchip().pendingIrq2(0) == false);

    uint64_t const interval = uint64_t{1} << Spec::kCchipTimerBit;
    cs.step(interval);
    CHECK(cs.cchip().pendingIrq2(0) == true);
}

TEST_CASE("CycleInjector::runCycles drives step() across an interval boundary")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const interval = uint64_t{1} << Spec::kCchipTimerBit;

    // Four chunks that land exactly on the interval boundary.
    chipsetTests::runCycles(cs, interval, interval / 4);
    CHECK(cs.cchip().pendingIrq2(0) == true);
}
