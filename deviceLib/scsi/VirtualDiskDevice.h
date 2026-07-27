// ============================================================================
// deviceLib/scsi/VirtualDiskDevice.h -- SCSI direct-access (disk) LUN target
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
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
// JRN-SCSI-001 P2: the direct-access (SBC) sibling of VirtualIsoDevice,
// backed by IBlockMedia (512-byte logical blocks).  Serves the NCR 53C810
// HBA's SCSI bus; command surface is the SRM pk class driver's boot set
// (apisrm scsi.c / dk driver): TEST UNIT READY, REQUEST SENSE, INQUIRY,
// MODE SENSE(6/10), READ CAPACITY(10), READ(6)/(10), WRITE(6)/(10),
// START STOP / PREVENT ALLOW (no-op good).  Unknown opcodes return
// CHECK CONDITION / ILLEGAL REQUEST with sense retained for REQUEST SENSE
// -- never silent, per the no-silent-absorbers house rule.
// ============================================================================

#ifndef DEVICELIB_SCSI_VIRTUALDISKDEVICE_H
#define DEVICELIB_SCSI_VIRTUALDISKDEVICE_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "deviceLib/scsi/IBlockMedia.h"
#include "deviceLib/scsi/ScsiCommand.h"
#include "deviceLib/scsi/ScsiSenseData.h"
#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/VirtualScsiDevice.h"

namespace scsi {

class VirtualDiskDevice final : public VirtualScsiDevice {
public:
    explicit VirtualDiskDevice(std::unique_ptr<IBlockMedia> media = nullptr) noexcept
    {
        setMedia(std::move(media));
    }

    void setMedia(std::unique_ptr<IBlockMedia> media) noexcept
    {
        m_media = std::move(media);
        if (m_media && !m_media->isOpen())
            (void) m_media->open();
    }

    bool hasMedia() const noexcept
    {
        return m_media && m_media->isOpen() && m_media->isPresent();
    }

    ScsiPeripheralDeviceType deviceType() const noexcept override
    {
        return ScsiPeripheralDeviceType::DirectAccessBlockDevice;
    }

    void handleCommand(ScsiCommand& cmd) noexcept override
    {
        cmd.dataTransferred = 0;
        // LUN discipline (SPC/SCSI-2; 2026-07-25 fix for the dka0-dka7
        // multi-enumeration): this target serves LUN 0 ONLY.  The SRM class
        // driver probes every LUN of a responding id -- an unsupported LUN
        // must answer INQUIRY with peripheral qualifier 011b / type 1Fh
        // ("no device at this LUN") and CHECK CONDITION (ILLEGAL REQUEST,
        // LOGICAL UNIT NOT SUPPORTED 05/25/00) for everything else, or the
        // console mints a phantom dk unit per LUN.
        if (cmd.lun != 0) {
            if (cmd.opcode() == ScsiOp::INQUIRY) {
                uint8_t buf[36] = {};
                buf[0] = 0x7F;                 // qualifier 011b | type 1Fh
                buf[2] = 0x02;
                buf[3] = 0x02;
                buf[4] = 31;
                cmd.dataTransferred = copyOut(cmd, buf, sizeof(buf));
                cmd.setGood();
                return;
            }
            if (cmd.opcode() == ScsiOp::REQUEST_SENSE) {
                ScsiFixedSenseData const s = ScsiFixedSenseData::make(
                    ScsiSenseKey::IllegalRequest, 0x25, 0x00);
                cmd.dataTransferred = copyOut(cmd, s.bytes(),
                    static_cast<uint32_t>(s.size()));
                cmd.setGood();
                return;
            }
            check(cmd, ScsiSenseKey::IllegalRequest, 0x25, 0x00);
            return;
        }
        switch (cmd.opcode()) {
        case ScsiOp::TEST_UNIT_READY: cmdTestUnitReady(cmd); return;
        case ScsiOp::REQUEST_SENSE:   cmdRequestSense(cmd);  return;
        case ScsiOp::INQUIRY:         cmdInquiry(cmd);       return;
        case 0x1A: /* MODE SENSE(6) */  cmdModeSense(cmd, false); return;
        case ScsiOp::MODE_SENSE10:      cmdModeSense(cmd, true);  return;
        case 0x15: /* MODE SELECT(6) */ cmdModeSelect(cmd, false); return;
        case 0x55: /* MODE SELECT(10) */cmdModeSelect(cmd, true);  return;
        case ScsiOp::READ_CAPACITY10: cmdReadCapacity(cmd);  return;
        case ScsiOp::READ6:           cmdReadWrite(cmd, false, false); return;
        case ScsiOp::READ10:          cmdReadWrite(cmd, true,  false); return;
        case 0x0A: /* WRITE(6) */     cmdReadWrite(cmd, false, true);  return;
        case 0x2A: /* WRITE(10) */    cmdReadWrite(cmd, true,  true);  return;
        case 0x2F: /* VERIFY(10) */   good(cmd);             return;
        case ScsiOp::START_STOP_UNIT: good(cmd);             return;
        case ScsiOp::PREVENT_ALLOW:   good(cmd);             return;
        default:
            std::fprintf(stderr,
                "VirtualDiskDevice: UNSUPPORTED opcode 0x%02X (len %u) -> "
                "ILLEGAL REQUEST\n", cmd.opcode(), cmd.cdbLength);
            check(cmd, ScsiSenseKey::IllegalRequest, 0x20, 0x00); // invalid opcode
            return;
        }
    }

private:
    // ---- status helpers ---------------------------------------------------
    void good(ScsiCommand& cmd) noexcept
    {
        m_lastSense = ScsiFixedSenseData{};
        cmd.setGood();
    }
    void check(ScsiCommand& cmd, ScsiSenseKey key, uint8_t asc, uint8_t ascq) noexcept
    {
        ScsiFixedSenseData const s = ScsiFixedSenseData::make(key, asc, ascq);
        m_lastSense = s;
        cmd.setCheckCondition(s);
    }

    // ---- commands ---------------------------------------------------------
    void cmdTestUnitReady(ScsiCommand& cmd) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }
        good(cmd);
    }

    void cmdRequestSense(ScsiCommand& cmd) noexcept
    {
        cmd.dataTransferred = copyOut(cmd, m_lastSense.bytes(),
                                      static_cast<uint32_t>(m_lastSense.size()));
        m_lastSense = ScsiFixedSenseData{};       // sense is consumed by delivery
        cmd.setGood();
    }

    void cmdInquiry(ScsiCommand& cmd) noexcept
    {
        // Standard INQUIRY data, 36 bytes (SPC-2 subset the SRM probes).
        uint8_t buf[36] = {};
        buf[0] = static_cast<uint8_t>(deviceType());   // direct-access, connected
        buf[1] = 0x00;                                 // non-removable
        buf[2] = 0x02;                                 // SCSI-2
        buf[3] = 0x02;                                 // response data format
        buf[4] = 31;                                   // additional length (36-5)
        std::memcpy(&buf[8],  "EMULATR ",         8);  // T10 vendor (8)
        std::memcpy(&buf[16], "VIRTUAL DISK    ", 16); // product id (16)
        std::memcpy(&buf[32], "0001",             4);  // revision (4)
        cmd.dataTransferred = copyOut(cmd, buf, sizeof(buf));
        good(cmd);
    }

    void cmdModeSense(ScsiCommand& cmd, bool ten) noexcept
    {
        // Minimal: header + 8-byte block descriptor, zero mode pages.  The SRM
        // dk driver only needs the block size / geometry-free identity.
        uint64_t const blocks = hasMedia() ? m_media->blockCount() : 0;
        uint32_t const bs     = hasMedia() ? m_media->blockSize()  : 512;
        uint8_t  buf[16]      = {};
        uint32_t n;
        if (!ten) {
            buf[0] = 11;                       // mode data length (n-1)
            buf[3] = 8;                        // block descriptor length
            put24(&buf[5],  blocks > 0xFFFFFF ? 0 : static_cast<uint32_t>(blocks));
            put24(&buf[9],  bs);               // bytes 9..11 of descriptor
            n = 12;
        } else {
            buf[1] = 14;                       // mode data length low (n-2)
            buf[7] = 8;                        // block descriptor length low
            put24(&buf[9],  blocks > 0xFFFFFF ? 0 : static_cast<uint32_t>(blocks));
            put24(&buf[13], bs);
            n = 16;
        }
        cmd.dataTransferred = copyOut(cmd, buf, n);
        good(cmd);
    }

    // MODE SELECT(6)/(10) -- SCSI-2 8.2.8/8.2.9.  A data-OUT command: the
    // initiator sends a parameter list (header + optional block descriptor +
    // mode pages).  OpenVMS SYSBOOT issues MODE SELECT(6) while bringing the
    // boot device up; without it the device answered ILLEGAL REQUEST and
    // SYSBOOT gave up with %SYSBOOT-F-LDFAIL before ever reading a file
    // (JRN-SCSI-026 Sec 7).
    //
    // This device has FIXED geometry backed by the media object, so there is
    // nothing to reconfigure: the faithful answer is to VALIDATE the list and
    // report GOOD, rejecting only a request that would actually change
    // geometry (a block descriptor naming a different block length), which is
    // the one field a target must not silently ignore -- the initiator would
    // then compute every subsequent LBA against a size we are not using.
    void cmdModeSelect(ScsiCommand& cmd, bool ten) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }

        // Parameter list length: CDB byte 4 (6-byte) or bytes 7..8 (10-byte).
        uint32_t const listLen = ten
            ? ((uint32_t(cmd.cdb[7]) << 8) | uint32_t(cmd.cdb[8]))
            :   uint32_t(cmd.cdb[4]);
        if (listLen == 0) { good(cmd); return; }   // legal: "no parameters"

        // Only inspect what the initiator actually delivered.  NOTE this must
        // come from dataBufferLength, NOT dataTransferred: handleCommand()
        // zeroes dataTransferred on entry (it is the target's OUTPUT), so a
        // data-out command reading it always sees 0.  (Caught by the tests
        // below -- the first cut used dataTransferred and rejected every
        // well-formed parameter list as truncated.)
        uint32_t const avail = (listLen <= cmd.dataBufferLength)
                             ? listLen
                             : cmd.dataBufferLength;
        uint32_t const hdr   = ten ? 8u : 4u;      // parameter list header size
        if (cmd.dataBuffer == nullptr || avail < hdr) {
            // Truncated list -- the initiator promised more than it sent.
            check(cmd, ScsiSenseKey::IllegalRequest, 0x1A, 0x00); // param list len err
            return;
        }

        // Block descriptor length lives at header byte 3 (6-byte) / 6..7 (10).
        uint32_t const bdLen = ten
            ? ((uint32_t(cmd.dataBuffer[6]) << 8) | uint32_t(cmd.dataBuffer[7]))
            :   uint32_t(cmd.dataBuffer[3]);
        if (bdLen >= 8 && avail >= hdr + 8) {
            uint8_t const* bd = cmd.dataBuffer + hdr;
            // Block descriptor bytes 5..7 = block length.  Zero means "keep
            // current" (SCSI-2), which every sane initiator uses.
            uint32_t const reqBs = (uint32_t(bd[5]) << 16)
                                 | (uint32_t(bd[6]) << 8) | uint32_t(bd[7]);
            uint32_t const curBs = m_media->blockSize();
            if (reqBs != 0 && reqBs != curBs) {
                std::fprintf(stderr,
                    "VirtualDiskDevice: MODE SELECT(%u) requests block size %u, "
                    "media is %u -- rejecting (INVALID FIELD IN PARAMETER LIST)\n",
                    ten ? 10u : 6u, reqBs, curBs);
                // 05/26/00 invalid field in parameter list.
                check(cmd, ScsiSenseKey::IllegalRequest, 0x26, 0x00);
                return;
            }
        }
        // Mode pages: accepted and applied to nothing.  This target exposes no
        // changeable parameters (its MODE SENSE returns header + block
        // descriptor with zero pages), so there is no state to update and no
        // page code that can be "unsupported" relative to what we advertise.
        good(cmd);
    }

    void cmdReadCapacity(ScsiCommand& cmd) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }
        uint64_t const last = m_media->blockCount() ? m_media->blockCount() - 1 : 0;
        uint32_t const bs   = m_media->blockSize();
        uint8_t buf[8];
        put32(&buf[0], last > 0xFFFFFFFFull ? 0xFFFFFFFFu
                                            : static_cast<uint32_t>(last));
        put32(&buf[4], bs);
        cmd.dataTransferred = copyOut(cmd, buf, sizeof(buf));
        good(cmd);
    }

    void cmdReadWrite(ScsiCommand& cmd, bool ten, bool isWrite) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }
        if (isWrite && m_media->isReadOnly()) {
            check(cmd, ScsiSenseKey::DataProtect, 0x27, 0x00);
            return;
        }
        uint64_t lba;  uint32_t cnt;
        if (ten) {
            lba = (uint64_t(cmd.cdb[2]) << 24) | (uint64_t(cmd.cdb[3]) << 16)
                | (uint64_t(cmd.cdb[4]) << 8)  |  uint64_t(cmd.cdb[5]);
            cnt = (uint32_t(cmd.cdb[7]) << 8)  |  uint32_t(cmd.cdb[8]);
        } else {
            lba = (uint64_t(cmd.cdb[1] & 0x1F) << 16)
                | (uint64_t(cmd.cdb[2]) << 8) | uint64_t(cmd.cdb[3]);
            cnt = cmd.cdb[4] ? cmd.cdb[4] : 256;   // SBC: 0 means 256 blocks
        }
        if (cnt == 0) { good(cmd); return; }       // READ(10) cnt 0 = no-op
        uint32_t const bs = m_media->blockSize();
        if (lba + cnt > m_media->blockCount()) {
            check(cmd, ScsiSenseKey::IllegalRequest, 0x21, 0x00); // LBA out of range
            return;
        }
        uint64_t const bytes = uint64_t(cnt) * bs;
        if (bytes > cmd.dataBufferLength) {
            std::fprintf(stderr,
                "VirtualDiskDevice: %s lba=%llu cnt=%u (%llu B) exceeds buffer %u B\n",
                isWrite ? "WRITE" : "READ",
                static_cast<unsigned long long>(lba), cnt,
                static_cast<unsigned long long>(bytes), cmd.dataBufferLength);
            check(cmd, ScsiSenseKey::IllegalRequest, 0x24, 0x00);
            return;
        }
        MediaStatus const st = isWrite
            ? m_media->write(lba, cnt, cmd.dataBuffer)
            : m_media->read (lba, cnt, cmd.dataBuffer);
        if (st != MediaStatus::Ok) {
            check(cmd, ScsiSenseKey::MediumError, isWrite ? 0x0C : 0x11, 0x00);
            return;
        }
        cmd.dataTransferred = static_cast<uint32_t>(bytes);
        good(cmd);
    }

    // ---- little helpers ---------------------------------------------------
    static void put32(uint8_t* p, uint32_t v) noexcept
    {
        p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
        p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
    }
    static void put24(uint8_t* p, uint32_t v) noexcept
    {
        p[0] = uint8_t(v >> 16); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v);
    }
    uint32_t copyOut(ScsiCommand& cmd, uint8_t const* src, uint32_t n) noexcept
    {
        uint32_t const c = (n <= cmd.dataBufferLength) ? n : cmd.dataBufferLength;
        if (cmd.dataBuffer && c) std::memcpy(cmd.dataBuffer, src, c);
        return c;
    }

    std::unique_ptr<IBlockMedia> m_media;
    ScsiFixedSenseData           m_lastSense{};
};

} // namespace scsi

#endif // DEVICELIB_SCSI_VIRTUALDISKDEVICE_H
