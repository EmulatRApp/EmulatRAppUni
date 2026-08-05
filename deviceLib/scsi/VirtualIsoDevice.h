// ============================================================================
// deviceLib/scsi/VirtualIsoDevice.h -- read-only ATAPI/SCSI CD-DVD logical unit
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
// S1 SCOPE (no media): INQUIRY reports a CDROM (type 0x05) so the SRM
// enumerates the unit (the LFU type-5 check); every media-bearing command
// returns the FAIL-FAST sense 02/3A/00 (NOT READY, MEDIUM NOT PRESENT) -- the
// firmware must NOT retry this, unlike 04/xx (becoming ready).
//
// S6 / IBlockMedia seam (2026-06-12, approved): the device no longer opens files
// itself.  Byte sourcing goes through an IBlockMedia (FileBlockMedia for an ISO,
// HostOpticalMedia for a physical drive, MockBlockMedia in tests).  setMedia()
// injects an already-open medium; loadMedia(path) is a convenience that builds a
// read-only 2048-byte FileBlockMedia.  isPresent() drives the no-media report.
//
// With media present:
//   - TEST UNIT READY        -> good
//   - READ CAPACITY (10)     -> last-LBA + 2048 block size
//   - READ (6) / (10) / (12) -> data from the medium at LBA (2048-byte blocks)
//   - READ TOC (fmt 0)       -> a single data track (track 1 @ LBA 0) + lead-out
// MediaStatus -> SCSI sense: Ok->good, NoMedia/NotOpen->02/3A/00,
// OutOfRange->05/21/00, IoError->03/11/00.
//
// BURST LIMIT (intentional): one transfer fills the controller's 2048-byte PIO
// buffer, so a multi-block READ is satisfied one logical block per command.
// Multi-burst streaming is the trace-confirmed follow-up (#32).
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//   2026-08-02  JRN-AUD-003 Batch G (architect-approved): S-5/S-14 parity
//               with the disk sibling.
//               FUNCTION: handleCommand (new LUN arm).
//               CHANGE:  LUN discipline (SPC/SCSI-2, same shape as the
//                        2026-07-25 disk fix): LUN != 0 answers INQUIRY
//                        qualifier 011b/type 1Fh and 05/25/00 otherwise --
//                        prevents the console minting phantom per-LUN CD
//                        units.
//               FUNCTION: doModeSense (new).
//               CHANGE:  MODE SENSE(6)/(10) header + 8-byte block
//                        descriptor at 2048 bytes/block, allocation-length
//                        clamped -- previously ILLEGAL REQUEST, a boot-set
//                        hole vs the disk sibling.
//               FUNCTION: doRequestSense.
//               CHANGE:  Sense now CLEARED on delivery (SCSI-2 8.2.14),
//                        matching the disk sibling; stale 02/3A/00 no
//                        longer reported after media becomes ready.
//               FUNCTION: doInquiry / doRequestSense.
//               CHANGE:  Allocation-length clamp (S-3 shape).
//               FUNCTION: doReadToc.
//               CHANGE:  MSF bit (cdb[1]<1>) honored -- addresses returned
//                        in minute/second/frame form when requested
//                        (lba+150 -> 00:M:S:F big-endian per SCSI-2 CD
//                        addressing); was silently LBA-form always.
// ============================================================================

#ifndef DEVICELIB_SCSI_VIRTUALISODEVICE_H
#define DEVICELIB_SCSI_VIRTUALISODEVICE_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/ScsiCommand.h"
#include "deviceLib/scsi/ScsiSenseData.h"
#include "deviceLib/scsi/VirtualScsiDevice.h"
#include "deviceLib/scsi/IBlockMedia.h"
#include "deviceLib/scsi/FileBlockMedia.h"

namespace scsi {

class VirtualIsoDevice : public VirtualScsiDevice {
public:
    static constexpr uint32_t kBlockBytes = 2048;   // CD logical block (ISO-9660)

    VirtualIsoDevice() noexcept = default;          // default: no media

    ScsiPeripheralDeviceType deviceType() const noexcept override
    {
        return ScsiPeripheralDeviceType::CdDvdDevice;   // 0x05
    }

    [[nodiscard]] bool hasMedia() const noexcept
    {
        return m_media && m_media->isOpen() && m_media->isPresent();
    }
    [[nodiscard]] uint64_t blockCount() const noexcept
    {
        return hasMedia() ? m_media->blockCount() : 0u;
    }

    // Inject an already-open block medium (the production path; the media_kind
    // factory builds and opens it).  Resets the no-media sense.
    void setMedia(std::unique_ptr<IBlockMedia> media) noexcept
    {
        m_media = std::move(media);
        m_lastSense = hasMedia() ? ScsiFixedSenseData{} : senseNotReadyMediumNotPresent();
    }

    // Convenience: attach a read-only 2048-byte ISO file (tests / simple wiring).
    // Returns true iff the image opened and presented at least one block.
    bool loadMedia(const std::string& path) noexcept
    {
        auto m = std::make_unique<FileBlockMedia>(path, kBlockBytes, /*readOnly=*/true);
        if (m->open() != MediaStatus::Ok) { ejectMedia(); return false; }
        setMedia(std::move(m));
        return hasMedia();
    }

    void ejectMedia() noexcept
    {
        m_media.reset();
        m_lastSense = senseNotReadyMediumNotPresent();
    }

    void handleCommand(ScsiCommand& cmd) noexcept override
    {
        // LUN discipline (Batch G S-5, 2026-08-02; same SPC/SCSI-2 rule as
        // the disk sibling's 2026-07-25 fix): LUN 0 only.  Unsupported LUN
        // answers INQUIRY with qualifier 011b / type 1Fh and 05/25/00 for
        // everything else -- otherwise the console mints a phantom CD unit
        // per LUN of this id.
        if (cmd.lun != 0) {
            if (cmd.opcode() == ScsiOp::INQUIRY) {
                uint8_t buf[36] = {};
                buf[0] = 0x7F;                 // qualifier 011b | type 1Fh
                buf[2] = 0x02;
                buf[3] = 0x02;
                buf[4] = 31;
                uint32_t n = clampAlloc(sizeof(buf), cmd.cdb[4]);
                if (n > cmd.dataBufferLength) n = cmd.dataBufferLength;
                if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, buf, n);
                cmd.dataTransferred = n;
                cmd.setGood();
                return;
            }
            if (cmd.opcode() == ScsiOp::REQUEST_SENSE) {
                ScsiFixedSenseData const s = ScsiFixedSenseData::make(
                    ScsiSenseKey::IllegalRequest, 0x25, 0x00);
                uint32_t n = clampAlloc(static_cast<uint32_t>(s.size()),
                                        cmd.cdb[4]);
                if (n > cmd.dataBufferLength) n = cmd.dataBufferLength;
                if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, s.bytes(), n);
                cmd.dataTransferred = n;
                cmd.setGood();
                return;
            }
            failCheck(cmd, ScsiFixedSenseData::make(
                ScsiSenseKey::IllegalRequest, 0x25, 0x00));
            return;
        }
        switch (cmd.opcode()) {
        case ScsiOp::INQUIRY:       doInquiry(cmd);      break;
        case ScsiOp::REQUEST_SENSE: doRequestSense(cmd); break;

        case 0x1A: /* MODE SENSE(6)  */ doModeSense(cmd, false); break;
        case 0x5A: /* MODE SENSE(10) */ doModeSense(cmd, true);  break;

        case ScsiOp::TEST_UNIT_READY:
            if (hasMedia()) cmd.setGood(); else failNotReady(cmd);
            break;

        case ScsiOp::READ_CAPACITY10:
            if (hasMedia()) doReadCapacity10(cmd); else failNotReady(cmd);
            break;

        case ScsiOp::READ6:
        case ScsiOp::READ10:
        case ScsiOp::READ12:
            if (hasMedia()) doRead(cmd); else failNotReady(cmd);
            break;

        case ScsiOp::READ_TOC:
            if (hasMedia()) doReadToc(cmd); else failNotReady(cmd);
            break;

        case ScsiOp::START_STOP_UNIT:
        case ScsiOp::PREVENT_ALLOW:
            cmd.setGood();              // accept; no medium required
            break;

        default:
            cmd.setCheckCondition(senseInvalidOpcode());
            break;
        }
    }

private:
    void failNotReady(ScsiCommand& cmd) noexcept
    {
        m_lastSense = senseNotReadyMediumNotPresent();   // 02/3A/00
        cmd.setCheckCondition(m_lastSense);
    }

    void failCheck(ScsiCommand& cmd, const ScsiFixedSenseData& s) noexcept
    {
        m_lastSense = s;
        cmd.setCheckCondition(s);
        cmd.dataTransferred = 0;
    }

    // Map an IBlockMedia status to the ATAPI/SCSI check-condition sense.
    void mapMediaFailure(ScsiCommand& cmd, MediaStatus st) noexcept
    {
        switch (st) {
        case MediaStatus::OutOfRange: failCheck(cmd, senseLbaOutOfRange());        break;
        case MediaStatus::IoError:    failCheck(cmd, senseUnrecoveredReadError()); break;
        case MediaStatus::NoMedia:
        case MediaStatus::NotOpen:
        default:                      failNotReady(cmd);                           break;
        }
    }

    void doInquiry(ScsiCommand& cmd) noexcept
    {
        uint8_t inq[36] = { 0 };
        inq[0] = static_cast<uint8_t>(ScsiPeripheralDeviceType::CdDvdDevice); // 0x05
        inq[1] = 0x80;                 // RMB: removable medium
        inq[2] = 0x05;                 // version: SPC-3
        inq[3] = 0x02;                 // response data format
        inq[4] = 36 - 5;               // additional length (31)
        std::memcpy(inq + 8,  "DEC     ", 8);            // vendor id (8 bytes)
        std::memcpy(inq + 16, "RRD46   (C) DEC ", 16);   // product id (16 bytes)
        std::memcpy(inq + 32, "1337", 4);                // product revision (4)

        // Batch G S-3 shape (2026-08-02): clamp to allocation length too.
        uint32_t n = clampAlloc(36u, cmd.cdb[4]);
        if (n > cmd.dataBufferLength) n = cmd.dataBufferLength;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, inq, n);
        cmd.dataTransferred = n;
        cmd.setGood();
    }

    void doRequestSense(ScsiCommand& cmd) noexcept
    {
        // Batch G S-3/S-14 (2026-08-02): allocation-length clamp, and sense
        // is CONSUMED by delivery (SCSI-2 8.2.14) -- matches the disk
        // sibling.  Previously stale 02/3A/00 persisted after media load.
        uint32_t n = clampAlloc(18u, cmd.cdb[4]);
        if (n > cmd.dataBufferLength) n = cmd.dataBufferLength;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, m_lastSense.bytes(), n);
        cmd.dataTransferred = n;
        m_lastSense = hasMedia() ? ScsiFixedSenseData{}
                                 : senseNotReadyMediumNotPresent();
        cmd.setGood();                 // REQUEST SENSE itself succeeds
    }

    // MODE SENSE(6)/(10) -- Batch G S-5 (2026-08-02): header + 8-byte block
    // descriptor at 2048 bytes/block, zero mode pages; same minimal shape
    // the disk sibling serves (its comment documents this as the SRM pk
    // class driver's boot-set need).  Previously ILLEGAL REQUEST.
    void doModeSense(ScsiCommand& cmd, bool ten) noexcept
    {
        uint64_t const blocks = blockCount();
        uint8_t  buf[16]      = {};
        uint32_t n;
        if (!ten) {
            buf[0] = 11;                       // mode data length (n-1)
            buf[2] = 0x80;                     // device-specific: write-protected
            buf[3] = 8;                        // block descriptor length
            putBe24(&buf[5], blocks > 0xFFFFFFull ? 0u
                             : static_cast<uint32_t>(blocks));
            putBe24(&buf[9], kBlockBytes);
            n = 12;
        } else {
            buf[1] = 14;                       // mode data length low (n-2)
            buf[3] = 0x80;                     // device-specific: write-protected
            buf[7] = 8;                        // block descriptor length low
            putBe24(&buf[9],  blocks > 0xFFFFFFull ? 0u
                              : static_cast<uint32_t>(blocks));
            putBe24(&buf[13], kBlockBytes);
            n = 16;
        }
        uint32_t const alloc = ten ? be16(cmd.cdb + 7) : uint32_t(cmd.cdb[4]);
        if (alloc < n) n = alloc;
        if (n > cmd.dataBufferLength) n = cmd.dataBufferLength;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, buf, n);
        cmd.dataTransferred = n;
        cmd.setGood();
    }

    // READ CAPACITY (10): 8-byte big-endian (last-LBA, block-length).
    void doReadCapacity10(ScsiCommand& cmd) noexcept
    {
        uint8_t cap[8] = { 0 };
        uint64_t const blocks = blockCount();
        uint32_t const lastLba = (blocks > 0)
            ? static_cast<uint32_t>((blocks - 1) & 0xFFFFFFFFu) : 0u;
        putBe32(cap + 0, lastLba);
        putBe32(cap + 4, kBlockBytes);
        uint32_t const n = cmd.dataBufferLength < 8u ? cmd.dataBufferLength : 8u;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, cap, n);
        cmd.dataTransferred = n;
        cmd.setGood();
    }

    // READ (6)/(10)/(12): decode LBA + block count per opcode, serve up to one
    // PIO buffer worth (single 2048 burst) from the medium.
    void doRead(ScsiCommand& cmd) noexcept
    {
        uint64_t lba    = 0;
        uint32_t blocks = 0;
        switch (cmd.opcode()) {
        case ScsiOp::READ6:
            lba    = (static_cast<uint64_t>(cmd.cdb[1] & 0x1Fu) << 16)
                   | (static_cast<uint64_t>(cmd.cdb[2]) << 8)
                   |  static_cast<uint64_t>(cmd.cdb[3]);
            blocks = (cmd.cdb[4] == 0u) ? 256u : cmd.cdb[4];   // 0 => 256
            break;
        case ScsiOp::READ10:
            lba    = be32(cmd.cdb + 2);
            blocks = be16(cmd.cdb + 7);
            break;
        case ScsiOp::READ12:
            lba    = be32(cmd.cdb + 2);
            blocks = be32(cmd.cdb + 6);
            break;
        default: break;
        }

        if (blocks == 0u) { cmd.dataTransferred = 0; cmd.setGood(); return; } // no-op

        uint32_t const maxBlocks = cmd.dataBufferLength / kBlockBytes;  // whole blocks that fit
        uint32_t const nBlocks   = blocks < maxBlocks ? blocks : maxBlocks;
        if (!cmd.dataBuffer || nBlocks == 0u) { cmd.dataTransferred = 0; cmd.setGood(); return; }

        uint32_t const xfer = nBlocks * kBlockBytes;
        std::memset(cmd.dataBuffer, 0, xfer);
        MediaStatus const st = m_media->read(lba, nBlocks, cmd.dataBuffer);
        if (st != MediaStatus::Ok) { mapMediaFailure(cmd, st); return; }
        cmd.dataTransferred = xfer;
        cmd.setGood();
    }

    // READ TOC, format 0000b: header + one data track (track 1 @ LBA 0) +
    // lead-out (track 0xAA @ blockCount).  Batch G S-14 (2026-08-02): the
    // MSF bit (cdb[1]<1>) is now honored -- addresses returned as
    // 00:MM:SS:FF (lba+150, 75 frames/sec, 60 sec/min) per SCSI-2 CD
    // addressing; previously always LBA form regardless of the request.
    void doReadToc(ScsiCommand& cmd) noexcept
    {
        bool const msf = (cmd.cdb[1] & 0x02u) != 0;
        uint8_t toc[20] = { 0 };
        toc[0] = 0x00; toc[1] = 0x12;          // length = 18 (bytes 2..19)
        toc[2] = 0x01;                          // first track
        toc[3] = 0x01;                          // last track
        toc[4] = 0x00; toc[5] = 0x14;          // ADR=1, control=0x4 (data track)
        toc[6] = 0x01;                          // track number 1
        toc[7] = 0x00;
        putTocAddr(toc + 8, 0u, msf);           // track start = LBA 0
        toc[12] = 0x00; toc[13] = 0x14;
        toc[14] = 0xAA;                         // lead-out
        toc[15] = 0x00;
        putTocAddr(toc + 16,
                   static_cast<uint32_t>(blockCount() & 0xFFFFFFFFu), msf);

        uint32_t const alloc = be16(cmd.cdb + 7);
        uint32_t n = 20u;
        if (alloc != 0u && alloc < n) n = alloc;
        if (cmd.dataBufferLength < n) n = cmd.dataBufferLength;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, toc, n);
        cmd.dataTransferred = n;
        cmd.setGood();
    }

    static uint32_t be16(const uint8_t* p) noexcept
    {
        return (static_cast<uint32_t>(p[0]) << 8) | p[1];
    }
    static uint32_t be32(const uint8_t* p) noexcept
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
             | (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    }
    static void putBe32(uint8_t* p, uint32_t v) noexcept
    {
        p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
        p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        p[2] = static_cast<uint8_t>((v >> 8)  & 0xFFu);
        p[3] = static_cast<uint8_t>( v        & 0xFFu);
    }
    static void putBe24(uint8_t* p, uint32_t v) noexcept
    {
        p[0] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        p[1] = static_cast<uint8_t>((v >> 8)  & 0xFFu);
        p[2] = static_cast<uint8_t>( v        & 0xFFu);
    }
    // Batch G S-14: TOC address field, LBA or MSF form (SCSI-2 CD
    // addressing: frame = lba + 150; 75 frames/sec, 60 sec/min; byte 0
    // reserved 0, then MM:SS:FF).
    static void putTocAddr(uint8_t* p, uint32_t lba, bool msf) noexcept
    {
        if (!msf) { putBe32(p, lba); return; }
        uint32_t const f = lba + 150u;
        p[0] = 0;
        p[1] = static_cast<uint8_t>( f / (75u * 60u));
        p[2] = static_cast<uint8_t>((f / 75u) % 60u);
        p[3] = static_cast<uint8_t>( f % 75u);
    }
    // Batch G S-3 shape: SCSI-2 allocation-length clamp.
    static uint32_t clampAlloc(uint32_t avail, uint32_t alloc) noexcept
    {
        return (alloc < avail) ? alloc : avail;
    }

    std::unique_ptr<IBlockMedia> m_media;
    ScsiFixedSenseData           m_lastSense = senseNotReadyMediumNotPresent();
};

} // namespace scsi

#endif // DEVICELIB_SCSI_VIRTUALISODEVICE_H
