// ============================================================================
// tests/traceLib/test_declistingsink.cpp -- doctest cases for DecListingSink
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// Smoke tests on the file-backed sink: feed a sequence of synthetic
// CommitRecords and confirm the resulting log files contain the
// expected listing-line and machine-line shape.  Also exercises the
// PAL window auto-on machinery: silent until onPalEntry, dumps the
// last 10 lookback entries, then loud through onPalExit + tail.
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "traceLib/CommitRecord.h"
#include "traceLib/DecListingSink.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>


using coreLib::BoxResult;
using coreLib::CpuState;
using traceLib::CommitRecord;
using traceLib::DecListingSink;


namespace {

// A synthetic CommitRecord for an ADDQ-shaped instruction, easy to
// match against the log output.  The encoded word here IS a real
// ADDQ R1, R2, R3 with INTA opcode 0x10 and func 0x20.
CommitRecord makeAddqRecord(uint64_t cycle, uint64_t pc,
                            BoxResult const& result)
{
    CommitRecord r{};
    r.cycle    = cycle;
    r.pc       = pc;
    r.encoded  = 0x40220403u;        // ADDQ R1, R2, R3
    r.mnemonic = "ADDQ";
    r.result   = &result;
    return r;
}

std::string slurp(std::filesystem::path const& p)
{
    std::ifstream in{p, std::ios::binary};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(std::string const& haystack, std::string const& needle)
{
    return haystack.find(needle) < haystack.size();
}

} // anonymous namespace


// =============================================================================
// Bare emit -- both channels active, TRACE_INSTR set
// =============================================================================

TEST_CASE("DecListingSink -- emits one listing line and one machine line")
{
    auto const dir   = std::filesystem::temp_directory_path();
    auto const dec   = dir / "emulatr_test_decsink_dec.log";
    auto const mach  = dir / "emulatr_test_decsink_mach.log";
    std::filesystem::remove(dec);
    std::filesystem::remove(mach);

    {
        DecListingSink sink{dec, mach, traceLib::TRACE_INSTR};

        BoxResult res{};
        res.regWriteIdx  = 3;
        res.regWriteIsFp = false;
        res.regWriteValue = 0x52ULL;

        CpuState cpu{};
        cpu.intReg[3] = 0x52ULL;

        CommitRecord rec = makeAddqRecord(/*cycle*/ 7, /*pc*/ 0x1000, res);
        sink.onCommit(rec, cpu);
    }

    auto const decText  = slurp(dec);
    auto const machText = slurp(mach);

    CHECK(contains(decText,  "ADDQ"));
    CHECK(contains(decText,  "R03 = 0x0000000000000052"));
    CHECK(contains(decText,  "00000007"));
    CHECK(contains(decText,  "o0 c00 00000007"));   // global ordinal + cpu-slot columns
    CHECK(contains(machText, "INS ord=0 cpu=0 rpcc=7"));
    CHECK(contains(machText, "mnem=ADDQ"));
    CHECK(contains(machText, "instr=40220403"));

    std::filesystem::remove(dec);
    std::filesystem::remove(mach);
}


// =============================================================================
// TRACE_REGFILE adds the REG line on the machine channel
// =============================================================================

TEST_CASE("DecListingSink -- TRACE_REGFILE emits REG line in machine channel")
{
    auto const dir  = std::filesystem::temp_directory_path();
    auto const mach = dir / "emulatr_test_decsink_regfile.log";
    std::filesystem::remove(mach);

    {
        DecListingSink sink{
            std::filesystem::path{},   // no DEC channel
            mach,
            traceLib::TRACE_INSTR | traceLib::TRACE_REGFILE
        };

        BoxResult res{};
        res.regWriteIdx  = 5;
        res.regWriteValue = 0xDEADBEEFu;

        CpuState cpu{};
        cpu.intReg[5] = 0xDEADBEEFu;

        CommitRecord rec = makeAddqRecord(11, 0x2000, res);
        sink.onCommit(rec, cpu);
    }

    auto const machText = slurp(mach);
    CHECK(contains(machText, "REG ord=0 cpu=0 rpcc=11"));
    CHECK(contains(machText, "R05=00000000deadbeef"));

    std::filesystem::remove(mach);
}


// =============================================================================
// PAL window auto-on: silent then loud, with lookback dump
// =============================================================================

TEST_CASE("DecListingSink -- PAL window auto-on dumps lookback on entry")
{
    auto const dir = std::filesystem::temp_directory_path();
    auto const dec = dir / "emulatr_test_decsink_palwindow.log";
    std::filesystem::remove(dec);

    {
        // PAL_WINDOW gating: emit only inside windows + their tail.
        DecListingSink sink{dec, std::filesystem::path{}, traceLib::TRACE_PAL_WINDOW};

        BoxResult res{};
        res.regWriteIdx  = 1;
        res.regWriteValue = 0x42u;

        CpuState cpu{};
        cpu.intReg[1] = 0x42u;

        // 5 commits BEFORE PAL entry -- should NOT emit; should be
        // captured in lookback.
        for (uint64_t c = 0; c < 5; ++c) {
            sink.onCommit(makeAddqRecord(c, 0x100 + c * 4, res), cpu);
        }

        // PAL entry -- should dump lookback (5 entries) and start
        // emitting forward.
        sink.onPalEntry(/*cycle*/ 5, /*entryPc*/ 0x600000, /*excAddr*/ 0x100);

        // 2 commits inside PAL window -- should emit.
        sink.onCommit(makeAddqRecord(6, 0x600000, res), cpu);
        sink.onCommit(makeAddqRecord(7, 0x600004, res), cpu);

        // PAL exit -- starts the post-exit countdown.
        sink.onPalExit(/*cycle*/ 8, /*targetPc*/ 0x108);

        // 3 commits in the tail; all should emit while countdown > 0.
        sink.onCommit(makeAddqRecord(9,  0x108, res), cpu);
        sink.onCommit(makeAddqRecord(10, 0x10C, res), cpu);
        sink.onCommit(makeAddqRecord(11, 0x110, res), cpu);
    }

    auto const decText = slurp(dec);
    CHECK(contains(decText, "PAL ENTRY"));
    CHECK(contains(decText, "PAL EXIT"));
    CHECK(contains(decText, "Lookback"));
    // The 5 pre-entry instructions should have been replayed from
    // lookback (cycles 0..4); inside-window commits 6 and 7 should
    // also appear; tail commits 9..11 should appear.
    CHECK(contains(decText, "00000000"));   // cycle 0 from lookback
    CHECK(contains(decText, "00000004"));   // cycle 4 from lookback
    CHECK(contains(decText, "00000006"));   // inside window
    CHECK(contains(decText, "00000011"));   // tail

    std::filesystem::remove(dec);
}
