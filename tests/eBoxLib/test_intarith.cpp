// ============================================================================
// tests/eBoxLib/test_intarith.cpp -- doctest cases for INTA leaves
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
// Doctest cases for the INTA dispatch arm (opcode 0x10) covering
// ADDL, SUBL, ADDQ, SUBQ, CMPEQ.  Each leaf is exercised in
// isolation: tests construct an InstructionGrain and ExecCtx
// directly and call the leaf, then assert the BoxResult shape.  The
// pipeline driver is not yet built; once it is, dispatch-through-
// table tests will join these per-leaf tests.
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
#include <ostream>          // doctest forward-declares basic_ostream;
                            // pull in the full type so any string_view
                            // comparison instantiations resolve.

using namespace coreLib;
using grainFactory::GrainSem;


namespace {

// Build an InstructionGrain shaped like an INTA (opcode 0x10) row.
// The caller specifies the 7-bit func, the three operand register
// fields, and the leaf function pointer.  Helps build grains for
// every INTA leaf without a per-leaf wrapper.
InstructionGrain makeIntaGrain(uint8_t func, uint8_t ra, uint8_t rb, uint8_t rc,
                               GrainFn execFn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{0x10} << 26)
                | (uint32_t{ra}   << 21)
                | (uint32_t{rb}   << 16)
                | (uint32_t{func} << 5)
                |  uint32_t{rc};
    g.primaryOp = 0x10;
    g.box       = Box::Ebox;
    g.semFlags  = GrainSem::S_OpFormat
                | GrainSem::S_ReadsRa
                | GrainSem::S_ReadsRb
                | GrainSem::S_ReadsInt
                | GrainSem::S_WritesRc
                | GrainSem::S_WritesInt;
    g.execFn    = execFn;
    return g;
}

} // anonymous namespace


// =============================================================================
// ADDL -- 32-bit integer add, sign-extended to 64 bits  (func 0x00)
// =============================================================================

TEST_CASE("eBox::execAddl -- small positive operands")
{
    InstructionGrain g = makeIntaGrain(0x00, 1, 2, 3, &eBox::execAddl);
    ExecCtx ctx{};
    ctx.opA = 7;
    ctx.opB = 5;

    BoxResult r = eBox::execAddl(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 3);
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(r.regWriteValue == 12u);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.divert);
}

TEST_CASE("eBox::execAddl -- result sign-extends from 32 to 64 bits")
{
    InstructionGrain g = makeIntaGrain(0x00, 1, 2, 5, &eBox::execAddl);
    ExecCtx ctx{};
    ctx.opA = 0xFFFFFFFEULL;
    ctx.opB = 0x00000001ULL;

    BoxResult r = eBox::execAddl(g, ctx);

    CHECK(r.regWriteIdx == 5);
    CHECK(r.regWriteValue == 0xFFFFFFFFFFFFFFFFULL);
}

TEST_CASE("eBox::execAddl -- 32-bit overflow wraps and sign-extends")
{
    InstructionGrain g = makeIntaGrain(0x00, 1, 2, 7, &eBox::execAddl);
    ExecCtx ctx{};
    ctx.opA = 0x7FFFFFFFULL;
    ctx.opB = 0x00000001ULL;

    BoxResult r = eBox::execAddl(g, ctx);

    CHECK(r.regWriteIdx == 7);
    CHECK(r.regWriteValue == 0xFFFFFFFF80000000ULL);
}

TEST_CASE("eBox::execAddl -- only the low 32 bits of operands enter the add")
{
    InstructionGrain g = makeIntaGrain(0x00, 1, 2, 9, &eBox::execAddl);
    ExecCtx ctx{};
    ctx.opA = 0xDEADBEEF00000003ULL;
    ctx.opB = 0xCAFEBABE00000004ULL;

    BoxResult r = eBox::execAddl(g, ctx);

    CHECK(r.regWriteIdx == 9);
    CHECK(r.regWriteValue == 7u);
}

TEST_CASE("eBox::execAddl -- semFlags propagate from grain to result")
{
    InstructionGrain g = makeIntaGrain(0x00, 1, 2, 3, &eBox::execAddl);
    ExecCtx ctx{};
    ctx.opA = 0;
    ctx.opB = 0;

    BoxResult r = eBox::execAddl(g, ctx);

    CHECK(any(r.semFlags & GrainSem::S_OpFormat));
    CHECK(any(r.semFlags & GrainSem::S_WritesRc));
    CHECK(any(r.semFlags & GrainSem::S_WritesInt));
    CHECK_FALSE(any(r.semFlags & GrainSem::S_Load));
    CHECK_FALSE(any(r.semFlags & GrainSem::S_ChangesPC));
}

TEST_CASE("eBox::execAddl -- reachable through grain.execFn")
{
    InstructionGrain g = makeIntaGrain(0x00, 1, 2, 11, &eBox::execAddl);
    ExecCtx ctx{};
    ctx.opA = 100;
    ctx.opB = 23;

    BoxResult r = (*g.execFn)(g, ctx);

    CHECK(r.regWriteIdx == 11);
    CHECK(r.regWriteValue == 123u);
}


// =============================================================================
// SUBL -- 32-bit integer subtract, sign-extended to 64 bits  (func 0x09)
// =============================================================================

TEST_CASE("eBox::execSubl -- basic positive subtraction")
{
    InstructionGrain g = makeIntaGrain(0x09, 1, 2, 4, &eBox::execSubl);
    ExecCtx ctx{};
    ctx.opA = 10;
    ctx.opB = 3;

    BoxResult r = eBox::execSubl(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 4);
    CHECK(r.regWriteValue == 7u);
}

TEST_CASE("eBox::execSubl -- underflow sign-extends to 64-bit all-ones")
{
    InstructionGrain g = makeIntaGrain(0x09, 1, 2, 4, &eBox::execSubl);
    ExecCtx ctx{};
    ctx.opA = 0;
    ctx.opB = 1;

    BoxResult r = eBox::execSubl(g, ctx);

    // 0 - 1 = -1 in 32 bits; sign-extended to 64 bits = all-ones.
    CHECK(r.regWriteValue == 0xFFFFFFFFFFFFFFFFULL);
}


// =============================================================================
// ADDQ -- 64-bit integer add  (func 0x20)
// =============================================================================

TEST_CASE("eBox::execAddq -- 64-bit positive add")
{
    InstructionGrain g = makeIntaGrain(0x20, 1, 2, 4, &eBox::execAddq);
    ExecCtx ctx{};
    ctx.opA = 0x1000000000ULL;
    ctx.opB = 0x0000000003ULL;

    BoxResult r = eBox::execAddq(g, ctx);

    CHECK(r.regWriteIdx == 4);
    CHECK(r.regWriteValue == 0x1000000003ULL);
}

TEST_CASE("eBox::execAddq -- 64-bit overflow wraps to zero")
{
    InstructionGrain g = makeIntaGrain(0x20, 1, 2, 4, &eBox::execAddq);
    ExecCtx ctx{};
    ctx.opA = 0xFFFFFFFFFFFFFFFFULL;
    ctx.opB = 0x0000000000000001ULL;

    BoxResult r = eBox::execAddq(g, ctx);

    // UINT64_MAX + 1 wraps to 0; no trap (the _V variant traps but
    // is not implemented in v1).
    CHECK(r.regWriteValue == 0u);
}


// =============================================================================
// SUBQ -- 64-bit integer subtract  (func 0x29)
// =============================================================================

TEST_CASE("eBox::execSubq -- basic 64-bit subtraction")
{
    InstructionGrain g = makeIntaGrain(0x29, 1, 2, 4, &eBox::execSubq);
    ExecCtx ctx{};
    ctx.opA = 0x1000000000ULL;
    ctx.opB = 0x0000000003ULL;

    BoxResult r = eBox::execSubq(g, ctx);

    CHECK(r.regWriteIdx == 4);
    CHECK(r.regWriteValue == 0xFFFFFFFFDULL);
}

TEST_CASE("eBox::execSubq -- 64-bit underflow wraps to UINT64_MAX")
{
    InstructionGrain g = makeIntaGrain(0x29, 1, 2, 4, &eBox::execSubq);
    ExecCtx ctx{};
    ctx.opA = 0;
    ctx.opB = 1;

    BoxResult r = eBox::execSubq(g, ctx);

    CHECK(r.regWriteValue == 0xFFFFFFFFFFFFFFFFULL);
}


// =============================================================================
// CMPEQ -- 64-bit compare equal  (func 0x2D)
// =============================================================================

TEST_CASE("eBox::execCmpeq -- equal operands return 1")
{
    InstructionGrain g = makeIntaGrain(0x2D, 1, 2, 4, &eBox::execCmpeq);
    ExecCtx ctx{};
    ctx.opA = 0x123456789ABCDEFULL;
    ctx.opB = 0x123456789ABCDEFULL;

    BoxResult r = eBox::execCmpeq(g, ctx);

    CHECK(r.regWriteIdx == 4);
    CHECK(r.regWriteValue == 1u);
}

TEST_CASE("eBox::execCmpeq -- unequal operands return 0")
{
    InstructionGrain g = makeIntaGrain(0x2D, 1, 2, 4, &eBox::execCmpeq);
    ExecCtx ctx{};
    ctx.opA = 5;
    ctx.opB = 7;

    BoxResult r = eBox::execCmpeq(g, ctx);

    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("eBox::execCmpeq -- full 64-bit comparison (high bits matter)")
{
    // Identical low 32 bits, different high 32 bits -> not equal.
    InstructionGrain g = makeIntaGrain(0x2D, 1, 2, 4, &eBox::execCmpeq);
    ExecCtx ctx{};
    ctx.opA = 0x1000000000000000ULL;
    ctx.opB = 0x2000000000000000ULL;

    BoxResult r = eBox::execCmpeq(g, ctx);

    CHECK(r.regWriteValue == 0u);
}


// =============================================================================
// RC / RS -- atomic Read-and-Clear / Read-and-Set on cpu.intrFlag
// (Misc-format opcode 0x18, function 0xE000 / 0xF000)
//
// Direct CpuState mutation: leaf reads cpu.intrFlag into Ra, then
// writes 0 (RC) or 1 (RS) back.  The MEM-drainer's "S_WritesRa
// without spurious commit" assertion is satisfied because both
// leaves carry S_WritesRa | S_WritesInt in their dispatch entry.
// =============================================================================

namespace {

InstructionGrain makeMiscGrain(uint16_t func, uint8_t ra, GrainFn execFn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{0x18} << 26)
                | (uint32_t{ra}   << 21)
                | uint32_t{func};
    g.primaryOp = 0x18;
    g.box       = Box::Ebox;
    g.semFlags  = GrainSem::S_MiscFormat
                | GrainSem::S_WritesRa
                | GrainSem::S_WritesInt
                | GrainSem::S_NoTrace;
    g.execFn    = execFn;
    return g;
}

} // anonymous namespace

TEST_CASE("eBox::execRc -- reads intrFlag into Ra and clears it")
{
    InstructionGrain g = makeMiscGrain(0xE000, /*ra*/ 5, &eBox::execRc);
    CpuState cpu{};
    cpu.intrFlag = 0x1ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = eBox::execRc(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 5);
    CHECK(r.regWriteValue == 0x1ULL);    // returned the prior value
    CHECK(cpu.intrFlag == 0u);            // and cleared the IPR
}

TEST_CASE("eBox::execRc -- preserves prior zero state and remains zero")
{
    InstructionGrain g = makeMiscGrain(0xE000, /*ra*/ 6, &eBox::execRc);
    CpuState cpu{};                       // intrFlag defaults to 0
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = eBox::execRc(g, ctx);

    CHECK(r.regWriteValue == 0u);
    CHECK(cpu.intrFlag == 0u);
}

TEST_CASE("eBox::execRc -- Ra=R31 suppresses commit, side effect still fires")
{
    // R31 is the architectural zero register -- regfile commit is
    // suppressed at MEM-drain (kNoRegWrite == 31).  The intrFlag
    // clear must still happen.
    InstructionGrain g = makeMiscGrain(0xE000, /*ra*/ 31, &eBox::execRc);
    CpuState cpu{};
    cpu.intrFlag = 0x1ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = eBox::execRc(g, ctx);

    CHECK(cpu.intrFlag == 0u);            // cleared regardless of Ra
    CHECK(r.regWriteIdx == 31);            // would suppress at MEM-drain
}

TEST_CASE("eBox::execRs -- reads intrFlag into Ra and sets it to 1")
{
    InstructionGrain g = makeMiscGrain(0xF000, /*ra*/ 7, &eBox::execRs);
    CpuState cpu{};
    cpu.intrFlag = 0x0ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = eBox::execRs(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 7);
    CHECK(r.regWriteValue == 0u);          // prior value
    CHECK(cpu.intrFlag == 1u);              // set to 1
}

TEST_CASE("eBox::execRs -- prior set value returns 1, stays 1")
{
    InstructionGrain g = makeMiscGrain(0xF000, /*ra*/ 8, &eBox::execRs);
    CpuState cpu{};
    cpu.intrFlag = 0x1ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    BoxResult r = eBox::execRs(g, ctx);

    CHECK(r.regWriteValue == 0x1ULL);
    CHECK(cpu.intrFlag == 1u);
}

TEST_CASE("eBox::execRc -> execRs -- atomic toggle round-trip")
{
    CpuState cpu{};
    cpu.intrFlag = 0x1ULL;
    ExecCtx ctx{};
    ctx.cpu = &cpu;

    // RC: returns 1, clears.
    InstructionGrain const gRc = makeMiscGrain(0xE000, /*ra*/ 1, &eBox::execRc);
    BoxResult const rRc = eBox::execRc(gRc, ctx);
    CHECK(rRc.regWriteValue == 0x1ULL);
    CHECK(cpu.intrFlag == 0u);

    // RS: returns 0 (now-cleared), sets to 1.
    InstructionGrain const gRs = makeMiscGrain(0xF000, /*ra*/ 2, &eBox::execRs);
    BoxResult const rRs = eBox::execRs(gRs, ctx);
    CHECK(rRs.regWriteValue == 0u);
    CHECK(cpu.intrFlag == 1u);
}
