// ============================================================================
// tests/deviceLib/test_floppy82077.cpp -- 82077 FDC IRQ6 poll bit (0x536)
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
// F4 of the floppy fast-fail (dva0).  The DS10 console floppy driver is polled
// (dv_driver.c ide_polled_flag=1 under TURBO); ide_poll() detects a pending
// floppy IRQ6 by writing 0x0A to port 0x536 and testing bit 0x80.  These cases
// verify RECALIBRATE/SEEK raise that bit, SENSE INTERRUPT clears it (returning
// ST0=0x70 abnormal so the driver concludes "drive not present" fast), and the
// 0x0A poll-select write is a no-op.  doctest CHECK only.
// ============================================================================

#include "doctest.h"

#include <cstdint>

#include "deviceLib/Tsunami/Floppy82077.h"

namespace {

uint8_t rd(Floppy82077& f, uint16_t p) { return static_cast<uint8_t>(f.ioRead(p, 1) & 0xFFu); }
void    wr(Floppy82077& f, uint16_t p, uint8_t v) { f.ioWrite(p, v, 1); }

constexpr uint16_t kFifo = 0x3F5;     // FDC data/FIFO (kBase + 5)
constexpr uint16_t kPoll = 0x536;     // TURBO floppy interrupt-poll register

} // namespace

TEST_CASE("Floppy82077: idle controller reports no pending interrupt at 0x536")
{
    Floppy82077 f;
    CHECK(rd(f, kPoll) == 0x00);
}

TEST_CASE("Floppy82077: RECALIBRATE raises the IRQ6 poll bit at 0x536")
{
    Floppy82077 f;
    wr(f, kFifo, 0x07);                       // RECALIBRATE opcode
    wr(f, kFifo, 0x00);                       // drive 0 -> command complete
    CHECK(rd(f, kPoll) == Floppy82077::kIntPending);   // 0x80 pending
}

TEST_CASE("Floppy82077: ide_poll sequence (write 0x0A, read 0x536) sees SEEK pending")
{
    Floppy82077 f;
    wr(f, kFifo, 0x0F);                       // SEEK opcode (nParam=2)
    wr(f, kFifo, 0x00);                       // drive 0
    wr(f, kFifo, 0x10);                       // cylinder -> command complete
    wr(f, kPoll, 0x0A);                       // poll-select (must be a no-op)
    CHECK(rd(f, kPoll) == Floppy82077::kIntPending);
}

TEST_CASE("Floppy82077: SENSE INTERRUPT clears 0x536 and returns ST0=0x70/PCN=0")
{
    Floppy82077 f;
    wr(f, kFifo, 0x07); wr(f, kFifo, 0x00);   // RECALIBRATE -> pending
    CHECK(rd(f, kPoll) == Floppy82077::kIntPending);

    wr(f, kFifo, 0x08);                        // SENSE INTERRUPT (no params)
    uint8_t const st0 = rd(f, kFifo);          // result byte 0 = ST0
    uint8_t const pcn = rd(f, kFifo);          // result byte 1 = PCN
    CHECK(st0 == 0x70);                        // abnormal + seek-end + equip-check
    CHECK(pcn == 0x00);
    CHECK(rd(f, kPoll) == 0x00);               // interrupt acknowledged -> cleared
}

TEST_CASE("Floppy82077: SENSE DRIVE STATUS does not raise the poll bit")
{
    Floppy82077 f;
    wr(f, kFifo, 0x04);                        // SENSE DRIVE STATUS (nParam=1)
    wr(f, kFifo, 0x00);                        // drive 0 -> complete
    (void) rd(f, kFifo);                       // drain ST3 result byte
    CHECK(rd(f, kPoll) == 0x00);               // no interrupt for this command
}
