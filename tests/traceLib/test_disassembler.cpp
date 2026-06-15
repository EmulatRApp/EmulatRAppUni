// ============================================================================
// tests/traceLib/test_disassembler.cpp -- doctest cases for Disassembler
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// Per-format operand string round-trips.  We hand-encode an
// instruction word and assert the rendered operand string matches
// the expected DEC listing shape.
//
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "grainFactoryLib/generated/DispatchKinds.h"
#include "traceLib/Disassembler.h"

#include <cstdint>
#include <ostream>
#include <string>

using grainFactory::DispatchKind;
using traceLib::disassembleOperands;
using traceLib::formatResult;


namespace {

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

constexpr uint32_t encOpLit(uint8_t op, uint8_t ra, uint8_t lit, uint8_t func, uint8_t rc)
{
    return (uint32_t{op}   << 26)
         | (uint32_t{ra}   << 21)
         | (uint32_t{lit}  << 13)
         | (uint32_t{1}    << 12)        // IMM bit set
         | (uint32_t{func} <<  5)
         |  uint32_t{rc};
}

constexpr uint32_t encBra(uint8_t op, uint8_t ra, int32_t disp21)
{
    return (uint32_t{op} << 26)
         | (uint32_t{ra} << 21)
         | (static_cast<uint32_t>(disp21) & 0x1FFFFFu);
}

} // anonymous namespace


// =============================================================================
// Mem-format
// =============================================================================

TEST_CASE("Disassembler -- LDA R1, 0x42(R31) renders correctly")
{
    uint32_t const enc = encMem(0x08, 1, 31, 0x0042);
    std::string const ops = disassembleOperands(enc, 0x08, DispatchKind::Direct);
    CHECK(ops == "R01, 0x0042(R31)");
}

TEST_CASE("Disassembler -- LDQ R7, -8(R30) renders signed disp as hex")
{
    uint32_t const enc = encMem(0x29, 7, 30, -8);
    std::string const ops = disassembleOperands(enc, 0x29, DispatchKind::Direct);
    // disp16 is rendered as the 16-bit unsigned hex value (0xfff8 == -8 signed).
    CHECK(ops == "R07, 0xfff8(R30)");
}


// =============================================================================
// Op-format integer
// =============================================================================

TEST_CASE("Disassembler -- ADDQ R1, R2, R3 renders as triple")
{
    // INTA opcode 0x10, func 0x20 (ADDQ).
    uint32_t const enc = encOp(0x10, 1, 2, 0x20, 3);
    std::string const ops = disassembleOperands(enc, 0x10, DispatchKind::IntArith);
    CHECK(ops == "R01, R02, R03");
}

TEST_CASE("Disassembler -- ADDQ R1, #0x42, R3 renders literal form")
{
    uint32_t const enc = encOpLit(0x10, 1, 0x42, 0x20, 3);
    std::string const ops = disassembleOperands(enc, 0x10, DispatchKind::IntArith);
    CHECK(ops == "R01, #0x42, R03");
}


// =============================================================================
// Bra-format
// =============================================================================

TEST_CASE("Disassembler -- BR R31, +8 longwords")
{
    uint32_t const enc = encBra(0x30, 31, 2);
    std::string const ops = disassembleOperands(enc, 0x30, DispatchKind::Direct);
    // disp21 = 2 longwords -> 8 bytes.
    CHECK(ops == "R31, +8");
}

TEST_CASE("Disassembler -- BEQ R3, -16 longwords negative")
{
    uint32_t const enc = encBra(0x39, 3, -4);
    std::string const ops = disassembleOperands(enc, 0x39, DispatchKind::Direct);
    CHECK(ops == "R03, -16");
}


// =============================================================================
// Jmp / Pal / Misc
// =============================================================================

TEST_CASE("Disassembler -- JMP renders Ra, (Rb)")
{
    uint32_t const enc = (uint32_t{0x1A} << 26)
                       | (uint32_t{4}    << 21)   // Ra = R4
                       | (uint32_t{5}    << 16);  // Rb = R5
    std::string const ops = disassembleOperands(enc, 0x1A, DispatchKind::JmpClass);
    CHECK(ops == "R04, (R05)");
}

TEST_CASE("Disassembler -- CALL_PAL HALT renders 0x000000")
{
    uint32_t const enc = 0x00000000u;
    std::string const ops = disassembleOperands(enc, 0x00, DispatchKind::Pal);
    CHECK(ops == "0x000000");
}

TEST_CASE("Disassembler -- MISC TRAPB renders 0x0000")
{
    uint32_t const enc = (uint32_t{0x18} << 26) | 0x0000u;
    std::string const ops = disassembleOperands(enc, 0x18, DispatchKind::Misc);
    CHECK(ops == "0x0000");
}


// =============================================================================
// FP / HW
// =============================================================================

TEST_CASE("Disassembler -- ADDT renders Fa, Fb, Fc")
{
    // Doesn't have to be a real ADDT subDecode; the formatter uses
    // primaryOp + DispatchKind only.
    uint32_t const enc = (uint32_t{0x16} << 26)
                       | (uint32_t{1}    << 21)
                       | (uint32_t{2}    << 16)
                       | (uint32_t{0x0A0} << 5)
                       |  uint32_t{3};
    std::string const ops = disassembleOperands(enc, 0x16, DispatchKind::FltIeee);
    CHECK(ops == "F01, F02, F03");
}

TEST_CASE("Disassembler -- HW_MFPR renders Ra, IPR(idx)")
{
    uint32_t const enc = (uint32_t{0x19} << 26)
                       | (uint32_t{4}    << 21)
                       | (0x0100u);            // IPR selector
    std::string const ops = disassembleOperands(enc, 0x19, DispatchKind::HwMfpr);
    CHECK(ops == "R04, IPR(0x0100)");
}


// =============================================================================
// formatResult
// =============================================================================

TEST_CASE("formatResult -- integer write")
{
    std::string const s = formatResult(/*idx*/ 3, /*val*/ 0x52u, /*isFp*/ false);
    CHECK(s == "R03 = 0x0000000000000052");
}

TEST_CASE("formatResult -- FP write")
{
    std::string const s = formatResult(/*idx*/ 7, /*val*/ 0x4000000000000000ULL, /*isFp*/ true);
    CHECK(s == "F07 = 0x4000000000000000");
}

TEST_CASE("formatResult -- kNoRegWrite returns empty")
{
    std::string const s = formatResult(coreLib::kNoRegWrite, 0xDEADBEEFu, false);
    CHECK(s.empty());
}
