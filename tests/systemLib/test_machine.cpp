// ============================================================================
// tests/systemLib/test_machine.cpp -- doctest cases for systemLib::Machine
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// End-to-end exercises that hand-assemble a tiny program directly into
// the Machine's GuestMemory (skipping FirmwareLoader, which has its
// own test file), reset, run, and confirm the post-run StopReason +
// register state.  These prove the Phase 1 spine composes: Machine
// orchestrates CpuState + GuestMemory + PipelineDriver as one unit.
//
// ============================================================================

#include "doctest.h"

#include "memoryLib/GuestMemory.h"
#include "systemLib/Machine.h"
#include "systemLib/StopReason.h"

#include <cstdint>
#include <ostream>


using memoryLib::MemStatus;
using systemLib::Machine;
using systemLib::StopReason;


namespace {

// Mini-assembler helpers -- duplicated from test_pipelinedriver.cpp so
// each test file is self-contained.
constexpr uint32_t encMem(uint8_t op, uint8_t ra, uint8_t rb, int16_t disp)
{
    return (uint32_t{op} << 26)
         | (uint32_t{ra} << 21)
         | (uint32_t{rb} << 16)
         | (static_cast<uint32_t>(static_cast<uint16_t>(disp)) & 0xFFFFu);
}

constexpr uint32_t encOp(uint8_t op, uint8_t ra, uint8_t rb,
                         uint8_t func, uint8_t rc)
{
    return (uint32_t{op}   << 26)
         | (uint32_t{ra}   << 21)
         | (uint32_t{rb}   << 16)
         | (uint32_t{func} <<  5)
         |  uint32_t{rc};
}

constexpr uint32_t encCallPal(uint32_t func)
{
    return (uint32_t{0x00} << 26) | (func & 0x03FFFFFFu);
}

constexpr uint32_t kHalt = encCallPal(0x0);

constexpr uint8_t  kOpLDA  = 0x08;
constexpr uint8_t  kOpINTA = 0x10;
constexpr uint8_t  kFuncADDQ = 0x20;

// Place a 32-bit instruction word at PA inside the machine's memory.
void writeWord(Machine& m, uint64_t pa, uint32_t word)
{
    CHECK(m.memory().write4(pa, word) == MemStatus::Ok);
}

} // anonymous namespace


// =============================================================================
// Trivial halt
// =============================================================================

TEST_CASE("Machine -- HALT at PC 0 returns HaltedClean")
{
    Machine m{4096};
    writeWord(m, 0x000, kHalt);

    m.reset(/*pc*/ 0x000, /*palMode*/ true);

    StopReason const sr = m.run(/*maxCycles*/ 8);

    CHECK(sr == StopReason::HaltedClean);
    CHECK(m.cpu().halted);
    CHECK(m.cpu().lastFaultCode == 13u);   // kFaultHalt
}


// =============================================================================
// LDA + ADDQ + HALT -- spine end-to-end
// =============================================================================

TEST_CASE("Machine -- LDA, LDA, ADDQ, HALT computes 0x42 + 0x10 = 0x52")
{
    Machine m{4096};

    // PC 0x000: LDA R1, 0x42(R31)         -> R1 = 0x42
    writeWord(m, 0x000, encMem(kOpLDA, 1, 31, 0x0042));
    // PC 0x004: LDA R2, 0x10(R31)         -> R2 = 0x10
    writeWord(m, 0x004, encMem(kOpLDA, 2, 31, 0x0010));
    // PC 0x008: ADDQ R3, R1, R2           -> R3 = R1 + R2
    writeWord(m, 0x008, encOp(kOpINTA, 1, 2, kFuncADDQ, 3));
    // PC 0x00C: HALT
    writeWord(m, 0x00C, kHalt);

    m.reset(/*pc*/ 0x000, /*palMode*/ true);

    StopReason const sr = m.run(/*maxCycles*/ 32);

    CHECK(sr == StopReason::HaltedClean);
    CHECK(m.cpu().intReg[1] == 0x42u);
    CHECK(m.cpu().intReg[2] == 0x10u);
    CHECK(m.cpu().intReg[3] == 0x52u);
}


// =============================================================================
// Max cycles bound
// =============================================================================

TEST_CASE("Machine -- maxCycles caps a non-halting program")
{
    Machine m{4096};

    // PC 0x000: BR R31, -1   -- branch back to itself, infinite loop
    constexpr uint32_t encBranchSelf =
          (uint32_t{0x30} << 26)            // opcode BR
        | (uint32_t{31}   << 21)            // Ra = R31 (no commit)
        | (static_cast<uint32_t>(-1) & 0x1FFFFFu);
    writeWord(m, 0x000, encBranchSelf);

    m.reset(/*pc*/ 0x000, /*palMode*/ true);

    StopReason const sr = m.run(/*maxCycles*/ 5);

    CHECK(sr == StopReason::MaxCyclesExceeded);
    CHECK_FALSE(m.cpu().halted);
    CHECK(m.cpu().cycleCount == 5u);
}


// =============================================================================
// Reset semantics
// =============================================================================

TEST_CASE("Machine -- reset clears regfile and halt flags")
{
    Machine m{4096};
    writeWord(m, 0x000, kHalt);

    // Run once -> halted, lastFaultCode = kFaultHalt.
    m.reset(0x000, true);
    (void)m.run(8);
    CHECK(m.cpu().halted);

    // Mutate a register, then reset and confirm clean state.
    m.cpu().intReg[5] = 0xDEADBEEFu;

    m.reset(0x000, /*palMode*/ false);

    CHECK(m.cpu().pc == 0x000u);
    CHECK_FALSE(m.cpu().inPalMode());
    CHECK_FALSE(m.cpu().halted);
    CHECK(m.cpu().lastFaultCode == 0u);
    CHECK(m.cpu().cycleCount == 0u);
    CHECK(m.cpu().intReg[5] == 0u);
}


// =============================================================================
// Step semantics
// =============================================================================

TEST_CASE("Machine -- step returns false when CPU halts")
{
    Machine m{4096};
    writeWord(m, 0x000, kHalt);

    m.reset(0x000, true);

    bool const stillRunning = m.step();
    CHECK_FALSE(stillRunning);
    CHECK(m.cpu().halted);
}


// =============================================================================
// IFetchOverride -- decouples IBox from corrupted GuestMemory in the
// SRM stub region.  The override is only active after a successful
// loadSrmFirmware; raw-binary loads (the case in this test file's
// other cases) leave m_srmDescriptor.valid = false and tryFetch
// always returns false, so the pipeline reads from GuestMemory
// normally.
//
// Direct API test: write a known instruction to GuestMemory at a PA
// in the stub region, then write garbage to the same PA, then call
// tryFetch and confirm the original instruction comes back -- proving
// the IF stage will fetch the correct bytes even after the
// decompressor copy loop overwrites them.
//
// We have to construct a minimal SRM payload + descriptor on Machine
// by hand because the real loadSrmFirmware path needs a file on disk.
// Mark the descriptor valid and seed m_srmPayload with crafted bytes
// via a friend-style backdoor would be cleaner, but for now we just
// reach into the public srmDescriptor / srmPayload accessors -- they
// return const references so we cannot mutate; therefore this test
// uses the actual loadSrmFirmware-then-corrupt path with a synthetic
// .exe.  When that lands as a helper, this case can simplify.
// =============================================================================

TEST_CASE("Machine::tryFetch -- without SRM load returns false")
{
    Machine m{4096};
    writeWord(m, 0x000, kHalt);

    uint32_t got = 0xDEADBEEF;
    bool const hit = m.tryFetch(/*pa*/ 0x000, got);
    CHECK_FALSE(hit);
    // got must be left unchanged on miss; pipeline relies on the
    // returned bool, not the out param, to decide what to do.
    CHECK(got == 0xDEADBEEFu);
}
