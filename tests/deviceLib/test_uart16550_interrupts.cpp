// ============================================================================
// tests/deviceLib/test_uart16550_interrupts.cpp -- 16550 interrupt-line tests
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
// Exercises the Increment 1 interrupt model of deviceLib/Tsunami/
// Uart16550.h (journals/20260604_serial_console_interrupt_design.md,
// Sections 1-3) against the behaviors the DS10 SRM tt driver depends on:
//
//   - The enable-while-already-empty kick-start: uartIer=0x03 written
//     with THR drained asserts the THRE source (the TEST 1 unstick).
//   - IIR priority (RX 0x04 above THRE 0x02) + clear-on-read semantics:
//     reporting THRE clears its latch; reporting RX does NOT.
//   - THR write re-arms the THRE source (per-byte fresh edge for the
//     Cypress 8259 edge-trigger mode).
//   - RX level: feedRxByte -> intPending with ERBFI; RBR drain drops it;
//     masking via uartIer gates the level without consuming it
//     (deferred-not-dropped, the UART layer analogue of design 4.1).
//   - RX FIFO depth 16 + sticky LSR overrun on overflow, clear-on-read.
//   - FCR RX-reset; DLAB divisor access does not touch the IER path.
//
// Per house rule, doctest CHECK only -- never REQUIRE (V4 builds disable
// exceptions; REQUIRE expands to a static_assert that fails compile).
// Reference: memory [[v4-doctest-no-require]].
// ============================================================================

#include "doctest.h"

#include <cstdint>

#include "deviceLib/Tsunami/Uart16550.h"

namespace {

constexpr uint16_t kBase = 0x3F8;   // COM1

uint8_t rd(Uart16550& u, uint8_t off)
{
    return static_cast<uint8_t>(u.ioRead(static_cast<uint16_t>(kBase + off), 1));
}

void wr(Uart16550& u, uint8_t off, uint8_t v)
{
    u.ioWrite(static_cast<uint16_t>(kBase + off), v, 1);
}

} // anonymous namespace

TEST_SUITE("Uart16550 interrupt line (Increment 1)")
{

TEST_CASE("Reset state: no interrupt pending, FIFO empty, TX ready")
{
    Uart16550 u;        // null backend
    CHECK(u.intPending() == false);
    CHECK(u.rxFifoCount() == 0);
    uint8_t const lsr = rd(u, Uart16550::kLSR);
    CHECK((lsr & Uart16550::kLSR_THRE) != 0);
    CHECK((lsr & Uart16550::kLSR_TEMT) != 0);
    CHECK((lsr & Uart16550::kLSR_DR) == 0);
}

TEST_CASE("Kick-start: IER=0x03 with THR already empty asserts THRE source")
{
    Uart16550 u;
    CHECK(u.intPending() == false);

    // The traced SRM hand-off event: enable RX+THR with the line idle.
    wr(u, Uart16550::kIER_DLM, 0x03);
    CHECK(u.intPending() == true);

    // IIR reports THRE (no RX data) and clears it on read.
    uint8_t const iir = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir & 0x0F) == Uart16550::kIIR_THRE);
    CHECK(u.intPending() == false);

    // A second IIR read reports nothing pending.
    uint8_t const iir2 = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir2 & 0x0F) == Uart16550::kIIR_NO_INT);
}

TEST_CASE("IER write without ETBEI edge does not assert; repeated 0x03 is edge-gated")
{
    Uart16550 u;
    wr(u, Uart16550::kIER_DLM, 0x01);   // ERBFI only, no RX data
    CHECK(u.intPending() == false);

    wr(u, Uart16550::kIER_DLM, 0x03);   // ETBEI rises -> kick-start
    CHECK(u.intPending() == true);
    (void) rd(u, Uart16550::kIIR_FCR);  // clear THRE
    CHECK(u.intPending() == false);

    wr(u, Uart16550::kIER_DLM, 0x03);   // ETBEI already set: no edge
    CHECK(u.intPending() == false);
}

TEST_CASE("THR write re-arms the THRE source (per-byte fresh edge)")
{
    Uart16550 u;
    wr(u, Uart16550::kIER_DLM, 0x02);   // ETBEI
    (void) rd(u, Uart16550::kIIR_FCR);  // consume the kick-start
    CHECK(u.intPending() == false);

    wr(u, Uart16550::kRBR_THR_DLL, 'P');    // transmit one byte
    CHECK(u.intPending() == true);           // THR empty again

    uint8_t const iir = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir & 0x0F) == Uart16550::kIIR_THRE);
    CHECK(u.intPending() == false);
}

TEST_CASE("RX level: feed -> pending; RBR drain -> level falls")
{
    Uart16550 u;
    wr(u, Uart16550::kIER_DLM, 0x01);   // ERBFI
    CHECK(u.intPending() == false);

    u.feedRxByte('x');
    CHECK(u.intPending() == true);
    CHECK((rd(u, Uart16550::kLSR) & Uart16550::kLSR_DR) != 0);

    uint8_t const iir = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir & 0x0F) == Uart16550::kIIR_RXAVAIL);
    // RX report does NOT consume the byte; the level holds until RBR.
    CHECK(u.intPending() == true);

    uint8_t const b = rd(u, Uart16550::kRBR_THR_DLL);
    CHECK(b == 'x');
    CHECK(u.intPending() == false);
    CHECK((rd(u, Uart16550::kLSR) & Uart16550::kLSR_DR) == 0);
}

TEST_CASE("IIR priority: RX-avail reported above THRE; THRE latch survives")
{
    Uart16550 u;
    wr(u, Uart16550::kIER_DLM, 0x03);   // kick-start arms THRE
    u.feedRxByte('a');                  // and RX is pending

    uint8_t const iir = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir & 0x0F) == Uart16550::kIIR_RXAVAIL);   // RX wins

    (void) rd(u, Uart16550::kRBR_THR_DLL);            // drain RX
    // THRE was NOT cleared by the RX-priority read; it reports now.
    uint8_t const iir2 = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir2 & 0x0F) == Uart16550::kIIR_THRE);
}

TEST_CASE("Masking defers, never drops: IER gate off/on preserves RX level")
{
    Uart16550 u;
    u.feedRxByte('q');                  // data arrives while masked
    CHECK(u.intPending() == false);     // uartIer = 0: gated

    wr(u, Uart16550::kIER_DLM, 0x01);   // unmask ERBFI
    CHECK(u.intPending() == true);      // level re-evaluates, not edge

    wr(u, Uart16550::kIER_DLM, 0x00);   // mask again
    CHECK(u.intPending() == false);
    CHECK(u.rxFifoCount() == 1);        // byte never lost

    wr(u, Uart16550::kIER_DLM, 0x01);
    CHECK(u.intPending() == true);
}

TEST_CASE("RX FIFO depth 16; overflow sets sticky LSR overrun, clears on read")
{
    Uart16550 u;
    for (int i = 0; i < 17; ++i) {
        u.feedRxByte(static_cast<uint8_t>('A' + i));
    }
    CHECK(u.rxFifoCount() == Uart16550::kRxFifoDepth);   // 17th dropped

    uint8_t const lsr = rd(u, Uart16550::kLSR);
    CHECK((lsr & Uart16550::kLSR_OE) != 0);              // overrun sticky
    uint8_t const lsr2 = rd(u, Uart16550::kLSR);
    CHECK((lsr2 & Uart16550::kLSR_OE) == 0);             // clear-on-read

    // FIFO order preserved; first byte is 'A'.
    CHECK(rd(u, Uart16550::kRBR_THR_DLL) == 'A');
}

TEST_CASE("FCR RX-reset clears the FIFO and the RX level")
{
    Uart16550 u;
    wr(u, Uart16550::kIER_DLM, 0x01);
    u.feedRxByte('z');
    CHECK(u.intPending() == true);

    wr(u, Uart16550::kIIR_FCR, 0x03);   // FIFO enable + RX reset
    CHECK(u.rxFifoCount() == 0);
    CHECK(u.intPending() == false);

    // FIFOs-on indicator appears in IIR bits 7:6 after enable.
    uint8_t const iir = rd(u, Uart16550::kIIR_FCR);
    CHECK((iir & Uart16550::kIIR_FIFO_ON) == Uart16550::kIIR_FIFO_ON);
}

TEST_CASE("DLAB redirects +0x01 to DLM; divisor writes never touch the IER path")
{
    Uart16550 u;
    wr(u, Uart16550::kLCR, Uart16550::kLCR_DLAB);   // DLAB on
    wr(u, Uart16550::kIER_DLM, 0x0C);               // writes DLM, not IER
    CHECK(u.intPending() == false);                  // no kick-start
    CHECK(rd(u, Uart16550::kIER_DLM) == 0x0C);       // reads DLM back

    wr(u, Uart16550::kLCR, 0x03);                    // DLAB off, 8N1
    CHECK(rd(u, Uart16550::kIER_DLM) == 0x00);       // IER untouched
}

TEST_CASE("MCR stores low 5 bits; OUT2 stored but does not gate intPending")
{
    Uart16550 u;
    wr(u, Uart16550::kMCR, 0xFF);
    CHECK(rd(u, Uart16550::kMCR) == 0x1F);

    // OUT2 clear; line must still assert (gate intentionally unarmed --
    // design doc Section 2 validated-negative resolution).
    wr(u, Uart16550::kMCR, 0x00);
    wr(u, Uart16550::kIER_DLM, 0x02);
    CHECK(u.intPending() == true);
}

TEST_CASE("reset() clears latch, FIFO, sticky errors")
{
    Uart16550 u;
    wr(u, Uart16550::kIER_DLM, 0x03);
    u.feedRxByte('r');
    CHECK(u.intPending() == true);

    u.reset();
    CHECK(u.intPending() == false);
    CHECK(u.rxFifoCount() == 0);
    CHECK((rd(u, Uart16550::kLSR) & Uart16550::kLSR_OE) == 0);
}

} // TEST_SUITE
