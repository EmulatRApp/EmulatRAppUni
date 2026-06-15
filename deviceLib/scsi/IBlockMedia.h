// ============================================================================
// deviceLib/scsi/IBlockMedia.h -- block-media seam for ATA disk + ATAPI CD
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
// Byte-sourcing seam (approved 2026-06-12).  The ATA disk and the ATAPI CD stop
// opening files themselves and route every read/write through an IBlockMedia.
// The drive command logic (READ SECTORS, READ(10), the packet FSM) does not know
// what backs the media; host optical passthrough and the test mock are then just
// more IBlockMedia implementations, not special cases in the drive code.
//
//   FileBlockMedia    : raw flat image -- 512 RW (ATA disk) or 2048 RO (ISO)
//   HostOpticalMedia  : raw host CD/DVD passthrough (2048, RO; Phase B)
//   MockBlockMedia    : in-memory, fault-injecting (doctest)
//
// Qt-free core.  No exceptions cross the seam: every fallible op returns a
// MediaStatus.  blockSize() is the drive's logical sector (512 disk / 2048 CD)
// and MUST match the drive type -- a mismatch corrupts the lba*blockSize offset
// math (a C2 decode value, not a C1 storage value).
// ============================================================================

#ifndef DEVICELIB_SCSI_IBLOCKMEDIA_H
#define DEVICELIB_SCSI_IBLOCKMEDIA_H

#include <cstdint>

namespace scsi {

enum class MediaStatus {
    Ok,          // operation completed; all requested blocks transferred
    NoMedia,     // removable unit has no disc loaded (or disc removed mid-use)
    OutOfRange,  // lba (or lba+cnt) beyond blockCount()
    IoError,     // backing read/write failed (short transfer, device error)
    ReadOnly,    // write attempted on read-only media
    NotOpen      // media not attached / open() failed
};

// Read/write-capable, removable-aware block medium.  Offsets are computed as
// lba * blockSize().  read()/write() transfer cnt whole logical blocks.
struct IBlockMedia {
    virtual ~IBlockMedia() = default;

    virtual MediaStatus open()             = 0;   // attach-time; may fail
    virtual void        close()            = 0;
    virtual bool        isOpen()     const = 0;
    virtual bool        isPresent()  const = 0;   // removable: media loaded?
    virtual bool        isReadOnly() const = 0;
    virtual uint32_t    blockSize()  const = 0;   // 512 ATA, 2048 ATAPI/ISO
    virtual uint64_t    blockCount() const = 0;   // LBA count; 0 if no media

    // Transfer cnt blocks at lba.  dst/src point to a cnt*blockSize() buffer the
    // caller owns.  Fallible: no exceptions, status only.
    virtual MediaStatus read (uint64_t lba, uint32_t cnt, void* dst)       = 0;
    virtual MediaStatus write(uint64_t lba, uint32_t cnt, const void* src) = 0;
};

} // namespace scsi

#endif // DEVICELIB_SCSI_IBLOCKMEDIA_H
