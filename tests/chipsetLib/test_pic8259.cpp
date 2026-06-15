// ============================================================================
// tests/chipsetLib/test_pic8259.cpp -- Cypress 8259 pair + device-IRQ chain
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
// Exercises Increment 2 of the serial-console interrupt design
// (journals/20260604_serial_console_interrupt_design.md, Sections 4+6):
//
//   - The pc264_io.c ICW1-4 / OCW1 init sequence (authoritative values
//     from cy82c693_def.h: ICW1=0x11 edge mode, vector bases 0x00/0x08).
//   - THE unstick invariant: an edge captured while IRQ4 is MASKED stays
//     pending in IRR; the OCW1 unmask delivers it (output re-evaluates
//     on the un-gating event, not the input edge).
//   - acknowledge() = INTA: IRR->ISR transfer drops the output and holds
//     off same-level requests until EOI -- the anti-storm contract for
//     native-mode guest ISRs.
//   - Edge mode: a level held high across ack+EOI does NOT re-trigger;
//     a fall+rise produces a fresh edge.
//   - OCW3 poll byte; master/slave cascade via IR2.
//   - Chain test: UART THRE kick-start -> evalDeviceIrqs -> DRIR<55>,
//     gated by the PIC mask; acknowledgeDeviceInterrupt drops the level.
//
// Per house rule, doctest CHECK only -- never REQUIRE (V4 builds disable
// exceptions; REQUIRE expands to a static_assert that fails compile).
// ============================================================================

#include "doctest.h"

#include <cstdint>

#include "chipsetLib/Pic8259Pair.h"
#include "chipsetLib/TsunamiChipset.h"
#include "deviceLib/Tsunami/Uart16550.h"

namespace {

// pc264_io.c initialize_hardware, exactly (cy82c693_def.h defaults).
// Slave first, then master, then the non-specific EOI sweep -- matching
// the firmware's order.
void pc264InitSequence(Pic8259Pair& pic)
{
    // Slave: ICW1-ICW4 then OCW1 (all masked).
    pic.ioWrite(0xA0, 0x11, 1);   // ICW1: edge, cascade, ICW4 required
    pic.ioWrite(0xA1, 0x08, 1);   // ICW2: vector base 8
    pic.ioWrite(0xA1, 0x02, 1);   // ICW3: slave ID 2
    pic.ioWrite(0xA1, 0x01, 1);   // ICW4: x86 mode, normal EOI
    pic.ioWrite(0xA1, 0xFF, 1);   // OCW1: IRQ8-15 disabled

    // Master: ICW1-ICW4 then OCW1 (only IRQ2 cascade enabled).
    pic.ioWrite(0x20, 0x11, 1);   // ICW1
    pic.ioWrite(0x21, 0x00, 1);   // ICW2: vector base 0
    pic.ioWrite(0x21, 0x04, 1);   // ICW3: slave on IR2
    pic.ioWrite(0x21, 0x01, 1);   // ICW4
    pic.ioWrite(0x21, 0xFB, 1);   // OCW1: all masked except IR2

    // Non-specific EOI sweep, both controllers (pc264_io.c:567-571).
    for (int i = 0; i < 8; ++i) {
        pic.ioWrite(0xA0, 0x20, 1);
        pic.ioWrite(0x20, 0x20, 1);
    }
}

} // anonymous namespace

TEST_SUITE("Pic8259Pair (Increment 2)")
{

TEST_CASE("Pre-init: edges latch nothing deliverable; output stays low")
{
    Pic8259Pair pic;
    pic.setIrqInput(4, true);
    CHECK(pic.outputAsserted() == false);
    CHECK(pic.initialized() == false);
}

TEST_CASE("pc264 init sequence completes; everything masked except cascade")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);
    CHECK(pic.initialized() == true);
    CHECK(pic.masterImr() == 0xFB);
    CHECK(pic.slaveImr() == 0xFF);
    CHECK(pic.outputAsserted() == false);

    // IRQ4 asserts while masked: captured, not delivered.
    pic.setIrqInput(4, true);
    CHECK((pic.masterIrr() & 0x10) != 0);
    CHECK(pic.outputAsserted() == false);
}

TEST_CASE("Unstick invariant: masked edge delivers on the OCW1 unmask")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);

    pic.setIrqInput(4, true);            // edge arrives while masked
    CHECK(pic.outputAsserted() == false);

    pic.ioWrite(0x21, 0xEB, 1);          // OCW1: unmask IRQ4 (keep IR2)
    CHECK(pic.outputAsserted() == true); // the unmask IS the trigger
}

TEST_CASE("acknowledge drops the output and holds the level until EOI")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);
    pic.ioWrite(0x21, 0xEB, 1);          // IRQ4 open
    pic.setIrqInput(4, true);
    CHECK(pic.outputAsserted() == true);

    int const vec = pic.acknowledge();   // INTA
    CHECK(vec == 0x04);                  // master base 0x00 | level 4
    CHECK(pic.outputAsserted() == false);
    CHECK((pic.masterIsr() & 0x10) != 0);

    // New edge while in service: latched but suppressed.
    pic.setIrqInput(4, false);
    pic.setIrqInput(4, true);
    CHECK((pic.masterIrr() & 0x10) != 0);
    CHECK(pic.outputAsserted() == false);

    pic.ioWrite(0x20, 0x20, 1);          // non-specific EOI
    CHECK((pic.masterIsr() & 0x10) == 0);
    CHECK(pic.outputAsserted() == true); // latched edge re-asserts
}

TEST_CASE("Edge mode: a held level does not re-trigger after ack + EOI")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);
    pic.ioWrite(0x21, 0xEB, 1);
    pic.setIrqInput(4, true);            // line rises and STAYS high
    CHECK(pic.outputAsserted() == true);

    (void) pic.acknowledge();
    pic.ioWrite(0x20, 0x20, 1);          // EOI; line still high, no new edge
    CHECK(pic.outputAsserted() == false);

    pic.setIrqInput(4, false);           // line falls...
    pic.setIrqInput(4, true);            // ...and rises: fresh edge
    CHECK(pic.outputAsserted() == true);
}

TEST_CASE("OCW3 poll returns 0x80|level and acknowledges")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);
    pic.ioWrite(0x21, 0xEB, 1);
    pic.setIrqInput(4, true);

    pic.ioWrite(0x20, 0x0C, 1);          // OCW3 with P bit
    uint8_t const poll = static_cast<uint8_t>(pic.ioRead(0x20, 1));
    CHECK(poll == 0x84);                 // pending | level 4
    CHECK(pic.outputAsserted() == false);// poll acked it
    CHECK((pic.masterIsr() & 0x10) != 0);
}

TEST_CASE("Cascade: slave request presents on master IR2 with slave vector")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);
    // Open slave IRQ12 (line 12 -> slave IR4) + master cascade already open.
    pic.ioWrite(0xA1, 0xEF, 1);          // slave OCW1: unmask IR4

    pic.setIrqInput(12, true);
    CHECK(pic.outputAsserted() == true);

    int const vec = pic.acknowledge();
    CHECK(vec == 0x0C);                  // slave base 0x08 | level 4
    CHECK(pic.outputAsserted() == false);
}

TEST_CASE("IMR gates output but never capture; remask hides a pending edge")
{
    Pic8259Pair pic;
    pc264InitSequence(pic);
    pic.ioWrite(0x21, 0xEB, 1);
    pic.setIrqInput(4, true);
    CHECK(pic.outputAsserted() == true);

    pic.ioWrite(0x21, 0xFB, 1);          // remask IRQ4
    CHECK(pic.outputAsserted() == false);
    CHECK((pic.masterIrr() & 0x10) != 0);// edge still pending

    pic.ioWrite(0x21, 0xEB, 1);          // unmask again
    CHECK(pic.outputAsserted() == true); // deferred, never dropped
}

TEST_CASE("Chain: UART THRE kick-start reaches DRIR<55> through the PIC")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 64ULL * 1024 * 1024);

    pc264InitSequence(cs.pic());

    // Driver hand-off: uartIer=0x03 with THR empty (the TEST 1 event).
    cs.com1().writeRegister(Uart16550::kIER_DLM, 0x03);
    CHECK(cs.com1().intPending() == true);

    // PIC still masks IRQ4: the boundary eval must NOT set DRIR<55>.
    cs.evalDeviceIrqs();
    CHECK(cs.cchip().pendingIrq1(0) == false);

    // DIM0 bit 55 (pc264_io.c:533) + the driver's IRQ4 unmask.
    cs.cchip().write(0x0200, uint64_t{1} << 55);   // DIM0
    cs.pic().ioWrite(0x21, 0xEB, 1);               // unmask IRQ4
    cs.evalDeviceIrqs();
    CHECK(cs.cchip().pendingIrq1(0) == true);      // the wire is live

    // INTA: level falls until the guest ISR would EOI.
    int const vec = cs.acknowledgeDeviceInterrupt();
    CHECK(vec == 0x04);
    CHECK(cs.cchip().pendingIrq1(0) == false);
}

} // TEST_SUITE
