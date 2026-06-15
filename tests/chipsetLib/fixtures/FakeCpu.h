// ============================================================================
// tests/chipsetLib/fixtures/FakeCpu.h
//   Reusable test fixture -- minimal CPU stand-in for chipset tests
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
// A minimal CPU stand-in carrying just the state the chipset's per-CPU
// surfaces touch: a cpuId, an IPL, a pending-b_irq bitmask, an IPI-pending
// flag, and a LDx_L / STx_C reservation pair (lock_flag, lock_address).
//
// Standalone by design.  Ticket 9 introduces an ICpuSnoopHandler
// interface; when it lands, FakeCpu can derive from it -- snoopWrite()
// below already matches the intended contract (clear the reservation
// when a write touches the locked 64-byte cache line).  Until then the
// fixture has no chipset dependency, so it compiles from Ticket 0.
//
// Per the silicon-provision principle, the chipset addresses CPUs by id
// 0..3; tests instantiate FakeCpu(0)..FakeCpu(3) to exercise the full
// per-CPU surface regardless of how many CPUs the live build wires.
// ============================================================================

#ifndef CHIPSET_TESTS_FIXTURES_FAKECPU_H
#define CHIPSET_TESTS_FIXTURES_FAKECPU_H

#include <cstdint>

namespace chipsetTests {

struct FakeCpu
{
    // EV6 reservation granularity: one 64-byte cache line.
    static constexpr uint64_t kCacheLineBytes = 64;

    int      cpuId        = 0;
    uint8_t  ipl          = 0;
    uint64_t pending_b_irq = 0;     // bitmask of asserted b_irq lines
    bool     ipiPending   = false;

    bool     lock_flag    = false;  // reservation valid
    uint64_t lock_address = 0;      // reserved PA

    FakeCpu() noexcept = default;
    explicit FakeCpu(int id) noexcept : cpuId(id) {}

    // LDx_L: take a reservation on the cache line containing pa.
    void takeReservation(uint64_t pa) noexcept
    {
        lock_flag    = true;
        lock_address = pa;
    }

    // System-bus snoop: a write of len bytes at pa (by another CPU or by
    // a DMA cycle) clears this reservation if it touches the locked
    // 64-byte line.  Ticket 9 wires the chipset broadcast to call this.
    void snoopWrite(uint64_t pa, uint8_t len) noexcept
    {
        if (!lock_flag)
            return;

        uint64_t const lineMask = ~(kCacheLineBytes - 1);
        uint64_t const lockLine = lock_address & lineMask;
        uint64_t const firstByte = pa;
        uint64_t const lastByte  = pa + (len ? static_cast<uint64_t>(len) - 1 : 0);
        uint64_t const writeLoLine = firstByte & lineMask;
        uint64_t const writeHiLine = lastByte  & lineMask;

        if (lockLine >= writeLoLine && lockLine <= writeHiLine)
            lock_flag = false;
    }
};

} // namespace chipsetTests

#endif // CHIPSET_TESTS_FIXTURES_FAKECPU_H
