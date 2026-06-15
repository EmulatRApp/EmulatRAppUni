// ============================================================================
// tests/chipsetLib/fixtures/FakeIoPortDevice.h
//   Reusable test fixture -- programmable IIoPortHandler stand-in
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
// A fake ISA / PC I/O-port device implementing the real IIoPortHandler
// interface from chipsetLib/TsunamiPchip.h.  Read responses are
// programmable per port via the responses map; every read and write is
// recorded with its port, width, and value so a test can assert the
// sparse-I/O decode path delivered the access to the right port.
//
// Used from Ticket 6 onward (ISA I/O dispatch / UART hookup); landed in
// Ticket 0 so its API is stable.
// ============================================================================

#ifndef CHIPSET_TESTS_FIXTURES_FAKEIOPORTDEVICE_H
#define CHIPSET_TESTS_FIXTURES_FAKEIOPORTDEVICE_H

#include "chipsetLib/TsunamiPchip.h"   // IIoPortHandler

#include <cstdint>
#include <map>
#include <vector>

namespace chipsetTests {

struct FakeIoPortDevice : IIoPortHandler
{
    struct IoAccess
    {
        uint16_t port;
        uint8_t  width;
        uint64_t value;
    };

    // Programmable read responses: port -> value.
    std::map<uint16_t, uint64_t> responses;

    // Access journals -- one entry per dispatched read / write.
    std::vector<IoAccess>        reads;
    std::vector<IoAccess>        writes;

    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        uint64_t v = 0;
        auto const it = responses.find(port);
        if (it != responses.end())
            v = it->second;

        reads.push_back(IoAccess{port, width, v});
        return v;
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        writes.push_back(IoAccess{port, width, value});
    }
};

} // namespace chipsetTests

#endif // CHIPSET_TESTS_FIXTURES_FAKEIOPORTDEVICE_H
