// ============================================================================
// AliM1543C.h -- Acer Labs (ALi) M1543C PCI-to-ISA South Bridge
// ============================================================================
// Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE
//   The AlphaServer ES40 / ES45 south bridge is the ALi M1543C -- NOT the
//   Cypress CY82C693 used by the DS10/DS20 (PC264).  This models the M1543C
//   Function 0 (PCI-to-ISA bridge) config space + the ALi-specific bridge/
//   interrupt-routing config registers the SRM programs, so ES40 PCI
//   enumeration finds the bridge (0x10B9/0x1533) instead of master-aborting.
//
//   Mirrors the Cy82C693IsaBridge contract exactly: implements
//   IPciDeviceHandler + IIoPortHandler, holds a 256-byte config space, and is
//   wired by the chipset via pchip.registerPciDevice(0,5,0,&ali) +
//   pchip.setIoPortHandler(&ali).  The standard ISA devices behind the bridge
//   (8259 pair, 8254, MC146818 RTC, 16550 UARTs, 8042 KBC) live at fixed ISA
//   ports and are registered SEPARATELY by the chipset (reused, bridge-
//   agnostic) -- this class only provides the bridge's PCI identity + the ISA
//   I/O fallback sink, exactly as the Cypress model does.
//
// PROVENANCE  (Processor Support/ALi M1543_Datasheet.{pdf,txt})
//   Vendor ID 0x10B9 (datasheet sec 4, "Vendor ID 10B9h").
//   Device ID 0x1533 (func0 PCI-to-ISA bridge, "Device ID 1533h").
//   Class    0x060100 (0Bh=06h bridge, 0Ah=01h ISA, 09h=00h progIF).
//   Companion functions (separate device IDs, modeled as optional stubs):
//     M5229 IDE  = 0x5229 ; M5237 USB = 0x5237 ; M7101 PMU/SMBus = 0x7101.
//   Bridge/IRQ-routing config registers at index 0x40-0x4F (sec 4):
//     0x40 ISA-bus / decode control; 0x42 ISA clock divisor; 0x43 USB/IDE/
//     decode enables (bit6 gates an interface per datasheet line 1681);
//     0x44 IDE primary INTAJ routing + level/edge; 0x48-0x4B PCI INTx (PIRQ)
//     steering to ISA IRQs.  Modeled as store-through (RO-protected IDs) for
//     bring-up -- faithful field semantics are TODO once the ES40 SRM init
//     sequence is traced.
//
// STATUS: bring-up scaffold.  Faithful: identity + enumeration + config
//   store-through.  Stubbed: IRQ-steering field effects, companion-function
//   internals, ISA pass-through (the real ISA devices are wired separately).
//   Authored without a local MSVC/Qt compile; verified standalone with g++.
// ============================================================================

#ifndef ALI_M1543C_H
#define ALI_M1543C_H

#include <cstdint>
#include <cstring>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler, IIoPortHandler

// ----------------------------------------------------------------------------
// Generic ALi companion-function config responder (IDE / USB / PMU).  Gives
// the SRM's multi-function probe a valid header to read instead of a master
// abort.  Legacy ATA ports / SMBus are handled by the existing device models;
// this only answers config cycles with the right identity.
// ----------------------------------------------------------------------------
class AliPciFunctionStub : public IPciDeviceHandler {
public:
    AliPciFunctionStub(uint16_t deviceId, uint32_t classCode) noexcept {
        std::memset(m_cfg, 0, sizeof(m_cfg));
        m_cfg[0x00] = 0xB9; m_cfg[0x01] = 0x10;                 // vendor 0x10B9
        m_cfg[0x02] = uint8_t(deviceId & 0xFF);
        m_cfg[0x03] = uint8_t(deviceId >> 8);
        m_cfg[0x09] = uint8_t(classCode & 0xFF);                // prog-IF
        m_cfg[0x0A] = uint8_t((classCode >> 8) & 0xFF);         // subclass
        m_cfg[0x0B] = uint8_t((classCode >> 16) & 0xFF);        // base class
        m_cfg[0x0E] = 0x80;                                     // part of multifunction device
    }
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override {
        if (uint32_t(reg) + width > sizeof(m_cfg)) return 0xFFFFFFFFu;
        uint32_t v = 0; std::memcpy(&v, &m_cfg[reg], width); return v;
    }
    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override {
        if (uint32_t(reg) + width > sizeof(m_cfg)) return;
        if (reg < 0x04) return;                                 // RO vendor/device
        std::memcpy(&m_cfg[reg], &value, width);
    }
private:
    uint8_t m_cfg[256];
};

// ----------------------------------------------------------------------------
// M1543C Function 0 -- PCI-to-ISA bridge.
// ----------------------------------------------------------------------------
class AliM1543C : public IPciDeviceHandler, public IIoPortHandler {
public:
    // ALi identity constants (datasheet sec 4).
    static constexpr uint16_t kVendorId      = 0x10B9;
    static constexpr uint16_t kDeviceIdBridge= 0x1533;  // func0 PCI-ISA bridge
    static constexpr uint16_t kDeviceIdIde   = 0x5229;  // M5229 IDE
    static constexpr uint16_t kDeviceIdUsb   = 0x5237;  // M5237 USB
    static constexpr uint16_t kDeviceIdPmu   = 0x7101;  // M7101 PMU/SMBus
    static constexpr uint32_t kClassIsaBridge= 0x060100;

    AliM1543C() noexcept {
        std::memset(m_configSpace, 0, sizeof(m_configSpace));

        // Vendor / Device.
        m_configSpace[0x00] = uint8_t(kVendorId & 0xFF);
        m_configSpace[0x01] = uint8_t(kVendorId >> 8);
        m_configSpace[0x02] = uint8_t(kDeviceIdBridge & 0xFF);
        m_configSpace[0x03] = uint8_t(kDeviceIdBridge >> 8);

        // Command: respond to I/O + MEM, bus master (0x0007).
        m_configSpace[0x04] = 0x07;
        m_configSpace[0x05] = 0x00;
        // Status: DEVSEL medium (0x0280).
        m_configSpace[0x06] = 0x00;
        m_configSpace[0x07] = 0x02;

        // Revision ID (M1543C rev; datasheet default, _PROVISIONAL).
        m_configSpace[0x08] = 0xC3;
        // Class code 0x060100 (ISA bridge).
        m_configSpace[0x09] = 0x00;
        m_configSpace[0x0A] = 0x01;
        m_configSpace[0x0B] = 0x06;

        // Header type 0x80 = multifunction (companion IDE/USB/PMU functions).
        // NOTE: datasheet sec 4 notes "0Eh bit7=0 always single-function chip"
        // as a default; the ES40/ES45 integration exposes the companions as
        // PCI functions, so we advertise multifunction here.  TODO-verify
        // against the ES40 strapping / SRM probe (one of the first things the
        // run will confirm).
        m_configSpace[0x0E] = 0x80;
    }

    // ---- IPciDeviceHandler ------------------------------------------------
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override {
        if (uint32_t(reg) + width > sizeof(m_configSpace)) return 0xFFFFFFFFu;
        uint32_t v = 0; std::memcpy(&v, &m_configSpace[reg], width); return v;
    }
    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override {
        if (uint32_t(reg) + width > sizeof(m_configSpace)) return;
        if (reg < 0x04) return;                       // RO vendor/device
        // TODO(ali-irq-route): index 0x48-0x4B PCI INTx (PIRQ) steering and
        // 0x44 IDE INTAJ level/edge have real routing effects the SRM relies
        // on.  Bring-up = store-through; wire field effects when the ES40
        // interrupt path is exercised.  See datasheet sec 4.
        std::memcpy(&m_configSpace[reg], &value, width);
    }

    // ---- IIoPortHandler ---------------------------------------------------
    // The bridge itself owns no decoded ISA registers here; the real ISA
    // devices are registered at their fixed ports by the chipset.  Unmapped
    // reads float high, writes are dropped -- identical to the Cypress model.
    uint64_t ioRead(uint16_t /*port*/, uint8_t /*width*/) override { return 0xFFULL; }
    void     ioWrite(uint16_t /*port*/, uint64_t /*value*/, uint8_t /*width*/) override {}

    // Optional companion config responders (construct + register at func1/3 if
    // the platform wants them enumerable; otherwise they master-abort safely).
    AliPciFunctionStub makeIdeFunction() const noexcept {
        return AliPciFunctionStub(kDeviceIdIde, 0x01018A);   // IDE, prog-IF 0x8A (bus-master, legacy)
    }
    AliPciFunctionStub makePmuFunction() const noexcept {
        return AliPciFunctionStub(kDeviceIdPmu, 0x068000);   // base 0x06 bridge / other
    }

private:
    uint8_t m_configSpace[256];
};

#endif // ALI_M1543C_H
