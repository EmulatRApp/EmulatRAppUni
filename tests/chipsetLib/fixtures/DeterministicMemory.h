// ============================================================================
// tests/chipsetLib/fixtures/DeterministicMemory.h
//   Reusable test fixture -- flat, deterministic system-memory stand-in
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
// A flat byte-addressable memory used by the DMA tests (Ticket 7) and the
// reservation-snoop tests (Ticket 9).  Deterministic and zero-initialized:
// no MMIO hooks, no paging, no sentinels -- just bytes -- so a test can
// place a PTE or a payload at a known PA and assert exact round-trip.
//
// Out-of-range accesses are silently ignored (no throw -- the V4 build
// disables exceptions); a test should size the buffer to cover its PAs.
// ============================================================================

#ifndef CHIPSET_TESTS_FIXTURES_DETERMINISTICMEMORY_H
#define CHIPSET_TESTS_FIXTURES_DETERMINISTICMEMORY_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace chipsetTests {

struct DeterministicMemory
{
    std::vector<uint8_t> bytes;

    explicit DeterministicMemory(size_t sizeBytes) : bytes(sizeBytes, 0u) {}

    void write(uint64_t pa, void const* src, size_t len) noexcept
    {
        if (pa > bytes.size() || len > bytes.size() - pa)
            return;
        std::memcpy(bytes.data() + pa, src, len);
    }

    void read(uint64_t pa, void* dst, size_t len) const noexcept
    {
        if (pa > bytes.size() || len > bytes.size() - pa)
            return;
        std::memcpy(dst, bytes.data() + pa, len);
    }

    size_t size() const noexcept { return bytes.size(); }
};

} // namespace chipsetTests

#endif // CHIPSET_TESTS_FIXTURES_DETERMINISTICMEMORY_H
