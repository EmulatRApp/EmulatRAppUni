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
void execJmp(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// JSR: indirect call
AXP_HOT AXP_FLATTEN
void execJsr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RET: return; hint that target was pushed by JSR
AXP_HOT AXP_FLATTEN
void execRet(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// JSR_COROUTINE: coroutine swap
AXP_HOT AXP_FLATTEN
void execJsrCoroutine(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BR: unconditional branch with link in Ra
AXP_HOT AXP_FLATTEN
void execBr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BSR: branch to subroutine; pushes return PC
AXP_HOT AXP_FLATTEN
void execBsr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BLBC: branch if low bit of Ra is clear
AXP_HOT AXP_FLATTEN
void execBlbc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BEQ: branch if Ra == 0
AXP_HOT AXP_FLATTEN
void execBeq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BLT: branch if Ra < 0
AXP_HOT AXP_FLATTEN
void execBlt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BLE: branch if Ra <= 0 (signed)
AXP_HOT AXP_FLATTEN
void execBle(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BLBS: branch if low bit of Ra is set
AXP_HOT AXP_FLATTEN
void execBlbs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BNE: branch if Ra != 0
AXP_HOT AXP_FLATTEN
void execBne(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BGE: branch if Ra >= 0
AXP_HOT AXP_FLATTEN
void execBge(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BGT: branch if Ra > 0 (signed)
AXP_HOT AXP_FLATTEN
void execBgt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

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
void execAddl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S4ADDL: scaled add longword: Rc = sext((Ra*4 + Rb)<31:0>)
AXP_HOT AXP_FLATTEN
void execS4addl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBL: 32-bit subtract
AXP_HOT AXP_FLATTEN
void execSubl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S4SUBL: scaled sub longword: Rc = sext((Ra*4 - Rb)<31:0>)
AXP_HOT AXP_FLATTEN
void execS4subl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPBGE: per-byte unsigned compare; 8-bit result in low byte of Rc
AXP_HOT AXP_FLATTEN
void execCmpbge(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S8ADDL: scaled add longword: Rc = sext((Ra*8 + Rb)<31:0>)
AXP_HOT AXP_FLATTEN
void execS8addl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S8SUBL: scaled sub longword: Rc = sext((Ra*8 - Rb)<31:0>)
AXP_HOT AXP_FLATTEN
void execS8subl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPULT: compare unsigned less than
AXP_HOT AXP_FLATTEN
void execCmpult(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDQ: 64-bit add
AXP_HOT AXP_FLATTEN
void execAddq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S4ADDQ: scaled add quadword: Rc = Ra*4 + Rb
AXP_HOT AXP_FLATTEN
void execS4addq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBQ: 64-bit subtract
AXP_HOT AXP_FLATTEN
void execSubq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S4SUBQ: scaled sub quadword: Rc = Ra*4 - Rb
AXP_HOT AXP_FLATTEN
void execS4subq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPEQ: compare equal
AXP_HOT AXP_FLATTEN
void execCmpeq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S8ADDQ: scaled add quadword: Rc = Ra*8 + Rb
AXP_HOT AXP_FLATTEN
void execS8addq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// S8SUBQ: scaled sub quadword: Rc = Ra*8 - Rb
AXP_HOT AXP_FLATTEN
void execS8subq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPULE: compare unsigned less or equal
AXP_HOT AXP_FLATTEN
void execCmpule(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDL_V: ADDL/V: 32-bit add, same stored result as ADDL; signals IOV on overflow (AARM 4.4.1)
AXP_HOT AXP_FLATTEN
void execAddlV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBL_V: SUBL/V: 32-bit subtract, same stored result as SUBL; signals IOV on overflow (AARM 4.4.15)
AXP_HOT AXP_FLATTEN
void execSublV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPLT: compare signed less than
AXP_HOT AXP_FLATTEN
void execCmplt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDQ_V: ADDQ/V: 64-bit add, same stored result as ADDQ; signals IOV on two's-complement overflow (AARM 4.4.2)
AXP_HOT AXP_FLATTEN
void execAddqV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBQ_V: SUBQ/V: 64-bit subtract, same stored result as SUBQ; signals IOV on two's-complement overflow (AARM 4.4.14)
AXP_HOT AXP_FLATTEN
void execSubqV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPLE: compare signed less or equal
AXP_HOT AXP_FLATTEN
void execCmple(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// AND: bitwise AND
AXP_HOT AXP_FLATTEN
void execAnd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BIC: bit clear (AND NOT): Rc = Ra & ~Rb
AXP_HOT AXP_FLATTEN
void execBic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVLBS: conditional move if Ra<0> set; else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovlbs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVLBC: conditional move if Ra<0> clear; else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovlbc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BIS: OR; canonical MOV pseudo-op (bis Rs, R31, Rd)
AXP_HOT AXP_FLATTEN
void execBis(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVEQ: conditional move if Ra == 0; else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmoveq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVNE: conditional move if Ra != 0; else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovne(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ORNOT: OR NOT: Rc = Ra | ~Rb
AXP_HOT AXP_FLATTEN
void execOrnot(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// XOR: bitwise XOR
AXP_HOT AXP_FLATTEN
void execXor(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVLT: conditional move if Ra < 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovlt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVGE: conditional move if Ra >= 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovge(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EQV: equivalence (XNOR): Rc = ~(Ra ^ Rb)
AXP_HOT AXP_FLATTEN
void execEqv(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// AMASK: architecture mask: Rc <- Rb & ~supported_features
AXP_HOT AXP_FLATTEN
void execAmask(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVLE: conditional move if Ra <= 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovle(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMOVGT: conditional move if Ra > 0 (signed); else Rc unchanged
AXP_HOT AXP_FLATTEN
void execCmovgt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// IMPLVER: report processor implementation version (EV6=2)
AXP_HOT AXP_FLATTEN
void execImplver(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKBL: mask byte low
AXP_HOT AXP_FLATTEN
void execMskbl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTBL: extract byte low
AXP_HOT AXP_FLATTEN
void execExtbl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSBL: insert byte low
AXP_HOT AXP_FLATTEN
void execInsbl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKWL: mask word low
AXP_HOT AXP_FLATTEN
void execMskwl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTWL: extract word low
AXP_HOT AXP_FLATTEN
void execExtwl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSWL: insert word low
AXP_HOT AXP_FLATTEN
void execInswl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKLL: mask longword low
AXP_HOT AXP_FLATTEN
void execMskll(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTLL: extract longword low
AXP_HOT AXP_FLATTEN
void execExtll(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSLL: insert longword low
AXP_HOT AXP_FLATTEN
void execInsll(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ZAP: zero per-byte where Rb<i> set
AXP_HOT AXP_FLATTEN
void execZap(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ZAPNOT: zero per-byte where Rb<i> clear
AXP_HOT AXP_FLATTEN
void execZapnot(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKQL: mask quadword low
AXP_HOT AXP_FLATTEN
void execMskql(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SRL: shift right logical
AXP_HOT AXP_FLATTEN
void execSrl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTQL: extract quadword low
AXP_HOT AXP_FLATTEN
void execExtql(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SLL: shift left logical
AXP_HOT AXP_FLATTEN
void execSll(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQL: insert quadword low
AXP_HOT AXP_FLATTEN
void execInsql(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SRA: shift right arithmetic
AXP_HOT AXP_FLATTEN
void execSra(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKWH: mask word high
AXP_HOT AXP_FLATTEN
void execMskwh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSWH: insert word high
AXP_HOT AXP_FLATTEN
void execInswh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTWH: extract word high
AXP_HOT AXP_FLATTEN
void execExtwh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKLH: mask longword high
AXP_HOT AXP_FLATTEN
void execMsklh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSLH: insert longword high
AXP_HOT AXP_FLATTEN
void execInslh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTLH: extract longword high
AXP_HOT AXP_FLATTEN
void execExtlh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MSKQH: mask quadword high
AXP_HOT AXP_FLATTEN
void execMskqh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQH: insert quadword high
AXP_HOT AXP_FLATTEN
void execInsqh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXTQH: extract quadword high
AXP_HOT AXP_FLATTEN
void execExtqh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULL: 32-bit multiply
AXP_HOT AXP_FLATTEN
void execMull(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULQ: 64-bit multiply
AXP_HOT AXP_FLATTEN
void execMulq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// UMULH: unsigned multiply high (upper 64 bits of 128-bit product)
AXP_HOT AXP_FLATTEN
void execUmulh(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULL_V: MULL/V: 32-bit multiply, same stored result as MULL; signals IOV on overflow (AARM 4.4.11)
AXP_HOT AXP_FLATTEN
void execMullV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULQ_V: MULQ/V: 64-bit multiply, same stored result as MULQ; signals IOV when 128-bit product high half is not the sign-extension of the low half (AARM 4.4.13)
AXP_HOT AXP_FLATTEN
void execMulqV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ITOFS: integer -> FP single-precision move
AXP_HOT AXP_FLATTEN
void execItofs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ITOFF: integer -> FP F_floating move
AXP_HOT AXP_FLATTEN
void execItoff(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ITOFT: integer -> FP T-format move
AXP_HOT AXP_FLATTEN
void execItoft(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RPCC: read cycle counter; non-deterministic
AXP_HOT AXP_FLATTEN
void execRpcc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RC: read-and-set intrFlag IPR; old value -> Ra
AXP_HOT AXP_FLATTEN
void execRc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RS: read-and-clear intrFlag IPR; old value -> Ra
AXP_HOT AXP_FLATTEN
void execRs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SEXTB: sign-extend byte
AXP_HOT AXP_FLATTEN
void execSextb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SEXTW: sign-extend word
AXP_HOT AXP_FLATTEN
void execSextw(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CTPOP: count population
AXP_HOT AXP_FLATTEN
void execCtpop(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// PERR: pixel error: sum of abs byte differences across 8 lanes
AXP_HOT AXP_FLATTEN
void execPerr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CTLZ: count leading zeros
AXP_HOT AXP_FLATTEN
void execCtlz(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CTTZ: count trailing zeros
AXP_HOT AXP_FLATTEN
void execCttz(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// UNPKBW: unpack low 4 bytes of Rb into 4 zero-extended 16-bit lanes
AXP_HOT AXP_FLATTEN
void execUnpkbw(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// UNPKBL: unpack low 2 bytes of Rb into 2 zero-extended 32-bit lanes
AXP_HOT AXP_FLATTEN
void execUnpkbl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// PKWB: pack low byte of each 16-bit lane of Rb into low halfword
AXP_HOT AXP_FLATTEN
void execPkwb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// PKLB: pack low byte of each 32-bit lane of Rb into low halfword
AXP_HOT AXP_FLATTEN
void execPklb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MINSB8: per-lane signed min, 8 byte lanes
AXP_HOT AXP_FLATTEN
void execMinsb8(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MINSW4: per-lane signed min, 4 word lanes
AXP_HOT AXP_FLATTEN
void execMinsw4(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MINUB8: per-lane unsigned min, 8 byte lanes
AXP_HOT AXP_FLATTEN
void execMinub8(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MINUW4: per-lane unsigned min, 4 word lanes
AXP_HOT AXP_FLATTEN
void execMinuw4(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MAXUB8: per-lane unsigned max, 8 byte lanes
AXP_HOT AXP_FLATTEN
void execMaxub8(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MAXUW4: per-lane unsigned max, 4 word lanes
AXP_HOT AXP_FLATTEN
void execMaxuw4(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MAXSB8: per-lane signed max, 8 byte lanes
AXP_HOT AXP_FLATTEN
void execMaxsb8(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MAXSW4: per-lane signed max, 4 word lanes
AXP_HOT AXP_FLATTEN
void execMaxsw4(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FTOIT: FP T-format -> integer register move
AXP_HOT AXP_FLATTEN
void execFtoit(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FTOIS: FP S-format -> integer register move
AXP_HOT AXP_FLATTEN
void execFtois(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

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
void execSqrtfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_C: IEEE SQRTS/C
AXP_HOT AXP_FLATTEN
void execSqrtsC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_C: VAX SQRTG/C
AXP_HOT AXP_FLATTEN
void execSqrtgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_C: IEEE SQRTT/C
AXP_HOT AXP_FLATTEN
void execSqrttC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_M: IEEE SQRTS/M
AXP_HOT AXP_FLATTEN
void execSqrtsM(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_M: IEEE SQRTT/M
AXP_HOT AXP_FLATTEN
void execSqrttM(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF: VAX SQRTF
AXP_HOT AXP_FLATTEN
void execSqrtf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS: Square root S_floating
AXP_HOT AXP_FLATTEN
void execSqrts(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG: VAX SQRTG
AXP_HOT AXP_FLATTEN
void execSqrtg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT: Square root T_floating
AXP_HOT AXP_FLATTEN
void execSqrtt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_D: IEEE SQRTS/D
AXP_HOT AXP_FLATTEN
void execSqrtsD(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_D: IEEE SQRTT/D
AXP_HOT AXP_FLATTEN
void execSqrttD(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF_UC: VAX SQRTF/UC
AXP_HOT AXP_FLATTEN
void execSqrtfUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_UC: IEEE SQRTS/UC
AXP_HOT AXP_FLATTEN
void execSqrtsUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_UC: VAX SQRTG/UC
AXP_HOT AXP_FLATTEN
void execSqrtgUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_UC: IEEE SQRTT/UC
AXP_HOT AXP_FLATTEN
void execSqrttUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_UM: IEEE SQRTS/UM
AXP_HOT AXP_FLATTEN
void execSqrtsUm(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_UM: IEEE SQRTT/UM
AXP_HOT AXP_FLATTEN
void execSqrttUm(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF_U: VAX SQRTF/U
AXP_HOT AXP_FLATTEN
void execSqrtfU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_U: IEEE SQRTS/U
AXP_HOT AXP_FLATTEN
void execSqrtsU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_U: VAX SQRTG/U
AXP_HOT AXP_FLATTEN
void execSqrtgU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_U: IEEE SQRTT/U
AXP_HOT AXP_FLATTEN
void execSqrttU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_UD: IEEE SQRTS/UD
AXP_HOT AXP_FLATTEN
void execSqrtsUd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_UD: IEEE SQRTT/UD
AXP_HOT AXP_FLATTEN
void execSqrttUd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF_SC: VAX SQRTF/SC
AXP_HOT AXP_FLATTEN
void execSqrtfSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_SC: VAX SQRTG/SC
AXP_HOT AXP_FLATTEN
void execSqrtgSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF_S: VAX SQRTF/S
AXP_HOT AXP_FLATTEN
void execSqrtfS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_S: VAX SQRTG/S
AXP_HOT AXP_FLATTEN
void execSqrtgS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF_SUC: VAX SQRTF/SUC
AXP_HOT AXP_FLATTEN
void execSqrtfSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUC: IEEE SQRTS/SUC
AXP_HOT AXP_FLATTEN
void execSqrtsSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_SUC: VAX SQRTG/SUC
AXP_HOT AXP_FLATTEN
void execSqrtgSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUC: IEEE SQRTT/SUC
AXP_HOT AXP_FLATTEN
void execSqrttSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUM: IEEE SQRTS/SUM
AXP_HOT AXP_FLATTEN
void execSqrtsSum(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUM: IEEE SQRTT/SUM
AXP_HOT AXP_FLATTEN
void execSqrttSum(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTF_SU: VAX SQRTF/SU
AXP_HOT AXP_FLATTEN
void execSqrtfSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SU: IEEE SQRTS/SU
AXP_HOT AXP_FLATTEN
void execSqrtsSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTG_SU: VAX SQRTG/SU
AXP_HOT AXP_FLATTEN
void execSqrtgSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SU: IEEE SQRTT/SU
AXP_HOT AXP_FLATTEN
void execSqrttSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUD: IEEE SQRTS/SUD
AXP_HOT AXP_FLATTEN
void execSqrtsSud(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUD: IEEE SQRTT/SUD
AXP_HOT AXP_FLATTEN
void execSqrttSud(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUIC: IEEE SQRTS/SUIC
AXP_HOT AXP_FLATTEN
void execSqrtsSuic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUIC: IEEE SQRTT/SUIC
AXP_HOT AXP_FLATTEN
void execSqrttSuic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUIM: IEEE SQRTS/SUIM
AXP_HOT AXP_FLATTEN
void execSqrtsSuim(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUIM: IEEE SQRTT/SUIM
AXP_HOT AXP_FLATTEN
void execSqrttSuim(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUI: IEEE SQRTS/SUI
AXP_HOT AXP_FLATTEN
void execSqrtsSui(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUI: IEEE SQRTT/SUI
AXP_HOT AXP_FLATTEN
void execSqrttSui(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTS_SUID: IEEE SQRTS/SUID
AXP_HOT AXP_FLATTEN
void execSqrtsSuid(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SQRTT_SUID: IEEE SQRTT/SUID
AXP_HOT AXP_FLATTEN
void execSqrttSuid(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_C: VAX ADDF/C
AXP_HOT AXP_FLATTEN
void execAddfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_C: VAX SUBF/C
AXP_HOT AXP_FLATTEN
void execSubfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_C: VAX MULF/C
AXP_HOT AXP_FLATTEN
void execMulfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_C: VAX DIVF/C
AXP_HOT AXP_FLATTEN
void execDivfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_C: VAX CVTDG/C
AXP_HOT AXP_FLATTEN
void execCvtdgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_C: VAX ADDG/C
AXP_HOT AXP_FLATTEN
void execAddgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_C: VAX SUBG/C
AXP_HOT AXP_FLATTEN
void execSubgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_C: VAX MULG/C
AXP_HOT AXP_FLATTEN
void execMulgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_C: VAX DIVG/C
AXP_HOT AXP_FLATTEN
void execDivgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_C: VAX CVTGF/C
AXP_HOT AXP_FLATTEN
void execCvtgfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_C: VAX CVTGD/C
AXP_HOT AXP_FLATTEN
void execCvtgdC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_C: VAX CVTGQ/C
AXP_HOT AXP_FLATTEN
void execCvtgqC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQF_C: VAX CVTQF/C
AXP_HOT AXP_FLATTEN
void execCvtqfC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQG_C: VAX CVTQG/C
AXP_HOT AXP_FLATTEN
void execCvtqgC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF: VAX ADDF
AXP_HOT AXP_FLATTEN
void execAddf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF: VAX SUBF
AXP_HOT AXP_FLATTEN
void execSubf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF: VAX MULF
AXP_HOT AXP_FLATTEN
void execMulf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF: VAX DIVF
AXP_HOT AXP_FLATTEN
void execDivf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG: VAX CVTDG
AXP_HOT AXP_FLATTEN
void execCvtdg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG: VAX ADDG
AXP_HOT AXP_FLATTEN
void execAddg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG: VAX SUBG
AXP_HOT AXP_FLATTEN
void execSubg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG: VAX MULG
AXP_HOT AXP_FLATTEN
void execMulg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG: VAX DIVG
AXP_HOT AXP_FLATTEN
void execDivg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPGEQ: VAX CMPGEQ
AXP_HOT AXP_FLATTEN
void execCmpgeq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPGLT: VAX CMPGLT
AXP_HOT AXP_FLATTEN
void execCmpglt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPGLE: VAX CMPGLE
AXP_HOT AXP_FLATTEN
void execCmpgle(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF: VAX CVTGF
AXP_HOT AXP_FLATTEN
void execCvtgf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD: VAX CVTGD
AXP_HOT AXP_FLATTEN
void execCvtgd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ: VAX CVTGQ
AXP_HOT AXP_FLATTEN
void execCvtgq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQF: VAX CVTQF
AXP_HOT AXP_FLATTEN
void execCvtqf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQG: VAX CVTQG
AXP_HOT AXP_FLATTEN
void execCvtqg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_UC: VAX ADDF/UC
AXP_HOT AXP_FLATTEN
void execAddfUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_UC: VAX SUBF/UC
AXP_HOT AXP_FLATTEN
void execSubfUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_UC: VAX MULF/UC
AXP_HOT AXP_FLATTEN
void execMulfUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_UC: VAX DIVF/UC
AXP_HOT AXP_FLATTEN
void execDivfUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_UC: VAX CVTDG/UC
AXP_HOT AXP_FLATTEN
void execCvtdgUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_UC: VAX ADDG/UC
AXP_HOT AXP_FLATTEN
void execAddgUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_UC: VAX SUBG/UC
AXP_HOT AXP_FLATTEN
void execSubgUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_UC: VAX MULG/UC
AXP_HOT AXP_FLATTEN
void execMulgUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_UC: VAX DIVG/UC
AXP_HOT AXP_FLATTEN
void execDivgUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_UC: VAX CVTGF/UC
AXP_HOT AXP_FLATTEN
void execCvtgfUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_UC: VAX CVTGD/UC
AXP_HOT AXP_FLATTEN
void execCvtgdUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_VC: VAX CVTGQ/VC
AXP_HOT AXP_FLATTEN
void execCvtgqVc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_U: VAX ADDF/U
AXP_HOT AXP_FLATTEN
void execAddfU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_U: VAX SUBF/U
AXP_HOT AXP_FLATTEN
void execSubfU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_U: VAX MULF/U
AXP_HOT AXP_FLATTEN
void execMulfU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_U: VAX DIVF/U
AXP_HOT AXP_FLATTEN
void execDivfU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_U: VAX CVTDG/U
AXP_HOT AXP_FLATTEN
void execCvtdgU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_U: VAX ADDG/U
AXP_HOT AXP_FLATTEN
void execAddgU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_U: VAX SUBG/U
AXP_HOT AXP_FLATTEN
void execSubgU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_U: VAX MULG/U
AXP_HOT AXP_FLATTEN
void execMulgU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_U: VAX DIVG/U
AXP_HOT AXP_FLATTEN
void execDivgU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_U: VAX CVTGF/U
AXP_HOT AXP_FLATTEN
void execCvtgfU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_U: VAX CVTGD/U
AXP_HOT AXP_FLATTEN
void execCvtgdU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_V: VAX CVTGQ/V
AXP_HOT AXP_FLATTEN
void execCvtgqV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_SC: VAX ADDF/SC
AXP_HOT AXP_FLATTEN
void execAddfSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_SC: VAX SUBF/SC
AXP_HOT AXP_FLATTEN
void execSubfSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_SC: VAX MULF/SC
AXP_HOT AXP_FLATTEN
void execMulfSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_SC: VAX DIVF/SC
AXP_HOT AXP_FLATTEN
void execDivfSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_SC: VAX CVTDG/SC
AXP_HOT AXP_FLATTEN
void execCvtdgSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_SC: VAX ADDG/SC
AXP_HOT AXP_FLATTEN
void execAddgSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_SC: VAX SUBG/SC
AXP_HOT AXP_FLATTEN
void execSubgSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_SC: VAX MULG/SC
AXP_HOT AXP_FLATTEN
void execMulgSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_SC: VAX DIVG/SC
AXP_HOT AXP_FLATTEN
void execDivgSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_SC: VAX CVTGF/SC
AXP_HOT AXP_FLATTEN
void execCvtgfSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_SC: VAX CVTGD/SC
AXP_HOT AXP_FLATTEN
void execCvtgdSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_SC: VAX CVTGQ/SC
AXP_HOT AXP_FLATTEN
void execCvtgqSc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_S: VAX ADDF/S
AXP_HOT AXP_FLATTEN
void execAddfS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_S: VAX SUBF/S
AXP_HOT AXP_FLATTEN
void execSubfS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_S: VAX MULF/S
AXP_HOT AXP_FLATTEN
void execMulfS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_S: VAX DIVF/S
AXP_HOT AXP_FLATTEN
void execDivfS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_S: VAX CVTDG/S
AXP_HOT AXP_FLATTEN
void execCvtdgS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_S: VAX ADDG/S
AXP_HOT AXP_FLATTEN
void execAddgS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_S: VAX SUBG/S
AXP_HOT AXP_FLATTEN
void execSubgS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_S: VAX MULG/S
AXP_HOT AXP_FLATTEN
void execMulgS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_S: VAX DIVG/S
AXP_HOT AXP_FLATTEN
void execDivgS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPGEQ_S: VAX CMPGEQ/S
AXP_HOT AXP_FLATTEN
void execCmpgeqS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPGLT_S: VAX CMPGLT/S
AXP_HOT AXP_FLATTEN
void execCmpgltS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPGLE_S: VAX CMPGLE/S
AXP_HOT AXP_FLATTEN
void execCmpgleS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_S: VAX CVTGF/S
AXP_HOT AXP_FLATTEN
void execCvtgfS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_S: VAX CVTGD/S
AXP_HOT AXP_FLATTEN
void execCvtgdS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_S: VAX CVTGQ/S
AXP_HOT AXP_FLATTEN
void execCvtgqS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_SUC: VAX ADDF/SUC
AXP_HOT AXP_FLATTEN
void execAddfSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_SUC: VAX SUBF/SUC
AXP_HOT AXP_FLATTEN
void execSubfSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_SUC: VAX MULF/SUC
AXP_HOT AXP_FLATTEN
void execMulfSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_SUC: VAX DIVF/SUC
AXP_HOT AXP_FLATTEN
void execDivfSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_SUC: VAX CVTDG/SUC
AXP_HOT AXP_FLATTEN
void execCvtdgSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_SUC: VAX ADDG/SUC
AXP_HOT AXP_FLATTEN
void execAddgSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_SUC: VAX SUBG/SUC
AXP_HOT AXP_FLATTEN
void execSubgSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_SUC: VAX MULG/SUC
AXP_HOT AXP_FLATTEN
void execMulgSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_SUC: VAX DIVG/SUC
AXP_HOT AXP_FLATTEN
void execDivgSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_SUC: VAX CVTGF/SUC
AXP_HOT AXP_FLATTEN
void execCvtgfSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_SUC: VAX CVTGD/SUC
AXP_HOT AXP_FLATTEN
void execCvtgdSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_SVC: VAX CVTGQ/SVC
AXP_HOT AXP_FLATTEN
void execCvtgqSvc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDF_SU: VAX ADDF/SU
AXP_HOT AXP_FLATTEN
void execAddfSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBF_SU: VAX SUBF/SU
AXP_HOT AXP_FLATTEN
void execSubfSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULF_SU: VAX MULF/SU
AXP_HOT AXP_FLATTEN
void execMulfSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVF_SU: VAX DIVF/SU
AXP_HOT AXP_FLATTEN
void execDivfSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTDG_SU: VAX CVTDG/SU
AXP_HOT AXP_FLATTEN
void execCvtdgSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDG_SU: VAX ADDG/SU
AXP_HOT AXP_FLATTEN
void execAddgSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBG_SU: VAX SUBG/SU
AXP_HOT AXP_FLATTEN
void execSubgSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULG_SU: VAX MULG/SU
AXP_HOT AXP_FLATTEN
void execMulgSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVG_SU: VAX DIVG/SU
AXP_HOT AXP_FLATTEN
void execDivgSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGF_SU: VAX CVTGF/SU
AXP_HOT AXP_FLATTEN
void execCvtgfSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGD_SU: VAX CVTGD/SU
AXP_HOT AXP_FLATTEN
void execCvtgdSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTGQ_SV: VAX CVTGQ/SV
AXP_HOT AXP_FLATTEN
void execCvtgqSv(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDS_C: IEEE ADDS (chopped)
AXP_HOT AXP_FLATTEN
void execAdds(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBS_C: IEEE SUBS (chopped)
AXP_HOT AXP_FLATTEN
void execSubs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULS_C: IEEE MULS (chopped)
AXP_HOT AXP_FLATTEN
void execMuls(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVS_C: IEEE DIVS (chopped)
AXP_HOT AXP_FLATTEN
void execDivs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ADDT_C: IEEE ADDT (chopped)
AXP_HOT AXP_FLATTEN
void execAddt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SUBT_C: IEEE SUBT (chopped)
AXP_HOT AXP_FLATTEN
void execSubt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MULT_C: IEEE MULT (chopped)
AXP_HOT AXP_FLATTEN
void execMult(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DIVT_C: IEEE DIVT (chopped)
AXP_HOT AXP_FLATTEN
void execDivt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_C: IEEE CVTTS/C
AXP_HOT AXP_FLATTEN
void execCvttsC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_C: IEEE CVTTQ/C
AXP_HOT AXP_FLATTEN
void execCvttqC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_C: IEEE CVTQS/C
AXP_HOT AXP_FLATTEN
void execCvtqsC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_C: IEEE CVTQT/C
AXP_HOT AXP_FLATTEN
void execCvtqtC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_M: IEEE CVTTS/M
AXP_HOT AXP_FLATTEN
void execCvttsM(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_M: IEEE CVTTQ/M
AXP_HOT AXP_FLATTEN
void execCvttqM(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_M: IEEE CVTQS/M
AXP_HOT AXP_FLATTEN
void execCvtqsM(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_M: IEEE CVTQT/M
AXP_HOT AXP_FLATTEN
void execCvtqtM(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTUN: IEEE compare unordered (T)
AXP_HOT AXP_FLATTEN
void execCmptun(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTEQ: IEEE compare equal (T)
AXP_HOT AXP_FLATTEN
void execCmpteq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTLT: IEEE compare less than (T)
AXP_HOT AXP_FLATTEN
void execCmptlt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTLE: IEEE compare less or equal (T)
AXP_HOT AXP_FLATTEN
void execCmptle(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS: Convert T_floating to S_floating
AXP_HOT AXP_FLATTEN
void execCvtts(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ: Convert T_floating to quadword
AXP_HOT AXP_FLATTEN
void execCvttq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS: Convert quadword to S_floating
AXP_HOT AXP_FLATTEN
void execCvtqs(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT: Convert quadword to T_floating
AXP_HOT AXP_FLATTEN
void execCvtqt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_D: IEEE CVTTS/D
AXP_HOT AXP_FLATTEN
void execCvttsD(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_D: IEEE CVTTQ/D
AXP_HOT AXP_FLATTEN
void execCvttqD(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_D: IEEE CVTQS/D
AXP_HOT AXP_FLATTEN
void execCvtqsD(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_D: IEEE CVTQT/D
AXP_HOT AXP_FLATTEN
void execCvtqtD(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_UC: IEEE CVTTS/UC
AXP_HOT AXP_FLATTEN
void execCvttsUc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_VC: IEEE CVTTQ/VC
AXP_HOT AXP_FLATTEN
void execCvttqVc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_UM: IEEE CVTTS/UM
AXP_HOT AXP_FLATTEN
void execCvttsUm(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_VM: IEEE CVTTQ/VM
AXP_HOT AXP_FLATTEN
void execCvttqVm(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_U: IEEE CVTTS/U
AXP_HOT AXP_FLATTEN
void execCvttsU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_V: IEEE CVTTQ/V
AXP_HOT AXP_FLATTEN
void execCvttqV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_UD: IEEE CVTTS/UD
AXP_HOT AXP_FLATTEN
void execCvttsUd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_VD: IEEE CVTTQ/VD
AXP_HOT AXP_FLATTEN
void execCvttqVd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTST: Convert S_floating to T_floating
AXP_HOT AXP_FLATTEN
void execCvtst(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUC: IEEE CVTTS/SUC
AXP_HOT AXP_FLATTEN
void execCvttsSuc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVC: IEEE CVTTQ/SVC
AXP_HOT AXP_FLATTEN
void execCvttqSvc(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUM: IEEE CVTTS/SUM
AXP_HOT AXP_FLATTEN
void execCvttsSum(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVM: IEEE CVTTQ/SVM
AXP_HOT AXP_FLATTEN
void execCvttqSvm(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTUN_SU: IEEE CMPTUN/SU
AXP_HOT AXP_FLATTEN
void execCmptunSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTEQ_SU: IEEE CMPTEQ/SU
AXP_HOT AXP_FLATTEN
void execCmpteqSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTLT_SU: IEEE CMPTLT/SU
AXP_HOT AXP_FLATTEN
void execCmptltSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CMPTLE_SU: IEEE CMPTLE/SU
AXP_HOT AXP_FLATTEN
void execCmptleSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SU: IEEE CVTTS/SU
AXP_HOT AXP_FLATTEN
void execCvttsSu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SV: IEEE CVTTQ/SV
AXP_HOT AXP_FLATTEN
void execCvttqSv(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUD: IEEE CVTTS/SUD
AXP_HOT AXP_FLATTEN
void execCvttsSud(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVD: IEEE CVTTQ/SVD
AXP_HOT AXP_FLATTEN
void execCvttqSvd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTST_S: IEEE CVTST/S
AXP_HOT AXP_FLATTEN
void execCvtstS(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUIC: IEEE CVTTS/SUIC
AXP_HOT AXP_FLATTEN
void execCvttsSuic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVIC: IEEE CVTTQ/SVIC
AXP_HOT AXP_FLATTEN
void execCvttqSvic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_SUIC: IEEE CVTQS/SUIC
AXP_HOT AXP_FLATTEN
void execCvtqsSuic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_SUIC: IEEE CVTQT/SUIC
AXP_HOT AXP_FLATTEN
void execCvtqtSuic(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUIM: IEEE CVTTS/SUIM
AXP_HOT AXP_FLATTEN
void execCvttsSuim(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVIM: IEEE CVTTQ/SVIM
AXP_HOT AXP_FLATTEN
void execCvttqSvim(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_SUIM: IEEE CVTQS/SUIM
AXP_HOT AXP_FLATTEN
void execCvtqsSuim(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_SUIM: IEEE CVTQT/SUIM
AXP_HOT AXP_FLATTEN
void execCvtqtSuim(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUI: IEEE CVTTS/SUI
AXP_HOT AXP_FLATTEN
void execCvttsSui(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVI: IEEE CVTTQ/SVI
AXP_HOT AXP_FLATTEN
void execCvttqSvi(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_SUI: IEEE CVTQS/SUI
AXP_HOT AXP_FLATTEN
void execCvtqsSui(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_SUI: IEEE CVTQT/SUI
AXP_HOT AXP_FLATTEN
void execCvtqtSui(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTS_SUID: IEEE CVTTS/SUID
AXP_HOT AXP_FLATTEN
void execCvttsSuid(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTTQ_SVID: IEEE CVTTQ/SVID
AXP_HOT AXP_FLATTEN
void execCvttqSvid(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQS_SUID: IEEE CVTQS/SUID
AXP_HOT AXP_FLATTEN
void execCvtqsSuid(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQT_SUID: IEEE CVTQT/SUID
AXP_HOT AXP_FLATTEN
void execCvtqtSuid(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTLQ: convert longword to quadword (FP-reg integer reformat)
AXP_HOT AXP_FLATTEN
void execCvtlq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CPYS: copy with sign of Ra
AXP_HOT AXP_FLATTEN
void execCpys(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CPYSN: copy with negated sign of Ra
AXP_HOT AXP_FLATTEN
void execCpysn(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CPYSE: copy sign+exp of Ra, fraction of Rb
AXP_HOT AXP_FLATTEN
void execCpyse(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MT_FPCR: move Ra to FPCR
AXP_HOT AXP_FLATTEN
void execMtFpcr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MF_FPCR: move FPCR to Rc
AXP_HOT AXP_FLATTEN
void execMfFpcr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FCMOVEQ: FP conditional move if Fa == 0.0
AXP_HOT AXP_FLATTEN
void execFcmoveq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FCMOVNE: FP conditional move if Fa != 0.0
AXP_HOT AXP_FLATTEN
void execFcmovne(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FCMOVLT: FP conditional move if Fa < 0.0
AXP_HOT AXP_FLATTEN
void execFcmovlt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FCMOVGE: FP conditional move if Fa >= 0.0
AXP_HOT AXP_FLATTEN
void execFcmovge(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FCMOVLE: FP conditional move if Fa <= 0.0
AXP_HOT AXP_FLATTEN
void execFcmovle(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FCMOVGT: FP conditional move if Fa > 0.0
AXP_HOT AXP_FLATTEN
void execFcmovgt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQL: convert quadword to longword (FP-reg integer reformat)
AXP_HOT AXP_FLATTEN
void execCvtql(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQL_V: convert quadword to longword, overflow enable
AXP_HOT AXP_FLATTEN
void execCvtqlV(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CVTQL_SV: convert quadword to longword, sw completion + overflow
AXP_HOT AXP_FLATTEN
void execCvtqlSv(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDF: VAX F_floating load: 32-bit memory -> 64-bit Fa register
AXP_HOT AXP_FLATTEN
void execLdf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDG: VAX G_floating load: 64-bit memory -> 64-bit Fa register
AXP_HOT AXP_FLATTEN
void execLdg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDS: IEEE S_floating load: 32-bit memory -> 64-bit Fa register (expanded)
AXP_HOT AXP_FLATTEN
void execLds(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDT: IEEE T_floating load: 64-bit memory -> 64-bit Fa register
AXP_HOT AXP_FLATTEN
void execLdt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STF: VAX F_floating store: 64-bit Fa register -> 32-bit memory
AXP_HOT AXP_FLATTEN
void execStf(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STG: VAX G_floating store: 64-bit Fa register -> 64-bit memory
AXP_HOT AXP_FLATTEN
void execStg(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STS: IEEE S_floating store: 64-bit Fa register -> 32-bit memory (collapsed)
AXP_HOT AXP_FLATTEN
void execSts(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STT: IEEE T_floating store: 64-bit Fa register -> 64-bit memory
AXP_HOT AXP_FLATTEN
void execStt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FBEQ: branch if Fa == 0.0 (signed zero counts)
AXP_HOT AXP_FLATTEN
void execFbeq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FBLT: branch if Fa < 0.0
AXP_HOT AXP_FLATTEN
void execFblt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FBLE: branch if Fa <= 0.0
AXP_HOT AXP_FLATTEN
void execFble(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FBNE: branch if Fa != 0.0
AXP_HOT AXP_FLATTEN
void execFbne(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FBGE: branch if Fa >= 0.0
AXP_HOT AXP_FLATTEN
void execFbge(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FBGT: branch if Fa > 0.0
AXP_HOT AXP_FLATTEN
void execFbgt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

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
void execLda(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDAH: load address high: Ra <- Rb + (sext(disp) << 16); no memory access
AXP_HOT AXP_FLATTEN
void execLdah(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDBU: load byte unsigned: Ra <- zero_extend(Mem[EA]<7:0>)
AXP_HOT AXP_FLATTEN
void execLdbu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDQ_U: load quadword unaligned: EA force-aligned by clearing low 3 bits
AXP_HOT AXP_FLATTEN
void execLdqU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDWU: load word unsigned: Ra <- zero_extend(Mem[EA]<15:0>)
AXP_HOT AXP_FLATTEN
void execLdwu(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STW: 16-bit store (word)
AXP_HOT AXP_FLATTEN
void execStw(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STB: 8-bit store (byte)
AXP_HOT AXP_FLATTEN
void execStb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STQ_U: store quadword unaligned: EA force-aligned by clearing low 3 bits
AXP_HOT AXP_FLATTEN
void execStqU(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// FETCH: prefetch hint
AXP_HOT AXP_FLATTEN
void execFetch(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// HW_LD: PALmode load with hint bits
AXP_HOT AXP_FLATTEN
void execHwLd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// HW_ST: PALmode store with hint bits
AXP_HOT AXP_FLATTEN
void execHwSt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDL: sign-extended 32-bit load: Ra <- sext_32(Mem[EA])
AXP_HOT AXP_FLATTEN
void execLdl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDQ: 64-bit aligned load
AXP_HOT AXP_FLATTEN
void execLdq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDL_L: sign-extended longword load locked
AXP_HOT AXP_FLATTEN
void execLdlL(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDQ_L: quadword load locked
AXP_HOT AXP_FLATTEN
void execLdqL(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STL: 32-bit aligned store
AXP_HOT AXP_FLATTEN
void execStl(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STQ: 64-bit aligned store
AXP_HOT AXP_FLATTEN
void execStq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STL_C: store longword conditional; writes Ra with success indicator
AXP_HOT AXP_FLATTEN
void execStlC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STQ_C: store quadword conditional; writes Ra with success indicator
AXP_HOT AXP_FLATTEN
void execStqC(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

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
void execTrapb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// EXCB: exception barrier
AXP_HOT AXP_FLATTEN
void execExcb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MB: memory barrier
AXP_HOT AXP_FLATTEN
void execMb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WMB: write memory barrier
AXP_HOT AXP_FLATTEN
void execWmb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// ECB: evict cache block
AXP_HOT AXP_FLATTEN
void execEcb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

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
void execHalt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CFLUSH: cache flush; 3 personalities (VMS:CFLUSH, Tru64+Linux:cflush)
AXP_HOT AXP_FLATTEN
void execCflush(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// DRAINA: drain aborts; 3 personalities (VMS:DRAINA, Tru64+Linux:draina); REQUIRED per AARM C-16
AXP_HOT AXP_FLATTEN
void execDraina(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// LDQP: load quadword physical intrinsic; R0 := mem[R16] (8 bytes, physical addressing); VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
void execLdqp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// STQP: store quadword physical intrinsic; mem[R16] := R17 (8 bytes, physical addressing); VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
void execStqp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SWPCTX: swap process context (VMS); R16=new HWPCB PA, R0=old PTBR; distinct from Tru64 swpctx at 0x30 (DIFFERENT opcode); v1 leaf is forward-looking stub (palBoxLib/grains/PalEntries.cpp execSwpctx_vms) -- needs CpuState shadow regs + leaf-side memory accessor
AXP_HOT AXP_FLATTEN
void execSwpctx_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_ASN: read processor register ASN (address space number)
AXP_HOT AXP_FLATTEN
void execMfprAsn_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_ASTEN: write processor register ASTEN (AST enable)
AXP_HOT AXP_FLATTEN
void execMtprAsten_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_ASTSR: write processor register ASTSR (AST summary)
AXP_HOT AXP_FLATTEN
void execMtprAstsr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CSERVE: console service intrinsic; inline-executed, no PAL transfer; R16=func, R0=result; 3 personalities (VMS:CSERVE, Tru64+Linux:cserve)
AXP_HOT AXP_FLATTEN
void execCserve(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SWPPAL: swap PALcode image; 3 personalities (VMS:SWPPAL, Tru64+Linux:swppal)
AXP_HOT AXP_FLATTEN
void execSwppal(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_FEN: read processor register FEN (floating-point enable)
AXP_HOT AXP_FLATTEN
void execMfprFen_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_FEN: write processor register FEN (floating-point enable)
AXP_HOT AXP_FLATTEN
void execMtprFen_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_IPIR: write inter-processor interrupt request (VMS:MTPR_IPIR / Tru64+Linux:wripir); same operation, three personalities
AXP_HOT AXP_FLATTEN
void execMtprIpir(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_IPL: read processor register IPL (interrupt priority level)
AXP_HOT AXP_FLATTEN
void execMfprIpl_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_IPL: write processor register IPL (interrupt priority level)
AXP_HOT AXP_FLATTEN
void execMtprIpl_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_MCES: read machine check error summary (VMS:MFPR_MCES / Tru64+Linux:rdmces); same operation, three personalities
AXP_HOT AXP_FLATTEN
void execMfprMces(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_MCES: write machine check error summary (VMS:MTPR_MCES / Tru64+Linux:wrmces); same operation, three personalities
AXP_HOT AXP_FLATTEN
void execMtprMces(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_PCBB: read process control block base
AXP_HOT AXP_FLATTEN
void execMfprPcbb_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_PRBR: read processor base register
AXP_HOT AXP_FLATTEN
void execMfprPrbr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_PRBR: write processor base register
AXP_HOT AXP_FLATTEN
void execMtprPrbr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_PTBR: read page table base register
AXP_HOT AXP_FLATTEN
void execMfprPtbr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_SCBB: read system control block base; V4 v1 intrinsic returns cpu.scbb in R0 (palBoxLib/grains/PalEntries.cpp execMfprScbb); SCB layout in deviceLib/Scb.h per AARM 14.6
AXP_HOT AXP_FLATTEN
void execMfprScbb_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_SCBB: write system control block base; V4 v1 intrinsic stores R16 into cpu.scbb (palBoxLib/grains/PalEntries.cpp execMtprScbb)
AXP_HOT AXP_FLATTEN
void execMtprScbb_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_SIRR: write software interrupt request
AXP_HOT AXP_FLATTEN
void execMtprSirr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_SISR: read software interrupt summary
AXP_HOT AXP_FLATTEN
void execMfprSisr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_TBCHK: check translation buffer for entry
AXP_HOT AXP_FLATTEN
void execMfprTbchk_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_TBIA: invalidate all translation buffer entries
AXP_HOT AXP_FLATTEN
void execMtprTbia_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_TBIAP: invalidate all process TB entries (ASM=0)
AXP_HOT AXP_FLATTEN
void execMtprTbiap_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_TBIS: invalidate single TB entry
AXP_HOT AXP_FLATTEN
void execMtprTbis_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_ESP: read executive stack pointer
AXP_HOT AXP_FLATTEN
void execMfprEsp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_ESP: write executive stack pointer
AXP_HOT AXP_FLATTEN
void execMtprEsp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_SSP: read supervisor stack pointer
AXP_HOT AXP_FLATTEN
void execMfprSsp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_SSP: write supervisor stack pointer
AXP_HOT AXP_FLATTEN
void execMtprSsp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_USP: read user stack pointer (VMS); distinct from Tru64 RDUSP at 0x3A (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execMfprUsp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_USP: write user stack pointer (VMS); distinct from Tru64 WRUSP at 0x38 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execMtprUsp_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_TBISD: invalidate single TB entry (data stream)
AXP_HOT AXP_FLATTEN
void execMtprTbisd_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_TBISI: invalidate single TB entry (instruction stream)
AXP_HOT AXP_FLATTEN
void execMtprTbisi_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_ASTEN: read AST enable register
AXP_HOT AXP_FLATTEN
void execMfprAsten_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_ASTSR: read AST summary register
AXP_HOT AXP_FLATTEN
void execMfprAstsr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_VPTB: read Virtual Page Table Base intrinsic; R0 := cpu.vptb; VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
void execMfprVptb_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_VPTB: write Virtual Page Table Base intrinsic; cpu.vptb := R16; VMS-only per AARM C-15 (Tru64/Linux: --)
AXP_HOT AXP_FLATTEN
void execMtprVptb_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_PERFMON: VMS:MTPR_PERFMON (write performance monitor) / Tru64+Linux:wrfen (write FP enable) -- DIFFERENT operations same opcode 0x2B; runtime must dispatch on personality
AXP_HOT AXP_FLATTEN
void execMtprPerfmon(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRVPTPTR: write virtual page table pointer (Tru64+Linux); no VMS counterpart at 0x2D
AXP_HOT AXP_FLATTEN
void execWrvptptr_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MTPR_DATFX: VMS:MTPR_DATFX (write data align trap fixup enable) / Tru64:wrasn (write ASN); Linux: not supported -- DIFFERENT operations same opcode 0x2E
AXP_HOT AXP_FLATTEN
void execMtprDatfx(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_VIRBND: VMS:MFPR_VIRBND (read virtual addr boundary; optional ext, AARM 13.3.24; AARM TOC has typo MFPT_VIRBND, normalized) / Tru64+Linux:swpctx (swap process context) -- DIFFERENT operations same opcode 0x30
AXP_HOT AXP_FLATTEN
void execMfprVirbnd(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRVAL: write system value (Tru64+Linux); no VMS counterpart at 0x31
AXP_HOT AXP_FLATTEN
void execWrval_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_SYSPTBR: VMS:MFPR_SYSPTBR (read system page table base; optional ext, AARM 13.3.18) / Tru64+Linux:rdval (read system value) -- DIFFERENT operations same opcode 0x32
AXP_HOT AXP_FLATTEN
void execMfprSysptbr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// TBI: translation buffer invalidate (Tru64+Linux); no VMS counterpart at 0x33
AXP_HOT AXP_FLATTEN
void execTbi_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRENT: write system entry vector (Tru64+Linux); no VMS counterpart at 0x34
AXP_HOT AXP_FLATTEN
void execWrent_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SWPIPL: swap interrupt priority level (Tru64+Linux); no VMS counterpart at 0x35
AXP_HOT AXP_FLATTEN
void execSwpipl_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RDPS: read processor status (Tru64+Linux); distinct from VMS unprivileged RD_PS at 0x91 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execRdps_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRKGP: write kernel global pointer (Tru64+Linux); no VMS counterpart at 0x37
AXP_HOT AXP_FLATTEN
void execWrkgp_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRUSP: write user stack pointer (Tru64+Linux); distinct from VMS MTPR_USP at 0x23 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execWrusp_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRPERFMON: write performance monitor (Tru64+Linux); no VMS counterpart at 0x39
AXP_HOT AXP_FLATTEN
void execWrperfmon_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RDUSP: read user stack pointer (Tru64+Linux); distinct from VMS MFPR_USP at 0x22 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execRdusp_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WHAMI: read CPU ID (Tru64+Linux); divert to PAL, body returns whami in R0; distinct from VMS MFPR_WHAMI at 0x3F (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execWhami_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RETSYS: return from system call (Tru64+Linux); no VMS counterpart at 0x3D
AXP_HOT AXP_FLATTEN
void execRetsys_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WTINT: wait for interrupt intrinsic; 3 personalities (AARM C-15: WTINT/wtint/wtint); V4 has no interrupt source so returns 0 in R0 immediately
AXP_HOT AXP_FLATTEN
void execWtint(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// MFPR_WHAMI: VMS:MFPR_WHAMI (read CPU ID) / Tru64+Linux:rti (return from trap/interrupt) -- DIFFERENT operations same opcode 0x3F; V4 v1 has VMS WHAMI intrinsic at this opcode (palBoxLib/grains/PalEntries.cpp execMfprWhami) -- Tru64/Linux RTI not yet implemented
AXP_HOT AXP_FLATTEN
void execMfprWhami(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BPT: breakpoint trap; 3 personalities (AARM C-15: BPT/bpt/bpt)
AXP_HOT AXP_FLATTEN
void execBpt(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// BUGCHK: bug check (VMS only)
AXP_HOT AXP_FLATTEN
void execBugchk_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CHME: change mode to executive (VMS only)
AXP_HOT AXP_FLATTEN
void execChme_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CHMK: change mode to kernel (Tru64+Linux: callsys) / VMS: CHMK; 3 personalities at opcode 0x83
AXP_HOT AXP_FLATTEN
void execChmk(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CHMS: change mode to supervisor (VMS only)
AXP_HOT AXP_FLATTEN
void execChms_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CHMU: change mode to user (VMS only)
AXP_HOT AXP_FLATTEN
void execChmu_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// IMB: instruction memory barrier; 3 personalities (VMS:IMB, Tru64+Linux:imb); REQUIRED per AARM C-16
AXP_HOT AXP_FLATTEN
void execImb(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQHIL: insert into queue at head, longword (interlocked)
AXP_HOT AXP_FLATTEN
void execInsqhil_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQTIL: insert into queue at tail, longword (interlocked)
AXP_HOT AXP_FLATTEN
void execInsqtil_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQHIQ: insert into queue at head, quadword (interlocked)
AXP_HOT AXP_FLATTEN
void execInsqhiq_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQTIQ: insert into queue at tail, quadword (interlocked)
AXP_HOT AXP_FLATTEN
void execInsqtiq_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQUEL: insert into queue, longword
AXP_HOT AXP_FLATTEN
void execInsquel_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQUEQ: insert into queue, quadword
AXP_HOT AXP_FLATTEN
void execInsqueq_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQUEL_D: insert into queue (deferred), longword (AARM mnemonic: INSQUEL/D; slash replaced with underscore for codegen)
AXP_HOT AXP_FLATTEN
void execInsquelD_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQUEQ_D: insert into queue (deferred), quadword (AARM mnemonic: INSQUEQ/D)
AXP_HOT AXP_FLATTEN
void execInsqueqD_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// PROBER: probe for read access
AXP_HOT AXP_FLATTEN
void execProber_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// PROBEW: probe for write access
AXP_HOT AXP_FLATTEN
void execProbew_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RD_PS: read processor status (VMS unprivileged); distinct from Tru64 RDPS at 0x36 (DIFFERENT opcode)
AXP_HOT AXP_FLATTEN
void execRdPs_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REI: VMS:REI (return from exception/interrupt) / Tru64:urti (user-level RTI); Linux: not supported -- DIFFERENT operations same opcode 0x92
AXP_HOT AXP_FLATTEN
void execRei(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQHIL: remove from queue at head, longword (interlocked)
AXP_HOT AXP_FLATTEN
void execRemqhil_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQTIL: remove from queue at tail, longword (interlocked)
AXP_HOT AXP_FLATTEN
void execRemqtil_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQHIQ: remove from queue at head, quadword (interlocked)
AXP_HOT AXP_FLATTEN
void execRemqhiq_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQTIQ: remove from queue at tail, quadword (interlocked)
AXP_HOT AXP_FLATTEN
void execRemqtiq_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQUEL: remove from queue, longword
AXP_HOT AXP_FLATTEN
void execRemquel_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQUEQ: remove from queue, quadword
AXP_HOT AXP_FLATTEN
void execRemqueq_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQUEL_D: remove from queue (deferred), longword (AARM mnemonic: REMQUEL/D)
AXP_HOT AXP_FLATTEN
void execRemquelD_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQUEQ_D: remove from queue (deferred), quadword (AARM mnemonic: REMQUEQ/D)
AXP_HOT AXP_FLATTEN
void execRemqueqD_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// SWASTEN: swap AST enable
AXP_HOT AXP_FLATTEN
void execSwasten_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WR_PS_SW: write processor status software bits
AXP_HOT AXP_FLATTEN
void execWrPsSw_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// RSCC: read system cycle counter
AXP_HOT AXP_FLATTEN
void execRscc_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// READ_UNQ: read process unique value (VMS:READ_UNQ / Tru64+Linux:rdunique); same operation, three personalities; thread-local storage primitive
AXP_HOT AXP_FLATTEN
void execReadUnq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// WRITE_UNQ: write process unique value (VMS:WRITE_UNQ / Tru64+Linux:wrunique); same operation, three personalities; thread-local storage primitive
AXP_HOT AXP_FLATTEN
void execWriteUnq(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// AMOVRR: atomic move register-to-register
AXP_HOT AXP_FLATTEN
void execAmovrr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// AMOVRM: atomic move register-to-memory
AXP_HOT AXP_FLATTEN
void execAmovrm_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQHILR: insert at head, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execInsqhilr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQTILR: insert at tail, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execInsqtilr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQHIQR: insert at head, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execInsqhiqr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// INSQTIQR: insert at tail, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execInsqtiqr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQHILR: remove at head, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execRemqhilr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQTILR: remove at tail, longword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execRemqtilr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQHIQR: remove at head, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execRemqhiqr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// REMQTIQR: remove at tail, quadword, resident-reentrant
AXP_HOT AXP_FLATTEN
void execRemqtiqr_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// GENTRAP: generate software trap; 3 personalities (VMS:GENTRAP, Tru64+Linux:gentrap)
AXP_HOT AXP_FLATTEN
void execGentrap(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// CLRFEN: clear floating-point enable; 3 personalities (VMS:CLRFEN, Tru64+Linux:clrfen)
AXP_HOT AXP_FLATTEN
void execClrfen(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// HW_MFPR: read internal processor register
AXP_HOT AXP_FLATTEN
void execHwMfpr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// HW_MTPR: write internal processor register
AXP_HOT AXP_FLATTEN
void execHwMtpr(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// HW_REI: return from PAL; resume EXC_ADDR
AXP_HOT AXP_FLATTEN
void execHwRei(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// execBpt_tru64: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
void execBpt_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// execBpt_vms: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
void execBpt_vms(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// execCallPalDispatch: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
void execCallPalDispatch(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

// execChmk_tru64: synthetic hand-written leaf (handwritten.tsv only; no GrainMaster row)
AXP_HOT AXP_FLATTEN
void execChmk_tru64(InstructionGrain const& g, ExecCtx const& c, BoxResult& out) noexcept;

} // namespace palBox
