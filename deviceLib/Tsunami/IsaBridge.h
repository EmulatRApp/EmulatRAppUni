// ============================================================================
// IsaBridge.h -- ALi M1533/M1543C PCI-to-ISA Bridge
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
//   Emulates the ALi M1533 PCI-to-ISA bridge found on ES40 (Tsunami)
//   and ES45 (Typhoon) platforms. This device sits on PCI bus 0 and
//   provides:
//     - PCI config space header (vendor/device ID, class code)
//     - I/O port routing to legacy ISA devices (UART, RTC, etc.)
//
//   The SRM firmware discovers this device during PCI enumeration by
//   reading its vendor/device ID from config space. Once identified,
//   the firmware uses the PCI I/O space window to access ISA devices
//   behind the bridge.
//
// PCI IDENTITY:
//   Vendor ID:  0x10B9 (ALi Corporation)
//   Device ID:  0x1533 (M1533 PCI-to-ISA Bridge)
//   Class:      0x0601 (ISA Bridge)
//   Revision:   0x00
//
// PCI CONFIG SPACE (Type 0 header, 256 bytes):
//   Offset  Size  Name              Value
//   0x00    2     Vendor ID         0x10B9
//   0x02    2     Device ID         0x1533
//   0x04    2     Command           stored (firmware configures)
//   0x06    2     Status            0x0200 (medium timing)
//   0x08    1     Revision ID       0x00
//   0x09    3     Class Code        0x060100 (ISA bridge)
//   0x0C    1     Cache Line Size   0x00
//   0x0D    1     Latency Timer     0x00
//   0x0E    1     Header Type       0x00 (single-function)
//   0x0F    1     BIST              0x00
//   0x10-2F       BARs              0x00 (ISA bridge has no BARs)
//   0x2C    2     Subsystem Vendor  0x10B9
//   0x2E    2     Subsystem ID      0x1533
//   0x3C    1     Interrupt Line    stored
//   0x3D    1     Interrupt Pin     0x00 (no PCI interrupt)
//
// I/O PORT ROUTING:
//   The ISA bridge accepts I/O port dispatches from the Pchip and
//   routes them to registered ISA device handlers. Port ranges:
//     0x3F8-0x3FF  COM1 (UART, OPA0)
//     0x2F8-0x2FF  COM2 (UART, OPA1)
//     0x070-0x071  RTC  (future)
//     0x060-0x064  KBD  (future)
//   Unhandled ports return 0xFF (ISA bus float).
//
// INTERFACES IMPLEMENTED:
//   IPciDeviceHandler -- PCI config space access (from TsunamiPchip)
//   IIoPortHandler    -- I/O port dispatch (from TsunamiPchip)
//
// REFERENCES:
//   ALi M1533 Datasheet
//   Tsunami/Typhoon Hardware Reference Manual (EC-RE2CA-TE)
// ============================================================================

#ifndef ISA_BRIDGE_H
#define ISA_BRIDGE_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include "chipsetLib/TsunamiPchip.h"       // IPciDeviceHandler, IIoPortHandler

// ISADIAG -- temporary device-access inventory probe.  Build with
// -DEMULATR_ISADIAG=1 (or flip the default below) to log every ISA I/O
// access that hits no registered handler (the silent 0xFF-float path),
// so a boot run enumerates exactly which legacy devices (RTC 0x70/0x71,
// KBD 0x60-0x64, etc.) the SRM touched that V4 does not model.  Throttled
// to first occurrence per port.  Revert to 0 when done.
#ifndef EMULATR_ISADIAG
#define EMULATR_ISADIAG 0
#endif

// ============================================================================
// ISA device registration entry
// ============================================================================

struct IsaDeviceEntry
{
    uint16_t         startPort;
    uint16_t         endPort;       // exclusive
    IIoPortHandler*  handler;
};

// ============================================================================
// IsaBridge
// ============================================================================

class IsaBridge : public IPciDeviceHandler, public IIoPortHandler
{
public:
    // ========================================================================
    // PCI identity constants
    // ========================================================================
    static constexpr uint16_t kVendorId      = 0x10B9;   // ALi Corporation
    static constexpr uint16_t kDeviceId      = 0x1533;   // M1533 PCI-to-ISA
    static constexpr uint8_t  kRevisionId    = 0x00;
    static constexpr uint32_t kClassCode     = 0x060100; // ISA bridge
    static constexpr uint8_t  kHeaderType    = 0x00;     // single-function

    // PCI bus/device/function (conventional for ES40)
    static constexpr uint8_t  kBus           = 0;
    static constexpr uint8_t  kDevice        = 1;
    static constexpr uint8_t  kFunction      = 0;

    // ========================================================================
    // Construction
    // ========================================================================

    IsaBridge() noexcept
    {
        reset();
    }

    // ========================================================================
    // Reset
    // ========================================================================

    void reset() noexcept
    {
        // Initialize PCI config space to zeros
        std::memset(m_configSpace, 0, sizeof(m_configSpace));

        // Populate identity registers
        writeConfig16(0x00, kVendorId);
        writeConfig16(0x02, kDeviceId);
        writeConfig16(0x04, 0x0000);            // Command: disabled
        writeConfig16(0x06, 0x0200);            // Status: medium timing
        m_configSpace[0x08] = kRevisionId;
        m_configSpace[0x09] = (kClassCode >> 0)  & 0xFF;   // Prog IF
        m_configSpace[0x0A] = (kClassCode >> 8)  & 0xFF;   // Subclass
        m_configSpace[0x0B] = (kClassCode >> 16) & 0xFF;   // Base class
        m_configSpace[0x0E] = kHeaderType;
        writeConfig16(0x2C, kVendorId);         // Subsystem vendor
        writeConfig16(0x2E, kDeviceId);         // Subsystem ID
    }

    // ========================================================================
    // ISA Device Registration
    // ========================================================================

    /**
     * @brief Register an ISA device at an I/O port range
     * @param startPort First port (inclusive)
     * @param endPort   Last port (exclusive)
     * @param handler   I/O port handler for the device
     *
     * The ISA bridge routes I/O port accesses from the Pchip to
     * registered ISA device handlers. Unregistered ports return 0xFF.
     */
    void registerIoDevice(uint16_t startPort, uint16_t endPort,
                          IIoPortHandler* handler) noexcept
    {
        m_isaDevices.push_back({ startPort, endPort, handler });
    }

    // ========================================================================
    // IPciDeviceHandler -- PCI config space access
    // ========================================================================

    /**
     * @brief Read PCI configuration space register
     * @param reg    Register offset (0x00-0xFF)
     * @param width  Access width (1, 2, or 4 bytes)
     * @return       Register value
     */
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override
    {
        const uint8_t alignedReg = reg & 0xFC;

        switch (width)
        {
        case 1:
            return m_configSpace[reg];

        case 2:
        {
            const uint8_t byteOff = reg & 0x01;
            uint16_t val = 0;
            std::memcpy(&val, &m_configSpace[alignedReg + byteOff], 2);
            return val;
        }

        case 4:
        {
            uint32_t val = 0;
            std::memcpy(&val, &m_configSpace[alignedReg], 4);
            return val;
        }

        default:
            return 0xFFFFFFFF;
        }
    }

    /**
     * @brief Write PCI configuration space register
     * @param reg    Register offset (0x00-0xFF)
     * @param value  Value to write
     * @param width  Access width (1, 2, or 4 bytes)
     *
     * Read-only registers (vendor ID, device ID, class code, etc.)
     * are silently ignored. Writable registers (command, interrupt
     * line, etc.) are stored.
     */
    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override
    {
        // Protect read-only identity registers
        if (reg < 0x04) return;                 // Vendor/Device ID
        if (reg >= 0x08 && reg < 0x0C) return;  // Rev/Class
        if (reg == 0x0E) return;                // Header type

        const uint8_t alignedReg = reg & 0xFC;

        switch (width)
        {
        case 1:
            m_configSpace[reg] = static_cast<uint8_t>(value);
            break;

        case 2:
        {
            const uint8_t byteOff = reg & 0x01;
            uint16_t val16 = static_cast<uint16_t>(value);
            std::memcpy(&m_configSpace[alignedReg + byteOff], &val16, 2);
            break;
        }

        case 4:
        {
            uint32_t val32 = value;
            std::memcpy(&m_configSpace[alignedReg], &val32, 4);
            break;
        }

        default:
            break;
        }
    }

    static uint64_t ioReadStatic(void* ctx, uint16_t port, uint8_t width) {
        return static_cast<IsaBridge*>(ctx)->ioRead(port, width);
    }

    static void ioWriteStatic(void* ctx, uint16_t port, uint64_t value, uint8_t width) {
        static_cast<IsaBridge*>(ctx)->ioWrite(port, value, width);
    }

    // ========================================================================
    // IIoPortHandler -- I/O port dispatch (called by Pchip)
    // ========================================================================

    /**
     * @brief Read from I/O port
     * @param port   I/O port address
     * @param width  Access width (1, 2, or 4 bytes)
     * @return       Port value, or 0xFF if unhandled
     *
     * The Pchip registers this bridge as the handler for the entire
     * ISA I/O port range. The bridge then routes to the specific
     * ISA device that owns the port.
     */
    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        for (const auto& dev : m_isaDevices) {
            if (port >= dev.startPort && port < dev.endPort) {
                return dev.handler->ioRead(port, width);
            }
        }
        logUnhandled(port, width, false);
        return 0xFF;        // ISA bus float value
    }

    /**
     * @brief Write to I/O port
     * @param port   I/O port address
     * @param value  Value to write
     * @param width  Access width (1, 2, or 4 bytes)
     */
    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        for (const auto& dev : m_isaDevices) {
            if (port >= dev.startPort && port < dev.endPort) {
                dev.handler->ioWrite(port, value, width);
                return;
            }
        }
        logUnhandled(port, width, true);
    }

private:
    // ISADIAG: announce the first access to each unhandled ISA port, so a
    // boot run enumerates the legacy devices the SRM expects but V4 lacks.
    void logUnhandled(uint16_t port, uint8_t width, bool isWrite) noexcept
    {
#if EMULATR_ISADIAG
        for (uint16_t p : m_isaDiagSeen) { if (p == port) return; }
        m_isaDiagSeen.push_back(port);
        char const* tag =
            (port >= 0x0070 && port <= 0x0071) ? " [RTC/NVRAM-TOY]" :
            (port >= 0x0060 && port <= 0x0064) ? " [KBD/8042]"      :
            (port >= 0x0020 && port <= 0x0021) ? " [PIC1]"          :
            (port >= 0x00A0 && port <= 0x00A1) ? " [PIC2]"          :
            (port >= 0x0040 && port <= 0x0043) ? " [PIT/8254]"      : "";
        std::fprintf(stderr,
            "ISADIAG: unhandled ISA %s port=0x%04x width=%u -> float%s\n",
            isWrite ? "write" : "read ", port, static_cast<unsigned>(width), tag);
#else
        (void)port; (void)width; (void)isWrite;
#endif
    }

    // ========================================================================
    // Config space helpers
    // ========================================================================

    void writeConfig16(uint8_t offset, uint16_t value) noexcept
    {
        std::memcpy(&m_configSpace[offset], &value, 2);
    }

    // ========================================================================
    // State
    // ========================================================================

    // PCI config space (256 bytes, Type 0 header)
    uint8_t m_configSpace[256];

    // Registered ISA devices
    std::vector<IsaDeviceEntry> m_isaDevices;

    // ISADIAG: ports already announced (first-occurrence throttle)
    std::vector<uint16_t> m_isaDiagSeen;
};

#endif // ISA_BRIDGE_H
