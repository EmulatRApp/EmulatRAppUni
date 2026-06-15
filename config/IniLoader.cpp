// ============================================================================
// IniLoader.cpp -- Load EmulatorSettings from EmulatrV4.ini
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================

#include "IniLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <stdexcept>

namespace emulatr::config {

// ============================================================================
// Helpers -- type-safe extraction with default + warning capture
// ============================================================================

namespace {

template <typename T>
T readValue(const QSettings& s,
            const QString& key,
            T defaultValue,
            std::vector<std::string>& warnings)
{
    if (!s.contains(key)) {
        return defaultValue;
    }
    QVariant v = s.value(key);
    bool ok = false;

    if constexpr (std::is_same_v<T, std::string>) {
        return v.toString().toStdString();
    } else if constexpr (std::is_same_v<T, bool>) {
        // QSettings reads "true"/"false"/"1"/"0" sensibly via toBool()
        return v.toBool();
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        uint val = v.toUInt(&ok);
        if (!ok || val > 65535) {
            warnings.emplace_back("INI key '" + key.toStdString()
                                  + "' is not a valid uint16; using default");
            return defaultValue;
        }
        return static_cast<uint16_t>(val);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        uint val = v.toUInt(&ok);
        if (!ok) {
            warnings.emplace_back("INI key '" + key.toStdString()
                                  + "' is not a valid uint32; using default");
            return defaultValue;
        }
        return val;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        qulonglong val = v.toULongLong(&ok);
        if (!ok) {
            warnings.emplace_back("INI key '" + key.toStdString()
                                  + "' is not a valid uint64; using default");
            return defaultValue;
        }
        return static_cast<uint64_t>(val);
    } else if constexpr (std::is_same_v<T, int>) {
        int val = v.toInt(&ok);
        if (!ok) {
            warnings.emplace_back("INI key '" + key.toStdString()
                                  + "' is not a valid int; using default");
            return defaultValue;
        }
        return val;
    } else {
        static_assert(!std::is_same_v<T, T>, "Unsupported type in readValue");
    }
}

// Parse hex-or-decimal string for trace mask: "0x07", "7", "0b111" etc.
uint32_t parseTraceMask(const QString& raw,
                        uint32_t defaultValue,
                        std::vector<std::string>& warnings)
{
    if (raw.isEmpty()) return defaultValue;
    bool ok = false;
    uint32_t v = raw.toUInt(&ok, 0);   // base=0 -> auto-detect 0x, 0, decimal
    if (!ok) {
        warnings.emplace_back("Trace.traceMask not parseable: '"
                              + raw.toStdString() + "'; using default");
        return defaultValue;
    }
    return v;
}

} // anonymous namespace

// ============================================================================
// Section loaders
// ============================================================================

namespace {

void loadSystem(QSettings& s, SystemSettings& out,
                std::vector<std::string>& warnings) {
    s.beginGroup("System");
    out.model           = readValue<std::string>(s, "model",           out.model,           warnings);
    out.cpuCount        = readValue<int>        (s, "cpuCount",        out.cpuCount,        warnings);
    out.activeCpus      = readValue<int>        (s, "activeCpus",      out.activeCpus,      warnings);
    out.memorySizeBytes = readValue<uint64_t>   (s, "memorySize",      out.memorySizeBytes, warnings);
    s.endGroup();
}

void loadRom(QSettings& s, RomSettings& out,
             std::vector<std::string>& warnings) {
    s.beginGroup("ROM");
    out.firmwareImage  = readValue<std::string>(s, "firmwareImage",  out.firmwareImage,  warnings);
    out.firmwareSha256 = readValue<std::string>(s, "firmwareSha256", out.firmwareSha256, warnings);
    s.endGroup();
}

void loadTrace(QSettings& s, TraceSettings& out,
               std::vector<std::string>& warnings) {
    s.beginGroup("Trace");
    out.traceMask = parseTraceMask(s.value("traceMask").toString(),
                                   out.traceMask, warnings);
    out.traceFile    = readValue<std::string>(s, "traceFile",    out.traceFile,    warnings);
    out.traceLstFile = readValue<std::string>(s, "traceLstFile", out.traceLstFile, warnings);
    s.endGroup();
}

void loadSnapshot(QSettings& s, SnapshotSettings& out,
                  std::vector<std::string>& warnings) {
    s.beginGroup("Snapshot");
    out.enabled     = readValue<bool>       (s, "enabled",     out.enabled,     warnings);
    out.targetCycle = readValue<uint64_t>   (s, "targetCycle", out.targetCycle, warnings);
    out.outputPath  = readValue<std::string>(s, "outputPath",  out.outputPath,  warnings);
    s.endGroup();
}

void loadLogging(QSettings& s, LoggingSettings& out,
                 std::vector<std::string>& warnings) {
    s.beginGroup("Logging");
    out.defaultLevel = readValue<std::string>(s, "defaultLevel", out.defaultLevel, warnings);

    // Per-component levels are stored as Logging.<component> = <level>
    // Iterate child keys to pick them up.
    for (const QString& key : s.childKeys()) {
        if (key == QStringLiteral("defaultLevel")) continue;
        out.componentLevels[key.toStdString()] = s.value(key).toString().toStdString();
    }
    s.endGroup();
}

void loadSrmConsole(QSettings& s, SrmConsoleSettings& out,
                    std::vector<std::string>& warnings) {
    s.beginGroup("SRMConsole");
    out.port             = readValue<uint16_t>   (s, "port",             out.port,             warnings);
    out.rxBufferSize     = readValue<uint32_t>   (s, "rxBufferSize",     out.rxBufferSize,     warnings);
    out.defaultTimeoutMs = readValue<uint32_t>   (s, "defaultTimeoutMs", out.defaultTimeoutMs, warnings);
    out.echoEnabled      = readValue<bool>       (s, "echoEnabled",      out.echoEnabled,      warnings);
    out.autoLaunchPutty  = readValue<bool>       (s, "autoLaunchPutty",  out.autoLaunchPutty,  warnings);
    out.puttyPath        = readValue<std::string>(s, "puttyPath",        out.puttyPath,        warnings);
    out.puttyExtraArgs   = readValue<std::string>(s, "puttyExtraArgs",   out.puttyExtraArgs,   warnings);
    s.endGroup();
}

void loadStorage(QSettings& s, StorageSettings& out,
                 std::vector<std::string>& warnings) {
    s.beginGroup("Storage");
    out.diskDir = readValue<std::string>(s, "diskDir", out.diskDir, warnings);
    s.endGroup();
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

IniLoader::LoadResult IniLoader::load(const std::filesystem::path& iniPath) {
    LoadResult result;
    result.foundFile = false;

    QString qpath = QString::fromStdString(iniPath.string());

    if (!QFileInfo::exists(qpath)) {
        result.warnings.emplace_back("INI file not found: " + iniPath.string()
                                     + " (using defaults)");
        return result;
    }

    QSettings s(qpath, QSettings::IniFormat);
    if (s.status() != QSettings::NoError) {
        throw std::runtime_error("Failed to parse INI file: " + iniPath.string());
    }

    result.foundFile           = true;
    result.settings.sourcePath = std::filesystem::absolute(iniPath).string();

    loadSystem    (s, result.settings.system,     result.warnings);
    loadRom       (s, result.settings.rom,        result.warnings);
    loadTrace     (s, result.settings.trace,      result.warnings);
    loadSnapshot  (s, result.settings.snapshot,   result.warnings);
    loadLogging   (s, result.settings.logging,    result.warnings);
    loadSrmConsole(s, result.settings.srmConsole, result.warnings);
    loadStorage   (s, result.settings.storage,    result.warnings);

    return result;
}

std::vector<std::filesystem::path> IniLoader::defaultSearchPaths() {
    std::vector<std::filesystem::path> paths;

    namespace fs = std::filesystem;
    fs::path cwd = fs::current_path();
    paths.push_back(cwd / "EmulatrV4.ini");
    paths.push_back(cwd / "config" / "EmulatrV4.ini");

    // Executable directory (only meaningful if QCoreApplication is initialized)
    if (QCoreApplication::instance() != nullptr) {
        QString exeDir = QCoreApplication::applicationDirPath();
        if (!exeDir.isEmpty()) {
            fs::path exe = fs::path(exeDir.toStdString());
            paths.push_back(exe / "EmulatrV4.ini");
            paths.push_back(exe / "config" / "EmulatrV4.ini");
        }
    }

    return paths;
}

IniLoader::LoadResult IniLoader::loadDefault() {
    for (const auto& candidate : defaultSearchPaths()) {
        if (std::filesystem::exists(candidate)) {
            return load(candidate);
        }
    }

    // Nothing found -- return defaults with a clear warning listing where we looked.
    LoadResult result;
    result.foundFile = false;
    std::string msg = "EmulatrV4.ini not found in any of:";
    for (const auto& p : defaultSearchPaths()) {
        msg += "\n  " + p.string();
    }
    msg += "\n(using built-in defaults)";
    result.warnings.emplace_back(std::move(msg));
    return result;
}

} // namespace emulatr::config
