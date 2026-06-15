// ============================================================================
// tests/chipsetLib/test_ticket06_isa_uart.cpp
//   Ticket 6 -- ISA I/O dispatch (sparse-I/O write path) + UART hookup
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
// Pins the sparse PCI I/O round-trip through the chipset MMIO surface: the
// sparse-offset decode, read/write dispatch to a registered IIoPortHandler,
// the 0xFF unconnected-port convention, and reachability of the two
// chipset-owned 16550 UARTs (COM1 0x3F8, COM2 0x2F8) registered in
// TsunamiChipset::wireDevices().
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiPchip.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"
#include "chipsetLib/fixtures/FakeIoPortDevice.h"
#include "deviceLib/Tsunami/Uart16550.h"

#include <cstdint>

using namespace Tsunami21272;

namespace {

// Build the full CPU physical address of a sparse PCI I/O access to `port`
// with the given transfer-length code.  Uses the same window-base constant
// the chipset subtracts (MMIOOffset::kPchip0_SparseIO) so the round-trip is
// exact: off - kPchip0_SparseIO == the sparse offset we encode here.
uint64_t sparseIoPA(uint16_t port, uint8_t xferCode = SparseSpace::kXferByte) noexcept
{
    uint64_t const sparseOff =
        (static_cast<uint64_t>(port) << SparseSpace::kPciAddrShift)
        | (xferCode & SparseSpace::kXferLenMask);
    return Base::kMMIO_Start + MMIOOffset::kPchip0_SparseIO + sparseOff;
}

} // anonymous namespace

TEST_CASE("Sparse I/O offset decodes back to the intended port and byte width")
{
    uint16_t const port = 0x3FD;
    uint64_t const sparseOff =
        (static_cast<uint64_t>(port) << SparseSpace::kPciAddrShift)
        | SparseSpace::kXferByte;

    CHECK((SparseSpace::decodePciAddr(sparseOff) & 0xFFFF) == port);
    CHECK(SparseSpace::decodeXferLen(sparseOff) == SparseSpace::kXferByte);
    CHECK(SparseSpace::xferLenToBytes(SparseSpace::decodeXferLen(sparseOff)) == 1);
}

TEST_CASE("Sparse I/O read dispatches to a registered handler at the decoded port")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::FakeIoPortDevice dev;
    dev.responses[0x1004] = 0x5A;
    cs.pchip().registerIoPortRange(0x1000, 0x1010, &dev);   // free range

    uint64_t const v = cs.mmioRead(sparseIoPA(0x1004), 1);
    CHECK((v & 0xFF) == 0x5A);
    CHECK(dev.reads.size() == 1);
    CHECK(dev.reads[0].port == 0x1004);
}

TEST_CASE("Sparse I/O write dispatches the byte to the registered handler")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    chipsetTests::FakeIoPortDevice dev;
    cs.pchip().registerIoPortRange(0x1000, 0x1010, &dev);   // free range

    cs.mmioWrite(sparseIoPA(0x1000), static_cast<uint64_t>('A'), 1);
    CHECK(dev.writes.size() == 1);
    CHECK(dev.writes[0].port == 0x1000);
    CHECK((dev.writes[0].value & 0xFF) == static_cast<uint64_t>('A'));
}

TEST_CASE("Unregistered sparse I/O port reads 0xFF (ISA floating bus)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // 0x1234 is outside the pre-registered COM1/COM2 ranges.
    CHECK((cs.mmioRead(sparseIoPA(0x1234), 1) & 0xFF) == 0xFFULL);
}

TEST_CASE("Chipset-owned COM1 LSR reads transmitter-ready via sparse I/O")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // COM1 LSR = 0x3F8 + 5.  Uart16550 pins THRE|TEMT ready.
    uint64_t const lsr = cs.mmioRead(sparseIoPA(0x3FD), 1) & 0xFF;
    CHECK((lsr & Uart16550::kLSR_THRE) != 0);
    CHECK((lsr & Uart16550::kLSR_TEMT) != 0);
}

TEST_CASE("Chipset-owned COM1 THR write is accepted (null backend tolerated)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // COM1 THR = 0x3F8.  No host backend wired; the UART tolerates nullptr.
    // The write must reach COM1 without faulting and leave it ready.
    cs.mmioWrite(sparseIoPA(0x3F8), static_cast<uint64_t>('Z'), 1);
    uint64_t const lsr = cs.mmioRead(sparseIoPA(0x3FD), 1) & 0xFF;
    CHECK((lsr & Uart16550::kLSR_THRE) != 0);
}

TEST_CASE("Chipset-owned COM2 LSR reachable via sparse I/O")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // COM2 LSR = 0x2F8 + 5.
    uint64_t const lsr = cs.mmioRead(sparseIoPA(0x2FD), 1) & 0xFF;
    CHECK((lsr & Uart16550::kLSR_THRE) != 0);
}
