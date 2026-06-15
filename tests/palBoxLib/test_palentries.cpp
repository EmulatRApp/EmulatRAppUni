// ============================================================================
// tests/palBoxLib/test_palentries.cpp -- doctest cases for palBox v1 leaves
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
// Doctest cases for the palBox dispatch arm.  HALT is the only real
// body in the v1 wave (faultCode = kFaultHalt; pipeline driver
// intercepts at WB to terminate).  HW_MFPR / HW_MTPR / HW_REI /
// BPT_tru64 / BPT_vms / CHME_vms / CHMK_tru64 are stubbed at
// kFaultUnimplemented pending the CpuState and PalVectorTable
// prerequisites documented in PalEntries.cpp.  Tests verify the
// documented faultCode and semFlags propagation per leaf.
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "grainFactoryLib/generated/GrainsForward.h"

#include <cstdint>
#include <ostream>

using namespace coreLib;
using grainFactory::GrainSem;


namespace {

// Build a CALL_PAL grain (opcode 0x00).  func is the 26-bit PAL
// function code (e.g., HALT = 0x0).
InstructionGrain makePalGrain(uint32_t func, GrainSem flags, GrainFn fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{0x00} << 26) | (func & 0x03FFFFFFu);
    g.primaryOp = 0x00;
    g.box       = Box::PalBox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

// Build an HW_xxx grain (opcodes 0x19, 0x1D, 0x1E).  PALmode-only
// Hw-format encodings.  ra goes in encoded[25:21]; scbd (the raw
// 8-bit IPR selector) goes in encoded[15:8].  HW_REI ignores ra and
// scbd; HW_MFPR and HW_MTPR consume both.
InstructionGrain makeHwGrain(uint8_t op, uint8_t ra, uint8_t scbd,
                             GrainSem flags, GrainFn fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{op}   << 26)
                | (uint32_t{ra}   << 21)
                | (uint32_t{scbd} <<  8);
    g.primaryOp = op;
    g.box       = Box::PalBox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

void checkUnimplStub(BoxResult const& r)
{
    CHECK(r.faultCode == kFaultUnimplemented);
    CHECK(r.regWriteIdx == kNoRegWrite);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.divert);
}

} // anonymous namespace


// =============================================================================
// HALT -- the one real body.  faultCode = kFaultHalt.
// =============================================================================

TEST_CASE("palBox::execHalt -- raises kFaultHalt; no other effect")
{
    GrainSem const flags = GrainSem::S_PalFormat
                         | GrainSem::S_PalEntry
                         | GrainSem::S_ChangesPC
                         | GrainSem::S_Uncond
                         | GrainSem::S_PalTru64
                         | GrainSem::S_PalVms;
    InstructionGrain g = makePalGrain(0x0, flags, &palBox::execHalt);
    ExecCtx ctx{};

    BoxResult r = palBox::execHalt(g, ctx);

    CHECK(r.faultCode == kFaultHalt);
    CHECK(r.regWriteIdx == kNoRegWrite);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.divert);
    CHECK(static_cast<uint64_t>(r.semFlags) == static_cast<uint64_t>(flags));
}


// =============================================================================
// HW_MFPR / HW_MTPR -- IPR read / write via CpuState
// =============================================================================

namespace {

constexpr GrainSem kHwMfprFlags = GrainSem::S_HwFormat
                                | GrainSem::S_Privileged
                                | GrainSem::S_IprRead
                                | GrainSem::S_WritesRa
                                | GrainSem::S_WritesInt;

constexpr GrainSem kHwMtprFlags = GrainSem::S_HwFormat
                                | GrainSem::S_Privileged
                                | GrainSem::S_IprWrite
                                | GrainSem::S_ReadsRb   // HW_MTPR source is Rb per Alpha PAL macro
                                | GrainSem::S_ReadsInt;

// Raw scbd values per HW_IPR.h (enum value minus the 0x0100 namespace
// offset).  The leaf adds 0x0100 back when matching against HW_IPR.
constexpr uint8_t kScbdExcAddr = 0x06;   // -> HW_EXC_ADDR
constexpr uint8_t kScbdCm      = 0x09;   // -> HW_CM
constexpr uint8_t kScbdICtl    = 0x11;   // -> HW_I_CTL
constexpr uint8_t kScbdPctrCtl = 0x14;   // -> HW_PCTR_CTL (silent-zero stub)
constexpr uint8_t kScbdMmStat  = 0x27;   // -> HW_MM_STAT
constexpr uint8_t kScbdMCtl    = 0x28;   // -> HW_M_CTL
constexpr uint8_t kScbdCc      = 0xC0;   // -> HW_CC
constexpr uint8_t kScbdVaCtl   = 0xC4;   // -> HW_VA_CTL

// scbd 0x60 lands in the gap between PAL_TEMP_31 (raw scbd 0x5F) and
// the bank-1 DTB regs (raw scbd 0xA0..0xA5 / HW_IPR 0x01A0..0x01A5).
// Not in V1's HW_IPR enum -- guaranteed to hit the default arm.
constexpr uint8_t kScbdUnknown = 0x60;

} // anonymous namespace


TEST_CASE("palBox::execHwMfpr -- reads HW_CC from CpuState::cycleCount")
{
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 1, kScbdCc,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    cpu.cycleCount = 0xDEADBEEFULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 1);
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(r.regWriteValue == 0xDEADBEEFULL);
}

TEST_CASE("palBox::execHwMfpr -- reads HW_EXC_ADDR")
{
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 2, kScbdExcAddr,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    cpu.excAddr = 0x12345000ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.regWriteIdx == 2);
    CHECK(r.regWriteValue == 0x12345000ULL);
}

TEST_CASE("palBox::execHwMfpr -- reads HW_CM as integer mode bits")
{
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 3, kScbdCm,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    cpu.mode = Mode_Privilege::User;       // 3
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.regWriteValue == 3u);
}

TEST_CASE("palBox::execHwMfpr -- unknown selector raises kFaultUnimplemented")
{
    // scbd 0x60 lands in a gap of the V1 HW_IPR enum (between HW_PCTX
    // and the bank-1 DTB regs); guaranteed to hit the default arm.
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 4, kScbdUnknown,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.faultCode == kFaultUnimplemented);
    CHECK(r.regWriteIdx == kNoRegWrite);
}

TEST_CASE("palBox::execHwMfpr -- silent-stub IPR returns 0 with no fault")
{
    // HW_PCTR_CTL is in the V1 enum but unbacked in CpuState; the
    // leaf returns 0 to Ra silently rather than faulting.  Permissive
    // stub for IPRs PALcode reads-but-doesn't-care-about during boot.
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 9, kScbdPctrCtl,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 9);
    CHECK(r.regWriteValue == 0u);
}

// PAL_TEMP read/write tests live further down with the HW_MTPR cases
// (the 2026-05-10 PT-extension landed the iprSelector disambiguator
// for raw scbd 0x40..0x5F).


TEST_CASE("palBox::execHwMtpr HW_CC -- round-trips through HW_MFPR HW_CC")
{
    // The 2026-05-11 two-counter split made cpu.cycleCount the sim-only
    // pipeline counter and the architectural HW_CC a derived value
    // (cycleCount + ccOffset).  HW_MTPR HW_CC now writes ccOffset such
    // that a subsequent HW_MFPR HW_CC returns the written value.  Verify
    // that round-trip; checking ccOffset directly would be brittle to
    // future representation changes.
    //
    // HW_MTPR sources from Rb per Alpha PAL macro convention (Ra is
    // hardcoded to R31 by the macro).  The leaf reads c.opB; tests
    // bypass the register-file read and assign c.opB directly.
    CpuState cpu{};
    cpu.cycleCount = 0;  // explicit -- the test assumes the sim counter is zero

    // Write phase: HW_MTPR HW_CC <- 0xCAFEBABE
    InstructionGrain gMt = makeHwGrain(0x1D, /*ra*/ 31, kScbdCc,
                                       kHwMtprFlags, &palBox::execHwMtpr);
    ExecCtx ctxMt{};
    ctxMt.cpu = &cpu;
    ctxMt.opB = 0xCAFEBABEULL;
    BoxResult rMt = palBox::execHwMtpr(gMt, ctxMt);
    CHECK(rMt.faultCode == kNoFault);

    // cycleCount must NOT be clobbered by HW_MTPR HW_CC.
    CHECK(cpu.cycleCount == 0);

    // Read phase: HW_MFPR HW_CC -- expect the value we wrote back.
    InstructionGrain gMf = makeHwGrain(0x19, /*ra*/ 1, kScbdCc,
                                       kHwMfprFlags, &palBox::execHwMfpr);
    ExecCtx ctxMf{};
    ctxMf.cpu = &cpu;
    BoxResult rMf = palBox::execHwMfpr(gMf, ctxMf);
    CHECK(rMf.faultCode == kNoFault);
    CHECK(rMf.regWriteValue == 0xCAFEBABEULL);
}

TEST_CASE("palBox::execHwMtpr -- writes HW_VA_CTL into CpuState::va_ctl")
{
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdVaCtl,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0x2ULL;   // bit 1 set: 48-bit VA mode

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.va_ctl == 0x2ULL);
}

TEST_CASE("palBox::execHwMtpr -- writes HW_CM clamps to bottom 2 bits")
{
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdCm,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    cpu.mode = Mode_Privilege::Kernel;
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0xFFFFFFFFFFFFFFFCULL | 2ULL;   // bits[1:0] = 10b -> Supervisor

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.mode == Mode_Privilege::Supervisor);
}

TEST_CASE("palBox::execHwMtpr -- unknown selector raises kFaultUnimplemented")
{
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdUnknown,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0x1234ULL;

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kFaultUnimplemented);
}

TEST_CASE("palBox::execHwMtpr -- silent-stub IPR swallows write with no fault")
{
    // HW_PCTR_CTL: in the V1 enum but unbacked.  Permissive stub --
    // write is discarded, no fault.  Lets PALcode init progress past
    // perf-counter setup blocks without halting on write-and-forget
    // sequences.
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdPctrCtl,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0xDEADULL;

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == kNoRegWrite);   // MTPR never writes a regfile slot
}

// ----------------------------------------------------------------------------
// Regression tests for the 2026-05-10 palBase Ra/Rb fix.
// ----------------------------------------------------------------------------
// The bug: V4's execHwMtpr read c.opA (= R[Ra]) instead of c.opB (= R[Rb])
// as the IPR write value.  Because the PALcode assembler macro hardcodes
// Ra=R31 (zero) on every hw_mtpr emit, every HW_MTPR silently wrote 0 to
// its target IPR.  Cycle 4194406 of the 2026-05-09 overnight trace shows
// this concretely: hw_mtpr R30, HW_PAL_BASE with R30=0x600000 zeroed
// palBase instead of setting it.  Journal: journals/2026-05-10_palBase_anomaly.md.

TEST_CASE("palBox::execHwMtpr -- HW_PAL_BASE sources from Rb (regression: 2026-05-10)")
{
    // Encoding shape per PALcode macro: Ra=R31, Rb=source GPR.
    constexpr uint8_t kScbdPalBase = 0x10;   // -> HW_PAL_BASE
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdPalBase,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    cpu.palBase = 0xDEAD0000ULL;   // pre-existing value the bug would zero
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0x0000000000600000ULL;   // R30 source per the trace

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.palBase == 0x0000000000600000ULL);   // must NOT be zero
}

TEST_CASE("palBox::execHwMtpr -- HW_PAL_BASE rejects opA noise (regression: 2026-05-10)")
{
    // Belt-and-braces: when opA carries garbage (as it would in the
    // production encoding -- Ra is hardcoded R31 which the regfile
    // returns as 0, but in a future encoding it could be anything),
    // the leaf must ignore opA and read opB.
    constexpr uint8_t kScbdPalBase = 0x10;
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdPalBase,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opA = 0xBADCAFE0ULL;             // noise; must be ignored
    ctx.opB = 0x0000000000800000ULL;     // real source

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(cpu.palBase == 0x0000000000800000ULL);
}

// ----------------------------------------------------------------------------
// PAL_TEMP round-trip tests -- iprSelector now disambiguates the
// PAL_TEMP range (raw scbd 0x40..0x5F -> HW_PAL_TEMP_0..31), so PT
// writes/reads can be exercised directly.
// ----------------------------------------------------------------------------

TEST_CASE("palBox::execHwMtpr -- writes PT5 (raw scbd 0x45) into palTemp[5]")
{
    constexpr uint8_t kScbdPt5 = 0x45;
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdPt5,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0xAABBCCDDEEFF0011ULL;

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.palTemp[5] == 0xAABBCCDDEEFF0011ULL);
}

TEST_CASE("palBox::execHwMtpr -- writes PT31 (raw scbd 0x5F, EV5 extension) into palTemp[31]")
{
    // PT31 is EV5-vintage; observed in the 2026-05-09 trace at PC 0x12f88
    // (hw_mtpr Rx, 0x5F).  V4 provisions the full 32-entry range.
    constexpr uint8_t kScbdPt31 = 0x5F;
    InstructionGrain g = makeHwGrain(0x1D, /*ra*/ 31, kScbdPt31,
                                      kHwMtprFlags, &palBox::execHwMtpr);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;
    ctx.opB = 0x0123456789ABCDEFULL;

    BoxResult r = palBox::execHwMtpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.palTemp[31] == 0x0123456789ABCDEFULL);
}

TEST_CASE("palBox::execHwMfpr -- reads PT5 from palTemp[5]")
{
    constexpr uint8_t kScbdPt5 = 0x45;
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 12, kScbdPt5,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    cpu.palTemp[5] = 0xDEADBEEFCAFEBABEULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 12);
    CHECK(r.regWriteValue == 0xDEADBEEFCAFEBABEULL);
}

TEST_CASE("palBox::execHwMfpr -- reads PT31 (EV5 extension) from palTemp[31]")
{
    constexpr uint8_t kScbdPt31 = 0x5F;
    InstructionGrain g = makeHwGrain(0x19, /*ra*/ 13, kScbdPt31,
                                      kHwMfprFlags, &palBox::execHwMfpr);
    CpuState cpu{};
    cpu.palTemp[31] = 0xFEDCBA9876543210ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwMfpr(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 13);
    CHECK(r.regWriteValue == 0xFEDCBA9876543210ULL);
}

TEST_CASE("palBox::execHwMtpr / execHwMfpr -- PT round-trip across all 32 slots")
{
    // Write a unique value to every PT, then read each back and
    // verify.  Catches off-by-one in iprSelector / palTempIndex and
    // confirms palTemp[32] storage is fully addressable.
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    for (uint8_t i = 0; i < 32; ++i) {
        uint8_t const scbd = 0x40 + i;
        uint64_t const value = 0x10000ULL + i;
        InstructionGrain gw = makeHwGrain(0x1D, /*ra*/ 31, scbd,
                                          kHwMtprFlags, &palBox::execHwMtpr);
        ctx.opB = value;
        BoxResult rw = palBox::execHwMtpr(gw, ctx);
        CHECK(rw.faultCode == kNoFault);
    }
    for (uint8_t i = 0; i < 32; ++i) {
        uint8_t const scbd = 0x40 + i;
        uint64_t const expected = 0x10000ULL + i;
        InstructionGrain gr = makeHwGrain(0x19, /*ra*/ 1, scbd,
                                          kHwMfprFlags, &palBox::execHwMfpr);
        BoxResult rr = palBox::execHwMfpr(gr, ctx);
        CHECK(rr.faultCode == kNoFault);
        CHECK(rr.regWriteValue == expected);
    }
}


// =============================================================================
// HW_REI -- divert to EXC_ADDR and clear palMode
// =============================================================================

TEST_CASE("palBox::execHwRei -- STACKED form diverts to EXC_ADDR and clears palMode")
{
    // Bit 12 of the encoding selects STACKED (=1) vs REGISTER (=0).
    // STACKED reads cpu.excAddr (exception-return path used by PAL
    // exception handlers).  REGISTER reads Rb (PAL-subroutine-return
    // path used by the SRM bootstrap).  Helper makeHwGrain does
    // not set bit 12, so OR it in here to exercise the STACKED arm.
    InstructionGrain g = makeHwGrain(0x1E, /*ra*/ 0, /*scbd*/ 0,
                                      GrainSem::S_HwFormat
                                    | GrainSem::S_Privileged
                                    | GrainSem::S_PalExit
                                    | GrainSem::S_ChangesPC
                                    | GrainSem::S_Uncond
                                    | GrainSem::S_NoTrace,
                                    &palBox::execHwRei);
    g.encoded |= (1u << 12);   // STACKED form
    CpuState cpu{};
    cpu.excAddr = 0x12345678ULL;
    cpu.pc |= uint64_t{1};   // enter PAL (PALmode == PC<0>)

    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwRei(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.divert);
    CHECK(r.divertTarget == 0x12345678ULL);
    CHECK_FALSE(cpu.inPalMode());   // side effect: PAL mode cleared
}

TEST_CASE("palBox::execHwRei -- REGISTER form diverts to Rb and tracks palMode bit")
{
    // REGISTER form: bit 12 clear.  Resume PC comes from intReg[Rb].
    // Bit 0 of the resume PC sets PAL mode -- 1 stays in PAL, 0 exits.
    // Test the "stay in PAL" path: pack a target with bit 0 set.
    InstructionGrain g = makeHwGrain(0x1E, /*ra*/ 0, /*Rb*/ 5,
                                      GrainSem::S_HwFormat
                                    | GrainSem::S_Privileged
                                    | GrainSem::S_PalExit
                                    | GrainSem::S_ChangesPC
                                    | GrainSem::S_Uncond
                                    | GrainSem::S_NoTrace,
                                    &palBox::execHwRei);
    // makeHwGrain packs Rb at encoded[20:16] via the scbd argument
    // path?  Inspect: encoded = op<<26 | ra<<21 | scbd<<8.  scbd
    // lands at [15:8], not [20:16].  Pack Rb at [20:16] manually.
    g.encoded = (0x1Eu << 26) | (0u << 21) | (5u << 16);  // bit 12 = 0
    CpuState cpu{};
    cpu.intReg[5] = 0x600100ULL | 0x1ULL;   // resume in PAL
    cpu.pc       &= ~uint64_t{1};            // pre-set non-PAL (PALmode == PC<0>)

    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execHwRei(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.divert);
    CHECK(r.divertTarget == 0x600101ULL);    // bit 0 KEPT: mode rides in PC<0>
    CHECK(cpu.inPalMode());                   // low bit -> PAL mode
}


// =============================================================================
// BPT / CHME / CHMK -- stubs pending PalVectorTable
// =============================================================================

TEST_CASE("palBox::execBpt_tru64 -- stub raises kFaultUnimplemented")
{
    GrainSem const flags = GrainSem::S_PalFormat
                         | GrainSem::S_PalEntry
                         | GrainSem::S_ChangesPC
                         | GrainSem::S_Uncond
                         | GrainSem::S_PalTru64;
    InstructionGrain g = makePalGrain(0x80, flags, &palBox::execBpt_tru64);
    ExecCtx ctx{};

    BoxResult r = palBox::execBpt_tru64(g, ctx);

    checkUnimplStub(r);
}

TEST_CASE("palBox::execBpt_vms -- stub raises kFaultUnimplemented")
{
    GrainSem const flags = GrainSem::S_PalFormat
                         | GrainSem::S_PalEntry
                         | GrainSem::S_ChangesPC
                         | GrainSem::S_Uncond
                         | GrainSem::S_PalVms;
    InstructionGrain g = makePalGrain(0x80, flags, &palBox::execBpt_vms);
    ExecCtx ctx{};

    BoxResult r = palBox::execBpt_vms(g, ctx);

    checkUnimplStub(r);
}

// 2026-06-03: modernized from the kFaultUnimplemented stub expectation.
// execChme_vms delegates to execCallPalDispatch since the 87-leaf bulk
// conversion (2026-05-27), which dereferences ctx.cpu -- the old stub
// test fed a null ExecCtx and SIGSEGV'd.  Now exercises the real
// contract: CHME (CALL_PAL 0x82, unprivileged) diverts to the EV6
// entry vector palBase | 0x3000 | ((0x82 & 0x3F) << 6) = palBase +
// 0x3080 (HRM Section 6.8.1), raises PALmode (PC<0>=1 on the divert
// target), and stores the linkage value (return PC | caller palMode)
// in EXC_ADDR and the linkage register (R27 with I_CTL bit 20 clear).
TEST_CASE("palBox::execChme_vms -- diverts to CALL_PAL entry palBase+0x3080")
{
    GrainSem const flags = GrainSem::S_PalFormat
                         | GrainSem::S_PalEntry
                         | GrainSem::S_ChangesPC
                         | GrainSem::S_Uncond
                         | GrainSem::S_PalVms;
    InstructionGrain g = makePalGrain(0x82, flags, &palBox::execChme_vms);

    CpuState cpu{};
    cpu.palBase = 0x8000;                     // DS10 OS-PAL base (32K aligned)
    cpu.pc      = g.pc;                       // caller in native mode (PC<0>=0)
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execChme_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.divert);
    // 0x8000 | 0x3000 | (0x02 << 6) = 0xB080; bit 0 set = enter PAL.
    CHECK(r.divertTarget == 0xB081ULL);
    CHECK(cpu.inPalMode());                   // palModeEnter fired
    // Linkage = return PC (g.pc + 4) with caller palMode (0) in bit 0,
    // mirrored in EXC_ADDR and the I_CTL-selected linkage register
    // (i_ctl = 0 -> CALL_PAL_R23 clear -> R27).
    CHECK(cpu.excAddr == 0x100004ULL);
    CHECK(cpu.intReg[27] == 0x100004ULL);
}

TEST_CASE("palBox::execChmk_tru64 -- stub raises kFaultUnimplemented")
{
    GrainSem const flags = GrainSem::S_PalFormat
                         | GrainSem::S_PalEntry
                         | GrainSem::S_ChangesPC
                         | GrainSem::S_Uncond
                         | GrainSem::S_PalTru64;
    InstructionGrain g = makePalGrain(0x83, flags, &palBox::execChmk_tru64);
    ExecCtx ctx{};

    BoxResult r = palBox::execChmk_tru64(g, ctx);

    checkUnimplStub(r);
}


// =============================================================================
// CSERVE -- inline-executed CALL_PAL function (S_PalIntrinsic).  No
// PAL transfer; the leaf reads R16 directly from CpuState, computes
// the result, and packs an R0 commit into BoxResult.  Pipeline
// retires to PC+4 because divert stays false.
// =============================================================================

namespace {

constexpr GrainSem kCserveFlags = GrainSem::S_PalFormat
                                | GrainSem::S_PalIntrinsic
                                | GrainSem::S_WritesRa
                                | GrainSem::S_WritesInt
                                | GrainSem::S_PalTru64
                                | GrainSem::S_PalVms;

} // anonymous namespace

TEST_CASE("palBox::execCserve -- function 0x44 (MTPR_EXC_ADDR) diverts to R17, no R0 write")
{
    // VMS PAL personality: CSERVE 0x44 is CSERVE$MTPR_EXC_ADDR, the
    // huf_decom switch: console hand-off (ev6_huf_decom.m64 l.308-311).
    // It loads EXC_ADDR = R17 and diverts the pipeline to R17 (bit 0 = 1
    // keeps PALmode), same shape as execHwRei register form.  It does
    // NOT write R0; the old "WRITE_PATTERN returns 0 in R0" contract was
    // V1's retired Namespace-5 fiction (see PalEntries.cpp l.989-996).
    InstructionGrain g = makePalGrain(0x09, kCserveFlags, &palBox::execCserve);
    CpuState cpu{};
    cpu.intReg[16] = 0x44;
    cpu.intReg[17] = 0x445f040501ad;          // PA of continuation | PAL bit
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCserve(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.divert);                          // PAL->PAL control transfer
    CHECK(r.divertTarget == 0x445f040501adull);
    CHECK(r.regWriteIdx == kNoRegWrite);      // R0 untouched (31)
    CHECK(cpu.excAddr == 0x445f040501adull);  // MTPR EXC_ADDR side effect
}

TEST_CASE("palBox::execCserve -- function 0x65 (MP_WORK_REQUEST) tolerated no-op; R0 untouched, no divert")
{
    // VMS PAL personality: CSERVE 0x65 is MP_WORK_REQUEST.  On the V4
    // uniprocessor model it has no secondary CPU to signal, so it falls
    // through to the tolerant default: R0 untouched (regWriteIdx ==
    // kNoRegWrite), no fault, no divert.  The old "returns 0 in R0"
    // contract was V1's retired Namespace-5 Bcache-init fiction.
    InstructionGrain g = makePalGrain(0x09, kCserveFlags, &palBox::execCserve);
    CpuState cpu{};
    cpu.intReg[16] = 0x65;            // CSERVE function code in R16
    cpu.intReg[17] = 0;
    cpu.intReg[18] = 1;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCserve(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == kNoRegWrite);   // R0 untouched (31)
    CHECK_FALSE(r.divert);                 // intrinsic: pipeline advances PC+4
    CHECK(r.memSize == kNoMemEffect);
}

TEST_CASE("palBox::execCserve -- function code in low 8 bits only")
{
    // Upper bits of R16 are reserved; the leaf must mask to bits[7:0]
    // when extracting the function code.  Pack 0x1234567800000065 to
    // verify the mask.
    InstructionGrain g = makePalGrain(0x09, kCserveFlags, &palBox::execCserve);
    CpuState cpu{};
    cpu.intReg[16] = 0x1234567800000065ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCserve(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("palBox::execCserve -- unhandled function is tolerated (no-op, no fault)")
{
    // Real-hardware fidelity: an unrecognized CSERVE function code is
    // EXPECTED and TOLERATED, not fatal (see PalEntries.cpp l.1129-1138).
    // V4 previously raised kFaultUnimplemented -- an artificial fatality
    // that diverged from silicon.  The default path now no-ops: R0 left
    // untouched, kNoFault, no divert.  The diagnostic "CSERVE Defaulted"
    // line still names the function for trace lookback.  Function 0x01
    // (GETC) is console I/O via the UART + SRM callback ABI, not a CSERVE
    // service, so it correctly reaches the tolerant default here.
    InstructionGrain g = makePalGrain(0x09, kCserveFlags, &palBox::execCserve);
    CpuState cpu{};
    cpu.intReg[16] = 0x01;            // GETC -- not a CSERVE service
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCserve(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == kNoRegWrite);   // R0 untouched (31)
    CHECK_FALSE(r.divert);
}


// =============================================================================
// CALL_PAL LDQP / STQP intrinsics (physical-address memEffect)
// =============================================================================
//
// LDQP / STQP set up a physical-address memEffect on BoxResult; the
// MEM-drainer applies the access and commits R0 (LDQP only).  These
// tests exercise the leaf body in isolation -- they verify that
// regWriteIdx / memAddr / memSize / S_PhysAddr / S_Load / S_Store
// land correctly in BoxResult; the actual memory access is the
// drainer's responsibility (covered in test_memdrainer.cpp).
// =============================================================================

TEST_CASE("palBox::execLdqp_vms -- packs phys-load memEffect targeting R0")
{
    constexpr GrainSem kLdqpFlags = GrainSem::S_PalFormat
                                  | GrainSem::S_PalIntrinsic
                                  | GrainSem::S_WritesRa
                                  | GrainSem::S_WritesInt
                                  | GrainSem::S_PalTru64
                                  | GrainSem::S_PalVms;

    InstructionGrain g = makePalGrain(0x03, kLdqpFlags, &palBox::execLdqp_vms);
    CpuState cpu{};
    cpu.intReg[16] = 0x4560ULL;   // physical address argument (R16 = a0)
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execLdqp_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.memAddr == 0x4560ULL);
    CHECK(r.memSize == 8);
    CHECK_FALSE(r.memIsStore);
    CHECK(r.regWriteIdx == 0);
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(grainFactory::has(r.semFlags, GrainSem::S_PhysAddr));
    CHECK(grainFactory::has(r.semFlags, GrainSem::S_Load));
}

TEST_CASE("palBox::execStqp_vms -- packs phys-store memEffect from R17")
{
    constexpr GrainSem kStqpFlags = GrainSem::S_PalFormat
                                  | GrainSem::S_PalIntrinsic
                                  | GrainSem::S_PalTru64
                                  | GrainSem::S_PalVms;

    InstructionGrain g = makePalGrain(0x04, kStqpFlags, &palBox::execStqp_vms);
    CpuState cpu{};
    cpu.intReg[16] = 0x4560ULL;            // R16 -- physical address (8-aligned)
    cpu.intReg[17] = 0xDEADBEEFCAFEBABEULL; // R17 -- value
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execStqp_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.memAddr == 0x4560ULL);
    CHECK(r.memData == 0xDEADBEEFCAFEBABEULL);
    CHECK(r.memSize == 8);
    CHECK(r.memIsStore);
    CHECK(r.regWriteIdx == kNoRegWrite);   // STQP doesn't commit a regfile slot
    CHECK(grainFactory::has(r.semFlags, GrainSem::S_PhysAddr));
    CHECK(grainFactory::has(r.semFlags, GrainSem::S_Store));
}

TEST_CASE("palBox::execLdqp_vms -- unaligned R16 raises kFaultUnaligned")
{
    constexpr GrainSem kLdqpFlags = GrainSem::S_PalFormat
                                  | GrainSem::S_PalIntrinsic
                                  | GrainSem::S_WritesRa
                                  | GrainSem::S_WritesInt
                                  | GrainSem::S_PalTru64
                                  | GrainSem::S_PalVms;

    InstructionGrain g = makePalGrain(0x03, kLdqpFlags, &palBox::execLdqp_vms);
    CpuState cpu{};
    cpu.unalignTrapEnabled = true;   // verify the trap mechanism still fires when enabled
    cpu.intReg[16] = 0x4567ULL;   // R16<2:0> = 7 -- not quadword aligned
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execLdqp_vms(g, ctx);

    CHECK(r.faultCode == kFaultUnaligned);
    CHECK(r.memSize == kNoMemEffect);          // no memEffect packed
    CHECK(r.regWriteIdx == kNoRegWrite);       // no regfile commit
    CHECK(cpu.mm_stat == 0x4567ULL);           // faulting EA captured
}

TEST_CASE("palBox::execStqp_vms -- unaligned R16 raises kFaultUnaligned")
{
    constexpr GrainSem kStqpFlags = GrainSem::S_PalFormat
                                  | GrainSem::S_PalIntrinsic
                                  | GrainSem::S_PalTru64
                                  | GrainSem::S_PalVms;

    InstructionGrain g = makePalGrain(0x04, kStqpFlags, &palBox::execStqp_vms);
    CpuState cpu{};
    cpu.unalignTrapEnabled = true;   // verify the trap mechanism still fires when enabled
    cpu.intReg[16] = 0x4561ULL;   // R16<0> = 1 -- not aligned at all
    cpu.intReg[17] = 0xCAFEULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execStqp_vms(g, ctx);

    CHECK(r.faultCode == kFaultUnaligned);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.memIsStore);                 // no store published
    CHECK(cpu.mm_stat == 0x4561ULL);
}

TEST_CASE("palBox::execLdqp_vms -- 8-byte aligned address passes alignment check")
{
    constexpr GrainSem kLdqpFlags = GrainSem::S_PalFormat
                                  | GrainSem::S_PalIntrinsic
                                  | GrainSem::S_WritesRa
                                  | GrainSem::S_WritesInt
                                  | GrainSem::S_PalTru64
                                  | GrainSem::S_PalVms;

    // Boundary: 0x0 (aligned), 0x8 (aligned), 0x10 (aligned).
    // Already covered: 0x4560 (aligned -- bottom 3 bits = 0).  Cover
    // 0x0 explicitly -- common case for low-PA boot tables.
    InstructionGrain g = makePalGrain(0x03, kLdqpFlags, &palBox::execLdqp_vms);
    CpuState cpu{};
    cpu.intReg[16] = 0x0ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execLdqp_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.memAddr == 0x0ULL);
    CHECK(r.memSize == 8);
}


// =============================================================================
// CALL_PAL VPTB intrinsics
// =============================================================================

namespace {

constexpr GrainSem kVptbReadFlags  = GrainSem::S_PalFormat
                                   | GrainSem::S_PalIntrinsic
                                   | GrainSem::S_WritesRa
                                   | GrainSem::S_WritesInt
                                   | GrainSem::S_PalTru64
                                   | GrainSem::S_PalVms;

constexpr GrainSem kVptbWriteFlags = GrainSem::S_PalFormat
                                   | GrainSem::S_PalIntrinsic
                                   | GrainSem::S_PalTru64
                                   | GrainSem::S_PalVms;

} // anonymous namespace

TEST_CASE("palBox::execMfprVptb_vms -- returns cpu.vptb in R0; no fault, no divert")
{
    InstructionGrain g = makePalGrain(0x29, kVptbReadFlags, &palBox::execMfprVptb_vms);
    CpuState cpu{};
    cpu.vptb = 0xFFFFFFC000000000ULL;   // canonical sign-extended VA
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execMfprVptb_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 0);          // R0 (v0)
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(r.regWriteValue == 0xFFFFFFC000000000ULL);
    CHECK_FALSE(r.divert);
}

TEST_CASE("palBox::execMfprVptb_vms -- returns 0 when cpu.vptb is unset")
{
    InstructionGrain g = makePalGrain(0x29, kVptbReadFlags, &palBox::execMfprVptb_vms);
    CpuState cpu{};                      // vptb defaults to 0
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execMfprVptb_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("palBox::execMtprVptb_vms -- stores R16 into cpu.vptb")
{
    InstructionGrain g = makePalGrain(0x2A, kVptbWriteFlags, &palBox::execMtprVptb_vms);
    CpuState cpu{};
    cpu.intReg[16] = 0x1234567800000000ULL;   // R16 (a0) is the standard CALL_PAL arg
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execMtprVptb_vms(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == kNoRegWrite);     // MTPR doesn't commit a regfile slot
    CHECK_FALSE(r.divert);
    CHECK(cpu.vptb == 0x1234567800000000ULL);
}

TEST_CASE("palBox::execMtprVptb_vms -> execMfprVptb_vms -- round-trips via cpu.vptb")
{
    CpuState cpu{};
    cpu.intReg[16] = 0xDEADBEEFCAFEBABEULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    InstructionGrain const wg = makePalGrain(0x2A, kVptbWriteFlags, &palBox::execMtprVptb_vms);
    palBox::execMtprVptb_vms(wg, ctx);

    InstructionGrain const rg = makePalGrain(0x29, kVptbReadFlags, &palBox::execMfprVptb_vms);
    BoxResult r = palBox::execMfprVptb_vms(rg, ctx);

    CHECK(r.regWriteValue == 0xDEADBEEFCAFEBABEULL);
}


// =============================================================================
// CALL_PAL WTINT intrinsic (function 0x3E) -- wait for interrupt
// =============================================================================

namespace {

constexpr GrainSem kIntrinsicReadFlags = GrainSem::S_PalFormat
                                       | GrainSem::S_PalIntrinsic
                                       | GrainSem::S_WritesRa
                                       | GrainSem::S_WritesInt
                                       | GrainSem::S_PalTru64
                                       | GrainSem::S_PalVms;

} // anonymous namespace

TEST_CASE("palBox::execWtint -- returns 0 in R0; no fault, no divert")
{
    InstructionGrain g = makePalGrain(0x3E, kIntrinsicReadFlags, &palBox::execWtint);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execWtint(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 0);          // R0 (v0)
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(r.regWriteValue == 0u);       // "interrupt arrived"
    CHECK_FALSE(r.divert);
    CHECK(r.memSize == kNoMemEffect);
}


// =============================================================================
// CALL_PAL MFPR_WHAMI intrinsic (function 0x3F) -- single-CPU returns 0
// =============================================================================

TEST_CASE("palBox::execMfprWhami -- returns 0 in R0 (single-CPU)")
{
    InstructionGrain g = makePalGrain(0x3F, kIntrinsicReadFlags, &palBox::execMfprWhami);
    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execMfprWhami(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 0);          // R0 (v0)
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(r.regWriteValue == 0u);       // CPU 0 in V4's single-CPU model
    CHECK_FALSE(r.divert);
}


// =============================================================================
// CALL_PAL generic dispatch -- divert into PALcode at palBase + vector
//
// Mirrors V1 coreLib/global_registermaster_hot.h computeExceptionVector
// and the EV6 HRM 6.8.1 CALL_PAL entry-vector formula.
// =============================================================================

namespace {

constexpr GrainSem kCallPalDispatchFlagsTru64 = GrainSem::S_PalFormat
                                              | GrainSem::S_PalEntry
                                              | GrainSem::S_ChangesPC
                                              | GrainSem::S_Uncond
                                              | GrainSem::S_PalTru64;

} // anonymous namespace

TEST_CASE("palBox::execCallPalDispatch -- privileged func 0x29 (MFPR_VPTB) computes 0x602A40")
{
    // Privileged formula: entry = (palBase & ~0x7FFF) | 0x2000 | (func << 6)
    // For palBase=0x600000, func=0x29: 0x600000 | 0x2000 | (0x29<<6)
    //                               = 0x600000 | 0x2000 | 0xA40
    //                               = 0x602A40
    // (Matches V1's CallPal_29 = 0x2A40 + palBase.)
    InstructionGrain g = makePalGrain(0x29, kCallPalDispatchFlagsTru64,
                                       &palBox::execCallPalDispatch);
    g.pc = 0x100;                      // PC of the CALL_PAL instruction
    CpuState cpu{};
    cpu.palBase = 0x600000ULL;
    cpu.pc &= ~uint64_t{1};   // prior mode native (PALmode == PC<0>)
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCallPalDispatch(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.divert);
    CHECK(r.divertTarget == 0x602A41ULL);  // entry PC | PAL bit (PC<0> set)
    CHECK(cpu.inPalMode());                     // entered PAL mode
    CHECK(cpu.excAddr == 0x104ULL);         // pc + 4, bit 0 = 0 (was not in PAL)
}

TEST_CASE("palBox::execCallPalDispatch -- unprivileged func 0x86 (IMB) hits 0x3000 base")
{
    // Unprivileged formula: entry = (palBase & ~0x7FFF) | 0x3000 | ((func & 0x3F) << 6)
    // For palBase=0x600000, func=0x86: 0x600000 | 0x3000 | ((0x86 & 0x3F) << 6)
    //                               = 0x600000 | 0x3000 | (6 << 6)
    //                               = 0x603180
    // (Matches V1's CallPal_46 = 0x3180 -- IMB.)
    InstructionGrain g = makePalGrain(0x86, kCallPalDispatchFlagsTru64,
                                       &palBox::execCallPalDispatch);
    g.pc = 0x200;
    CpuState cpu{};
    cpu.palBase = 0x600000ULL;
    cpu.pc &= ~uint64_t{1};   // prior mode native (PALmode == PC<0>)
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCallPalDispatch(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x603181ULL);
    CHECK(cpu.inPalMode());
}

TEST_CASE("palBox::execCallPalDispatch -- excAddr captures prior PALmode in bit 0")
{
    // Nested CALL_PAL from within PALcode: caller is already in PAL
    // mode.  Bit 0 of excAddr must be 1 so HW_REI's STACKED form
    // restores PALmode = true on return.
    InstructionGrain g = makePalGrain(0x29, kCallPalDispatchFlagsTru64,
                                       &palBox::execCallPalDispatch);
    g.pc = 0x600100;                   // already in PAL region
    CpuState cpu{};
    cpu.palBase = 0x600000ULL;
    cpu.pc |= uint64_t{1};             // we were in PAL when the CALL_PAL fired
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCallPalDispatch(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x602A41ULL);
    CHECK(cpu.inPalMode());                          // still in PAL
    CHECK(cpu.excAddr == (0x600104ULL | 1ULL));  // pc+4 with prior PAL bit
}

TEST_CASE("palBox::execCallPalDispatch -- preserves palBase high bits above 32K window")
{
    // Real palBase placement is 32K-aligned but high bits matter -- the
    // formula is (palBase & ~0x7FFF), so a palBase like 0x4_0060_0000
    // should keep its high bits.
    InstructionGrain g = makePalGrain(0x00, kCallPalDispatchFlagsTru64,
                                       &palBox::execCallPalDispatch);
    g.pc = 0;
    CpuState cpu{};
    cpu.palBase = 0x4000060000ULL;     // arbitrary high-bits palBase
    cpu.pc &= ~uint64_t{1};   // prior mode native (PALmode == PC<0>)
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = palBox::execCallPalDispatch(g, ctx);

    // entry = (0x4000060000 & ~0x7FFF) | 0x2000 = 0x4000060000 | 0x2000
    //       = 0x4000062000
    CHECK(r.divertTarget == 0x4000062001ULL);
}
