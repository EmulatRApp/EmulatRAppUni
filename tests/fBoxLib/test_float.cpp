// ============================================================================
// tests/fBoxLib/test_float.cpp -- doctest cases for fBox v1 leaves
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
// Doctest cases for the fBox v1 leaves:
//
//   FltLogical: CPYS, CPYSN, CPYSE
//   FltIeee:    ADDT, SUBT, MULT, DIVT, CMPTEQ, CMPTLT, CMPTLE
//
// Each test constructs an InstructionGrain by hand, populates ExecCtx
// opA / opB with raw 64-bit IEEE bit patterns, calls the leaf, and
// checks the BoxResult: regWriteIdx, regWriteIsFp, regWriteValue
// (interpreted as IEEE bits or via std::bit_cast back to double for
// arithmetic).
//
// V4 doctest convention: CHECK only, never REQUIRE (exceptions
// disabled in build).
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "grainFactoryLib/generated/GrainsForward.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <ostream>

using namespace coreLib;
using grainFactory::GrainSem;


namespace {

// Build a FltLogical / FltIeee grain.  The leaves only consult
// g.encoded[4:0] (Fc) and g.semFlags, so the FA / FB encoded fields
// are cosmetic for this test surface; what matters is c.opA / c.opB
// in the ExecCtx and the Fc bits in the encoding.
InstructionGrain makeFpGrain(uint8_t  primaryOp,
                             uint16_t func,
                             uint8_t  fa,
                             uint8_t  fb,
                             uint8_t  fc,
                             GrainSem flags,
                             GrainFn  fn)
{
    InstructionGrain g{};
    g.pc        = 0x100000;
    g.encoded   = (uint32_t{primaryOp} << 26)
                | (uint32_t{fa}        << 21)
                | (uint32_t{fb}        << 16)
                | (uint32_t{func}      <<  5)
                |  uint32_t{fc};
    g.primaryOp = primaryOp;
    g.box       = Box::Fbox;
    g.semFlags  = flags;
    g.execFn    = fn;
    return g;
}

constexpr GrainSem kIeeeFlags =
      GrainSem::S_FpFormat
    | GrainSem::S_ReadsRa | GrainSem::S_ReadsRb | GrainSem::S_ReadsFp
    | GrainSem::S_WritesRc | GrainSem::S_WritesFp
    | GrainSem::S_FpTrap;

constexpr GrainSem kLogicalFlags =
      GrainSem::S_FpFormat
    | GrainSem::S_ReadsRa | GrainSem::S_ReadsRb | GrainSem::S_ReadsFp
    | GrainSem::S_WritesRc | GrainSem::S_WritesFp;

uint64_t bitsOf(double d) { return std::bit_cast<uint64_t>(d); }
double   doubleOf(uint64_t b) { return std::bit_cast<double>(b); }

// IEEE constants used in checks.
constexpr uint64_t kPos20Bits = 0x4000000000000000ULL;   // +2.0
constexpr uint64_t kSignBit   = 0x8000000000000000ULL;
constexpr uint64_t kQuietNan  = 0x7FF8000000000000ULL;

} // anonymous namespace


// =============================================================================
// CPYS / CPYSN / CPYSE -- sign manipulation
// =============================================================================

TEST_CASE("fBox::execCpys -- sign of Fa onto magnitude of Fb")
{
    InstructionGrain g = makeFpGrain(0x17, 0x020, 1, 2, 3,
                                     kLogicalFlags, &fBox::execCpys);
    ExecCtx ctx{};
    ctx.opA = bitsOf(-1.0);   // sign = 1
    ctx.opB = bitsOf( 5.0);   // magnitude = 5.0

    BoxResult r = fBox::execCpys(g, ctx);

    CHECK(r.regWriteIdx == 3);
    CHECK(r.regWriteIsFp);
    CHECK(doubleOf(r.regWriteValue) == -5.0);
}

TEST_CASE("fBox::execCpys -- positive sign carries through")
{
    InstructionGrain g = makeFpGrain(0x17, 0x020, 1, 2, 7,
                                     kLogicalFlags, &fBox::execCpys);
    ExecCtx ctx{};
    ctx.opA = bitsOf( 1.0);   // sign = 0
    ctx.opB = bitsOf(-3.0);   // magnitude = 3.0

    BoxResult r = fBox::execCpys(g, ctx);

    CHECK(r.regWriteIdx == 7);
    CHECK(doubleOf(r.regWriteValue) == 3.0);
}

TEST_CASE("fBox::execCpysn -- negated sign of Fa onto Fb")
{
    InstructionGrain g = makeFpGrain(0x17, 0x021, 1, 2, 4,
                                     kLogicalFlags, &fBox::execCpysn);
    ExecCtx ctx{};
    ctx.opA = bitsOf( 1.0);   // sign = 0; flipped = 1
    ctx.opB = bitsOf( 7.0);

    BoxResult r = fBox::execCpysn(g, ctx);

    CHECK(r.regWriteIdx == 4);
    CHECK(doubleOf(r.regWriteValue) == -7.0);
}

TEST_CASE("fBox::execCpysn -- negative Fa flips to positive on Fb")
{
    InstructionGrain g = makeFpGrain(0x17, 0x021, 1, 2, 4,
                                     kLogicalFlags, &fBox::execCpysn);
    ExecCtx ctx{};
    ctx.opA = bitsOf(-1.0);   // sign = 1; flipped = 0
    ctx.opB = bitsOf(-2.5);

    BoxResult r = fBox::execCpysn(g, ctx);

    CHECK(doubleOf(r.regWriteValue) == 2.5);
}

TEST_CASE("fBox::execCpyse -- sign+exp of Fa, fraction of Fb")
{
    InstructionGrain g = makeFpGrain(0x17, 0x022, 1, 2, 5,
                                     kLogicalFlags, &fBox::execCpyse);
    ExecCtx ctx{};
    // Fa = -8.0 (sign=1, exp=10000000010, fraction=0)
    // Fb = +1.5 (sign=0, exp=01111111111, fraction=1000...0)
    // Result: sign=1, exp from Fa, fraction from Fb
    //   = -1.0 * 2^3 * (1.0 + 0.5) = -1.5 * 8 = -12.0
    ctx.opA = bitsOf(-8.0);
    ctx.opB = bitsOf( 1.5);

    BoxResult r = fBox::execCpyse(g, ctx);

    CHECK(r.regWriteIdx == 5);
    CHECK(doubleOf(r.regWriteValue) == -12.0);
}


// =============================================================================
// ADDT / SUBT / MULT / DIVT -- IEEE T-format arithmetic
// =============================================================================

TEST_CASE("fBox::execAddt -- 1.5 + 2.5 = 4.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A0, 1, 2, 3,
                                     kIeeeFlags, &fBox::execAddt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(1.5);
    ctx.opB = bitsOf(2.5);

    BoxResult r = fBox::execAddt(g, ctx);

    CHECK(r.regWriteIdx == 3);
    CHECK(r.regWriteIsFp);
    CHECK(doubleOf(r.regWriteValue) == 4.0);
}

TEST_CASE("fBox::execAddt -- additive inverse cancels to zero")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A0, 1, 2, 3,
                                     kIeeeFlags, &fBox::execAddt);
    ExecCtx ctx{};
    ctx.opA = bitsOf( 7.25);
    ctx.opB = bitsOf(-7.25);

    BoxResult r = fBox::execAddt(g, ctx);

    CHECK(doubleOf(r.regWriteValue) == 0.0);
}

TEST_CASE("fBox::execSubt -- 5.0 - 2.0 = 3.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A1, 1, 2, 8,
                                     kIeeeFlags, &fBox::execSubt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(5.0);
    ctx.opB = bitsOf(2.0);

    BoxResult r = fBox::execSubt(g, ctx);

    CHECK(r.regWriteIdx == 8);
    CHECK(doubleOf(r.regWriteValue) == 3.0);
}

TEST_CASE("fBox::execMult -- 2.0 * 3.0 = 6.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A2, 1, 2, 9,
                                     kIeeeFlags, &fBox::execMult);
    ExecCtx ctx{};
    ctx.opA = bitsOf(2.0);
    ctx.opB = bitsOf(3.0);

    BoxResult r = fBox::execMult(g, ctx);

    CHECK(r.regWriteIdx == 9);
    CHECK(doubleOf(r.regWriteValue) == 6.0);
}

TEST_CASE("fBox::execMult -- zero times finite is zero")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A2, 1, 2, 9,
                                     kIeeeFlags, &fBox::execMult);
    ExecCtx ctx{};
    ctx.opA = bitsOf(0.0);
    ctx.opB = bitsOf(42.0);

    BoxResult r = fBox::execMult(g, ctx);

    CHECK(doubleOf(r.regWriteValue) == 0.0);
}

TEST_CASE("fBox::execDivt -- 10.0 / 2.0 = 5.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A3, 1, 2, 10,
                                     kIeeeFlags, &fBox::execDivt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(10.0);
    ctx.opB = bitsOf( 2.0);

    BoxResult r = fBox::execDivt(g, ctx);

    CHECK(r.regWriteIdx == 10);
    CHECK(doubleOf(r.regWriteValue) == 5.0);
}

TEST_CASE("fBox::execDivt -- 1.0 / 0.0 yields IEEE +infinity")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A3, 1, 2, 10,
                                     kIeeeFlags, &fBox::execDivt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(1.0);
    ctx.opB = bitsOf(0.0);

    BoxResult r = fBox::execDivt(g, ctx);

    double const result = doubleOf(r.regWriteValue);
    CHECK(std::isinf(result));
    CHECK(result > 0.0);
}


// =============================================================================
// CMPTEQ / CMPTLT / CMPTLE -- IEEE compares (Alpha quirk: result is 2.0/0.0)
// =============================================================================

TEST_CASE("fBox::execCmpteq -- equal operands -> 2.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A5, 1, 2, 3,
                                     kIeeeFlags, &fBox::execCmpteq);
    ExecCtx ctx{};
    ctx.opA = bitsOf(3.14);
    ctx.opB = bitsOf(3.14);

    BoxResult r = fBox::execCmpteq(g, ctx);

    CHECK(r.regWriteIdx == 3);
    CHECK(r.regWriteIsFp);
    CHECK(r.regWriteValue == kPos20Bits);
}

TEST_CASE("fBox::execCmpteq -- unequal operands -> 0.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A5, 1, 2, 3,
                                     kIeeeFlags, &fBox::execCmpteq);
    ExecCtx ctx{};
    ctx.opA = bitsOf(1.0);
    ctx.opB = bitsOf(2.0);

    BoxResult r = fBox::execCmpteq(g, ctx);

    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("fBox::execCmpteq -- NaN compares unordered (false) -> 0.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A5, 1, 2, 3,
                                     kIeeeFlags, &fBox::execCmpteq);
    ExecCtx ctx{};
    ctx.opA = kQuietNan;
    ctx.opB = bitsOf(1.0);

    BoxResult r = fBox::execCmpteq(g, ctx);

    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("fBox::execCmptlt -- 1.0 < 2.0 -> 2.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A6, 1, 2, 4,
                                     kIeeeFlags, &fBox::execCmptlt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(1.0);
    ctx.opB = bitsOf(2.0);

    BoxResult r = fBox::execCmptlt(g, ctx);

    CHECK(r.regWriteValue == kPos20Bits);
}

TEST_CASE("fBox::execCmptlt -- 2.0 < 1.0 is false -> 0.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A6, 1, 2, 4,
                                     kIeeeFlags, &fBox::execCmptlt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(2.0);
    ctx.opB = bitsOf(1.0);

    BoxResult r = fBox::execCmptlt(g, ctx);

    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("fBox::execCmptlt -- equal operands are not less than -> 0.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A6, 1, 2, 4,
                                     kIeeeFlags, &fBox::execCmptlt);
    ExecCtx ctx{};
    ctx.opA = bitsOf(1.0);
    ctx.opB = bitsOf(1.0);

    BoxResult r = fBox::execCmptlt(g, ctx);

    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("fBox::execCmptle -- 1.0 <= 1.0 -> 2.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A7, 1, 2, 5,
                                     kIeeeFlags, &fBox::execCmptle);
    ExecCtx ctx{};
    ctx.opA = bitsOf(1.0);
    ctx.opB = bitsOf(1.0);

    BoxResult r = fBox::execCmptle(g, ctx);

    CHECK(r.regWriteValue == kPos20Bits);
}

TEST_CASE("fBox::execCmptle -- 2.0 <= 1.0 is false -> 0.0")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A7, 1, 2, 5,
                                     kIeeeFlags, &fBox::execCmptle);
    ExecCtx ctx{};
    ctx.opA = bitsOf(2.0);
    ctx.opB = bitsOf(1.0);

    BoxResult r = fBox::execCmptle(g, ctx);

    CHECK(r.regWriteValue == 0u);
}

TEST_CASE("fBox::execCmptle -- NaN -> 0.0 (unordered)")
{
    InstructionGrain g = makeFpGrain(0x16, 0x0A7, 1, 2, 5,
                                     kIeeeFlags, &fBox::execCmptle);
    ExecCtx ctx{};
    ctx.opA = kQuietNan;
    ctx.opB = bitsOf(1.0);

    BoxResult r = fBox::execCmptle(g, ctx);

    CHECK(r.regWriteValue == 0u);
}


// =============================================================================
// Negative-sign round trip through CPYS (sanity for kSignBit handling)
// =============================================================================

TEST_CASE("fBox::execCpys -- explicit sign bit comparison")
{
    InstructionGrain g = makeFpGrain(0x17, 0x020, 1, 2, 6,
                                     kLogicalFlags, &fBox::execCpys);
    ExecCtx ctx{};
    ctx.opA = kSignBit;       // -0.0 (only sign bit set)
    ctx.opB = bitsOf(2.0);

    BoxResult r = fBox::execCpys(g, ctx);

    CHECK((r.regWriteValue & kSignBit) == kSignBit);
    CHECK(doubleOf(r.regWriteValue) == -2.0);
}
