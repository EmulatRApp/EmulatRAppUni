// ============================================================================
// Uart16550.h -- 16550 UART Register Model
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
//   Emulates a 16550-compatible UART at the register level. The SRM
//   firmware discovers this UART through I/O port probing and uses it
//   for all console output (banner, >>> prompt, command echo).
//
//   The UART connects to an IConsoleDevice backend (e.g.,
//   StdoutConsoleBackend or SRMConsoleDevice) which provides the
//   actual byte-stream transport for TX.  RX bytes arrive ONLY via
//   feedRxByte() (the deterministic injection seam -- the chipset
//   drains the backend at the step() boundary; this model never pulls
//   the backend itself).
//
// I/O PORT MAP (base = 0x3F8 for COM1, 0x2F8 for COM2):
//
//   Offset  DLAB  Read            Write           Description
//   +0x00   0     RBR             THR             Receive/Transmit
//   +0x00   1     DLL             DLL             Divisor Latch Low
//   +0x01   0     IER             IER             Interrupt Enable
//   +0x01   1     DLM             DLM             Divisor Latch High
//   +0x02   -     IIR             FCR             Interrupt ID / FIFO Ctrl
//   +0x03   -     LCR             LCR             Line Control
//   +0x04   -     MCR             MCR             Modem Control
//   +0x05   -     LSR             -               Line Status (read-only)
//   +0x06   -     MSR             -               Modem Status (read-only)
//   +0x07   -     SCR             SCR             Scratch Register
//
// LINE STATUS REGISTER (LSR) -- bits returned on read:
//   Bit 0:  Data Ready        (RX FIFO non-empty)
//   Bit 1:  Overrun Error     (sticky; set on RX FIFO overflow, cleared
//                              by LSR read per 16550 datasheet)
//   Bit 2:  Parity Error      (always 0)
//   Bit 3:  Framing Error     (always 0)
//   Bit 4:  Break Interrupt   (always 0)
//   Bit 5:  THR Empty         (TX always ready -- instantaneous TX model)
//   Bit 6:  Transmitter Empty (TX always ready)
//   Bit 7:  FIFO Error        (always 0)
//
// INTERRUPT MODEL (2026-06-04, Increment 1 of the serial-console
// interrupt design -- journals/20260604_serial_console_interrupt_design.md):
//
//   The interrupt line is a LEVEL-SENSITIVE COMBINATIONAL function of
//   (uartIer & sources), re-evaluated implicitly on every register
//   event because intPending() computes it from live state:
//
//     intPending = (uartIer.ERBFI & rxAvail)        // RX data available
//                | (uartIer.ETBEI & threLatch)      // THR empty
//
//   ("uartIer" = this device's IER at +0x01.  NOT the CPU's HW_IER
//   EIEN field -- two registers, one name; see design doc Section 4.)
//
//   THRE source latch (m_threLatch): LSR.THRE is constantly 1 in the
//   instantaneous-TX model, so the THRE *interrupt source* needs its
//   own latch with datasheet set/clear semantics:
//     SET   on ETBEI 0->1 edge while THR empty (the enable-while-
//           already-empty kick-start -- the TEST 1 unstick event), and
//     SET   on every THR write (TX completes instantly; THR empties
//           again -> fresh source, which in turn gives the Cypress
//           8259 the fresh input EDGE it needs in edge-trigger mode).
//     CLEAR on IIR read that reports code 0x02 (THRE highest pending).
//   RX-avail has no latch: it is the live level (uartIer.ERBFI AND
//   RX FIFO non-empty), cleared naturally when RBR drains the FIFO.
//
//   IIR read priority (FIFOs-on bits 0xC0 OR'd in when enabled):
//     0x04  RX data available   (higher)
//     0x02  THR empty           (lower; cleared by this read)
//     0x01  no interrupt pending
//
//   The line is consumed by TsunamiChipset::step(), which mirrors it
//   into the Cypress 8259 IRQ4 input once per step boundary (one
//   interrupt edge per boundary = the design's storm guard).
//
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior
// ============================================================================
// FCR.trigger  -- trigger-level bits <7:6> stored; RX-avail asserts on
//                 >=1 byte regardless of programmed trigger. Wire when a
//                 driver programs a non-default trigger level.
// IIR 0x0C     -- char-timeout interrupt not asserted; RX-avail (0x04)
//                 covers the traced driver. Wire when the FIFO timeout
//                 path is exercised.
// IIR 0x06     -- line-status interrupt (ELSI) not asserted; LSR error
//                 bits store but raise nothing. Wire when an error
//                 injection path exists.
// IIR 0x00     -- modem-status interrupt (EDSSI) not asserted; MSR
//                 stores but raises nothing. Wire when modem-control
//                 lines are exercised.
// MCR.OUT2     -- stored; delivery gate NOT armed (validated negative
//                 2026-06-04: zero MCR writes in the hand-off window).
//                 One-shot stderr log on first MCR write is a RESEARCH
//                 SIGNAL only; arming requires confirming the board
//                 gates delivery on OUT2 (evidence says it does not).
// ============================================================================
//
// BACKEND WIRING:
//   THR write       -> IConsoleDevice::putChar(byte)  (+ stderr mirror)
//   RBR read        -> internal RX FIFO head (NEVER the backend; the
//                      chipset feeds the FIFO via feedRxByte() at the
//                      step() boundary -- determinism seam)
//   LSR bit 0       -> RX FIFO non-empty
//   LSR bit 5       -> always 1 (output is always ready)
//
// INTERFACE IMPLEMENTED:
//   IIoPortHandler (from TsunamiPchip.h)
//
// REFERENCES:
//   National Semiconductor PC16550D UART Datasheet
//   OX16C950 Enhanced UART Datasheet (compatible superset)
//   journals/20260604_serial_console_interrupt_design.md (Sections 1-3)
//
// CHANGE LOG:
//   2026-06-04  Increment 1: combinational interrupt line + THRE latch
//               + IIR priority/clear-on-read + internal RX FIFO
//               (feedRxByte seam) + MCR one-shot log.  Removed the
//               2026-05-28 UARTBP#8 diagnostic (its removal trigger,
//               LSR-wedge closure, was met 2026-05-28).  RBR/LSR no
//               longer pull the backend directly.
// ============================================================================

#ifndef UART_16550_H
#define UART_16550_H

#include <atomic>
#include <chrono>                                // 2026-06-08: timestamped console mirror (#5/#8)
#include <cstdint>
#include <cstdio>
#include <cstdlib>                              // std::getenv (console-snapshot gate)
#include <cstring>                              // std::memcmp (badge suffix match)
#include <deque>
#include <string>
#include <QDataStream>                          // snapshot serialize seam (v2)
#include "chipsetLib/IDeviceHandlers.h"         // IIoPortHandler
#include "deviceLib/IConsoleDevice.h"           // IConsoleDevice backend
#include "deviceLib/BadgeMhzGauge.h"            // 2026-07-01: live effMhz for badge rewrite

class Uart16550 : public IIoPortHandler
{
public:
    // ========================================================================
    // Register offsets from base port
    // ========================================================================
    static constexpr uint8_t kRBR_THR_DLL = 0x00;   // RX / TX / Divisor Low
    static constexpr uint8_t kIER_DLM     = 0x01;   // Int Enable / Divisor High
    static constexpr uint8_t kIIR_FCR     = 0x02;   // Int ID (R) / FIFO Ctrl (W)
    static constexpr uint8_t kLCR         = 0x03;   // Line Control
    static constexpr uint8_t kMCR         = 0x04;   // Modem Control
    static constexpr uint8_t kLSR         = 0x05;   // Line Status (R)
    static constexpr uint8_t kMSR         = 0x06;   // Modem Status (R)
    static constexpr uint8_t kSCR         = 0x07;   // Scratch Register

    // LCR bit masks
    static constexpr uint8_t kLCR_DLAB    = 0x80;   // Divisor Latch Access Bit

    // IER bit masks (this device's interrupt enables -- "uartIer", NOT
    // the CPU HW_IER EIEN field; see header INTERRUPT MODEL note).
    static constexpr uint8_t kIER_ERBFI   = 0x01;   // RX data available enable
    static constexpr uint8_t kIER_ETBEI   = 0x02;   // THR empty enable
    static constexpr uint8_t kIER_ELSI    = 0x04;   // Line status enable (TODO)
    static constexpr uint8_t kIER_EDSSI   = 0x08;   // Modem status enable (TODO)

    // MCR bit masks
    static constexpr uint8_t kMCR_OUT2    = 0x08;   // Int gate on PC/AT boards
                                                    // (stored; gate NOT armed)

    // LSR bit masks
    static constexpr uint8_t kLSR_DR      = 0x01;   // Data Ready
    static constexpr uint8_t kLSR_OE      = 0x02;   // Overrun Error (sticky)
    static constexpr uint8_t kLSR_THRE    = 0x20;   // THR Empty
    static constexpr uint8_t kLSR_TEMT    = 0x40;   // Transmitter Empty

    // IIR values
    static constexpr uint8_t kIIR_NO_INT  = 0x01;   // No interrupt pending
    static constexpr uint8_t kIIR_THRE    = 0x02;   // THR-empty pending
    static constexpr uint8_t kIIR_RXAVAIL = 0x04;   // RX-data pending
    static constexpr uint8_t kIIR_FIFO_ON = 0xC0;   // FIFOs enabled indicator

    // RX FIFO depth (16550 silicon: 16 bytes).
    static constexpr size_t  kRxFifoDepth = 16;

    // ========================================================================
    // Construction
    // ========================================================================

    /**
     * @brief Construct UART with console backend
     * @param backend   IConsoleDevice for TX (may be nullptr)
     * @param basePort  I/O base port (0x3F8 for COM1, 0x2F8 for COM2)
     * @param name      Device name for logging (e.g., "COM1", "OPA0")
     */
    explicit Uart16550(IConsoleDevice* backend = nullptr,
                       uint16_t basePort = 0x3F8,
                       const std::string& name = "COM1") noexcept
        : m_backend(backend)
        , m_basePort(basePort)
        , m_name(name)
    {
        reset();
    }

    // ========================================================================
    // Reset
    // ========================================================================

    void reset() noexcept
    {
        m_ier = 0x00;
        m_fcr = 0x00;
        m_lcr = 0x00;
        m_mcr = 0x00;
        m_scr = 0x00;
        m_dll = 0x01;       // default divisor = 1
        m_dlm = 0x00;
        m_fifosEnabled = false;
        m_threLatch    = false;
        m_lsrSticky    = 0x00;
        m_rxFifo.clear();
        // Drop any withheld badge-rewrite bytes (transient TX-side only).
        m_badgeState = BadgeState::Normal;
        m_badgePend.clear();
        m_badgeSuf.clear();
    }

    // ========================================================================
    // Snapshot serialization (kChipsetVersion 2, 2026-06-05)
    // ========================================================================
    // Guest-visible register state + interrupt-source latches + RX FIFO
    // content.  Excluded: m_backend / m_basePort / m_name (construction
    // wiring, re-established by Machine), and the MCR one-shot log flag
    // (diagnostic).  IIR is combinational off serialized state.
    // Rationale: a snapshot taken after the firmware programmed IER
    // restored as reset-default leaves the interrupt chain dead -- the
    // guest never reprograms it (2026-06-05 deaf-console diagnosis).
    // ========================================================================

    void serialize(QDataStream& ds) const noexcept
    {
        ds << m_ier << m_fcr << m_lcr << m_mcr << m_scr
           << m_dll << m_dlm
           << static_cast<quint8>(m_fifosEnabled ? 1 : 0)
           << static_cast<quint8>(m_threLatch    ? 1 : 0)
           << m_lsrSticky;
        ds << static_cast<quint32>(m_rxFifo.size());
        for (uint8_t const b : m_rxFifo) ds << b;
    }

    void deserialize(QDataStream& ds) noexcept
    {
        quint8 fifosEn = 0;
        quint8 threLat = 0;
        ds >> m_ier >> m_fcr >> m_lcr >> m_mcr >> m_scr
           >> m_dll >> m_dlm >> fifosEn >> threLat >> m_lsrSticky;
        m_fifosEnabled = (fifosEn != 0);
        m_threLatch    = (threLat != 0);
        quint32 rxCount = 0;
        ds >> rxCount;
        m_rxFifo.clear();
        for (quint32 i = 0; i < rxCount && i < kRxFifoDepth; ++i) {
            quint8 b = 0;
            ds >> b;
            m_rxFifo.push_back(static_cast<uint8_t>(b));
        }
    }

    // ========================================================================
    // Backend management
    // ========================================================================

    void setBackend(IConsoleDevice* backend) noexcept { m_backend = backend; }
    IConsoleDevice* backend() const noexcept { return m_backend; }
    const std::string& name() const noexcept { return m_name; }

    // ========================================================================
    // Interrupt line + RX injection seam (Increment 1, 2026-06-04)
    // ========================================================================

    /**
     * @brief Computed interrupt line -- level-sensitive combinational.
     *
     * intPending = (uartIer.ERBFI & RX-FIFO-non-empty)
     *            | (uartIer.ETBEI & THRE-source-latch)
     *
     * Recomputed from live state on every call, so every register
     * event that changes an operand is reflected at the next
     * evaluation -- the design doc Section 1 requirement.  Consumed by
     * TsunamiChipset::step() once per boundary (storm guard).
     */
    [[nodiscard]] bool intPending() const noexcept
    {
        bool const rx   = ((m_ier & kIER_ERBFI) != 0) && !m_rxFifo.empty();
        bool const thre = ((m_ier & kIER_ETBEI) != 0) && m_threLatch;
        return rx || thre;
    }

    /**
     * @brief Deterministic RX injection seam.
     *
     * The ONLY way bytes enter the RX FIFO.  Called by the chipset at
     * the step() boundary (it drains the backend queue there), never
     * from the backend thread.  FIFO overflow sets the sticky LSR
     * Overrun Error bit and drops the byte (16550 datasheet: new data
     * is lost on overrun).
     */
    void feedRxByte(uint8_t byte) noexcept
    {
        snapshotWatch(byte);            // Option-A console-snapshot marker watch
                                        // (env-gated, non-consuming; tracks every
                                        //  injected byte regardless of FIFO state)
        if (m_rxFifo.size() >= kRxFifoDepth) {
            m_lsrSticky |= kLSR_OE;     // overrun: sticky until LSR read
            return;                     // byte lost
        }
        m_rxFifo.push_back(byte);
    }

    /// RX FIFO occupancy (doctest observability).
    [[nodiscard]] size_t rxFifoCount() const noexcept { return m_rxFifo.size(); }

    // ------------------------------------------------------------------------
    // Console-snapshot trigger (Option A: RX marker-watch).  When
    // EMULATR_CONSOLE_SNAPSHOT is set, feedRxByte mirrors the operator's input
    // line; a completed line equal to EMULATR_SNAPSHOT_MARKER (default
    // "set oem_string snapshot") latches a one-shot request.  Bytes still pass
    // through unchanged -- the SRM processes the command normally.  Machine::run
    // polls takeSnapshotRequest() and writes a predig_oemsnap snapshot.
    //
    // Replay-safe: the watch is on the RX-INJECTION seam only, so a restored RX
    // FIFO (read straight from the snapshot, never re-fed) cannot re-trigger.
    // feedRxByte is a step()-boundary call on the run-loop thread, so the plain
    // bool needs no atomic.  m_snapLine / m_snapRequest are transient and are
    // deliberately NOT serialized.
    [[nodiscard]] bool takeSnapshotRequest() noexcept
    {
        bool const r = m_snapRequest;
        m_snapRequest = false;
        return r;
    }

    // ========================================================================
    // IIoPortHandler -- I/O port access (called by IsaBridge)
    // ========================================================================

    /**
     * @brief Read from UART I/O port
     * @param port   Absolute I/O port address
     * @param width  Access width (always 1 for UART)
     * @return       Register value
     */
    uint64_t ioRead(uint16_t port, uint8_t /*width*/) override
    {
        // ================================================================
        // UARTBP#9 -- out-of-window port reaching the UART, 2026-06-04
        // ================================================================
        // The uint8_t reg truncation below destroys the original port,
        // so an out-of-range dispatch (registry overlap, wrong base,
        // COM2 cross-talk) is invisible inside readRegister -- its
        // default case sees only the wrapped reg.  Log the intact port
        // HERE.  First 16 loud, then every 4096th.
        //
        // REMOVAL TRIGGER: delete when the readRegister-default-0xFF
        // source is identified and fixed.
        // ================================================================
#if EMULATR_BRINGUP_PROBES
        if (static_cast<uint16_t>(port - m_basePort) > 7) {
            static std::atomic<uint64_t> s_cnt{0};
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 16 || (n & 0xFFFu) == 0) {
                std::fprintf(stderr,
                             "UARTBP#9 %s ioRead OUT-OF-WINDOW port=0x%04x "
                             "basePort=0x%04x (occurrence %llu)\n",
                             m_name.c_str(),
                             static_cast<unsigned>(port),
                             static_cast<unsigned>(m_basePort),
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
        }
#endif

        const uint8_t reg = static_cast<uint8_t>(port - m_basePort);
        return readRegister(reg);
    }

    /**
     * @brief Write to UART I/O port
     * @param port   Absolute I/O port address
     * @param value  Value to write
     * @param width  Access width (always 1 for UART)
     */
    void ioWrite(uint16_t port, uint64_t value, uint8_t /*width*/) override
    {
        // UARTBP#10 -- write-side twin of UARTBP#9 above; same removal
        // trigger.  Logs the intact port + value before reg truncation.
#if EMULATR_BRINGUP_PROBES
        if (static_cast<uint16_t>(port - m_basePort) > 7) {
            static std::atomic<uint64_t> s_cnt{0};
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 16 || (n & 0xFFFu) == 0) {
                std::fprintf(stderr,
                             "UARTBP#10 %s ioWrite OUT-OF-WINDOW port=0x%04x "
                             "basePort=0x%04x value=0x%02x (occurrence %llu)\n",
                             m_name.c_str(),
                             static_cast<unsigned>(port),
                             static_cast<unsigned>(m_basePort),
                             static_cast<unsigned>(value & 0xFF),
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
        }
#endif

        const uint8_t reg = static_cast<uint8_t>(port - m_basePort);
        writeRegister(reg, static_cast<uint8_t>(value & 0xFF));
    }

    // ========================================================================
    // Register read
    // ========================================================================
    // NOT const: RBR pops the RX FIFO, IIR clears the THRE source, LSR
    // clears the sticky error bits -- 16550 reads have side effects.

    uint8_t readRegister(uint8_t reg) noexcept
    {
        const bool dlab = (m_lcr & kLCR_DLAB) != 0;

        switch (reg)
        {
        case kRBR_THR_DLL:
            if (dlab) {
                return m_dll;
            }
            // RBR: pop the RX FIFO head (clears RX-avail when emptied)
            return readRBR();

        case kIER_DLM:
            if (dlab) {
                return m_dlm;
            }
            return m_ier;

        case kIIR_FCR:
            return readIIR();

        case kLCR:
            return m_lcr;

        case kMCR:
            return m_mcr;

        case kLSR:
            return readLSR();

        case kMSR:
            return readMSR();

        case kSCR:
            return m_scr;

        default:
            // UARTBP#9 companion -- default-case hit with the (wrapped)
            // reg value.  The intact port is in the ioRead log above.
            // Same removal trigger.
#if EMULATR_BRINGUP_PROBES
            {
                static std::atomic<uint64_t> s_cnt{0};
                uint64_t const n =
                    s_cnt.fetch_add(1, std::memory_order_relaxed);
                if (n < 16 || (n & 0xFFFu) == 0) {
                    std::fprintf(stderr,
                                 "UARTBP#9d %s readRegister DEFAULT "
                                 "reg=0x%02x -> 0xFF (occurrence %llu)\n",
                                 m_name.c_str(),
                                 static_cast<unsigned>(reg),
                                 static_cast<unsigned long long>(n + 1));
                    std::fflush(stderr);
                }
            }
#endif
            return 0xFF;
        }
    }

    // ========================================================================
    // Register write
    // ========================================================================

    void writeRegister(uint8_t reg, uint8_t value) noexcept
    {
        const bool dlab = (m_lcr & kLCR_DLAB) != 0;

        switch (reg)
        {
        case kRBR_THR_DLL:
            if (dlab) {
                m_dll = value;
            } else {
                // THR: transmit byte through backend
                writeTHR(value);
            }
            break;

        case kIER_DLM:
            if (dlab) {
                m_dlm = value;
            } else {
                writeIER(value);
            }
            break;

        case kIIR_FCR:
            // FCR: FIFO control (write-only at this offset)
            writeFCR(value);
            break; 

        case kLCR:
            m_lcr = value;
            break;

        case kMCR:
            writeMCR(value);
            break;

        case kLSR:
            // LSR is read-only, ignore writes
            break;

        case kMSR:
            // MSR is read-only, ignore writes
            break;

        case kSCR:
            m_scr = value;
            break;

        default:
            // UARTBP#10 companion -- default-case hit on the write side
            // with the (wrapped) reg + value.  Same removal trigger.
#if EMULATR_BRINGUP_PROBES
            {
                static std::atomic<uint64_t> s_cnt{0};
                uint64_t const n =
                    s_cnt.fetch_add(1, std::memory_order_relaxed);
                if (n < 16 || (n & 0xFFFu) == 0) {
                    std::fprintf(stderr,
                                 "UARTBP#10d %s writeRegister DEFAULT "
                                 "reg=0x%02x value=0x%02x DROPPED "
                                 "(occurrence %llu)\n",
                                 m_name.c_str(),
                                 static_cast<unsigned>(reg),
                                 static_cast<unsigned>(value),
                                 static_cast<unsigned long long>(n + 1));
                    std::fflush(stderr);
                }
            }
#endif
            break;
        }
    }

private:
    // ========================================================================
    // Console-snapshot marker watch (Option A) -- see takeSnapshotRequest().
    // ========================================================================
    static bool consoleSnapshotEnabled() noexcept
    {
        static bool const on = (std::getenv("EMULATR_CONSOLE_SNAPSHOT") != nullptr);
        return on;
    }
    static const std::string& snapshotMarker() noexcept
    {
        static const std::string m = [] {
            char const* const e = std::getenv("EMULATR_SNAPSHOT_MARKER");
            return std::string(e ? e : "set oem_string snapshot");
        }();
        return m;
    }
    // Mirror the operator's input line; latch a request when a completed line
    // (leading/trailing whitespace trimmed) equals the marker.
    void snapshotWatch(uint8_t byte) noexcept
    {
        if (!consoleSnapshotEnabled()) return;
        if (byte == '\r' || byte == '\n') {
            std::string line = m_snapLine;
            m_snapLine.clear();
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            std::size_t s = 0;
            while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
            if (line.compare(s, std::string::npos, snapshotMarker()) == 0)
                m_snapRequest = true;
        } else if (byte == 0x08u || byte == 0x7Fu) {        // BS / DEL
            if (!m_snapLine.empty()) m_snapLine.pop_back();
        } else if (byte >= 0x20u && byte < 0x7Fu) {         // printable ASCII
            if (m_snapLine.size() < 128u)
                m_snapLine.push_back(static_cast<char>(byte));
        }
    }

    std::string m_snapLine;             // accumulated input line (transient)
    bool        m_snapRequest = false;  // one-shot latch; NOT serialized

    // 2026-06-08: timestamped console-output mirror (EMULATR_CONSOLE_MIRROR).
    // Transient, NOT serialized.  m_mirrorLine accumulates a TX line; each
    // completed line is emitted to stderr with the wall-clock delta since the
    // previous line (boot-phase / timer-loop profiling, #5/#8).
    std::string                           m_mirrorLine;
    std::chrono::steady_clock::time_point m_mirrorLast{};
    bool                                  m_mirrorFirst = true;

    // 2026-07-01: cosmetic "<n> MHz" badge-rewrite TX state (see badgeTxEmit).
    // Transient, NOT serialized -- empty at every line boundary.
    enum class BadgeState { Normal, Digits, Suffix };
    BadgeState  m_badgeState = BadgeState::Normal;
    std::string m_badgePend;   // withheld digit run
    std::string m_badgeSuf;    // withheld bytes matched against " MHz"

    // ========================================================================
    // Register behavior implementations
    // ========================================================================

    /**
     * @brief Read Receive Buffer Register
     *
     * Pops one byte from the internal RX FIFO (fed exclusively by
     * feedRxByte at the step() boundary).  Returns 0x00 when empty --
     * matches floating-RBR-after-drain silicon behavior closely enough
     * for the SRM driver, which gates RBR reads on LSR.DR / RX-avail.
     */
    uint8_t readRBR() noexcept
    {
        if (m_rxFifo.empty()) return 0x00;

        uint8_t const b = m_rxFifo.front();
        m_rxFifo.pop_front();
        return b;
        // RX-avail interrupt level falls automatically when the FIFO
        // empties -- intPending() computes from live FIFO state.
    }

    /**
     * @brief Write Interrupt Enable Register (uartIer)
     *
     * Stores bits 3:0.  The ETBEI 0->1 edge while THR is empty SETS
     * the THRE source latch -- the 16550 "enable-while-already-empty"
     * kick-start (datasheet) and the design's TEST 1 unstick event:
     * the SRM tt driver queues output, writes uartIer=0x03 with the
     * line drained, and expects the THRE interrupt to fire its ISR.
     */
    void writeIER(uint8_t value) noexcept
    {
        uint8_t const prev = m_ier;
        m_ier = value & 0x0F;   // only low 4 bits valid

        bool const etbeiRose = ((prev & kIER_ETBEI) == 0)
                            && ((m_ier & kIER_ETBEI) != 0);
        if (etbeiRose) {
            // LSR.THRE is constantly true in the instantaneous-TX
            // model, so the enable edge always finds THR empty.
            m_threLatch = true;
        }
    }

    /**
     * @brief Write Transmit Holding Register
     *
     * Sends one byte through the console backend.
     * If no backend is attached, the byte is discarded.
     *
     * 2026-05-29: every byte is ALSO mirrored to stderr regardless of
     * backend state.  This makes the SRM banner / >>> prompt visible
     * in the host terminal even when no TCP client (PuTTY) has yet
     * connected, and gives a permanent record in the run log if the
     * client disconnects mid-stream.
     *
     * 2026-06-04: TX completes instantly, so THR empties again the
     * moment the write retires -> the THRE source latch re-arms.  In
     * Cypress edge-trigger mode this is what produces the fresh IRQ4
     * input edge per transmitted byte (design doc Section 6 item 4).
     */
    void writeTHR(uint8_t value) noexcept
    {
        // 2026-07-01: cosmetic badge rewrite.  The console stream goes through
        // badgeTxEmit(), which replaces the digits of any "<n> MHz" token with
        // the live effective MHz (see badgeTxEmit).  Emitted bytes fan out to
        // BOTH the backend and the stderr console mirror (badgeEmitByte), so
        // the injected value is visible in headless/batch runs captured via
        // EMULATR_CONSOLE_MIRROR (e.g. 2> run.log), not just in an attached
        // PuTTY.  The raw firmware value (100) is a known constant if needed.
        badgeTxEmit(value);

        m_threLatch = true;     // THR empty again (instantaneous TX)
    }

    // ------------------------------------------------------------------------
    // Cosmetic "<n> MHz" badge rewrite on the console TX stream (2026-07-01).
    // ------------------------------------------------------------------------
    // The SRM banner speed ("AlphaServer DS20 100 MHz") is a firmware compile-
    // time constant surfaced by the ISP path (see BadgeMhzGauge.h).  We edit
    // only the bytes leaving the console: a small state machine withholds a
    // "<digits> MHz" token and substitutes the live MHz_eff for its digits.
    // Everything else -- including the ">>>" prompt (no digits) -- passes
    // through unbuffered, so streaming/interactivity is unchanged.  Guest
    // memory and the functional HWRPB CC_FREQ field are never touched.
    //
    // Withholding is bounded: at most a run of digits plus up to 4 lookahead
    // bytes (" MHz").  Any newline or divergence from " MHz" flushes the
    // withheld bytes verbatim.  These buffers are transient and NOT serialized
    // (they are empty at every line boundary, hence at any snapshot point).

    void badgeEmitByte(uint8_t b) noexcept
    {
        consoleMirror(b);           // gated stderr mirror sees the rewritten bytes
        if (m_backend) {
            m_backend->putChar(b);
        }
    }

    void badgeEmitBytes(char const* s, std::size_t n) noexcept
    {
        for (std::size_t i = 0; i < n; ++i) {
            badgeEmitByte(static_cast<uint8_t>(s[i]));
        }
    }

    // Flush any withheld digit/suffix bytes verbatim and return to Normal.
    void badgeFlushPending() noexcept
    {
        if (!m_badgePend.empty()) {
            badgeEmitBytes(m_badgePend.data(), m_badgePend.size());
            m_badgePend.clear();
        }
        if (!m_badgeSuf.empty()) {
            badgeEmitBytes(m_badgeSuf.data(), m_badgeSuf.size());
            m_badgeSuf.clear();
        }
        m_badgeState = BadgeState::Normal;
    }

    void badgeTxEmit(uint8_t value) noexcept
    {
        char const  c        = static_cast<char>(value);
        bool const  isDigit  = (value >= '0' && value <= '9');
        char const* const kSuffix = " MHz";   // token tail we rewrite before
        std::size_t const kSuffixLen = 4;

        switch (m_badgeState) {
        case BadgeState::Normal:
            if (isDigit) {
                m_badgePend.assign(1, c);        // begin withholding a digit run
                m_badgeState = BadgeState::Digits;
            } else {
                badgeEmitByte(value);
            }
            return;

        case BadgeState::Digits:
            if (isDigit) {
                if (m_badgePend.size() < 24) {   // cap: no real speed field is this long
                    m_badgePend.push_back(c);
                } else {
                    // Absurd digit run -- not a speed field; flush and move on.
                    badgeFlushPending();
                    badgeEmitByte(value);
                }
                return;
            }
            // Digit run ended; begin matching the " MHz" suffix with this byte.
            m_badgeSuf.clear();
            m_badgeState = BadgeState::Suffix;
            [[fallthrough]];

        case BadgeState::Suffix: {
            m_badgeSuf.push_back(c);
            std::size_t const n = m_badgeSuf.size();
            bool const stillPrefix =
                (n <= kSuffixLen)
                && (std::memcmp(m_badgeSuf.data(), kSuffix, n) == 0);
            if (stillPrefix) {
                if (n == kSuffixLen) {
                    // Full "<digits> MHz" -- substitute the live effective MHz,
                    // right-justified to the original digit-field width so the
                    // firmware's "%3d" column alignment is preserved.
                    unsigned const    v   = deviceLib::badge::effMhzNow();
                    std::string       rep = std::to_string(v);
                    while (rep.size() < m_badgePend.size()) {
                        rep.insert(rep.begin(), ' ');
                    }
                    badgeEmitBytes(rep.data(), rep.size());
                    badgeEmitBytes(m_badgeSuf.data(), m_badgeSuf.size());
                    m_badgePend.clear();
                    m_badgeSuf.clear();
                    m_badgeState = BadgeState::Normal;
                }
                // else: partial " MHz" match -- keep withholding.
            } else {
                // Not a speed token.  Flush the withheld digit run verbatim,
                // then re-feed the suffix bytes from Normal so that a digit
                // which begins a NEW token is re-withheld rather than leaked.
                // This matters because the model name "DS20" abuts the real
                // speed ("...DS20 100 MHz"): the "20" is a false digit run
                // whose mismatch must not swallow the leading "1" of "100".
                std::string suf;
                suf.swap(m_badgeSuf);
                if (!m_badgePend.empty()) {
                    badgeEmitBytes(m_badgePend.data(), m_badgePend.size());
                    m_badgePend.clear();
                }
                m_badgeState = BadgeState::Normal;
                for (char const sc : suf) {
                    badgeTxEmit(static_cast<uint8_t>(sc));
                }
            }
            return;
        }
        }
    }

    // Console output mirror to stderr -- gated, line-buffered, timestamped.
    // EMULATR_CONSOLE_MIRROR=1 emits each completed console line prefixed with
    // the wall-clock delta since the previous line, so large "+dt" gaps reveal
    // time spent in firmware timer/poll loops (#5/#8).  Silent by default,
    // which also retires the old ungated per-byte mirror (#7).
    void consoleMirror(uint8_t value) noexcept
    {
        static bool const on = (std::getenv("EMULATR_CONSOLE_MIRROR") != nullptr);
        if (!on) return;
        char const c = static_cast<char>(value);
        if (c == '\r') return;                       // wait for LF to flush the line
        if (c != '\n') {
            if (m_mirrorLine.size() < 1024) m_mirrorLine.push_back(c);
            return;
        }
        auto const now = std::chrono::steady_clock::now();
        double dtMs = 0.0;
        if (!m_mirrorFirst) {
            dtMs = std::chrono::duration<double, std::milli>(now - m_mirrorLast).count();
        }
        m_mirrorFirst = false;
        m_mirrorLast  = now;
        std::fprintf(stderr, "[CON %s +%9.3f ms] %s\n",
                     m_name.c_str(), dtMs, m_mirrorLine.c_str());
        std::fflush(stderr);
        m_mirrorLine.clear();
    }

    /**
     * @brief Read Line Status Register
     *
     * Bit 0 (DR):   1 if the internal RX FIFO is non-empty
     * Bit 1 (OE):   sticky overrun flag; cleared by this read
     * Bit 5 (THRE): always 1 (output is always ready)
     * Bit 6 (TEMT): always 1 (transmitter always empty)
     */
    uint8_t readLSR() noexcept
    {
        uint8_t lsr = static_cast<uint8_t>(kLSR_THRE | kLSR_TEMT
                                           | m_lsrSticky);
        if (!m_rxFifo.empty()) {
            lsr |= kLSR_DR;
        }
        m_lsrSticky = 0x00;     // error bits clear on LSR read (datasheet)
        return lsr;
    }

    /**
     * @brief Read Interrupt Identification Register
     *
     * Priority + clear-on-read per the design doc Section 1:
     *   RX-avail (0x04) reported above THRE (0x02); reporting THRE
     *   CLEARS the THRE source latch; reading with nothing pending
     *   returns 0x01.  FIFO-on bits 0xC0 OR'd in when FCR enabled
     *   them (consistent with the existing FIFOs-on advertisement).
     */
    uint8_t readIIR() noexcept
    {
        uint8_t const fifo = m_fifosEnabled ? kIIR_FIFO_ON : 0x00;

        bool const rxPending   = ((m_ier & kIER_ERBFI) != 0)
                              && !m_rxFifo.empty();
        bool const threPending = ((m_ier & kIER_ETBEI) != 0)
                              && m_threLatch;

        if (rxPending) {
            return static_cast<uint8_t>(fifo | kIIR_RXAVAIL);
        }
        if (threPending) {
            m_threLatch = false;        // clear-on-read (datasheet)
            return static_cast<uint8_t>(fifo | kIIR_THRE);
        }
        return static_cast<uint8_t>(fifo | kIIR_NO_INT);
    }

    /**
     * @brief Read Modem Status Register
     *
     * Returns CTS and DSR asserted (indicates connected terminal).
     * Bit 4 (CTS):  1
     * Bit 5 (DSR):  1
     * Bit 7 (DCD):  1 if backend is connected
     */
    uint8_t readMSR() const noexcept
    {
        uint8_t msr = 0x30;                      // CTS + DSR asserted

        if (m_backend && m_backend->isConnected()) {
            msr |= 0x80;                         // DCD asserted
        }

        return msr;
    }

    /**
     * @brief Write Modem Control Register
     *
     * Full storage of bits 4:0.  OUT2 (bit 3) is STORED but does NOT
     * gate interrupt delivery (design doc Section 2: validated
     * negative -- the SRM driver writes no MCR in the hand-off window
     * and AXPBox carries no OUT2 gate).  The one-shot stderr log below
     * is a RESEARCH SIGNAL so the first cold-boot trace that does
     * write MCR settles the question permanently; arming the gate
     * additionally requires confirming the BOARD routes INTRPT through
     * OUT2 -- do not arm on bit observation alone.
     */
    void writeMCR(uint8_t value) noexcept
    {
#if EMULATR_BRINGUP_PROBES
        static std::atomic<bool> s_loggedFirstWrite{false};
        if (!s_loggedFirstWrite.exchange(true, std::memory_order_acq_rel)) {
            std::fprintf(stderr,
                         "Uart16550[%s]: first MCR write value=0x%02x "
                         "(OUT2=%d) -- research signal, gate not armed\n",
                         m_name.c_str(),
                         static_cast<unsigned>(value & 0x1F),
                         (value & kMCR_OUT2) ? 1 : 0);
            std::fflush(stderr);
        }
#endif
        m_mcr = value & 0x1F;       // only low 5 bits valid
    }

    /**
     * @brief Write FIFO Control Register
     *
     * Bit 0: FIFO enable
     * Bit 1: RX FIFO reset (clears the RX FIFO)
     * Bit 2: TX FIFO reset (no-op -- TX is instantaneous)
     * Bits 7:6: RX trigger level (stored; no trigger effect -- TODO)
     */
    void writeFCR(uint8_t value) noexcept
    {
        m_fifosEnabled = (value & 0x01) != 0;
        if ((value & 0x02) != 0) {
            m_rxFifo.clear();       // RX FIFO reset (self-clearing bit)
        }
        m_fcr = value & 0xC9;                   // store non-self-clearing bits
    }

    // ========================================================================
    // State
    // ========================================================================

    // Backend (not owned)
    IConsoleDevice* m_backend;

    // Configuration
    uint16_t    m_basePort;
    std::string m_name;

    // Register storage
    uint8_t  m_ier;              // uartIer -- device Interrupt Enable Register
                                 // (NOT the CPU HW_IER EIEN field)
    uint8_t  m_fcr;              // FIFO Control Register (stored bits)
    uint8_t  m_lcr;              // Line Control Register
    uint8_t  m_mcr;              // Modem Control Register (OUT2 stored, unarmed)
    uint8_t  m_scr;              // Scratch Register
    uint8_t  m_dll;              // Divisor Latch Low
    uint8_t  m_dlm;              // Divisor Latch High
    bool     m_fifosEnabled;     // FIFO enable state (from FCR bit 0)

    // Interrupt-model state (Increment 1, 2026-06-04)
    bool     m_threLatch;        // THRE interrupt source latch (see header)
    uint8_t  m_lsrSticky;        // sticky LSR error bits (OE); clear on read
    std::deque<uint8_t> m_rxFifo;// internal RX FIFO (fed via feedRxByte only)
};

#endif // UART_16550_H
