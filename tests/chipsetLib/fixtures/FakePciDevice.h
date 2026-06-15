// ============================================================================
// tests/chipsetLib/fixtures/FakePciDevice.h
//   Reusable test fixture -- programmable IPciDeviceHandler stand-in
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
// A fake PCI device that implements the real IPciDeviceHandler interface
// from chipsetLib/TsunamiPchip.h.  Config registers are programmable via
// the configRegs map; every read and write is recorded so a test can
// assert exactly which (reg, width) accesses the dispatch path produced.
// wasReset supports SPRST / bus-reset testing (Ticket 5 / Surface 20).
//
// Used from Ticket 5 onward; landed in Ticket 0 so its API is stable.
// ============================================================================

#ifndef CHIPSET_TESTS_FIXTURES_FAKEPCIDEVICE_H
#define CHIPSET_TESTS_FIXTURES_FAKEPCIDEVICE_H

#include "chipsetLib/TsunamiPchip.h"   // IPciDeviceHandler

#include <cstdint>
#include <map>
#include <vector>

namespace chipsetTests {

struct FakePciDevice : IPciDeviceHandler
{
    struct ConfigAccess
    {
        uint8_t  reg;
        uint8_t  width;
        uint32_t value;
        bool     isWrite;
    };

    // Programmable config space: reg offset -> value.
    std::map<uint8_t, uint32_t> configRegs;

    // Access journals -- one entry per dispatched read / write.
    std::vector<ConfigAccess>   configReads;
    std::vector<ConfigAccess>   configWrites;

    // Set by reset(); lets a test confirm a bus reset reached the device.
    bool wasReset = false;

    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override
    {
        uint32_t v = 0;
        auto const it = configRegs.find(reg);
        if (it != configRegs.end())
            v = it->second;

        uint32_t const widthMask =
            (width >= 4) ? 0xFFFFFFFFu
                         : ((static_cast<uint32_t>(1) << (width * 8)) - 1u);
        v &= widthMask;

        configReads.push_back(ConfigAccess{reg, width, v, false});
        return v;
    }

    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override
    {
        configRegs[reg] = value;
        configWrites.push_back(ConfigAccess{reg, width, value, true});
    }

    void reset() noexcept { wasReset = true; }
};

} // namespace chipsetTests

#endif // CHIPSET_TESTS_FIXTURES_FAKEPCIDEVICE_H
