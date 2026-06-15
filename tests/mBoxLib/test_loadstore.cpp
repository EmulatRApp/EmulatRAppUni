// ============================================================================
// tests/mBoxLib/test_loadstore.cpp -- doctest cases for mBox v1 leaves
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
// Doctest cases for the mBox dispatch arm (Mem-format opcodes 0x08-
// 0x2F plus FETCH at 0x18 / 0x8000).  Each leaf is exercised in
// isolation: tests construct an InstructionGrain and ExecCtx
// directly and call the leaf, then assert the BoxResult shape.
// Translation, GuestMemory access, and fault delivery are MEM-stage
// drainer concerns and are not exercised here -- these tests verify
// the leaf packs the right memEffect / regEffect for the drainer to
// consume.
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

// Build a Mem-format grain.  disp is the 16-bit signed displacement;
// callers pass its int16_t value directly and the helper packs it into
// encoded[15:0].
InstructionGrain makeMemGrain(uint8_t op, uint8_t ra, uint8_t rb,
                              int16_t disp, GrainSem flags, GrainFn fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{op} << 26)
                | (uint32_t{ra} << 21)
                | (uint32_t{rb} << 16)
                | (static_cast<uint32_t>(static_cast<uint16_t>(disp)) & 0xFFFFu);
    g.primaryOp = op;
    g.box       = Box::Mbox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

// Build a Misc-format grain for FETCH (opcode 0x18, func 0x8000).
InstructionGrain makeFetchGrain()
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{0x18} << 26) | uint32_t{0x8000};
    g.primaryOp = 0x18;
    g.box       = Box::Mbox;
    g.semFlags  = GrainSem::S_MiscFormat
                | GrainSem::S_ReadsRb
                | GrainSem::S_ReadsInt
                | GrainSem::S_PrefetchOnly;
    g.execFn    = &mBox::execFetch;
    return g;
}

constexpr GrainSem kLoadFlags  = GrainSem::S_MemFormat
                               | GrainSem::S_ReadsRb
                               | GrainSem::S_WritesRa
                               | GrainSem::S_ReadsInt
                               | GrainSem::S_WritesInt
                               | GrainSem::S_Load
                               | GrainSem::S_Cacheable;

constexpr GrainSem kStoreFlags = GrainSem::S_MemFormat
                               | GrainSem::S_ReadsRa
                               | GrainSem::S_ReadsRb
                               | GrainSem::S_ReadsInt
                               | GrainSem::S_Store
                               | GrainSem::S_Cacheable;

constexpr GrainSem kLdaFlags   = GrainSem::S_MemFormat
                               | GrainSem::S_ReadsRb
                               | GrainSem::S_WritesRa
                               | GrainSem::S_ReadsInt
                               | GrainSem::S_WritesInt;

constexpr GrainSem kLockedLoadFlags  = kLoadFlags | GrainSem::S_Locked;
constexpr GrainSem kLockedStoreFlags = GrainSem::S_MemFormat
                                     | GrainSem::S_ReadsRa
                                     | GrainSem::S_ReadsRb
                                     | GrainSem::S_ReadsInt
                                     | GrainSem::S_WritesRa
                                     | GrainSem::S_WritesInt
                                     | GrainSem::S_Store
                                     | GrainSem::S_Locked
                                     | GrainSem::S_Cacheable;

} // anonymous namespace


// =============================================================================
// LDA -- compute Ra <- Rb + sext(disp); no memory access
// =============================================================================

TEST_CASE("mBox::execLda -- positive displacement")
{
    InstructionGrain g = makeMemGrain(0x08, 1, 2, 0x0010, kLdaFlags, &mBox::execLda);
    ExecCtx ctx{};
    ctx.opB = 0x1000;

    BoxResult r = mBox::execLda(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == 1);
    CHECK_FALSE(r.regWriteIsFp);
    CHECK(r.regWriteValue == 0x1010ULL);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.divert);
}

TEST_CASE("mBox::execLda -- negative displacement sign-extends")
{
    InstructionGrain g = makeMemGrain(0x08, 3, 4, -16, kLdaFlags, &mBox::execLda);
    ExecCtx ctx{};
    ctx.opB = 0x1000;

    BoxResult r = mBox::execLda(g, ctx);

    CHECK(r.regWriteIdx == 3);
    CHECK(r.regWriteValue == 0x0FF0ULL);
}

TEST_CASE("mBox::execLda -- R31 destination is just kNoRegWrite")
{
    InstructionGrain g = makeMemGrain(0x08, 31, 4, 0x0008, kLdaFlags, &mBox::execLda);
    ExecCtx ctx{};
    ctx.opB = 0x2000;

    BoxResult r = mBox::execLda(g, ctx);

    CHECK(r.regWriteIdx == kNoRegWrite);   // R31 == 31 == kNoRegWrite
}


// =============================================================================
// LDAH -- compute Ra <- Rb + (sext(disp) << 16)
// =============================================================================

TEST_CASE("mBox::execLdah -- displacement scaled by 65536")
{
    InstructionGrain g = makeMemGrain(0x09, 5, 6, 0x0001, kLdaFlags, &mBox::execLdah);
    ExecCtx ctx{};
    ctx.opB = 0;

    BoxResult r = mBox::execLdah(g, ctx);

    CHECK(r.regWriteIdx == 5);
    CHECK(r.regWriteValue == 0x10000ULL);
}

TEST_CASE("mBox::execLdah -- negative disp shifts and sign-extends correctly")
{
    InstructionGrain g = makeMemGrain(0x09, 5, 6, -1, kLdaFlags, &mBox::execLdah);
    ExecCtx ctx{};
    ctx.opB = 0x100000ULL;

    BoxResult r = mBox::execLdah(g, ctx);

    CHECK(r.regWriteValue == (0x100000ULL + static_cast<uint64_t>(-65536LL)));
}


// =============================================================================
// LDBU / LDWU -- 1-byte / 2-byte zero-extending loads
// =============================================================================

TEST_CASE("mBox::execLdbu -- packs memSize=1 and EA")
{
    InstructionGrain g = makeMemGrain(0x0A, 7, 8, 0x0004, kLoadFlags, &mBox::execLdbu);
    ExecCtx ctx{};
    ctx.opB = 0x2000;

    BoxResult r = mBox::execLdbu(g, ctx);

    CHECK(r.memAddr == 0x2004ULL);
    CHECK(r.memSize == 1);
    CHECK_FALSE(r.memIsStore);
    CHECK(r.regWriteIdx == 7);
    CHECK_FALSE(r.regWriteIsFp);
}

TEST_CASE("mBox::execLdwu -- packs memSize=2 and EA")
{
    InstructionGrain g = makeMemGrain(0x0C, 9, 10, 0x0008, kLoadFlags, &mBox::execLdwu);
    ExecCtx ctx{};
    ctx.opB = 0x3000;

    BoxResult r = mBox::execLdwu(g, ctx);

    CHECK(r.memAddr == 0x3008ULL);
    CHECK(r.memSize == 2);
    CHECK(r.regWriteIdx == 9);
}


// =============================================================================
// LDL / LDQ -- 4-byte sign-extended / 8-byte loads
// =============================================================================

TEST_CASE("mBox::execLdl -- packs memSize=4 (drainer sign-extends)")
{
    InstructionGrain g = makeMemGrain(0x28, 11, 12, 0x0010, kLoadFlags, &mBox::execLdl);
    ExecCtx ctx{};
    ctx.opB = 0x4000;

    BoxResult r = mBox::execLdl(g, ctx);

    CHECK(r.memAddr == 0x4010ULL);
    CHECK(r.memSize == 4);
    CHECK_FALSE(r.memIsStore);
    CHECK(r.regWriteIdx == 11);
}

TEST_CASE("mBox::execLdq -- packs memSize=8")
{
    InstructionGrain g = makeMemGrain(0x29, 13, 14, 0x0020, kLoadFlags, &mBox::execLdq);
    ExecCtx ctx{};
    ctx.opB = 0x5000;

    BoxResult r = mBox::execLdq(g, ctx);

    CHECK(r.memAddr == 0x5020ULL);
    CHECK(r.memSize == 8);
    CHECK(r.regWriteIdx == 13);
}


// =============================================================================
// LDQ_U -- 8-byte load, EA force-aligned to quadword
// =============================================================================

TEST_CASE("mBox::execLdqU -- masks low 3 bits of computed EA")
{
    InstructionGrain g = makeMemGrain(0x0B, 15, 16, 0x0007, kLoadFlags, &mBox::execLdqU);
    ExecCtx ctx{};
    ctx.opB = 0x6000;

    BoxResult r = mBox::execLdqU(g, ctx);

    // EA = (0x6000 + 0x0007) & ~7 = 0x6000
    CHECK(r.memAddr == 0x6000ULL);
    CHECK(r.memSize == 8);
    CHECK_FALSE(r.memIsStore);
}

TEST_CASE("mBox::execLdqU -- already-aligned EA passes through")
{
    InstructionGrain g = makeMemGrain(0x0B, 15, 16, 0x0008, kLoadFlags, &mBox::execLdqU);
    ExecCtx ctx{};
    ctx.opB = 0x6000;

    BoxResult r = mBox::execLdqU(g, ctx);

    CHECK(r.memAddr == 0x6008ULL);
}


// =============================================================================
// LDL_L / LDQ_L -- locked loads.  Same packing as LDL/LDQ; S_Locked
// in semFlags tells the drainer to set the per-CPU reservation.
// =============================================================================

TEST_CASE("mBox::execLdlL -- packs memSize=4, propagates S_Locked")
{
    InstructionGrain g = makeMemGrain(0x2A, 17, 18, 0x0000, kLockedLoadFlags, &mBox::execLdlL);
    ExecCtx ctx{};
    ctx.opB = 0x7000;

    BoxResult r = mBox::execLdlL(g, ctx);

    CHECK(r.memAddr == 0x7000ULL);
    CHECK(r.memSize == 4);
    CHECK((static_cast<uint64_t>(r.semFlags) & static_cast<uint64_t>(GrainSem::S_Locked)) != 0);
}

TEST_CASE("mBox::execLdqL -- packs memSize=8, propagates S_Locked")
{
    InstructionGrain g = makeMemGrain(0x2B, 19, 20, 0x0000, kLockedLoadFlags, &mBox::execLdqL);
    ExecCtx ctx{};
    ctx.opB = 0x8000;

    BoxResult r = mBox::execLdqL(g, ctx);

    CHECK(r.memAddr == 0x8000ULL);
    CHECK(r.memSize == 8);
    CHECK((static_cast<uint64_t>(r.semFlags) & static_cast<uint64_t>(GrainSem::S_Locked)) != 0);
}


// =============================================================================
// STB / STW / STL / STQ -- byte/word/long/quad stores
// =============================================================================

TEST_CASE("mBox::execStb -- packs memSize=1, memData=opA, no register effect")
{
    InstructionGrain g = makeMemGrain(0x0E, 21, 22, 0x0004, kStoreFlags, &mBox::execStb);
    ExecCtx ctx{};
    ctx.opA = 0xAB;
    ctx.opB = 0x9000;

    BoxResult r = mBox::execStb(g, ctx);

    CHECK(r.memAddr == 0x9004ULL);
    CHECK(r.memSize == 1);
    CHECK(r.memIsStore);
    CHECK(r.memData == 0xABULL);
    CHECK(r.regWriteIdx == kNoRegWrite);
}

TEST_CASE("mBox::execStw -- packs memSize=2")
{
    InstructionGrain g = makeMemGrain(0x0D, 23, 24, 0x0002, kStoreFlags, &mBox::execStw);
    ExecCtx ctx{};
    ctx.opA = 0xBEEF;
    ctx.opB = 0xA000;

    BoxResult r = mBox::execStw(g, ctx);

    CHECK(r.memAddr == 0xA002ULL);
    CHECK(r.memSize == 2);
    CHECK(r.memIsStore);
    CHECK(r.memData == 0xBEEFULL);
    CHECK(r.regWriteIdx == kNoRegWrite);
}

TEST_CASE("mBox::execStl -- packs memSize=4")
{
    InstructionGrain g = makeMemGrain(0x2C, 25, 26, 0x0008, kStoreFlags, &mBox::execStl);
    ExecCtx ctx{};
    ctx.opA = 0xCAFEBABEULL;
    ctx.opB = 0xB000;

    BoxResult r = mBox::execStl(g, ctx);

    CHECK(r.memAddr == 0xB008ULL);
    CHECK(r.memSize == 4);
    CHECK(r.memIsStore);
    CHECK(r.memData == 0xCAFEBABEULL);
    CHECK(r.regWriteIdx == kNoRegWrite);
}

TEST_CASE("mBox::execStq -- packs memSize=8")
{
    InstructionGrain g = makeMemGrain(0x2D, 27, 28, 0x0010, kStoreFlags, &mBox::execStq);
    ExecCtx ctx{};
    ctx.opA = 0xDEADBEEFCAFEBABEULL;
    ctx.opB = 0xC000;

    BoxResult r = mBox::execStq(g, ctx);

    CHECK(r.memAddr == 0xC010ULL);
    CHECK(r.memSize == 8);
    CHECK(r.memIsStore);
    CHECK(r.memData == 0xDEADBEEFCAFEBABEULL);
    CHECK(r.regWriteIdx == kNoRegWrite);
}


// =============================================================================
// STQ_U -- 8-byte store, EA force-aligned to quadword
// =============================================================================

TEST_CASE("mBox::execStqU -- masks low 3 bits of computed EA")
{
    InstructionGrain g = makeMemGrain(0x0F, 29, 30, 0x0005, kStoreFlags, &mBox::execStqU);
    ExecCtx ctx{};
    ctx.opA = 0x1122334455667788ULL;
    ctx.opB = 0xD000;

    BoxResult r = mBox::execStqU(g, ctx);

    CHECK(r.memAddr == 0xD000ULL);     // (0xD005) & ~7
    CHECK(r.memSize == 8);
    CHECK(r.memData == 0x1122334455667788ULL);
}


// =============================================================================
// STL_C / STQ_C -- store conditional.  Both memEffect AND regEffect
// (Ra receives the success indicator written by the drainer).
// =============================================================================

TEST_CASE("mBox::execStlC -- packs memEffect AND regWriteIdx=Ra")
{
    InstructionGrain g = makeMemGrain(0x2E, 1, 2, 0x0000, kLockedStoreFlags, &mBox::execStlC);
    ExecCtx ctx{};
    ctx.opA = 0x12345678ULL;
    ctx.opB = 0xE000;

    BoxResult r = mBox::execStlC(g, ctx);

    CHECK(r.memAddr == 0xE000ULL);
    CHECK(r.memSize == 4);
    CHECK(r.memIsStore);
    CHECK(r.memData == 0x12345678ULL);
    CHECK(r.regWriteIdx == 1);          // Ra; drainer writes 1 or 0 here
    CHECK(r.regWriteValue == 0u);       // placeholder; drainer overwrites
    CHECK_FALSE(r.regWriteIsFp);
}

TEST_CASE("mBox::execStqC -- packs memEffect AND regWriteIdx=Ra")
{
    InstructionGrain g = makeMemGrain(0x2F, 3, 4, 0x0000, kLockedStoreFlags, &mBox::execStqC);
    ExecCtx ctx{};
    ctx.opA = 0xAABBCCDDEEFF0011ULL;
    ctx.opB = 0xF000;

    BoxResult r = mBox::execStqC(g, ctx);

    CHECK(r.memAddr == 0xF000ULL);
    CHECK(r.memSize == 8);
    CHECK(r.memIsStore);
    CHECK(r.memData == 0xAABBCCDDEEFF0011ULL);
    CHECK(r.regWriteIdx == 3);
}


// =============================================================================
// FETCH -- prefetch hint.  No register or memory effect.
// =============================================================================

TEST_CASE("mBox::execFetch -- propagates semFlags only, no effect")
{
    InstructionGrain g = makeFetchGrain();
    ExecCtx ctx{};
    ctx.opB = 0x12340000ULL;   // ignored by leaf

    BoxResult r = mBox::execFetch(g, ctx);

    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == kNoRegWrite);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.divert);
}
