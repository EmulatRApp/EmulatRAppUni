// ============================================================================
// LoggingInit.cpp -- spdlog level configuration from EmulatorSettings
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================

#include "LoggingInit.h"

namespace emulatr::logging {

spdlog::level::level_enum parseLevel(std::string_view name, bool* recognized) {
    auto set = [&](bool ok) { if (recognized) *recognized = ok; };

    if (name == "trace")    { set(true); return spdlog::level::trace; }
    if (name == "debug")    { set(true); return spdlog::level::debug; }
    if (name == "info")     { set(true); return spdlog::level::info; }
    if (name == "warn")     { set(true); return spdlog::level::warn; }
    if (name == "err")      { set(true); return spdlog::level::err; }
    if (name == "error")    { set(true); return spdlog::level::err; }   // alias
    if (name == "critical") { set(true); return spdlog::level::critical; }
    if (name == "off")      { set(true); return spdlog::level::off; }

    set(false);
    return spdlog::level::warn;   // safe fallback
}

void applyComponentLevel(const std::string& component,
                         const std::string& level) {
    bool ok = false;
    auto lvl = parseLevel(level, &ok);

    if (auto logger = spdlog::get(component)) {
        logger->set_level(lvl);
        if (!ok) {
            logger->warn("Unknown log level '{}' for component '{}'; falling back to warn",
                         level, component);
        }
    }
    // If the component isn't registered yet, the override is silently lost.
    // Components that want late application should call this themselves
    // after spdlog::register_logger().
}

void applySettings(const config::LoggingSettings& settings) {
    bool defaultOk = false;
    spdlog::set_level(parseLevel(settings.defaultLevel, &defaultOk));
    if (!defaultOk) {
        spdlog::warn("Unknown default log level '{}'; falling back to warn",
                     settings.defaultLevel);
    }

    for (const auto& [component, level] : settings.componentLevels) {
        applyComponentLevel(component, level);
    }
}

} // namespace emulatr::logging
