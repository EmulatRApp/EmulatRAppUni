// ============================================================================
// deviceLib/Tsunami/AliM5229Ide.h -- ALi M1543C integrated M5229 IDE (Func 1)
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
// FILE: deviceLib/Tsunami/AliM5229Ide.h  (NEW, Phase 2B)
// FUNCTION: AliM5229Ide (whole file)
// CHANGE: The ES40/ES45/DS25 south-bridge IDE function.  Composes the SHARED
//   AtaTaskfileEngine (identical ATA/ATAPI behavior as the Cypress path) + a
//   PciConfigSpace seeded with the faithful M5229 IDENTITY (0x10B9/0x5229) so
//   the pc264/ES40 console recognizes the controller it expects and proceeds
//   into the taskfile (IDENTIFY).
//
// CAPABILITY PROFILE = POLLED PIO (2026-07-12, _PROVISIONAL).  The shared
//   AtaTaskfileEngine is polled: it never asserts an interrupt and does no
//   bus-master DMA.  So this config advertises ONLY what the device backs --
//   interrupt-pin 0 (no INTA), no relocatable BARs, prog-IF 0x00 (compatibility
//   mode, fixed 0x1F0/0x170 taskfile).  This mirrors the Cypress CY82C693 that
//   the SRM already POLLS successfully on DS10/DS20.  Advertising the real
//   M5229 INTA (IP=0x01) + bus-master BAR4 made the ES40 console try to wire an
//   interrupt the engine never fires -> vector-allocation collision (0x00a8) ->
//   NXM/hang (run_es40_showdev_20260712_164743.log: 70 config reads, ZERO
//   taskfile access).  Presenting a capability the device does not implement
//   violates the faithful-implementation rule; the polled profile is the
//   inert-truthful state for the current engine.
//
//   TODO(ali-ide-dma-irq): Phase B (OS bring-up) -- model the M5229 in native
//   mode faithfully: interrupt-pin INTA (0x3D=0x01), bus-master IDE (BAR4 BMIDE
//   register block + PRD DMA), datasheet BAR reset values (BAI 0x1F1 ...), and
//   the ALi M1543C bridge PIRQ/INTAJ routing (0x44 / 0x48-0x4B) so INTA maps to
//   a valid Cchip DRIR bit + non-colliding vector, plus engine interrupt
//   assertion on command completion.  Datasheet Sec 4.1.2; axpbox
//   AliM1543C_ide.cpp.  The SRM console POLLS the IDE (does not need this); an
//   OS driver will.  See journals/20260712_es40_ali_m5229_ide_faithful_spec.md.
//
// Identity map -- ALi M1543C Datasheet v1.10 Sec 4.1.2 (IDSEL=AD27):
//   00 VID 0x10B9   02 DID 0x5229   06 STATUS 0x0280   08 RID 0xC1
//   09-0B CC 0x010100 (prog-IF pinned 0x00 here = compat/polled; datasheet
//         reset is 0xFA -- deferred with the DMA/interrupt model above).
// Class-code bytes 0x09-0x0B are pinned READ-ONLY (project decision Q3).
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_ALIM5229IDE_H
#define DEVICELIB_TSUNAMI_ALIM5229IDE_H

#include <cstdint>
#include <cstdio>    // EMULATR_IDE_TRACE config-cycle trace
#include <cstdlib>
#include <memory>
#include <string>

#include "deviceLib/Tsunami/ITsunamiIde.h"
#include "deviceLib/Tsunami/AtaTaskfileEngine.h"
#include "deviceLib/Tsunami/PciConfigSpace.h"

class AliM5229Ide : public ITsunamiIde
{
public:
    AliM5229Ide() noexcept { initConfig(); }   // engine self-resets in its own ctor

    // ---- ATA engine forwarders (ITsunamiIde) -------------------------------
    void attachDevice(int channel, int unit, scsi::VirtualScsiDevice* dev) noexcept override
    { m_eng.attachDevice(channel, unit, dev); }
    bool attachMedia(int channel, int unit, std::unique_ptr<scsi::IBlockMedia> media) noexcept override
    { return m_eng.attachMedia(channel, unit, std::move(media)); }
    void reset() noexcept override { m_eng.reset(); }

    // convenience (parity with Cy82C693Ide; not on the interface)
    bool attachDisk(int channel, int unit, const std::string& path) noexcept
    { return m_eng.attachDisk(channel, unit, path); }

    // ---- IIoPortHandler (legacy taskfile windows -> engine) ----------------
    uint64_t ioRead(uint16_t port, uint8_t width) override
    { return m_eng.ioRead(port, width); }
    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    { m_eng.ioWrite(port, value, width); }

    // ---- IPciDeviceHandler -- Function 1 M5229 config space ----------------
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
    AtaTaskfileEngine m_eng;   // shared ATA/ATAPI taskfile engine (polled PIO)
    PciConfigSpace    m_cfg;   // Function-1 PCI config space (ALi M5229 identity)

    // Seed the M5229 IDENTITY with a POLLED-PIO capability profile (see the file
    // header): faithful vendor/device/class so the console recognizes it, but no
    // INTA / no BARs / prog-IF compat, matching what the polled engine actually
    // implements.  All bytes writable then the RO set applied (identity 00-03,
    // RID+class 08-0B per Q3, header type 0E).  BARs stay writable+0 (like the
    // Cypress path): a console size-probe reads back what it writes; no BAR.
    void initConfig() noexcept
    {
        m_cfg.fillZero();
        m_cfg.setAllWritable();
        m_cfg.seedByte(0x00, 0xB9); m_cfg.seedByte(0x01, 0x10);  // vendor 0x10B9 (Acer Labs)
        m_cfg.seedByte(0x02, 0x29); m_cfg.seedByte(0x03, 0x52);  // device 0x5229 (M5229 IDE)
        m_cfg.seedByte(0x06, 0x00); m_cfg.seedByte(0x07, 0x02);  // status 0x0280
        m_cfg.seedByte(0x08, 0xC1);                               // revision 0xC1
        m_cfg.seedByte(0x09, 0x00);                               // prog-IF 0x00 compat _PROVISIONAL
        m_cfg.seedByte(0x0A, 0x01);                               // subclass: IDE
        m_cfg.seedByte(0x0B, 0x01);                               // base class: mass storage
        m_cfg.seedByte(0x0E, 0x00);                               // header type 0
        // NO interrupt pin (0x3D=0), NO BARs (0x10-0x23=0), NO Min_Gnt/Max_Lat:
        // polled-PIO profile.  Full native/DMA/IRQ deferred -> TODO(ali-ide-dma-irq).
        m_cfg.setReadOnlyRange(0x00, 0x03);   // VID / DID
        m_cfg.setReadOnlyRange(0x08, 0x0B);   // RID + class code (Q3: RO)
        m_cfg.setReadOnly(0x0E);              // header type
        // BARs 0x10-0x27 pinned READ-ONLY ZERO (2026-07-12): the ES40 console
        // size-probes the BARs (writes 0xFFFFFFFF) as native-mode PCI; a WRITABLE
        // BAR reads back 0xFFFFFFFF -> the console assigns relocatable I/O and
        // drives the M5229 in NATIVE mode (BAR-relative taskfile), which the
        // polled engine's fixed 0x1F0/0x170 ports do NOT answer -> no IDENTIFY.
        // RO-zero BARs report "no relocatable BAR" so the console uses
        // COMPATIBILITY mode (fixed ports) and polls -- matching the engine.
        // _PROVISIONAL: real M5229 has relocatable BARs; deferred with the native
        // model -> TODO(ali-ide-dma-irq).
        m_cfg.setReadOnlyRange(0x10, 0x27);   // BAR0..BAR5 (+ 0x24/0x27) = no BAR
    }
};

#endif // DEVICELIB_TSUNAMI_ALIM5229IDE_H
