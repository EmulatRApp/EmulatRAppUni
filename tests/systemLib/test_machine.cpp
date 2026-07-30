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
#include "systemLib/SrmLoader.h"
#include "systemLib/StopReason.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <vector>


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


// =============================================================================
// tryFetch retirement at Step D (TASK-MEMWB-001 root cause, 2026-07-29).
//
// Before Step D the override serves the frozen payload bytes for any
// I-fetch in [loadPa, loadPa + payloadSize) -- the decompressor-stub
// coherency window.  Once onBeforeFetch fires at descriptor.entryPa()
// (the JSR into the decompressed image), the stub era is over and the
// override must DISARM: OpenVMS later loads exec code into that same
// physical range, and a still-armed override feeds the IF stage stale
// firmware bytes while the D-stream sees real memory (bugcheck
// INVEXCEPTN at PA 0x9151C0, VA FFFFFFFF.801151C0).
//
// Synthetic .exe layout mirrors test_srmloader.cpp's makeFakeExe.
// =============================================================================

TEST_CASE("Machine::tryFetch -- serves payload before Step D, disarms after")
{
    // Build a minimal fake SRM .exe: sig at 0x300, PAL_BASE 0x600000,
    // LDA disp (finalPC) 0x5C0, 0x4000 total.
    constexpr size_t   kSigOff   = 0x300;
    constexpr uint64_t kPalBase  = 0x600000ULL;
    constexpr uint16_t kFinalPc  = 0x05C0;
    constexpr size_t   kExeSize  = 0x4000;
    constexpr uint64_t kLoadPa   = systemLib::kDefaultLoadPa;   // 0x900000

    std::vector<uint8_t> bytes(kExeSize, 0);
    std::memcpy(&bytes[kSigOff], systemLib::kDecompSig, systemLib::kDecompSigLen);
    std::memcpy(&bytes[kSigOff + 0x10], &kPalBase, sizeof(kPalBase));
    uint32_t const lda = 0x201A0000u | uint32_t{kFinalPc};   // LDA R0, disp(R26)
    uint32_t const jsr = 0x6BE04000u;                        // JSR R31, (R0)
    std::memcpy(&bytes[kSigOff + 0x18], &lda, sizeof(lda));
    std::memcpy(&bytes[kSigOff + 0x1C], &jsr, sizeof(jsr));

    auto const path = std::filesystem::temp_directory_path() /
                      "emulatr_machine_stepd_disarm.exe";
    {
        std::ofstream out{path, std::ios::binary};
        out.write(reinterpret_cast<char const*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    Machine m{16ULL * 1024ULL * 1024ULL};
    REQUIRE(m.loadSrmFirmware(path, kLoadPa));
    REQUIRE(m.srmDescriptor().valid);

    // Pick a probe PA inside the payload window and overwrite the guest
    // RAM copy there, emulating the stub's self-overwrite.
    constexpr uint64_t kProbeOff = 0x1000;
    uint32_t origWord = 0;
    std::memcpy(&origWord, m.srmPayload().data() + kProbeOff, sizeof(origWord));
    writeWord(m, kLoadPa + kProbeOff, 0xBADC0DE5u);

    // Stub era: override serves the frozen payload bytes, not RAM.
    uint32_t got = 0;
    CHECK(m.tryFetch(kLoadPa + kProbeOff, got));
    CHECK(got == origWord);

    // Fire Step D: first fetch from descriptor.entryPa() (= targetPalBase
    // + finalPC, OUTSIDE the payload window).
    CHECK(m.srmDescriptor().entryPa() == kPalBase + kFinalPc);
    m.onBeforeFetch(m.srmDescriptor().entryPa());

    // Post-Step-D: the override is retired.  IF reads now fall through
    // to GuestMemory, so guest code loaded into the old stub range
    // executes its OWN bytes.
    got = 0xDEADBEEF;
    CHECK_FALSE(m.tryFetch(kLoadPa + kProbeOff, got));
    CHECK(got == 0xDEADBEEFu);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}


// =============================================================================
// Loads to R31 / F31 are prefetches / UNOP -- no access, no fault.
//
// 21264 HRM Sec 2.6: LDBU/LDWU/LDL/LDQ/LDF/LDG/LDS/LDT with destination
// R31/F31 are software-directed prefetches; the HRM's fault-capable
// instruction list reads "LDQ_U (not to R31)" -- the UNOP never touches
// the memory pipe at all.  EmulatR previously executed these as real
// loads: at an unmapped/out-of-range EA they faulted, and since the real
// VMS PALcode has no dismiss arm for LDQ_U R31 (hardware guarantees that
// fault cannot occur), the fault escalated to an impossible guest ACCVIO
// -- INVEXCEPTN in SWAPPER's SCH$FIND_NEXT_PROC epilogue, LDQ_U R31,(SP)
// at the popped stack boundary (2026-07-29).
// =============================================================================

TEST_CASE("Machine -- loads to R31/F31 never access memory or fault")
{
    constexpr uint8_t  kOpLDAH = 0x09;
    constexpr uint8_t  kOpLDQ_U = 0x0B;
    constexpr uint8_t  kOpLDS  = 0x22;
    constexpr uint8_t  kOpLDL  = 0x28;
    constexpr uint8_t  kOpLDQ  = 0x29;

    // R1 = 0x10000 -- far outside this machine's 4 KB memory, so a REAL
    // load through this EA takes kFaultBusError (control case below).
    SUBCASE("prefetch/UNOP forms sail through")
    {
        Machine m{4096};
        writeWord(m, 0x000, encMem(kOpLDAH, 1, 31, 0x0001));   // R1 = 0x10000
        writeWord(m, 0x004, encMem(kOpLDQ_U, 31, 1, 0x0000));  // UNOP
        writeWord(m, 0x008, encMem(kOpLDQ,   31, 1, 0x0008));  // prefetch
        writeWord(m, 0x00C, encMem(kOpLDL,   31, 1, 0x0004));  // prefetch
        writeWord(m, 0x010, encMem(kOpLDS,   31, 1, 0x0004));  // prefetch w/ intent
        writeWord(m, 0x014, encMem(kOpLDA,    2, 31, 0x0077)); // marker
        writeWord(m, 0x018, kHalt);

        m.reset(/*pc*/ 0x000, /*palMode*/ true);
        StopReason const sr = m.run(/*maxCycles*/ 64);

        CHECK(sr == StopReason::HaltedClean);
        CHECK(m.cpu().intReg[2] == 0x77u);            // marker reached
        CHECK(m.cpu().lastFaultCode == 13u);          // kFaultHalt only
    }

    // Control: the SAME EA through a real destination register must
    // still fault (bus error) -- proving the R31 path above skipped the
    // access rather than the fault having gone soft.
    SUBCASE("real load at the same EA still faults")
    {
        Machine m{4096};
        writeWord(m, 0x000, encMem(kOpLDAH, 1, 31, 0x0001));   // R1 = 0x10000
        writeWord(m, 0x004, encMem(kOpLDQ,   3, 1, 0x0000));   // real LDQ -> fault
        writeWord(m, 0x008, encMem(kOpLDA,   2, 31, 0x0077));  // marker
        writeWord(m, 0x00C, kHalt);

        m.reset(/*pc*/ 0x000, /*palMode*/ true);
        (void)m.run(/*maxCycles*/ 64);

        CHECK(m.cpu().intReg[2] != 0x77u);            // marker NOT reached
    }
}
