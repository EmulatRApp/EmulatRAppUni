// ============================================================================
// tests/deviceLib/MockBlockMedia.h -- in-memory IBlockMedia for doctest
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
// Configurable in-memory block medium for unit tests: set blockSize / blockCount
// / readOnly / present before open(); open() allocates and fills each block N
// with byte (N & 0xFF) so reads are content-verifiable.  Fault injection
// (faultOnRead / faultLba / faultStatus) exercises the drive error paths without
// a real device.  Drives can be tested against this with no filesystem or
// hardware (the whole point of the IBlockMedia seam).
// ============================================================================

#ifndef TESTS_DEVICELIB_MOCKBLOCKMEDIA_H
#define TESTS_DEVICELIB_MOCKBLOCKMEDIA_H

#include <cstdint>
#include <cstring>
#include <vector>

#include "deviceLib/scsi/IBlockMedia.h"

namespace scsi {

class MockBlockMedia : public IBlockMedia {
public:
    // --- configuration (set before open()) ---
    uint32_t cfgBlockSize  = 2048;
    uint64_t cfgBlockCount = 0;
    bool     cfgReadOnly   = false;
    bool     cfgPresent    = true;     // removable: pretend a disc is loaded?
    bool     cfgOpenFails  = false;    // make open() return NotOpen

    // --- fault injection ---
    bool        faultOnRead  = false;
    bool        faultOnWrite = false;
    uint64_t    faultLba     = 0;      // fault fires if the op covers this LBA
    MediaStatus faultStatus  = MediaStatus::IoError;

    MediaStatus open() override
    {
        if (cfgOpenFails) return MediaStatus::NotOpen;
        m_buf.assign(static_cast<size_t>(cfgBlockCount) * cfgBlockSize, 0u);
        for (uint64_t b = 0; b < cfgBlockCount; ++b)
            std::memset(m_buf.data() + b * cfgBlockSize,
                        static_cast<int>(b & 0xFFu), cfgBlockSize);
        m_open = true;
        return MediaStatus::Ok;
    }
    void close() override { m_open = false; m_buf.clear(); }

    bool     isOpen()     const override { return m_open; }
    bool     isPresent()  const override { return cfgPresent; }
    bool     isReadOnly() const override { return cfgReadOnly; }
    uint32_t blockSize()  const override { return cfgBlockSize; }
    uint64_t blockCount() const override { return cfgPresent ? cfgBlockCount : 0; }

    MediaStatus read(uint64_t lba, uint32_t cnt, void* dst) override
    {
        if (!m_open)                  return MediaStatus::NotOpen;
        if (!cfgPresent)              return MediaStatus::NoMedia;
        if (lba + cnt > cfgBlockCount) return MediaStatus::OutOfRange;
        if (faultOnRead && covers(lba, cnt, faultLba)) return faultStatus;
        if (dst && cnt)
            std::memcpy(dst, m_buf.data() + lba * cfgBlockSize,
                        static_cast<size_t>(cnt) * cfgBlockSize);
        return MediaStatus::Ok;
    }

    MediaStatus write(uint64_t lba, uint32_t cnt, const void* src) override
    {
        if (!m_open)                   return MediaStatus::NotOpen;
        if (!cfgPresent)               return MediaStatus::NoMedia;
        if (cfgReadOnly)               return MediaStatus::ReadOnly;
        if (lba + cnt > cfgBlockCount) return MediaStatus::OutOfRange;
        if (faultOnWrite && covers(lba, cnt, faultLba)) return faultStatus;
        if (src && cnt)
            std::memcpy(m_buf.data() + lba * cfgBlockSize, src,
                        static_cast<size_t>(cnt) * cfgBlockSize);
        return MediaStatus::Ok;
    }

    // test introspection
    const std::vector<uint8_t>& buffer() const noexcept { return m_buf; }

private:
    static bool covers(uint64_t lba, uint32_t cnt, uint64_t target) noexcept
    {
        return target >= lba && target < lba + cnt;
    }

    std::vector<uint8_t> m_buf;
    bool                 m_open = false;
};

} // namespace scsi

#endif // TESTS_DEVICELIB_MOCKBLOCKMEDIA_H
