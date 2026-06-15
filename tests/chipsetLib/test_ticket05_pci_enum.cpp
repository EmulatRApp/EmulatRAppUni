// ============================================================================
// tests/chipsetLib/test_ticket05_pci_enum.cpp
//   Ticket 5 -- Pchip live CSRs + PCI config dispatch + Cypress ISA bridge
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
// Pins the Pchip's live CSR storage, Type-0 config dispatch (slot/func/reg
// decode, empty-slot 0xFFFFFFFF), and the Cypress CY82C693 ISA bridge
// identity at its slot, all through the chipset MMIO surface.  The bridge
// is wired via registerPciDevice in the test; production ownership/wiring
// (chipset vs Machine) is a separate platform-assembly decision.
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiPchip.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"
#include "chipsetLib/fixtures/FakePciDevice.h"
#include "chipsetLib/Cypress_CY82C693ISABridge.h"

#include <cstdint>

using namespace Tsunami21272;

TEST_CASE("Pchip WSBA0 holds its last write (live CSR)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t const pa = Base::kPchip0_CSR + Pchip::WSBA0;
    cs.mmioWrite(pa, 0x0000000012345678ULL, 8);
    CHECK(cs.mmioRead(pa, 8) == 0x0000000012345678ULL);
}

TEST_CASE("Empty PCI config slot returns 0xFFFFFFFF")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // Slot 4 is unpopulated.  NOTE: slot 5 is no longer empty -- the chipset
    // now wires the on-board Cypress ISA bridge there (authoritative PC264
    // BDF Bus0/Dev5/Func0 per apisrm pc264_io.c, Ticket 6 wireDevices()).
    uint64_t const pa = Base::kPchip0_CfgType0 + (4u << 11);   // slot 4, reg 0
    CHECK((cs.mmioRead(pa, 4) & 0xFFFFFFFFULL) == 0xFFFFFFFFULL);
}

TEST_CASE("Registered FakePciDevice answers config reads at its slot")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::FakePciDevice dev;
    dev.configRegs[0x00] = 0xBEEF1234u;
    cs.pchip().registerPciDevice(0, /*device*/ 7, /*func*/ 0, &dev);

    uint64_t const pa = Base::kPchip0_CfgType0 + (7u << 11) + 0x00;
    CHECK((cs.mmioRead(pa, 4) & 0xFFFFFFFFULL) == 0xBEEF1234u);
    CHECK(dev.configReads.size() >= 1);
}

TEST_CASE("Cypress ISA bridge identifies at its slot (vendor 0x1080 / dev 0xC693)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    Cy82C693IsaBridge bridge;
    cs.pchip().registerPciDevice(0, /*slot*/ 0, /*func*/ 0, &bridge);

    uint64_t const cfg = Base::kPchip0_CfgType0 + (uint64_t(0) << 11);
    uint32_t const vendev = static_cast<uint32_t>(cs.mmioRead(cfg + 0x00, 4));
    CHECK((vendev & 0xFFFF)         == 0x1080);   // Cypress Semiconductor
    CHECK(((vendev >> 16) & 0xFFFF) == 0xC693);   // CY82C693
}

TEST_CASE("Cypress ISA bridge reports the ISA-bridge class code 0x060100")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    Cy82C693IsaBridge bridge;
    cs.pchip().registerPciDevice(0, /*slot*/ 0, /*func*/ 0, &bridge);

    uint64_t const cfg = Base::kPchip0_CfgType0 + (uint64_t(0) << 11);
    uint32_t const classRev = static_cast<uint32_t>(cs.mmioRead(cfg + 0x08, 4));
    CHECK(((classRev >> 8) & 0xFFFFFF) == 0x060100);  // base/sub/progIF = ISA bridge
}
