// ============================================================================
// tests/pipelineLib/test_pipelinedriver.cpp -- doctest cases for PipelineDriver
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
// End-to-end exercises that hand-assemble a small program into
// GuestMemory, set up CpuState, and call PipelineDriver::run.  The
// programs use PAL-mode bypass translation (cpu.palMode = true) so
// the kseg-only translator passes VA = PA without needing TLB or
// page tables.
//
// Each test verifies the post-run CpuState shape: register values,
// halted flag, faulting condition, branch outcomes.  The driver is
// the integration seam where decode / GR / EX / MEM / WB all meet,
// so these tests double as the smoke test for the pipeline as a
// whole.
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/DispatchEntry.h"
#include "coreLib/PalShadow.h"
#include "memoryLib/GuestMemory.h"
#include "pipelineLib/PipelineDriver.h"

#include "support/FakeSystemBus.h"

#include <cstdint>
#include <ostream>

using namespace coreLib;
using memoryLib::GuestMemory;
using memoryLib::MemStatus;
using pipelineLib::PipelineDriver;


namespace {

// Wrap a GuestMemory in a FakeSystemBus and run/step -- keeps the call sites
// one-liners.  The bus is DRAM-only for these pipeline tests.
inline void runBus(coreLib::CpuState& cpu, memoryLib::GuestMemory& mem,
                   uint64_t maxCycles) noexcept {
    FakeSystemBus bus(mem);
    pipelineLib::PipelineDriver::run(cpu, bus, mem, maxCycles);
}
inline bool stepBus(coreLib::CpuState& cpu, memoryLib::GuestMemory& mem) noexcept {
    FakeSystemBus bus(mem);
    return pipelineLib::PipelineDriver::step(cpu, bus, mem);
}

// -------------------------------------------------------------------
// Mini-assembler helpers.
// -------------------------------------------------------------------

constexpr uint32_t encMem(uint8_t op, uint8_t ra, uint8_t rb, int16_t disp)
{
    return (uint32_t{op} << 26)
         | (uint32_t{ra} << 21)
         | (uint32_t{rb} << 16)
         | (static_cast<uint32_t>(static_cast<uint16_t>(disp)) & 0xFFFFu);
}

constexpr uint32_t encOp(uint8_t op, uint8_t ra, uint8_t rb, uint8_t func, uint8_t rc)
{
    return (uint32_t{op}   << 26)
         | (uint32_t{ra}   << 21)
         | (uint32_t{rb}   << 16)
         | (uint32_t{func} <<  5)
         |  uint32_t{rc};
}

constexpr uint32_t encBra(uint8_t op, uint8_t ra, int32_t disp21)
{
    return (uint32_t{op} << 26)
         | (uint32_t{ra} << 21)
         | (static_cast<uint32_t>(disp21) & 0x1FFFFFu);
}

constexpr uint32_t encJmp(uint8_t ra, uint8_t rb, uint8_t func)
{
    return (uint32_t{0x1A}   << 26)
         | (uint32_t{ra}     << 21)
         | (uint32_t{rb}     << 16)
         | (uint32_t{func}   << 14);
}

constexpr uint32_t encCallPal(uint32_t func)
{
    return (uint32_t{0x00} << 26) | (func & 0x03FFFFFFu);
}

// CALL_PAL HALT is function code 0.
constexpr uint32_t kHalt = encCallPal(0x0);

// Common opcodes.
constexpr uint8_t kOpLDA  = 0x08;
constexpr uint8_t kOpLDQ  = 0x29;
constexpr uint8_t kOpSTQ  = 0x2D;
constexpr uint8_t kOpINTA = 0x10;
constexpr uint8_t kOpINTL = 0x11;
constexpr uint8_t kOpBR   = 0x30;
constexpr uint8_t kOpBEQ  = 0x39;

// INTA / INTL func codes.
constexpr uint8_t kFuncADDQ = 0x20;
constexpr uint8_t kFuncBIS  = 0x20;   // INTL OR; canonical MOV pseudo


CpuState palModeCpu(uint64_t startPc = 0)
{
    CpuState c{};
    c.mode    = Mode_Privilege::Kernel;
    c.pc      = startPc | uint64_t{1};   // PALmode == PC<0>: bit 0 set => in PAL
    return c;
}

} // anonymous namespace


// =============================================================================
// Trivial halt: HALT at PC 0
// =============================================================================

TEST_CASE("PipelineDriver -- bare HALT halts immediately")
{
    GuestMemory mem(4096);
    CHECK(mem.write4(0x000, kHalt) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    runBus(cpu, mem, /*maxCycles*/ 4);

    CHECK(cpu.halted);
    CHECK(cpu.cycleCount == 1u);   // exactly one slot retired
}


// =============================================================================
// LDA + ADDQ + HALT: register file commits across multiple instructions
// =============================================================================

TEST_CASE("PipelineDriver -- LDA / ADDQ / HALT sequence sets R1 and R2")
{
    GuestMemory mem(4096);
    // LDA R1, 0x100(R31): R1 <- 0x100
    CHECK(mem.write4(0x000, encMem(kOpLDA, /*Ra*/1, /*Rb*/31, /*disp*/0x100))
          == MemStatus::Ok);
    // ADDQ R1, R31, R2: R2 <- R1 + 0
    CHECK(mem.write4(0x004, encOp(kOpINTA, /*Ra*/1, /*Rb*/31, kFuncADDQ, /*Rc*/2))
          == MemStatus::Ok);
    // HALT
    CHECK(mem.write4(0x008, kHalt) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    runBus(cpu, mem, /*maxCycles*/ 16);

    CHECK(cpu.halted);
    CHECK(cpu.intReg[1] == 0x100ULL);
    CHECK(cpu.intReg[2] == 0x100ULL);
    CHECK(cpu.cycleCount == 3u);
}


// =============================================================================
// Memory round-trip: STQ then LDQ confirms the MEM drainer
// =============================================================================

TEST_CASE("PipelineDriver -- STQ / LDQ round-trip via GuestMemory")
{
    GuestMemory mem(4096);
    // LDA R1, 0x800(R31): R1 <- 0x800 (the EA we will store to)
    CHECK(mem.write4(0x000, encMem(kOpLDA, 1, 31, 0x0800)) == MemStatus::Ok);
    // LDA R2, 0x42(R31):  R2 <- 0x42  (the value we will store)
    CHECK(mem.write4(0x004, encMem(kOpLDA, 2, 31, 0x0042)) == MemStatus::Ok);
    // STQ R2, 0(R1):  Mem[R1+0] <- R2
    CHECK(mem.write4(0x008, encMem(kOpSTQ, 2, 1, 0x0000)) == MemStatus::Ok);
    // LDQ R3, 0(R1):  R3 <- Mem[R1+0]
    CHECK(mem.write4(0x00C, encMem(kOpLDQ, 3, 1, 0x0000)) == MemStatus::Ok);
    // HALT
    CHECK(mem.write4(0x010, kHalt) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    // 2026-06-04: C5 removed the blanket PAL-mode physical bypass for
    // D-stream accesses (Ev6Translator.h) -- plain STQ/LDQ now translate
    // even in PAL mode, exactly like silicon.  Seed one identity DTB
    // entry for page 0 (VA 0x800 lives in VPN 0; PFN 0; kernel R+W) so
    // the round-trip exercises the real DTB-hit path instead of the
    // removed bypass.  I-stream fetch is unaffected (PAL I-fetch is
    // physical), which is why only the data accesses needed this.
    cpu.dtbMgr.insert(pteLib::TlbRealm::Dtb, /*va=*/0x0, cpu.asn,
                      pteLib::AlphaPte::makeValid(/*pfn=*/0x0,
                                                  /*kre=*/true,
                                                  /*kwe=*/true));

    runBus(cpu, mem, /*maxCycles*/ 16);

    CHECK(cpu.halted);
    CHECK(cpu.intReg[3] == 0x42ULL);

    // Confirm the store actually landed at PA 0x800.
    uint64_t stored = 0;
    CHECK(mem.read8(0x800, stored) == MemStatus::Ok);
    CHECK(stored == 0x42ULL);
}


// =============================================================================
// Branch flow: BR diverts PC; following instruction at the target executes
// =============================================================================

TEST_CASE("PipelineDriver -- BR diverts past a no-op into HALT")
{
    GuestMemory mem(4096);
    // LDA R1, 0x11(R31)  at PC 0x000  (will execute, R1 <- 0x11)
    CHECK(mem.write4(0x000, encMem(kOpLDA, 1, 31, 0x0011)) == MemStatus::Ok);
    // BR R31, +1 longword  at PC 0x004  (target = 0x004 + 4 + 1*4 = 0x00C)
    CHECK(mem.write4(0x004, encBra(kOpBR, 31, 1)) == MemStatus::Ok);
    // LDA R2, 0x22(R31)  at PC 0x008  (SKIPPED -- branch jumps over)
    CHECK(mem.write4(0x008, encMem(kOpLDA, 2, 31, 0x0022)) == MemStatus::Ok);
    // HALT  at PC 0x00C
    CHECK(mem.write4(0x00C, kHalt) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    runBus(cpu, mem, /*maxCycles*/ 16);

    CHECK(cpu.halted);
    CHECK(cpu.intReg[1] == 0x11ULL);
    CHECK(cpu.intReg[2] == 0u);          // skipped instruction did not run
}


// =============================================================================
// Conditional branch: BEQ with R1 != 0 falls through; with R1 == 0 takes
// =============================================================================

TEST_CASE("PipelineDriver -- BEQ falls through when Ra is non-zero")
{
    GuestMemory mem(4096);
    // LDA R1, 0x1(R31)  -> R1 = 1, non-zero
    CHECK(mem.write4(0x000, encMem(kOpLDA, 1, 31, 0x0001)) == MemStatus::Ok);
    // BEQ R1, +2 longwords -> would skip past HALT to LDA R3 if taken
    CHECK(mem.write4(0x004, encBra(kOpBEQ, 1, 2)) == MemStatus::Ok);
    // HALT  at PC 0x008  (executes -- branch not taken)
    CHECK(mem.write4(0x008, kHalt) == MemStatus::Ok);
    // LDA R3, 0x99(R31)  at PC 0x00C  (NOT reached)
    CHECK(mem.write4(0x00C, encMem(kOpLDA, 3, 31, 0x0099)) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    runBus(cpu, mem, /*maxCycles*/ 16);

    CHECK(cpu.halted);
    CHECK(cpu.intReg[1] == 1u);
    CHECK(cpu.intReg[3] == 0u);   // not reached
}

TEST_CASE("PipelineDriver -- BEQ takes when Ra is zero")
{
    GuestMemory mem(4096);
    // R1 stays at 0 (default); BEQ R1, +1 longword -> target = 0x00C
    CHECK(mem.write4(0x000, encBra(kOpBEQ, 1, 2)) == MemStatus::Ok);
    // LDA R2, 0xBAD(R31)  at PC 0x004  (SKIPPED on taken branch)
    CHECK(mem.write4(0x004, encMem(kOpLDA, 2, 31, 0x0BAD)) == MemStatus::Ok);
    // HALT  at PC 0x008  (also SKIPPED -- target jumps further)
    CHECK(mem.write4(0x008, kHalt) == MemStatus::Ok);
    // LDA R3, 0x77(R31)  at PC 0x00C  (executes after taken branch)
    CHECK(mem.write4(0x00C, encMem(kOpLDA, 3, 31, 0x0077)) == MemStatus::Ok);
    // HALT  at PC 0x010
    CHECK(mem.write4(0x010, kHalt) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    runBus(cpu, mem, /*maxCycles*/ 16);

    CHECK(cpu.halted);
    CHECK(cpu.intReg[2] == 0u);    // not reached
    CHECK(cpu.intReg[3] == 0x77ULL);
}


// =============================================================================
// run() honours maxCycles; non-terminating program is bounded by the caller
// =============================================================================

TEST_CASE("PipelineDriver -- maxCycles caps a non-halting program")
{
    GuestMemory mem(4096);
    // BR R31, -1: branch back to itself.  Infinite loop until
    // maxCycles fires.
    CHECK(mem.write4(0x000, encBra(kOpBR, 31, -1)) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    runBus(cpu, mem, /*maxCycles*/ 5);

    CHECK_FALSE(cpu.halted);
    CHECK(cpu.cycleCount == 5u);
}


// =============================================================================
// Bus-error fault: PC outside the GuestMemory backing
// =============================================================================

TEST_CASE("PipelineDriver -- IF fetch beyond GuestMemory halts with excAddr set")
{
    GuestMemory mem(4096);

    CpuState cpu = palModeCpu(0x10000);   // beyond 4 KiB backing

    runBus(cpu, mem, /*maxCycles*/ 4);

    CHECK(cpu.halted);
    // PALmode == PC<0> (2026-05-21 refactor): the faulting PC carries the
    // mode bit, so palModeCpu(0x10000) runs at pc 0x10001 and excAddr is
    // captured as 0x10001 (PipelineDriver.h l.554).
    CHECK(cpu.excAddr == 0x10001ULL);
    // The faulting fetch ticks one cycle on the bus-error retire path
    // (PipelineDriver.h l.201), same as every other slot.
    CHECK(cpu.cycleCount == 1u);
}


// =============================================================================
// Single-step path
// =============================================================================

TEST_CASE("PipelineDriver::step -- returns false once halt fires")
{
    GuestMemory mem(4096);
    CHECK(mem.write4(0x000, kHalt) == MemStatus::Ok);

    CpuState cpu = palModeCpu(0x000);

    bool const stillRunning = stepBus(cpu, mem);

    CHECK_FALSE(stillRunning);
    CHECK(cpu.halted);
    CHECK(cpu.cycleCount == 1u);
}


// =============================================================================
// PAL shadow swap on trap delivery (regression for the 2026-05-26 fix)
// =============================================================================
//
// EV6 swaps R4-R7 / R20-R23 with their PAL shadow copies on every
// native<->PAL transition gated by I_CTL[SDE].  Trap delivery in
// PipelineDriver::retire() previously raised PALmode with a bare
// "cpu.pc |= 1" and SKIPPED the swap, while HW_REI's return path did
// swap -- an asymmetry that left the PAL handler on the native GPRs and
// made a replayed faulting instruction resolve its base operand from a
// stale shadow register (the DS10 store at PC 0x600d3c read R21 as the
// shadow value 0x0f01 instead of native 0x5f0004, giving a bogus EA).
//
// Repro without TB/page-table scaffolding: a NATIVE-mode fetch of a
// non-kseg VA takes an ITB miss; with a non-zero palBase the trap is
// delivered (entry palBase + 0x580).  With SDE set, the swap must have
// fired by the time the vector is entered.  Before the fix the shadow
// set is untouched and these CHECKs fail.
TEST_CASE("PipelineDriver -- trap delivery swaps PAL shadow regs when SDE set")
{
    GuestMemory mem(4096);

    CpuState cpu{};
    cpu.mode    = Mode_Privilege::Kernel;
    cpu.pc      = 0x0000000000100000ULL;       // native (bit0 = 0), non-kseg => ITB miss
    cpu.palBase = 0x0000000000600000ULL;       // 32 KiB-aligned (EV6 PAL_BASE) => trap delivered
    cpu.i_ctl   = coreLib::kSdeBit;            // enable the R4-7 / R20-23 shadow swap

    // Distinct sentinels so a swap is unambiguous: 0xA*/0xB* for the
    // R4-R7 set, 0xC*/0xD* for the R20-R23 set.
    for (int i = 0; i < 4; ++i) {
        cpu.intReg[4 + i]    = 0x00000000000000A4ULL + static_cast<uint64_t>(i);
        cpu.intShadow[i]     = 0x00000000000000B0ULL + static_cast<uint64_t>(i);
        cpu.intReg[20 + i]   = 0x0000000000000C20ULL + static_cast<uint64_t>(i);
        cpu.intShadow[4 + i] = 0x0000000000000D40ULL + static_cast<uint64_t>(i);
    }

    (void)stepBus(cpu, mem);

    // Trap delivered to the ITB-miss vector, now running in PAL mode.
    CHECK(cpu.pcAddr() == 0x0000000000600580ULL);   // palBase | kEntry_ITB_MISS (0x580)
    CHECK(cpu.inPalMode());

    // The SDE swap fired: the architectural R4-R7 / R20-R23 now hold the
    // former shadow copies, and the shadow store holds the former
    // architectural values.  R21 (intReg[21]) carrying the old shadow is
    // the exact condition that produced the DS10 0x0f01 EA.
    CHECK(cpu.intReg[4]     == 0x00000000000000B0ULL);   // was intShadow[0]
    CHECK(cpu.intReg[21]    == 0x0000000000000D41ULL);   // was intShadow[5]
    CHECK(cpu.intShadow[0]  == 0x00000000000000A4ULL);   // was intReg[4]
    CHECK(cpu.intShadow[5]  == 0x0000000000000C21ULL);   // was intReg[21]
}
