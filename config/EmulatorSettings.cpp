// ============================================================================
// EmulatorSettings.cpp -- validation implementation
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================

#include "EmulatorSettings.h"

#include <vector>

namespace emulatr::config {

std::vector<std::string> EmulatorSettings::validate() const {
    std::vector<std::string> issues;

    // ---- System ------------------------------------------------------------
    if (system.cpuCount < 1 || system.cpuCount > 64) {
        issues.emplace_back("System.cpuCount out of range [1..64]: "
                            + std::to_string(system.cpuCount));
    }
    if (system.activeCpus < 1 || system.activeCpus > system.cpuCount) {
        issues.emplace_back("System.activeCpus must be in [1..cpuCount]");
    }
    if (system.memorySizeBytes < (16ULL * 1024 * 1024)) {
        issues.emplace_back("System.memorySize below 16 MiB minimum");
    }

    // ---- ROM ---------------------------------------------------------------
    if (rom.firmwareImage.empty()) {
        issues.emplace_back("ROM.firmwareImage is required but empty");
    }

    // ---- SrmConsole --------------------------------------------------------
    if (srmConsole.port == 0) {
        issues.emplace_back("SRMConsole.port is 0 (not bindable)");
    }
    if (srmConsole.rxBufferSize < 64) {
        issues.emplace_back("SRMConsole.rxBufferSize below 64 byte minimum");
    }
    if (srmConsole.autoLaunchPutty && srmConsole.puttyPath.empty()) {
        issues.emplace_back("SRMConsole.autoLaunchPutty=true but puttyPath empty");
    }

    // ---- Logging -----------------------------------------------------------
    // Level name validation happens inside LoggingInit::applySettings;
    // unknown names there fall back to defaultLevel and emit a warning.

    return issues;
}

} // namespace emulatr::config
