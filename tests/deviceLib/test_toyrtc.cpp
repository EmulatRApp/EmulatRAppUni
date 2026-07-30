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
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

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


TEST_CASE("ToyRtc: Reg D VRT -- set in fixed mode, clear in unset (SPEC-TOY-001)")
{
    // fixed mode: battery-good, the pre-2026-07-30 behavior.
    ToyRtc t;
    t.setTimeMode(ToyRtc::TimeMode::Fixed);
    CHECK(rtcRead(t, ToyRtc::kRegD) == ToyRtc::kRegD_VRT);

    // Reg D is read-only: a write must not disturb VRT.
    rtcWrite(t, ToyRtc::kRegD, 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegD) == ToyRtc::kRegD_VRT);

    // unset mode (the default): dead battery until a guest write.
    ToyRtc u;
    CHECK(u.timeMode() == ToyRtc::TimeMode::Unset);
    CHECK(rtcRead(u, ToyRtc::kRegD) == 0x00);
    rtcWrite(u, ToyRtc::kRegD, 0xFF);                 // read-only: dropped
    CHECK(rtcRead(u, ToyRtc::kRegD) == 0x00);
}


TEST_CASE("ToyRtc: unset mode -- invalid fields, clock does not advance")
{
    uint64_t cycles = 90061ull * 1000000000ull;       // deep into 'time'
    ToyRtc t;                                         // default = unset
    t.bindCycleSource(&cycles);

    // D-2: two independent invalidity signals -- VRT clear AND
    // out-of-range clock fields (month 0 / day 0).
    CHECK(rtcRead(t, ToyRtc::kRegD)     == 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegMonth) == 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegDom)   == 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegYear)  == 0x00);

    // The clock does NOT advance while invalid.
    cycles += 3600ull * 1000000000ull;
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegHours)   == 0x00);
}


TEST_CASE("ToyRtc: W-2 write protocol -- SET/stage/clear latches the origin")
{
    uint64_t cycles = 1000ull * 1000000000ull;        // arbitrary start
    ToyRtc t;                                         // unset: dead battery
    t.bindCycleSource(&cycles);
    CHECK(rtcRead(t, ToyRtc::kRegD) == 0x00);

    // Guest sets 2026-07-30 03:45:00 (BCD, 24-hour) via the protocol.
    rtcWrite(t, ToyRtc::kRegB,
             static_cast<uint8_t>(ToyRtc::kRegB_SET | ToyRtc::kRegB_H24));
    rtcWrite(t, ToyRtc::kRegSeconds, 0x00);
    rtcWrite(t, ToyRtc::kRegMinutes, 0x45);
    rtcWrite(t, ToyRtc::kRegHours,   0x03);
    rtcWrite(t, ToyRtc::kRegDom,     0x30);
    rtcWrite(t, ToyRtc::kRegMonth,   0x07);
    rtcWrite(t, ToyRtc::kRegYear,    0x26);
    rtcWrite(t, ToyRtc::kRegB, ToyRtc::kRegB_H24);    // SET 1->0: latch (W-2)

    // W-3: valid now; readback returns the WRITTEN time.
    CHECK(rtcRead(t, ToyRtc::kRegD)       == ToyRtc::kRegD_VRT);
    CHECK(rtcRead(t, ToyRtc::kRegMinutes) == 0x45);
    CHECK(rtcRead(t, ToyRtc::kRegHours)   == 0x03);
    CHECK(rtcRead(t, ToyRtc::kRegDom)     == 0x30);
    CHECK(rtcRead(t, ToyRtc::kRegMonth)   == 0x07);
    CHECK(rtcRead(t, ToyRtc::kRegYear)    == 0x26);

    // ...and ADVANCES from it on the cycle source (the discarded-write
    // defect regression: +90 seconds -> 03:46:30).
    cycles += 90ull * 1000000000ull;
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x30);
    CHECK(rtcRead(t, ToyRtc::kRegMinutes) == 0x46);
    CHECK(rtcRead(t, ToyRtc::kRegHours)   == 0x03);

    // A garbage write (month 0) must be rejected without disturbing
    // the previously latched valid time.
    rtcWrite(t, ToyRtc::kRegB,
             static_cast<uint8_t>(ToyRtc::kRegB_SET | ToyRtc::kRegB_H24));
    rtcWrite(t, ToyRtc::kRegMonth, 0x00);
    rtcWrite(t, ToyRtc::kRegB, ToyRtc::kRegB_H24);
    CHECK(rtcRead(t, ToyRtc::kRegD)     == ToyRtc::kRegD_VRT);
    CHECK(rtcRead(t, ToyRtc::kRegMonth) == 0x07);
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
    t.setTimeMode(ToyRtc::TimeMode::Fixed);   // SPEC-TOY-001: epoch clock
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
    t.setTimeMode(ToyRtc::TimeMode::Fixed);
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
    t.setTimeMode(ToyRtc::TimeMode::Fixed);
    t.bindCycleSource(&cycles);

    // Selecting Reg B materializes the epoch clock first (B2), so the
    // non-staged fields hold a coherent 2026-01-01 00:00:42.  Halt
    // updates, stage a seconds value, and confirm re-indexing the
    // clock does NOT overwrite it while SET is held (W-1).
    rtcWrite(t, ToyRtc::kRegB, ToyRtc::kRegB_SET);
    rtcWrite(t, ToyRtc::kRegSeconds, 0x33);
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x33);   // staged byte stands

    // Clearing SET LATCHES the staged (coherent, valid) time as the new
    // origin -- W-2.  Pre-2026-07-30 the stage was DISCARDED here and
    // the clock snapped back to the epoch; that was the defect.
    rtcWrite(t, ToyRtc::kRegB, 0x00);
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x33);   // written time sticks

    // ...and advances from it: +9 s -> 00:00:42 again, by a new route.
    cycles += 9ull * 1000000000ull;
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x42);
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
    a.setTimeMode(ToyRtc::TimeMode::Fixed);   // gate-safety: pure f(cycles)
    b.setTimeMode(ToyRtc::TimeMode::Fixed);
    a.bindCycleSource(&cycles);
    b.bindCycleSource(&cycles);

    for (uint8_t idx = 0; idx < ToyRtc::kClockRegEnd; ++idx) {
        CHECK(rtcRead(a, idx) == rtcRead(b, idx));    // byte-identical
    }
}


TEST_CASE("ToyRtc: timestampMMDDhhmm (CSERVE get_time) matches a 24h/BCD RTC read")
{
    // get_time (CSERVE 0x66) returns sys__get_timestamp's packed value:
    //   R0 = (month<<24)|(dom<<16)|(hour<<8)|min, BCD / 24-hour.  The static
    // helper shares calendarFromCycles() with the RTC port path, so it must
    // byte-match a 24h/BCD RTC materialized from the same cycles.
    uint64_t cycles = (40ull * 86400ull + 13ull * 3600ull + 45ull * 60ull + 30ull)
                    * 1000000000ull;                     // ToyRtc default cyclesPerSecond
    ToyRtc t;                                            // default cyclesPerSecond
    t.setTimeMode(ToyRtc::TimeMode::Fixed);              // CSERVE path = epoch clock
    rtcWrite(t, ToyRtc::kRegB, ToyRtc::kRegB_H24);       // 24-hour, BCD (DM = 0)
    t.bindCycleSource(&cycles);

    uint8_t const mo = rtcRead(t, ToyRtc::kRegMonth);
    uint8_t const dd = rtcRead(t, ToyRtc::kRegDom);
    uint8_t const hh = rtcRead(t, ToyRtc::kRegHours);
    uint8_t const mm = rtcRead(t, ToyRtc::kRegMinutes);
    uint32_t const expected = (static_cast<uint32_t>(mo) << 24)
                            | (static_cast<uint32_t>(dd) << 16)
                            | (static_cast<uint32_t>(hh) <<  8)
                            |  static_cast<uint32_t>(mm);

    CHECK(ToyRtc::timestampMMDDhhmm(cycles) == expected);
    CHECK(ToyRtc::timestampMMDDhhmm(cycles) == 0x02101345u);   // 2026-02-10 13:45 BCD
}


// ============================================================================
// SPEC-TOY-001 Sec 6: persistence.
// ============================================================================

namespace {

std::string tempStorePath(char const* tag)
{
    return (std::filesystem::temp_directory_path()
            / (std::string("emulatr_toy_") + tag + ".bin")).string();
}

} // namespace


TEST_CASE("ToyRtc: persistence -- write, flush, re-init from file")
{
    std::string const path = tempStorePath("roundtrip");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    uint64_t cycles = 500ull * 1000000000ull;
    {
        ToyRtc t;                                     // unset
        t.bindCycleSource(&cycles);
        t.bindBacking(path);                          // absent -> stays unset
        CHECK(rtcRead(t, ToyRtc::kRegD) == 0x00);

        // Guest sets 2026-07-30 04:00:00 and stashes an NVRAM byte the
        // console really uses (0x3E, quick_start).
        rtcWrite(t, ToyRtc::kRegB,
                 static_cast<uint8_t>(ToyRtc::kRegB_SET | ToyRtc::kRegB_H24));
        rtcWrite(t, ToyRtc::kRegSeconds, 0x00);
        rtcWrite(t, ToyRtc::kRegMinutes, 0x00);
        rtcWrite(t, ToyRtc::kRegHours,   0x04);
        rtcWrite(t, ToyRtc::kRegDom,     0x30);
        rtcWrite(t, ToyRtc::kRegMonth,   0x07);
        rtcWrite(t, ToyRtc::kRegYear,    0x26);
        rtcWrite(t, ToyRtc::kRegB, ToyRtc::kRegB_H24);
        rtcWrite(t, 0x3E, 0xA7);                      // W-4: NVRAM byte
        CHECK(rtcRead(t, ToyRtc::kRegD) == ToyRtc::kRegD_VRT);
    }                                                 // instance destroyed

    // New instance + same store: VRT = 1, time continues from the
    // written value (frozen across downtime), NVRAM byte intact.
    uint64_t cycles2 = 7ull * 1000000000ull;          // fresh run, new cycles
    ToyRtc r;
    r.bindCycleSource(&cycles2);
    r.bindBacking(path);
    CHECK(rtcRead(r, ToyRtc::kRegD)       == ToyRtc::kRegD_VRT);
    CHECK(rtcRead(r, 0x3E)                == 0xA7);
    CHECK(rtcRead(r, ToyRtc::kRegHours)   == 0x04);
    CHECK(rtcRead(r, ToyRtc::kRegDom)     == 0x30);
    CHECK(rtcRead(r, ToyRtc::kRegMonth)   == 0x07);
    CHECK(rtcRead(r, ToyRtc::kRegYear)    == 0x26);

    std::filesystem::remove(path, ec);
}


TEST_CASE("ToyRtc: corrupt / mode-mismatched store -> UNSET, never consumed")
{
    std::string const path = tempStorePath("corrupt");

    // Corrupt: garbage shorter than the header.
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite("JUNK", 1, 4, f);
        std::fclose(f);
    }
    ToyRtc t;                                         // unset
    t.bindBacking(path);
    CHECK(rtcRead(t, ToyRtc::kRegD) == 0x00);         // stayed dead-battery

    // Mode mismatch: a store written by an UNSET run must not be
    // consumed by a FIXED (gate) run -- Sec 7.
    {
        uint64_t cycles = 0;
        ToyRtc w;
        w.bindCycleSource(&cycles);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        w.bindBacking(path);
        rtcWrite(w, ToyRtc::kRegB,
                 static_cast<uint8_t>(ToyRtc::kRegB_SET | ToyRtc::kRegB_H24));
        rtcWrite(w, ToyRtc::kRegSeconds, 0x00);
        rtcWrite(w, ToyRtc::kRegMinutes, 0x30);
        rtcWrite(w, ToyRtc::kRegHours,   0x02);
        rtcWrite(w, ToyRtc::kRegDom,     0x15);
        rtcWrite(w, ToyRtc::kRegMonth,   0x03);
        rtcWrite(w, ToyRtc::kRegYear,    0x99);
        rtcWrite(w, ToyRtc::kRegB, ToyRtc::kRegB_H24);
    }
    uint64_t cycles = 0;
    ToyRtc g;
    g.setTimeMode(ToyRtc::TimeMode::Fixed);
    g.bindCycleSource(&cycles);
    g.bindBacking(path);                              // mismatch -> ignored
    CHECK(rtcRead(g, ToyRtc::kRegD)    == ToyRtc::kRegD_VRT);   // fixed: valid
    CHECK(rtcRead(g, ToyRtc::kRegYear) == 0x26);      // epoch, NOT stored 0x99

    std::error_code ec;
    std::filesystem::remove(path, ec);
}


// ============================================================================
// SPEC-TOY-001 C-2: the fourteen clock/control registers a boot-era read
// actually returns, dumped for the record (fixed mode, cyc = 2.4e9 -- the
// neighborhood where OpenVMS read the TOY on 2026-07-29).
// ============================================================================

TEST_CASE("ToyRtc: C-2 register dump at a boot-representative instant")
{
    uint64_t cycles = 2400000000ull;                  // ~2.4 emulated seconds
    ToyRtc t;
    t.setTimeMode(ToyRtc::TimeMode::Fixed);
    t.bindCycleSource(&cycles);

    for (uint8_t idx = 0; idx <= ToyRtc::kRegD; ++idx) {
        MESSAGE("C-2 reg 0x" << std::hex << int(idx)
                << " = 0x" << int(rtcRead(t, idx)));
    }
    MESSAGE("C-2 century byte 0x32 = 0x" << std::hex << int(rtcRead(t, 0x32)));

    // The dump's load-bearing assertions: epoch-derived, valid-looking
    // BCD time (2026-01-01 00:00:02), VRT set, century byte zero.
    CHECK(rtcRead(t, ToyRtc::kRegSeconds) == 0x02);
    CHECK(rtcRead(t, ToyRtc::kRegMonth)   == 0x01);
    CHECK(rtcRead(t, ToyRtc::kRegDom)     == 0x01);
    CHECK(rtcRead(t, ToyRtc::kRegYear)    == 0x26);
    CHECK(rtcRead(t, ToyRtc::kRegD)       == ToyRtc::kRegD_VRT);
    CHECK(rtcRead(t, 0x32)                == 0x00);
}
