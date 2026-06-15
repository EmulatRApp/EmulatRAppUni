// ============================================================================
// deviceLib/Tsunami/Smc37c669SuperIo.h -- SMC/FDC37C669 SuperIO config model
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// PURPOSE (task #22): the DS10/PC264 floppy + serial + parallel + kbd path live
// in an SMC/FDC37C669 SuperIO.  pc264_init's SMC_init() calls SMC37c669_detect()
// which, per smcc669_driver.c, must succeed before any logical device is
// configured.  Until EmulatR answers the config port, detect returns NULL and
// SMC_init skips everything -- and the firmware's later floppy access has no
// configured controller.
//
// PROTOCOL (authoritative, smcc669_driver.c / smcc669_def.h):
//   index_port = base+0 = 0x3F0 ; data_port = base+1 = 0x3F1
//   enter config : wb(0x3F0, 0x55) TWICE in succession        (CONFIG_ON_KEY)
//   exit  config : wb(0x3F0, 0xAA)                            (CONFIG_OFF_KEY)
//   read  reg    : wb(0x3F0, index) ; data = rb(0x3F1)
//   write reg    : wb(0x3F0, index) ; wb(0x3F1, data)
//   detect       : enter; read CR0D (index 0x0D); exit; require
//                  device_id == SMC37c669_DEVICE_ID (0x03)
//
// PORT SHARING: 0x3F0/0x3F1 are the config index/data WHEN IN CONFIG MODE, and
// the FDC's SRA/SRB legacy registers otherwise.  0x3F2-0x3F5 + 0x3F7 are always
// the FDC.  This object owns the whole window and routes: config registers in
// config mode, else delegate to the Floppy82077 FDC logical device (#20).
//
// PHASE 1 (this file): make detect succeed (CR0D=0x03) and accept/store config
// register writes (base/IRQ/DMA/activate) as a no-op register file -- enough for
// SMC_init to complete.  The FDC stays at its legacy 0x3F0 window (default).
// PHASE 2 (later): honor the configured logical-device bases/enables.
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_SMC37C669SUPERIO_H
#define DEVICELIB_TSUNAMI_SMC37C669SUPERIO_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "chipsetLib/IDeviceHandlers.h"          // IIoPortHandler
#include "deviceLib/Tsunami/Floppy82077.h"       // FDC logical device (#20)

class Smc37c669SuperIo : public IIoPortHandler
{
public:
    static constexpr uint16_t kIndexPort     = 0x3F0;  // config index / FDC SRA
    static constexpr uint16_t kDataPort      = 0x3F1;  // config data  / FDC SRB
    static constexpr uint8_t  kConfigOnKey   = 0x55;   // SMC37c669_CONFIG_ON_KEY
    static constexpr uint8_t  kConfigOffKey  = 0xAA;   // SMC37c669_CONFIG_OFF_KEY
    static constexpr uint8_t  kDeviceIdIndex = 0x0D;   // CR0D  (SMC37c669_DEVICE_ID_INDEX)
    static constexpr uint8_t  kDeviceId      = 0x03;   // SMC37c669_DEVICE_ID

    Smc37c669SuperIo() noexcept
    {
        m_creg.fill(0);
        m_creg[0x0D] = 0x03;     // CR0D device ID (SMC37c669) -> smcc669 detect() match
        m_creg[0x20] = 0x02;     // CR20 device ID (FDC37C93x) -> 935-style detect() match
        m_creg[0x21] = 0x01;     // CR21 device revision
        // The SROM opens SuperIO config mode before handing off to SRM -- the
        // live trace shows config-register writes with NO 0x55 enter key.  Start
        // open so SMC_init's writes land; firmware may 0xAA-exit at the end.
        m_configMode = true;
    }

    // ---- IIoPortHandler ----------------------------------------------------
    // CRITICAL: the firmware accesses the config port as 16-bit WORDS to 0x3F0
    // (low byte = index, high byte = data) -- confirmed live (e.g. w=2 val=0x360
    // = index 0x60 / data 0x03 = FDD base high).  EmulatR delivers that as ONE
    // wide access, so we DECOMPOSE into per-byte port ops: byte i -> port+i.
    // This routes index->0x3F0 and data->0x3F1 exactly as the chip would, and
    // keeps single-byte FDC accesses (0x3F2-0x3F5) working unchanged.
    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        // Split ONLY the config index port (0x3F0): a wide read spans index +
        // data (0x3F0/0x3F1).  0x3F1 alone and the FDC functional range
        // (0x3F2-0x3F5, esp the FIFO) are single registers -- pass unsplit.
        if (port == kIndexPort && width > 1) {
            uint64_t out = 0;
            for (uint8_t i = 0; i < width; ++i) {
                out |= static_cast<uint64_t>(readByte(static_cast<uint16_t>(port + i)))
                       << (8u * i);
            }
            return out;
        }
        if (port == kIndexPort || port == kDataPort) return readByte(port);
        return m_fdc.ioRead(port, width);              // FDC functional, unsplit
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        if (port == kIndexPort && width > 1) {         // index(low) + data(high)
            for (uint8_t i = 0; i < width; ++i) {
                writeByte(static_cast<uint16_t>(port + i),
                          static_cast<uint8_t>((value >> (8u * i)) & 0xFFu));
            }
            return;
        }
        if (port == kIndexPort || port == kDataPort) {
            writeByte(port, static_cast<uint8_t>(value & 0xFFu));
            return;
        }
        m_fdc.ioWrite(port, value, width);             // FDC functional, unsplit
    }

    // F5 (2026-06-11): expose the embedded FDC's ISA IRQ6 level so the chipset
    // can feed it to the 8259 (mirrors COM1's IRQ4).  ISA-standard mapping
    // (floppy = IRQ6), platform-invariant across the Tsunami-family SuperIO.
    [[nodiscard]] bool fdcInterruptPending() const noexcept { return m_fdc.interruptPending(); }

private:
    // ---- per-byte config/FDC routing (called by the decomposing ioR/W) -----
    uint8_t readByte(uint16_t port) noexcept
    {
        if (m_configMode) {
            if (port == kIndexPort) { trace('R', port, m_index, true); return m_index; }
            if (port == kDataPort)  {
                uint8_t const v = m_creg[m_index];
                trace('R', port, v, true);
                return v;
            }
        }
        return static_cast<uint8_t>(m_fdc.ioRead(port, 1));   // FDC functional / SRA-SRB
    }

    void writeByte(uint16_t port, uint8_t v) noexcept
    {
        if (m_configMode) {
            if (port == kIndexPort) {
                if (v == kConfigOffKey) { m_configMode = false; trace('W', port, v, true); return; }
                m_index = v;                           // set config register index
                trace('W', port, v, true);
                return;
            }
            if (port == kDataPort) {
                m_creg[m_index] = v;                   // write config register
                trace('W', port, v, true);
                return;
            }
        } else if (port == kIndexPort && v == kConfigOnKey) {
            if (++m_enterCount >= 2) { m_configMode = true; m_enterCount = 0; }
            trace('W', port, v, true);
            return;
        } else if (port == kIndexPort) {
            m_enterCount = 0;
        }
        m_fdc.ioWrite(port, v, 1);                     // FDC functional / SRA-SRB
    }

private:
    Floppy82077              m_fdc;                    // FDC logical device behind the SuperIO
    std::array<uint8_t, 256> m_creg{};                 // config register file
    uint8_t                  m_index      = 0;         // current config index
    bool                     m_configMode = false;
    int                      m_enterCount = 0;         // consecutive CONFIG_ON_KEY count

    static void trace(char rw, uint16_t port, uint8_t v, bool cfg) noexcept
    {
        static bool const on = (std::getenv("EMULATR_FDC_TRACE") != nullptr);
        if (!on) return;
        std::fprintf(stderr, "SIO-TRACE %c port=0x%03X val=0x%02X %s\n",
                     rw, port, v, cfg ? "[cfg]" : "");
    }
};

#endif // DEVICELIB_TSUNAMI_SMC37C669SUPERIO_H
