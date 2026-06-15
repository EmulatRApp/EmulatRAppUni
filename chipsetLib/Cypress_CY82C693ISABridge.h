
// ============================================================================
// Cy82C693IsaBridge.h -- Cypress CY82C693 HyperCore PCI-to-ISA Bridge
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2026 eNVy Systems, Inc. All rights reserved.
//
// PURPOSE:
//   Emulates the core Function 0 (PCI-to-ISA Bridge) of the Cypress CY82C693.
//   Provides the necessary PCI configuration space headers to prevent the
//   Alpha SRM firmware from stalling during slot enumeration loops.
//
// ============================================================================
// INTERFACE IMPLEMENTATION NOTE & INTEGRATION INSTRUCTIONS
// ============================================================================
// To successfully wire this component into the ASA-EMulatR platform topology 
// alongside the TsunamiPchip model, this class implements both the 
// IPciDeviceHandler and IIoPortHandler interfaces. The machine layout orchestration 
// loop must adhere to the following hardwired architectural rules:
//
// 1. CHIPSET COMPONENT INSTANTIATION:
//    Ensure both the core host bridge (Hose 0) and this target slave device are 
//    initialized sequentially within your machine or motherboard initialization file:
//
//      auto pchip0 = std::make_unique<TsunamiPchip>(ChipsetVariant::Tsunami);
//      auto isaBridge = std::make_shared<Cy82C693IsaBridge>();
//
// 2. PCI CONFIGURATION SPACE ROUTING (TOPOLOGY):
//    The CY82C693 ISA bridge must be bound to Bus 0, Slot (Device) 0, Function 0.
//    Register the instance pointer into Pchip0's Type 0 routing matrix:
//
//      pchip0->registerPciDevice(0, 0, 0, isaBridge.get());
//
//    NOTE ON MULTI-FUNCTION DISPATCH: 
//    Because Register 0x0E (Header Type) is hardwired to 0x80 inside this class, 
//    the Alpha SRM firmware will sequentially probe Function 1 (IDE Controller) 
//    and Function 2 (USB Host) on Slot 0. Since TsunamiPchip::readPciConfig0() 
//    automatically returns 0xFFFFFFFFULL when a map lookup fails, Functions 1 and 2 
//    will naturally fall back to a "Device Not Present" master abort signal. This is 
//    perfectly safe and expected by SRM if those companion modules are omitted.
//
// 3. LEGACY ISA I/O WINDOW REGISTRATION:
//    The TsunamiPchip intercepts CPU physical addresses landing in the dense I/O
//    window (kIODenseOffset = 0x801.FC00.0000) and transforms them into 16-bit 
//    ISA ports. The core legacy motherboard operational regions must be directed 
//    down to this bridge using the Pchip port registration surface:
//
//      pchip0->registerIoPortRange(0x0000, 0x00F0, isaBridge.get());
//
//    This captures and passes legacy system interactions (such as the standard 
//    system timers, PIC loops, and system DMA page registers) directly into 
//    this handler.
// ============================================================================

#ifndef CYPRESS_CY82C693ISABRIDGE_H
#define CYPRESS_CY82C693ISABRIDGE_H

#include <cstdint>
#include <cstring>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler, IIoPortHandler

/*
 * Cypress CY82C693 ISA bridge occupies slot 0 and bridges to the ISA/Super I/O plane.
 * SRM's PCI enumeration will scan all 32 slots and expect to find the ISA bridge.
 * If it returns 0xFFFFFFFF for the ISA bridge's config reads (which come through
 * 0x801.FE00.0000, not 0x800), SRM may stall or retry.
 * The dense memory window 0x800 itself is only accessed after BARs are assigned,
 * so returning all-ones here is safe for now.
 */

class Cy82C693IsaBridge : public IPciDeviceHandler, public IIoPortHandler {
public:
    Cy82C693IsaBridge() noexcept
    {
        // Initialize the 256-byte PCI config space to 0
        std::memset(m_configSpace, 0, sizeof(m_configSpace));

        // ------------------------------------------------------------------
        // Standard PCI Header Initialization
        // ------------------------------------------------------------------

        // Vendor ID: Cypress Semiconductor (0x1080)
        m_configSpace[0x00] = 0x80;
        m_configSpace[0x01] = 0x10;

        // Device ID: CY82C693 (0xC693)
        m_configSpace[0x02] = 0x93;
        m_configSpace[0x03] = 0xC6;

        // Command: Respond to I/O and Memory space, enable Bus Mastering (0x0007)
        m_configSpace[0x04] = 0x07;
        m_configSpace[0x05] = 0x00;

        // Status: DEVSEL Medium (0x0280)
        m_configSpace[0x06] = 0x00;
        m_configSpace[0x07] = 0x02;

        // Revision ID
        m_configSpace[0x08] = 0x00;

        // Class Code: Bridge (0x06), ISA Bridge (0x01), ProgIF (0x00)
        m_configSpace[0x09] = 0x00;
        m_configSpace[0x0A] = 0x01;
        m_configSpace[0x0B] = 0x06;

        // Header Type: 0x80 indicates a multifunction device 
        // (CY82C693 usually hosts IDE and USB on functions 1 and 2)
        m_configSpace[0x0E] = 0x80;
    }

    /// Read from the PCI configuration space
    /// @param offset   Register offset (0-255)
    /// @param size     Access size in bytes (1, 2, or 4)
    /// @return         Read data, or 0xFFFFFFFF if out of bounds
    uint32_t readConfig(uint8_t offset, uint8_t size) const noexcept
    {
        if (offset + size > sizeof(m_configSpace)) {
            return 0xFFFFFFFF; // Master abort / no response
        }

        uint32_t val = 0;
        std::memcpy(&val, &m_configSpace[offset], size);
        return val;
    }

    /// Write to the PCI configuration space
    /// @param offset   Register offset (0-255)
    /// @param size     Access size in bytes (1, 2, or 4)
    /// @param data     Data to write
    void writeConfig(uint8_t offset, uint8_t size, uint32_t data) noexcept
    {
        if (offset + size > sizeof(m_configSpace)) {
            return;
        }

        // TODO(Emulator): Mask out read-only registers (like Vendor/Device ID) 
        // to prevent guest OS or SRM from clobbering them. 
        // For early SRM boot, blind write-through is usually enough.
        std::memcpy(&m_configSpace[offset], &data, size);
    }

    // ========================================================================
    // IPciDeviceHandler Implementation
    // ========================================================================

    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override
    {
        if (reg + width > sizeof(m_configSpace)) {
            return 0xFFFFFFFFU;
        }

        uint32_t value = 0;
        std::memcpy(&value, &m_configSpace[reg], width);
        return value;
    }

    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override
    {
        if (reg + width > sizeof(m_configSpace)) {
            return;
        }

        // Prevent modification to Read-Only Hardware Identification Registers
        if (reg < 0x04) {
            return;
        }

        std::memcpy(&m_configSpace[reg], &value, width);
    }

    // ========================================================================
    // IIoPortHandler Implementation (Placeholder for actual ISA routing)
    // ========================================================================

    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        // Unmapped ISA internal registers safely return floating state bus values
        return 0xFFULL;
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        // Drop write transactions into the legacy bit-sink
    }
private:
    uint8_t m_configSpace[256];
};

#endif // CYPRESS_CY82C693ISABRIDGE_H
