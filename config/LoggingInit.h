// ============================================================================
// LoggingInit.h -- Apply LoggingSettings to spdlog at runtime
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Bridge from configLib::LoggingSettings to spdlog runtime configuration.
// Called once at startup, after IniLoader has populated EmulatorSettings,
// before any subsystem starts producing log output.
//
// Compile-time gate vs runtime gate:
//   This module governs the *runtime* level (spdlog::set_level), which
//   filters which calls actually emit.  It cannot lower the *compile-time*
//   floor (SPDLOG_ACTIVE_LEVEL in coreLib/LoggingMacros.h) -- that
//   determines which calls exist in the binary at all.
//
//   For runtime control to be meaningful, SPDLOG_ACTIVE_LEVEL must be
//   set to SPDLOG_LEVEL_TRACE (everything compiles in) and this module
//   does the filtering.  See coreLib/LoggingMacros.h.
//
// ============================================================================

#ifndef EMULATR_LOGGING_INIT_H
#define EMULATR_LOGGING_INIT_H

#include <string_view>

#include "configLib/EmulatorSettings.h"

#include <spdlog/spdlog.h>

namespace emulatr::logging {

/// Apply runtime logging settings.  Sets the default level on spdlog's
/// global logger, and applies any per-component overrides to loggers
/// already registered via spdlog::get(name).  Components registered
/// later should call applyComponentLevel() themselves.
void applySettings(const config::LoggingSettings& settings);

/// Apply a per-component override.  Safe to call after applySettings()
/// for components that register late (e.g. plugins).
void applyComponentLevel(const std::string& component,
                         const std::string& level);

/// Parse a level name to spdlog::level::level_enum.
/// Accepts: trace, debug, info, warn, err, error, critical, off.
/// Unknown names return spdlog::level::warn and the function returns false
/// in *recognized*, so the caller can warn appropriately.
spdlog::level::level_enum parseLevel(std::string_view name,
                                     bool* recognized = nullptr);

} // namespace emulatr::logging

#endif // EMULATR_LOGGING_INIT_H
