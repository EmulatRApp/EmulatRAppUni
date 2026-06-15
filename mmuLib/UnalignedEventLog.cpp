// ============================================================================
// mmuLib/UnalignedEventLog.cpp -- implementation of the forensic unaligned-
// fixup log.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "mmuLib/UnalignedEventLog.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <ios>
#include <mutex>
#include <system_error>

#include <spdlog/spdlog.h>


namespace mmuLib {

namespace {

// File path is relative to CWD.  Build's runtime-layout step creates
// the logs/ directory next to the executable; if a user launches
// from a different CWD the file lands in that CWD's logs/ instead.
// Acceptable; the snapshot subsystem has the same property.
constexpr char const* kLogPath        = "logs/unaligned.log";

// Loud-event threshold.  First N occurrences emit at WARN level for
// immediate visibility; subsequent occurrences are silent on stderr
// but still written to the file.  Adjustable; lower values reduce
// stderr noise during many-event runs at the cost of less
// developer-friendly early surfacing.
constexpr uint64_t    kLoudThreshold  = 16;

// Summary stride.  Every kSummaryStride events past kLoudThreshold,
// emit one SPDLOG_INFO heartbeat so long-running runs surface a
// "still fixing up" pulse.  Tuned to ~256 K events; coarser than
// loud, finer than once-per-run.
constexpr uint64_t    kSummaryStride  = 256ULL * 1024ULL;


std::atomic<uint64_t> s_count{0};
std::ofstream         s_stream;
std::mutex            s_streamMutex;   // serialises file writes; uncontended on V4 v1
bool                  s_streamOpen = false;


// Lazy-open the underlying file on first emit after a reset.  Caller
// must hold s_streamMutex.  On open, truncate + rewrite the header.
// I/O failures leave s_streamOpen = false; the emit call then drops
// the file write and proceeds silently (the SPDLOG surface message
// still fires).
void openIfNeeded()
{
    if (s_streamOpen) return;

    std::error_code ec;
    std::filesystem::create_directories("logs", ec);

    s_stream.open(kLogPath, std::ios::out | std::ios::trunc);
    if (s_stream) {
        s_stream << "# EmulatR V4 unaligned-access fixup log\n";
        s_stream << "# cycle\tpc\tva\twidth\tpalMode\n";
        s_streamOpen = true;
    }
}

} // namespace


void logUnalignedEvent(uint64_t cycle,
                       uint64_t pc,
                       uint64_t va,
                       uint8_t  width,
                       bool     palMode) noexcept
{
    uint64_t const n = s_count.fetch_add(1, std::memory_order_relaxed);

    if (n < kLoudThreshold) {
        SPDLOG_WARN(
            "UNALIGN-FIXUP[{}]: cyc={} pc=0x{:016x} va=0x{:016x} width={} palMode={}",
            n, cycle, pc, va, static_cast<int>(width), palMode ? 1 : 0);
    } else if (((n - kLoudThreshold) % kSummaryStride) == 0) {
        SPDLOG_INFO("UNALIGN-FIXUP: {} total occurrences "
                    "(loud-stderr muted past first {})",
                    n + 1, kLoudThreshold);
    }

    std::lock_guard<std::mutex> lock(s_streamMutex);
    openIfNeeded();
    if (s_streamOpen) {
        // Bypass std::dec/std::hex sticky-flag traps by formatting
        // each field explicitly.  Field separators are tabs so the
        // file is awk/python-parseable as a TSV.
        s_stream << std::dec << cycle << '\t'
                 << "0x" << std::hex << pc << '\t'
                 << "0x" << va << '\t'
                 << std::dec << static_cast<unsigned>(width) << '\t'
                 << (palMode ? 1 : 0) << '\n';
        // No flush per event; the OS buffers and we trade fsync cost
        // for throughput.  On clean halt the destructor flushes; on
        // crash the tail of the file may be lost but the first
        // several events (which are usually the ones that matter)
        // are still preserved by SPDLOG_WARN on stderr.
    }
}


uint64_t unalignedEventCount() noexcept
{
    return s_count.load(std::memory_order_relaxed);
}


void resetUnalignedEventLog() noexcept
{
    std::lock_guard<std::mutex> lock(s_streamMutex);
    if (s_streamOpen) {
        s_stream.close();
        s_streamOpen = false;
    }
    s_count.store(0, std::memory_order_relaxed);
    // Next emit reopens with std::ios::trunc and rewrites the header.
}

} // namespace mmuLib
