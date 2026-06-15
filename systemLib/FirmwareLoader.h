// ============================================================================
// systemLib/FirmwareLoader.h -- raw-binary firmware load into GuestMemory
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
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// Phase 1 v1: read a flat .bin from disk and copy it into the guest's
// physical memory at a caller-specified physical address.  No header
// parsing, no decompression, no SROM analysis.  V1's path B contract.
//
// Caller supplies:
//   - GuestMemory reference                                                                     (mutable)
//   - filesystem path                                                                           (input)
//   - loadPa                                                                                    (where in PA the image lands)
//   - startPa                                                                                   (where execution begins; low bit = PAL bit, V1 convention)
//
// Returns LoadResult{ ok, error, startPc, palMode }:
//   - ok        : true on success
//   - error     : populated with a human-readable message on failure
//   - startPc   : startPa with the PAL bit stripped (low bit cleared)
//   - palMode   : true if the PAL bit was set in startPa
//
// Failure modes:
//   FileNotFound   open() returned an error
//   ReadFailed     stat said size > 0 but read returned a short result
//   OutOfRange     loadPa + image size > guest memory size
//
// Out of scope for v1: SRM decompressor, ELF section copy, multi-segment
// image, header validation.  Each can layer behind this same shape later
// without churning callers.
//
// ============================================================================

#ifndef SYSTEMLIB_FIRMWARELOADER_H
#define SYSTEMLIB_FIRMWARELOADER_H

#include <cstdint>
#include <filesystem>
#include <string>

namespace memoryLib {
struct GuestMemory;
}

namespace systemLib {

struct LoadResult
{
    bool        ok       = false;
    std::string error;            // populated on failure; empty otherwise
    uint64_t    startPc  = 0;     // startPa with PAL bit cleared
    bool        palMode  = false; // true if startPa had low bit set
    uint64_t    bytesLoaded = 0;  // size of the image landed in memory
};


// Read the file at path and copy it into mem starting at loadPa.
// Returns startPa interpreted per V1 PAL-bit convention.
LoadResult loadRawBinary(memoryLib::GuestMemory&        mem,
                         std::filesystem::path const&   path,
                         uint64_t                       loadPa,
                         uint64_t                       startPa);

} // namespace systemLib

#endif // SYSTEMLIB_FIRMWARELOADER_H
