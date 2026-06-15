// ============================================================================
// traceLib/Disassembler.h -- per-format operand string formatter
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
// Renders the operand portion of a DEC-listing-style instruction
// trace line from the raw 32-bit encoding plus the dispatch-kind
// metadata the codegen already provides.  No per-leaf hooks; one
// switch over DispatchKind handles every Alpha instruction format:
//
//   Op-format (IntArith / IntLogical / IntShift / IntMul / FltLogical
//              / ItFp / FpTiExt):
//     Ra, Rb, Rc            (or Ra, #lit, Rc when the IMM bit is set)
//
//   Mem-format (LDA / LDAH / LDx / STx / FETCH / HW_LD / HW_ST):
//     Ra, disp(Rb)
//
//   Bra-format (BR / BSR / Bxx / FBxx):
//     Ra, disp              (disp is signed longwords from PC+4)
//
//   Jmp-format (JMP / JSR / RET / JSR_COROUTINE):
//     Ra, (Rb)              (hint bits cosmetic, not rendered)
//
//   FltIeee:
//     Fa, Fb, Fc            (FP-format takes precedence over Op-format
//                            at this level; the fcIndex is bits[4:0])
//
//   Pal:
//     <funcCode>            (rendered as "0xNNNNNN")
//
//   Misc:
//     <funcCode>            (rendered as "0xNNNN")
//
//   HwMfpr / HwMtpr:
//     Ra, IPR(<index>)      (the IPR enum index from encoded[15:0])
//
//   HwRei / Reserved / Direct (when no row populated):
//     ""                    (no operand text)
//
// The output is a std::string returned by value -- one allocation per
// instruction at most, on the hot trace path.  Sinks that need per-
// frame formatting churn can cache; v1 keeps it simple.
//
// ============================================================================

#ifndef TRACELIB_DISASSEMBLER_H
#define TRACELIB_DISASSEMBLER_H

#include <cstdint>
#include <string>

#include "grainFactoryLib/generated/DispatchKinds.h"

namespace traceLib {

// Render the operand string for the given encoding.  primaryOp is
// encoded[31:26]; kind is the DispatchKind from the primary table.
std::string disassembleOperands(uint32_t                       encoded,
                                uint8_t                        primaryOp,
                                grainFactory::DispatchKind     kind);


// Render the result string for the listing line: the post-WB value
// committed to the destination register, formatted as
// "Rxx = 0xHHHH..."  (or "Fxx = 0x..." for FP writes).  When no
// register write happened (regWriteIdx == kNoRegWrite), returns "".
std::string formatResult(uint8_t  regWriteIdx,
                         uint64_t regWriteValue,
                         bool     regWriteIsFp);

} // namespace traceLib

#endif // TRACELIB_DISASSEMBLER_H
