// ============================================================================
// RetireProfiler.h -- always-on retire-PC histogram (boot profiler)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE (ticket 2026-06-05, "silent-console" profiling):
//   Attribute the billions of trace-silent boot cycles to firmware
//   regions WITHOUT trace overhead.  The retire path calls record()
//   once per retired instruction: one masked shift + one array
//   increment + one relaxed atomic load.  No I/O, no allocation, no
//   guest-visible effect -- determinism untouched.
//
// LAYOUT:
//   Buckets of 1 KiB (pc >> 10) spanning the low 16 MiB of PA space,
//   which covers every region the console executes from:
//     0x0000000-0x0200000   decompressed console image (post-takeover)
//     0x0600000-0x0900000   initial PAL + SRM staging (pre-takeover)
//     0x0900000-0x0A00000   compressed image / decompressor
//   PCs above 16 MiB fold into the final catch-all bucket.  The PAL
//   bit (PC<0>) is masked before bucketing.
//
// OPERATOR CONTROLS (VS Immediate window; atomics need the
// ._Storage._Value spelling, no operator=):
//
//   traceLib::RetireProfiler::s_control._Storage._Value = 1   // dump now
//   traceLib::RetireProfiler::s_control._Storage._Value = 2   // mark epoch
//   traceLib::RetireProfiler::s_control._Storage._Value = 3   // dump, then mark
//
//   "Mark epoch" snapshots the counters as a baseline; the next dump
//   reports both absolute counts and counts since the mark, then the
//   control word self-clears.  Machine::run also dumps once at exit.
//
// OUTPUT:
//   ASCII table, one row per non-zero bucket, sorted by count desc:
//   bucket base PA, retire count, percent, since-mark count, first /
//   last cycle seen.  Symbol attribution is an OFFLINE post-step
//   (resolve bucket bases against the Ghidra ApplySrmSymbols map).
//   Directory resolution matches BreakpointSink: EMULATR_RETIRE_TRACE_DIR
//   env override, else D:\EmulatR\traces.
// ============================================================================

#ifndef EMULATR_TRACELIB_RETIREPROFILER_H
#define EMULATR_TRACELIB_RETIREPROFILER_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace traceLib {

class RetireProfiler
{
public:
    static constexpr int      kBucketShift = 10;            // 1 KiB buckets
    static constexpr uint32_t kBucketCount = 16384;         // 16 MiB span
    static constexpr uint32_t kCatchAll    = kBucketCount - 1;

    // Control word (operator-pokeable).  Bit 0 = dump request, bit 1 =
    // epoch-mark request.  Checked with one relaxed load per retire;
    // self-clears after service.
    static constexpr uint32_t kCtlDump = 0x1;
    static constexpr uint32_t kCtlMark = 0x2;
    static inline std::atomic<uint32_t> s_control{ 0 };

    // --------------------------------------------------------------------
    // Hot path -- one call per retired instruction.
    // --------------------------------------------------------------------
    static inline void record(uint64_t pc, uint64_t cycle) noexcept
    {
        uint64_t const pa  = pc & ~uint64_t{ 0x3 };          // strip PAL bit
        uint32_t const idx = (pa >> kBucketShift) < kBucketCount
                           ? static_cast<uint32_t>(pa >> kBucketShift)
                           : kCatchAll;
        State& st = state();
        st.count[idx] += 1;
        if (st.firstCycle[idx] == 0) st.firstCycle[idx] = cycle;
        st.lastCycle[idx] = cycle;
        st.total += 1;

        uint32_t const ctl = s_control.load(std::memory_order_relaxed);
        if (ctl != 0) {
            service(ctl, cycle);
        }
    }

    // --------------------------------------------------------------------
    // Explicit dump (Machine::run exit path).  Tag lands in the header.
    // --------------------------------------------------------------------
    static void dump(char const* tag) noexcept
    {
        dumpToDir(resolveDir().c_str(), tag);
    }

    // Test seam: dump into an explicit directory.
    static void dumpToDir(char const* dir, char const* tag) noexcept
    {
        State& st = state();
        char path[512];
        std::time_t const now = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        std::snprintf(path, sizeof(path),
                      "%s/profile_%04d%02d%02d-%02d%02d%02d_%s.txt",
                      dir,
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                      (tag != nullptr) ? tag : "dump");

        std::FILE* f = std::fopen(path, "w");
        if (f == nullptr) return;

        // Sort non-zero buckets by count, descending.
        std::vector<uint32_t> order;
        order.reserve(256);
        for (uint32_t i = 0; i < kBucketCount; ++i) {
            if (st.count[i] != 0) order.push_back(i);
        }
        std::sort(order.begin(), order.end(),
                  [&st](uint32_t a, uint32_t b) noexcept {
                      return st.count[a] > st.count[b];
                  });

        std::fprintf(f,
            "# EmulatR V4 retire-PC profile -- tag=%s\n"
            "# total_retires=%llu buckets_nonzero=%zu bucket_bytes=%u\n"
            "# columns: bucket_base_pa count pct since_mark first_cyc last_cyc\n",
            (tag != nullptr) ? tag : "dump",
            static_cast<unsigned long long>(st.total),
            order.size(),
            1u << kBucketShift);

        for (uint32_t const i : order) {
            uint64_t const cnt   = st.count[i];
            uint64_t const since = cnt - st.baseline[i];
            double const pct = (st.total != 0)
                ? (100.0 * static_cast<double>(cnt)
                         / static_cast<double>(st.total))
                : 0.0;
            std::fprintf(f,
                "%016llx %12llu %6.2f %12llu %llu %llu%s\n",
                static_cast<unsigned long long>(
                    static_cast<uint64_t>(i) << kBucketShift),
                static_cast<unsigned long long>(cnt),
                pct,
                static_cast<unsigned long long>(since),
                static_cast<unsigned long long>(st.firstCycle[i]),
                static_cast<unsigned long long>(st.lastCycle[i]),
                (i == kCatchAll) ? "  # CATCH-ALL (pc >= 16MiB)" : "");
        }
        std::fclose(f);
        std::fprintf(stderr, "RetireProfiler: dumped %zu buckets to %s\n",
                     order.size(), path);
        std::fflush(stderr);
    }

    // Snapshot current counts as the since-mark baseline.
    static void markEpoch() noexcept
    {
        State& st = state();
        std::memcpy(st.baseline, st.count, sizeof(st.count));
    }

    // Test seam: zero everything.
    static void resetForTest() noexcept
    {
        State& st = state();
        std::memset(&st, 0, sizeof(State));
        s_control.store(0, std::memory_order_relaxed);
    }

    static uint64_t bucketCount(uint32_t idx) noexcept
    {
        return (idx < kBucketCount) ? state().count[idx] : 0;
    }
    static uint64_t totalRetires() noexcept { return state().total; }

private:
    struct State {
        uint64_t count[kBucketCount];
        uint64_t baseline[kBucketCount];
        uint64_t firstCycle[kBucketCount];
        uint64_t lastCycle[kBucketCount];
        uint64_t total;
    };

    // Function-local static: zero-initialized at first use, no static
    // init-order hazard, single definition across TUs (C++17 inline).
    static State& state() noexcept
    {
        static State s_state{};
        return s_state;
    }

    // Cold path: service operator pokes, then self-clear.  Dump runs
    // BEFORE mark so control=3 reports the closing epoch and then
    // opens a fresh one.
    static void service(uint32_t ctl, uint64_t /*cycle*/) noexcept
    {
        if ((ctl & kCtlDump) != 0) dump("poke");
        if ((ctl & kCtlMark) != 0) markEpoch();
        s_control.store(0, std::memory_order_relaxed);
    }

    static std::string resolveDir() noexcept
    {
        char const* const envDir = std::getenv("EMULATR_RETIRE_TRACE_DIR");
        if (envDir != nullptr && envDir[0] != '\0') return envDir;
        return "D:\\EmulatR\\traces";
    }
};

} // namespace traceLib

#endif // EMULATR_TRACELIB_RETIREPROFILER_H
