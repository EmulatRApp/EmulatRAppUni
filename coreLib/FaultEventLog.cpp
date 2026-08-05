// ============================================================================
// coreLib/FaultEventLog.cpp -- implementation of the fault-delivery log.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================

#include "coreLib/FaultEventLog.h"

#include "coreLib/BoxResult.h"   // kFault* constants for faultName()
#include "coreLib/LogArtifactPath.h"  // stem-keyed artifact name (2026-07-31)

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <mutex>

#include <spdlog/spdlog.h>


namespace coreLib {

namespace {

// FILE 3: coreLib/FaultEventLog.cpp
// FUNCTION: openIfNeeded -- lazy-open of the fault-delivery log.
// CHANGE (2026-07-31): the fixed kLogPath constant ("logs/faults.log") is
//   GONE.  It made concurrent same-run-dir instances truncate one another.
//   The path is now built by coreLib/LogArtifactPath as
//   logs/<stem>_faults.log, keyed to this run's --firmware stem.

// First N faults emit at WARN level for immediate stderr visibility;
// subsequent ones are silent on stderr but still written to the file
// (the FILE is never capped -- only stderr loudness is).  Faults are sparse
// relative to retired instructions, so a generous threshold is cheap and
// surfaces the early demand list loudly.
constexpr uint64_t    kLoudThreshold = 64;

// Heartbeat stride past the loud threshold.
constexpr uint64_t    kSummaryStride = 64ULL * 1024ULL;

// JRN-VMB-016 Sec 3.14 probe: the default 64-fault stderr threshold is fully
// consumed by the powerup burst (~cyc 1.21B), so the LATER handoff faults at
// cyc ~1.9B are silent on the console (though still in logs/<stem>_faults.log).  Two
// env-gated overrides make a chosen window loud again, zero-cost when unset:
//   EMULATR_FAULT_LOUD=<n>              -- raise (or lower) the loud threshold.
//   EMULATR_FAULT_CYCLO / _CYCHI=<cyc>  -- ALSO emit loud for any fault whose
//                                          cycle is in [CYCLO,CYCHI], regardless
//                                          of the threshold (the "cyc-filter").
// Read once on first fault after program start (getenv is not re-checked).
struct FaultLoudCfg {
    uint64_t threshold = kLoudThreshold;
    uint64_t cycLo     = 0;
    uint64_t cycHi     = 0;   // hi==0 => window disabled
    FaultLoudCfg() noexcept
    {
        if (char const* s = std::getenv("EMULATR_FAULT_LOUD"))
            threshold = std::strtoull(s, nullptr, 0);
        if (char const* s = std::getenv("EMULATR_FAULT_CYCLO"))
            cycLo = std::strtoull(s, nullptr, 0);
        if (char const* s = std::getenv("EMULATR_FAULT_CYCHI"))
            cycHi = std::strtoull(s, nullptr, 0);
    }
};


std::atomic<uint64_t> s_count{0};
std::ofstream         s_stream;
std::mutex            s_streamMutex;   // serialises file writes; uncontended on V4 v1
bool                  s_streamOpen = false;


// Human-readable fault name.  Kept in sync with coreLib/BoxResult.h kFault*.
char const* faultName(uint16_t code) noexcept
{
    switch (code) {
        case kNoFault:            return "kNoFault";
        case kFaultOpcDec:        return "kFaultOpcDec";
        case kFaultPrivileged:    return "kFaultPrivileged";
        case kFaultUnimplemented: return "kFaultUnimplemented";
        case kFaultUnaligned:     return "kFaultUnaligned";
        case kFaultDtbMiss:       return "kFaultDtbMiss";
        case kFaultItbMiss:       return "kFaultItbMiss";
        case kFaultAcv:           return "kFaultAcv";
        case kFaultFor:           return "kFaultFor";
        case kFaultFow:           return "kFaultFow";
        case kFaultFoe:           return "kFaultFoe";
        case kFaultBusError:      return "kFaultBusError";
        case kFaultNonCanonical:  return "kFaultNonCanonical";
        case kFaultHalt:          return "kFaultHalt";
        case kFaultDtbMissDouble: return "kFaultDtbMissDouble";
        default:                  return "<unknown-fault>";
    }
}


// Lazy-open on first emit after a reset.  Caller holds s_streamMutex.
void openIfNeeded()
{
    if (s_streamOpen) return;

    // Stem-keyed path; the helper also owns the logs/ create.  See
    // coreLib/LogArtifactPath.h FILE 1 for the concurrency rationale.
    s_stream.open(logArtifactPath("faults", "log"),
                  std::ios::out | std::ios::trunc);
    if (s_stream) {
        s_stream << "# EmulatR V4 fault-delivery log\n";
        s_stream << "# cycle\tpc\tencoded\topcode\tfaultCode\tfaultName\tpalMode\n";
        s_streamOpen = true;
    }
}

} // namespace


void logFaultEvent(uint64_t cycle,
                   uint64_t pc,
                   uint32_t encoded,
                   uint16_t faultCode,
                   bool     palMode,
                   uint64_t va) noexcept
{
    static FaultLoudCfg const cfg;   // env read once, thread-safe init

    uint64_t const n      = s_count.fetch_add(1, std::memory_order_relaxed);
    unsigned const opcode = static_cast<unsigned>((encoded >> 26) & 0x3Fu);

    bool const inCycWindow =
        cfg.cycHi != 0 && cycle >= cfg.cycLo && cycle <= cfg.cycHi;

    if (n < cfg.threshold || inCycWindow) {
        SPDLOG_WARN(
            "FAULT[{}]: cyc={} pc=0x{:016x} encoded=0x{:08x} op=0x{:02x} "
            "fault={} ({}) palMode={} va=0x{:016x}",
            n, cycle, pc, encoded, opcode,
            faultCode, faultName(faultCode), palMode ? 1 : 0, va);
    } else if (((n - cfg.threshold) % kSummaryStride) == 0) {
        SPDLOG_INFO("FAULT: {} total deliveries "
                    "(loud-stderr muted past first {})",
                    n + 1, cfg.threshold);
    }

    std::lock_guard<std::mutex> lock(s_streamMutex);
    openIfNeeded();
    if (s_streamOpen) {
        // Explicit per-field formatting to dodge std::dec/std::hex sticky
        // flags; tab-separated so the file parses as a TSV.
        s_stream << std::dec << cycle << '\t'
                 << "0x" << std::hex << pc << '\t'
                 << "0x" << encoded << '\t'
                 << "0x" << opcode << '\t'
                 << std::dec << faultCode << '\t'
                 << faultName(faultCode) << '\t'
                 << (palMode ? 1 : 0) << '\t'
                 << "0x" << std::hex << va << '\n';
    }
}


uint64_t faultEventCount() noexcept
{
    return s_count.load(std::memory_order_relaxed);
}


void resetFaultEventLog() noexcept
{
    std::lock_guard<std::mutex> lock(s_streamMutex);
    if (s_streamOpen) {
        s_stream.close();
        s_streamOpen = false;
    }
    s_count.store(0, std::memory_order_relaxed);
    // Next emit reopens with std::ios::trunc and rewrites the header.
}

} // namespace coreLib
