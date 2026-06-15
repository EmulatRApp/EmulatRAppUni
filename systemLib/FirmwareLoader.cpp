// ============================================================================
// systemLib/FirmwareLoader.cpp -- raw-binary firmware load implementation
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "systemLib/FirmwareLoader.h"

#include "memoryLib/GuestMemory.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>


namespace systemLib {

LoadResult loadRawBinary(memoryLib::GuestMemory&        mem,
                         std::filesystem::path const&   path,
                         uint64_t                       loadPa,
                         uint64_t                       startPa)
{
    LoadResult r;

    // ------------------------------------------------------------------
    // Sanity: file present and readable.
    // ------------------------------------------------------------------

    std::error_code ec;
    auto const exists = std::filesystem::exists(path, ec);
    if (ec || !exists) {
        r.error = "FirmwareLoader: file not found: " + path.string();
        return r;
    }

    auto const fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        r.error = "FirmwareLoader: cannot stat file: " + path.string();
        return r;
    }
    if (fileSize == 0) {
        r.error = "FirmwareLoader: file is empty: " + path.string();
        return r;
    }

    // ------------------------------------------------------------------
    // Range check against guest memory.
    // ------------------------------------------------------------------

    if (loadPa + fileSize > mem.sizeBytes()) {
        r.error = "FirmwareLoader: image (" + std::to_string(fileSize)
                + " bytes) at PA 0x" + std::to_string(loadPa)
                + " exceeds guest memory size 0x"
                + std::to_string(mem.sizeBytes());
        return r;
    }

    // ------------------------------------------------------------------
    // Slurp the file into a buffer, then copy byte-by-byte into the
    // guest's flat physical memory.  Byte-by-byte is fine for v1 sizes
    // (Alpha SRM ROMs are < 16 MiB) and avoids leaking the GuestMemory
    // internal pointer.  If perf becomes a concern, GuestMemory can
    // grow a writeBlock(pa, src, len) primitive later.
    // ------------------------------------------------------------------

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        r.error = "FirmwareLoader: cannot open for read: " + path.string();
        return r;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    in.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(fileSize));
    if (in.gcount() != static_cast<std::streamsize>(fileSize)) {
        r.error = "FirmwareLoader: short read: " + path.string();
        return r;
    }

    for (uint64_t i = 0; i < fileSize; ++i) {
        auto const status = mem.write1(loadPa + i, buffer[i]);
        if (status != memoryLib::MemStatus::Ok) {
            r.error = "FirmwareLoader: write1 OOR at PA 0x"
                    + std::to_string(loadPa + i);
            return r;
        }
    }

    // ------------------------------------------------------------------
    // PAL-bit handling on startPa.  V1 convention: low bit of startPa
    // means "start in PAL mode at the cleared address".  V4 stores
    // palMode separately on CpuState, so split the bit out here and
    // hand the caller a clean PC plus a palMode flag.
    // ------------------------------------------------------------------

    r.ok           = true;
    r.startPc      = startPa & ~uint64_t{1};
    r.palMode      = (startPa & uint64_t{1}) != 0;
    r.bytesLoaded  = fileSize;
    return r;
}

} // namespace systemLib
