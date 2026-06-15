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
//     - G1b (PERSISTENCE): volatile CMOS, zero-initialized each cold
//       boot, matching the AXPBox reference.  Durable configuration
//       lives in the FlashRom device, not here (realm boundary).
//     - G1c (NMI MASK): index bit 7 is dropped (masked with 0x7F),
//       matching the reference.
//     - G1d: no deviation from the B1 latch model.
//
// DETERMINISM INVARIANT (the one deliberate deviation from the spec's
// host-clock model):
//   Time is NEVER read from the host.  The clock is derived from the
//   emulated cycle counter:
//
//       elapsed_seconds = *cycleSource / cyclesPerSecond
//       time            = kEpoch (2026-01-01 00:00:00, Thursday) + elapsed
//
//   so identical boots produce byte-identical TOY reads.  The divisor is
//   a constructor argument (default 1,000,000,000 = the established
//   ~1 GHz modeled second); emulating a different clock rate means
//   passing a different divisor -- no formula change.  This knob belongs
//   in the planned "GHz-coupled interfaces" document (task #10) together
//   with the Cchip interval-timer mask, RSCC multiplier, and warp
//   constants.  If no cycle source is bound the clock reads the epoch,
//   which is still deterministic.
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
#include <cstring>

#include "chipsetLib/IDeviceHandlers.h"  // IIoPortHandler


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
    // Deterministic epoch: 2026-01-01 00:00:00 was a Thursday.
    // MC146818 day-of-week convention is 1 = Sunday, so Thursday = 5.
    // ------------------------------------------------------------------
    static constexpr int      kEpochYear   = 2026;  // four-digit epoch year
    static constexpr int      kEpochDow    = 5;     // Thursday (1 = Sunday)

    // ------------------------------------------------------------------
    // Construction.  cyclesPerSecond converts the bound cycle counter to
    // elapsed seconds; the default matches the established ~1 GHz modeled
    // second (see DETERMINISM INVARIANT in the header comment).
    // ------------------------------------------------------------------
    explicit ToyRtc(uint64_t cyclesPerSecond = 1000000000ull) noexcept
        : m_cyclesPerSecond(cyclesPerSecond ? cyclesPerSecond : 1ull)
    {
        reset();
    }

    // Bind the emulated cycle counter (CpuState::cycleCount).  CPU-thread
    // only; the pointer is read at index-write time (B2 lazy fill).
    void bindCycleSource(uint64_t const* cycleCounter) noexcept
    {
        m_cycleSource = cycleCounter;
    }

    // Volatile CMOS (G1b): everything zero, latch clear, index 0.
    void reset() noexcept
    {
        std::memset(m_cmos, 0, sizeof(m_cmos));
        m_index = 0;
        m_latch = 0;
    }

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
    //   Reg D : VRT always set -- "battery good, RAM and time valid".
    //           The SRM checks this; a clear VRT sends date.c down the
    //           "TOY dead" path.
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
        case kRegD: return kRegD_VRT;                // always valid
        default:    return m_cmos[idx];
        }
    }

    // ------------------------------------------------------------------
    // Per-register WRITE semantics.
    //   Reg C / Reg D are read-only on the real part: writes dropped.
    //   Reg A bit 7 (UIP) is read-only: masked off on store.
    //   Everything else (clock, alarms, Reg B, NVRAM) stores through.
    // ------------------------------------------------------------------
    void writeRegister(uint8_t idx, uint8_t v) noexcept
    {
        switch (idx) {
        case kRegC:                                  // read-only
        case kRegD:                                  // read-only
            break;
        case kRegA:
            m_cmos[kRegA] = static_cast<uint8_t>(v & ~kRegA_UIP);
            break;
        default:
            m_cmos[idx] = v;
            break;
        }
    }

    // ------------------------------------------------------------------
    // B2 lazy time: fill sec/min/hour/dow/dom/month/year from the
    // deterministic time source, honoring Reg B DM (binary vs BCD) and
    // H24 (24 vs 12 hour).  Alarm registers are untouched.
    // ------------------------------------------------------------------
    void materializeClock() noexcept
    {
        uint64_t const cycles  = m_cycleSource ? *m_cycleSource : 0ull;
        uint64_t const elapsed = cycles / m_cyclesPerSecond;

        // Split elapsed seconds into day count + time of day.
        uint64_t const daySecs = elapsed % 86400ull;
        uint64_t       days    = elapsed / 86400ull;
        int const sec  = static_cast<int>(daySecs % 60ull);
        int const min  = static_cast<int>((daySecs / 60ull) % 60ull);
        int const hour = static_cast<int>(daySecs / 3600ull);

        // Walk the calendar forward from the epoch.  Boot runs cover
        // minutes of modeled time, so the simple loop is never hot.
        int year = kEpochYear;                       // four-digit working year
        int mon  = 1;                                // January
        int dom  = 1;                                // walking day-of-month
        int remaining = static_cast<int>(days);      // whole days to consume
        for (;;) {
            int const dim = daysInMonth(year, mon);
            if (remaining < dim - (dom - 1)) {       // fits in this month
                dom += remaining;
                break;
            }
            remaining -= dim - (dom - 1);            // consume rest of month
            dom = 1;
            if (++mon > 12) { mon = 1; ++year; }
        }
        int const dow = 1 + ((kEpochDow - 1) + static_cast<int>(days % 7ull)) % 7;

        bool const binary = (m_cmos[kRegB] & kRegB_DM)  != 0;
        bool const h24    = (m_cmos[kRegB] & kRegB_H24) != 0;

        // Hours register: 24-hour straight, or 12-hour with PM in bit 7.
        uint8_t hourReg;
        if (h24) {
            hourReg = encode(static_cast<uint8_t>(hour), binary);
        } else {
            int const h12 = (hour % 12 == 0) ? 12 : (hour % 12);
            hourReg = encode(static_cast<uint8_t>(h12), binary);
            if (hour >= 12) {
                hourReg = static_cast<uint8_t>(hourReg | kHourPmBit);
            }
        }

        m_cmos[kRegSeconds] = encode(static_cast<uint8_t>(sec), binary);
        m_cmos[kRegMinutes] = encode(static_cast<uint8_t>(min), binary);
        m_cmos[kRegHours]   = hourReg;
        m_cmos[kRegDow]     = encode(static_cast<uint8_t>(dow), binary);
        m_cmos[kRegDom]     = encode(static_cast<uint8_t>(dom), binary);
        m_cmos[kRegMonth]   = encode(static_cast<uint8_t>(mon), binary);
        m_cmos[kRegYear]    = encode(static_cast<uint8_t>(year % 100), binary);
        // NOTE: no century byte is written.  PC CMOS convention parks the
        // century at NVRAM index 0x32; wire it here if SRM SHOW DATE ever
        // reports the wrong century.
    }

    // Gregorian month length.
    static int daysInMonth(int year, int mon) noexcept
    {
        static int const kDim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (mon == 2 &&
            ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            return 29;                               // leap February
        }
        return kDim[mon - 1];
    }

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

    uint8_t         m_cmos[256];                     // volatile CMOS (G1b)
    uint8_t         m_index{0};                      // selected register
    uint8_t         m_latch{0};                      // data-port latch (B1)
    uint64_t const* m_cycleSource{nullptr};          // CpuState::cycleCount
    uint64_t        m_cyclesPerSecond;               // cycles -> seconds
};

#endif // TOY_RTC_H
