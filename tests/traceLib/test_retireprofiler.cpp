// ============================================================================
// tests/traceLib/test_retireprofiler.cpp -- doctest cases for the boot
// profiler (always-on retire-PC histogram, 2026-06-05 ticket).
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// doctest note: V4 uses CHECK only, never REQUIRE (exceptions are
// disabled in the build; REQUIRE static_asserts at compile time).
//
// The profiler is process-global static state; every case starts with
// resetForTest() so cases are order-independent.
// ============================================================================

#include "doctest.h"

#include "traceLib/RetireProfiler.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using traceLib::RetireProfiler;


TEST_CASE("RetireProfiler: bucket math -- 1KiB granularity, PAL bit masked")
{
    RetireProfiler::resetForTest();

    // Two PCs in the same 1 KiB bucket; one carries the PAL bit.
    RetireProfiler::record(0x0000000000008681ULL, /*cycle*/ 100);  // PAL entry pc
    RetireProfiler::record(0x0000000000008684ULL, /*cycle*/ 101);

    // bucket = 0x8680 >> 10 = 0x21 (PAL bit stripped before bucketing).
    CHECK(RetireProfiler::bucketCount(0x8680u >> 10) == 2);
    CHECK(RetireProfiler::totalRetires() == 2);

    // Different bucket: console image PC.
    RetireProfiler::record(0x00000000001c6b2cULL, /*cycle*/ 102);
    CHECK(RetireProfiler::bucketCount(0x1c6b2cu >> 10) == 1);
    CHECK(RetireProfiler::totalRetires() == 3);
}


TEST_CASE("RetireProfiler: PCs above the 16MiB span fold into catch-all")
{
    RetireProfiler::resetForTest();

    RetireProfiler::record(0x0000000040000000ULL, /*cycle*/ 5);
    RetireProfiler::record(0x00000801fc000000ULL, /*cycle*/ 6);

    CHECK(RetireProfiler::bucketCount(RetireProfiler::kCatchAll) == 2);
    CHECK(RetireProfiler::totalRetires() == 2);
}


TEST_CASE("RetireProfiler: control poke -- dump then mark; since-mark resets")
{
    RetireProfiler::resetForTest();

    auto const dir = std::filesystem::temp_directory_path()
                   / "emulatr_profiler_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Redirect the poke-path dump into the test dir -- without this
    // the control-word service writes profile_*_poke.txt into the
    // real traces directory on every suite run (litter observed
    // 2026-06-05).
#if defined(_WIN32)
    _putenv_s("EMULATR_RETIRE_TRACE_DIR", dir.string().c_str());
#else
    setenv("EMULATR_RETIRE_TRACE_DIR", dir.string().c_str(), 1);
#endif

    // Phase 1: some traffic, then poke dump+mark (control word 3).
    for (int i = 0; i < 50; ++i) {
        RetireProfiler::record(0x0000000000060000ULL + 4u * i, 1000u + i);
    }
    RetireProfiler::s_control.store(RetireProfiler::kCtlDump
                                  | RetireProfiler::kCtlMark);
    // Service happens inside the next record() call.
    RetireProfiler::record(0x0000000000060000ULL, 2000);
    CHECK(RetireProfiler::s_control.load() == 0);      // self-cleared

    // Phase 2: fresh traffic after the mark, dump to the test dir and
    // verify the since-mark column counts only phase-2 retires.
    for (int i = 0; i < 7; ++i) {
        RetireProfiler::record(0x0000000000061000ULL, 3000u + i);
    }
    RetireProfiler::dumpToDir(dir.string().c_str(), "doctest");

    // Find the dump file and the 0x61000 bucket row.
    bool foundRow = false;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        std::ifstream in(e.path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("0000000000061000", 0) == 0) {
                // columns: base count pct since_mark first last
                unsigned long long cnt = 0, since = 0;
                double pct = 0.0;
                std::sscanf(line.c_str(), "%*s %llu %lf %llu", &cnt, &pct,
                            &since);
                CHECK(cnt == 7);
                CHECK(since == 7);   // all post-mark
                foundRow = true;
            }
        }
    }
    CHECK(foundRow);

    // Clear the env override so later cases / sinks see the default.
#if defined(_WIN32)
    _putenv_s("EMULATR_RETIRE_TRACE_DIR", "");
#else
    unsetenv("EMULATR_RETIRE_TRACE_DIR");
#endif

    std::filesystem::remove_all(dir, ec);
}
