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
        switch (cmd.opcode()) {
        case ScsiOp::INQUIRY:       doInquiry(cmd);      break;
        case ScsiOp::REQUEST_SENSE: doRequestSense(cmd); break;

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

        uint32_t n = cmd.dataBufferLength < 36u ? cmd.dataBufferLength : 36u;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, inq, n);
        cmd.dataTransferred = n;
        cmd.setGood();
    }

    void doRequestSense(ScsiCommand& cmd) noexcept
    {
        uint32_t n = cmd.dataBufferLength < 18u ? cmd.dataBufferLength : 18u;
        if (cmd.dataBuffer && n) std::memcpy(cmd.dataBuffer, m_lastSense.bytes(), n);
        cmd.dataTransferred = n;
        cmd.setGood();                 // REQUEST SENSE itself succeeds
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

    // READ TOC, format 0000b (LBA addressing): header + one data track (track 1
    // @ LBA 0) + lead-out (track 0xAA @ blockCount).
    void doReadToc(ScsiCommand& cmd) noexcept
    {
        uint8_t toc[20] = { 0 };
        toc[0] = 0x00; toc[1] = 0x12;          // length = 18 (bytes 2..19)
        toc[2] = 0x01;                          // first track
        toc[3] = 0x01;                          // last track
        toc[4] = 0x00; toc[5] = 0x14;          // ADR=1, control=0x4 (data track)
        toc[6] = 0x01;                          // track number 1
        toc[7] = 0x00;
        putBe32(toc + 8, 0u);                   // track start LBA = 0
        toc[12] = 0x00; toc[13] = 0x14;
        toc[14] = 0xAA;                         // lead-out
        toc[15] = 0x00;
        putBe32(toc + 16, static_cast<uint32_t>(blockCount() & 0xFFFFFFFFu));

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

    std::unique_ptr<IBlockMedia> m_media;
    ScsiFixedSenseData           m_lastSense = senseNotReadyMediumNotPresent();
};

} // namespace scsi

#endif // DEVICELIB_SCSI_VIRTUALISODEVICE_H
