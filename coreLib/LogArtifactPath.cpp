// ============================================================================
// coreLib/LogArtifactPath.cpp -- implementation of the stem-keyed log path.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// FILE 2: coreLib/LogArtifactPath.cpp
// FUNCTION: resolve the stem once, hand out logs/<stem>_<purpose>.<ext>.
// CHANGE (2026-07-31): NEW FILE.  See LogArtifactPath.h for the rationale.
//
// Thread-safety: the three callers are event logs that already serialise their
// own writes behind a mutex, but they are reached from the pipeline at retire
// time and there is no guarantee two subsystems do not first-touch in the same
// window.  A single small mutex guards the stem and the one-shot directory
// create; both are cold paths (once per process) so the cost is irrelevant.
// ============================================================================

#include "coreLib/LogArtifactPath.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace coreLib {

namespace {

constexpr char const* kLogDir      = "logs";
constexpr char const* kDefaultStem = "emulatr";

std::mutex  s_mutex;
std::string s_stem;                  // set by setLogArtifactStem()
bool        s_dirChecked = false;    // logs/ create attempted once per process

// Reduce an arbitrary path or name to a filename-safe token: take the
// basename, drop one trailing extension, and fold every character outside
// [A-Za-z0-9_-] to '_'.  This keeps a Windows path, a quoted argv element or
// a hostile EMULATR_LOG_STEM from ever reaching the filesystem as a directory
// separator.  "firmware/ds20_v7_3.exe" -> "ds20_v7_3".
std::string sanitizeStem(std::string_view raw)
{
    std::string s(raw);

    auto const cut = s.find_last_of("/\\");
    if (cut != std::string::npos) s = s.substr(cut + 1);

    auto const dot = s.find_last_of('.');
    if (dot != std::string::npos && dot != 0) s = s.substr(0, dot);

    for (char& c : s) {
        unsigned char const u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) != 0 || c == '_' || c == '-')) c = '_';
    }
    return s;
}

} // namespace


void setLogArtifactStem(std::string_view stem) noexcept
{
    std::string const clean = sanitizeStem(stem);
    if (clean.empty()) return;                 // keep the fallback

    std::lock_guard<std::mutex> const lock(s_mutex);
    s_stem = clean;
}


std::string logArtifactStem()
{
    // 1. operator override -- wins even after main() set the firmware stem, so
    //    two same-platform instances can be separated without a code change.
    if (char const* const env = std::getenv("EMULATR_LOG_STEM"); env != nullptr && *env != '\0') {
        std::string const clean = sanitizeStem(env);
        if (!clean.empty()) return clean;
    }

    // 2. the --firmware basename, or 3. the fallback.
    std::lock_guard<std::mutex> const lock(s_mutex);
    return s_stem.empty() ? std::string(kDefaultStem) : s_stem;
}


std::string logArtifactPath(char const* purpose, char const* ext)
{
    {
        std::lock_guard<std::mutex> const lock(s_mutex);
        if (!s_dirChecked) {
            std::error_code ec;
            std::filesystem::create_directories(kLogDir, ec);
            s_dirChecked = true;   // one attempt; a failure surfaces as the
                                   // caller's open() failing, same as before
        }
    }

    std::string path(kLogDir);
    path += '/';
    path += logArtifactStem();     // resolved OUTSIDE the lock above
    path += '_';
    path += (purpose != nullptr ? purpose : "log");
    path += '.';
    path += (ext != nullptr ? ext : "log");
    return path;
}

} // namespace coreLib
