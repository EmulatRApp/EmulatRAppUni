// ============================================================================
// traceLib/Disassembler.cpp -- per-format operand string formatter impl
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "traceLib/Disassembler.h"

#include "coreLib/BoxResult.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>


namespace traceLib {

namespace {

// Field extractors; encoded[X:Y] semantics mirror the Alpha SRM.
uint8_t  raField(uint32_t e) noexcept { return static_cast<uint8_t>((e >> 21) & 0x1Fu); }
uint8_t  rbField(uint32_t e) noexcept { return static_cast<uint8_t>((e >> 16) & 0x1Fu); }
uint8_t  rcField(uint32_t e) noexcept { return static_cast<uint8_t>(e & 0x1Fu); }
uint16_t memDisp(uint32_t e) noexcept { return static_cast<uint16_t>(e & 0xFFFFu); }

bool     hasLiteral(uint32_t e) noexcept { return (e & (1u << 12)) != 0; }
uint8_t  literal8(uint32_t e)   noexcept { return static_cast<uint8_t>((e >> 13) & 0xFFu); }

uint32_t miscFunc(uint32_t e)   noexcept { return e & 0xFFFFu; }
uint32_t palFunc(uint32_t e)    noexcept { return e & 0x03FFFFFFu; }
uint32_t iprIndex(uint32_t e)   noexcept { return e & 0xFFFFu; }

// Bra-format displacement: encoded[20:0] is signed 21-bit, scaled by 4.
int32_t  braDispBytes(uint32_t e) noexcept
{
    int32_t const signedEnc = static_cast<int32_t>(e);
    int32_t const disp21    = (signedEnc << 11) >> 11;
    return disp21 * 4;
}

// Small printf-into-string helper.  std::format is C++20-mandated but
// MSVC's <format> support is gated on /std:c++20 plus a recent toolchain;
// snprintf into a fixed buffer is portable, fast enough on the trace
// path, and matches the rest of the project's style.
std::string vfmt(char const* fmt, ...) noexcept
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int const n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return std::string{};
    return std::string{buf, static_cast<size_t>(n < int(sizeof(buf)) ? n : int(sizeof(buf)) - 1)};
}

} // anonymous namespace


std::string disassembleOperands(uint32_t                       encoded,
                                uint8_t                        primaryOp,
                                grainFactory::DispatchKind     kind)
{
    switch (kind) {
        // --------------------------------------------------------------
        // Integer Op-format families.  Use Ra / Rb / Rc; honour IMM bit.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::IntArith:
        case grainFactory::DispatchKind::IntLogical:
        case grainFactory::DispatchKind::IntShift:
        case grainFactory::DispatchKind::IntMul: {
            uint8_t const ra = raField(encoded);
            uint8_t const rc = rcField(encoded);
            if (hasLiteral(encoded)) {
                return vfmt("R%02u, #0x%02x, R%02u", ra, literal8(encoded), rc);
            }
            return vfmt("R%02u, R%02u, R%02u", ra, rbField(encoded), rc);
        }

        // --------------------------------------------------------------
        // FP register file: Fa, Fb, Fc.  ItFp transfers between regfiles
        // and uses Ra (int) / Fc (fp) so it shares this column shape.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::FltIeee:
        case grainFactory::DispatchKind::FltLogical:
        case grainFactory::DispatchKind::FpTiExt:
        case grainFactory::DispatchKind::ItFp:
            return vfmt("F%02u, F%02u, F%02u",
                        raField(encoded), rbField(encoded), rcField(encoded));

        // --------------------------------------------------------------
        // Direct primary opcode.  primaryOp tells us whether this is a
        // Mem-format slot (loads / stores / LDA family) or a Bra-format
        // slot (BR / BSR / Bxx).  Bra range is 0x30..0x3F.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::Direct: {
            if (primaryOp >= 0x30 && primaryOp <= 0x3F) {
                return vfmt("R%02u, %+d",
                            raField(encoded), braDispBytes(encoded));
            }
            return vfmt("R%02u, 0x%04x(R%02u)",
                        raField(encoded),
                        memDisp(encoded),
                        rbField(encoded));
        }

        // --------------------------------------------------------------
        // Indirect jump: Ra, (Rb).  hint bits cosmetic; not rendered.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::JmpClass:
            return vfmt("R%02u, (R%02u)",
                        raField(encoded), rbField(encoded));

        // --------------------------------------------------------------
        // Pal (CALL_PAL) -- 26-bit function code.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::Pal:
            return vfmt("0x%06x", palFunc(encoded));

        // --------------------------------------------------------------
        // Misc (TRAPB / EXCB / MB / WMB / FETCH / RPCC / RC / RS / ECB)
        // -- 16-bit function code.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::Misc:
            return vfmt("0x%04x", miscFunc(encoded));

        // --------------------------------------------------------------
        // PALmode HW_LD / HW_ST -- Ra, disp(Rb) like Mem-format but the
        // displacement is 12 bits in encoded[11:0] and the high bits
        // carry hints.  Render the Mem-format shape; the hint bits are
        // cosmetic for trace purposes.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::HwLd:
        case grainFactory::DispatchKind::HwSt:
            return vfmt("R%02u, 0x%03x(R%02u)",
                        raField(encoded),
                        encoded & 0xFFFu,
                        rbField(encoded));

        // --------------------------------------------------------------
        // HW_MFPR / HW_MTPR -- Ra is the int regfile slot; encoded[15:0]
        // is the IPR selector.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::HwMfpr:
        case grainFactory::DispatchKind::HwMtpr:
            return vfmt("R%02u, IPR(0x%04x)",
                        raField(encoded),
                        iprIndex(encoded));

        // --------------------------------------------------------------
        // HW_REI takes no operands; the divert target is implicit
        // (excAddr).  Reserved slots have no meaningful encoding.
        // --------------------------------------------------------------
        case grainFactory::DispatchKind::HwRei:
        case grainFactory::DispatchKind::Reserved:
            return std::string{};
    }
    return std::string{};
}


std::string formatResult(uint8_t  regWriteIdx,
                         uint64_t regWriteValue,
                         bool     regWriteIsFp)
{
    if (regWriteIdx == coreLib::kNoRegWrite) {
        return std::string{};
    }
    char const prefix = regWriteIsFp ? 'F' : 'R';
    return vfmt("%c%02u = 0x%016llx",
                prefix,
                static_cast<unsigned>(regWriteIdx),
                static_cast<unsigned long long>(regWriteValue));
}

} // namespace traceLib
