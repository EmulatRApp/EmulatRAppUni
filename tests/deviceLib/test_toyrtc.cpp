// ============================================================================
// tests/deviceLib/test_toyrtc.cpp -- MC146818 TOY clock + CMOS unit tests
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
// Exercises deviceLib/Tsunami/ToyRtc.h against the behaviors the DS10 SRM
// firmware actually depends on (apisrm toy_driver.c rtc_read/rtc_write,
// timer.c krn$_reset_toy, date.c):
//
//   - B1 latch model: data-port reads return the latch filled at
//     index-write time, exactly the write-index-then-read-data flow.
//   - B2 lazy time: clock registers materialize from the deterministic
//     cycle-derived time source at index-write time.
//   - Reg A: UIP always reads low (G1a); divisor bits store through.
//   - Reg B: stores through; DM / H24 govern encoding of the clock regs.
//   - Reg C: read-clears (no IRQ source modeled yet, so reads 0).
//   - Reg D: VRT (0x80) always set -- the SRM "battery good" check.
//   - The literal krn$_reset_toy sequence: write A=0x26, RMW B
//     (clear SET 0x80, set SQWE 0x08 on PC264), read-back verified.
//   - Determinism: two instances over the same cycle counter read
//     byte-identical clock registers.
//
// Per house rule, doctest CHECK only -- never REQUIRE (V4 builds disable
// exceptions; REQUIRE expands to a static_assert that fails compile).
// Reference: memory [[v4-doctest-no-require]].
// ============================================================================

#include "doctest.h"

#include <cstdint>

#include "deviceLib/Tsunami/ToyRtc.h"

namespace {

// Convenience byte-wide port helpers (the firmware path is byte-only).
uint8_t rd(ToyRtc& t, uint16_t port)
{
    return static_cast<uint8_t>(t.ioRead(port, 1) & 0xFFu);
}

void wr(ToyRtc& t, uint16_t port, uint8_t v)
{
    t.ioWrite(port, v, 1);
}

// The firmware's rtc_read idiom: index then data.
uint8_t rtcRead(ToyRtc& t, uint8_t idx)
{
    wr(t, 0x70, idx);
    return rd(t, 0x71);
}

// The firmware's rtc_write idiom: index then data.
void rtcWrite(ToyRtc& t, uint8_t idx, uint8_t v)
{
    wr(t, 0x70, idx);
    wr(t, 0x71, v);
}

} // namespace


TEST_CASE("ToyRtc: Reg D reads VRT set -- SRM battery-good check")
{
    ToyRtc t;
    CHECK(rtcRead(t, ToyRtc::kRegD) == ToyRtc::kRegD_VRT);

    // Reg D is read-only: a write must not disturb VRT.
    rtcWrite(t, ToyRtc::kRegD, 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegD) == ToyRtc::kRegD_VRT);
}


TEST_CASE("ToyRtc: B1 latch -- data port returns index-write-time snapshot")
{
    ToyRtc t;

    // Store a recognizable NVRAM byte, then select it: the latch is
    // filled at INDEX WRITE time.
    rtcWrite(t, 0x20, 0x5A);
    wr(t, 0x70, 0x20);
    CHECK(rd(t, 0x71) == 0x5A);

    // Repeated data reads return the same latch without re-indexing.
    CHECK(rd(t, 0x71) == 0x5A);

    // Index port read returns the last index written (reference model).
    CHECK(rd(t, 0x70) == 0x20);

    // G1c: NMI-mask bit 7 of the index is dropped.
    wr(t, 0x70, static_cast<uint8_t>(0x80u | 0x20u));
    CHECK(rd(t, 0x70) == 0x20);
    CHECK(rd(t, 0x71) == 0x5A);
}


TEST_CASE("ToyRtc: lazy time materialization from the cycle source (BCD)")
{
    // 1 GHz divisor; 90061 seconds = 1 day + 1 hour + 1 minute + 1 second.
    uint64_t cycles = 90061ull * 1000000000ull;
    ToyRtc t;
    t.bindCycleSource(&cycles);

    // Zero-init Reg B: DM=0 (BCD), H24=0 (12-hour).  Epoch is
    // 2026-01-01 00:00:00 Thursday; +1d 01:01:01 = Fri 2026-01-02,
    // 01:01:01 AM.
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x01);
    CHECK(rtcRead(t, ToyRtc::kRegMinutes) == 0x01);
    CHECK(rtcRead(t, ToyRtc::kRegHours)   == 0x01);   // 1 AM, PM bit clear
    CHECK(rtcRead(t, ToyRtc::kRegDow)     == 0x06);   // Friday (1 = Sunday)
    CHECK(rtcRead(t, ToyRtc::kRegDom)     == 0x02);
    CHECK(rtcRead(t, ToyRtc::kRegMonth)   == 0x01);
    CHECK(rtcRead(t, ToyRtc::kRegYear)    == 0x26);   // BCD two-digit 26
}


TEST_CASE("ToyRtc: binary + 24-hour modes honored via Reg B")
{
    // 13:00:00 on the epoch day.
    uint64_t cycles = 13ull * 3600ull * 1000000000ull;
    ToyRtc t;
    t.bindCycleSource(&cycles);

    // Select binary (DM) + 24-hour (H24) before reading the clock.
    rtcWrite(t, ToyRtc::kRegB,
             static_cast<uint8_t>(ToyRtc::kRegB_DM | ToyRtc::kRegB_H24));
    CHECK(rtcRead(t, ToyRtc::kRegHours) == 13);       // binary 24h, no PM bit
    CHECK(rtcRead(t, ToyRtc::kRegYear)  == 26);       // binary two-digit year

    // 12-hour BCD view of the same instant: 1 PM = 0x01 | PM bit.
    rtcWrite(t, ToyRtc::kRegB, 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegHours) ==
          static_cast<uint8_t>(0x01u | ToyRtc::kHourPmBit));
}


TEST_CASE("ToyRtc: krn$_reset_toy sequence -- RegA=0x26, RMW RegB SQWE")
{
    ToyRtc t;

    // fseek 0x0a; fwrite 0x26      (Reg A: 32.768 kHz base, ~1024 Hz rate)
    rtcWrite(t, ToyRtc::kRegA, 0x26);

    // fseek 0x0b; fread            (Reg B read-modify-write)
    uint8_t b = rtcRead(t, ToyRtc::kRegB);
    b = static_cast<uint8_t>(b & ~ToyRtc::kRegB_SET);  // clear SET
    b = static_cast<uint8_t>(b | ToyRtc::kRegB_SQWE);  // PC264: enable SQW
    rtcWrite(t, ToyRtc::kRegB, b);

    // Read-back: Reg A stored (UIP low), Reg B holds SQWE, SET clear.
    CHECK(rtcRead(t, ToyRtc::kRegA) == 0x26);
    CHECK((rtcRead(t, ToyRtc::kRegA) & ToyRtc::kRegA_UIP) == 0);  // G1a
    CHECK(rtcRead(t, ToyRtc::kRegB) == ToyRtc::kRegB_SQWE);
}


TEST_CASE("ToyRtc: Reg B SET holds off materialization for time staging")
{
    uint64_t cycles = 42ull * 1000000000ull;          // 00:00:42
    ToyRtc t;
    t.bindCycleSource(&cycles);

    // Halt updates, stage a seconds value, and confirm re-indexing the
    // clock does NOT overwrite it while SET is held.
    rtcWrite(t, ToyRtc::kRegB, ToyRtc::kRegB_SET);
    rtcWrite(t, ToyRtc::kRegSeconds, 0x33);
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x33);   // staged byte stands

    // Clearing SET resumes the deterministic clock on next index write.
    rtcWrite(t, ToyRtc::kRegB, 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x42);   // BCD 42
}


TEST_CASE("ToyRtc: Reg C read-clears and is write-protected")
{
    ToyRtc t;
    rtcWrite(t, ToyRtc::kRegC, 0xFF);                 // write dropped
    CHECK(rtcRead(t, ToyRtc::kRegC) == 0x00);         // no flags modeled yet
    CHECK(rtcRead(t, ToyRtc::kRegC) == 0x00);         // still clear
}


TEST_CASE("ToyRtc: non-byte access floats reads and drops writes")
{
    ToyRtc t;
    rtcWrite(t, 0x40, 0xA5);
    CHECK(t.ioRead(0x71, 4) == 0xFFull);              // wide read floats
    t.ioWrite(0x71, 0x00, 4);                         // wide write dropped
    CHECK(rtcRead(t, 0x40) == 0xA5);                  // byte path intact
}


TEST_CASE("ToyRtc: determinism -- same cycles, same bytes")
{
    uint64_t cycles = 123456789ull * 10ull;           // arbitrary instant
    ToyRtc a;
    ToyRtc b;
    a.bindCycleSource(&cycles);
    b.bindCycleSource(&cycles);

    for (uint8_t idx = 0; idx < ToyRtc::kClockRegEnd; ++idx) {
        CHECK(rtcRead(a, idx) == rtcRead(b, idx));    // byte-identical
    }
}
