// ============================================================================
// MinimalIsaStub.h -- 8042 keyboard controller + MC146818 RTC stubs (idle-ready)
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
//   Minimal-functionality stubs for the two ISA legacy devices the SRM
//   firmware polls after the chipset probe sweep but before emitting its
//   console banner:
//     - 8042 keyboard / mouse controller   (ports 0x60-0x64)
//     - MC146818 real-time clock + NVRAM   (ports 0x70-0x71)
//
//   Without these, the firmware spins on port 0x64 waiting for the
//   controller-self-test status bit and on port 0x71 for a CMOS time/date
//   probe, never reaching the COM1 emit path.  Each stub returns the
//   canonical "idle, ready, nothing pending" value on read and records
//   command/data writes for future state-machine work but takes no other
//   action.
//
//   Introduced 2026-05-28 as the immediate unblocker after the UART
//   routing was verified working post `kPfnWidth = 32` fix in
//   pteLib/AlphaPte.h.  See [[phase-b-verified-and-os-pal-takeover]]
//   memory and task list for the planned full implementations.
//
// FUTURE WORK (DEFERRED -- tracked as separate tasks):
//
//   8042 keyboard controller:
//     - Real command parsing: 0xAA self-test (returns 0x55), 0xAB
//       interface test, 0xAD disable KBD, 0xAE enable KBD, 0xC0 read
//       input port, 0xD0/0xD1 read/write output port, 0xD4 send AUX
//       command, etc.  Each command updates an internal state machine
//       and possibly enqueues a data byte for the next 0x60 read.
//     - KBD-AUX channel switching (which side of the controller is the
//       next 0x60 data byte coming from / going to).
//     - IRQ generation (IRQ 1 = KBD ready, IRQ 12 = AUX ready) via the
//       ISA bridge -- requires the bridge to expose an "assert IRQ N"
//       method that lands on the Cchip DRIRn line per PC264 wiring.
//     - PS/2 keyboard / mouse backend interface (IKbdDevice /
//       IMouseDevice) for actual interactive input.
//
//   MC146818 RTC:
//     - Real time / date.  Two modes: (a) live host clock for casual
//       interactive use, (b) cycle-deterministic synthesized clock for
//       snapshot-reproducible boots.  V4 convention favors (b) per
//       [[v4-scope-discipline]] (skip "real" until a use case demands it).
//     - NVRAM cell storage for CMOS settings (50 bytes user-visible at
//       indexes 0x0E-0x7F).  Persistable so SRM configuration survives
//       across cold boots when desired.
//     - Register A (rate select / UIP), Register B (interrupt enables,
//       binary/BCD, 12/24 hr), Register C (interrupt flags), Register D
//       (valid RAM and time).
//     - IRQ 8 generation for periodic / alarm / update-ended interrupts
//       via the ISA bridge.
//
// INTERFACE:
//   Both classes implement IIoPortHandler.  They are registered with the
//   TsunamiPchip I/O port registry by TsunamiChipset::wireDevices() at
//   the same point COM1/COM2 are registered.  The Pchip dispatches each
//   I/O port access to whichever handler claims the port range.
//
// REFERENCES:
//   Intel 8042 datasheet / Cypress CY82C693 ISA bridge (8042 is integrated)
//   Motorola MC146818 / 146818A RTC datasheet
// ============================================================================

#ifndef MINIMAL_ISA_STUB_H
#define MINIMAL_ISA_STUB_H

#include <atomic>
#include <cstdint>

#include "chipsetLib/IDeviceHandlers.h"  // IIoPortHandler


// ============================================================================
// Kbd8042Stub -- ports 0x60-0x64 (8042 keyboard/mouse controller)
// ============================================================================
//
// READ behavior:
//   0x60  data port           -> queued response byte (0x55 after 0xAA
//                                self-test) and clears OBF; else 0x00
//   0x61  system control      -> 0x00  (NMI mask / speaker control; not modeled)
//   0x62  reserved            -> 0x00
//   0x63  reserved            -> 0x00
//   0x64  status register     -> kStatusIdleReady | (OBF bit if response
//                                queued from a prior controller command)
//
// WRITE behavior:
//   0x60  data byte (KBD/AUX command argument) -> recorded in m_lastDataWrite
//   0x64  controller command                   -> recorded in m_lastCmdWrite;
//         if value == 0xAA (controller self-test), queue 0x55 for the
//         next 0x60 read and assert OBF in subsequent 0x64 reads
//   others                                     -> ignored
//
// CHANGE 2026-05-28: minimal 0xAA self-test handshake added.  Without
// this, the SRM firmware sat at IPL 0x1F polling 0x64 every ~266K cycles
// waiting for OBF=1 + 0x55 on 0x60 to indicate the 8042 passed its
// power-on self-test.  See task #49 trace analysis: PC 0x1c69e8 LDBU
// from PA 0x801_fc00_0064 returning 0x04 in a tight RPCC-delay loop
// without any of the standard chipset/timer/UART activity.  This is the
// single minimum command-response pair needed to unblock; the full
// command state machine remains task #45.  Other commands recorded but
// not yet acted on -- they leave OBF clear, which is the safe default
// (SRM treats "no response" as either success or skipped depending on
// the command class).
// ============================================================================
class Kbd8042Stub : public IIoPortHandler
{
public:
    // 8042 status register (port 0x64 read) bit positions.
    static constexpr uint8_t kStatus_OBF     = 0x01;  // Output buffer full
    static constexpr uint8_t kStatus_IBF     = 0x02;  // Input buffer full
    static constexpr uint8_t kStatus_SYS     = 0x04;  // System flag (POST OK)
    static constexpr uint8_t kStatus_CMD     = 0x08;  // 1=last write was cmd
    static constexpr uint8_t kStatus_INH     = 0x10;  // Keyboard inhibited
    static constexpr uint8_t kStatus_AUX     = 0x20;  // AUX data in OBF
    static constexpr uint8_t kStatus_TIMEOUT = 0x40;
    static constexpr uint8_t kStatus_PARITY  = 0x80;

    // Idle-ready: SYS=1 (POST passed) only; OBF/IBF clear, KBD not inhibited.
    // Firmware reads "controller present, healthy, nothing happening".
    static constexpr uint8_t kStatusIdleReady = kStatus_SYS;  // 0x04

    Kbd8042Stub() noexcept = default;

    uint64_t ioRead(uint16_t port, uint8_t /*width*/) override
    {
        switch (port) {
        case 0x60: {
            // Data port read.  If a response byte was queued by a prior
            // controller command (currently only 0xAA self-test queues
            // 0x55), deliver it and clear the OBF pending flag.  Real
            // hardware also clears the OBF bit in the status register
            // as a side effect of the data-port read; the m_obfPending
            // store handles that for the next 0x64 read.
            uint8_t const pending =
                m_obfPending.load(std::memory_order_acquire);
            if (pending) {
                uint8_t const data =
                    m_queuedData.load(std::memory_order_relaxed);
                m_obfPending.store(0, std::memory_order_release);
                return static_cast<uint64_t>(data);
            }
            return 0x00;
        }
        case 0x61:  return 0x00;                                       // sysctl
        case 0x62:  return 0x00;                                       // rsvd
        case 0x63:  return 0x00;                                       // rsvd
        case 0x64: {
            // Status register.  Idle-ready baseline (SYS=1) plus OBF
            // bit if a response byte is queued and not yet drained.
            uint8_t status = kStatusIdleReady;
            if (m_obfPending.load(std::memory_order_acquire)) {
                status |= kStatus_OBF;
            }
            return static_cast<uint64_t>(status);
        }
        default:    return 0xFFULL;                                    // float
        }
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t /*width*/) override
    {
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);
        switch (port) {
        case 0x60:
            // Data write: future cmd argument or KBD/AUX byte.  Record
            // for the planned command-response state machine.
            m_lastDataWrite.store(v, std::memory_order_relaxed);
            break;
        case 0x64:
            // Controller command.  Record for diagnostics, then check
            // the single command we currently implement: 0xAA = 8042
            // controller self-test.  Real hardware spends a few hundred
            // microseconds in self-test then queues 0x55 on the data
            // port with OBF asserted to indicate "passed".  Our stub
            // queues 0x55 immediately -- the firmware's RPCC delay
            // loop bounds how long it waits before re-polling, so an
            // instantaneous response is fine.  Other commands leave
            // m_obfPending clear, which the firmware will read as
            // "controller still busy" until a real implementation lands
            // (task #45).
            m_lastCmdWrite.store(v, std::memory_order_relaxed);
            if (v == 0xAA) {
                m_queuedData.store(0x55, std::memory_order_relaxed);
                m_obfPending.store(1, std::memory_order_release);
            }
            break;
        default:
            // 0x61-0x63 writes: silently ignored in stub
            break;
        }
    }

    // Accessors for diagnostics / future state machine.
    uint8_t lastDataWrite() const noexcept {
        return m_lastDataWrite.load(std::memory_order_relaxed);
    }
    uint8_t lastCmdWrite() const noexcept {
        return m_lastCmdWrite.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint8_t> m_lastDataWrite{0x00};
    std::atomic<uint8_t> m_lastCmdWrite{0x00};

    // 2026-05-28: minimal command-response state for 0xAA self-test.
    // m_queuedData holds the byte the next 0x60 read will return.
    // m_obfPending mirrors the status-register OBF bit -- set when a
    // queued response is waiting, cleared when the data port is read.
    // Full command state machine is task #45.
    std::atomic<uint8_t> m_queuedData{0x00};
    std::atomic<uint8_t> m_obfPending{0x00};
};


// ============================================================================
// Mc146818RtcStub -- ports 0x70-0x71 (MC146818 RTC + 64 byte NVRAM)
// ============================================================================
//
// Two-port indexed register file.  CPU writes the desired RTC register
// number (0..127) to port 0x70, then reads/writes the selected register
// via port 0x71.  Real hardware has 128 registers: indexes 0x00-0x0D
// are clock fields (seconds, minutes, hours, etc. + four control
// registers), 0x0E-0x7F are 50 bytes of battery-backed NVRAM for CMOS
// configuration storage.
//
// READ behavior:
//   0x70 -> 0x00 (index latch is write-only on real hardware)
//   0x71 -> 0x00 (no time / no NVRAM contents in stub)
//
// WRITE behavior:
//   0x70 -> stores low 7 bits as register index for the next 0x71 access
//   0x71 -> ignored
// ============================================================================
class Mc146818RtcStub : public IIoPortHandler
{
public:
    Mc146818RtcStub() noexcept = default;

    uint64_t ioRead(uint16_t port, uint8_t /*width*/) override
    {
        switch (port) {
        case 0x70:  return 0x00;        // index reg is effectively write-only
        case 0x71:  return 0x00;        // stub: no time, no NVRAM
        default:    return 0xFFULL;
        }
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t /*width*/) override
    {
        switch (port) {
        case 0x70:
            m_index.store(static_cast<uint8_t>(value & 0x7Fu),
                          std::memory_order_relaxed);
            break;
        case 0x71:
            // TODO(rtc): apply write to register at m_index when the
            // real RTC + NVRAM model lands.
            break;
        default:
            break;
        }
    }

    uint8_t currentIndex() const noexcept {
        return m_index.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint8_t> m_index{0x00};
};

#endif // MINIMAL_ISA_STUB_H
