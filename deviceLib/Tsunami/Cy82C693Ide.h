// ============================================================================
// deviceLib/Tsunami/Cy82C693Ide.h -- Cypress CY82C693 IDE controller (Func 1)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// IDE/ATAPI scaffold (journals/IDE_ATAPI_CD_Scaffold_Spec_20260607.md).
//   S2: ATA taskfile registers + ATA/ATAPI DETECTION (signature + IDENTIFY
//       abort) + contract C1 (absent unit = BSY-clear, never 0xFF).
//   S3: ATAPI PACKET transport -- PACKET (0xA0) captures a 12-byte SCSI CDB via
//       the data register, dispatches it to the attached scsi::VirtualScsiDevice
//       (S1), and streams the response (or the 02/3A/00 no-media sense) back via
//       PIO data-register reads.  PIDENTIFY (0xA1) returns the ATAPI identify.
//   S4: Function-1 PCI config space (IPciDeviceHandler) so the firmware
//       enumerates the controller; legacy compatibility-mode ports for the data
//       path.  Identity is _PROVISIONAL (spec 7.5 / contract C3).
//   S5: ATA FIXED-DISK path (2026-06-11) so the console DQDRIVER enumerates and
//       boots dqa0.  Adds ATA IDENTIFY DEVICE (0xEC) returning a real 256-word
//       identify block, READ SECTORS (0x20/0x21) PIO from a raw flat image
//       (512-byte logical sectors, byte offset = LBA*512), and clean-success
//       handling of EXECUTE DEVICE DIAGNOSTIC (0x90) / SET FEATURES (0xEF) /
//       INITIALIZE DEVICE PARAMETERS (0x91).  Authoritative source: apisrm
//       dq_driver.c (struct identify field layout, COMMAND_READ_SECTOR 0x20,
//       COMMAND_IDENTIFY 0xEC) -- NOT the web ALi/M5229 spec, which mis-named
//       the DS10 southbridge; pc264_io.c includes cy82c693_def.h (Cypress).
//       The console POLLS (dq_wait_on_busy / dq_wait_for_drq; Cypress IDE IRQ
//       routing is programmed to 0x80 = disabled), so NO interrupt wiring is
//       required: BSY is modeled transient and DRQ/DRDY drive the poll.  A
//       backing image is opened per READ (fopen/fseek/fread/fclose) so no FILE*
//       lifetime or copy/dtor concern, and the model stays snapshot-clean (the
//       image file is the persistent backing, not serialized machine state).
//
// PIO byte order: data register is 16-bit little-endian (insw/outsw); CDB and
// data-in stream are low-byte-first.  ATAPI "interrupt reason" (sector-count
// register): bit0 CoD (1=command/CDB, 0=data), bit1 IO (1=to host).
// ============================================================================
/*
 *  Test
     EMULATR_NO_AUTOLOAD=1 EMULATR_IDE_TRACE=1 \
      ./out/build/relwithdebinfo/Emulatr.exe \
      --firmware firmware/ds10_v7_3.exe --autosnapshot off 2> run_ide.log
    grep IDE-TRACE run_ide.log | head -80
 */

#ifndef DEVICELIB_TSUNAMI_CY82C693IDE_H
#define DEVICELIB_TSUNAMI_CY82C693IDE_H

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>    // TEMP IDE probe trace (EMULATR_IDE_TRACE) -- strip per roadmap S0
#include <cstdlib>
#include <memory>    // IBlockMedia seam: per-unit backing
#include <string>    // disk attach-by-path convenience

#include "chipsetLib/IDeviceHandlers.h"          // IIoPortHandler, IPciDeviceHandler
#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/ScsiCommand.h"
#include "deviceLib/scsi/ScsiSenseData.h"
#include "deviceLib/scsi/VirtualScsiDevice.h"
#include "deviceLib/scsi/IBlockMedia.h"          // byte-sourcing seam (2026-06-12)
#include "deviceLib/scsi/FileBlockMedia.h"       // attach-by-path convenience

class Cy82C693Ide : public IIoPortHandler, public IPciDeviceHandler
{
public:
    // ---- ATA status / error bits, commands, signature ----------------------
    static constexpr uint8_t kBSY  = 0x80;
    static constexpr uint8_t kDRDY = 0x40;
    static constexpr uint8_t kDF   = 0x20;
    static constexpr uint8_t kDSC  = 0x10;
    static constexpr uint8_t kDRQ  = 0x08;
    static constexpr uint8_t kERR  = 0x01;
    static constexpr uint8_t kERR_ABRT = 0x04;

    static constexpr uint8_t kCMD_DEVICE_RESET = 0x08;
    static constexpr uint8_t kCMD_PIDENTIFY    = 0xA1;
    static constexpr uint8_t kCMD_PACKET       = 0xA0;
    static constexpr uint8_t kCMD_IDENTIFY     = 0xEC;
    // S5 ATA fixed-disk command set (apisrm dq_driver.c: COMMAND_READ_SECTOR
    // 0x20, COMMAND_IDENTIFY 0xEC).  0x21/0x90/0xEF/0x91 are accepted so the
    // console probe never hangs BSY on a command it issues during attach.
    static constexpr uint8_t kCMD_READ_SECTORS    = 0x20; // PIO read, with retry
    static constexpr uint8_t kCMD_READ_SECTORS_NR = 0x21; // PIO read, no retry
    static constexpr uint8_t kCMD_EXEC_DIAG       = 0x90; // EXECUTE DEVICE DIAGNOSTIC
    static constexpr uint8_t kCMD_SET_FEATURES    = 0xEF; // SET FEATURES (no-op ok)
    static constexpr uint8_t kCMD_INIT_DEV_PARAMS = 0x91; // INITIALIZE DEVICE PARAMETERS

    static constexpr uint32_t kSectorBytes = 512;        // ATA logical sector

    static constexpr uint8_t kAtapiSigLo = 0x14;
    static constexpr uint8_t kAtapiSigHi = 0xEB;

    static constexpr uint16_t kPriCmdBase = 0x1F0;
    static constexpr uint16_t kSecCmdBase = 0x170;
    static constexpr uint16_t kPriCtrl    = 0x3F6;
    static constexpr uint16_t kSecCtrl    = 0x376;

    Cy82C693Ide() noexcept { initConfig(); reset(); }

    void attachDevice(int channel, int unit, scsi::VirtualScsiDevice* dev) noexcept
    {
        if (channel < 0 || channel > 1 || unit < 0 || unit > 1) return;
        m_chan[channel].dev[unit] = dev;
        if (channel == m_curChan && unit == selectedUnit(channel))
            loadSignature(channel, unit);
    }

    // S5: attach a raw flat ATA disk image to (channel, unit).  Opens the file
    // once to derive capacity (size / 512) and a plausible LBA-friendly CHS
    // (16 heads, 63 sectors/track); the image is re-opened per READ.  Returns
    // true iff the file opened and held at least one sector.  An empty path or a
    // missing/zero-length file leaves the slot "no device present" (the probe
    // skips it cleanly, exactly as today).
    // Seam: attach an already-open 512-byte block medium to (channel, unit).
    // Derives capacity (blockCount) and a plausible LBA-friendly CHS (16 heads,
    // 63 sectors/track).  Returns true iff the medium is open + present with at
    // least one sector; nullptr or a not-ready medium detaches the slot ("no
    // device present", the probe skips it cleanly).
    bool attachMedia(int channel, int unit, std::unique_ptr<scsi::IBlockMedia> media) noexcept
    {
        if (channel < 0 || channel > 1 || unit < 0 || unit > 1) return false;
        Disk& d = m_chan[channel].disk[unit];
        d = Disk{};                                   // detach any prior image
        if (!media || !media->isOpen() || !media->isPresent()) return false;
        if (media->blockCount() == 0u) return false;
        d.totalSectors = media->blockCount();
        d.heads        = 16;
        d.sectors      = 63;
        uint64_t cyl   = d.totalSectors / (static_cast<uint64_t>(d.heads) * d.sectors);
        if (cyl > 65535u) cyl = 65535u;               // CHS word fields cap at 16 bits
        if (cyl == 0u)    cyl = 1u;
        d.cylinders    = static_cast<uint16_t>(cyl);
        d.media        = std::move(media);
        if (channel == m_curChan && unit == selectedUnit(channel))
            loadSignature(channel, unit);
        return true;
    }

    // Convenience: attach a read-write 512-byte raw flat image by path (tests /
    // simple wiring).  An empty path detaches the slot; a missing/too-small file
    // leaves it empty.  The drive itself opens nothing -- FileBlockMedia does.
    bool attachDisk(int channel, int unit, const std::string& path) noexcept
    {
        if (path.empty()) {                           // explicit detach
            if (channel >= 0 && channel <= 1 && unit >= 0 && unit <= 1) {
                m_chan[channel].disk[unit] = Disk{};
                if (channel == m_curChan && unit == selectedUnit(channel))
                    loadSignature(channel, unit);
            }
            return false;
        }
        auto m = std::make_unique<scsi::FileBlockMedia>(path, kSectorBytes, /*readOnly=*/false);
        if (m->open() != scsi::MediaStatus::Ok) return false;
        return attachMedia(channel, unit, std::move(m));
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) {
            Channel& ch = m_chan[c];
            ch.features = ch.error = ch.sectorCount = 0;
            ch.lbaLow = ch.lbaMid = ch.lbaHigh = ch.driveHead = ch.devCtrl = 0;
            ch.phase = Phase::Idle;
            ch.cdbPos = 0; ch.pioPos = ch.pioLen = 0;
            loadSignature(c, 0);
        }
        m_curChan = 0;
    }

    // ---- IIoPortHandler (legacy taskfile windows) --------------------------
    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        uint64_t r;
        if      (port >= kPriCmdBase && port <= kPriCmdBase + 7) r = cmdRead(0, port - kPriCmdBase, width);
        else if (port >= kSecCmdBase && port <= kSecCmdBase + 7) r = cmdRead(1, port - kSecCmdBase, width);
        else if (port == kPriCtrl) r = m_chan[0].status;
        else if (port == kSecCtrl) r = m_chan[1].status;
        else r = 0xFFULL;
        ideTrace('R', port, r, width);
        return r;
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);
        ideTrace('W', port, value, width);
        if (port >= kPriCmdBase && port <= kPriCmdBase + 7) { cmdWrite(0, port - kPriCmdBase, value, width); return; }
        if (port >= kSecCmdBase && port <= kSecCmdBase + 7) { cmdWrite(1, port - kSecCmdBase, value, width); return; }
        if (port == kPriCtrl) { ctrlWrite(0, v); return; }
        if (port == kSecCtrl) { ctrlWrite(1, v); return; }
    }

    // TEMP read-only probe trace (EMULATR_IDE_TRACE=1). Strip per roadmap S0.
    // Zero behavior change when env unset (static bool short-circuits).
    void ideTrace(char rw, uint16_t port, uint64_t val, uint8_t width) const noexcept
    {
        static bool const on = (std::getenv("EMULATR_IDE_TRACE") != nullptr);
        if (!on) return;
        int const ch = (port >= kSecCmdBase && port <= kSecCmdBase + 7) ? 1 : 0;
        std::fprintf(stderr,
            "IDE-TRACE %c port=0x%03X ch%d reg%d w=%u val=0x%02llX st=0x%02X ph=%d\n",
            rw, port, ch, (port & 0x7), width,
            static_cast<unsigned long long>(val & 0xFFu),
            m_chan[ch].status, static_cast<int>(m_chan[ch].phase));
    }

    // ---- IPciDeviceHandler -- Function 1 config space (legacy/compat IDE) ----
    // Identity _PROVISIONAL (spec 7.5 / C3): vendor 0x1080 (Cypress), device
    // 0xC693 (IDE function ID may differ -- confirm vs a real dump), class 0x0101
    // (mass storage / IDE), prog-IF 0x00 (legacy ports 0x1F0/0x170).
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) override
    {
        uint32_t v = 0;
        for (int i = 0; i < width && (reg + i) < 256; ++i)
            v |= static_cast<uint32_t>(m_cfg[static_cast<size_t>(reg + i)]) << (8 * i);
        static bool const cfgOn = (std::getenv("EMULATR_IDE_TRACE") != nullptr);
        if (cfgOn)
            std::fprintf(stderr, "IDE-TRACE C cfg reg=0x%02X w=%u val=0x%08X\n",
                         reg, width, v);
        return v;
    }
    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) override
    {
        for (int i = 0; i < width && (reg + i) < 256; ++i) {
            int const off = reg + i;
            if (off <= 0x03 || (off >= 0x08 && off <= 0x0B) || off == 0x0E)
                continue;                       // read-only identity header
            m_cfg[static_cast<size_t>(off)] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
        }
    }

    // Attach a SCSI/ATAPI target to (channel 0|1, unit 0=master|1=slave).
    // (Inspection accessors for doctest.)
    [[nodiscard]] uint8_t status(int ch) const noexcept { return m_chan[ch & 1].status; }
    [[nodiscard]] uint8_t error(int ch)  const noexcept { return m_chan[ch & 1].error; }
    [[nodiscard]] int     selectedUnit(int ch) const noexcept { return (m_chan[ch & 1].driveHead >> 4) & 1; }

private:
    enum class Phase : uint8_t { Idle, AwaitCdb, DataIn, Complete };

    // ATA fixed-disk backing one (channel, unit) slot.  The byte source is an
    // IBlockMedia (FileBlockMedia for a flat image, MockBlockMedia in tests);
    // the drive does not open files itself (seam, 2026-06-12).
    struct Disk {
        std::unique_ptr<scsi::IBlockMedia> media;     // 512-byte block backing
        uint64_t    totalSectors = 0;
        uint16_t    cylinders    = 0;
        uint16_t    heads        = 0;
        uint16_t    sectors      = 0;

        [[nodiscard]] bool isAttached() const noexcept {
            return media && media->isOpen() && media->isPresent();
        }
    };

    struct Channel {
        uint8_t features = 0, error = 0, sectorCount = 0;
        uint8_t lbaLow = 0, lbaMid = 0, lbaHigh = 0, driveHead = 0;
        uint8_t status = 0, devCtrl = 0;
        scsi::VirtualScsiDevice* dev[2] = { nullptr, nullptr };
        Disk                     disk[2];          // S5: ATA fixed-disk per unit

        Phase                       phase  = Phase::Idle;
        uint8_t                     cdb[12] = { 0 };
        int                         cdbPos = 0;
        std::array<uint8_t, 2048>   pio{};
        uint32_t                    pioPos = 0;
        uint32_t                    pioLen = 0;
        uint64_t                    diskLba       = 0;   // S5: next LBA to stream
        uint32_t                    diskRemaining = 0;   // S5: sectors left after current
    };
    Channel m_chan[2];
    int     m_curChan = 0;
    std::array<uint8_t, 256> m_cfg{};            // Function-1 PCI config space

    void initConfig() noexcept
    {
        m_cfg.fill(0);
        m_cfg[0x00] = 0x80; m_cfg[0x01] = 0x10;  // vendor 0x1080   _PROVISIONAL
        m_cfg[0x02] = 0x93; m_cfg[0x03] = 0xC6;  // device 0xC693   _PROVISIONAL
        m_cfg[0x08] = 0x00;                       // revision
        m_cfg[0x09] = 0x00;                       // prog-IF (legacy) _PROVISIONAL
        m_cfg[0x0A] = 0x01;                       // subclass: IDE
        m_cfg[0x0B] = 0x01;                       // base class: mass storage
        m_cfg[0x0E] = 0x00;                       // header type 0 (single function)
        // BARs (0x10..) left 0: legacy/compat IDE answers fixed ports.
    }

    [[nodiscard]] bool present(int ch, int unit) const noexcept
    {
        return m_chan[ch].dev[unit] != nullptr || m_chan[ch].disk[unit].isAttached();
    }
    [[nodiscard]] bool isDisk(int ch, int unit) const noexcept
    {
        return m_chan[ch].disk[unit].isAttached();      // ATA fixed disk slot
    }
    [[nodiscard]] bool isAtapi(int ch, int unit) const noexcept
    {
        scsi::VirtualScsiDevice* d = m_chan[ch].dev[unit];
        return d && d->deviceType() == scsi::ScsiPeripheralDeviceType::CdDvdDevice;
    }

    void loadSignature(int ch, int unit) noexcept
    {
        Channel& c = m_chan[ch];
        c.phase = Phase::Idle; c.cdbPos = 0; c.pioPos = c.pioLen = 0;
        if (!present(ch, unit)) {                 // C1: BSY clear, no DRDY, not 0xFF
            c.status = 0x00; c.error = 0x00;
            c.sectorCount = c.lbaLow = c.lbaMid = c.lbaHigh = 0x00;
            return;
        }
        c.status = static_cast<uint8_t>(kDRDY | kDSC);
        c.error  = 0x00; c.sectorCount = 0x01; c.lbaLow = 0x01;
        if (isAtapi(ch, unit)) { c.lbaMid = kAtapiSigLo; c.lbaHigh = kAtapiSigHi; }
        else                   { c.lbaMid = 0x00;        c.lbaHigh = 0x00;        }
    }

    uint64_t cmdRead(int ch, int reg, uint8_t width) noexcept
    {
        m_curChan = ch;
        Channel& c = m_chan[ch];
        switch (reg) {
        case 0: {                                  // data register (16-bit PIO)
            (void) width;
            if (c.phase != Phase::DataIn) return 0x0000ULL;
            uint16_t w = 0;
            if (c.pioPos       < c.pioLen) w  = c.pio[c.pioPos];
            if (c.pioPos + 1u  < c.pioLen) w |= static_cast<uint16_t>(c.pio[c.pioPos + 1]) << 8;
            c.pioPos += 2u;
            if (c.pioPos >= c.pioLen) pioDrained(ch);   // S5: next sector or complete
            return w;
        }
        case 1: return c.error;
        case 2: return c.sectorCount;
        case 3: return c.lbaLow;
        case 4: return c.lbaMid;
        case 5: return c.lbaHigh;
        case 6: return c.driveHead;
        case 7: return c.status;
        default: return 0xFFULL;
        }
    }

    void cmdWrite(int ch, int reg, uint64_t value, uint8_t width) noexcept
    {
        m_curChan = ch;
        Channel& c = m_chan[ch];
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);
        switch (reg) {
        case 0:                                    // data register (CDB / data-out)
            (void) width;
            if (c.phase == Phase::AwaitCdb) {
                if (c.cdbPos < 12) c.cdb[c.cdbPos++] = static_cast<uint8_t>(value & 0xFFu);
                if (c.cdbPos < 12) c.cdb[c.cdbPos++] = static_cast<uint8_t>((value >> 8) & 0xFFu);
                if (c.cdbPos >= 12) executePacket(ch);
            }
            break;
        case 1: c.features    = v; break;
        case 2: c.sectorCount = v; break;
        case 3: c.lbaLow      = v; break;
        case 4: c.lbaMid      = v; break;
        case 5: c.lbaHigh     = v; break;
        case 6: c.driveHead = v; loadSignature(ch, (v >> 4) & 1); break;
        case 7: handleCommand(ch, v); break;
        default: break;
        }
    }

    void ctrlWrite(int ch, uint8_t v) noexcept
    {
        Channel& c = m_chan[ch];
        bool const srstWas = (c.devCtrl & 0x04) != 0;
        c.devCtrl = v;
        if (srstWas && (v & 0x04) == 0) loadSignature(ch, selectedUnit(ch));
    }

    void handleCommand(int ch, uint8_t cmd) noexcept
    {
        Channel& c = m_chan[ch];
        int const unit = selectedUnit(ch);
        if (!present(ch, unit)) { c.status = 0x00; return; }   // C1
        if (isDisk(ch, unit)) { handleDiskCommand(ch, unit, cmd); return; } // S5

        switch (cmd) {
        case kCMD_DEVICE_RESET:
            loadSignature(ch, unit);
            break;

        case kCMD_IDENTIFY:                        // ATA IDENTIFY DEVICE
            if (isAtapi(ch, unit)) {               // ATAPI aborts + posts signature
                c.error   = kERR_ABRT;
                c.status  = static_cast<uint8_t>(kDRDY | kERR);
                c.lbaMid  = kAtapiSigLo; c.lbaHigh = kAtapiSigHi;
            } else {
                c.error = kERR_ABRT; c.status = static_cast<uint8_t>(kDRDY | kERR);
            }
            break;

        case kCMD_PIDENTIFY:                        // ATAPI IDENTIFY PACKET DEVICE
            if (!isAtapi(ch, unit)) { c.error = kERR_ABRT; c.status = static_cast<uint8_t>(kDRDY | kERR); break; }
            buildAtapiIdentify(ch);
            c.pioLen = 512; c.pioPos = 0;
            c.sectorCount = 0x02;
            c.status = static_cast<uint8_t>(kDRDY | kDRQ);
            c.error  = 0x00; c.phase = Phase::DataIn;
            break;

        case kCMD_PACKET:                           // ATAPI PACKET -- await 12-byte CDB
            if (!isAtapi(ch, unit)) { c.error = kERR_ABRT; c.status = static_cast<uint8_t>(kDRDY | kERR); break; }
            c.phase = Phase::AwaitCdb; c.cdbPos = 0;
            c.sectorCount = 0x01;                   // command phase (CoD=1, IO=0)
            c.status = static_cast<uint8_t>(kDRDY | kDRQ);
            c.error  = 0x00;
            break;

        default:
            c.error = kERR_ABRT; c.status = static_cast<uint8_t>(kDRDY | kERR);
            break;
        }
    }

    void executePacket(int ch) noexcept
    {
        Channel& c = m_chan[ch];
        scsi::VirtualScsiDevice* dev = c.dev[selectedUnit(ch)];
        if (!dev) { packetComplete(ch); return; }

        scsi::ScsiCommand cmd;
        cmd.cdb = c.cdb; cmd.cdbLength = 12;
        cmd.dataBuffer = c.pio.data();
        cmd.dataBufferLength = static_cast<uint32_t>(c.pio.size());
        dev->handleCommand(cmd);

        if (cmd.status == scsi::ScsiStatus::CheckCondition) {
            uint8_t const key = cmd.senseData.bytes()[2] & 0x0Fu;   // e.g. NotReady=2
            c.error  = static_cast<uint8_t>((key << 4) | kERR_ABRT);
            c.status = static_cast<uint8_t>(kDRDY | kERR);
            c.sectorCount = 0x03;
            c.phase = Phase::Complete; c.pioPos = c.pioLen = 0;
            return;
        }
        if (cmd.dataTransferred > 0) {
            c.pioLen = cmd.dataTransferred; c.pioPos = 0;
            c.lbaMid  = static_cast<uint8_t>(c.pioLen & 0xFFu);
            c.lbaHigh = static_cast<uint8_t>((c.pioLen >> 8) & 0xFFu);
            c.sectorCount = 0x02;
            c.status = static_cast<uint8_t>(kDRDY | kDRQ);
            c.error  = 0x00; c.phase = Phase::DataIn;
            return;
        }
        packetComplete(ch);
    }

    void packetComplete(int ch) noexcept
    {
        Channel& c = m_chan[ch];
        c.status = static_cast<uint8_t>(kDRDY | kDSC);
        c.error = 0x00; c.sectorCount = 0x03;
        c.phase = Phase::Complete; c.pioPos = c.pioLen = 0;
    }

    void buildAtapiIdentify(int ch) noexcept
    {
        Channel& c = m_chan[ch];
        c.pio.fill(0);
        c.pio[0] = 0xC0; c.pio[1] = 0x85;          // word0: ATAPI CD config (0x85C0)
        static const char model[] = "EMULATR VIRTUAL CDROM                   ";
        for (int i = 0; i < 40; i += 2) {
            c.pio[static_cast<size_t>(54 + i)]     = static_cast<uint8_t>(model[i + 1]);
            c.pio[static_cast<size_t>(54 + i + 1)] = static_cast<uint8_t>(model[i]);
        }
    }

    // ===== S5: ATA fixed-disk path =========================================

    // Data-register buffer fully drained.  A disk read with sectors still
    // pending loads the next one and keeps DRQ; everything else (ATAPI single
    // transfer, ATA IDENTIFY, last disk sector) completes the command.
    void pioDrained(int ch) noexcept
    {
        if (m_chan[ch].diskRemaining > 0u) { loadDiskSector(ch); return; }
        packetComplete(ch);
    }

    // ATA command dispatch for a fixed-disk unit (BSY modeled transient; the
    // console polls Status/DRQ, no interrupt -- Cypress IDE IRQ routing 0x80).
    void handleDiskCommand(int ch, int unit, uint8_t cmd) noexcept
    {
        Channel& c = m_chan[ch];
        switch (cmd) {
        case kCMD_IDENTIFY:                         // ATA IDENTIFY DEVICE -> 512 B
            buildAtaIdentify(ch, unit);
            c.pioLen = kSectorBytes; c.pioPos = 0; c.diskRemaining = 0;
            c.status = static_cast<uint8_t>(kDRDY | kDRQ | kDSC);
            c.error  = 0x00; c.phase = Phase::DataIn;
            break;

        case kCMD_READ_SECTORS:
        case kCMD_READ_SECTORS_NR:
            startDiskRead(ch, unit);
            break;

        case kCMD_EXEC_DIAG:                        // device 0 passed (code 0x01)
            c.error  = 0x01;
            c.status = static_cast<uint8_t>(kDRDY | kDSC);
            break;

        case kCMD_SET_FEATURES:                     // accept + no-op
        case kCMD_INIT_DEV_PARAMS:                  // accept CHS set
            c.error  = 0x00;
            c.status = static_cast<uint8_t>(kDRDY | kDSC);
            break;

        case kCMD_DEVICE_RESET:
            loadSignature(ch, unit);
            break;

        default:                                    // unimplemented: clean ABRT,
            c.error  = kERR_ABRT;                   // never a hung BSY
            c.status = static_cast<uint8_t>(kDRDY | kERR);
            break;
        }
    }

    // Decode the taskfile LBA28 + sector count and begin a PIO read.
    void startDiskRead(int ch, int unit) noexcept
    {
        Channel& c = m_chan[ch];
        Disk&    d = c.disk[unit];

        // LBA28: bits 27..24 = driveHead<3:0>; 23..0 = the LBA taskfile regs.
        // Sector count 0 means 256.
        uint64_t const lba =
              (static_cast<uint64_t>(c.lbaLow))
            | (static_cast<uint64_t>(c.lbaMid)  << 8)
            | (static_cast<uint64_t>(c.lbaHigh) << 16)
            | (static_cast<uint64_t>(c.driveHead & 0x0Fu) << 24);
        uint32_t const count = (c.sectorCount == 0u) ? 256u : c.sectorCount;

        if (lba + count > d.totalSectors) {         // out of range -> ABRT, no DRQ
            c.error  = kERR_ABRT;
            c.status = static_cast<uint8_t>(kDRDY | kERR);
            c.phase  = Phase::Complete; c.pioPos = c.pioLen = 0; c.diskRemaining = 0;
            return;
        }
        c.diskLba       = lba;
        c.diskRemaining = count;
        loadDiskSector(ch);
    }

    // Read one 512-byte sector at c.diskLba from the backing image into the PIO
    // buffer and present it (DRQ).  The image is opened per sector so there is
    // no FILE* lifetime; reads are infrequent at console boot.
    void loadDiskSector(int ch) noexcept
    {
        Channel& c = m_chan[ch];
        Disk&    d = c.disk[selectedUnit(ch)];
        c.pio.fill(0);
        scsi::MediaStatus const st = d.media
            ? d.media->read(c.diskLba, 1, c.pio.data())   // one 512-byte sector
            : scsi::MediaStatus::NotOpen;
        if (st != scsi::MediaStatus::Ok) {                // unreadable media -> abort
            c.error  = kERR_ABRT;
            c.status = static_cast<uint8_t>(kDRDY | kERR);
            c.phase  = Phase::Complete; c.pioPos = c.pioLen = 0; c.diskRemaining = 0;
            return;
        }
        c.pioPos = 0; c.pioLen = kSectorBytes;
        c.status = static_cast<uint8_t>(kDRDY | kDRQ | kDSC);
        c.error  = 0x00; c.phase = Phase::DataIn;
        ++c.diskLba;
        --c.diskRemaining;
    }

    // Populate the 256-word ATA IDENTIFY block.  Field set = exactly what the
    // apisrm console reads (dq_driver.c struct identify): config(0),
    // cylinders(1)/heads(3)/sectors(6), serial(10-19), fw(23-26),
    // model(27-46), capabilities(49) LBA bit, current CHS + capacity(54-58),
    // and LBA28 total sectors(60-61).
    void buildAtaIdentify(int ch, int unit) noexcept
    {
        Channel& c = m_chan[ch];
        Disk&    d = c.disk[unit];
        c.pio.fill(0);
        setIdWord(ch, 0,  0x0040);                  // fixed, non-removable ATA
        setIdWord(ch, 1,  d.cylinders);
        setIdWord(ch, 3,  d.heads);
        setIdWord(ch, 6,  d.sectors);
        setIdStr (ch, 10, "EMULATR-0001", 20);      // serial number
        setIdStr (ch, 23, "1.0", 8);                // firmware revision
        setIdStr (ch, 27, "EMULATR VIRTUAL DISK", 40); // model -> show device
        setIdWord(ch, 47, 0x0000);                  // no READ/WRITE MULTIPLE
        setIdWord(ch, 49, 0x0200);                  // capabilities: LBA supported
        setIdWord(ch, 53, 0x0001);                  // words 54-58 valid
        setIdWord(ch, 54, d.cylinders);
        setIdWord(ch, 55, d.heads);
        setIdWord(ch, 56, d.sectors);
        uint32_t const cap = static_cast<uint32_t>(d.totalSectors & 0xFFFFFFFFu);
        setIdWord(ch, 57, static_cast<uint16_t>(cap & 0xFFFFu));         // capacity lo
        setIdWord(ch, 58, static_cast<uint16_t>((cap >> 16) & 0xFFFFu)); // capacity hi
        setIdWord(ch, 60, static_cast<uint16_t>(cap & 0xFFFFu));         // LBA28 lo
        setIdWord(ch, 61, static_cast<uint16_t>((cap >> 16) & 0xFFFFu)); // LBA28 hi
    }

    void setIdWord(int ch, int word, uint16_t val) noexcept
    {
        Channel& c = m_chan[ch];
        size_t const off = static_cast<size_t>(word) * 2u;
        c.pio[off]     = static_cast<uint8_t>(val & 0xFFu);
        c.pio[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFFu);
    }

    // ASCII identify fields: space-padded, byte-swapped within each 16-bit word
    // (ATA convention; mirrors the ATAPI model decode and dq_driver's reader).
    void setIdStr(int ch, int firstWord, const char* s, int maxChars) noexcept
    {
        Channel& c = m_chan[ch];
        size_t const base = static_cast<size_t>(firstWord) * 2u;
        int const len = static_cast<int>(std::strlen(s));
        for (int i = 0; i < maxChars; i += 2) {
            char const c0 = (i     < len) ? s[i]     : ' ';
            char const c1 = (i + 1 < len) ? s[i + 1] : ' ';
            c.pio[base + static_cast<size_t>(i)]     = static_cast<uint8_t>(c1);
            c.pio[base + static_cast<size_t>(i) + 1] = static_cast<uint8_t>(c0);
        }
    }
};

#endif // DEVICELIB_TSUNAMI_CY82C693IDE_H
