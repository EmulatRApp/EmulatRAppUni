// ============================================================================
// tests/pipelineLib/test_memdrainer.cpp -- doctest cases for MemDrainer
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
// Tests exercise drain across the contract surface:
//   * compute-only effects commit Ra without memory
//   * loads land sign-extension per memSize / regWriteIsFp
//   * stores publish memData at translated PA
//   * STL_C / STQ_C interact with the per-CPU reservation
//   * translation faults populate faultCode and stash mm_stat
//
// Translation here uses the kseg-only Ev6Translator: tests configure
// CpuState with palMode = true so the bypass path is exercised
// without needing a TLB / page walker.  PA equals the low 44 bits
// of VA in that path.
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/PipelineSlot.h"
#include "coreLib/VA_types.h"
#include "memoryLib/GuestMemory.h"
#include "pipelineLib/MemDrainer.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

#include <cstdint>
#include <ostream>

#include "memoryLib/ISystemBus.h"

using namespace coreLib;
using grainFactory::GrainSem;
using memoryLib::GuestMemory;
using memoryLib::MemStatus;
using pipelineLib::MemDrainer;


namespace {

// FakeSystemBus -- minimal ISystemBus over a GuestMemory so MemDrainer (now
// taking an ISystemBus&) is unit-testable without the chipset.  DRAM-only;
// MMIO/NXM routing is covered by tests/chipsetLib/test_systembus_arbiter.cpp.
struct FakeSystemBus final : memoryLib::ISystemBus {
    GuestMemory& mem;
    explicit FakeSystemBus(GuestMemory& m) noexcept : mem(m) {}
    memoryLib::BusResult read(uint64_t pa, uint8_t width) noexcept override {
        if (pa + width > mem.sizeBytes()) return { memoryLib::BusStatus::BusError, 0 };
        uint64_t out = 0;
        switch (width) {
        case 1: { uint8_t  v = 0; mem.read1(pa, v); out = v; break; }
        case 2: { uint16_t v = 0; mem.read2(pa, v); out = v; break; }
        case 4: { uint32_t v = 0; mem.read4(pa, v); out = v; break; }
        case 8: {                 mem.read8(pa, out);       break; }
        default: return { memoryLib::BusStatus::BusError, 0 };
        }
        return { memoryLib::BusStatus::Ok, out };
    }
    memoryLib::BusResult write(uint64_t pa, uint64_t value, uint8_t width) noexcept override {
        if (pa + width > mem.sizeBytes()) return { memoryLib::BusStatus::BusError, 0 };
        switch (width) {
        case 1: mem.write1(pa, static_cast<uint8_t> (value)); break;
        case 2: mem.write2(pa, static_cast<uint16_t>(value)); break;
        case 4: mem.write4(pa, static_cast<uint32_t>(value)); break;
        case 8: mem.write8(pa,                       value);  break;
        default: return { memoryLib::BusStatus::BusError, 0 };
        }
        return { memoryLib::BusStatus::Ok, 0 };
    }
    memoryLib::BusResult fetch(uint64_t pa, uint8_t width) noexcept override { return read(pa, width); }
};

// Wrap a GuestMemory and run drain -- keeps each call site a one-liner.
inline void drainBus(PipelineSlot& s, CpuState& cpu, GuestMemory& mem) noexcept {
    FakeSystemBus bus(mem);
    MemDrainer::drain(s, cpu, bus, mem.lockMonitor());
}

// Build a slot whose result carries the given memEffect.  PAL-mode
// CpuState lets the translator pass VA -> PA (= VA & 44-bit mask).
PipelineSlot slotWithEffect(uint8_t  memSize,
                            uint64_t memAddr,
                            uint64_t memData,
                            bool     memIsStore,
                            uint8_t  regWriteIdx,
                            GrainSem semFlags)
{
    PipelineSlot s{};
    s.result.memSize      = memSize;
    s.result.memAddr      = memAddr;
    s.result.memData      = memData;
    s.result.memIsStore   = memIsStore;
    s.result.regWriteIdx  = regWriteIdx;
    s.result.regWriteIsFp = false;
    s.result.semFlags     = semFlags;
    return s;
}

CpuState palModeCpu()
{
    CpuState c{};
    c.pc |= uint64_t{ 1 };   // PALmode == PC<0>: bit 0 set => in PAL (was c.palMode = true)
    c.mode    = Mode_Privilege::Kernel;
    return c;
}

// Flag sets mirror the production GrainEntry shapes for each Mem-format
// instruction: loads commit Ra (the fill), STx_C commits Ra (success
// indicator), plain stores commit nothing.  S_WritesRa + S_WritesInt
// are required by the MEM-drainer's structural assertion -- a leaf
// that returns regWriteIdx != kNoRegWrite must declare the commit
// channel in semFlags or the drainer flags it as a spurious commit.
//
// 2026-06-04: S_PhysAddr added to every memory-effect flag set.  These
// tests validate DRAIN COMMIT semantics (fill routing, sign-extension,
// reservation behavior), not translation.  They previously leaned on
// the pre-C5 "PAL mode = physical" bypass; C5 (Ev6Translator.h, dated
// 2026-05-27) correctly removed it -- EV6 PAL mode does NOT disable
// D-stream translation -- so a plain VA against an empty DTB now
// faults DtbMiss(5) and the old flag sets broke.  Tagging the slots
// S_PhysAddr routes them through the HW_LD/HW_ST/LDQP-style physical
// path (pa == va), decoupling drain tests from MMU state permanently.
// Translation has its own coverage (tests/pteLib/test_spam.cpp).
constexpr GrainSem kPlainLoadFlags = GrainSem::S_MemFormat
                                   | GrainSem::S_Load
                                   | GrainSem::S_PhysAddr
                                   | GrainSem::S_WritesRa
                                   | GrainSem::S_WritesInt;

constexpr GrainSem kPlainStoreFlags = GrainSem::S_MemFormat
                                    | GrainSem::S_Store
                                    | GrainSem::S_PhysAddr;

constexpr GrainSem kLockedLoadFlags  = GrainSem::S_MemFormat
                                     | GrainSem::S_Load
                                     | GrainSem::S_Locked
                                     | GrainSem::S_PhysAddr
                                     | GrainSem::S_WritesRa
                                     | GrainSem::S_WritesInt;

constexpr GrainSem kLockedStoreFlags = GrainSem::S_MemFormat
                                     | GrainSem::S_Store
                                     | GrainSem::S_Locked
                                     | GrainSem::S_PhysAddr
                                     | GrainSem::S_WritesRa
                                     | GrainSem::S_WritesInt;

} // anonymous namespace


// =============================================================================
// Compute-only: no memEffect, register commit only
// =============================================================================

TEST_CASE("MemDrainer -- LDA-shaped result commits Ra without memory")
{
    PipelineSlot s{};
    s.result.memSize      = kNoMemEffect;
    s.result.regWriteIdx  = 5;
    s.result.regWriteIsFp = false;
    s.result.regWriteValue = 0xDEADBEEFu;
    s.result.semFlags     = GrainSem::S_MemFormat
                          | GrainSem::S_WritesRa
                          | GrainSem::S_WritesInt;

    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kNoFault);
    CHECK(cpu.intReg[5] == 0xDEADBEEFu);
}

TEST_CASE("MemDrainer -- regWriteIdx == kNoRegWrite suppresses commit")
{
    PipelineSlot s{};
    s.result.regWriteIdx   = kNoRegWrite;
    s.result.regWriteValue = 0xCAFE;
    s.result.semFlags      = GrainSem::S_MemFormat;

    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    drainBus(s, cpu, mem);

    // intReg[31] is the architectural zero register; commit must be
    // suppressed even if regWriteValue is non-zero.
    CHECK(cpu.intReg[31] == 0u);
}


// =============================================================================
// Load: sign-extension per memSize / regWriteIsFp
// =============================================================================

TEST_CASE("MemDrainer -- LDQ commits 64-bit fill into Ra")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);
    CHECK(mem.write8(0x100, 0x1122334455667788ull) == MemStatus::Ok);

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x100, /*data*/0,
                                    /*isStore*/false, /*Ra*/7, kPlainLoadFlags);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kNoFault);
    CHECK(cpu.intReg[7] == 0x1122334455667788ull);
}

TEST_CASE("MemDrainer -- LDL sign-extends 32-bit fill to 64 bits")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);
    CHECK(mem.write4(0x200, 0xFFFFFFFEu) == MemStatus::Ok);

    PipelineSlot s = slotWithEffect(/*size*/4, /*addr*/0x200, /*data*/0,
                                    /*isStore*/false, /*Ra*/8, kPlainLoadFlags);

    drainBus(s, cpu, mem);

    CHECK(cpu.intReg[8] == 0xFFFFFFFFFFFFFFFEull);
}

TEST_CASE("MemDrainer -- LDBU zero-extends 8-bit fill")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);
    CHECK(mem.write1(0x300, 0x80) == MemStatus::Ok);

    PipelineSlot s = slotWithEffect(/*size*/1, /*addr*/0x300, /*data*/0,
                                    /*isStore*/false, /*Ra*/9, kPlainLoadFlags);

    drainBus(s, cpu, mem);

    CHECK(cpu.intReg[9] == 0x80ull);   // zero-extended, not sign-extended
}

TEST_CASE("MemDrainer -- LDWU zero-extends 16-bit fill")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);
    CHECK(mem.write2(0x400, 0x8000) == MemStatus::Ok);

    PipelineSlot s = slotWithEffect(/*size*/2, /*addr*/0x400, /*data*/0,
                                    /*isStore*/false, /*Ra*/10, kPlainLoadFlags);

    drainBus(s, cpu, mem);

    CHECK(cpu.intReg[10] == 0x8000ull);   // zero-extended
}


// =============================================================================
// Store: publish memData at translated PA
// =============================================================================

TEST_CASE("MemDrainer -- STQ publishes 64-bit memData")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x500,
                                    /*data*/0xAABBCCDDEEFF0011ull,
                                    /*isStore*/true, /*Ra*/kNoRegWrite,
                                    kPlainStoreFlags);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kNoFault);
    uint64_t back = 0;
    CHECK(mem.read8(0x500, back) == MemStatus::Ok);
    CHECK(back == 0xAABBCCDDEEFF0011ull);
}

TEST_CASE("MemDrainer -- STB publishes 1-byte memData")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    PipelineSlot s = slotWithEffect(/*size*/1, /*addr*/0x600, /*data*/0xCC,
                                    /*isStore*/true, /*Ra*/kNoRegWrite,
                                    kPlainStoreFlags);

    drainBus(s, cpu, mem);

    uint8_t back = 0;
    CHECK(mem.read1(0x600, back) == MemStatus::Ok);
    CHECK(back == 0xCC);
}


// =============================================================================
// LDQ_L / STQ_C: reservation pair
// =============================================================================

TEST_CASE("MemDrainer -- LDQ_L sets reservation at cache-line granularity")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);
    CHECK(mem.write8(0x700, 0x1234ull) == MemStatus::Ok);

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x700, /*data*/0,
                                    /*isStore*/false, /*Ra*/11,
                                    kLockedLoadFlags);

    drainBus(s, cpu, mem);

    CHECK(cpu.intReg[11] == 0x1234ull);
    // Reservation now lives in the LockMonitor SSOT, line-masked to 64B.
    CHECK(mem.lockMonitor().check(static_cast<int>(cpu.cpuSlot), 0x700ULL));
}

TEST_CASE("MemDrainer -- STQ_C succeeds when reservation matches; writes 1 to Ra")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    // Pre-reserve as if LDQ_L at 0x700 had already drained.
    mem.lockMonitor().set(static_cast<int>(cpu.cpuSlot), 0x700ULL);

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x700,
                                    /*data*/0xFEEDFACEull,
                                    /*isStore*/true, /*Ra*/12,
                                    kLockedStoreFlags);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kNoFault);
    CHECK(cpu.intReg[12] == 1u);
    CHECK_FALSE(mem.lockMonitor().check(static_cast<int>(cpu.cpuSlot),
                                        0x700ULL));   // cleared regardless

    uint64_t back = 0;
    CHECK(mem.read8(0x700, back) == MemStatus::Ok);
    CHECK(back == 0xFEEDFACEull);
}

TEST_CASE("MemDrainer -- STQ_C fails when reservation cleared; writes 0; no publish")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);
    CHECK(mem.write8(0x800, 0xDEADBEEFull) == MemStatus::Ok);

    // No reservation held (fresh LockMonitor); make it explicit.
    mem.lockMonitor().clear(static_cast<int>(cpu.cpuSlot));

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x800,
                                    /*data*/0xCAFEBABEull,
                                    /*isStore*/true, /*Ra*/13,
                                    kLockedStoreFlags);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kNoFault);
    CHECK(cpu.intReg[13] == 0u);
    CHECK_FALSE(mem.lockMonitor().check(static_cast<int>(cpu.cpuSlot),
                                        0x800ULL));

    uint64_t back = 0;
    CHECK(mem.read8(0x800, back) == MemStatus::Ok);
    CHECK(back == 0xDEADBEEFull);   // unchanged; conditional store skipped
}

TEST_CASE("MemDrainer -- STQ_C fails when reservation tags a different cache line")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    // Reserve a different line than the one STQ_C targets.
    mem.lockMonitor().set(static_cast<int>(cpu.cpuSlot), 0x900ULL);

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x800,
                                    /*data*/0x1111ull,
                                    /*isStore*/true, /*Ra*/14,
                                    kLockedStoreFlags);

    drainBus(s, cpu, mem);

    CHECK(cpu.intReg[14] == 0u);
    // STx_C clears this CPU's reservation regardless of hit/miss.
    CHECK_FALSE(mem.lockMonitor().check(static_cast<int>(cpu.cpuSlot),
                                        0x900ULL));
}


// =============================================================================
// Fault paths
// =============================================================================

TEST_CASE("MemDrainer -- pre-existing faultCode aborts before drain work")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x100, /*data*/0,
                                    /*isStore*/false, /*Ra*/15, kPlainLoadFlags);
    s.result.faultCode = kFaultUnimplemented;
    s.result.regWriteValue = 0xDEAD;

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kFaultUnimplemented);
    // No regfile commit for a pre-faulted slot.
    CHECK(cpu.intReg[15] == 0u);
}

TEST_CASE("MemDrainer -- GuestMemory OOR populates kFaultBusError + mm_stat")
{
    CpuState cpu = palModeCpu();
    GuestMemory mem(4096);

    // Address well past the 4 KiB backing.
    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x10000, /*data*/0,
                                    /*isStore*/false, /*Ra*/16, kPlainLoadFlags);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kFaultBusError);
    CHECK(cpu.mm_stat == 0x10000ULL);
    CHECK(cpu.intReg[16] == 0u);   // commit suppressed on fault
}

TEST_CASE("MemDrainer -- unaligned LDQ raises kFaultUnaligned (trap enabled)")
{
    CpuState cpu = palModeCpu();
    cpu.unalignTrapEnabled = true;   // verify trap mechanism still fires when explicitly enabled
    GuestMemory mem(4096);

    // 2026-06-04: deliberately WITHOUT S_PhysAddr -- the alignment check
    // lives in translateDataAligned (Ev6Translator.h), which the
    // S_PhysAddr bypass skips entirely ("alignment is the caller's
    // responsibility" per the MemDrainer contract).  The check runs
    // BEFORE any DTB lookup, so the empty-DTB fixture still faults
    // Unaligned, never DtbMiss.
    constexpr GrainSem kUnalignedProbeFlags = GrainSem::S_MemFormat
                                            | GrainSem::S_Load
                                            | GrainSem::S_WritesRa
                                            | GrainSem::S_WritesInt;

    // Misaligned 8-byte access.
    PipelineSlot s = slotWithEffect(/*size*/8, /*addr*/0x101, /*data*/0,
                                    /*isStore*/false, /*Ra*/17,
                                    kUnalignedProbeFlags);

    drainBus(s, cpu, mem);

    CHECK(s.result.faultCode == kFaultUnaligned);
    // EV6 MM_STAT is a STATUS word (WR/ACV/FOR/FOW + opcode<<4), not the
    // faulting address -- the old `mm_stat == 0x101` expectation encoded
    // the pre-2026-06-03 VA-in-mm_stat bug.  This fixture's grain has
    // encoded=0 and the access is a load, so every status bit is clear.
    // The faulting VA lands in HW_VA (cpu.va) per HRM 5.x.
    CHECK(cpu.mm_stat == 0u);
    CHECK(cpu.va == 0x101ULL);
    CHECK(cpu.intReg[17] == 0u);
}
