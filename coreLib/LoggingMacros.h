// ============================================================================
// LoggingMacros.h -- Project-wide logging surface
// ============================================================================
// Project:           EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C):     2025, 2026 eNVy Systems, Inc.  All rights reserved.
// License:           eNVy Systems Non-Commercial License v1.1
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Two channels:
//
//   1. Project-wide level gate (SPDLOG_ACTIVE_LEVEL).  Wraps spdlog's
//      level macros so disabled levels compile to no-ops while still
//      type-checking format strings.  Defaults to WARN floor: TRACE,
//      DEBUG, INFO are silenced; WARN, ERROR, CRITICAL still fire.
//
//   2. Dedicated CSERVE channel (EMULATR_CONSOLE_TRACE).  Always emits
//      regardless of SPDLOG_ACTIVE_LEVEL.  Use for byte-level console
//      exchange during SRM/PAL/OS bring-up so traffic stays visible
//      when the project-wide level gate is high.
//
// Discipline:
//   - Every .cpp using logging includes this header instead of
//     <spdlog/spdlog.h> directly.  That keeps the level gate uniform.
//   - To re-enable a level project-wide, change SPDLOG_ACTIVE_LEVEL
//     below.  To re-enable per file, define SPDLOG_ACTIVE_LEVEL before
//     including this header in that translation unit.
//
// ============================================================================

#ifndef EMULATR_LOGGING_MACROS_H
#define EMULATR_LOGGING_MACROS_H

// ---------------------------------------------------------------------------
// Project-wide level gate -- must be defined before <spdlog/spdlog.h>
// ---------------------------------------------------------------------------
#ifndef SPDLOG_ACTIVE_LEVEL
#  define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_WARN
#endif

#include <spdlog/spdlog.h>
#include <fmt/format.h>

// ---------------------------------------------------------------------------
// Project logging surface
// ---------------------------------------------------------------------------
// Thin wrappers around spdlog's level macros so all project code calls a
// uniform surface.  At disabled levels, spdlog's own SPDLOG_xxx macros
// already collapse to no-ops while keeping format strings type-checked.

#define EMULATR_LOG_TRACE(...)     SPDLOG_TRACE(__VA_ARGS__)
#define EMULATR_LOG_DEBUG(...)     SPDLOG_DEBUG(__VA_ARGS__)
#define EMULATR_LOG_INFO(...)      SPDLOG_INFO(__VA_ARGS__)
#define EMULATR_LOG_WARN(...)      SPDLOG_WARN(__VA_ARGS__)
#define EMULATR_LOG_ERROR(...)     SPDLOG_ERROR(__VA_ARGS__)
#define EMULATR_LOG_CRITICAL(...)  SPDLOG_CRITICAL(__VA_ARGS__)

// ---------------------------------------------------------------------------
// CSERVE traffic channel -- bypasses SPDLOG_ACTIVE_LEVEL
// ---------------------------------------------------------------------------
// Toggle with EMULATR_TRACE_CONSOLE.  When off, the macro compiles to a
// type-checked no-op so format strings still get validated.

#ifndef EMULATR_TRACE_CONSOLE
#  define EMULATR_TRACE_CONSOLE 1
#endif

#if EMULATR_TRACE_CONSOLE
#  define EMULATR_CONSOLE_TRACE(...) \
       ::spdlog::log(::spdlog::level::info, __VA_ARGS__)
#else
#  define EMULATR_CONSOLE_TRACE(...) \
       do { if constexpr (false) { (void)::fmt::format(__VA_ARGS__); } } while (0)
#endif

#endif // EMULATR_LOGGING_MACROS_H

