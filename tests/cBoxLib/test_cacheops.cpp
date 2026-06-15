// ============================================================================
// tests/cBoxLib/test_cacheops.cpp -- doctest cases for cBox v1 leaves
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
// Doctest cases for the cBox dispatch arm: TRAPB, EXCB, MB, WMB,
// ECB.  All five v1 leaves are no-ops at the V4 contract -- they
// propagate semFlags and produce no register or memory effect.  The
// tests assert that absence and confirm the inbound flag set is
// reflected on the BoxResult unchanged.
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

using namespace coreLib;
using grainFactory::GrainSem;


namespace {

// Build a Misc-format grain (opcode 0x18) for a Cbox leaf.  func is
// the 16-bit function code; flags carries the per-leaf S_* bit
// (S_TrapBarrier, S_ExcBarrier, S_Mb, S_Wmb, S_PrefetchOnly).
InstructionGrain makeMiscCbGrain(uint16_t func, GrainSem flags, GrainFn fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{0x18} << 26) | uint32_t{func};
    g.primaryOp = 0x18;
    g.box       = Box::Cbox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

void checkNoEffect(BoxResult const& r)
{
    CHECK(r.faultCode == kNoFault);
    CHECK(r.regWriteIdx == kNoRegWrite);
    CHECK(r.memSize == kNoMemEffect);
    CHECK_FALSE(r.divert);
}

} // anonymous namespace


// =============================================================================
// TRAPB / EXCB -- trap and exception barriers
// =============================================================================

TEST_CASE("cBox::execTrapb -- propagates S_TrapBarrier, no effect")
{
    GrainSem const flags = GrainSem::S_MiscFormat | GrainSem::S_TrapBarrier;
    InstructionGrain g = makeMiscCbGrain(0x0000, flags, &cBox::execTrapb);
    ExecCtx ctx{};

    BoxResult r = cBox::execTrapb(g, ctx);

    CHECK(static_cast<uint64_t>(r.semFlags) == static_cast<uint64_t>(flags));
    checkNoEffect(r);
}

TEST_CASE("cBox::execExcb -- propagates S_ExcBarrier, no effect")
{
    GrainSem const flags = GrainSem::S_MiscFormat | GrainSem::S_ExcBarrier;
    InstructionGrain g = makeMiscCbGrain(0x0400, flags, &cBox::execExcb);
    ExecCtx ctx{};

    BoxResult r = cBox::execExcb(g, ctx);

    CHECK(static_cast<uint64_t>(r.semFlags) == static_cast<uint64_t>(flags));
    checkNoEffect(r);
}


// =============================================================================
// MB / WMB -- memory barriers
// =============================================================================

TEST_CASE("cBox::execMb -- propagates S_Mb, no effect")
{
    GrainSem const flags = GrainSem::S_MiscFormat | GrainSem::S_Mb;
    InstructionGrain g = makeMiscCbGrain(0x4000, flags, &cBox::execMb);
    ExecCtx ctx{};

    BoxResult r = cBox::execMb(g, ctx);

    CHECK(static_cast<uint64_t>(r.semFlags) == static_cast<uint64_t>(flags));
    checkNoEffect(r);
}

TEST_CASE("cBox::execWmb -- propagates S_Wmb, no effect")
{
    GrainSem const flags = GrainSem::S_MiscFormat | GrainSem::S_Wmb;
    InstructionGrain g = makeMiscCbGrain(0x4400, flags, &cBox::execWmb);
    ExecCtx ctx{};

    BoxResult r = cBox::execWmb(g, ctx);

    CHECK(static_cast<uint64_t>(r.semFlags) == static_cast<uint64_t>(flags));
    checkNoEffect(r);
}


// =============================================================================
// ECB -- evict cache block (Rb is the EA base; ignored in v1)
// =============================================================================

TEST_CASE("cBox::execEcb -- propagates S_PrefetchOnly, no effect; opB ignored")
{
    GrainSem const flags = GrainSem::S_MiscFormat
                         | GrainSem::S_ReadsRb
                         | GrainSem::S_ReadsInt
                         | GrainSem::S_PrefetchOnly;
    InstructionGrain g = makeMiscCbGrain(0xE800, flags, &cBox::execEcb);
    ExecCtx ctx{};
    ctx.opB = 0xDEADBEEFULL;   // a cache subsystem would consult this; v1 ignores

    BoxResult r = cBox::execEcb(g, ctx);

    CHECK(static_cast<uint64_t>(r.semFlags) == static_cast<uint64_t>(flags));
    checkNoEffect(r);
}
