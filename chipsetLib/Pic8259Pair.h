// ============================================================================
// Pic8259Pair.h -- Cypress CY82C693 embedded 8259A interrupt controller pair
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE (Increment 2 of the serial-console interrupt design --
// journals/20260604_serial_console_interrupt_design.md, Section 6):
//
//   The CY82C693 ISA bridge embeds a standard master/slave 8259A pair.
//   The DS10 SRM programs it at hardware init (apisrm pc264_io.c:520-571:
//   full ICW1-ICW4 on both controllers, OCW1 masks, non-specific EOIs)
//   and the serial driver's IRQ4 must pass the master's IMR before it
//   may assert the bridge interrupt output -> Cchip DRIR<55>.  The OCW1
//   mask is LOAD-BEARING: during the polled-output boot phase the UART
//   THRE source asserts on every uartIer=0x03 period, and the masked
//   IRQ4 is what keeps those asserts off DRIR<55>.
//
// PROGRAMMED CONFIGURATION (authoritative, cy82c693_def.h:85-135):
//   ICW1 = 0x11  ->  EDGE-TRIGGERED (LTIM=0), cascade, ICW4 required
//   ICW2 = 0x00 (master vector base) / 0x08 (slave vector base)
//   ICW3 = slave on master IR2 / slave ID 2
//   ICW4 = x86 mode, normal EOI
//
// MODEL CONTRACT (the three-layer re-evaluation discipline, design doc
// Section 6 items 3-4):
//
//   * IRR captures the INPUT RISING EDGE regardless of IMR -- the mask
//     gates the output stage, not edge capture.  A masked-while-asserted
//     edge is still pending in IRR; the OCW1 unmask DELIVERS it.  This
//     is the 8259-level analogue of design 4.1 and the actual unstick
//     mechanism for the hand-off sequence (UART THRE asserts first, the
//     driver's last un-gating write opens the path).
//   * The INT output is a LEVEL function -- output() recomputes
//     (IRR & ~IMR & ~inService-suppression) from live state on every
//     call, so OCW1 writes and EOIs re-evaluate by construction.
//   * IN-SERVICE suppression is REAL, not stubbed: acknowledge() (the
//     INTA-cycle seam, called by Machine::run when it stages the device
//     divert) moves the highest-priority pending IRR bit to ISR and
//     drops the output.  While ISR holds a level in service, that level
//     and lower priorities are suppressed; OCW2 EOI clears ISR and the
//     next latched IRR edge re-asserts.  Without this, the level-held
//     output re-diverts the CPU on every step of the NATIVE-mode guest
//     ISR (canAcceptInterrupt gates only PAL mode) -- an interrupt
//     storm.  This is exactly the 8259's architectural job.
//   * Edge mode: a latched IRR bit persists until acknowledge -- the
//     input falling WITHOUT service does NOT clear it (matches silicon;
//     the spurious-IRQ7 refinement is in the TODO table).
//
// PRIORITY: fixed priority 0 (highest) .. 7, master IR2 cascades the
// slave (slave levels report as 8..15).  Rotating priority (OCW2 rotate
// ops) is not modeled -- TODO table.
//
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior
// ============================================================================
// OCW2 rotate   -- rotate-on-EOI / set-priority ops store nothing and
//                  log once; fixed priority covers the traced firmware.
// IRQ7 spurious -- input-drop-before-INTA delivers nothing instead of
//                  the architectural spurious IRQ7.  Wire if a guest
//                  driver is ever observed depending on it.
// ICW4 modes    -- AEOI / buffered / SFNM bits stored, not honored
//                  (pc264 programs normal-EOI x86 mode only).
// ELCR 0x4D0/1  -- stored; per-line level-trigger override not honored
//                  (pc264 init never writes ELCR; ICW1 edge mode rules).
// ============================================================================
//
// INTERFACE IMPLEMENTED:
//   IIoPortHandler (ports 0x20-0x21 master, 0xA0-0xA1 slave,
//                   0x4D0-0x4D1 ELCR), registered by
//   TsunamiChipset::wireDevices.
//
// REFERENCES:
//   Intel 8259A datasheet (Order 231468)
//   apisrm/ref/cy82c693_def.h (programmed defaults) + pc264_io.c:520-571
//   AXPBox AliM1543C.cpp pic_* (working reference, simplified model)
// ============================================================================

#ifndef PIC_8259_PAIR_H
#define PIC_8259_PAIR_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <QDataStream>                      // snapshot serialize seam (v2)
#include "chipsetLib/IDeviceHandlers.h"     // IIoPortHandler

class Pic8259Pair : public IIoPortHandler
{
public:
    static constexpr uint16_t kMasterBase = 0x20;
    static constexpr uint16_t kSlaveBase  = 0xA0;
    static constexpr uint16_t kElcrBase   = 0x4D0;

    static constexpr int      kCascadeIr  = 2;     // slave rides master IR2

    Pic8259Pair() noexcept { reset(); }

    void reset() noexcept
    {
        for (Unit& u : m_unit) {
            u.irr = 0; u.isr = 0; u.imr = 0xFF;   // pre-init: everything off
            u.icw2 = 0; u.icw3 = 0; u.icw4 = 0;
            u.initState = InitState::Idle;
            u.readIsrSelected = false;
            u.pollArmed = false;
            u.ltim = false;
        }
        m_elcr[0] = m_elcr[1] = 0;
        for (bool& lvl : m_lastInput) lvl = false;
    }

    // ========================================================================
    // Interrupt inputs (called by TsunamiChipset::evalDeviceIrqs, step
    // boundary only -- the determinism seam).
    // ========================================================================

    /**
     * @brief Drive one IRQ input line (0-15) with the current level.
     *
     * Edge capture: the RISING edge latches IRR regardless of IMR (the
     * mask gates the output stage only).  Falling edges latch nothing
     * and clear nothing -- a captured edge persists until acknowledge()
     * services it (8259 edge-mode contract).
     */
    void setIrqInput(int line, bool level) noexcept
    {
        int const idx = line & 0xF;
        bool const prev = m_lastInput[idx];
        m_lastInput[idx] = level;
        if (level && !prev) {
            Unit& u = m_unit[idx >> 3];
            u.irr = static_cast<uint8_t>(u.irr | (1u << (idx & 7)));
        }
    }

    /**
     * @brief Bridge INT output -- level-combinational, recomputed per call.
     *
     * Slave first (its unmasked, un-suppressed pending feeds master IR2
     * as a cascade level), then master.  Output is gated on init
     * completion: an unprogrammed PIC drives nothing.
     */
    [[nodiscard]] bool outputAsserted() const noexcept
    {
        return pendingLevel() >= 0;
    }

    /**
     * @brief INTA-cycle seam: acknowledge the highest-priority pending
     *        request.
     *
     * Called by Machine::run at device-divert staging time (the model's
     * INTA).  Moves the winning IRR bit to ISR (suppressing the level
     * until EOI) and returns the programmed vector, or -1 if nothing is
     * pending (spurious -- caller should not have staged).  For a
     * cascaded slave level the slave's IRR/ISR transfer happens too,
     * per the 8259 cascade INTA sequence.
     */
    int acknowledge() noexcept
    {
        int const lvl = pendingLevel();
        // TEMP DIAGNOSTIC -- see ioRead twin above; same removal trigger.
        if (traceEnabled()) {
            std::fprintf(stderr, "PICTRACE IACK -> lvl=%d "
                         "(irr=%02x imr=%02x isr=%02x | s.irr=%02x s.imr=%02x)\n",
                         lvl,
                         m_unit[0].irr, m_unit[0].imr, m_unit[0].isr,
                         m_unit[1].irr, m_unit[1].imr);
            std::fflush(stderr);
        }
        if (lvl < 0) return -1;

        if (lvl < 8) {
            transferIrrToIsr(m_unit[0], lvl);
            return static_cast<int>(m_unit[0].icw2 | static_cast<unsigned>(lvl));
        }
        // Cascade: slave level. Master IR2 + slave IR(lvl-8) both ack.
        transferIrrToIsr(m_unit[0], kCascadeIr);
        transferIrrToIsr(m_unit[1], lvl - 8);
        return static_cast<int>(m_unit[1].icw2 | static_cast<unsigned>(lvl - 8));
    }

    // Observability (doctests + trace digs).
    [[nodiscard]] uint8_t masterIrr() const noexcept { return m_unit[0].irr; }
    [[nodiscard]] uint8_t masterImr() const noexcept { return m_unit[0].imr; }
    [[nodiscard]] uint8_t masterIsr() const noexcept { return m_unit[0].isr; }
    [[nodiscard]] uint8_t slaveIrr()  const noexcept { return m_unit[1].irr; }
    [[nodiscard]] uint8_t slaveImr()  const noexcept { return m_unit[1].imr; }
    [[nodiscard]] bool    initialized() const noexcept
    {
        return m_unit[0].initState == InitState::Ready;
    }

    // ========================================================================
    // IIoPortHandler -- ISA port surface
    // ========================================================================

    uint64_t ioRead(uint16_t port, uint8_t /*width*/) override
    {
        // TEMP DIAGNOSTIC (2026-06-04, EMULATR_PIC_TRACE): full port-level
        // trace of guest<->PIC traffic.  Boot programs the PIC a few
        // hundred times total, so unthrottled is safe.  REMOVAL TRIGGER:
        // delete when the missing-device-ISR-EOI question is settled.
        uint64_t const v = ioReadInner(port);
        if (traceEnabled()) {
            std::fprintf(stderr, "PICTRACE R port=0x%03x -> 0x%02llx "
                         "(irr=%02x imr=%02x isr=%02x | s.irr=%02x s.imr=%02x)\n",
                         port, static_cast<unsigned long long>(v),
                         m_unit[0].irr, m_unit[0].imr, m_unit[0].isr,
                         m_unit[1].irr, m_unit[1].imr);
            std::fflush(stderr);
        }
        return v;
    }

    uint64_t ioReadInner(uint16_t port)
    {
        switch (port) {
        case kMasterBase:     return readBase(m_unit[0]);
        case kMasterBase + 1: return m_unit[0].imr;
        case kSlaveBase:      return readBase(m_unit[1]);
        case kSlaveBase + 1:  return m_unit[1].imr;
        case kElcrBase:       return m_elcr[0];
        case kElcrBase + 1:   return m_elcr[1];
        default:              return 0xFF;
        }
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t /*width*/) override
    {
        uint8_t const v = static_cast<uint8_t>(value & 0xFF);
        // TEMP DIAGNOSTIC -- see ioRead twin above; same removal trigger.
        if (traceEnabled()) {
            std::fprintf(stderr, "PICTRACE W port=0x%03x <- 0x%02x "
                         "(irr=%02x imr=%02x isr=%02x | s.irr=%02x s.imr=%02x)\n",
                         port, v,
                         m_unit[0].irr, m_unit[0].imr, m_unit[0].isr,
                         m_unit[1].irr, m_unit[1].imr);
            std::fflush(stderr);
        }
        switch (port) {
        case kMasterBase:     writeBase(m_unit[0], v, /*master*/true);  break;
        case kMasterBase + 1: writeData(m_unit[0], v, /*master*/true);  break;
        case kSlaveBase:      writeBase(m_unit[1], v, /*master*/false); break;
        case kSlaveBase + 1:  writeData(m_unit[1], v, /*master*/false); break;
        case kElcrBase:       m_elcr[0] = v; break;   // store-only (TODO table)
        case kElcrBase + 1:   m_elcr[1] = v; break;
        default:              break;
        }
    }

    // --------------------------------------------------------------------
    // Snapshot serialization (kChipsetVersion 2, 2026-06-05).
    //
    // Full pair state: both Units (registers + ICW init FSM + OCW3
    // posture), ELCR store, and the 16-line edge-detector cache.
    // m_lastInput must travel: without it a level held high across the
    // snapshot re-edges on restore (spurious IRR capture).
    // Rationale: the firmware programs ICW1-4 exactly once at boot; a
    // restore that loses initState leaves the pair non-Ready and the
    // output permanently deasserted (2026-06-05 deaf-console diagnosis).
    // --------------------------------------------------------------------
    void serialize(QDataStream& ds) const noexcept
    {
        for (int i = 0; i < 2; ++i) {
            Unit const& u = m_unit[i];
            ds << u.irr << u.isr << u.imr
               << u.icw2 << u.icw3 << u.icw4
               << static_cast<quint8>(u.initState)
               << static_cast<quint8>(u.readIsrSelected ? 1 : 0)
               << static_cast<quint8>(u.pollArmed       ? 1 : 0)
               << static_cast<quint8>(u.ltim            ? 1 : 0);
        }
        ds << m_elcr[0] << m_elcr[1];
        for (bool const lvl : m_lastInput)
            ds << static_cast<quint8>(lvl ? 1 : 0);
    }

    void deserialize(QDataStream& ds) noexcept
    {
        for (int i = 0; i < 2; ++i) {
            Unit& u = m_unit[i];
            quint8 init = 0;
            quint8 ris  = 0;
            quint8 poll = 0;
            quint8 ltim = 0;
            ds >> u.irr >> u.isr >> u.imr
               >> u.icw2 >> u.icw3 >> u.icw4
               >> init >> ris >> poll >> ltim;
            u.initState       = static_cast<InitState>(init);
            u.readIsrSelected = (ris  != 0);
            u.pollArmed       = (poll != 0);
            u.ltim            = (ltim != 0);
        }
        ds >> m_elcr[0] >> m_elcr[1];
        for (bool& lvl : m_lastInput) {
            quint8 b = 0;
            ds >> b;
            lvl = (b != 0);
        }
    }

private:
    // TEMP DIAGNOSTIC helper -- env-gated once.  Same removal trigger as
    // the PICTRACE sites above.
    static bool traceEnabled() noexcept
    {
        static bool const s_on = (std::getenv("EMULATR_PIC_TRACE") != nullptr);
        return s_on;
    }

    enum class InitState : uint8_t { Idle, ExpectIcw2, ExpectIcw3,
                                     ExpectIcw4, Ready };

    struct Unit {
        uint8_t   irr;               // edge-latched requests
        uint8_t   isr;               // in-service (set at INTA, cleared by EOI)
        uint8_t   imr;               // OCW1 mask (gates output, NOT capture)
        uint8_t   icw2, icw3, icw4;  // programmed config
        InitState initState;
        bool      readIsrSelected;   // OCW3 RR/RIS: base read returns ISR
        bool      pollArmed;         // OCW3 P: next base read is a poll byte
        bool      ltim;              // ICW1<3> level-trigger (pc264: 0 = edge)
    };

    // --------------------------------------------------------------------
    // Pending resolution -- the level function.
    //
    // Per unit: serviceable = IRR & ~IMR, suppressed by any in-service
    // level of equal-or-higher priority (fixed priority, 0 highest).
    // Master IR2 cascades the slave's result.  Returns the winning
    // GLOBAL level 0-15, or -1 when nothing may assert.
    // --------------------------------------------------------------------
    [[nodiscard]] int unitPending(Unit const& u, uint8_t extraIrr) const noexcept
    {
        if (u.initState != InitState::Ready) return -1;
        uint8_t const req = static_cast<uint8_t>((u.irr | extraIrr) & ~u.imr);
        if (req == 0) return -1;
        for (int lvl = 0; lvl < 8; ++lvl) {
            uint8_t const bit = static_cast<uint8_t>(1u << lvl);
            if (u.isr & bit) return -1;        // equal/higher in service
            if (req & bit)   return lvl;       // highest pending wins
        }
        return -1;
    }

    [[nodiscard]] int pendingLevel() const noexcept
    {
        int const slaveLvl = unitPending(m_unit[1], 0);
        // Cascade: slave pending presents as a level on master IR2.
        uint8_t const cascadeBit =
            (slaveLvl >= 0) ? static_cast<uint8_t>(1u << kCascadeIr) : 0;
        int const masterLvl = unitPending(m_unit[0], cascadeBit);
        if (masterLvl < 0) return -1;
        if (masterLvl == kCascadeIr && slaveLvl >= 0) return 8 + slaveLvl;
        return masterLvl;
    }

    static void transferIrrToIsr(Unit& u, int lvl) noexcept
    {
        uint8_t const bit = static_cast<uint8_t>(1u << (lvl & 7));
        u.irr = static_cast<uint8_t>(u.irr & ~bit);
        u.isr = static_cast<uint8_t>(u.isr | bit);
    }

    // --------------------------------------------------------------------
    // Port behavior
    // --------------------------------------------------------------------

    uint8_t readBase(Unit& u) noexcept
    {
        if (u.pollArmed) {
            // OCW3 poll: one-shot.  Returns 0x80|level when a request is
            // serviceable, else 0.  The poll ALSO acknowledges per the
            // datasheet -- treat it like a software INTA on this unit.
            u.pollArmed = false;
            int const lvl = unitPending(u, 0);
            if (lvl < 0) return 0x00;
            transferIrrToIsr(u, lvl);
            return static_cast<uint8_t>(0x80u | static_cast<unsigned>(lvl));
        }
        return u.readIsrSelected ? u.isr : u.irr;
    }

    void writeBase(Unit& u, uint8_t v, bool master) noexcept
    {
        if (v & 0x10) {                          // ICW1
            u.initState = InitState::ExpectIcw2;
            u.ltim      = (v & 0x08) != 0;
            u.imr       = 0x00;                  // datasheet: ICW1 clears IMR
            u.irr       = 0x00;
            u.isr       = 0x00;
            u.readIsrSelected = false;
            u.pollArmed       = false;
            // ICW1<0> = IC4 (ICW4 follows); pc264 sets it.  Cascade
            // assumed (SNGL ignored -- single mode "should NEVER be
            // used" per cy82c693_def.h).
            u.icw4 = (v & 0x01) ? u.icw4 : 0;
            logFirstIcw(master, v);
            return;
        }
        if (v & 0x08) {                          // OCW3
            // bits 1:0 = RR/RIS: 10 -> read IRR, 11 -> read ISR.
            if ((v & 0x03) == 0x02) u.readIsrSelected = false;
            if ((v & 0x03) == 0x03) u.readIsrSelected = true;
            if (v & 0x04)           u.pollArmed = true;     // P bit
            return;
        }
        // OCW2: bits 7:5 = op, bits 2:0 = level.
        switch ((v >> 5) & 0x7) {
        case 0x1:                                // non-specific EOI
            // Clear the highest-priority in-service bit.
            for (int lvl = 0; lvl < 8; ++lvl) {
                uint8_t const bit = static_cast<uint8_t>(1u << lvl);
                if (u.isr & bit) {
                    u.isr = static_cast<uint8_t>(u.isr & ~bit);
                    break;
                }
            }
            logFirstEoi(master, v);
            break;
        case 0x3:                                // specific EOI
            u.isr = static_cast<uint8_t>(u.isr & ~(1u << (v & 0x7)));
            logFirstEoi(master, v);
            break;
        default:
            // Rotate / set-priority ops -- TODO table; log once.
#if EMULATR_BRINGUP_PROBES
            {
                static std::atomic<bool> s_logged{false};
                if (!s_logged.exchange(true, std::memory_order_acq_rel)) {
                    std::fprintf(stderr,
                                 "Pic8259Pair: OCW2 op 0x%02x not modeled "
                                 "(rotate/priority -- TODO table)\n",
                                 static_cast<unsigned>(v));
                    std::fflush(stderr);
                }
            }
#endif
            break;
        }
    }

    void writeData(Unit& u, uint8_t v, bool master) noexcept
    {
        switch (u.initState) {
        case InitState::ExpectIcw2:
            u.icw2 = static_cast<uint8_t>(v & 0xF8);
            u.initState = InitState::ExpectIcw3;
            return;
        case InitState::ExpectIcw3:
            u.icw3 = v;
            u.initState = InitState::ExpectIcw4;   // pc264 always writes ICW4
            return;
        case InitState::ExpectIcw4:
            u.icw4 = v;
            u.initState = InitState::Ready;
            return;
        case InitState::Ready:
        case InitState::Idle:
        default:
            // OCW1: the interrupt mask.  Output re-evaluates on the next
            // outputAsserted() query by construction (level function).
            logFirstUnmask(master, u.imr, v);
            u.imr = v;
            return;
        }
    }

    // --------------------------------------------------------------------
    // One-shot observability (the design doc's "which ports does the
    // handler read" capture rides these).
    // --------------------------------------------------------------------
    static void logFirstIcw(bool master, uint8_t v) noexcept
    {
#if EMULATR_BRINGUP_PROBES
        static std::atomic<bool> s_logged{false};
        if (!s_logged.exchange(true, std::memory_order_acq_rel)) {
            std::fprintf(stderr,
                         "Pic8259Pair: first ICW1 (%s) value=0x%02x "
                         "(LTIM=%d -> %s-triggered)\n",
                         master ? "master" : "slave",
                         static_cast<unsigned>(v),
                         (v & 0x08) ? 1 : 0,
                         (v & 0x08) ? "level" : "edge");
            std::fflush(stderr);
        }
#else
        (void)master; (void)v;
#endif
    }

    static void logFirstUnmask(bool master, uint8_t oldImr,
                               uint8_t newImr) noexcept
    {
        // Log the first OCW1 that UNMASKS IRQ4 on the master -- the
        // serial driver's hand-off event (design doc TEST 1 anchor).
        if (!master) return;
#if EMULATR_BRINGUP_PROBES
        bool const wasMasked = (oldImr & 0x10) != 0;
        bool const nowOpen   = (newImr & 0x10) == 0;
        if (wasMasked && nowOpen) {
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_acq_rel)) {
                std::fprintf(stderr,
                             "Pic8259Pair: first IRQ4 unmask (master OCW1 "
                             "0x%02x -> 0x%02x)\n",
                             static_cast<unsigned>(oldImr),
                             static_cast<unsigned>(newImr));
                std::fflush(stderr);
            }
        }
#else
        (void)oldImr; (void)newImr;
#endif
    }

    static void logFirstEoi(bool master, uint8_t v) noexcept
    {
#if EMULATR_BRINGUP_PROBES
        static std::atomic<bool> s_logged{false};
        if (!s_logged.exchange(true, std::memory_order_acq_rel)) {
            std::fprintf(stderr,
                         "Pic8259Pair: first EOI (%s) OCW2=0x%02x\n",
                         master ? "master" : "slave",
                         static_cast<unsigned>(v));
            std::fflush(stderr);
        }
#else
        (void)master; (void)v;
#endif
    }

    // --------------------------------------------------------------------
    // State
    // --------------------------------------------------------------------
    Unit    m_unit[2];          // [0] master (0x20), [1] slave (0xA0)
    uint8_t m_elcr[2];          // 0x4D0/0x4D1 edge/level override (stored)
    bool    m_lastInput[16];    // per-line previous level (edge detector)
};

#endif // PIC_8259_PAIR_H
