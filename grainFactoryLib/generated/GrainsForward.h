// ============================================================================
// GrainsForward.h -- generated for EmulatR V4
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
// AUTO-GENERATED -- DO NOT EDIT.
// Source:    grainFactoryLib/GrainMasterV4.tsv
// Generator: grainFactoryLib/codegen/genGrains.py
//
// Edit the source TSV and re-run the generator.  Hand-edits to this
// file will be lost on the next codegen pass.
// ============================================================================


#pragma once

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"

// Forward declarations of every leaf function referenced by
// the codegen-emitted dispatch tables.  Hand-written leaf
// implementations live under {box}Lib/grains/ and resolve at
// link time.  A missing implementation surfaces as a linker
// error naming the unresolved leaf -- the structural guarantee
// that every GrainMasterV4.tsv row has a corresponding body.

// ---------------------------------------------------------------------------
// Ibox -- namespace iBox
// ---------------------------------------------------------------------------
namespace iBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// JMP: indirect jump
AXP_HOT AXP_FLATTEN
BoxResult execJmp(InstructionGrain const& g, ExecCtx const& c) noexcept;

// JSR: indirect call
AXP_HOT AXP_FLATTEN
BoxResult execJsr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RET: return; hint that target was pushed by JSR
AXP_HOT AXP_FLATTEN
BoxResult execRet(InstructionGrain const& g, ExecCtx const& c) noexcept;

// JSR_COROUTINE: coroutine swap
AXP_HOT AXP_FLATTEN
BoxResult execJsrCoroutine(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BR: unconditional branch with link in Ra
AXP_HOT AXP_FLATTEN
BoxResult execBr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BSR: branch to subroutine; pushes return PC
AXP_HOT AXP_FLATTEN
BoxResult execBsr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BLBC: branch if low bit of Ra is clear
AXP_HOT AXP_FLATTEN
BoxResult execBlbc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BEQ: branch if Ra == 0
AXP_HOT AXP_FLATTEN
BoxResult execBeq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BLT: branch if Ra < 0
AXP_HOT AXP_FLATTEN
BoxResult execBlt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BLE: branch if Ra <= 0 (signed)
AXP_HOT AXP_FLATTEN
BoxResult execBle(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BLBS: branch if low bit of Ra is set
AXP_HOT AXP_FLATTEN
BoxResult execBlbs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BNE: branch if Ra != 0
AXP_HOT AXP_FLATTEN
BoxResult execBne(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BGE: branch if Ra >= 0
AXP_HOT AXP_FLATTEN
BoxResult execBge(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BGT: branch if Ra > 0 (signed)
AXP_HOT AXP_FLATTEN
BoxResult execBgt(InstructionGrain const& g, ExecCtx const& c) noexcept;

} // namespace iBox

// ---------------------------------------------------------------------------
// Ebox -- namespace eBox
// ---------------------------------------------------------------------------
namespace eBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// ADDL: 32-bit add; result sign-extended to 64
AXP_HOT AXP_FLATTEN
BoxResult execAddl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S4ADDL: scaled add longword: Rc = sext((Ra*4 + Rb)<31:0>)
AXP_HOT AXP_FLATTEN
BoxResult execS4addl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBL: 32-bit subtract
AXP_HOT AXP_FLATTEN
BoxResult execSubl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S4SUBL: scaled sub longword: Rc = sext((Ra*4 - Rb)<31:0>)
AXP_HOT AXP_FLATTEN
BoxResult execS4subl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPBGE: per-byte unsigned compare; 8-bit result in low byte of Rc
AXP_HOT AXP_FLATTEN
BoxResult execCmpbge(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S8ADDL: scaled add longword: Rc = sext((Ra*8 + Rb)<31:0>)
AXP_HOT AXP_FLATTEN
BoxResult execS8addl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S8SUBL: scaled sub longword: Rc = sext((Ra*8 - Rb)<31:0>)
AXP_HOT AXP_FLATTEN
BoxResult execS8subl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPULT: compare unsigned less than
AXP_HOT AXP_FLATTEN
BoxResult execCmpult(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDQ: 64-bit add
AXP_HOT AXP_FLATTEN
BoxResult execAddq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S4ADDQ: scaled add quadword: Rc = Ra*4 + Rb
AXP_HOT AXP_FLATTEN
BoxResult execS4addq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBQ: 64-bit subtract
AXP_HOT AXP_FLATTEN
BoxResult execSubq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S4SUBQ: scaled sub quadword: Rc = Ra*4 - Rb
AXP_HOT AXP_FLATTEN
BoxResult execS4subq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPEQ: compare equal
AXP_HOT AXP_FLATTEN
BoxResult execCmpeq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S8ADDQ: scaled add quadword: Rc = Ra*8 + Rb
AXP_HOT AXP_FLATTEN
BoxResult execS8addq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// S8SUBQ: scaled sub quadword: Rc = Ra*8 - Rb
AXP_HOT AXP_FLATTEN
BoxResult execS8subq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPULE: compare unsigned less or equal
AXP_HOT AXP_FLATTEN
BoxResult execCmpule(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPLT: compare signed less than
AXP_HOT AXP_FLATTEN
BoxResult execCmplt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPLE: compare signed less or equal
AXP_HOT AXP_FLATTEN
BoxResult execCmple(InstructionGrain const& g, ExecCtx const& c) noexcept;

// AND: bitwise AND
AXP_HOT AXP_FLATTEN
BoxResult execAnd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BIC: bit clear (AND NOT): Rc = Ra & ~Rb
AXP_HOT AXP_FLATTEN
BoxResult execBic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVLBS: conditional move if Ra<0> set; else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovlbs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVLBC: conditional move if Ra<0> clear; else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovlbc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BIS: OR; canonical MOV pseudo-op (bis Rs, R31, Rd)
AXP_HOT AXP_FLATTEN
BoxResult execBis(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVEQ: conditional move if Ra == 0; else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmoveq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVNE: conditional move if Ra != 0; else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovne(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ORNOT: OR NOT: Rc = Ra | ~Rb
AXP_HOT AXP_FLATTEN
BoxResult execOrnot(InstructionGrain const& g, ExecCtx const& c) noexcept;

// XOR: bitwise XOR
AXP_HOT AXP_FLATTEN
BoxResult execXor(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVLT: conditional move if Ra < 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovlt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVGE: conditional move if Ra >= 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovge(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EQV: equivalence (XNOR): Rc = ~(Ra ^ Rb)
AXP_HOT AXP_FLATTEN
BoxResult execEqv(InstructionGrain const& g, ExecCtx const& c) noexcept;

// AMASK: architecture mask: Rc <- Rb & ~supported_features
AXP_HOT AXP_FLATTEN
BoxResult execAmask(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVLE: conditional move if Ra <= 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovle(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMOVGT: conditional move if Ra > 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
BoxResult execCmovgt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// IMPLVER: report processor implementation version (EV6=2)
AXP_HOT AXP_FLATTEN
BoxResult execImplver(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKBL: mask byte low
AXP_HOT AXP_FLATTEN
BoxResult execMskbl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTBL: extract byte low
AXP_HOT AXP_FLATTEN
BoxResult execExtbl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSBL: insert byte low
AXP_HOT AXP_FLATTEN
BoxResult execInsbl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKWL: mask word low
AXP_HOT AXP_FLATTEN
BoxResult execMskwl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTWL: extract word low
AXP_HOT AXP_FLATTEN
BoxResult execExtwl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSWL: insert word low
AXP_HOT AXP_FLATTEN
BoxResult execInswl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKLL: mask longword low
AXP_HOT AXP_FLATTEN
BoxResult execMskll(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTLL: extract longword low
AXP_HOT AXP_FLATTEN
BoxResult execExtll(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSLL: insert longword low
AXP_HOT AXP_FLATTEN
BoxResult execInsll(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ZAP: zero per-byte where Rb<i> set
AXP_HOT AXP_FLATTEN
BoxResult execZap(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ZAPNOT: zero per-byte where Rb<i> clear
AXP_HOT AXP_FLATTEN
BoxResult execZapnot(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKQL: mask quadword low
AXP_HOT AXP_FLATTEN
BoxResult execMskql(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SRL: shift right logical
AXP_HOT AXP_FLATTEN
BoxResult execSrl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTQL: extract quadword low
AXP_HOT AXP_FLATTEN
BoxResult execExtql(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SLL: shift left logical
AXP_HOT AXP_FLATTEN
BoxResult execSll(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQL: insert quadword low
AXP_HOT AXP_FLATTEN
BoxResult execInsql(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SRA: shift right arithmetic
AXP_HOT AXP_FLATTEN
BoxResult execSra(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKWH: mask word high
AXP_HOT AXP_FLATTEN
BoxResult execMskwh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSWH: insert word high
AXP_HOT AXP_FLATTEN
BoxResult execInswh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTWH: extract word high
AXP_HOT AXP_FLATTEN
BoxResult execExtwh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKLH: mask longword high
AXP_HOT AXP_FLATTEN
BoxResult execMsklh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSLH: insert longword high
AXP_HOT AXP_FLATTEN
BoxResult execInslh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTLH: extract longword high
AXP_HOT AXP_FLATTEN
BoxResult execExtlh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MSKQH: mask quadword high
AXP_HOT AXP_FLATTEN
BoxResult execMskqh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQH: insert quadword high
AXP_HOT AXP_FLATTEN
BoxResult execInsqh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXTQH: extract quadword high
AXP_HOT AXP_FLATTEN
BoxResult execExtqh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULL: 32-bit multiply
AXP_HOT AXP_FLATTEN
BoxResult execMull(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULQ: 64-bit multiply
AXP_HOT AXP_FLATTEN
BoxResult execMulq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// UMULH: unsigned multiply high (upper 64 bits of 128-bit product)
AXP_HOT AXP_FLATTEN
BoxResult execUmulh(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ITOFS: integer -> FP single-precision move
AXP_HOT AXP_FLATTEN
BoxResult execItofs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ITOFF: integer -> FP F_floating move
AXP_HOT AXP_FLATTEN
BoxResult execItoff(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ITOFT: integer -> FP T-format move
AXP_HOT AXP_FLATTEN
BoxResult execItoft(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RPCC: read cycle counter; non-deterministic
AXP_HOT AXP_FLATTEN
BoxResult execRpcc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RC: read-and-set intrFlag IPR; old value -> Ra
AXP_HOT AXP_FLATTEN
BoxResult execRc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RS: read-and-clear intrFlag IPR; old value -> Ra
AXP_HOT AXP_FLATTEN
BoxResult execRs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SEXTB: sign-extend byte
AXP_HOT AXP_FLATTEN
BoxResult execSextb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SEXTW: sign-extend word
AXP_HOT AXP_FLATTEN
BoxResult execSextw(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CTPOP: count population
AXP_HOT AXP_FLATTEN
BoxResult execCtpop(InstructionGrain const& g, ExecCtx const& c) noexcept;

// PERR: pixel error: sum of abs byte differences across 8 lanes
AXP_HOT AXP_FLATTEN
BoxResult execPerr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CTLZ: count leading zeros
AXP_HOT AXP_FLATTEN
BoxResult execCtlz(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CTTZ: count trailing zeros
AXP_HOT AXP_FLATTEN
BoxResult execCttz(InstructionGrain const& g, ExecCtx const& c) noexcept;

// UNPKBW: unpack low 4 bytes of Rb into 4 zero-extended 16-bit lanes
AXP_HOT AXP_FLATTEN
BoxResult execUnpkbw(InstructionGrain const& g, ExecCtx const& c) noexcept;

// UNPKBL: unpack low 2 bytes of Rb into 2 zero-extended 32-bit lanes
AXP_HOT AXP_FLATTEN
BoxResult execUnpkbl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// PKWB: pack low byte of each 16-bit lane of Rb into low halfword
AXP_HOT AXP_FLATTEN
BoxResult execPkwb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// PKLB: pack low byte of each 32-bit lane of Rb into low halfword
AXP_HOT AXP_FLATTEN
BoxResult execPklb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MINSB8: per-lane signed min, 8 byte lanes
AXP_HOT AXP_FLATTEN
BoxResult execMinsb8(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MINSW4: per-lane signed min, 4 word lanes
AXP_HOT AXP_FLATTEN
BoxResult execMinsw4(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MINUB8: per-lane unsigned min, 8 byte lanes
AXP_HOT AXP_FLATTEN
BoxResult execMinub8(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MINUW4: per-lane unsigned min, 4 word lanes
AXP_HOT AXP_FLATTEN
BoxResult execMinuw4(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MAXUB8: per-lane unsigned max, 8 byte lanes
AXP_HOT AXP_FLATTEN
BoxResult execMaxub8(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MAXUW4: per-lane unsigned max, 4 word lanes
AXP_HOT AXP_FLATTEN
BoxResult execMaxuw4(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MAXSB8: per-lane signed max, 8 byte lanes
AXP_HOT AXP_FLATTEN
BoxResult execMaxsb8(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MAXSW4: per-lane signed max, 4 word lanes
AXP_HOT AXP_FLATTEN
BoxResult execMaxsw4(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FTOIT: FP T-format -> integer register move
AXP_HOT AXP_FLATTEN
BoxResult execFtoit(InstructionGrain const& g, ExecCtx const& c) noexcept;

} // namespace eBox

// ---------------------------------------------------------------------------
// Fbox -- namespace fBox
// ---------------------------------------------------------------------------
namespace fBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// SQRTF_C: VAX SQRTF/C
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_C: IEEE SQRTS/C
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_C: VAX SQRTG/C
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_C: IEEE SQRTT/C
AXP_HOT AXP_FLATTEN
BoxResult execSqrttC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_M: IEEE SQRTS/M
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsM(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_M: IEEE SQRTT/M
AXP_HOT AXP_FLATTEN
BoxResult execSqrttM(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF: VAX SQRTF
AXP_HOT AXP_FLATTEN
BoxResult execSqrtf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS: Square root S_floating
AXP_HOT AXP_FLATTEN
BoxResult execSqrts(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG: VAX SQRTG
AXP_HOT AXP_FLATTEN
BoxResult execSqrtg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT: Square root T_floating
AXP_HOT AXP_FLATTEN
BoxResult execSqrtt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_D: IEEE SQRTS/D
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsD(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_D: IEEE SQRTT/D
AXP_HOT AXP_FLATTEN
BoxResult execSqrttD(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF_UC: VAX SQRTF/UC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_UC: IEEE SQRTS/UC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_UC: VAX SQRTG/UC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_UC: IEEE SQRTT/UC
AXP_HOT AXP_FLATTEN
BoxResult execSqrttUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_UM: IEEE SQRTS/UM
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsUm(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_UM: IEEE SQRTT/UM
AXP_HOT AXP_FLATTEN
BoxResult execSqrttUm(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF_U: VAX SQRTF/U
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_U: IEEE SQRTS/U
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_U: VAX SQRTG/U
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_U: IEEE SQRTT/U
AXP_HOT AXP_FLATTEN
BoxResult execSqrttU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_UD: IEEE SQRTS/UD
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsUd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_UD: IEEE SQRTT/UD
AXP_HOT AXP_FLATTEN
BoxResult execSqrttUd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF_SC: VAX SQRTF/SC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_SC: VAX SQRTG/SC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF_S: VAX SQRTF/S
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_S: VAX SQRTG/S
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF_SUC: VAX SQRTF/SUC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUC: IEEE SQRTS/SUC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_SUC: VAX SQRTG/SUC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUC: IEEE SQRTT/SUC
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUM: IEEE SQRTS/SUM
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSum(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUM: IEEE SQRTT/SUM
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSum(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTF_SU: VAX SQRTF/SU
AXP_HOT AXP_FLATTEN
BoxResult execSqrtfSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SU: IEEE SQRTS/SU
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTG_SU: VAX SQRTG/SU
AXP_HOT AXP_FLATTEN
BoxResult execSqrtgSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SU: IEEE SQRTT/SU
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUD: IEEE SQRTS/SUD
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSud(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUD: IEEE SQRTT/SUD
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSud(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUIC: IEEE SQRTS/SUIC
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSuic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUIC: IEEE SQRTT/SUIC
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSuic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUIM: IEEE SQRTS/SUIM
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSuim(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUIM: IEEE SQRTT/SUIM
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSuim(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUI: IEEE SQRTS/SUI
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSui(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUI: IEEE SQRTT/SUI
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSui(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTS_SUID: IEEE SQRTS/SUID
AXP_HOT AXP_FLATTEN
BoxResult execSqrtsSuid(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SQRTT_SUID: IEEE SQRTT/SUID
AXP_HOT AXP_FLATTEN
BoxResult execSqrttSuid(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_C: VAX ADDF/C
AXP_HOT AXP_FLATTEN
BoxResult execAddfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_C: VAX SUBF/C
AXP_HOT AXP_FLATTEN
BoxResult execSubfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_C: VAX MULF/C
AXP_HOT AXP_FLATTEN
BoxResult execMulfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_C: VAX DIVF/C
AXP_HOT AXP_FLATTEN
BoxResult execDivfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_C: VAX CVTDG/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_C: VAX ADDG/C
AXP_HOT AXP_FLATTEN
BoxResult execAddgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_C: VAX SUBG/C
AXP_HOT AXP_FLATTEN
BoxResult execSubgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_C: VAX MULG/C
AXP_HOT AXP_FLATTEN
BoxResult execMulgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_C: VAX DIVG/C
AXP_HOT AXP_FLATTEN
BoxResult execDivgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_C: VAX CVTGF/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_C: VAX CVTGD/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_C: VAX CVTGQ/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQF_C: VAX CVTQF/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtqfC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQG_C: VAX CVTQG/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtqgC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF: VAX ADDF
AXP_HOT AXP_FLATTEN
BoxResult execAddf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF: VAX SUBF
AXP_HOT AXP_FLATTEN
BoxResult execSubf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF: VAX MULF
AXP_HOT AXP_FLATTEN
BoxResult execMulf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF: VAX DIVF
AXP_HOT AXP_FLATTEN
BoxResult execDivf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG: VAX CVTDG
AXP_HOT AXP_FLATTEN
BoxResult execCvtdg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG: VAX ADDG
AXP_HOT AXP_FLATTEN
BoxResult execAddg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG: VAX SUBG
AXP_HOT AXP_FLATTEN
BoxResult execSubg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG: VAX MULG
AXP_HOT AXP_FLATTEN
BoxResult execMulg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG: VAX DIVG
AXP_HOT AXP_FLATTEN
BoxResult execDivg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPGEQ: VAX CMPGEQ
AXP_HOT AXP_FLATTEN
BoxResult execCmpgeq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPGLT: VAX CMPGLT
AXP_HOT AXP_FLATTEN
BoxResult execCmpglt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPGLE: VAX CMPGLE
AXP_HOT AXP_FLATTEN
BoxResult execCmpgle(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF: VAX CVTGF
AXP_HOT AXP_FLATTEN
BoxResult execCvtgf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD: VAX CVTGD
AXP_HOT AXP_FLATTEN
BoxResult execCvtgd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ: VAX CVTGQ
AXP_HOT AXP_FLATTEN
BoxResult execCvtgq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQF: VAX CVTQF
AXP_HOT AXP_FLATTEN
BoxResult execCvtqf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQG: VAX CVTQG
AXP_HOT AXP_FLATTEN
BoxResult execCvtqg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_UC: VAX ADDF/UC
AXP_HOT AXP_FLATTEN
BoxResult execAddfUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_UC: VAX SUBF/UC
AXP_HOT AXP_FLATTEN
BoxResult execSubfUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_UC: VAX MULF/UC
AXP_HOT AXP_FLATTEN
BoxResult execMulfUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_UC: VAX DIVF/UC
AXP_HOT AXP_FLATTEN
BoxResult execDivfUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_UC: VAX CVTDG/UC
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_UC: VAX ADDG/UC
AXP_HOT AXP_FLATTEN
BoxResult execAddgUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_UC: VAX SUBG/UC
AXP_HOT AXP_FLATTEN
BoxResult execSubgUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_UC: VAX MULG/UC
AXP_HOT AXP_FLATTEN
BoxResult execMulgUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_UC: VAX DIVG/UC
AXP_HOT AXP_FLATTEN
BoxResult execDivgUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_UC: VAX CVTGF/UC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_UC: VAX CVTGD/UC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_VC: VAX CVTGQ/VC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqVc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_U: VAX ADDF/U
AXP_HOT AXP_FLATTEN
BoxResult execAddfU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_U: VAX SUBF/U
AXP_HOT AXP_FLATTEN
BoxResult execSubfU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_U: VAX MULF/U
AXP_HOT AXP_FLATTEN
BoxResult execMulfU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_U: VAX DIVF/U
AXP_HOT AXP_FLATTEN
BoxResult execDivfU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_U: VAX CVTDG/U
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_U: VAX ADDG/U
AXP_HOT AXP_FLATTEN
BoxResult execAddgU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_U: VAX SUBG/U
AXP_HOT AXP_FLATTEN
BoxResult execSubgU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_U: VAX MULG/U
AXP_HOT AXP_FLATTEN
BoxResult execMulgU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_U: VAX DIVG/U
AXP_HOT AXP_FLATTEN
BoxResult execDivgU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_U: VAX CVTGF/U
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_U: VAX CVTGD/U
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_V: VAX CVTGQ/V
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqV(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_SC: VAX ADDF/SC
AXP_HOT AXP_FLATTEN
BoxResult execAddfSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_SC: VAX SUBF/SC
AXP_HOT AXP_FLATTEN
BoxResult execSubfSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_SC: VAX MULF/SC
AXP_HOT AXP_FLATTEN
BoxResult execMulfSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_SC: VAX DIVF/SC
AXP_HOT AXP_FLATTEN
BoxResult execDivfSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_SC: VAX CVTDG/SC
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_SC: VAX ADDG/SC
AXP_HOT AXP_FLATTEN
BoxResult execAddgSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_SC: VAX SUBG/SC
AXP_HOT AXP_FLATTEN
BoxResult execSubgSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_SC: VAX MULG/SC
AXP_HOT AXP_FLATTEN
BoxResult execMulgSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_SC: VAX DIVG/SC
AXP_HOT AXP_FLATTEN
BoxResult execDivgSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_SC: VAX CVTGF/SC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_SC: VAX CVTGD/SC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_SC: VAX CVTGQ/SC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqSc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_S: VAX ADDF/S
AXP_HOT AXP_FLATTEN
BoxResult execAddfS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_S: VAX SUBF/S
AXP_HOT AXP_FLATTEN
BoxResult execSubfS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_S: VAX MULF/S
AXP_HOT AXP_FLATTEN
BoxResult execMulfS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_S: VAX DIVF/S
AXP_HOT AXP_FLATTEN
BoxResult execDivfS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_S: VAX CVTDG/S
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_S: VAX ADDG/S
AXP_HOT AXP_FLATTEN
BoxResult execAddgS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_S: VAX SUBG/S
AXP_HOT AXP_FLATTEN
BoxResult execSubgS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_S: VAX MULG/S
AXP_HOT AXP_FLATTEN
BoxResult execMulgS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_S: VAX DIVG/S
AXP_HOT AXP_FLATTEN
BoxResult execDivgS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPGEQ_S: VAX CMPGEQ/S
AXP_HOT AXP_FLATTEN
BoxResult execCmpgeqS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPGLT_S: VAX CMPGLT/S
AXP_HOT AXP_FLATTEN
BoxResult execCmpgltS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPGLE_S: VAX CMPGLE/S
AXP_HOT AXP_FLATTEN
BoxResult execCmpgleS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_S: VAX CVTGF/S
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_S: VAX CVTGD/S
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_S: VAX CVTGQ/S
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_SUC: VAX ADDF/SUC
AXP_HOT AXP_FLATTEN
BoxResult execAddfSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_SUC: VAX SUBF/SUC
AXP_HOT AXP_FLATTEN
BoxResult execSubfSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_SUC: VAX MULF/SUC
AXP_HOT AXP_FLATTEN
BoxResult execMulfSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_SUC: VAX DIVF/SUC
AXP_HOT AXP_FLATTEN
BoxResult execDivfSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_SUC: VAX CVTDG/SUC
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_SUC: VAX ADDG/SUC
AXP_HOT AXP_FLATTEN
BoxResult execAddgSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_SUC: VAX SUBG/SUC
AXP_HOT AXP_FLATTEN
BoxResult execSubgSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_SUC: VAX MULG/SUC
AXP_HOT AXP_FLATTEN
BoxResult execMulgSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_SUC: VAX DIVG/SUC
AXP_HOT AXP_FLATTEN
BoxResult execDivgSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_SUC: VAX CVTGF/SUC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_SUC: VAX CVTGD/SUC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_SVC: VAX CVTGQ/SVC
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqSvc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDF_SU: VAX ADDF/SU
AXP_HOT AXP_FLATTEN
BoxResult execAddfSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBF_SU: VAX SUBF/SU
AXP_HOT AXP_FLATTEN
BoxResult execSubfSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULF_SU: VAX MULF/SU
AXP_HOT AXP_FLATTEN
BoxResult execMulfSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVF_SU: VAX DIVF/SU
AXP_HOT AXP_FLATTEN
BoxResult execDivfSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTDG_SU: VAX CVTDG/SU
AXP_HOT AXP_FLATTEN
BoxResult execCvtdgSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDG_SU: VAX ADDG/SU
AXP_HOT AXP_FLATTEN
BoxResult execAddgSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBG_SU: VAX SUBG/SU
AXP_HOT AXP_FLATTEN
BoxResult execSubgSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULG_SU: VAX MULG/SU
AXP_HOT AXP_FLATTEN
BoxResult execMulgSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVG_SU: VAX DIVG/SU
AXP_HOT AXP_FLATTEN
BoxResult execDivgSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGF_SU: VAX CVTGF/SU
AXP_HOT AXP_FLATTEN
BoxResult execCvtgfSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGD_SU: VAX CVTGD/SU
AXP_HOT AXP_FLATTEN
BoxResult execCvtgdSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTGQ_SV: VAX CVTGQ/SV
AXP_HOT AXP_FLATTEN
BoxResult execCvtgqSv(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDS_C: IEEE ADDS (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execAdds(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBS_C: IEEE SUBS (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execSubs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULS_C: IEEE MULS (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execMuls(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVS_C: IEEE DIVS (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execDivs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ADDT_C: IEEE ADDT (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execAddt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SUBT_C: IEEE SUBT (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execSubt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MULT_C: IEEE MULT (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execMult(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DIVT_C: IEEE DIVT (chopped)
AXP_HOT AXP_FLATTEN
BoxResult execDivt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_C: IEEE CVTTS/C
AXP_HOT AXP_FLATTEN
BoxResult execCvttsC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_C: IEEE CVTTQ/C
AXP_HOT AXP_FLATTEN
BoxResult execCvttqC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_C: IEEE CVTQS/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_C: IEEE CVTQT/C
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_M: IEEE CVTTS/M
AXP_HOT AXP_FLATTEN
BoxResult execCvttsM(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_M: IEEE CVTTQ/M
AXP_HOT AXP_FLATTEN
BoxResult execCvttqM(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_M: IEEE CVTQS/M
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsM(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_M: IEEE CVTQT/M
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtM(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTUN: IEEE compare unordered (T)
AXP_HOT AXP_FLATTEN
BoxResult execCmptun(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTEQ: IEEE compare equal (T)
AXP_HOT AXP_FLATTEN
BoxResult execCmpteq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTLT: IEEE compare less than (T)
AXP_HOT AXP_FLATTEN
BoxResult execCmptlt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTLE: IEEE compare less or equal (T)
AXP_HOT AXP_FLATTEN
BoxResult execCmptle(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS: Convert T_floating to S_floating
AXP_HOT AXP_FLATTEN
BoxResult execCvtts(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ: Convert T_floating to quadword
AXP_HOT AXP_FLATTEN
BoxResult execCvttq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS: Convert quadword to S_floating
AXP_HOT AXP_FLATTEN
BoxResult execCvtqs(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT: Convert quadword to T_floating
AXP_HOT AXP_FLATTEN
BoxResult execCvtqt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_D: IEEE CVTTS/D
AXP_HOT AXP_FLATTEN
BoxResult execCvttsD(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_D: IEEE CVTTQ/D
AXP_HOT AXP_FLATTEN
BoxResult execCvttqD(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_D: IEEE CVTQS/D
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsD(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_D: IEEE CVTQT/D
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtD(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_UC: IEEE CVTTS/UC
AXP_HOT AXP_FLATTEN
BoxResult execCvttsUc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_VC: IEEE CVTTQ/VC
AXP_HOT AXP_FLATTEN
BoxResult execCvttqVc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_UM: IEEE CVTTS/UM
AXP_HOT AXP_FLATTEN
BoxResult execCvttsUm(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_VM: IEEE CVTTQ/VM
AXP_HOT AXP_FLATTEN
BoxResult execCvttqVm(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_U: IEEE CVTTS/U
AXP_HOT AXP_FLATTEN
BoxResult execCvttsU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_V: IEEE CVTTQ/V
AXP_HOT AXP_FLATTEN
BoxResult execCvttqV(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_UD: IEEE CVTTS/UD
AXP_HOT AXP_FLATTEN
BoxResult execCvttsUd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_VD: IEEE CVTTQ/VD
AXP_HOT AXP_FLATTEN
BoxResult execCvttqVd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTST: Convert S_floating to T_floating
AXP_HOT AXP_FLATTEN
BoxResult execCvtst(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUC: IEEE CVTTS/SUC
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSuc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVC: IEEE CVTTQ/SVC
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvc(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUM: IEEE CVTTS/SUM
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSum(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVM: IEEE CVTTQ/SVM
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvm(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTUN_SU: IEEE CMPTUN/SU
AXP_HOT AXP_FLATTEN
BoxResult execCmptunSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTEQ_SU: IEEE CMPTEQ/SU
AXP_HOT AXP_FLATTEN
BoxResult execCmpteqSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTLT_SU: IEEE CMPTLT/SU
AXP_HOT AXP_FLATTEN
BoxResult execCmptltSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CMPTLE_SU: IEEE CMPTLE/SU
AXP_HOT AXP_FLATTEN
BoxResult execCmptleSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SU: IEEE CVTTS/SU
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SV: IEEE CVTTQ/SV
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSv(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUD: IEEE CVTTS/SUD
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSud(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVD: IEEE CVTTQ/SVD
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTST_S: IEEE CVTST/S
AXP_HOT AXP_FLATTEN
BoxResult execCvtstS(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUIC: IEEE CVTTS/SUIC
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSuic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVIC: IEEE CVTTQ/SVIC
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_SUIC: IEEE CVTQS/SUIC
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsSuic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_SUIC: IEEE CVTQT/SUIC
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtSuic(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUIM: IEEE CVTTS/SUIM
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSuim(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVIM: IEEE CVTTQ/SVIM
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvim(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_SUIM: IEEE CVTQS/SUIM
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsSuim(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_SUIM: IEEE CVTQT/SUIM
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtSuim(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUI: IEEE CVTTS/SUI
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSui(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVI: IEEE CVTTQ/SVI
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvi(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_SUI: IEEE CVTQS/SUI
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsSui(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_SUI: IEEE CVTQT/SUI
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtSui(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTS_SUID: IEEE CVTTS/SUID
AXP_HOT AXP_FLATTEN
BoxResult execCvttsSuid(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTTQ_SVID: IEEE CVTTQ/SVID
AXP_HOT AXP_FLATTEN
BoxResult execCvttqSvid(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQS_SUID: IEEE CVTQS/SUID
AXP_HOT AXP_FLATTEN
BoxResult execCvtqsSuid(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQT_SUID: IEEE CVTQT/SUID
AXP_HOT AXP_FLATTEN
BoxResult execCvtqtSuid(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTLQ: convert longword to quadword (FP-reg integer reformat)
AXP_HOT AXP_FLATTEN
BoxResult execCvtlq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CPYS: copy with sign of Ra
AXP_HOT AXP_FLATTEN
BoxResult execCpys(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CPYSN: copy with negated sign of Ra
AXP_HOT AXP_FLATTEN
BoxResult execCpysn(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CPYSE: copy sign+exp of Ra, fraction of Rb
AXP_HOT AXP_FLATTEN
BoxResult execCpyse(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MT_FPCR: move Ra to FPCR
AXP_HOT AXP_FLATTEN
BoxResult execMtFpcr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MF_FPCR: move FPCR to Rc
AXP_HOT AXP_FLATTEN
BoxResult execMfFpcr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FCMOVEQ: FP conditional move if Fa == 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFcmoveq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FCMOVNE: FP conditional move if Fa != 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFcmovne(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FCMOVLT: FP conditional move if Fa < 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFcmovlt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FCMOVGE: FP conditional move if Fa >= 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFcmovge(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FCMOVLE: FP conditional move if Fa <= 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFcmovle(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FCMOVGT: FP conditional move if Fa > 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFcmovgt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQL: convert quadword to longword (FP-reg integer reformat)
AXP_HOT AXP_FLATTEN
BoxResult execCvtql(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQL_V: convert quadword to longword, overflow enable
AXP_HOT AXP_FLATTEN
BoxResult execCvtqlV(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CVTQL_SV: convert quadword to longword, sw completion + overflow
AXP_HOT AXP_FLATTEN
BoxResult execCvtqlSv(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDF: VAX F_floating load: 32-bit memory -> 64-bit Fa register
AXP_HOT AXP_FLATTEN
BoxResult execLdf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDG: VAX G_floating load: 64-bit memory -> 64-bit Fa register
AXP_HOT AXP_FLATTEN
BoxResult execLdg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDS: IEEE S_floating load: 32-bit memory -> 64-bit Fa register (expanded)
AXP_HOT AXP_FLATTEN
BoxResult execLds(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDT: IEEE T_floating load: 64-bit memory -> 64-bit Fa register
AXP_HOT AXP_FLATTEN
BoxResult execLdt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STF: VAX F_floating store: 64-bit Fa register -> 32-bit memory
AXP_HOT AXP_FLATTEN
BoxResult execStf(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STG: VAX G_floating store: 64-bit Fa register -> 64-bit memory
AXP_HOT AXP_FLATTEN
BoxResult execStg(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STS: IEEE S_floating store: 64-bit Fa register -> 32-bit memory (collapsed)
AXP_HOT AXP_FLATTEN
BoxResult execSts(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STT: IEEE T_floating store: 64-bit Fa register -> 64-bit memory
AXP_HOT AXP_FLATTEN
BoxResult execStt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FBEQ: branch if Fa == 0.0 (signed zero counts)
AXP_HOT AXP_FLATTEN
BoxResult execFbeq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FBLT: branch if Fa < 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFblt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FBLE: branch if Fa <= 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFble(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FBNE: branch if Fa != 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFbne(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FBGE: branch if Fa >= 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFbge(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FBGT: branch if Fa > 0.0
AXP_HOT AXP_FLATTEN
BoxResult execFbgt(InstructionGrain const& g, ExecCtx const& c) noexcept;

} // namespace fBox

// ---------------------------------------------------------------------------
// Mbox -- namespace mBox
// ---------------------------------------------------------------------------
namespace mBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// LDA: load address: Ra <- Rb + sext(disp); no memory access
AXP_HOT AXP_FLATTEN
BoxResult execLda(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDAH: load address high: Ra <- Rb + (sext(disp) << 16); no memory access
AXP_HOT AXP_FLATTEN
BoxResult execLdah(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDBU: load byte unsigned: Ra <- zero_extend(Mem[EA]<7:0>)
AXP_HOT AXP_FLATTEN
BoxResult execLdbu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDQ_U: load quadword unaligned: EA force-aligned by clearing low 3 bits
AXP_HOT AXP_FLATTEN
BoxResult execLdqU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDWU: load word unsigned: Ra <- zero_extend(Mem[EA]<15:0>)
AXP_HOT AXP_FLATTEN
BoxResult execLdwu(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STW: 16-bit store (word)
AXP_HOT AXP_FLATTEN
BoxResult execStw(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STB: 8-bit store (byte)
AXP_HOT AXP_FLATTEN
BoxResult execStb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STQ_U: store quadword unaligned: EA force-aligned by clearing low 3 bits
AXP_HOT AXP_FLATTEN
BoxResult execStqU(InstructionGrain const& g, ExecCtx const& c) noexcept;

// FETCH: prefetch hint
AXP_HOT AXP_FLATTEN
BoxResult execFetch(InstructionGrain const& g, ExecCtx const& c) noexcept;

// HW_LD: PALmode load with hint bits
AXP_HOT AXP_FLATTEN
BoxResult execHwLd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// HW_ST: PALmode store with hint bits
AXP_HOT AXP_FLATTEN
BoxResult execHwSt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDL: sign-extended 32-bit load: Ra <- sext_32(Mem[EA])
AXP_HOT AXP_FLATTEN
BoxResult execLdl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDQ: 64-bit aligned load
AXP_HOT AXP_FLATTEN
BoxResult execLdq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDL_L: sign-extended longword load locked
AXP_HOT AXP_FLATTEN
BoxResult execLdlL(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDQ_L: quadword load locked
AXP_HOT AXP_FLATTEN
BoxResult execLdqL(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STL: 32-bit aligned store
AXP_HOT AXP_FLATTEN
BoxResult execStl(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STQ: 64-bit aligned store
AXP_HOT AXP_FLATTEN
BoxResult execStq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STL_C: store longword conditional; writes Ra with success indicator
AXP_HOT AXP_FLATTEN
BoxResult execStlC(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STQ_C: store quadword conditional; writes Ra with success indicator
AXP_HOT AXP_FLATTEN
BoxResult execStqC(InstructionGrain const& g, ExecCtx const& c) noexcept;

} // namespace mBox

// ---------------------------------------------------------------------------
// Cbox -- namespace cBox
// ---------------------------------------------------------------------------
namespace cBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// TRAPB: trap barrier; serializes precise traps
AXP_HOT AXP_FLATTEN
BoxResult execTrapb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// EXCB: exception barrier
AXP_HOT AXP_FLATTEN
BoxResult execExcb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MB: memory barrier
AXP_HOT AXP_FLATTEN
BoxResult execMb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WMB: write memory barrier
AXP_HOT AXP_FLATTEN
BoxResult execWmb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// ECB: evict cache block
AXP_HOT AXP_FLATTEN
BoxResult execEcb(InstructionGrain const& g, ExecCtx const& c) noexcept;

} // namespace cBox

// ---------------------------------------------------------------------------
// PalBox -- namespace palBox
// ---------------------------------------------------------------------------
namespace palBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// HALT: halt processor; 3 personalities (AARM C-15: HALT/halt/halt)
AXP_HOT AXP_FLATTEN
BoxResult execHalt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CFLUSH: cache flush; 3 personalities (VMS:CFLUSH, Tru64+Linux:cflush)
AXP_HOT AXP_FLATTEN
BoxResult execCflush(InstructionGrain const& g, ExecCtx const& c) noexcept;

// DRAINA: drain aborts; 3 personalities (VMS:DRAINA, Tru64+Linux:draina); REQUIRED per AARM C-16
AXP_HOT AXP_FLATTEN
BoxResult execDraina(InstructionGrain const& g, ExecCtx const& c) noexcept;

// LDQP: load quadword physical intrinsic; R0 := mem[R16] (8 bytes, physical addressing); VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
BoxResult execLdqp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// STQP: store quadword physical intrinsic; mem[R16] := R17 (8 bytes, physical addressing); VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
BoxResult execStqp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SWPCTX: swap process context (VMS); R16=new HWPCB PA, R0=old PTBR; distinct from Tru64 swpctx at 0x30 (DIFFERENT opcode); v1 leaf is forward-looking stub (palBoxLib/grains/PalEntries.cpp execSwpctx_vms) -- needs CpuState shadow regs + leaf-side memory accessor
AXP_HOT AXP_FLATTEN
BoxResult execSwpctx_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_ASN: read processor register ASN (address space number)
AXP_HOT AXP_FLATTEN
BoxResult execMfprAsn_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_ASTEN: write processor register ASTEN (AST enable)
AXP_HOT AXP_FLATTEN
BoxResult execMtprAsten_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_ASTSR: write processor register ASTSR (AST summary)
AXP_HOT AXP_FLATTEN
BoxResult execMtprAstsr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CSERVE: console service intrinsic; inline-executed, no PAL transfer; R16=func, R0=result; 3 personalities (VMS:CSERVE, Tru64+Linux:cserve)
AXP_HOT AXP_FLATTEN
BoxResult execCserve(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SWPPAL: swap PALcode image; 3 personalities (VMS:SWPPAL, Tru64+Linux:swppal)
AXP_HOT AXP_FLATTEN
BoxResult execSwppal(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_FEN: read processor register FEN (floating-point enable)
AXP_HOT AXP_FLATTEN
BoxResult execMfprFen_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_FEN: write processor register FEN (floating-point enable)
AXP_HOT AXP_FLATTEN
BoxResult execMtprFen_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_IPIR: write inter-processor interrupt request (VMS:MTPR_IPIR / Tru64+Linux:wripir); same operation, three personalities
AXP_HOT AXP_FLATTEN
BoxResult execMtprIpir(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_IPL: read processor register IPL (interrupt priority level)
AXP_HOT AXP_FLATTEN
BoxResult execMfprIpl_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_IPL: write processor register IPL (interrupt priority level)
AXP_HOT AXP_FLATTEN
BoxResult execMtprIpl_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_MCES: read machine check error summary (VMS:MFPR_MCES / Tru64+Linux:rdmces); same operation, three personalities
AXP_HOT AXP_FLATTEN
BoxResult execMfprMces(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_MCES: write machine check error summary (VMS:MTPR_MCES / Tru64+Linux:wrmces); same operation, three personalities
AXP_HOT AXP_FLATTEN
BoxResult execMtprMces(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_PCBB: read process control block base
AXP_HOT AXP_FLATTEN
BoxResult execMfprPcbb_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_PRBR: read processor base register
AXP_HOT AXP_FLATTEN
BoxResult execMfprPrbr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_PRBR: write processor base register
AXP_HOT AXP_FLATTEN
BoxResult execMtprPrbr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_PTBR: read page table base register
AXP_HOT AXP_FLATTEN
BoxResult execMfprPtbr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_SCBB: read system control block base; V4 v1 intrinsic returns cpu.scbb in R0 (palBoxLib/grains/PalEntries.cpp execMfprScbb); SCB layout in deviceLib/Scb.h per AARM 14.6
AXP_HOT AXP_FLATTEN
BoxResult execMfprScbb_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_SCBB: write system control block base; V4 v1 intrinsic stores R16 into cpu.scbb (palBoxLib/grains/PalEntries.cpp execMtprScbb)
AXP_HOT AXP_FLATTEN
BoxResult execMtprScbb_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_SIRR: write software interrupt request
AXP_HOT AXP_FLATTEN
BoxResult execMtprSirr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_SISR: read software interrupt summary
AXP_HOT AXP_FLATTEN
BoxResult execMfprSisr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_TBCHK: check translation buffer for entry
AXP_HOT AXP_FLATTEN
BoxResult execMfprTbchk_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_TBIA: invalidate all translation buffer entries
AXP_HOT AXP_FLATTEN
BoxResult execMtprTbia_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_TBIAP: invalidate all process TB entries (ASM=0)
AXP_HOT AXP_FLATTEN
BoxResult execMtprTbiap_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_TBIS: invalidate single TB entry
AXP_HOT AXP_FLATTEN
BoxResult execMtprTbis_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_ESP: read executive stack pointer
AXP_HOT AXP_FLATTEN
BoxResult execMfprEsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_ESP: write executive stack pointer
AXP_HOT AXP_FLATTEN
BoxResult execMtprEsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_SSP: read supervisor stack pointer
AXP_HOT AXP_FLATTEN
BoxResult execMfprSsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_SSP: write supervisor stack pointer
AXP_HOT AXP_FLATTEN
BoxResult execMtprSsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_USP: read user stack pointer (VMS); distinct from Tru64 RDUSP at 0x3A (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execMfprUsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_USP: write user stack pointer (VMS); distinct from Tru64 WRUSP at 0x38 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execMtprUsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_TBISD: invalidate single TB entry (data stream)
AXP_HOT AXP_FLATTEN
BoxResult execMtprTbisd_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_TBISI: invalidate single TB entry (instruction stream)
AXP_HOT AXP_FLATTEN
BoxResult execMtprTbisi_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_ASTEN: read AST enable register
AXP_HOT AXP_FLATTEN
BoxResult execMfprAsten_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_ASTSR: read AST summary register
AXP_HOT AXP_FLATTEN
BoxResult execMfprAstsr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_VPTB: read Virtual Page Table Base intrinsic; R0 := cpu.vptb; VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
BoxResult execMfprVptb_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_VPTB: write Virtual Page Table Base intrinsic; cpu.vptb := R16; VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
BoxResult execMtprVptb_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_PERFMON: VMS:MTPR_PERFMON (write performance monitor) / Tru64+Linux:wrfen (write FP enable) -- DIFFERENT operations same opcode 0x2B; runtime must dispatch on personality
AXP_HOT AXP_FLATTEN
BoxResult execMtprPerfmon(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRVPTPTR: write virtual page table pointer (Tru64+Linux); no VMS counterpart at 0x2D
AXP_HOT AXP_FLATTEN
BoxResult execWrvptptr_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MTPR_DATFX: VMS:MTPR_DATFX (write data align trap fixup enable) / Tru64:wrasn (write ASN); Linux: not supported -- DIFFERENT operations same opcode 0x2E
AXP_HOT AXP_FLATTEN
BoxResult execMtprDatfx(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_VIRBND: VMS:MFPR_VIRBND (read virtual addr boundary; optional ext, AARM 13.3.24; AARM TOC has typo MFPT_VIRBND, normalized) / Tru64+Linux:swpctx (swap process context) -- DIFFERENT operations same opcode 0x30
AXP_HOT AXP_FLATTEN
BoxResult execMfprVirbnd(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRVAL: write system value (Tru64+Linux); no VMS counterpart at 0x31
AXP_HOT AXP_FLATTEN
BoxResult execWrval_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_SYSPTBR: VMS:MFPR_SYSPTBR (read system page table base; optional ext, AARM 13.3.18) / Tru64+Linux:rdval (read system value) -- DIFFERENT operations same opcode 0x32
AXP_HOT AXP_FLATTEN
BoxResult execMfprSysptbr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// TBI: translation buffer invalidate (Tru64+Linux); no VMS counterpart at 0x33
AXP_HOT AXP_FLATTEN
BoxResult execTbi_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRENT: write system entry vector (Tru64+Linux); no VMS counterpart at 0x34
AXP_HOT AXP_FLATTEN
BoxResult execWrent_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SWPIPL: swap interrupt priority level (Tru64+Linux); no VMS counterpart at 0x35
AXP_HOT AXP_FLATTEN
BoxResult execSwpipl_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RDPS: read processor status (Tru64+Linux); distinct from VMS unprivileged RD_PS at 0x91 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execRdps_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRKGP: write kernel global pointer (Tru64+Linux); no VMS counterpart at 0x37
AXP_HOT AXP_FLATTEN
BoxResult execWrkgp_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRUSP: write user stack pointer (Tru64+Linux); distinct from VMS MTPR_USP at 0x23 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execWrusp_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRPERFMON: write performance monitor (Tru64+Linux); no VMS counterpart at 0x39
AXP_HOT AXP_FLATTEN
BoxResult execWrperfmon_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RDUSP: read user stack pointer (Tru64+Linux); distinct from VMS MFPR_USP at 0x22 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execRdusp_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WHAMI: read CPU ID (Tru64+Linux); divert to PAL, body returns whami in R0; distinct from VMS MFPR_WHAMI at 0x3F (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execWhami_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RETSYS: return from system call (Tru64+Linux); no VMS counterpart at 0x3D
AXP_HOT AXP_FLATTEN
BoxResult execRetsys_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WTINT: wait for interrupt intrinsic; 3 personalities (AARM C-15: WTINT/wtint/wtint); V4 has no interrupt source so returns 0 in R0 immediately
AXP_HOT AXP_FLATTEN
BoxResult execWtint(InstructionGrain const& g, ExecCtx const& c) noexcept;

// MFPR_WHAMI: VMS:MFPR_WHAMI (read CPU ID) / Tru64+Linux:rti (return from trap/interrupt) -- DIFFERENT operations same opcode 0x3F; V4 v1 has VMS WHAMI intrinsic at this opcode (palBoxLib/grains/PalEntries.cpp execMfprWhami) -- Tru64/Linux RTI not yet implemented
AXP_HOT AXP_FLATTEN
BoxResult execMfprWhami(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BPT: breakpoint trap; 3 personalities (AARM C-15: BPT/bpt/bpt)
AXP_HOT AXP_FLATTEN
BoxResult execBpt(InstructionGrain const& g, ExecCtx const& c) noexcept;

// BUGCHK: bug check (VMS only)
AXP_HOT AXP_FLATTEN
BoxResult execBugchk_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CHME: change mode to executive (VMS only)
AXP_HOT AXP_FLATTEN
BoxResult execChme_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CHMK: change mode to kernel (Tru64+Linux: callsys) / VMS: CHMK; 3 personalities at opcode 0x83
AXP_HOT AXP_FLATTEN
BoxResult execChmk(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CHMS: change mode to supervisor (VMS only)
AXP_HOT AXP_FLATTEN
BoxResult execChms_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CHMU: change mode to user (VMS only)
AXP_HOT AXP_FLATTEN
BoxResult execChmu_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// IMB: instruction memory barrier; 3 personalities (VMS:IMB, Tru64+Linux:imb); REQUIRED per AARM C-16
AXP_HOT AXP_FLATTEN
BoxResult execImb(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQHIL: insert into queue at head, longword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execInsqhil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQTIL: insert into queue at tail, longword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execInsqtil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQHIQ: insert into queue at head, quadword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execInsqhiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQTIQ: insert into queue at tail, quadword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execInsqtiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQUEL: insert into queue, longword
AXP_HOT AXP_FLATTEN
BoxResult execInsquel_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQUEQ: insert into queue, quadword
AXP_HOT AXP_FLATTEN
BoxResult execInsqueq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQUEL_D: insert into queue (deferred), longword (AARM mnemonic: INSQUEL/D; slash replaced with underscore for codegen)
AXP_HOT AXP_FLATTEN
BoxResult execInsquelD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQUEQ_D: insert into queue (deferred), quadword (AARM mnemonic: INSQUEQ/D)
AXP_HOT AXP_FLATTEN
BoxResult execInsqueqD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// PROBER: probe for read access
AXP_HOT AXP_FLATTEN
BoxResult execProber_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// PROBEW: probe for write access
AXP_HOT AXP_FLATTEN
BoxResult execProbew_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RD_PS: read processor status (VMS unprivileged); distinct from Tru64 RDPS at 0x36 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
BoxResult execRdPs_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REI: VMS:REI (return from exception/interrupt) / Tru64:urti (user-level RTI); Linux: not supported -- DIFFERENT operations same opcode 0x92
AXP_HOT AXP_FLATTEN
BoxResult execRei(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQHIL: remove from queue at head, longword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execRemqhil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQTIL: remove from queue at tail, longword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execRemqtil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQHIQ: remove from queue at head, quadword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execRemqhiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQTIQ: remove from queue at tail, quadword (interlocked)
AXP_HOT AXP_FLATTEN
BoxResult execRemqtiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQUEL: remove from queue, longword
AXP_HOT AXP_FLATTEN
BoxResult execRemquel_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQUEQ: remove from queue, quadword
AXP_HOT AXP_FLATTEN
BoxResult execRemqueq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQUEL_D: remove from queue (deferred), longword (AARM mnemonic: REMQUEL/D)
AXP_HOT AXP_FLATTEN
BoxResult execRemquelD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQUEQ_D: remove from queue (deferred), quadword (AARM mnemonic: REMQUEQ/D)
AXP_HOT AXP_FLATTEN
BoxResult execRemqueqD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// SWASTEN: swap AST enable
AXP_HOT AXP_FLATTEN
BoxResult execSwasten_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WR_PS_SW: write processor status software bits
AXP_HOT AXP_FLATTEN
BoxResult execWrPsSw_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// RSCC: read system cycle counter
AXP_HOT AXP_FLATTEN
BoxResult execRscc_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// READ_UNQ: read process unique value (VMS:READ_UNQ / Tru64+Linux:rdunique); same operation, three personalities; thread-local storage primitive
AXP_HOT AXP_FLATTEN
BoxResult execReadUnq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// WRITE_UNQ: write process unique value (VMS:WRITE_UNQ / Tru64+Linux:wrunique); same operation, three personalities; thread-local storage primitive
AXP_HOT AXP_FLATTEN
BoxResult execWriteUnq(InstructionGrain const& g, ExecCtx const& c) noexcept;

// AMOVRR: atomic move register-to-register
AXP_HOT AXP_FLATTEN
BoxResult execAmovrr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// AMOVRM: atomic move register-to-memory
AXP_HOT AXP_FLATTEN
BoxResult execAmovrm_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQHILR: insert at head, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execInsqhilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQTILR: insert at tail, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execInsqtilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQHIQR: insert at head, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execInsqhiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// INSQTIQR: insert at tail, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execInsqtiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQHILR: remove at head, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execRemqhilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQTILR: remove at tail, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execRemqtilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQHIQR: remove at head, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execRemqhiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// REMQTIQR: remove at tail, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
BoxResult execRemqtiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// GENTRAP: generate software trap; 3 personalities (VMS:GENTRAP, Tru64+Linux:gentrap)
AXP_HOT AXP_FLATTEN
BoxResult execGentrap(InstructionGrain const& g, ExecCtx const& c) noexcept;

// CLRFEN: clear floating-point enable; 3 personalities (VMS:CLRFEN, Tru64+Linux:clrfen)
AXP_HOT AXP_FLATTEN
BoxResult execClrfen(InstructionGrain const& g, ExecCtx const& c) noexcept;

// HW_MFPR: read internal processor register
AXP_HOT AXP_FLATTEN
BoxResult execHwMfpr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// HW_MTPR: write internal processor register
AXP_HOT AXP_FLATTEN
BoxResult execHwMtpr(InstructionGrain const& g, ExecCtx const& c) noexcept;

// HW_REI: return from PAL; resume EXC_ADDR
AXP_HOT AXP_FLATTEN
BoxResult execHwRei(InstructionGrain const& g, ExecCtx const& c) noexcept;

// execBpt_tru64: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
BoxResult execBpt_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

// execBpt_vms: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
BoxResult execBpt_vms(InstructionGrain const& g, ExecCtx const& c) noexcept;

// execCallPalDispatch: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
BoxResult execCallPalDispatch(InstructionGrain const& g, ExecCtx const& c) noexcept;

// execChmk_tru64: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
BoxResult execChmk_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept;

} // namespace palBox
