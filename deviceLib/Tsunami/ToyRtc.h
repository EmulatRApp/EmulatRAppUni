// ============================================================================
// ToyRtc.h -- MC146818-compatible TOY clock + CMOS (ports 0x70-0x71)
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE:
//   Functional MC146818 real-time-clock / CMOS model for the DS10 (PC264).
//   On real hardware the 0x70/0x71 index/data pair is decoded by the
//   Cypress CY82C693 ISA bridge; the register-level behavior is the
//   industry-standard MC146818 core.  This device replaces the 2026-05-28
//   Mc146818RtcStub (deviceLib/Tsunami/MinimalIsaStub.h) which answered
//   every data read with 0x00.
//
//   The SRM firmware's TOY driver (apisrm toy_driver.c rtc_read/rtc_write)
//   drives exactly this pair: write register index to 0x70, read/write the
//   byte at 0x71.  krn$_reset_toy writes Reg A = 0x26 and read-modify-
//   writes Reg B (clear SET, set SQWE on PC264) during early console
//   bring-up; date.c reads/writes the clock registers for SHOW/SET DATE.
//
// SPECIFICATION:
//   "Toy Specification" grounding note (2026-06-xx, AXPBox-verified
//   MC146818 behavior) with the DS10 deltas agreed 2026-06-03:
//     - Provenance: Cypress CY82C693 decode, NOT ALi M1543C (that is the
//       ES40/AXPBox part).  Same MC146818 core either way.
//     - Ports 0x70/0x71 only.  The ALi-specific 0x72/0x73 high-128 bank
//       is NOT implemented; the DS10 toy driver never touches it.
//     - B1 (LATCH MODEL): a data-port read returns a LATCH that was
//       filled at index-write time, not a live register read.  This
//       matches the AXPBox reference and the firmware's canonical
//       write-index-then-read-data sequence.
//     - B2 (LAZY TIME): writing an index < 0x0E materializes the clock
//       registers (sec/min/hour/dow/day/mon/year) from the time source
//       at that moment, honoring Reg B DM (binary/BCD) and 24/12-hour
//       bits.  Alarm registers are never auto-filled.
//     - G1a (UIP): always-low.  The DS10 boot path (krn$_reset_toy,
//       toy_read/toy_write) never polls UIP; revisit when date.c
//       set-time support matters.
//     - G1b (PERSISTENCE): superseded 2026-07-30 (SPEC-TOY-001).  The
//       256-byte CMOS plus the time origin persist to a backing file
//       (<run-dir>/nvram/toy_cmos.bin; see bindBacking /
//       Machine::loadSrmFirmware).  Loaded at bind (absent/short/
//       corrupt/mode-mismatch -> UNSET + ONE loud line), flushed on
//       every data-port store -- hard kills lose nothing.  Unbound
//       instances (unit tests) remain volatile as before.
//     - G1c (NMI MASK): index bit 7 is dropped (masked with 0x7F),
//       matching the reference.
//     - G1d: no deviation from the B1 latch model.
//
// TIME-SOURCE MODES (SPEC-TOY-001 Sec 4; EMULATR_TOY_MODE env, manifest
// wiring to follow):
//   unset (DEFAULT)  Dead-battery lifecycle.  Fresh state: Reg D VRT
//                    reads 0, clock fields read all-zero BCD (month 0 /
//                    day 0 -- out of range on a real MC146818: two
//                    independent invalidity signals, D-2), and the
//                    clock does NOT advance.  The first successful
//                    guest write (SET protocol, W-2/W-3 below) flips
//                    VRT to 1, starts the clock from the written
//                    value, and flushes the store.  Subsequent runs
//                    load the store and report valid.
//   fixed            Always valid; time = kEpoch (2026-01-01 00:00:00)
//                    + cycles/cyclesPerSecond.  Pure function of guest
//                    cycles -- MANDATORY for do-no-harm / differential
//                    / Oracle gate runs (SPEC-TOY-001 Sec 7), which
//                    must also NOT consume a persisted store.
//   host             Seeded from the host wall clock at construction,
//                    then advances on the cycle source.  Best
//                    usability; BREAKS bit-reproducibility (guest-
//                    visible time is no longer a function of guest
//                    state).  Never valid for a gate run.
//   (offset          Deferred per D-3 until a workload needs it.)
//
// DETERMINISM INVARIANT (now mode-scoped): in unset and fixed modes,
//   time is NEVER read from the host; the clock advances on
//
//       elapsed_seconds = (*cycleSource - originCycles) / cyclesPerSecond
//
//   from a deterministic origin (the epoch, or the guest-written time),
//   so identical boots with identical stores produce byte-identical TOY
//   reads.  The divisor is a constructor argument (default 1e9 = the
//   established ~1 GHz modeled second).  If no cycle source is bound
//   the clock reads its origin, which is still deterministic.
//
// GUEST WRITE PROTOCOL (W-1..W-4, the substantive fix -- guest time
// writes were previously DISCARDED by re-materialization):
//   1. Guest sets Reg B SET=1: updates halt (W-1, pre-existing).
//   2. Guest writes the clock fields (BCD/binary per Reg B DM).
//   3. Guest clears SET: the written fields are decoded and LATCHED as
//      the new time origin (W-2), VRT flips valid, store flushes (W-3).
//   Two-digit year pivot (_PROVISIONAL until a century reader is
//   confirmed -- C-1 found NO console reader; century byte 0x32 is
//   plain NVRAM storage only): yy < 70 -> 20yy, else 19yy.
//   CMOS NVRAM bytes (0x0E-0x7F+) persist independently (W-4); the
//   console really uses 0x0F, 0x22, 0x24/0x25, 0x3E, 0x3F.
//
// THREADING:
//   ioRead/ioWrite are invoked on the CPU thread only (MemDrainer ->
//   chipset -> Pchip I/O port registry).  The bound cycle counter is the
//   CPU's own cycleCount field, read on the same thread; no atomics are
//   required.  (Contrast Kbd8042Stub, which kept atomics for possible
//   cross-thread diagnostics access -- not needed here.)
//
// REFERENCES:
//   Motorola MC146818A datasheet (register map, Reg A-D bit layouts)
//   Cypress CY82C693 datasheet ("RTC Address Map", "External RTC Control")
//   apisrm ref/toy_driver.c, ref/timer.c (krn$_reset_toy), ref/date.c
//   Toy Specification grounding note (uploaded 2026-06-03)
// ============================================================================

#ifndef TOY_RTC_H
#define TOY_RTC_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>     // std::getenv -- EMULATR_TOY_MODE
#include <cstring>
#include <ctime>       // std::time -- host mode seed only
#include <filesystem>  // backing-store parent dir creation
#include <string>

#include "../../chipsetLib/IDeviceHandlers.h"  // IIoPortHandler


// ============================================================================
// ToyRtc -- ports 0x70 (index) / 0x71 (data)
// ============================================================================
class ToyRtc : public IIoPortHandler
{
public:
    // ------------------------------------------------------------------
    // Port and register-index constants (MC146818 register map).
    // ------------------------------------------------------------------
    static constexpr uint16_t kIndexPort   = 0x70;  // write: select register
    static constexpr uint16_t kDataPort    = 0x71;  // read/write: selected reg

    static constexpr uint8_t  kRegSeconds  = 0x00;  // clock: seconds
    static constexpr uint8_t  kRegSecAlarm = 0x01;  // alarm: seconds
    static constexpr uint8_t  kRegMinutes  = 0x02;  // clock: minutes
    static constexpr uint8_t  kRegMinAlarm = 0x03;  // alarm: minutes
    static constexpr uint8_t  kRegHours    = 0x04;  // clock: hours
    static constexpr uint8_t  kRegHrAlarm  = 0x05;  // alarm: hours
    static constexpr uint8_t  kRegDow      = 0x06;  // day of week (1 = Sunday)
    static constexpr uint8_t  kRegDom      = 0x07;  // day of month (1-31)
    static constexpr uint8_t  kRegMonth    = 0x08;  // month (1-12)
    static constexpr uint8_t  kRegYear     = 0x09;  // year (two digits)
    static constexpr uint8_t  kRegA        = 0x0a;  // control A (UIP, divisor)
    static constexpr uint8_t  kRegB        = 0x0b;  // control B (SET/DM/24h..)
    static constexpr uint8_t  kRegC        = 0x0c;  // flags (read clears)
    static constexpr uint8_t  kRegD        = 0x0d;  // VRT (valid RAM and time)
    static constexpr uint8_t  kClockRegEnd = 0x0e;  // first NVRAM index

    // Reg A bits.
    static constexpr uint8_t  kRegA_UIP    = 0x80;  // update in progress

    // Reg B bits.
    static constexpr uint8_t  kRegB_SET    = 0x80;  // halt updates for setting
    static constexpr uint8_t  kRegB_PIE    = 0x40;  // periodic int enable
    static constexpr uint8_t  kRegB_AIE    = 0x20;  // alarm int enable
    static constexpr uint8_t  kRegB_UIE    = 0x10;  // update-ended int enable
    static constexpr uint8_t  kRegB_SQWE   = 0x08;  // square wave enable
    static constexpr uint8_t  kRegB_DM     = 0x04;  // 1 = binary, 0 = BCD
    static constexpr uint8_t  kRegB_H24    = 0x02;  // 1 = 24-hour, 0 = 12-hour
    static constexpr uint8_t  kRegB_DSE    = 0x01;  // daylight saving (unused)

    // Reg D bits.
    static constexpr uint8_t  kRegD_VRT    = 0x80;  // valid RAM and time

    // 12-hour mode PM flag (bit 7 of the hours register).
    static constexpr uint8_t  kHourPmBit   = 0x80;

    // ------------------------------------------------------------------
    // Deterministic epoch for fixed mode / CSERVE get_time:
    // 2026-01-01 00:00:00 (day-of-week derives from the civil math).
    // ------------------------------------------------------------------
    static constexpr int      kEpochYear   = 2026;  // four-digit epoch year

    // Default modeled-second rate.  One named source for the ~1 GHz second,
    // shared by the ctor default and the static get_time helper
    // (timestampMMDDhhmm / CSERVE 0x66) so both time paths agree.  [2026-07-07]
    static constexpr uint64_t kDefaultCyclesPerSecond = 1000000000ull;

    // ------------------------------------------------------------------
    // Construction.  cyclesPerSecond converts the bound cycle counter to
    // elapsed seconds; the default matches the established ~1 GHz modeled
    // second (see DETERMINISM INVARIANT in the header comment).
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // Time-source mode (SPEC-TOY-001 Sec 4).  Selected from
    // EMULATR_TOY_MODE at construction; setTimeMode() is the test hook
    // and re-runs origin initialization for the new mode.
    // ------------------------------------------------------------------
    enum class TimeMode : uint8_t { Unset = 0, Fixed = 1, Host = 2 };

    explicit ToyRtc(uint64_t cyclesPerSecond = kDefaultCyclesPerSecond) noexcept
        : m_cyclesPerSecond(cyclesPerSecond ? cyclesPerSecond : 1ull)
    {
        m_mode = modeFromEnv();
        reset();
    }

    void setTimeMode(TimeMode m) noexcept
    {
        m_mode = m;
        initOriginForMode();
    }
    [[nodiscard]] TimeMode timeMode()  const noexcept { return m_mode; }
    [[nodiscard]] bool     timeValid() const noexcept { return m_timeValid; }

    // Bind the emulated cycle counter (CpuState::cycleCount).  CPU-thread
    // only; the pointer is read at index-write time (B2 lazy fill).
    void bindCycleSource(uint64_t const* cycleCounter) noexcept
    {
        m_cycleSource = cycleCounter;
    }

    // ------------------------------------------------------------------
    // get_time (CSERVE func 0x66) return value -- the packed TOY timestamp
    // sys__get_timestamp produces (ev6_vms_pc264_pal.mar:5336):
    //   R0 = (month << 24) | (day << 16) | (hour << 8) | minute
    // each byte in the RTC's encoding.  The SRM runs the TOY in BCD /
    // 24-hour, so this encodes BCD, 24-hour.  DETERMINISTIC (cycle-derived
    // from the same epoch as the RTC port path) -- NEVER host wall-clock,
    // which would break the AXPBox byte-identical oracle.  Consumed by
    // execCserve (palBoxLib) for the ES40 memory-test timing primitive at
    // guest 0x8c2d0 (return = input - get_time()).  Self-consistent for the
    // firmware's elapsed-time deltas; if it must byte-match a direct RTC read
    // in a non-24h/binary mode, honor Reg B here.  [2026-07-07]
    // ------------------------------------------------------------------
    static uint32_t timestampMMDDhhmm(
        uint64_t cycles,
        uint64_t cyclesPerSecond = kDefaultCyclesPerSecond) noexcept
    {
        CalFields const f = calendarFromCycles(cycles, cyclesPerSecond);
        uint8_t const mo = encode(static_cast<uint8_t>(f.mon),  false);  // BCD
        uint8_t const dd = encode(static_cast<uint8_t>(f.dom),  false);
        uint8_t const hh = encode(static_cast<uint8_t>(f.hour), false);  // 24-hour
        uint8_t const mm = encode(static_cast<uint8_t>(f.min),  false);
        return (static_cast<uint32_t>(mo) << 24)
             | (static_cast<uint32_t>(dd) << 16)
             | (static_cast<uint32_t>(hh) <<  8)
             |  static_cast<uint32_t>(mm);
    }

    // Cold state: everything zero, latch clear, index 0, then the
    // mode's origin policy (unset -> invalid; fixed/host -> valid from
    // epoch / host seed).  A bound backing store re-loads AFTER reset
    // via bindBacking, matching real CMOS surviving a machine reset.
    void reset() noexcept
    {
        std::memset(m_cmos, 0, sizeof(m_cmos));
        m_index = 0;
        m_latch = 0;
        initOriginForMode();
    }

    // ------------------------------------------------------------------
    // Backing store (SPEC-TOY-001 Sec 6): <run-dir>/nvram/toy_cmos.bin.
    // Header: magic "TOY1", version, writer mode, valid flag, then the
    // absolute origin seconds at flush time and the 256-byte CMOS.
    // Absent / short / corrupt / mode-mismatch -> UNSET semantics plus
    // exactly ONE loud stderr line naming the reason (hard-stop over
    // silent degradation).  Time is frozen across downtime (no host
    // coupling): on load the stored seconds become the origin at the
    // CURRENT cycle count.
    // ------------------------------------------------------------------
    void bindBacking(std::string path) noexcept
    {
        m_backingPath = std::move(path);
        loadBacking();
    }
    [[nodiscard]] std::string const& backingPath() const noexcept { return m_backingPath; }

    // ------------------------------------------------------------------
    // IIoPortHandler -- read.
    //   0x70 -> last index written (reference latch model: the access-
    //           port byte simply holds what was stored there).
    //   0x71 -> the LATCH filled at index-write time (B1).
    // Non-byte widths float (the RTC is byte-only; AXPBox FAILUREs them).
    // ------------------------------------------------------------------
    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        if (width != 1) {
            noteNonByteAccess();                     // one-shot diagnostic
            return 0xFFull;                          // floating ISA bus
        }
        switch (port) {
        case 0x70:  return static_cast<uint64_t>(m_index);
        case 0x71:  return static_cast<uint64_t>(m_latch);
        default:    return 0xFFull;                  // not ours / floating
        }
    }

    // ------------------------------------------------------------------
    // IIoPortHandler -- write.
    //   0x70 -> select register (bit 7 dropped, G1c).  Side effects:
    //           B2 lazy time materialization when a clock index is
    //           selected, Reg C clear-on-read staging, then fill the
    //           data-port latch from the selected register (B1).
    //   0x71 -> store to the selected register, honoring read-only and
    //           control-register semantics.
    // ------------------------------------------------------------------
    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        if (width != 1) {
            noteNonByteAccess();                     // one-shot diagnostic
            return;                                  // byte-only device
        }
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);

        switch (port) {
        case 0x70: {
            m_index = static_cast<uint8_t>(v & 0x7Fu);   // G1c: drop NMI bit

            // B2: selecting any clock register refreshes the clock from
            // the deterministic time source -- unless Reg B SET is held
            // (firmware is staging a time write; let its bytes stand).
            if (m_index < kClockRegEnd &&
                (m_cmos[kRegB] & kRegB_SET) == 0) {
                materializeClock();
            }

            // Fill the data-port latch from the selected register (B1),
            // applying per-register read semantics.
            m_latch = readRegisterForLatch(m_index);
            break;
        }
        case 0x71: {
            writeRegister(m_index, v);
            // Keep the latch coherent with what a subsequent data read
            // should observe (the reference stores through and the next
            // index write re-fills anyway; this just avoids a stale byte
            // if firmware read-back-checks without re-indexing).
            m_latch = readRegisterForLatch(m_index);
            break;
        }
        default:
            break;                                   // not ours: drop
        }
    }

    // Diagnostic accessors (tests + future SHOW TOY style dumps).
    uint8_t currentIndex() const noexcept { return m_index; }
    uint8_t cmosByte(uint8_t idx) const noexcept { return m_cmos[idx]; }

private:
    // ------------------------------------------------------------------
    // Per-register READ semantics feeding the data-port latch.
    //   Reg A : stored value with UIP forced low (G1a always-low).
    //   Reg C : current flags, then clear (read-clears).  No interrupt
    //           path is modeled yet, so this is always 0 today; the
    //           structure is kept so the IRQ-8 work drops in cleanly.
    //   Reg D : VRT reflects the time-valid state (SPEC-TOY-001).  In
    //           unset mode with no store and no guest write it reads 0
    //           (dead battery); the SRM checks this and a clear VRT
    //           sends date.c down the "TOY dead" path -- which is the
    //           faithful signal.  fixed/host modes and any state after
    //           a successful guest SET-protocol write read 0x80.
    //   others: stored byte (clock regs were refreshed at index time).
    // ------------------------------------------------------------------
    uint8_t readRegisterForLatch(uint8_t idx) noexcept
    {
        switch (idx) {
        case kRegA: return static_cast<uint8_t>(m_cmos[kRegA] & ~kRegA_UIP);
        case kRegC: {
            uint8_t const flags = m_cmos[kRegC];
            m_cmos[kRegC] = 0;                       // read clears
            return flags;
        }
        case kRegD: return m_timeValid ? kRegD_VRT : 0u;
        default:    return m_cmos[idx];
        }
    }

    // ------------------------------------------------------------------
    // Per-register WRITE semantics.
    //   Reg C / Reg D are read-only on the real part: writes dropped.
    //   Reg A bit 7 (UIP) is read-only: masked off on store.
    //   Reg B: stores through, and the SET 1->0 falling edge latches
    //          the staged clock fields as the new time origin (W-2),
    //          marks the time valid (W-3), and flushes the store.
    //   Everything else (clock, alarms, NVRAM) stores through; any
    //   store to a bound instance flushes (W-4 -- writes are rare and
    //   the file is 264 bytes, so flush-on-write costs nothing and
    //   survives hard kills).
    // ------------------------------------------------------------------
    void writeRegister(uint8_t idx, uint8_t v) noexcept
    {
        switch (idx) {
        case kRegC:                                  // read-only
        case kRegD:                                  // read-only
            return;                                  // no store, no flush
        case kRegA:
            m_cmos[kRegA] = static_cast<uint8_t>(v & ~kRegA_UIP);
            break;
        case kRegB: {
            bool const setWasHeld = (m_cmos[kRegB] & kRegB_SET) != 0;
            m_cmos[kRegB] = v;
            if (setWasHeld && (v & kRegB_SET) == 0) {
                captureGuestTime();                  // W-2 + W-3
            }
            break;
        }
        default:
            m_cmos[idx] = v;
            break;
        }
        flushBacking();
    }

    // ------------------------------------------------------------------
    // B2 lazy time: fill sec/min/hour/dow/dom/month/year from the
    // deterministic time source, honoring Reg B DM (binary vs BCD) and
    // H24 (24 vs 12 hour).  Alarm registers are untouched.
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // Pure calendar fields from a deterministic cycle count.  ONE source of
    // truth for both the lazy RTC materialization (port reads) and the static
    // get_time helper (timestampMMDDhhmm / CSERVE 0x66).  No device or host
    // state.  cyclesPerSecond converts cycles -> seconds.  [2026-07-07]
    // ------------------------------------------------------------------
    struct CalFields { int sec; int min; int hour; int dow; int dom; int mon; int year; };

    // ------------------------------------------------------------------
    // Civil-calendar math (Howard Hinnant's algorithms, public domain):
    // absolute days <-> y/m/d, exact for the whole Gregorian range.
    // Replaces the forward-walking loop so a guest-written origin in
    // ANY year (SET TIME to 2005, 1999, ...) derives correctly.
    // Absolute seconds are civil seconds since 1970-01-01 00:00:00.
    // ------------------------------------------------------------------
    static constexpr int64_t daysFromCivil(int y, int m, int d) noexcept
    {
        y -= (m <= 2);
        int64_t const era = (y >= 0 ? y : y - 399) / 400;
        unsigned const yoe = static_cast<unsigned>(y - era * 400);
        unsigned const doy = static_cast<unsigned>(
            (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
        unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

    static CalFields calendarFromAbsSeconds(int64_t secs) noexcept
    {
        int64_t days = secs / 86400;
        int64_t rem  = secs % 86400;
        if (rem < 0) { rem += 86400; --days; }

        CalFields f{};
        f.sec  = static_cast<int>(rem % 60);
        f.min  = static_cast<int>((rem / 60) % 60);
        f.hour = static_cast<int>(rem / 3600);

        int64_t const z   = days + 719468;
        int64_t const era = (z >= 0 ? z : z - 146096) / 146097;
        unsigned const doe = static_cast<unsigned>(z - era * 146097);
        unsigned const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int64_t  const y   = static_cast<int64_t>(yoe) + era * 400;
        unsigned const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        unsigned const mp  = (5 * doy + 2) / 153;
        unsigned const d   = doy - (153 * mp + 2) / 5 + 1;
        unsigned const m   = mp < 10 ? mp + 3 : mp - 9;
        f.year = static_cast<int>(y + (m <= 2));
        f.mon  = static_cast<int>(m);
        f.dom  = static_cast<int>(d);
        // 1970-01-01 was a Thursday; MC146818 convention 1 = Sunday.
        f.dow  = 1 + static_cast<int>(((days % 7) + 7 + 4) % 7);
        return f;
    }

    // Epoch (fixed mode / CSERVE get_time origin): 2026-01-01 00:00:00
    // = 1767225600 civil seconds since 1970-01-01.  Precomputed literal:
    // an in-class constexpr member function cannot initialize a static
    // member of its own (incomplete) class.  The value is pinned by the
    // materialization doctests (epoch + 90061 s == 2026-01-02 01:01:01).
    static constexpr int64_t kEpochAbsSeconds = 1767225600ll;

    // Legacy shape kept for timestampMMDDhhmm (CSERVE 0x66): pure
    // epoch + cycles, identical results to the pre-2026-07-30 walker.
    static CalFields calendarFromCycles(uint64_t cycles,
                                        uint64_t cyclesPerSecond) noexcept
    {
        uint64_t const elapsed = cycles / (cyclesPerSecond ? cyclesPerSecond : 1ull);
        return calendarFromAbsSeconds(
            kEpochAbsSeconds + static_cast<int64_t>(elapsed));
    }

    void materializeClock() noexcept
    {
        if (!m_timeValid) {
            // Unset mode before any successful guest write: the clock
            // fields read all-zero BCD -- month 0 / day 0 are out of
            // range on a real MC146818, so a guest that ignores VRT
            // still sees an invalid time (D-2, two independent
            // signals) -- and the clock does NOT advance.
            m_cmos[kRegSeconds] = 0; m_cmos[kRegMinutes] = 0;
            m_cmos[kRegHours]   = 0; m_cmos[kRegDow]     = 0;
            m_cmos[kRegDom]     = 0; m_cmos[kRegMonth]   = 0;
            m_cmos[kRegYear]    = 0;
            return;
        }

        uint64_t const cycles = m_cycleSource ? *m_cycleSource : 0ull;
        int64_t const elapsed = (cycles >= m_originCycles)
            ? static_cast<int64_t>((cycles - m_originCycles) / m_cyclesPerSecond)
            : 0;
        CalFields const f = calendarFromAbsSeconds(m_originSeconds + elapsed);

        bool const binary = (m_cmos[kRegB] & kRegB_DM)  != 0;
        bool const h24    = (m_cmos[kRegB] & kRegB_H24) != 0;

        // Hours register: 24-hour straight, or 12-hour with PM in bit 7.
        uint8_t hourReg;
        if (h24) {
            hourReg = encode(static_cast<uint8_t>(f.hour), binary);
        } else {
            int const h12 = (f.hour % 12 == 0) ? 12 : (f.hour % 12);
            hourReg = encode(static_cast<uint8_t>(h12), binary);
            if (f.hour >= 12) {
                hourReg = static_cast<uint8_t>(hourReg | kHourPmBit);
            }
        }

        m_cmos[kRegSeconds] = encode(static_cast<uint8_t>(f.sec), binary);
        m_cmos[kRegMinutes] = encode(static_cast<uint8_t>(f.min), binary);
        m_cmos[kRegHours]   = hourReg;
        m_cmos[kRegDow]     = encode(static_cast<uint8_t>(f.dow), binary);
        m_cmos[kRegDom]     = encode(static_cast<uint8_t>(f.dom), binary);
        m_cmos[kRegMonth]   = encode(static_cast<uint8_t>(f.mon), binary);
        m_cmos[kRegYear]    = encode(static_cast<uint8_t>(f.year % 100), binary);
        // Century byte 0x32: NOT synthesized.  C-1 (2026-07-30) found no
        // console reader; it is plain NVRAM -- a guest that writes it
        // keeps it via persistence (W-4).  _PROVISIONAL pending a
        // confirmed OS-side reader.
    }

    // ------------------------------------------------------------------
    // Mode / origin machinery (SPEC-TOY-001 Sec 4).
    // ------------------------------------------------------------------
    static TimeMode modeFromEnv() noexcept
    {
        char const* const e = std::getenv("EMULATR_TOY_MODE");
        if (e != nullptr) {
            if (std::strcmp(e, "fixed") == 0) return TimeMode::Fixed;
            if (std::strcmp(e, "host")  == 0) return TimeMode::Host;
            if (std::strcmp(e, "unset") != 0) {
                std::fprintf(stderr,
                    "ToyRtc: unknown EMULATR_TOY_MODE '%s' -- using unset\n", e);
            }
        }
        return TimeMode::Unset;
    }

    void initOriginForMode() noexcept
    {
        switch (m_mode) {
        case TimeMode::Fixed:
            // Pure function of guest cycles: origin pinned to cycle 0.
            m_originSeconds = kEpochAbsSeconds;
            m_originCycles  = 0;
            m_timeValid     = true;
            break;
        case TimeMode::Host: {
            // Host wall clock (local time, RTC convention) at init;
            // advances on the cycle source thereafter.  Documented
            // determinism break -- never valid for gate runs.
            std::time_t const now = std::time(nullptr);
            std::tm tmv{};
#if defined(_MSC_VER)
            localtime_s(&tmv, &now);
#else
            std::tm* const p = std::localtime(&now);
            if (p != nullptr) tmv = *p;
#endif
            m_originSeconds = daysFromCivil(tmv.tm_year + 1900,
                                            tmv.tm_mon + 1,
                                            tmv.tm_mday) * 86400
                            + tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec;
            m_originCycles  = m_cycleSource ? *m_cycleSource : 0ull;
            m_timeValid     = true;
            break;
        }
        case TimeMode::Unset:
        default:
            m_originSeconds = kEpochAbsSeconds;   // meaningful only after a write
            m_originCycles  = 0;
            m_timeValid     = false;              // dead battery until written
            break;
        }
    }

    // W-2: decode the staged clock fields on the SET 1->0 edge and latch
    // them as the new origin; W-3: mark valid.  A syntactically invalid
    // write (month 0, hour 25, ...) is rejected and validity is left
    // unchanged -- never latch garbage as a valid time.
    void captureGuestTime() noexcept
    {
        bool const binary = (m_cmos[kRegB] & kRegB_DM)  != 0;
        bool const h24    = (m_cmos[kRegB] & kRegB_H24) != 0;

        int const sec = decode(m_cmos[kRegSeconds], binary);
        int const min = decode(m_cmos[kRegMinutes], binary);
        int hour;
        if (h24) {
            hour = decode(m_cmos[kRegHours], binary);
        } else {
            bool const pm = (m_cmos[kRegHours] & kHourPmBit) != 0;
            hour = decode(static_cast<uint8_t>(m_cmos[kRegHours] & ~kHourPmBit),
                          binary) % 12;
            if (pm) hour += 12;
        }
        int const dom = decode(m_cmos[kRegDom],   binary);
        int const mon = decode(m_cmos[kRegMonth], binary);
        int const yy  = decode(m_cmos[kRegYear],  binary);

        if (mon < 1 || mon > 12 || dom < 1 || dom > 31 ||
            hour > 23 || min > 59 || sec > 59 || yy > 99) {
            return;
        }
        // Two-digit year pivot, _PROVISIONAL (C-1: no confirmed century
        // reader): yy < 70 -> 20yy, else 19yy.
        int const year = (yy < 70) ? 2000 + yy : 1900 + yy;

        m_originSeconds = daysFromCivil(year, mon, dom) * 86400
                        + hour * 3600 + min * 60 + sec;
        m_originCycles  = m_cycleSource ? *m_cycleSource : 0ull;
        m_timeValid     = true;
    }

    // Inverse of encode(): BCD or binary per Reg B DM.
    static int decode(uint8_t v, bool binary) noexcept
    {
        if (binary) return static_cast<int>(v);
        return static_cast<int>(((v >> 4) & 0x0Fu) * 10u + (v & 0x0Fu));
    }

    // ------------------------------------------------------------------
    // Backing store I/O (SPEC-TOY-001 Sec 6).  Layout, little-endian:
    //   [0..3]   magic "TOY1"
    //   [4]      version (1)
    //   [5]      writer mode (TimeMode)
    //   [6]      time-valid flag
    //   [7]      pad
    //   [8..15]  absolute origin seconds at flush time (int64)
    //   [16..271] the 256-byte CMOS array
    // ------------------------------------------------------------------
    void loadBacking() noexcept
    {
        if (m_backingPath.empty()) return;
        std::FILE* const f = std::fopen(m_backingPath.c_str(), "rb");
        if (f == nullptr) {
            std::fprintf(stderr,
                "ToyRtc: no CMOS store at '%s' -- starting UNSET/fresh\n",
                m_backingPath.c_str());
            return;
        }
        uint8_t hdr[16] = {};
        uint8_t cmos[sizeof(m_cmos)] = {};
        size_t const gotH = std::fread(hdr, 1, sizeof(hdr), f);
        size_t const gotC = std::fread(cmos, 1, sizeof(cmos), f);
        std::fclose(f);

        if (gotH != sizeof(hdr) || gotC != sizeof(cmos) ||
            std::memcmp(hdr, "TOY1", 4) != 0 || hdr[4] != kBackingVersion) {
            std::fprintf(stderr,
                "ToyRtc: CMOS store '%s' short/corrupt (magic/version) -- "
                "ignored, starting UNSET/fresh\n", m_backingPath.c_str());
            return;
        }
        if (hdr[5] != static_cast<uint8_t>(m_mode)) {
            std::fprintf(stderr,
                "ToyRtc: CMOS store '%s' written by mode %u, current mode %u "
                "-- ignored (a fixed-gate run never consumes a persisted "
                "store, and vice versa)\n", m_backingPath.c_str(),
                static_cast<unsigned>(hdr[5]),
                static_cast<unsigned>(m_mode));
            return;
        }

        std::memcpy(m_cmos, cmos, sizeof(m_cmos));
        m_cmos[kRegC] = 0;                                       // volatile
        m_cmos[kRegA] = static_cast<uint8_t>(m_cmos[kRegA] & ~kRegA_UIP);

        int64_t secs = 0;
        std::memcpy(&secs, hdr + 8, sizeof(secs));
        if (hdr[6] != 0) {
            // Time frozen across downtime (no host coupling): the
            // stored seconds become the origin at the CURRENT cycles.
            m_originSeconds = secs;
            m_originCycles  = m_cycleSource ? *m_cycleSource : 0ull;
            m_timeValid     = true;
        }
    }

    void flushBacking() noexcept
    {
        if (m_backingPath.empty()) return;

        std::error_code ec;
        auto const parent = std::filesystem::path(m_backingPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);

        uint64_t const cycles = m_cycleSource ? *m_cycleSource : 0ull;
        int64_t const elapsed = (m_timeValid && cycles >= m_originCycles)
            ? static_cast<int64_t>((cycles - m_originCycles) / m_cyclesPerSecond)
            : 0;
        int64_t const secs = m_originSeconds + elapsed;

        uint8_t hdr[16] = {};
        std::memcpy(hdr, "TOY1", 4);
        hdr[4] = kBackingVersion;
        hdr[5] = static_cast<uint8_t>(m_mode);
        hdr[6] = m_timeValid ? 1u : 0u;
        std::memcpy(hdr + 8, &secs, sizeof(secs));

        std::FILE* const f = std::fopen(m_backingPath.c_str(), "wb");
        if (f == nullptr) {
            static bool s_noted = false;
            if (!s_noted) {
                s_noted = true;
                std::fprintf(stderr,
                    "ToyRtc: cannot write CMOS store '%s' -- persistence "
                    "disabled this run\n", m_backingPath.c_str());
            }
            return;
        }
        std::fwrite(hdr, 1, sizeof(hdr), f);
        std::fwrite(m_cmos, 1, sizeof(m_cmos), f);
        std::fclose(f);
    }

    static constexpr uint8_t kBackingVersion = 1;

    // Binary or BCD encode per Reg B DM.
    static uint8_t encode(uint8_t v, bool binary) noexcept
    {
        if (binary) {
            return v;
        }
        return static_cast<uint8_t>(((v / 10u) << 4) | (v % 10u));
    }

    // One-shot stderr note for non-byte accesses (byte-only device).
    static void noteNonByteAccess() noexcept
    {
        static bool s_noted = false;
        if (!s_noted) {
            s_noted = true;
            std::fprintf(stderr,
                "ToyRtc: non-byte access dropped (RTC is byte-only); "
                "further occurrences silent\n");
            std::fflush(stderr);
        }
    }

    uint8_t         m_cmos[256];                     // CMOS array (persisted when bound)
    uint8_t         m_index{0};                      // selected register
    uint8_t         m_latch{0};                      // data-port latch (B1)
    uint64_t const* m_cycleSource{nullptr};          // CpuState::cycleCount
    uint64_t        m_cyclesPerSecond;               // cycles -> seconds

    // SPEC-TOY-001 state.
    TimeMode        m_mode{TimeMode::Unset};         // EMULATR_TOY_MODE
    bool            m_timeValid{false};              // Reg D VRT truth
    int64_t         m_originSeconds{0};              // abs civil secs at origin
    uint64_t        m_originCycles{0};               // cycles at origin
    std::string     m_backingPath;                   // empty = volatile
};

#endif // TOY_RTC_H
