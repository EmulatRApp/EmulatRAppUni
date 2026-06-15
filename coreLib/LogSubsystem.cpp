// ============================================================================
// coreLib/LogSubsystem.cpp -- centralized per-subsystem diagnostic logging
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "coreLib/LogSubsystem.h"

#include <array>
#include <atomic>
#include <cctype>
#include <fstream>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace coreLib {

namespace {

// ----------------------------------------------------------------------------
// Throttle counter table.  One atomic per subsystem; relaxed ordering is
// adequate -- we only care about ordinal partition into loud/summary/mute
// buckets, not strict cross-thread visibility of consecutive counts.
// ----------------------------------------------------------------------------
constexpr std::size_t kSubsysCount =
    static_cast<std::size_t>(LogSubsystem::kCount);

constexpr uint64_t kLoudThreshold     = 16ULL;
constexpr uint64_t kLoudThenMuteCutoff= 32ULL;
constexpr uint64_t kSummaryStride     = 256ULL * 1024ULL;

std::array<std::atomic<uint64_t>, kSubsysCount> s_eventCount{};

// ----------------------------------------------------------------------------
// Configuration table.  Initialized with defaults, mutated via
// configureSubsystem().  Reads on hot path go through getSubsystemConfig
// which returns by value -- copies are cheap (one bool, one byte, one
// optional<path> which is mostly empty).
//
// Defaults chosen per subsystem to match prior per-call-site behavior:
//
//   Cbox           LoudThenSummary  (matches mmuLib/CboxEventLog)
//   Unalign        LoudThenSummary  (matches mmuLib/UnalignedEventLog)
//   IntervalTimer  LoudThenSummary  (matches Machine.cpp's SUPPRESSED throttle)
//   Snapshot       Unthrottled      (rare events, one per save/load)
//   PalRelocation  Unthrottled      (one-shot per boot)
//   ChipsetCsr     LoudThenSummary  (CSR_LOG hits a lot during bring-up)
//   StepD          Unthrottled      (one-shot per boot)
//   Misc           LoudThenSummary  (default catchall)
// ----------------------------------------------------------------------------
std::array<LogConfig, kSubsysCount> s_config = []() {
    std::array<LogConfig, kSubsysCount> a{};
    auto set = [&](LogSubsystem s, ThrottlePolicy t) {
        a[static_cast<std::size_t>(s)] = {true, t, std::nullopt};
    };
    set(LogSubsystem::Cbox,          ThrottlePolicy::LoudThenSummary);
    set(LogSubsystem::Unalign,       ThrottlePolicy::LoudThenSummary);
    set(LogSubsystem::IntervalTimer, ThrottlePolicy::LoudThenSummary);
    set(LogSubsystem::Snapshot,      ThrottlePolicy::Unthrottled);
    set(LogSubsystem::PalRelocation, ThrottlePolicy::Unthrottled);
    set(LogSubsystem::ChipsetCsr,    ThrottlePolicy::LoudThenSummary);
    set(LogSubsystem::StepD,         ThrottlePolicy::Unthrottled);
    set(LogSubsystem::Misc,          ThrottlePolicy::LoudThenSummary);
    return a;
}();

std::mutex s_configMutex;

// ----------------------------------------------------------------------------
// File sink state.  One ofstream + mutex per subsystem.  Opened lazily on
// first write after a path is configured; closed (and discarded) by
// resetAllSubsystems().
// ----------------------------------------------------------------------------
struct FileSinkState {
    std::ofstream stream;
    std::mutex    mu;
    bool          open = false;
};

std::array<FileSinkState, kSubsysCount> s_sinks{};


// Map enum to printable name and accept the inverse mapping for CLI parsing.
constexpr std::array<char const*, kSubsysCount> kNames = {
    "Cbox",
    "Unalign",
    "IntervalTimer",
    "Snapshot",
    "PalRelocation",
    "ChipsetCsr",
    "StepD",
    "Misc"
};


// Compare two strings case-insensitively.  CLI users typing "unalign" or
// "Unalign" or "UNALIGN" all reach the same subsystem.
bool ieq(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

// Split a comma-separated list into trimmed tokens.
std::vector<std::string_view> splitCsv(std::string_view body)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == ',') {
            // trim leading/trailing whitespace in [start, i)
            std::size_t s = start;
            std::size_t e = i;
            while (s < e && std::isspace(static_cast<unsigned char>(body[s]))) ++s;
            while (e > s && std::isspace(static_cast<unsigned char>(body[e-1]))) --e;
            if (s != e) out.emplace_back(body.substr(s, e - s));
            start = i + 1;
        }
    }
    return out;
}

} // anonymous namespace


// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
void configureSubsystem(LogSubsystem s, LogConfig const& cfg) noexcept
{
    auto const idx = static_cast<std::size_t>(s);
    if (idx >= kSubsysCount) return;
    std::lock_guard<std::mutex> lock(s_configMutex);
    s_config[idx] = cfg;
}


LogConfig getSubsystemConfig(LogSubsystem s) noexcept
{
    auto const idx = static_cast<std::size_t>(s);
    if (idx >= kSubsysCount) return {};
    std::lock_guard<std::mutex> lock(s_configMutex);
    return s_config[idx];
}


void resetAllSubsystems() noexcept
{
    for (auto& c : s_eventCount) c.store(0, std::memory_order_relaxed);
    for (auto& fs : s_sinks) {
        std::lock_guard<std::mutex> lock(fs.mu);
        if (fs.open) {
            fs.stream.close();
            fs.open = false;
        }
    }
}


char const* subsystemName(LogSubsystem s) noexcept
{
    auto const idx = static_cast<std::size_t>(s);
    if (idx >= kSubsysCount) return "?";
    return kNames[idx];
}


bool subsystemFromName(std::string_view name, LogSubsystem& out) noexcept
{
    for (std::size_t i = 0; i < kSubsysCount; ++i) {
        if (ieq(name, kNames[i])) {
            out = static_cast<LogSubsystem>(i);
            return true;
        }
    }
    return false;
}


#if EMULATR_DIAGNOSTIC_LOGGING

bool subsystemShouldEmit(LogSubsystem s) noexcept
{
    auto const idx = static_cast<std::size_t>(s);
    if (idx >= kSubsysCount) return false;

    // Snapshot the config under the mutex.  Cheap copy.
    LogConfig cfg;
    {
        std::lock_guard<std::mutex> lock(s_configMutex);
        cfg = s_config[idx];
    }
    if (!cfg.enabled) return false;

    uint64_t const n = s_eventCount[idx].fetch_add(1, std::memory_order_relaxed);

    switch (cfg.throttle) {
        case ThrottlePolicy::Off:
            return false;
        case ThrottlePolicy::Unthrottled:
            return true;
        case ThrottlePolicy::LoudThenMute:
            return n < kLoudThenMuteCutoff;
        case ThrottlePolicy::LoudThenSummary:
            if (n < kLoudThreshold) return true;
            return ((n - kLoudThreshold) % kSummaryStride) == 0;
    }
    return false;   // unreachable; satisfies non-void warning
}


void writeToFileSink(LogSubsystem s, std::string_view formatted) noexcept
{
    auto const idx = static_cast<std::size_t>(s);
    if (idx >= kSubsysCount) return;

    // Snapshot the path under the config mutex; release before doing I/O.
    std::optional<std::filesystem::path> path;
    bool enabled = true;
    {
        std::lock_guard<std::mutex> lock(s_configMutex);
        enabled = s_config[idx].enabled;
        path    = s_config[idx].fileSink;
    }
    if (!enabled || !path) return;

    auto& fs = s_sinks[idx];
    std::lock_guard<std::mutex> lock(fs.mu);
    if (!fs.open) {
        std::error_code ec;
        if (auto parent = path->parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        fs.stream.open(*path, std::ios::out | std::ios::trunc);
        if (fs.stream) {
            fs.stream << "# EmulatR V4 " << kNames[idx]
                      << " log -- one line per emit\n";
            fs.open = true;
        } else {
            return;     // I/O failed; emit silently dropped
        }
    }
    fs.stream.write(formatted.data(),
                    static_cast<std::streamsize>(formatted.size()));
    fs.stream.put('\n');
    // No flush per event -- destructor handles it; on crash the tail may
    // be lost but the stderr SPDLOG copy remains.
}

#endif // EMULATR_DIAGNOSTIC_LOGGING


// ----------------------------------------------------------------------------
// CLI wiring helpers
// ----------------------------------------------------------------------------
bool applyLogFlagDisable(std::string_view body, std::string& errMsg) noexcept
{
    LogSubsystem s{};
    if (!subsystemFromName(body, s)) {
        errMsg = std::string{"--log-disable: unknown subsystem '"}
               + std::string{body} + "'";
        return false;
    }
    LogConfig cfg = getSubsystemConfig(s);
    cfg.enabled = false;
    configureSubsystem(s, cfg);
    return true;
}


bool applyLogFlagOnly(std::string_view body, std::string& errMsg) noexcept
{
    auto const tokens = splitCsv(body);
    std::array<bool, kSubsysCount> keep{};
    for (auto t : tokens) {
        LogSubsystem s{};
        if (!subsystemFromName(t, s)) {
            errMsg = std::string{"--log-only: unknown subsystem '"}
                   + std::string{t} + "'";
            return false;
        }
        keep[static_cast<std::size_t>(s)] = true;
    }
    for (std::size_t i = 0; i < kSubsysCount; ++i) {
        LogConfig cfg = getSubsystemConfig(static_cast<LogSubsystem>(i));
        cfg.enabled = keep[i];
        configureSubsystem(static_cast<LogSubsystem>(i), cfg);
    }
    return true;
}


bool applyLogFlagVerbose(std::string_view body, std::string& errMsg) noexcept
{
    LogSubsystem s{};
    if (!subsystemFromName(body, s)) {
        errMsg = std::string{"--log-verbose: unknown subsystem '"}
               + std::string{body} + "'";
        return false;
    }
    LogConfig cfg = getSubsystemConfig(s);
    cfg.enabled  = true;
    cfg.throttle = ThrottlePolicy::Unthrottled;
    configureSubsystem(s, cfg);
    return true;
}


bool applyLogFlagFile(std::string_view body, std::string& errMsg) noexcept
{
    auto const eq = body.find('=');
    if (eq == std::string_view::npos) {
        errMsg = "--log-file: expected <subsystem>=<path>";
        return false;
    }
    std::string_view const subsysName = body.substr(0, eq);
    std::string_view const pathText   = body.substr(eq + 1);
    LogSubsystem s{};
    if (!subsystemFromName(subsysName, s)) {
        errMsg = std::string{"--log-file: unknown subsystem '"}
               + std::string{subsysName} + "'";
        return false;
    }
    if (pathText.empty()) {
        errMsg = "--log-file: empty path after '='";
        return false;
    }
    LogConfig cfg = getSubsystemConfig(s);
    cfg.fileSink = std::filesystem::path(std::string{pathText});
    configureSubsystem(s, cfg);
    return true;
}

} // namespace coreLib
