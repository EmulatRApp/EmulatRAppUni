// ============================================================================
// coreLib/LogSubsystem.h -- centralized per-subsystem diagnostic logging
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
// PURPOSE
//
//   Generalizes the two-level gating pattern that CsrDiag.h pioneered
//   (compile-time on/off + runtime mute) across every diagnostic stream
//   in the emulator.  Replaces six independent per-subsystem throttle
//   plumbings with one shared table indexed by an enum.
//
// DESIGN
//
//   * Each diagnostic-producing subsystem registers itself via the
//     LogSubsystem enum.  Adding a subsystem is two lines: an enum
//     value and an entry in the default-config table in the .cpp.
//
//   * A LogConfig per subsystem holds three knobs:
//
//       enabled       master on/off  (default true)
//       throttle      one of:
//                       Unthrottled       -- every emit loud
//                       LoudThenSummary   -- first 16 loud, then one
//                                            INFO heartbeat per 256K
//                       LoudThenMute      -- first 32 loud, then silent
//                       Off               -- never emit
//       fileSink      optional path; opened lazily on first emit
//
//   * Call sites use LOG_SUBSYS(subsys, level, fmt, ...).  Under
//     EMULATR_DIAGNOSTIC_LOGGING the macro expands to:
//
//       1. consult the per-subsystem throttle (cheap atomic load + cmp);
//       2. if loud: emit via SPDLOG_<level>() to stderr;
//       3. if a fileSink is configured: also write the formatted line
//          to that file (lazy-opened, serialized by an internal mutex);
//
//     When EMULATR_DIAGNOSTIC_LOGGING=0 the macro is ((void)0) so the
//     argument expression isn't evaluated and the compiler discards
//     the call entirely.  No call site needs an #ifdef.
//
// CMAKE GATE
//
//   Mirrors the EMULATR_CHIPSET_DIAG pattern in CMakeLists.txt:
//
//     option(EMULATR_DIAGNOSTIC_LOGGING
//            "Compile-in centralized diagnostic log facility" ON)
//
//     if(EMULATR_DIAGNOSTIC_LOGGING)
//         target_compile_definitions(Emulatr PRIVATE
//             $<$<NOT:$<CONFIG:Release>>:EMULATR_DIAGNOSTIC_LOGGING=1>)
//     endif()
//
//   Default ON for Debug and RelWithDebInfo; forced OFF in Release.
//   A production Release build pays zero overhead for the facility.
//
// CLI SURFACE  (parsed in AppOptions; see appliedFromOptions() helper)
//
//   --log-disable <subsys>          (e.g. --log-disable Unalign)
//   --log-only    <subsys>[,..]     silences every subsys not listed
//   --log-verbose <subsys>          forces Unthrottled
//   --log-file    <subsys>=<path>   redirects subsys to a dedicated file
//
//   <subsys> is one of:
//     Cbox  Unalign  IntervalTimer  Snapshot
//     PalRelocation  ChipsetCsr  StepD  Misc
//
// USAGE  (Phase 2 onward, after the call-site sweep)
//
//   #include "coreLib/LogSubsystem.h"
//   using coreLib::LogSubsystem;
//
//   LOG_SUBSYS(IntervalTimer, INFO,
//              "IntervalTimer fired cyc={} pendingIrq2={}",
//              cycleCount, pending);
//
//   LOG_SUBSYS_THROTTLED(Unalign, WARN,
//              "UNALIGN-FIXUP cyc={} pc=0x{:016x} va=0x{:016x}",
//              cycleCount, pc, va);
//
// THREAD SAFETY
//
//   Configuration mutations (configureSubsystem) are protected by an
//   internal mutex.  Hot-path reads (subsystemShouldEmit) use relaxed
//   atomic loads on the enabled flag and the running event counter.
//   File-sink writes are serialized by a per-subsystem mutex; expected
//   contention is zero on V4 v1 (single-threaded).
//
// REFERENCES
//
//   Companion design notes:
//     EmulatRAppUniV4\Emulatr\journals\LogSubsystem_Design_Notes.md
//   Pattern progenitor (chipset-only):
//     EmulatRAppUniV4\Emulatr\chipsetLib\CsrDiag.h
//   Original feedback memory:
//     feedback_logging_toggle_per_subsystem.md
// ============================================================================
//
// CHANGE HISTORY
//
//   2026-05-19  Initial commit -- facility scaffolding without call-site
//               sweep.  Header, .cpp, CMake guard, and the AppOptions CLI
//               wiring land first so the next refactor pass (six call
//               sites) is a mechanical conversion.
// ============================================================================

#ifndef CORELIB_LOG_SUBSYSTEM_H
#define CORELIB_LOG_SUBSYSTEM_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "coreLib/LoggingMacros.h"

namespace coreLib {

// ----------------------------------------------------------------------------
// Subsystem enum -- one per diagnostic stream.  Adding a value here also
// requires adding an entry to s_defaults[] in LogSubsystem.cpp and a name
// in subsystemName().  kCount must stay last.
// ----------------------------------------------------------------------------
enum class LogSubsystem : uint8_t {
    Cbox          = 0,
    Unalign       = 1,
    IntervalTimer = 2,
    Snapshot      = 3,
    PalRelocation = 4,
    ChipsetCsr    = 5,
    StepD         = 6,
    Misc          = 7,
    kCount        = 8
};

// ----------------------------------------------------------------------------
// Throttle policies.  Each subsystem picks one as its default; --log-verbose
// promotes to Unthrottled at runtime.
// ----------------------------------------------------------------------------
enum class ThrottlePolicy : uint8_t {
    Unthrottled     = 0,   // every emit fires
    LoudThenSummary = 1,   // first 16 loud, then 1 INFO per 256 K events
    LoudThenMute    = 2,   // first 32 loud, then silent on stderr
    Off             = 3    // never emit (sink may still receive file writes)
};

// ----------------------------------------------------------------------------
// Per-subsystem configuration record.
// ----------------------------------------------------------------------------
struct LogConfig {
    bool                                 enabled  = true;
    ThrottlePolicy                       throttle = ThrottlePolicy::LoudThenSummary;
    std::optional<std::filesystem::path> fileSink;
};

// ----------------------------------------------------------------------------
// Configuration API (cold path; not called from the run loop).
// ----------------------------------------------------------------------------
void           configureSubsystem(LogSubsystem, LogConfig const&) noexcept;
LogConfig      getSubsystemConfig(LogSubsystem) noexcept;
void           resetAllSubsystems() noexcept;   // called from Machine::run()
char const*    subsystemName(LogSubsystem) noexcept;
bool           subsystemFromName(std::string_view, LogSubsystem& out) noexcept;

// ----------------------------------------------------------------------------
// Hot-path predicate consulted by the LOG_SUBSYS macro.  Returns true if
// the subsystem is enabled AND the current event index is in the loud
// window OR it falls on a summary stride.  Always increments the
// per-subsystem event counter on the call.
// ----------------------------------------------------------------------------
#if EMULATR_DIAGNOSTIC_LOGGING

bool subsystemShouldEmit(LogSubsystem) noexcept;
void writeToFileSink(LogSubsystem, std::string_view formatted) noexcept;

#else  // EMULATR_DIAGNOSTIC_LOGGING

inline bool subsystemShouldEmit(LogSubsystem) noexcept { return false; }
inline void writeToFileSink(LogSubsystem, std::string_view) noexcept {}

#endif // EMULATR_DIAGNOSTIC_LOGGING

// ----------------------------------------------------------------------------
// CLI wiring helper.  Parses one of the --log-* flag bodies and applies it
// to the current LogConfig table.  Returns true on success; on failure sets
// errMsg (caller surfaces via AppOptions parseError).  Defined inline-safe
// so AppOptions can call this without an #ifdef.
// ----------------------------------------------------------------------------
bool applyLogFlagDisable(std::string_view body, std::string& errMsg) noexcept;
bool applyLogFlagOnly   (std::string_view body, std::string& errMsg) noexcept;
bool applyLogFlagVerbose(std::string_view body, std::string& errMsg) noexcept;
bool applyLogFlagFile   (std::string_view body, std::string& errMsg) noexcept;

} // namespace coreLib


// ============================================================================
// Public macros
// ============================================================================
//
// LOG_SUBSYS         -- emit at the given spdlog level (TRACE/DEBUG/INFO/
//                       WARN/ERROR/CRITICAL) gated by per-subsystem enable.
//                       Throttle policy is ignored (every call that passes
//                       the enable gate emits).  Use for one-shot or rare
//                       events where rate-limiting is not desired.
//
// LOG_SUBSYS_THROTTLED -- same as LOG_SUBSYS but consults the throttle
//                       policy; loud emits go to stderr+file, summary
//                       emits go to stderr only via SPDLOG_INFO; muted
//                       events go to file only.  Use for high-frequency
//                       streams (UNALIGN-FIXUP, CSR access).
//
// Both macros expand to ((void)0) when EMULATR_DIAGNOSTIC_LOGGING is off.
// ============================================================================

#if EMULATR_DIAGNOSTIC_LOGGING

#define LOG_SUBSYS(subsys, level, ...)                                          \
    do {                                                                        \
        if (::coreLib::getSubsystemConfig(::coreLib::LogSubsystem::subsys)      \
                .enabled) {                                                     \
            SPDLOG_##level(__VA_ARGS__);                                        \
            ::coreLib::writeToFileSink(                                         \
                ::coreLib::LogSubsystem::subsys,                                \
                ::fmt::format(__VA_ARGS__));                                    \
        }                                                                       \
    } while (0)

#define LOG_SUBSYS_THROTTLED(subsys, level, ...)                                \
    do {                                                                        \
        if (::coreLib::subsystemShouldEmit(                                     \
                ::coreLib::LogSubsystem::subsys)) {                             \
            SPDLOG_##level(__VA_ARGS__);                                        \
        }                                                                       \
        ::coreLib::writeToFileSink(                                             \
            ::coreLib::LogSubsystem::subsys,                                    \
            ::fmt::format(__VA_ARGS__));                                        \
    } while (0)

#else // EMULATR_DIAGNOSTIC_LOGGING

#define LOG_SUBSYS(subsys, level, ...)            ((void)0)
#define LOG_SUBSYS_THROTTLED(subsys, level, ...)  ((void)0)

#endif // EMULATR_DIAGNOSTIC_LOGGING

#endif // CORELIB_LOG_SUBSYSTEM_H
