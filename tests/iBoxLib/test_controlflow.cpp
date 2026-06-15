// ============================================================================
// tests/iBoxLib/test_controlflow.cpp -- doctest cases for iBox v1 leaves
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
// Doctest cases for the iBox dispatch arm: BR / BSR / BEQ / BNE /
// BLT / BGE (Bra-format direct branches, opcodes 0x30..0x3F) plus
// JMP / JSR / RET / JSR_COROUTINE (Jmp-format indirect, opcode
// 0x1A).  Each leaf is exercised in isolation with a hand-built
// grain and ExecCtx; tests assert on divert, divertTarget, and
// regWriteValue (the return PC for branches with link).
//
// Branch-target convention under test:
//
//   Bra:  divertTarget = grain.pc + 4 + (sext_21(disp) << 2)
//   Jmp:  divertTarget = c.opB & ~3
//
// All ten leaves carry S_WritesRa; an Ra of R31 trips kNoRegWrite
// and suppresses the regfile commit.  Tests cover R31 suppression
// at least once per family.
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "grainFactoryLib/generated/GrainsForward.h"

#include <cstdint>
#include <ostream>

#include "tests/doctest.h"

using namespace coreLib;
using grainFactory::GrainSem;


namespace {

// Build a Bra-format grain.  disp21 is the 21-bit signed longword
// displacement; the helper packs it into encoded[20:0] via masking.
InstructionGrain makeBraGrain(uint8_t op, uint8_t ra, int32_t disp21,
                              GrainSem flags, GrainFn fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{op} << 26)
                | (uint32_t{ra} << 21)
                | (static_cast<uint32_t>(disp21) & 0x1FFFFFu);
    g.primaryOp = op;
    g.box       = Box::Ibox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

// Build a Jmp-format grain (opcode 0x1A).  func 0..3 selects JMP,
// JSR, RET, JSR_COROUTINE respectively.
InstructionGrain makeJmpGrain(uint8_t ra, uint8_t rb, uint8_t func,
                              GrainSem flags, GrainFn fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{0x1A}   << 26)
                | (uint32_t{ra}     << 21)
                | (uint32_t{rb}     << 16)
                | (uint32_t{func}   << 14);
    g.primaryOp = 0x1A;
    g.box       = Box::Ibox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

constexpr GrainSem kUncondBraFlags = GrainSem::S_BraFormat
                                   | GrainSem::S_WritesRa
                                   | GrainSem::S_WritesInt
                                   | GrainSem::S_ChangesPC
                                   | GrainSem::S_Uncond
                                   | GrainSem::S_Branch
                                   | GrainSem::S_CallBased;

constexpr GrainSem kCondBraFlags   = GrainSem::S_BraFormat
                                   | GrainSem::S_ReadsRa
                                   | GrainSem::S_ReadsInt
                                   | GrainSem::S_ChangesPC
                                   | GrainSem::S_Branch;

constexpr GrainSem kJmpFlags       = GrainSem::S_JmpFormat
                                   | GrainSem::S_ReadsRb
                                   | GrainSem::S_ReadsInt
                                   | GrainSem::S_WritesRa
                                   | GrainSem::S_WritesInt
                                   | GrainSem::S_ChangesPC
                                   | GrainSem::S_Uncond
                                   | GrainSem::S_Indirect;

} // anonymous namespace


// =============================================================================
// BR -- unconditional branch with link
// =============================================================================

TEST_CASE("iBox::execBr -- forward 1 instruction")
{
    // disp = 1 (one longword forward) -> target = pc + 4 + 4 = pc + 8
    InstructionGrain g = makeBraGrain(0x30, 26, 1, kUncondBraFlags, &iBox::execBr);
    ExecCtx ctx{};

    BoxResult r = iBox::execBr(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x100008ULL);
    CHECK(r.regWriteIdx == 26);
    CHECK(r.regWriteValue == 0x100004ULL);
}

TEST_CASE("iBox::execBr -- backward to itself (disp = -1)")
{
    // disp = -1 -> target = pc + 4 - 4 = pc.  Infinite-loop case.
    InstructionGrain g = makeBraGrain(0x30, 26, -1, kUncondBraFlags, &iBox::execBr);
    ExecCtx ctx{};

    BoxResult r = iBox::execBr(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x100000ULL);
    CHECK(r.regWriteValue == 0x100004ULL);
}

TEST_CASE("iBox::execBr -- R31 destination suppresses link via kNoRegWrite")
{
    InstructionGrain g = makeBraGrain(0x30, 31, 4, kUncondBraFlags, &iBox::execBr);
    ExecCtx ctx{};

    BoxResult r = iBox::execBr(g, ctx);

    CHECK(r.divert);
    CHECK(r.regWriteIdx == kNoRegWrite);
}


// =============================================================================
// BSR -- branch to subroutine
// =============================================================================

TEST_CASE("iBox::execBsr -- positive disp links Ra to pc+4")
{
    InstructionGrain g = makeBraGrain(0x34, 26, 16, kUncondBraFlags, &iBox::execBsr);
    ExecCtx ctx{};

    BoxResult r = iBox::execBsr(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x100044ULL);   // pc + 4 + 16*4
    CHECK(r.regWriteIdx == 26);
    CHECK(r.regWriteValue == 0x100004ULL);
}


// =============================================================================
// Conditional branches: BEQ / BNE / BLT / BGE
// =============================================================================

TEST_CASE("iBox::execBeq -- taken when Ra == 0")
{
    InstructionGrain g = makeBraGrain(0x39, 5, 4, kCondBraFlags, &iBox::execBeq);
    ExecCtx ctx{};
    ctx.opA = 0;

    BoxResult r = iBox::execBeq(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x100014ULL);   // pc + 4 + 4*4
}

TEST_CASE("iBox::execBeq -- not taken when Ra != 0")
{
    InstructionGrain g = makeBraGrain(0x39, 5, 4, kCondBraFlags, &iBox::execBeq);
    ExecCtx ctx{};
    ctx.opA = 1;

    BoxResult r = iBox::execBeq(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBne -- taken when Ra != 0")
{
    InstructionGrain g = makeBraGrain(0x3D, 5, -4, kCondBraFlags, &iBox::execBne);
    ExecCtx ctx{};
    ctx.opA = 0xFFFFFFFFFFFFFFFFULL;

    BoxResult r = iBox::execBne(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0xFFFF4ULL);     // pc + 4 - 16
}

TEST_CASE("iBox::execBne -- not taken when Ra == 0")
{
    InstructionGrain g = makeBraGrain(0x3D, 5, 4, kCondBraFlags, &iBox::execBne);
    ExecCtx ctx{};
    ctx.opA = 0;

    BoxResult r = iBox::execBne(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBlt -- taken on signed-negative Ra")
{
    InstructionGrain g = makeBraGrain(0x3A, 5, 8, kCondBraFlags, &iBox::execBlt);
    ExecCtx ctx{};
    ctx.opA = static_cast<uint64_t>(-1LL);

    BoxResult r = iBox::execBlt(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBlt -- not taken on zero Ra")
{
    InstructionGrain g = makeBraGrain(0x3A, 5, 8, kCondBraFlags, &iBox::execBlt);
    ExecCtx ctx{};
    ctx.opA = 0;

    BoxResult r = iBox::execBlt(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBge -- taken on zero Ra (>= 0)")
{
    InstructionGrain g = makeBraGrain(0x3E, 5, 4, kCondBraFlags, &iBox::execBge);
    ExecCtx ctx{};
    ctx.opA = 0;

    BoxResult r = iBox::execBge(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBge -- not taken on signed-negative Ra")
{
    InstructionGrain g = makeBraGrain(0x3E, 5, 4, kCondBraFlags, &iBox::execBge);
    ExecCtx ctx{};
    ctx.opA = static_cast<uint64_t>(-1LL);

    BoxResult r = iBox::execBge(g, ctx);

    CHECK_FALSE(r.divert);
}


// =============================================================================
// Conditional branches: BLE / BGT / BLBC / BLBS
// =============================================================================

TEST_CASE("iBox::execBle -- taken on zero (Ra <= 0)")
{
    InstructionGrain g = makeBraGrain(0x3B, 5, 4, kCondBraFlags, &iBox::execBle);
    ExecCtx ctx{};
    ctx.opA = 0;

    BoxResult r = iBox::execBle(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBle -- taken on signed-negative")
{
    InstructionGrain g = makeBraGrain(0x3B, 5, 4, kCondBraFlags, &iBox::execBle);
    ExecCtx ctx{};
    ctx.opA = static_cast<uint64_t>(-1LL);

    BoxResult r = iBox::execBle(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBle -- not taken on positive Ra")
{
    InstructionGrain g = makeBraGrain(0x3B, 5, 4, kCondBraFlags, &iBox::execBle);
    ExecCtx ctx{};
    ctx.opA = 1;

    BoxResult r = iBox::execBle(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBgt -- taken on positive Ra")
{
    InstructionGrain g = makeBraGrain(0x3F, 5, 4, kCondBraFlags, &iBox::execBgt);
    ExecCtx ctx{};
    ctx.opA = 1;

    BoxResult r = iBox::execBgt(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBgt -- not taken on zero")
{
    InstructionGrain g = makeBraGrain(0x3F, 5, 4, kCondBraFlags, &iBox::execBgt);
    ExecCtx ctx{};
    ctx.opA = 0;

    BoxResult r = iBox::execBgt(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBgt -- not taken on signed-negative")
{
    InstructionGrain g = makeBraGrain(0x3F, 5, 4, kCondBraFlags, &iBox::execBgt);
    ExecCtx ctx{};
    ctx.opA = static_cast<uint64_t>(-1LL);

    BoxResult r = iBox::execBgt(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBlbc -- taken when low bit clear (even Ra)")
{
    InstructionGrain g = makeBraGrain(0x38, 5, 4, kCondBraFlags, &iBox::execBlbc);
    ExecCtx ctx{};
    ctx.opA = 0xFFFFFFFFFFFFFFFEULL;   // all bits set except bit 0

    BoxResult r = iBox::execBlbc(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBlbc -- not taken when low bit set (odd Ra)")
{
    InstructionGrain g = makeBraGrain(0x38, 5, 4, kCondBraFlags, &iBox::execBlbc);
    ExecCtx ctx{};
    ctx.opA = 1;

    BoxResult r = iBox::execBlbc(g, ctx);

    CHECK_FALSE(r.divert);
}

TEST_CASE("iBox::execBlbs -- taken when low bit set (odd Ra)")
{
    InstructionGrain g = makeBraGrain(0x3C, 5, 4, kCondBraFlags, &iBox::execBlbs);
    ExecCtx ctx{};
    ctx.opA = 0x12345679ULL;            // low bit set; high bits irrelevant

    BoxResult r = iBox::execBlbs(g, ctx);

    CHECK(r.divert);
}

TEST_CASE("iBox::execBlbs -- not taken when low bit clear (even Ra)")
{
    InstructionGrain g = makeBraGrain(0x3C, 5, 4, kCondBraFlags, &iBox::execBlbs);
    ExecCtx ctx{};
    ctx.opA = 0x12345678ULL;            // low bit clear

    BoxResult r = iBox::execBlbs(g, ctx);

    CHECK_FALSE(r.divert);
}


// =============================================================================
// JMP / JSR / RET / JSR_COROUTINE -- indirect via Rb
// =============================================================================

TEST_CASE("iBox::execJmp -- target masks low 2 bits of opB")
{
    InstructionGrain g = makeJmpGrain(31, 16, 0, kJmpFlags, &iBox::execJmp);
    ExecCtx ctx{};
    ctx.opB = 0x200007ULL;   // low 3 bits set; bottom 2 cleared by leaf

    BoxResult r = iBox::execJmp(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x200004ULL);
    CHECK(r.regWriteIdx == kNoRegWrite);     // Ra = R31
}

TEST_CASE("iBox::execJsr -- links Ra to pc+4, target from opB")
{
    InstructionGrain g = makeJmpGrain(26, 16,
                                       1,
                                       kJmpFlags | GrainSem::S_CallBased,
                                       &iBox::execJsr);
    ExecCtx ctx{};
    ctx.opB = 0x300000ULL;

    BoxResult r = iBox::execJsr(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x300000ULL);
    CHECK(r.regWriteIdx == 26);
    CHECK(r.regWriteValue == 0x100004ULL);
}

TEST_CASE("iBox::execRet -- target from opB; Ra typically R31")
{
    InstructionGrain g = makeJmpGrain(31, 26,
                                       2,
                                       kJmpFlags | GrainSem::S_RetBased,
                                       &iBox::execRet);
    ExecCtx ctx{};
    ctx.opB = 0x400008ULL;

    BoxResult r = iBox::execRet(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x400008ULL);
    CHECK(r.regWriteIdx == kNoRegWrite);
}

TEST_CASE("iBox::execJsrCoroutine -- both link and indirect")
{
    InstructionGrain g = makeJmpGrain(26, 27,
                                       3,
                                       kJmpFlags | GrainSem::S_CallBased | GrainSem::S_RetBased,
                                       &iBox::execJsrCoroutine);
    ExecCtx ctx{};
    ctx.opB = 0x500003ULL;

    BoxResult r = iBox::execJsrCoroutine(g, ctx);

    CHECK(r.divert);
    CHECK(r.divertTarget == 0x500000ULL);    // low 2 bits cleared
    CHECK(r.regWriteIdx == 26);
    CHECK(r.regWriteValue == 0x100004ULL);
}
