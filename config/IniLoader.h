// ============================================================================
// IniLoader.h -- Load EmulatorSettings from Emulatr.ini
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Loads Emulatr.ini into an EmulatorSettings struct.  Uses QSettings
// internally for INI parsing -- this is startup-time periphery, the
// V4-spec-permitted "Qt for utilities" case.  EmulatorSettings itself
// stays Qt-free so that core code can read it without pulling in Qt.
//
// Discovery:
//   load(path) loads from an explicit path (typically passed via CLI).
//   loadDefault() searches well-known locations:
//     1. ./Emulatr.ini             (current working directory)
//     2. ./config/Emulatr.ini      (config subdir next to executable)
//     3. <exe-dir>/Emulatr.ini
//     4. <exe-dir>/config/Emulatr.ini
//   First hit wins.  If none found, returns defaults with a warning.
//
// Error handling:
//   Missing keys keep their struct defaults; the loader records a warning
//   for each.  Type-coercion failures (e.g. non-numeric port) keep the
//   default and warn.  Truly fatal conditions (file unreadable) throw
//   std::runtime_error -- the caller should catch in main() and exit
//   with a clear message.
// ============================================================================

#ifndef EMULATR_INI_LOADER_H
#define EMULATR_INI_LOADER_H

#include <filesystem>
#include <string>
#include <vector>

#include "EmulatorSettings.h"

namespace emulatr::config {

class IniLoader {
public:
    struct LoadResult {
        EmulatorSettings         settings;
        std::vector<std::string> warnings;   // Non-fatal issues
        bool                     foundFile;  // false = fell back to defaults
    };

    /// Load INI from an explicit path.  Throws std::runtime_error if the
    /// file exists but cannot be parsed.  If the file does not exist,
    /// returns defaults with foundFile=false.
    static LoadResult load(const std::filesystem::path& iniPath);

    /// Search well-known locations and load the first hit.  Returns
    /// defaults with foundFile=false if nothing is found.
    static LoadResult loadDefault();

    /// Return the search-path list used by loadDefault(), in order.
    /// Useful for diagnostics ("we looked here, here, and here").
    static std::vector<std::filesystem::path> defaultSearchPaths();
};

} // namespace emulatr::config

#endif // EMULATR_INI_LOADER_H
