// ============================================================================
// EmulatorSettings.h -- V4 runtime configuration (INI-driven)
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Runtime configuration for the emulator, populated at startup from
// EmulatrV4.ini by configLib::IniLoader.  This struct is Qt-free so that
// any subsystem (including the Qt-free core: pipelineLib, cpuLib, etc.)
// can include it without contaminating Qt-free code.
//
// Scope:
//   - Bootstrap-time config: which firmware to load, how much memory to
//     allocate, where to write traces, etc.
//   - Subsystem tunables: SRM console port, PuTTY auto-launch, log level.
//
// Out of scope:
//   - Device tree (which simulated devices exist, how they're wired):
//     belongs to a separate device-config layer, loaded from a separate
//     source.
//   - Command-line arguments: parsed separately in main(); CLI overrides
//     INI values where they overlap, but the INI has no representation
//     of CLI-only flags.
//
// Lifetime:
//   Constructed once at startup, passed by const reference into
//   subsystems that need it.  No mutation after init.
//
// ============================================================================

#ifndef EMULATR_EMULATOR_SETTINGS_H
#define EMULATR_EMULATOR_SETTINGS_H

#include <QMap>
#include <cstdint>
#include <map>
#include <string>

namespace emulatr::config {

// ----------------------------------------------------------------------------
// SystemSettings -- target machine identity and capacity
// ----------------------------------------------------------------------------
struct SystemSettings {
    std::string model           = "DS10";       // master switch: identity + <model>_platform.json + default firmware
    int         cpuCount        = 1;            // DS10 single-socket 21264 (never advertise >1 while only 1 executes)
    int         activeCpus      = 1;            // CPUs actually exercised in v1
    uint64_t    memorySizeBytes = 1ULL << 30;   // 1 GiB default
};

// ----------------------------------------------------------------------------
// RomSettings -- firmware image
// ----------------------------------------------------------------------------
struct RomSettings {
    std::string firmwareImage;          // Path to firmware (e.g. es45_v7_3.exe)
    std::string firmwareSha256;         // Optional integrity check (empty = skip)
    std::string flashImage;             // Optional NVRAM backing; empty => derive <firmware-stem>.rom (co-located)
};

// ----------------------------------------------------------------------------
// TraceSettings -- pipeline / instruction tracing
// ----------------------------------------------------------------------------
struct TraceSettings {
    // Bit flags (OR together in INI as hex)
    static constexpr uint32_t INSTR    = 0x01;
    static constexpr uint32_t PIPELINE = 0x02;
    static constexpr uint32_t REGFILE  = 0x04;
    static constexpr uint32_t FPRFILE  = 0x08;
    static constexpr uint32_t EVENT    = 0x10;

    uint32_t    traceMask    = 0;       // 0 disables all tracing
    std::string traceFile    = "trace/cpu_trace.log";
    std::string traceLstFile = "trace/cpu_trace.lst";

    bool enabled() const noexcept { return traceMask != 0; }
};

// ----------------------------------------------------------------------------
// SnapshotSettings -- save/restore (post-v1 implementation)
// ----------------------------------------------------------------------------
struct SnapshotSettings {
    bool        enabled     = false;
    uint64_t    targetCycle = 0;
    std::string outputPath  = "snapshots/";
};

// ----------------------------------------------------------------------------
// LoggingSettings -- spdlog runtime levels
// ----------------------------------------------------------------------------
// Per-component overrides keyed by spdlog logger name.  Components register
// themselves with spdlog::get("name") and the logging-init pass applies the
// override.  Components without an override inherit defaultLevel.

struct LoggingSettings {
    std::string defaultLevel = "warn";  // trace|debug|info|warn|err|critical|off
    std::map<std::string, std::string> componentLevels;
};

// ----------------------------------------------------------------------------
// SrmConsoleSettings -- OPA0 / SRM console TCP server (PuTTY-facing)
// ----------------------------------------------------------------------------
// Replaces V3's SRMConsoleDevice::Config nested struct.  SRMConsoleDevice
// constructs from this in V4.

struct SrmConsoleSettings {
    uint16_t    port             = 10023;       // TCP port (PuTTY connects here); 10023 = unprivileged default
    uint32_t    rxBufferSize     = 4096;
    uint32_t    defaultTimeoutMs = 30000;
    bool        echoEnabled      = true;
    bool        autoLaunchPutty  = false;       // Spawn PuTTY at start?
    std::string puttyPath        = "putty.exe"; // Resolved against PATH if relative
    std::string puttyExtraArgs;                 // Optional extra args (e.g. log path)
};

// ----------------------------------------------------------------------------
// StorageSettings -- host location of disk/media image files
// ----------------------------------------------------------------------------
// The platform manifest (ds10_platform.json) names each drive's media as a bare
// FILENAME (portable, no host path); this directory is where those filenames
// resolve.  An absolute media value in the manifest is used as-is; a bare name
// is joined to `diskDir'; an empty media value = no media (drive enumerates as
// "no media present", today's behavior).  Keeping the host directory here -- not
// in the portable json -- mirrors how firmwareImage is sourced from the ini.
struct StorageSettings {
    std::string diskDir;        // base dir for disk/media images (empty = CWD)
};

struct ControllerConfig {
    QString name;
    QString classType;

    // All properties including PCI, MMIO, IRQ
    QMap<QString, QVariant> fields;
};



struct DeviceConfig {
    QString name;
    QString classType;
    QString parent;

    // All properties stored here with dot notation:
    // "container.deviceType", "geometry.logical_sector", "Irq.irqStr", etc.
    QMap<QString, QVariant> fields;
};
// ----------------------------------------------------------------------------
// EmulatorSettings -- top-level container
// ----------------------------------------------------------------------------
struct EmulatorSettings {
    SystemSettings     system;
    RomSettings        rom;
    TraceSettings      trace;
    SnapshotSettings   snapshot;
    LoggingSettings    logging;
    SrmConsoleSettings srmConsole;
    StorageSettings    storage;
    QMap<QString, ControllerConfig>       controllers;
    QMap<QString, DeviceConfig>           devices;

    // Diagnostic: where did this config come from?
    std::string sourcePath;             // Absolute path of the loaded INI

    // Sanity check: returns empty vector if settings are coherent, otherwise
    // returns human-readable diagnostic strings.  Caller decides whether to
    // treat each as fatal or as a warning.
    std::vector<std::string> validate() const;
};

} // namespace emulatr::config

#endif // EMULATR_EMULATOR_SETTINGS_H
