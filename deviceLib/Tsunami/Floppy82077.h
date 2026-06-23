// ============================================================================
// deviceLib/Tsunami/Floppy82077.h -- minimal 82077-class FDC, fast-fail probe
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// PURPOSE (task #20): the SRM floppy probe for dva0 stalls ~8.2 min because the
// FDC at 0x3F0 is unmodeled -- reads return 0xFF and the firmware times out.
// This is the dqa0/VirtualIsoDevice pattern applied to the floppy: present a
// controller that answers the probe IMMEDIATELY with "drive not ready / no
// media" so the firmware gives up fast.  This is NOT a working floppy.
//
// 82077 register map (base 0x3F0):
//   +0 SRA (ro)  +1 SRB (ro)  +2 DOR (rw)  +3 TDR (rw)
//   +4 MSR (ro) / DSR (wo)    +5 FIFO (rw) +6 (IDE owns 0x3F6) +7 DIR(ro)/CCR(wo)
//
// MSR bits: 7 RQM (ready for byte), 6 DIO (1=FDC->CPU result), 5 NDMA, 4 CB
//           (command busy), 3..0 drive-seek-busy.
//
// CONTRACT (fast-fail, no hangs):
//   F1  MSR always asserts RQM in the command phase (0x80) so the firmware's
//       "wait for RQM" poll terminates instantly -- never the 0xFF "busy
//       forever" the unmodeled port returned.
//   F2  Every command consumes its exact parameter count, then (if it has a
//       result phase) returns a result that says ABNORMAL / NOT READY:
//       SENSE INTERRUPT -> ST0=0x70 (abnormal+seek-end+equip-check), PCN=0;
//       SENSE DRIVE STATUS -> ST3=0x00 (Ready bit clear); READ/READ ID ->
//       ST0=0x40 abnormal; VERSION -> 0x90; invalid opcode -> ST0=0x80.
//   F3  (RESOLVED 2026-06-11) The DS10 console floppy driver runs POLLED, not
//       interrupt-driven: dv_driver.c sets ide_polled_flag=1 under #if TURBO,
//       and ide_poll() (the TURBO branch) detects a pending floppy interrupt by
//       writing 0x0A to I/O port 0x536 and testing bit 0x80 of the read-back;
//       when set it calls ide_interrupt(), which merely posts the recalibrate/
//       seek semaphore.  So the ~20-min dva0 stall was the recalibrate
//       krn$_wait timing out (5000 units x 2 retries, repeated) because 0x536
//       never reported the pending bit.  FIX (F4): model the floppy IRQ6 line
//       as a pending bit visible at 0x536, NOT a wired interrupt -- no Cchip/PIC
//       delivery is needed because the console only polls.
//   F4  Interrupt-pending model: a command that raises IRQ6 on completion
//       (RECALIBRATE 0x07, SEEK 0x0F, and the data commands READ/WRITE/READ ID/
//       FORMAT/SCAN) sets m_intPending; SENSE INTERRUPT (0x08) and a controller
//       reset clear it.  Port 0x536 reads 0x80 while pending, else 0x00; the
//       0x0A poll-select write is accepted and ignored.  After the poll posts
//       the semaphore, recalibrate issues SENSE INTERRUPT and reads ST0=0x70
//       (abnormal/equip-check), so the driver concludes "drive not present"
//       FAST instead of running the full timeout.  Routed to this object via
//       the SuperIO (which forwards non-config ports to the embedded FDC); the
//       chipset registers 0x536 to the SuperIO window.
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_FLOPPY82077_H
#define DEVICELIB_TSUNAMI_FLOPPY82077_H

#include <array>
#include <cstdint>
#include <cstdio>     // EMULATR_FDC_TRACE
#include <cstdlib>

#include "chipsetLib/IDeviceHandlers.h"   // IIoPortHandler

class Floppy82077 : public IIoPortHandler
{
public:
    static constexpr uint16_t kBase = 0x3F0;

    // F4: TURBO floppy interrupt-poll register.  dv_driver.c ide_poll() writes
    // 0x0A here then tests bit 0x80 to detect a pending floppy IRQ6 in polled
    // mode.  Not in the 0x3F0 FDC window -- routed via the SuperIO.
    static constexpr uint16_t kIntPoll    = 0x536;
    static constexpr uint8_t  kIntPending = 0x80;   // bit ide_poll tests

    // MSR bits
    static constexpr uint8_t kMSR_RQM = 0x80;
    static constexpr uint8_t kMSR_DIO = 0x40;
    static constexpr uint8_t kMSR_CB  = 0x10;

    Floppy82077() noexcept { reset(); }

    void reset() noexcept
    {
        m_dor = 0; m_dsr = 0; m_ccr = 0;
        m_phase = Phase::Command;
        m_cmdPos = m_cmdLen = 0;
        m_resPos = m_resLen = 0;
        m_msr = kMSR_RQM;                 // idle, ready for a command byte
        m_intPending = false;            // F4: no floppy IRQ6 pending after reset
    }

    // F5 (2026-06-11): level source for ISA IRQ6.  The shipped ds10_v7_3
    // floppy driver runs in INTERRUPT mode (DOR bit3 set, no 0x536 poll -- per
    // EMULATR_FDC_TRACE), so the recalibrate/seek krn$_wait is released by a
    // real IRQ6, not the 0x536 poll bit.  TsunamiChipset::evalDeviceIrqs feeds
    // this into the 8259 as IRQ6 alongside COM1's IRQ4.  Asserted on command
    // completion (RECALIBRATE/SEEK/RW), cleared by SENSE INTERRUPT / reset.
    [[nodiscard]] bool interruptPending() const noexcept { return m_intPending; }

    // ---- IIoPortHandler ----------------------------------------------------
    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        if (port == kIntPoll) {                  // F4: floppy IRQ6 poll register
            uint8_t const ip = m_intPending ? kIntPending : 0x00;
            fdcTrace('R', port, ip, width);
            return ip;
        }
        uint8_t r;
        switch (port - kBase) {
        case 0: r = 0x00;        break;          // SRA
        case 1: r = 0x00;        break;          // SRB
        case 2: r = m_dor;       break;          // DOR
        case 3: r = 0x00;        break;          // TDR
        case 4: r = m_msr;       break;          // MSR
        case 5: r = fifoRead();  break;          // FIFO (result phase)
        case 7: r = 0x00;        break;          // DIR (no disk change)
        default: r = 0xFF;       break;
        }
        fdcTrace('R', port, r, width);
        return r;
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);
        fdcTrace('W', port, value, width);
        if (port == kIntPoll) return;            // F4: 0x0A poll-select, no-op
        switch (port - kBase) {
        case 2: {                                // DOR
            // bit2 = /RESET.  Entering reset (1->0) resets the controller FSM;
            // LEAVING reset (0->1) makes the 82077 raise IRQ6 (reset-completion
            // interrupt), which the polled/interrupt driver waits on before its
            // 4x SENSE INTERRUPT drive-state drain.  Without that edge the
            // post-reset krn$_wait runs its full timeout -- the dva0 option-
            // firmware-scan stall (EMULATR_FDC_TRACE 2026-06-22: only 0x08 then
            // 0x0C, then silence).  enterReset/exitReset are mutually exclusive;
            // reset() zeroes m_dor, so m_dor=v must follow it.
            bool const enterReset = (m_dor & 0x04) && !(v & 0x04);  // 1->0
            bool const exitReset  = !(m_dor & 0x04) && (v & 0x04);  // 0->1
            if (enterReset) reset();             // clears FSM state + m_intPending
            m_dor = v;
            if (exitReset)  m_intPending = true; // 82077 reset-completion IRQ6
            break;
        }
        case 4: m_dsr = v;        break;         // DSR
        case 5: fifoWrite(v);     break;         // FIFO (command/param)
        case 7: m_ccr = v;        break;         // CCR
        default: break;                          // SRA/SRB/TDR ro/ignored
        }
    }

private:
    enum class Phase : uint8_t { Command, Result };

    Phase                    m_phase  = Phase::Command;
    uint8_t                  m_msr    = kMSR_RQM;
    uint8_t                  m_dor = 0, m_dsr = 0, m_ccr = 0;
    bool                     m_intPending = false;   // F4: floppy IRQ6 pending (polled at 0x536)
    std::array<uint8_t, 16>  m_cmd{};
    int                      m_cmdPos = 0, m_cmdLen = 0;
    std::array<uint8_t, 16>  m_res{};
    int                      m_resPos = 0, m_resLen = 0;

    // Param/result byte counts for the 82077 commands a probe touches.
    // op already has MT/MF/SK (0xE0) stripped by the caller.
    static void cmdShape(uint8_t op, int& nParam, int& nResult) noexcept
    {
        switch (op & 0x1F) {
        case 0x03: nParam = 2; nResult = 0;  break; // SPECIFY
        case 0x04: nParam = 1; nResult = 1;  break; // SENSE DRIVE STATUS -> ST3
        case 0x05: nParam = 8; nResult = 7;  break; // WRITE DATA
        case 0x06: nParam = 8; nResult = 7;  break; // READ DATA
        case 0x07: nParam = 1; nResult = 0;  break; // RECALIBRATE (would IRQ)
        case 0x08: nParam = 0; nResult = 2;  break; // SENSE INTERRUPT -> ST0,PCN
        case 0x0A: nParam = 1; nResult = 7;  break; // READ ID
        case 0x0C: nParam = 8; nResult = 7;  break; // READ DELETED DATA
        case 0x0D: nParam = 5; nResult = 7;  break; // FORMAT TRACK
        case 0x0F: nParam = 2; nResult = 0;  break; // SEEK (would IRQ)
        case 0x11: nParam = 8; nResult = 7;  break; // SCAN EQUAL
        case 0x12: nParam = 1; nResult = 0;  break; // PERPENDICULAR MODE
        case 0x13: nParam = 3; nResult = 0;  break; // CONFIGURE
        case 0x0E: nParam = 0; nResult = 10; break; // DUMPREG
        case 0x10: nParam = 0; nResult = 1;  break; // VERSION -> 0x90
        default:   nParam = 0; nResult = 1;  break; // invalid -> ST0=0x80
        }
    }

    void fifoWrite(uint8_t v) noexcept
    {
        if (m_phase != Phase::Command) return;   // ignore data during result
        if (m_cmdPos == 0) {                      // opcode byte
            int nParam = 0, nResult = 0;
            cmdShape(v, nParam, nResult);
            m_cmd[0]  = v;
            m_cmdPos  = 1;
            m_cmdLen  = 1 + nParam;
            // 82077: accepting a command opcode takes the controller BUSY,
            // which DEASSERTS any INT left from the prior command -- e.g. the
            // reset-completion IRQ6 that enable_controller waits on but never
            // SENSE-INTERRUPTs (EMULATR_FDC_TRACE 2026-06-22: reset edge, then
            // no 0x08 drain).  Completion re-asserts INT, giving a fresh 0->1
            // edge the edge-triggered ISA IRQ6 can latch.  Without this deassert
            // the next IRQ-raising command (RECALIBRATE) sets m_intPending 1->1
            // (no edge), the ide_recalibrate_cmd krn$_wait never wakes, and it
            // burns its 5000-unit timeout x2 retries x every floppy_devtab row
            // -- the dva0 spin.  Cleared here (a guest step before the param
            // write that triggers completion) so the PIC samples the edge.
            m_intPending = false;
        } else if (m_cmdPos < static_cast<int>(m_cmd.size())) {
            m_cmd[static_cast<size_t>(m_cmdPos++)] = v;
        }
        if (m_cmdPos >= m_cmdLen) execute();
    }

    uint8_t fifoRead() noexcept
    {
        if (m_phase != Phase::Result) return 0x00;
        uint8_t const r = (m_resPos < m_resLen)
                              ? m_res[static_cast<size_t>(m_resPos)] : 0x00;
        ++m_resPos;
        if (m_resPos >= m_resLen) {               // result drained -> idle
            m_phase  = Phase::Command;
            m_cmdPos = 0;
            m_msr    = kMSR_RQM;                   // ready for next command
            // 82077 Path B (result-phase commands: READ DATA/READ ID/WRITE):
            // the completion interrupt is cleared by READING the result phase
            // (last byte out of 0x3F5), NOT by SENSE INTERRUPT.  Drop IRQ6 here
            // so a Path-B command leaves no lingering edge.  (Path A --
            // RECALIBRATE/SEEK -- has no result phase and is cleared by SENSE
            // INTERRUPT in execute(); the command-busy deassert on the next
            // opcode still covers any leftover.)
            m_intPending = false;
        }
        return r;
    }

    void execute() noexcept
    {
        uint8_t const op = static_cast<uint8_t>(m_cmd[0] & 0x1F);
        int nParam = 0, nResult = 0;
        cmdShape(m_cmd[0], nParam, nResult);

        m_resPos = 0;
        for (auto& b : m_res) b = 0;

        switch (op) {
        case 0x08:                                // SENSE INTERRUPT
            m_res[0] = 0x70;                      // ST0: abnormal+seek-end+equip-check
            m_res[1] = 0x00;                      // PCN
            m_intPending = false;                 // F4: acknowledges/clears the IRQ6
            break;
        case 0x04:                                // SENSE DRIVE STATUS
            m_res[0] = 0x00;                      // ST3: Ready bit (0x20) clear
            break;                                // (no interrupt)
        case 0x10:                                // VERSION
            m_res[0] = 0x90;                      // 82077AA
            break;                                // (no interrupt)
        case 0x05: case 0x06: case 0x0A:          // WRITE/READ/READ ID
        case 0x0C: case 0x0D: case 0x11:
            m_res[0] = 0x40;                      // ST0 IC=01 abnormal
            // ST1..ST2 = 0; C/H/R/N = 0
            m_intPending = true;                  // F4: data command raises IRQ6
            break;
        case 0x07: case 0x0F:                     // RECALIBRATE / SEEK
            // No result phase; on real HW these raise IRQ6 at seek-end.  F4:
            // flag it pending so the polled ide_poll(0x536) posts the semaphore.
            m_intPending = true;
            break;
        case 0x03: case 0x12: case 0x13:          // SPECIFY/PERPENDICULAR/CONFIGURE
            // no result phase, no interrupt
            break;
        case 0x0E:                                // DUMPREG (10 bytes of zero)
            break;
        default:                                  // invalid command (no interrupt)
            m_res[0] = 0x80;                      // ST0 IC=10 invalid
            break;
        }

        m_resLen = nResult;
        if (nResult > 0) {
            m_phase = Phase::Result;
            m_msr   = static_cast<uint8_t>(kMSR_RQM | kMSR_DIO | kMSR_CB);
        } else {
            m_phase  = Phase::Command;            // back to idle
            m_cmdPos = 0;
            m_msr    = kMSR_RQM;
        }
    }

    static void fdcTrace(char rw, uint16_t port, uint64_t val, uint8_t width) noexcept
    {
        static bool const on = (std::getenv("EMULATR_FDC_TRACE") != nullptr);
        if (!on) return;
        // w= access width; val= FULL value (high bytes carry the data for an
        // odd port folded into a wide store to the even base) -- claude-web step 2.
        std::fprintf(stderr, "FDC-TRACE %c port=0x%03X off=%d w=%u val=0x%llX\n",
                     rw, port, (port - kBase), static_cast<unsigned>(width),
                     static_cast<unsigned long long>(val));
    }
};

#endif // DEVICELIB_TSUNAMI_FLOPPY82077_H
