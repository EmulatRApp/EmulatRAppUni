// ============================================================================
// mmuLib/CboxEventLog.cpp -- implementation of the CBox CSR forensic log.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "mmuLib/CboxEventLog.h"

#include "coreLib/LogArtifactPath.h"  // stem-keyed artifact name (2026-07-31)

#include <atomic>
#include <fstream>
#include <ios>
#include <mutex>

#include <spdlog/spdlog.h>


namespace mmuLib {

namespace {

// FILE 5: mmuLib/CboxEventLog.cpp
// FUNCTION: openIfNeeded -- lazy-open of the CBox CSR access log.
// CHANGE (2026-07-31): fixed kLogPath ("logs/cbox_csr.log") REMOVED; the
//   path is now logs/<stem>_cbox_csr.log via coreLib/LogArtifactPath, so
//   concurrent instances in one run dir no longer truncate each other.
constexpr uint64_t    kLoudThreshold  = 32;
constexpr uint64_t    kSummaryStride  = 64ULL * 1024ULL;

std::atomic<uint64_t> s_count{0};
std::ofstream         s_stream;
std::mutex            s_streamMutex;
bool                  s_streamOpen = false;

void openIfNeeded()
{
    if (s_streamOpen) return;
    // Stem-keyed path; the helper also owns the logs/ create.  See
    // coreLib/LogArtifactPath.h FILE 1 for the concurrency rationale.
    s_stream.open(coreLib::logArtifactPath("cbox_csr", "log"),
                  std::ios::out | std::ios::trunc);
    if (s_stream) {
        s_stream << "# EmulatR V4 CBox CSR (HW_C_DATA / HW_C_SHFT) access log\n";
        s_stream << "# cycle\tpc\top\tipr\tvalue\tcBoxCsr_after\n";
        s_streamOpen = true;
    }
}

char const* opChar(CboxOp op) noexcept
{
    switch (op) {
        case CboxOp::Read:  return "R";
        case CboxOp::Write: return "W";
    }
    return "?";
}

} // namespace


void logCboxEvent(uint64_t cycle,
                  uint64_t pc,
                  CboxOp   op,
                  uint16_t ipr,
                  uint64_t value,
                  uint64_t cBoxCsrAfter) noexcept
{
    uint64_t const n = s_count.fetch_add(1, std::memory_order_relaxed);

    if (n < kLoudThreshold) {
        SPDLOG_WARN(
            "CBOX[{}]: cyc={} pc=0x{:016x} op={} ipr=0x{:03x} value=0x{:016x} cBoxCsr=0x{:016x}",
            n, cycle, pc, opChar(op), static_cast<unsigned>(ipr), value, cBoxCsrAfter);
    } else if (((n - kLoudThreshold) % kSummaryStride) == 0) {
        SPDLOG_INFO("CBOX: {} total events (loud-stderr muted past first {})",
            n + 1, kLoudThreshold);
    }

    std::lock_guard<std::mutex> lock(s_streamMutex);
    openIfNeeded();
    if (s_streamOpen) {
        s_stream << std::dec << cycle << '\t'
                 << "0x" << std::hex << pc << '\t'
                 << opChar(op) << '\t'
                 << "0x" << static_cast<unsigned>(ipr) << '\t'
                 << "0x" << value << '\t'
                 << "0x" << cBoxCsrAfter << '\n';
    }
}


uint64_t cboxEventCount() noexcept
{
    return s_count.load(std::memory_order_relaxed);
}


void resetCboxEventLog() noexcept
{
    std::lock_guard<std::mutex> lock(s_streamMutex);
    if (s_streamOpen) {
        s_stream.close();
        s_streamOpen = false;
    }
    s_count.store(0, std::memory_order_relaxed);
}

} // namespace mmuLib
