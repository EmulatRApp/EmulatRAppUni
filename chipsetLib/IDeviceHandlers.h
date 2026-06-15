// ============================================================================
// IDeviceHandlers.h -- Chipset device-attachment plug-in interfaces
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
//   The chipset is an adapter: a distinct, plug-in interface between the
//   Machine->CPU[s] side and the devices whose accesses it routes.  This
//   header is that plug-in seam.  It declares the two device-attachment
//   contracts the Pchip dispatch tables hold, plus the I/O-port range entry.
//
//   These were previously defined inside TsunamiPchip.h.  They live in their
//   own minimal header (depends only on <cstdint>) so that device modules
//   such as deviceLib/Tsunami/Uart16550.h can implement them WITHOUT pulling
//   in the full Pchip translation unit -- which would otherwise create a
//   chipsetLib <-> deviceLib include cycle once the chipset owns the UARTs.
//
//   A registered device is dispatched polymorphically (one vcall) only on
//   the cold PCI config / port-I/O paths; the linear PCI-memory hot path
//   does not route through these interfaces.
// ============================================================================
#ifndef IDEVICE_HANDLERS_H
#define IDEVICE_HANDLERS_H

#include <cstdint>

// ============================================================================
// PCI Config-Space Device Handler Interface
// ============================================================================

/**
 * @brief Interface for PCI devices that respond to config space accesses.
 *
 * Each PCI device registers with the Pchip at a (bus, device, function)
 * address.  The Pchip decodes incoming PAs and dispatches to the
 * appropriate device handler.
 */
struct IPciDeviceHandler
{
    virtual ~IPciDeviceHandler() = default;

    /**
     * @brief Read PCI configuration space register
     * @param reg    Register offset (0x00-0xFF, 4-byte aligned)
     * @param width  Access width (1, 2, or 4 bytes)
     * @return       Register value
     */
    virtual uint32_t pciConfigRead(uint8_t reg, uint8_t width) = 0;

    /**
     * @brief Write PCI configuration space register
     * @param reg    Register offset (0x00-0xFF, 4-byte aligned)
     * @param value  Value to write
     * @param width  Access width (1, 2, or 4 bytes)
     */
    virtual void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) = 0;
};

// ============================================================================
// I/O Port Device Handler Interface
// ============================================================================

/**
 * @brief Interface for devices that respond to I/O port accesses.
 *
 * Devices register an I/O port range with the Pchip.  When the firmware
 * accesses a PA in the PCI I/O window, the Pchip extracts the port
 * address and dispatches to the registered handler.
 */
struct IIoPortHandler
{
    virtual ~IIoPortHandler() = default;

    /**
     * @brief Read from I/O port
     * @param port   I/O port address
     * @param width  Access width (1, 2, or 4 bytes)
     * @return       Port value
     */
    virtual uint64_t ioRead(uint16_t port, uint8_t width) = 0;

    /**
     * @brief Write to I/O port
     * @param port   I/O port address
     * @param value  Value to write
     * @param width  Access width (1, 2, or 4 bytes)
     */
    virtual void ioWrite(uint16_t port, uint64_t value, uint8_t width) = 0;
};

// ============================================================================
// I/O Port Registration Entry
// ============================================================================

struct IoPortRange
{
    uint16_t         startPort;
    uint16_t         endPort;        // exclusive
    IIoPortHandler*  handler;
};

// ============================================================================
// PCI Dense-Memory Claim Entry (G3-lite, 2026-06-03)
// ============================================================================
// Fixed-range claimant inside a Pchip's PCI dense MEMORY window (offsets
// below 0x1_0000_0000 in Pchip space).  Reuses IIoPortHandler: the handler
// receives the REBASED offset (offset - start) in the port parameter, so a
// two-register device sees 0x0 and 0x1 regardless of where it is claimed.
// First consumer: the DS10's PCF8584 IIC controller at 0xFFFF0000-0xFFFF0001
// (see journals/IIC_PCF8584_Specification.txt and V4_IO_Machinery_Map.txt gap
// G3).  BAR-tracked claims (PCI walk, gap G4) will extend this same seam.
//
// Offsets are Pchip-window-relative, 64-bit; ranges must fit a rebased
// offset in 16 bits (asserted at registration) because IIoPortHandler's
// port parameter is uint16_t.

struct PciMemRange
{
    uint64_t         start;          // window-relative offset, inclusive
    uint64_t         end;            // window-relative offset, exclusive
    IIoPortHandler*  handler;
};

#endif // IDEVICE_HANDLERS_H
