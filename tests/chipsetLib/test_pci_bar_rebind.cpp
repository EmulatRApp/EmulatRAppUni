// ============================================================================
// tests/chipsetLib/test_pci_bar_rebind.cpp
//   S1 seam (JRN-SCSI-002 G-B): dynamic decode -- unregister APIs + live
//   BAR re-program rebinding, plus the G-A bulk DMA helpers.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
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
// Acceptance for JRN-VMB-019 Track B item B1 / JRN-SCSI-001 S1:
//   1. unregisterIoPortRange / unregisterPciMemRange retire claims exactly
//      (stale-claim shadowing is the BAR-move failure mode).
//   2. A tulip-style BAR re-program through pciConfigWrite unregisters the
//      old window and registers the new one (unit, via captured callbacks).
//   3. The same flow against a REAL TsunamiChipset/Pchip moves live decode:
//      CSR reads route to the new base, the old base floats all-ones.
//   4. dmaReadBytes/dmaWriteBytes round-trip guest memory across a 4 KiB
//      chunk boundary (bulk bus-master seam over translateDmaToPa; windows
//      disabled -> identity mapping is the documented fallback).
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiPchip.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"
#include "chipsetLib/fixtures/FakeIoPortDevice.h"
#include "deviceLib/Tsunami/Dec21143Tulip.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace Tsunami21272;

// ----------------------------------------------------------------------------
// 1. Unregister APIs retire claims exactly.
// ----------------------------------------------------------------------------

TEST_CASE("unregisterIoPortRange retires the claim; dispatch reverts to float")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::FakeIoPortDevice dev;
    dev.responses[0x2004] = 0x5A;

    cs.pchip().registerIoPortRange(0x2000, 0x2080, &dev);
    uint64_t const pa = Base::kPchip0_IODense + 0x2004;
    CHECK(cs.mmioRead(pa, 1) == 0x5A);          // claim live
    CHECK(dev.reads.size() == 1);

    cs.pchip().unregisterIoPortRange(0x2000, 0x2080, &dev);
    CHECK(cs.mmioRead(pa, 1) == 0xFF);          // ISA float, handler NOT called
    CHECK(dev.reads.size() == 1);
}

TEST_CASE("unregisterPciMemRange retires the claim; dispatch reverts to all-ones")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::FakeIoPortDevice dev;
    dev.responses[0x10] = 0xCAFE;

    cs.pchip().registerPciMemRange(0x00400000, 0x00400080, &dev);
    uint64_t const pa = Base::kPchip0_PciMem + 0x00400010;
    CHECK((cs.mmioRead(pa, 2) & 0xFFFF) == 0xCAFE);   // rebased offset 0x10
    CHECK(dev.reads.size() == 1);

    cs.pchip().unregisterPciMemRange(0x00400000, 0x00400080, &dev);
    CHECK((cs.mmioRead(pa, 2) & 0xFFFF) == 0xFFFF);   // all-ones float
    CHECK(dev.reads.size() == 1);
}

TEST_CASE("unregister with a non-matching range is a logged no-op")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::FakeIoPortDevice dev;
    // Pchip io-port dispatch passes the RAW port (not rebased) to handlers.
    dev.responses[0x2100] = 0x77;

    cs.pchip().registerIoPortRange(0x2100, 0x2110, &dev);
    cs.pchip().unregisterIoPortRange(0x2100, 0x2118, &dev);   // wrong end -> MISS
    uint64_t const pa = Base::kPchip0_IODense + 0x2100;
    CHECK(cs.mmioRead(pa, 1) == 0x77);          // claim still live
}

// ----------------------------------------------------------------------------
// 2. Tulip-style BAR re-program (unit: captured callbacks).
// ----------------------------------------------------------------------------

namespace {
struct RangeEvent {
    bool     reg;      // true = register, false = unregister
    uint64_t base;
    uint32_t len;
    bool     isMem;
};
} // namespace

TEST_CASE("BAR re-program unregisters the old window and registers the new one")
{
    deviceLib::Dec21143Tulip nic;
    std::vector<RangeEvent> events;
    nic.setRangeCallbacks(
        [&](uint64_t b, uint32_t l, bool m, IIoPortHandler*) {
            events.push_back({true, b, l, m});
        },
        [&](uint64_t b, uint32_t l, bool m, IIoPortHandler*) {
            events.push_back({false, b, l, m});
        });

    // Size probe (all-ones) must NOT move decode.
    nic.pciConfigWrite(0x14, 0xFFFFFFFFu, 4);
    CHECK(events.empty());

    // First assignment: register only.
    nic.pciConfigWrite(0x14, 0x00100000u, 4);
    CHECK(events.size() == 1);
    CHECK(events[0].reg);
    CHECK(events[0].base == 0x00100000u);
    CHECK(events[0].isMem);

    // Re-assignment: unregister old, register new -- THE B1 acceptance.
    nic.pciConfigWrite(0x14, 0x00200000u, 4);
    CHECK(events.size() == 3);
    CHECK(!events[1].reg);
    CHECK(events[1].base == 0x00100000u);
    CHECK(events[2].reg);
    CHECK(events[2].base == 0x00200000u);

    // Same-value rewrite: no decode movement.
    nic.pciConfigWrite(0x14, 0x00200000u, 4);
    CHECK(events.size() == 3);
}

// ----------------------------------------------------------------------------
// 3. Integration: BAR re-program moves LIVE decode through a real Pchip.
// ----------------------------------------------------------------------------

TEST_CASE("BAR re-program moves live decode (chipset integration)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    deviceLib::Dec21143Tulip nic;
    nic.setRangeCallbacks(
        [&cs](uint64_t b, uint32_t l, bool m, IIoPortHandler* self) {
            if (m) cs.pchip().registerPciMemRange(b, b + l, self);
            else   cs.pchip().registerIoPortRange(
                       static_cast<uint16_t>(b), static_cast<uint16_t>(b + l), self);
        },
        [&cs](uint64_t b, uint32_t l, bool m, IIoPortHandler* self) {
            if (m) cs.pchip().unregisterPciMemRange(b, b + l, self);
            else   cs.pchip().unregisterIoPortRange(
                       static_cast<uint16_t>(b), static_cast<uint16_t>(b + l), self);
        });

    // Assign mem BAR1 at 0x00300000: CSR0 (bus-mode reset value 0xFE000000)
    // must be readable at the new base.
    nic.pciConfigWrite(0x14, 0x00300000u, 4);
    uint64_t const paA = Base::kPchip0_PciMem + 0x00300000;
    CHECK((cs.mmioRead(paA, 4) & 0xFFFFFFFFULL) == 0xFE000000ULL);

    // Move the BAR: old base floats, new base answers.
    nic.pciConfigWrite(0x14, 0x00500000u, 4);
    uint64_t const paB = Base::kPchip0_PciMem + 0x00500000;
    CHECK((cs.mmioRead(paA, 4) & 0xFFFFFFFFULL) == 0xFFFFFFFFULL);
    CHECK((cs.mmioRead(paB, 4) & 0xFFFFFFFFULL) == 0xFE000000ULL);
}

// ----------------------------------------------------------------------------
// 4. Bulk bus-master DMA helpers (G-A seam).
// ----------------------------------------------------------------------------

TEST_CASE("dmaWriteBytes/dmaReadBytes round-trip across a 4 KiB chunk boundary")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);

    // No DMA window enabled: translateDmaToPa's documented fallback is the
    // identity mapping (PCI addr used as raw PA), which suits a DRAM target.
    uint64_t const pci = 0x00123F80ULL;         // straddles ...3FFF / ...4000
    uint8_t src[0x100];
    for (unsigned i = 0; i < sizeof src; ++i)
        src[i] = static_cast<uint8_t>(i * 7 + 3);

    CHECK(cs.dmaWriteBytes(pci, src, sizeof src) == sizeof src);

    uint8_t dst[0x100];
    std::memset(dst, 0, sizeof dst);
    CHECK(cs.dmaReadBytes(pci, dst, sizeof dst) == sizeof dst);
    CHECK(std::memcmp(src, dst, sizeof dst) == 0);
}
