// ============================================================================
// deviceLib/scsi/FileBlockMedia.h -- flat-image IBlockMedia (ATA disk + ISO)
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
// Raw flat-image backing (approved 2026-06-12).  Serves BOTH roles:
//   - ATA fixed disk: blockSize 512, read-write.
//   - ATAPI ISO-9660: blockSize 2048, read-only (an ISO is just a flat
//     2048-byte-sector file).
// Cross-platform via std::fstream / std::filesystem.  Offset = lba * blockSize.
// The stream is held open between attach and close(); media is external backing
// and is never serialized, so a snapshot restore re-attaches via the factory.
// A flat image is always "present" once open() succeeds (no removable tray).
// ============================================================================

#ifndef DEVICELIB_SCSI_FILEBLOCKMEDIA_H
#define DEVICELIB_SCSI_FILEBLOCKMEDIA_H

#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <filesystem>

#include "deviceLib/scsi/IBlockMedia.h"

namespace scsi {

class FileBlockMedia : public IBlockMedia {
public:
    // createBytes > 0 asks open() to CREATE a blank backing of that size if the
    // file is absent (writable media only) -- the runtime "create_if_missing"
    // path.  An existing file is NEVER overwritten.  Default 0 = never create.
    FileBlockMedia(std::string path, uint32_t blockSize, bool readOnly,
                   uint64_t createBytes = 0) noexcept
        : m_path(std::move(path)), m_blockSize(blockSize),
          m_readOnly(readOnly), m_createBytes(createBytes) {}

    ~FileBlockMedia() override { close(); }

    MediaStatus open() override
    {
        close();
        if (m_path.empty() || m_blockSize == 0) return MediaStatus::NotOpen;

        std::error_code ec;
        // create_if_missing: only for writable media, only when absent, never
        // overwriting.  The file reads back as zeros (a blank install target);
        // on a sparse-capable FS resize_file does not pre-allocate the bytes.
        if (m_createBytes > 0 && !m_readOnly && !std::filesystem::exists(m_path, ec)) {
            { std::ofstream mk(m_path, std::ios::binary | std::ios::out | std::ios::trunc); }
            std::error_code rc;
            std::filesystem::resize_file(m_path,
                static_cast<std::uintmax_t>(m_createBytes), rc);
            if (rc) return MediaStatus::NotOpen;
        }

        std::uintmax_t const bytes = std::filesystem::file_size(m_path, ec);
        if (ec || bytes < m_blockSize) return MediaStatus::NotOpen;

        std::ios::openmode mode = std::ios::binary | std::ios::in;
        if (!m_readOnly) mode |= std::ios::out;          // in|out: keep contents
        m_fs.open(m_path, mode);
        if (!m_fs.is_open()) return MediaStatus::NotOpen;

        m_blocks = static_cast<uint64_t>(bytes) / m_blockSize;
        m_open   = true;
        return MediaStatus::Ok;
    }

    void close() override
    {
        if (m_fs.is_open()) m_fs.close();
        m_fs.clear();
        m_open   = false;
        m_blocks = 0;
    }

    bool     isOpen()     const override { return m_open; }
    bool     isPresent()  const override { return m_open; }   // flat image: no tray
    bool     isReadOnly() const override { return m_readOnly; }
    uint32_t blockSize()  const override { return m_blockSize; }
    uint64_t blockCount() const override { return m_blocks; }

    MediaStatus read(uint64_t lba, uint32_t cnt, void* dst) override
    {
        if (!m_open)                         return MediaStatus::NotOpen;
        if (cnt == 0)                        return MediaStatus::Ok;
        if (!dst)                            return MediaStatus::IoError;
        if (lba + cnt > m_blocks)            return MediaStatus::OutOfRange;

        std::streamsize const bytes = static_cast<std::streamsize>(cnt) * m_blockSize;
        m_fs.clear();
        m_fs.seekg(static_cast<std::streamoff>(lba) * m_blockSize, std::ios::beg);
        m_fs.read(static_cast<char*>(dst), bytes);
        if (m_fs.gcount() != bytes) { m_fs.clear(); return MediaStatus::IoError; }
        return MediaStatus::Ok;
    }

    MediaStatus write(uint64_t lba, uint32_t cnt, const void* src) override
    {
        if (m_readOnly)                      return MediaStatus::ReadOnly;
        if (!m_open)                         return MediaStatus::NotOpen;
        if (cnt == 0)                        return MediaStatus::Ok;
        if (!src)                            return MediaStatus::IoError;
        if (lba + cnt > m_blocks)            return MediaStatus::OutOfRange;

        std::streamsize const bytes = static_cast<std::streamsize>(cnt) * m_blockSize;
        m_fs.clear();
        m_fs.seekp(static_cast<std::streamoff>(lba) * m_blockSize, std::ios::beg);
        m_fs.write(static_cast<const char*>(src), bytes);
        m_fs.flush();
        if (!m_fs) { m_fs.clear(); return MediaStatus::IoError; }
        return MediaStatus::Ok;
    }

private:
    std::string  m_path;
    uint32_t     m_blockSize  = 0;
    bool         m_readOnly   = true;
    bool         m_open       = false;
    uint64_t     m_blocks     = 0;
    uint64_t     m_createBytes = 0;   // >0 = create blank if missing (writable)
    std::fstream m_fs;
};

} // namespace scsi

#endif // DEVICELIB_SCSI_FILEBLOCKMEDIA_H
