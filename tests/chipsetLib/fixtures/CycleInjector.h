// ============================================================================
// tests/chipsetLib/fixtures/CycleInjector.h
//   Reusable test fixture -- deterministic chipset tick driver
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
// runCycles(chipset, N) advances a chipset by N cycles via step(),
// splitting N into fixed chunks so a timer-driven test never skips an
// interval boundary by stepping over it in one large jump.  Templated on
// the chipset type so the fixture carries no heavy include and works with
// any object exposing `void step(uint64_t)`.
//
// Used from Ticket 4 onward; landed in Ticket 1 alongside TsunamiChipset::
// step().
// ============================================================================

#ifndef CHIPSET_TESTS_FIXTURES_CYCLEINJECTOR_H
#define CHIPSET_TESTS_FIXTURES_CYCLEINJECTOR_H

#include <cstdint>

namespace chipsetTests {

template <typename Chipset>
inline void runCycles(Chipset& chipset, uint64_t totalCycles,
                      uint64_t chunk = 1) noexcept
{
    if (chunk == 0)
        chunk = 1;

    uint64_t remaining = totalCycles;
    while (remaining >= chunk) {
        chipset.step(chunk);
        remaining -= chunk;
    }
    if (remaining != 0)
        chipset.step(remaining);
}

} // namespace chipsetTests

#endif // CHIPSET_TESTS_FIXTURES_CYCLEINJECTOR_H
