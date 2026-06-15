// ============================================================================
// tests/chipsetLib/test_ide_wiring.cpp -- CY82C693 IDE func1 chipset routing
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
// S4 of the IDE/ATAPI scaffold.  The device itself is covered by
// test_cy82c693ide.cpp; this validates the PRODUCTION wiring in
// TsunamiChipset::wireDevices(): func1 PCI config enumerates, and the legacy
// taskfile ports route THROUGH the chipset to the controller (with the no-media
// ATAPI CD attached).  Catches a mis-registered BDF / port range OR a
// registry-vs-Cypress-catch-all precedence problem -- without a cold boot.
// CHECK only.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"

#include <cstdint>

using namespace Tsunami21272;

namespace {

uint64_t sparseIoPA(uint16_t port) noexcept
{
    uint64_t const sparseOff =
        (static_cast<uint64_t>(port) << SparseSpace::kPciAddrShift)
        | (SparseSpace::kXferByte & SparseSpace::kXferLenMask);
    return Base::kMMIO_Start + MMIOOffset::kPchip0_SparseIO + sparseOff;
}

uint64_t cfgPA(unsigned dev, unsigned func, unsigned reg) noexcept
{
    return Base::kPchip0_CfgType0
         + (static_cast<uint64_t>(dev)  << 11)
         + (static_cast<uint64_t>(func) << 8)
         + reg;
}

} // namespace

TEST_CASE("IDE wiring: CY82C693 IDE enumerates at bus0/dev5/func1")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint32_t const id = static_cast<uint32_t>(cs.mmioRead(cfgPA(5, 1, 0x00), 4) & 0xFFFFFFFFu);
    CHECK((id & 0xFFFFu) == 0x1080);             // vendor: Cypress
    CHECK(((id >> 16) & 0xFFFFu) == 0xC693);     // device: _PROVISIONAL
    // class code at 0x08: [rev, prog-IF, subclass, base-class] little-endian.
    uint32_t const cls = static_cast<uint32_t>(cs.mmioRead(cfgPA(5, 1, 0x08), 4) & 0xFFFFFFFFu);
    CHECK(((cls >> 16) & 0xFFu) == 0x01);        // subclass: IDE      (reg 0x0A)
    CHECK(((cls >> 24) & 0xFFu) == 0x01);        // base class: mass storage (reg 0x0B)
}

// 2026-06-11: dqa0 (primary master) is now the bootable ATA disk, attached at
// runtime by Machine from [Storage] diskDir; the ATAPI CD moved to primary
// SLAVE (dqa1).  A bare chipset (no Machine, no image) therefore has master
// absent and the CD on the slave -- these cases select the slave accordingly.
TEST_CASE("IDE wiring: legacy taskfile routes to the controller (CD on slave)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.mmioWrite(sparseIoPA(0x1F6), 0x10, 1);                       // select slave (dqa1)
    uint8_t const st = static_cast<uint8_t>(cs.mmioRead(sparseIoPA(0x1F7), 1) & 0xFFu);
    CHECK((st & 0x40) != 0);    // DRDY: ATAPI CD attached + I/O routed to m_ide
    CHECK((st & 0x80) == 0);    // BSY clear
    CHECK(st != 0xFF);          // not float / catch-all shadow

    cs.mmioWrite(sparseIoPA(0x1F6), 0x00, 1);                       // select master (empty)
    uint8_t const stm = static_cast<uint8_t>(cs.mmioRead(sparseIoPA(0x1F7), 1) & 0xFFu);
    CHECK(stm == 0x00);         // C1: no disk image in a bare chipset, not 0xFF
}

TEST_CASE("IDE wiring: IDENTIFY DEVICE through the chipset posts the ATAPI signature")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.mmioWrite(sparseIoPA(0x1F6), 0x10, 1);                       // select slave (dqa1 CD)
    cs.mmioWrite(sparseIoPA(0x1F7), 0xEC, 1);                       // IDENTIFY DEVICE
    CHECK((cs.mmioRead(sparseIoPA(0x1F7), 1) & 0x01) != 0);         // ERR (aborted)
    CHECK((cs.mmioRead(sparseIoPA(0x1F4), 1) & 0xFFu) == 0x14);     // ATAPI sig lo
    CHECK((cs.mmioRead(sparseIoPA(0x1F5), 1) & 0xFFu) == 0xEB);     // ATAPI sig hi
}
