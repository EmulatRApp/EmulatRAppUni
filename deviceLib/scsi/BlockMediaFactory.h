// ============================================================================
// deviceLib/scsi/BlockMediaFactory.h -- media_kind -> IBlockMedia factory
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
// Selects the IBlockMedia implementation from the manifest media_kind (approved
// 2026-06-12):
//   "image" | "iso" | absent  -> FileBlockMedia(path, blockSize, readOnly)
//   "host"                     -> HostOpticalMedia (Phase B; not yet built)
//   unknown                    -> FAIL CLOSED: nullptr + diagnostic, NO silent
//                                 fallback (a wrong kind must not become a file)
// Returns an OPEN medium (open() already succeeded) or nullptr with `err` set.
// blockSize / readOnly are decided by the drive type at the call site
// (512 RW for an ATA disk, 2048 RO for an ATAPI CD/ISO).
// ============================================================================

#ifndef DEVICELIB_SCSI_BLOCKMEDIAFACTORY_H
#define DEVICELIB_SCSI_BLOCKMEDIAFACTORY_H

#include <cstdint>
#include <memory>
#include <string>

#include "deviceLib/scsi/IBlockMedia.h"
#include "deviceLib/scsi/FileBlockMedia.h"

namespace scsi {

// createBytes > 0 (file kinds, writable only) asks FileBlockMedia to create a
// blank backing if the file is absent (the runtime "create_if_missing" path);
// 0 = never create.  Ignored for read-only and host kinds.
inline std::unique_ptr<IBlockMedia> makeBlockMedia(const std::string& kind,
                                                   const std::string& path,
                                                   uint32_t           blockSize,
                                                   bool               readOnly,
                                                   uint64_t           createBytes,
                                                   std::string&       err)
{
    err.clear();

    if (kind.empty() || kind == "image" || kind == "iso") {
        if (path.empty()) { err = "empty media path"; return nullptr; }
        auto m = std::make_unique<FileBlockMedia>(path, blockSize, readOnly, createBytes);
        MediaStatus const st = m->open();
        if (st != MediaStatus::Ok) {
            err = "FileBlockMedia.open() failed (missing / too small / unreadable)";
            return nullptr;
        }
        return m;
    }

    if (kind == "host") {
        // Phase B: HostOpticalMedia (Windows + Linux passthrough, host:N resolver).
        err = "host passthrough not yet built (Phase B)";
        return nullptr;
    }

    err = "unknown media_kind '" + kind + "'";   // fail closed -- no fallback
    return nullptr;
}

} // namespace scsi

#endif // DEVICELIB_SCSI_BLOCKMEDIAFACTORY_H
