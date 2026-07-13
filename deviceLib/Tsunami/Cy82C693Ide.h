// ============================================================================
// deviceLib/Tsunami/Cy82C693Ide.h -- Cypress CY82C693 IDE controller (Func 1)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// FILE: deviceLib/Tsunami/Cy82C693Ide.h  (REFACTORED, Phase 2A; ITsunamiIde 2B)
// FUNCTION: Cy82C693Ide (whole file)
// CHANGE: Thin south-bridge IDE controller -- composes the SHARED
//   AtaTaskfileEngine (ATA/ATAPI behavior, moved out verbatim) + a
//   PciConfigSpace seeded with the Cypress CY82C693 func-1 identity.  Now
//   derives from ITsunamiIde (2B) so the chipset can hold it and AliM5229Ide
//   through one active pointer.  The public API (ioRead/ioWrite,
//   pciConfigRead/Write, attachDevice/attachMedia/attachDisk, reset,
//   status/error/selectedUnit, and the k* constants) is PRESERVED so the
//   chipset wiring and the doctests are unchanged.  The Cypress config seed +
//   writable mask reproduce the old initConfig()/pciConfigWrite() exactly ->
//   DS10/DS20 byte-identical.  Identity _PROVISIONAL (spec 7.5 / C3): vendor
//   0x1080, device 0xC693, class 0x0101, prog-IF 0x00 (legacy 0x1F0/0x170).
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_CY82C693IDE_H
#define DEVICELIB_TSUNAMI_CY82C693IDE_H

#include <cstdint>
#include <cstdio>    // EMULATR_IDE_TRACE config-cycle trace
#include <cstdlib>
#include <memory>
#include <string>

#include "deviceLib/Tsunami/ITsunamiIde.h"         // ITsunamiIde (IIoPort + IPciDevice)
#include "deviceLib/scsi/VirtualScsiDevice.h"
#include "deviceLib/scsi/IBlockMedia.h"
#include "deviceLib/Tsunami/AtaTaskfileEngine.h"   // shared ATA/ATAPI engine (Phase 2A)
#include "deviceLib/Tsunami/PciConfigSpace.h"      // config-space mechanics (Phase 2A)

class Cy82C693Ide : public ITsunamiIde
{
public:
    // ---- constants re-exposed from the engine (API compat: tests reference
    //      Cy82C693Ide::kBSY / kDRDY / kCMD_* ...) ----------------------------
    static constexpr uint8_t  kBSY  = AtaTaskfileEngine::kBSY;
    static constexpr uint8_t  kDRDY = AtaTaskfileEngine::kDRDY;
    static constexpr uint8_t  kDF   = AtaTaskfileEngine::kDF;
    static constexpr uint8_t  kDSC  = AtaTaskfileEngine::kDSC;
    static constexpr uint8_t  kDRQ  = AtaTaskfileEngine::kDRQ;
    static constexpr uint8_t  kERR  = AtaTaskfileEngine::kERR;
    static constexpr uint8_t  kERR_ABRT = AtaTaskfileEngine::kERR_ABRT;
    static constexpr uint8_t  kCMD_DEVICE_RESET   = AtaTaskfileEngine::kCMD_DEVICE_RESET;
    static constexpr uint8_t  kCMD_PIDENTIFY      = AtaTaskfileEngine::kCMD_PIDENTIFY;
    static constexpr uint8_t  kCMD_PACKET         = AtaTaskfileEngine::kCMD_PACKET;
    static constexpr uint8_t  kCMD_IDENTIFY       = AtaTaskfileEngine::kCMD_IDENTIFY;
    static constexpr uint8_t  kCMD_READ_SECTORS   = AtaTaskfileEngine::kCMD_READ_SECTORS;
    static constexpr uint8_t  kCMD_READ_SECTORS_NR= AtaTaskfileEngine::kCMD_READ_SECTORS_NR;
    static constexpr uint8_t  kCMD_EXEC_DIAG      = AtaTaskfileEngine::kCMD_EXEC_DIAG;
    static constexpr uint8_t  kCMD_SET_FEATURES   = AtaTaskfileEngine::kCMD_SET_FEATURES;
    static constexpr uint8_t  kCMD_INIT_DEV_PARAMS= AtaTaskfileEngine::kCMD_INIT_DEV_PARAMS;
    static constexpr uint32_t kSectorBytes        = AtaTaskfileEngine::kSectorBytes;
    static constexpr uint8_t  kAtapiSigLo         = AtaTaskfileEngine::kAtapiSigLo;
    static constexpr uint8_t  kAtapiSigHi         = AtaTaskfileEngine::kAtapiSigHi;

    Cy82C693Ide() noexcept { initConfig(); }   // engine self-resets in its own ctor

    // ---- ATA engine forwarders (ITsunamiIde) -------------------------------
    void attachDevice(int channel, int unit, scsi::VirtualScsiDevice* dev) noexcept override
    { m_eng.attachDevice(channel, unit, dev); }
    bool attachMedia(int channel, int unit, std::unique_ptr<scsi::IBlockMedia> media) noexcept override
    { return m_eng.attachMedia(channel, unit, std::move(media)); }
    void reset() noexcept override { m_eng.reset(); }

    // convenience (tests / simple wiring; not on the interface)
    bool attachDisk(int channel, int unit, const std::string& path) noexcept
    { return m_eng.attachDisk(channel, unit, path); }

    // ---- IIoPortHandler (legacy taskfile windows -> engine) ----------------
    uint64_t ioRead(uint16_t port, uint8_t width) override
    { return m_eng.ioRead(port, width); }
    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    { m_eng.ioWrite(port, value, width); }

    // ---- IPciDeviceHandler -- Function 1 Cypress config space --------------
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override
    {
        uint32_t const v = m_cfg.read(reg, width);
        static bool const cfgOn = (std::getenv("EMULATR_IDE_TRACE") != nullptr);
        if (cfgOn)
            std::fprintf(stderr, "IDE-TRACE C cfg reg=0x%02X w=%u val=0x%08X\n",
                         reg, width, v);
        return v;
    }
    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override
    { m_cfg.write(reg, value, width); }

    // ---- inspection accessors (doctest) ------------------------------------
    [[nodiscard]] uint8_t status(int ch) const noexcept { return m_eng.status(ch); }
    [[nodiscard]] uint8_t error(int ch)  const noexcept { return m_eng.error(ch); }
    [[nodiscard]] int     selectedUnit(int ch) const noexcept { return m_eng.selectedUnit(ch); }

private:
    AtaTaskfileEngine m_eng;   // shared ATA/ATAPI taskfile engine
    PciConfigSpace    m_cfg;   // Function-1 PCI config space (Cypress identity)

    // Seed the Cypress CY82C693 func-1 identity.  Byte-identical to the
    // pre-refactor initConfig(): all bytes writable EXCEPT the RO identity
    // header (0x00-0x03 vendor/device, 0x08-0x0B rev/prog-IF/class, 0x0E header
    // type) -- exactly the offsets the old pciConfigWrite continue-guard blocked.
    void initConfig() noexcept
    {
        m_cfg.fillZero();
        m_cfg.setAllWritable();
        m_cfg.seedByte(0x00, 0x80); m_cfg.seedByte(0x01, 0x10);  // vendor 0x1080   _PROVISIONAL
        m_cfg.seedByte(0x02, 0x93); m_cfg.seedByte(0x03, 0xC6);  // device 0xC693   _PROVISIONAL
        m_cfg.seedByte(0x08, 0x00);                               // revision
        m_cfg.seedByte(0x09, 0x00);                               // prog-IF (legacy) _PROVISIONAL
        m_cfg.seedByte(0x0A, 0x01);                               // subclass: IDE
        m_cfg.seedByte(0x0B, 0x01);                               // base class: mass storage
        m_cfg.seedByte(0x0E, 0x00);                               // header type 0 (single function)
        m_cfg.setReadOnlyRange(0x00, 0x03);
        m_cfg.setReadOnlyRange(0x08, 0x0B);
        m_cfg.setReadOnly(0x0E);
        // BARs (0x10..) left 0 + writable: legacy/compat IDE answers fixed ports.
    }
};

#endif // DEVICELIB_TSUNAMI_CY82C693IDE_H
