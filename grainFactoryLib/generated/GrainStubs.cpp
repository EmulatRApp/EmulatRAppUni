// ============================================================================
// GrainStubs.cpp -- generated for EmulatR V4
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


#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

// Stub bodies for every leaf declared in GrainsForward.h.
// Each stub returns BoxResult with faultCode =
// kFaultUnimplemented so dispatch reaches a fault delivery
// rather than crashing on an unresolved symbol.
//
// Replace stubs with real implementations under
// {boxLib}/grains/ and remove the stub from this file when
// the real definition lands.  Multiple definition error
// surfaces immediately if a stub is left behind.
//
// Each stub also announces itself on stderr the first few
// times it runs.  Reaching a stub means the firmware hit an
// instruction V4 has not implemented yet; the call returns
// kFaultUnimplemented and diverts into PALcode's fault vector.
// The announcement names the next leaf that needs a real body
// -- it turns trace archaeology into a direct read.

namespace {

using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// Throttled per-stub: each stub passes its own counter, so a
// hot stub spinning in a loop cannot mute a rarely-hit stub's
// first announcement.  First 8 hits loud, then a summary every
// 64K -- matches the CboxEventLog / MmioRegistry stderr posture.
void logUnimplementedStub(char const* mnem,
                          InstructionGrain const& g,
                          ExecCtx const& c,
                          std::atomic<uint64_t>& counter) noexcept
{
    uint64_t const n = counter.fetch_add(1, std::memory_order_relaxed);
    if (n < 8) {
        std::fprintf(stderr,
                     "GrainStub: UNIMPLEMENTED %s pc=0x%016llx "
                     "encoded=0x%08x cyc=%llu (hit %llu)\n",
                     mnem,
                     static_cast<unsigned long long>(g.pc),
                     static_cast<unsigned>(g.encoded),
                     static_cast<unsigned long long>(c.cycleCount),
                     static_cast<unsigned long long>(n));
        std::fflush(stderr);
    } else if ((n & 0xFFFFu) == 0) {
        std::fprintf(stderr,
                     "GrainStub: %s hit %llu times "
                     "(loud-stderr muted past 8)\n",
                     mnem,
                     static_cast<unsigned long long>(n + 1));
        std::fflush(stderr);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Ibox -- namespace iBox
// ---------------------------------------------------------------------------
namespace iBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// JMP: hand-written -- see iBoxLib/grains/ for iBox::execJmp

// JSR: hand-written -- see iBoxLib/grains/ for iBox::execJsr

// RET: hand-written -- see iBoxLib/grains/ for iBox::execRet

// JSR_COROUTINE: hand-written -- see iBoxLib/grains/ for iBox::execJsrCoroutine

// BR: hand-written -- see iBoxLib/grains/ for iBox::execBr

// BSR: hand-written -- see iBoxLib/grains/ for iBox::execBsr

// BLBC: hand-written -- see iBoxLib/grains/ for iBox::execBlbc

// BEQ: hand-written -- see iBoxLib/grains/ for iBox::execBeq

// BLT: hand-written -- see iBoxLib/grains/ for iBox::execBlt

// BLE: hand-written -- see iBoxLib/grains/ for iBox::execBle

// BLBS: hand-written -- see iBoxLib/grains/ for iBox::execBlbs

// BNE: hand-written -- see iBoxLib/grains/ for iBox::execBne

// BGE: hand-written -- see iBoxLib/grains/ for iBox::execBge

// BGT: hand-written -- see iBoxLib/grains/ for iBox::execBgt

} // namespace iBox

// ---------------------------------------------------------------------------
// Ebox -- namespace eBox
// ---------------------------------------------------------------------------
namespace eBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// ADDL: hand-written -- see eBoxLib/grains/ for eBox::execAddl

// S4ADDL: hand-written -- see eBoxLib/grains/ for eBox::execS4addl

// SUBL: hand-written -- see eBoxLib/grains/ for eBox::execSubl

// S4SUBL: hand-written -- see eBoxLib/grains/ for eBox::execS4subl

// CMPBGE: hand-written -- see eBoxLib/grains/ for eBox::execCmpbge

// S8ADDL: hand-written -- see eBoxLib/grains/ for eBox::execS8addl

// S8SUBL: hand-written -- see eBoxLib/grains/ for eBox::execS8subl

// CMPULT: hand-written -- see eBoxLib/grains/ for eBox::execCmpult

// ADDQ: hand-written -- see eBoxLib/grains/ for eBox::execAddq

// S4ADDQ: hand-written -- see eBoxLib/grains/ for eBox::execS4addq

// SUBQ: hand-written -- see eBoxLib/grains/ for eBox::execSubq

// S4SUBQ: hand-written -- see eBoxLib/grains/ for eBox::execS4subq

// CMPEQ: hand-written -- see eBoxLib/grains/ for eBox::execCmpeq

// S8ADDQ: hand-written -- see eBoxLib/grains/ for eBox::execS8addq

// S8SUBQ: hand-written -- see eBoxLib/grains/ for eBox::execS8subq

// CMPULE: hand-written -- see eBoxLib/grains/ for eBox::execCmpule

// CMPLT: hand-written -- see eBoxLib/grains/ for eBox::execCmplt

// CMPLE: hand-written -- see eBoxLib/grains/ for eBox::execCmple

// AND: hand-written -- see eBoxLib/grains/ for eBox::execAnd

// BIC: hand-written -- see eBoxLib/grains/ for eBox::execBic

// CMOVLBS: hand-written -- see eBoxLib/grains/ for eBox::execCmovlbs

// CMOVLBC: hand-written -- see eBoxLib/grains/ for eBox::execCmovlbc

// BIS: hand-written -- see eBoxLib/grains/ for eBox::execBis

// CMOVEQ: hand-written -- see eBoxLib/grains/ for eBox::execCmoveq

// CMOVNE: hand-written -- see eBoxLib/grains/ for eBox::execCmovne

// ORNOT: hand-written -- see eBoxLib/grains/ for eBox::execOrnot

// XOR: hand-written -- see eBoxLib/grains/ for eBox::execXor

// CMOVLT: hand-written -- see eBoxLib/grains/ for eBox::execCmovlt

// CMOVGE: hand-written -- see eBoxLib/grains/ for eBox::execCmovge

// EQV: hand-written -- see eBoxLib/grains/ for eBox::execEqv

// AMASK: hand-written -- see eBoxLib/grains/ for eBox::execAmask

// CMOVLE: hand-written -- see eBoxLib/grains/ for eBox::execCmovle

// CMOVGT: hand-written -- see eBoxLib/grains/ for eBox::execCmovgt

// IMPLVER: hand-written -- see eBoxLib/grains/ for eBox::execImplver

// MSKBL: hand-written -- see eBoxLib/grains/ for eBox::execMskbl

// EXTBL: hand-written -- see eBoxLib/grains/ for eBox::execExtbl

// INSBL: hand-written -- see eBoxLib/grains/ for eBox::execInsbl

// MSKWL: hand-written -- see eBoxLib/grains/ for eBox::execMskwl

// EXTWL: hand-written -- see eBoxLib/grains/ for eBox::execExtwl

// INSWL: hand-written -- see eBoxLib/grains/ for eBox::execInswl

// MSKLL: hand-written -- see eBoxLib/grains/ for eBox::execMskll

// EXTLL: hand-written -- see eBoxLib/grains/ for eBox::execExtll

// INSLL: hand-written -- see eBoxLib/grains/ for eBox::execInsll

// ZAP: hand-written -- see eBoxLib/grains/ for eBox::execZap

// ZAPNOT: hand-written -- see eBoxLib/grains/ for eBox::execZapnot

// MSKQL: hand-written -- see eBoxLib/grains/ for eBox::execMskql

// SRL: hand-written -- see eBoxLib/grains/ for eBox::execSrl

// EXTQL: hand-written -- see eBoxLib/grains/ for eBox::execExtql

// SLL: hand-written -- see eBoxLib/grains/ for eBox::execSll

// INSQL: hand-written -- see eBoxLib/grains/ for eBox::execInsql

// SRA: hand-written -- see eBoxLib/grains/ for eBox::execSra

// MSKWH: hand-written -- see eBoxLib/grains/ for eBox::execMskwh

// INSWH: hand-written -- see eBoxLib/grains/ for eBox::execInswh

// EXTWH: hand-written -- see eBoxLib/grains/ for eBox::execExtwh

// MSKLH: hand-written -- see eBoxLib/grains/ for eBox::execMsklh

// INSLH: hand-written -- see eBoxLib/grains/ for eBox::execInslh

// EXTLH: hand-written -- see eBoxLib/grains/ for eBox::execExtlh

// MSKQH: hand-written -- see eBoxLib/grains/ for eBox::execMskqh

// INSQH: hand-written -- see eBoxLib/grains/ for eBox::execInsqh

// EXTQH: hand-written -- see eBoxLib/grains/ for eBox::execExtqh

// MULL: hand-written -- see eBoxLib/grains/ for eBox::execMull

// MULQ: hand-written -- see eBoxLib/grains/ for eBox::execMulq

// UMULH: hand-written -- see eBoxLib/grains/ for eBox::execUmulh

// ITOFS: hand-written -- see eBoxLib/grains/ for eBox::execItofs

// ITOFF: hand-written -- see eBoxLib/grains/ for eBox::execItoff

// ITOFT: hand-written -- see eBoxLib/grains/ for eBox::execItoft

// RPCC: hand-written -- see eBoxLib/grains/ for eBox::execRpcc

// RC: hand-written -- see eBoxLib/grains/ for eBox::execRc

// RS: hand-written -- see eBoxLib/grains/ for eBox::execRs

// SEXTB: hand-written -- see eBoxLib/grains/ for eBox::execSextb

// SEXTW: hand-written -- see eBoxLib/grains/ for eBox::execSextw

// CTPOP: hand-written -- see eBoxLib/grains/ for eBox::execCtpop

// PERR: hand-written -- see eBoxLib/grains/ for eBox::execPerr

// CTLZ: hand-written -- see eBoxLib/grains/ for eBox::execCtlz

// CTTZ: hand-written -- see eBoxLib/grains/ for eBox::execCttz

// UNPKBW: hand-written -- see eBoxLib/grains/ for eBox::execUnpkbw

// UNPKBL: hand-written -- see eBoxLib/grains/ for eBox::execUnpkbl

// PKWB: hand-written -- see eBoxLib/grains/ for eBox::execPkwb

// PKLB: hand-written -- see eBoxLib/grains/ for eBox::execPklb

// MINSB8: hand-written -- see eBoxLib/grains/ for eBox::execMinsb8

// MINSW4: hand-written -- see eBoxLib/grains/ for eBox::execMinsw4

// MINUB8: hand-written -- see eBoxLib/grains/ for eBox::execMinub8

// MINUW4: hand-written -- see eBoxLib/grains/ for eBox::execMinuw4

// MAXUB8: hand-written -- see eBoxLib/grains/ for eBox::execMaxub8

// MAXUW4: hand-written -- see eBoxLib/grains/ for eBox::execMaxuw4

// MAXSB8: hand-written -- see eBoxLib/grains/ for eBox::execMaxsb8

// MAXSW4: hand-written -- see eBoxLib/grains/ for eBox::execMaxsw4

// FTOIT: hand-written -- see eBoxLib/grains/ for eBox::execFtoit

} // namespace eBox

// ---------------------------------------------------------------------------
// Fbox -- namespace fBox
// ---------------------------------------------------------------------------
namespace fBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// SQRTF_C: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfC

// SQRTS_C: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsC

// SQRTG_C: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgC

// SQRTT_C: hand-written -- see fBoxLib/grains/ for fBox::execSqrttC

// SQRTS_M: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsM

// SQRTT_M: hand-written -- see fBoxLib/grains/ for fBox::execSqrttM

// SQRTF: hand-written -- see fBoxLib/grains/ for fBox::execSqrtf

// SQRTS: hand-written -- see fBoxLib/grains/ for fBox::execSqrts

// SQRTG: hand-written -- see fBoxLib/grains/ for fBox::execSqrtg

// SQRTT: hand-written -- see fBoxLib/grains/ for fBox::execSqrtt

// SQRTS_D: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsD

// SQRTT_D: hand-written -- see fBoxLib/grains/ for fBox::execSqrttD

// SQRTF_UC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfUc

// SQRTS_UC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsUc

// SQRTG_UC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgUc

// SQRTT_UC: hand-written -- see fBoxLib/grains/ for fBox::execSqrttUc

// SQRTS_UM: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsUm

// SQRTT_UM: hand-written -- see fBoxLib/grains/ for fBox::execSqrttUm

// SQRTF_U: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfU

// SQRTS_U: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsU

// SQRTG_U: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgU

// SQRTT_U: hand-written -- see fBoxLib/grains/ for fBox::execSqrttU

// SQRTS_UD: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsUd

// SQRTT_UD: hand-written -- see fBoxLib/grains/ for fBox::execSqrttUd

// SQRTF_SC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfSc

// SQRTG_SC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgSc

// SQRTF_S: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfS

// SQRTG_S: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgS

// SQRTF_SUC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfSuc

// SQRTS_SUC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSuc

// SQRTG_SUC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgSuc

// SQRTT_SUC: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSuc

// SQRTS_SUM: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSum

// SQRTT_SUM: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSum

// SQRTF_SU: hand-written -- see fBoxLib/grains/ for fBox::execSqrtfSu

// SQRTS_SU: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSu

// SQRTG_SU: hand-written -- see fBoxLib/grains/ for fBox::execSqrtgSu

// SQRTT_SU: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSu

// SQRTS_SUD: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSud

// SQRTT_SUD: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSud

// SQRTS_SUIC: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSuic

// SQRTT_SUIC: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSuic

// SQRTS_SUIM: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSuim

// SQRTT_SUIM: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSuim

// SQRTS_SUI: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSui

// SQRTT_SUI: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSui

// SQRTS_SUID: hand-written -- see fBoxLib/grains/ for fBox::execSqrtsSuid

// SQRTT_SUID: hand-written -- see fBoxLib/grains/ for fBox::execSqrttSuid

// ADDF_C: hand-written -- see fBoxLib/grains/ for fBox::execAddfC

// SUBF_C: hand-written -- see fBoxLib/grains/ for fBox::execSubfC

// MULF_C: hand-written -- see fBoxLib/grains/ for fBox::execMulfC

// DIVF_C: hand-written -- see fBoxLib/grains/ for fBox::execDivfC

// CVTDG_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgC

// ADDG_C: hand-written -- see fBoxLib/grains/ for fBox::execAddgC

// SUBG_C: hand-written -- see fBoxLib/grains/ for fBox::execSubgC

// MULG_C: hand-written -- see fBoxLib/grains/ for fBox::execMulgC

// DIVG_C: hand-written -- see fBoxLib/grains/ for fBox::execDivgC

// CVTGF_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfC

// CVTGD_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdC

// CVTGQ_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqC

// CVTQF_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtqfC

// CVTQG_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtqgC

// ADDF: hand-written -- see fBoxLib/grains/ for fBox::execAddf

// SUBF: hand-written -- see fBoxLib/grains/ for fBox::execSubf

// MULF: hand-written -- see fBoxLib/grains/ for fBox::execMulf

// DIVF: hand-written -- see fBoxLib/grains/ for fBox::execDivf

// CVTDG: hand-written -- see fBoxLib/grains/ for fBox::execCvtdg

// ADDG: hand-written -- see fBoxLib/grains/ for fBox::execAddg

// SUBG: hand-written -- see fBoxLib/grains/ for fBox::execSubg

// MULG: hand-written -- see fBoxLib/grains/ for fBox::execMulg

// DIVG: hand-written -- see fBoxLib/grains/ for fBox::execDivg

// CMPGEQ: hand-written -- see fBoxLib/grains/ for fBox::execCmpgeq

// CMPGLT: hand-written -- see fBoxLib/grains/ for fBox::execCmpglt

// CMPGLE: hand-written -- see fBoxLib/grains/ for fBox::execCmpgle

// CVTGF: hand-written -- see fBoxLib/grains/ for fBox::execCvtgf

// CVTGD: hand-written -- see fBoxLib/grains/ for fBox::execCvtgd

// CVTGQ: hand-written -- see fBoxLib/grains/ for fBox::execCvtgq

// CVTQF: hand-written -- see fBoxLib/grains/ for fBox::execCvtqf

// CVTQG: hand-written -- see fBoxLib/grains/ for fBox::execCvtqg

// ADDF_UC: hand-written -- see fBoxLib/grains/ for fBox::execAddfUc

// SUBF_UC: hand-written -- see fBoxLib/grains/ for fBox::execSubfUc

// MULF_UC: hand-written -- see fBoxLib/grains/ for fBox::execMulfUc

// DIVF_UC: hand-written -- see fBoxLib/grains/ for fBox::execDivfUc

// CVTDG_UC: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgUc

// ADDG_UC: hand-written -- see fBoxLib/grains/ for fBox::execAddgUc

// SUBG_UC: hand-written -- see fBoxLib/grains/ for fBox::execSubgUc

// MULG_UC: hand-written -- see fBoxLib/grains/ for fBox::execMulgUc

// DIVG_UC: hand-written -- see fBoxLib/grains/ for fBox::execDivgUc

// CVTGF_UC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfUc

// CVTGD_UC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdUc

// CVTGQ_VC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqVc

// ADDF_U: hand-written -- see fBoxLib/grains/ for fBox::execAddfU

// SUBF_U: hand-written -- see fBoxLib/grains/ for fBox::execSubfU

// MULF_U: hand-written -- see fBoxLib/grains/ for fBox::execMulfU

// DIVF_U: hand-written -- see fBoxLib/grains/ for fBox::execDivfU

// CVTDG_U: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgU

// ADDG_U: hand-written -- see fBoxLib/grains/ for fBox::execAddgU

// SUBG_U: hand-written -- see fBoxLib/grains/ for fBox::execSubgU

// MULG_U: hand-written -- see fBoxLib/grains/ for fBox::execMulgU

// DIVG_U: hand-written -- see fBoxLib/grains/ for fBox::execDivgU

// CVTGF_U: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfU

// CVTGD_U: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdU

// CVTGQ_V: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqV

// ADDF_SC: hand-written -- see fBoxLib/grains/ for fBox::execAddfSc

// SUBF_SC: hand-written -- see fBoxLib/grains/ for fBox::execSubfSc

// MULF_SC: hand-written -- see fBoxLib/grains/ for fBox::execMulfSc

// DIVF_SC: hand-written -- see fBoxLib/grains/ for fBox::execDivfSc

// CVTDG_SC: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgSc

// ADDG_SC: hand-written -- see fBoxLib/grains/ for fBox::execAddgSc

// SUBG_SC: hand-written -- see fBoxLib/grains/ for fBox::execSubgSc

// MULG_SC: hand-written -- see fBoxLib/grains/ for fBox::execMulgSc

// DIVG_SC: hand-written -- see fBoxLib/grains/ for fBox::execDivgSc

// CVTGF_SC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfSc

// CVTGD_SC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdSc

// CVTGQ_SC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqSc

// ADDF_S: hand-written -- see fBoxLib/grains/ for fBox::execAddfS

// SUBF_S: hand-written -- see fBoxLib/grains/ for fBox::execSubfS

// MULF_S: hand-written -- see fBoxLib/grains/ for fBox::execMulfS

// DIVF_S: hand-written -- see fBoxLib/grains/ for fBox::execDivfS

// CVTDG_S: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgS

// ADDG_S: hand-written -- see fBoxLib/grains/ for fBox::execAddgS

// SUBG_S: hand-written -- see fBoxLib/grains/ for fBox::execSubgS

// MULG_S: hand-written -- see fBoxLib/grains/ for fBox::execMulgS

// DIVG_S: hand-written -- see fBoxLib/grains/ for fBox::execDivgS

// CMPGEQ_S: hand-written -- see fBoxLib/grains/ for fBox::execCmpgeqS

// CMPGLT_S: hand-written -- see fBoxLib/grains/ for fBox::execCmpgltS

// CMPGLE_S: hand-written -- see fBoxLib/grains/ for fBox::execCmpgleS

// CVTGF_S: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfS

// CVTGD_S: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdS

// CVTGQ_S: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqS

// ADDF_SUC: hand-written -- see fBoxLib/grains/ for fBox::execAddfSuc

// SUBF_SUC: hand-written -- see fBoxLib/grains/ for fBox::execSubfSuc

// MULF_SUC: hand-written -- see fBoxLib/grains/ for fBox::execMulfSuc

// DIVF_SUC: hand-written -- see fBoxLib/grains/ for fBox::execDivfSuc

// CVTDG_SUC: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgSuc

// ADDG_SUC: hand-written -- see fBoxLib/grains/ for fBox::execAddgSuc

// SUBG_SUC: hand-written -- see fBoxLib/grains/ for fBox::execSubgSuc

// MULG_SUC: hand-written -- see fBoxLib/grains/ for fBox::execMulgSuc

// DIVG_SUC: hand-written -- see fBoxLib/grains/ for fBox::execDivgSuc

// CVTGF_SUC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfSuc

// CVTGD_SUC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdSuc

// CVTGQ_SVC: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqSvc

// ADDF_SU: hand-written -- see fBoxLib/grains/ for fBox::execAddfSu

// SUBF_SU: hand-written -- see fBoxLib/grains/ for fBox::execSubfSu

// MULF_SU: hand-written -- see fBoxLib/grains/ for fBox::execMulfSu

// DIVF_SU: hand-written -- see fBoxLib/grains/ for fBox::execDivfSu

// CVTDG_SU: hand-written -- see fBoxLib/grains/ for fBox::execCvtdgSu

// ADDG_SU: hand-written -- see fBoxLib/grains/ for fBox::execAddgSu

// SUBG_SU: hand-written -- see fBoxLib/grains/ for fBox::execSubgSu

// MULG_SU: hand-written -- see fBoxLib/grains/ for fBox::execMulgSu

// DIVG_SU: hand-written -- see fBoxLib/grains/ for fBox::execDivgSu

// CVTGF_SU: hand-written -- see fBoxLib/grains/ for fBox::execCvtgfSu

// CVTGD_SU: hand-written -- see fBoxLib/grains/ for fBox::execCvtgdSu

// CVTGQ_SV: hand-written -- see fBoxLib/grains/ for fBox::execCvtgqSv

// ADDS_C: hand-written -- see fBoxLib/grains/ for fBox::execAdds

// SUBS_C: hand-written -- see fBoxLib/grains/ for fBox::execSubs

// MULS_C: hand-written -- see fBoxLib/grains/ for fBox::execMuls

// DIVS_C: hand-written -- see fBoxLib/grains/ for fBox::execDivs

// ADDT_C: hand-written -- see fBoxLib/grains/ for fBox::execAddt

// SUBT_C: hand-written -- see fBoxLib/grains/ for fBox::execSubt

// MULT_C: hand-written -- see fBoxLib/grains/ for fBox::execMult

// DIVT_C: hand-written -- see fBoxLib/grains/ for fBox::execDivt

// CVTTS_C: hand-written -- see fBoxLib/grains/ for fBox::execCvttsC

// CVTTQ_C: hand-written -- see fBoxLib/grains/ for fBox::execCvttqC

// CVTQS_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsC

// CVTQT_C: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtC

// CVTTS_M: hand-written -- see fBoxLib/grains/ for fBox::execCvttsM

// CVTTQ_M: hand-written -- see fBoxLib/grains/ for fBox::execCvttqM

// CVTQS_M: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsM

// CVTQT_M: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtM

// CMPTUN: hand-written -- see fBoxLib/grains/ for fBox::execCmptun

// CMPTEQ: hand-written -- see fBoxLib/grains/ for fBox::execCmpteq

// CMPTLT: hand-written -- see fBoxLib/grains/ for fBox::execCmptlt

// CMPTLE: hand-written -- see fBoxLib/grains/ for fBox::execCmptle

// CVTTS: hand-written -- see fBoxLib/grains/ for fBox::execCvtts

// CVTTQ: hand-written -- see fBoxLib/grains/ for fBox::execCvttq

// CVTQS: hand-written -- see fBoxLib/grains/ for fBox::execCvtqs

// CVTQT: hand-written -- see fBoxLib/grains/ for fBox::execCvtqt

// CVTTS_D: hand-written -- see fBoxLib/grains/ for fBox::execCvttsD

// CVTTQ_D: hand-written -- see fBoxLib/grains/ for fBox::execCvttqD

// CVTQS_D: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsD

// CVTQT_D: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtD

// CVTTS_UC: hand-written -- see fBoxLib/grains/ for fBox::execCvttsUc

// CVTTQ_VC: hand-written -- see fBoxLib/grains/ for fBox::execCvttqVc

// CVTTS_UM: hand-written -- see fBoxLib/grains/ for fBox::execCvttsUm

// CVTTQ_VM: hand-written -- see fBoxLib/grains/ for fBox::execCvttqVm

// CVTTS_U: hand-written -- see fBoxLib/grains/ for fBox::execCvttsU

// CVTTQ_V: hand-written -- see fBoxLib/grains/ for fBox::execCvttqV

// CVTTS_UD: hand-written -- see fBoxLib/grains/ for fBox::execCvttsUd

// CVTTQ_VD: hand-written -- see fBoxLib/grains/ for fBox::execCvttqVd

// CVTST: hand-written -- see fBoxLib/grains/ for fBox::execCvtst

// CVTTS_SUC: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSuc

// CVTTQ_SVC: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvc

// CVTTS_SUM: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSum

// CVTTQ_SVM: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvm

// CMPTUN_SU: hand-written -- see fBoxLib/grains/ for fBox::execCmptunSu

// CMPTEQ_SU: hand-written -- see fBoxLib/grains/ for fBox::execCmpteqSu

// CMPTLT_SU: hand-written -- see fBoxLib/grains/ for fBox::execCmptltSu

// CMPTLE_SU: hand-written -- see fBoxLib/grains/ for fBox::execCmptleSu

// CVTTS_SU: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSu

// CVTTQ_SV: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSv

// CVTTS_SUD: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSud

// CVTTQ_SVD: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvd

// CVTST_S: hand-written -- see fBoxLib/grains/ for fBox::execCvtstS

// CVTTS_SUIC: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSuic

// CVTTQ_SVIC: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvic

// CVTQS_SUIC: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsSuic

// CVTQT_SUIC: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtSuic

// CVTTS_SUIM: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSuim

// CVTTQ_SVIM: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvim

// CVTQS_SUIM: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsSuim

// CVTQT_SUIM: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtSuim

// CVTTS_SUI: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSui

// CVTTQ_SVI: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvi

// CVTQS_SUI: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsSui

// CVTQT_SUI: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtSui

// CVTTS_SUID: hand-written -- see fBoxLib/grains/ for fBox::execCvttsSuid

// CVTTQ_SVID: hand-written -- see fBoxLib/grains/ for fBox::execCvttqSvid

// CVTQS_SUID: hand-written -- see fBoxLib/grains/ for fBox::execCvtqsSuid

// CVTQT_SUID: hand-written -- see fBoxLib/grains/ for fBox::execCvtqtSuid

// CVTLQ: hand-written -- see fBoxLib/grains/ for fBox::execCvtlq

// CPYS: hand-written -- see fBoxLib/grains/ for fBox::execCpys

// CPYSN: hand-written -- see fBoxLib/grains/ for fBox::execCpysn

// CPYSE: hand-written -- see fBoxLib/grains/ for fBox::execCpyse

// MT_FPCR: hand-written -- see fBoxLib/grains/ for fBox::execMtFpcr

// MF_FPCR: hand-written -- see fBoxLib/grains/ for fBox::execMfFpcr

// FCMOVEQ: hand-written -- see fBoxLib/grains/ for fBox::execFcmoveq

// FCMOVNE: hand-written -- see fBoxLib/grains/ for fBox::execFcmovne

// FCMOVLT: hand-written -- see fBoxLib/grains/ for fBox::execFcmovlt

// FCMOVGE: hand-written -- see fBoxLib/grains/ for fBox::execFcmovge

// FCMOVLE: hand-written -- see fBoxLib/grains/ for fBox::execFcmovle

// FCMOVGT: hand-written -- see fBoxLib/grains/ for fBox::execFcmovgt

// CVTQL: hand-written -- see fBoxLib/grains/ for fBox::execCvtql

// CVTQL_V: hand-written -- see fBoxLib/grains/ for fBox::execCvtqlV

// CVTQL_SV: hand-written -- see fBoxLib/grains/ for fBox::execCvtqlSv

// LDF: hand-written -- see fBoxLib/grains/ for fBox::execLdf

// LDG: hand-written -- see fBoxLib/grains/ for fBox::execLdg

// LDS: hand-written -- see fBoxLib/grains/ for fBox::execLds

// LDT: hand-written -- see fBoxLib/grains/ for fBox::execLdt

// STF: hand-written -- see fBoxLib/grains/ for fBox::execStf

// STG: hand-written -- see fBoxLib/grains/ for fBox::execStg

// STS: hand-written -- see fBoxLib/grains/ for fBox::execSts

// STT: hand-written -- see fBoxLib/grains/ for fBox::execStt

// FBEQ: hand-written -- see fBoxLib/grains/ for fBox::execFbeq

// FBLT: hand-written -- see fBoxLib/grains/ for fBox::execFblt

// FBLE: hand-written -- see fBoxLib/grains/ for fBox::execFble

// FBNE: hand-written -- see fBoxLib/grains/ for fBox::execFbne

// FBGE: hand-written -- see fBoxLib/grains/ for fBox::execFbge

// FBGT: hand-written -- see fBoxLib/grains/ for fBox::execFbgt

} // namespace fBox

// ---------------------------------------------------------------------------
// Mbox -- namespace mBox
// ---------------------------------------------------------------------------
namespace mBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// LDA: hand-written -- see mBoxLib/grains/ for mBox::execLda

// LDAH: hand-written -- see mBoxLib/grains/ for mBox::execLdah

// LDBU: hand-written -- see mBoxLib/grains/ for mBox::execLdbu

// LDQ_U: hand-written -- see mBoxLib/grains/ for mBox::execLdqU

// LDWU: hand-written -- see mBoxLib/grains/ for mBox::execLdwu

// STW: hand-written -- see mBoxLib/grains/ for mBox::execStw

// STB: hand-written -- see mBoxLib/grains/ for mBox::execStb

// STQ_U: hand-written -- see mBoxLib/grains/ for mBox::execStqU

// FETCH: hand-written -- see mBoxLib/grains/ for mBox::execFetch

// HW_LD: hand-written -- see mBoxLib/grains/ for mBox::execHwLd

// HW_ST: hand-written -- see mBoxLib/grains/ for mBox::execHwSt

// LDL: hand-written -- see mBoxLib/grains/ for mBox::execLdl

// LDQ: hand-written -- see mBoxLib/grains/ for mBox::execLdq

// LDL_L: hand-written -- see mBoxLib/grains/ for mBox::execLdlL

// LDQ_L: hand-written -- see mBoxLib/grains/ for mBox::execLdqL

// STL: hand-written -- see mBoxLib/grains/ for mBox::execStl

// STQ: hand-written -- see mBoxLib/grains/ for mBox::execStq

// STL_C: hand-written -- see mBoxLib/grains/ for mBox::execStlC

// STQ_C: hand-written -- see mBoxLib/grains/ for mBox::execStqC

} // namespace mBox

// ---------------------------------------------------------------------------
// Cbox -- namespace cBox
// ---------------------------------------------------------------------------
namespace cBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// TRAPB: hand-written -- see cBoxLib/grains/ for cBox::execTrapb

// EXCB: hand-written -- see cBoxLib/grains/ for cBox::execExcb

// MB: hand-written -- see cBoxLib/grains/ for cBox::execMb

// WMB: hand-written -- see cBoxLib/grains/ for cBox::execWmb

// ECB: hand-written -- see cBoxLib/grains/ for cBox::execEcb

} // namespace cBox

// ---------------------------------------------------------------------------
// PalBox -- namespace palBox
// ---------------------------------------------------------------------------
namespace palBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;

// HALT: hand-written -- see palBoxLib/grains/ for palBox::execHalt

// CFLUSH: hand-written -- see palBoxLib/grains/ for palBox::execCflush

// DRAINA: hand-written -- see palBoxLib/grains/ for palBox::execDraina

// LDQP: hand-written -- see palBoxLib/grains/ for palBox::execLdqp_vms

// STQP: hand-written -- see palBoxLib/grains/ for palBox::execStqp_vms

// SWPCTX: swap process context (VMS); R16=new HWPCB PA, R0=old PTBR; distinct from Tru64 swpctx at 0x30 (DIFFERENT opcode); v1 leaf is forward-looking stub (palBoxLib/grains/PalEntries.cpp execSwpctx_vms) -- needs CpuState shadow regs + leaf-side memory accessor
AXP_HOT AXP_FLATTEN
BoxResult execSwpctx_vms(InstructionGrain const& g, ExecCtx const& c) noexcept
{
    static std::atomic<uint64_t> s_cnt{ 0 };
    logUnimplementedStub("SWPCTX", g, c, s_cnt);
    BoxResult r;
    r.semFlags  = g.semFlags;
    r.faultCode = coreLib::kFaultUnimplemented;
    return r;
}

// MFPR_ASN: hand-written -- see palBoxLib/grains/ for palBox::execMfprAsn_vms

// MTPR_ASTEN: hand-written -- see palBoxLib/grains/ for palBox::execMtprAsten_vms

// MTPR_ASTSR: hand-written -- see palBoxLib/grains/ for palBox::execMtprAstsr_vms

// CSERVE: hand-written -- see palBoxLib/grains/ for palBox::execCserve

// SWPPAL: hand-written -- see palBoxLib/grains/ for palBox::execSwppal

// MFPR_FEN: hand-written -- see palBoxLib/grains/ for palBox::execMfprFen_vms

// MTPR_FEN: hand-written -- see palBoxLib/grains/ for palBox::execMtprFen_vms

// MTPR_IPIR: hand-written -- see palBoxLib/grains/ for palBox::execMtprIpir

// MFPR_IPL: hand-written -- see palBoxLib/grains/ for palBox::execMfprIpl_vms

// MTPR_IPL: hand-written -- see palBoxLib/grains/ for palBox::execMtprIpl_vms

// MFPR_MCES: hand-written -- see palBoxLib/grains/ for palBox::execMfprMces

// MTPR_MCES: hand-written -- see palBoxLib/grains/ for palBox::execMtprMces

// MFPR_PCBB: hand-written -- see palBoxLib/grains/ for palBox::execMfprPcbb_vms

// MFPR_PRBR: hand-written -- see palBoxLib/grains/ for palBox::execMfprPrbr_vms

// MTPR_PRBR: hand-written -- see palBoxLib/grains/ for palBox::execMtprPrbr_vms

// MFPR_PTBR: hand-written -- see palBoxLib/grains/ for palBox::execMfprPtbr_vms

// MFPR_SCBB: hand-written -- see palBoxLib/grains/ for palBox::execMfprScbb_vms

// MTPR_SCBB: hand-written -- see palBoxLib/grains/ for palBox::execMtprScbb_vms

// MTPR_SIRR: hand-written -- see palBoxLib/grains/ for palBox::execMtprSirr_vms

// MFPR_SISR: hand-written -- see palBoxLib/grains/ for palBox::execMfprSisr_vms

// MFPR_TBCHK: hand-written -- see palBoxLib/grains/ for palBox::execMfprTbchk_vms

// MTPR_TBIA: hand-written -- see palBoxLib/grains/ for palBox::execMtprTbia_vms

// MTPR_TBIAP: hand-written -- see palBoxLib/grains/ for palBox::execMtprTbiap_vms

// MTPR_TBIS: hand-written -- see palBoxLib/grains/ for palBox::execMtprTbis_vms

// MFPR_ESP: hand-written -- see palBoxLib/grains/ for palBox::execMfprEsp_vms

// MTPR_ESP: hand-written -- see palBoxLib/grains/ for palBox::execMtprEsp_vms

// MFPR_SSP: hand-written -- see palBoxLib/grains/ for palBox::execMfprSsp_vms

// MTPR_SSP: hand-written -- see palBoxLib/grains/ for palBox::execMtprSsp_vms

// MFPR_USP: hand-written -- see palBoxLib/grains/ for palBox::execMfprUsp_vms

// MTPR_USP: hand-written -- see palBoxLib/grains/ for palBox::execMtprUsp_vms

// MTPR_TBISD: hand-written -- see palBoxLib/grains/ for palBox::execMtprTbisd_vms

// MTPR_TBISI: hand-written -- see palBoxLib/grains/ for palBox::execMtprTbisi_vms

// MFPR_ASTEN: hand-written -- see palBoxLib/grains/ for palBox::execMfprAsten_vms

// MFPR_ASTSR: hand-written -- see palBoxLib/grains/ for palBox::execMfprAstsr_vms

// MFPR_VPTB: hand-written -- see palBoxLib/grains/ for palBox::execMfprVptb_vms

// MTPR_VPTB: hand-written -- see palBoxLib/grains/ for palBox::execMtprVptb_vms

// MTPR_PERFMON: hand-written -- see palBoxLib/grains/ for palBox::execMtprPerfmon

// WRVPTPTR: hand-written -- see palBoxLib/grains/ for palBox::execWrvptptr_tru64

// MTPR_DATFX: hand-written -- see palBoxLib/grains/ for palBox::execMtprDatfx

// MFPR_VIRBND: hand-written -- see palBoxLib/grains/ for palBox::execMfprVirbnd

// WRVAL: hand-written -- see palBoxLib/grains/ for palBox::execWrval_tru64

// MFPR_SYSPTBR: hand-written -- see palBoxLib/grains/ for palBox::execMfprSysptbr

// TBI: hand-written -- see palBoxLib/grains/ for palBox::execTbi_tru64

// WRENT: hand-written -- see palBoxLib/grains/ for palBox::execWrent_tru64

// SWPIPL: hand-written -- see palBoxLib/grains/ for palBox::execSwpipl_tru64

// RDPS: hand-written -- see palBoxLib/grains/ for palBox::execRdps_tru64

// WRKGP: hand-written -- see palBoxLib/grains/ for palBox::execWrkgp_tru64

// WRUSP: hand-written -- see palBoxLib/grains/ for palBox::execWrusp_tru64

// WRPERFMON: hand-written -- see palBoxLib/grains/ for palBox::execWrperfmon_tru64

// RDUSP: hand-written -- see palBoxLib/grains/ for palBox::execRdusp_tru64

// WHAMI: hand-written -- see palBoxLib/grains/ for palBox::execWhami_tru64

// RETSYS: hand-written -- see palBoxLib/grains/ for palBox::execRetsys_tru64

// WTINT: hand-written -- see palBoxLib/grains/ for palBox::execWtint

// MFPR_WHAMI: hand-written -- see palBoxLib/grains/ for palBox::execMfprWhami

// BPT: hand-written -- see palBoxLib/grains/ for palBox::execBpt

// BUGCHK: hand-written -- see palBoxLib/grains/ for palBox::execBugchk_vms

// CHME: hand-written -- see palBoxLib/grains/ for palBox::execChme_vms

// CHMK: hand-written -- see palBoxLib/grains/ for palBox::execChmk

// CHMS: hand-written -- see palBoxLib/grains/ for palBox::execChms_vms

// CHMU: hand-written -- see palBoxLib/grains/ for palBox::execChmu_vms

// IMB: hand-written -- see palBoxLib/grains/ for palBox::execImb

// INSQHIL: hand-written -- see palBoxLib/grains/ for palBox::execInsqhil_vms

// INSQTIL: hand-written -- see palBoxLib/grains/ for palBox::execInsqtil_vms

// INSQHIQ: hand-written -- see palBoxLib/grains/ for palBox::execInsqhiq_vms

// INSQTIQ: hand-written -- see palBoxLib/grains/ for palBox::execInsqtiq_vms

// INSQUEL: hand-written -- see palBoxLib/grains/ for palBox::execInsquel_vms

// INSQUEQ: hand-written -- see palBoxLib/grains/ for palBox::execInsqueq_vms

// INSQUEL_D: hand-written -- see palBoxLib/grains/ for palBox::execInsquelD_vms

// INSQUEQ_D: hand-written -- see palBoxLib/grains/ for palBox::execInsqueqD_vms

// PROBER: hand-written -- see palBoxLib/grains/ for palBox::execProber_vms

// PROBEW: hand-written -- see palBoxLib/grains/ for palBox::execProbew_vms

// RD_PS: hand-written -- see palBoxLib/grains/ for palBox::execRdPs_vms

// REI: hand-written -- see palBoxLib/grains/ for palBox::execRei

// REMQHIL: hand-written -- see palBoxLib/grains/ for palBox::execRemqhil_vms

// REMQTIL: hand-written -- see palBoxLib/grains/ for palBox::execRemqtil_vms

// REMQHIQ: hand-written -- see palBoxLib/grains/ for palBox::execRemqhiq_vms

// REMQTIQ: hand-written -- see palBoxLib/grains/ for palBox::execRemqtiq_vms

// REMQUEL: hand-written -- see palBoxLib/grains/ for palBox::execRemquel_vms

// REMQUEQ: hand-written -- see palBoxLib/grains/ for palBox::execRemqueq_vms

// REMQUEL_D: hand-written -- see palBoxLib/grains/ for palBox::execRemquelD_vms

// REMQUEQ_D: hand-written -- see palBoxLib/grains/ for palBox::execRemqueqD_vms

// SWASTEN: hand-written -- see palBoxLib/grains/ for palBox::execSwasten_vms

// WR_PS_SW: hand-written -- see palBoxLib/grains/ for palBox::execWrPsSw_vms

// RSCC: hand-written -- see palBoxLib/grains/ for palBox::execRscc_vms

// READ_UNQ: hand-written -- see palBoxLib/grains/ for palBox::execReadUnq

// WRITE_UNQ: hand-written -- see palBoxLib/grains/ for palBox::execWriteUnq

// AMOVRR: hand-written -- see palBoxLib/grains/ for palBox::execAmovrr_vms

// AMOVRM: hand-written -- see palBoxLib/grains/ for palBox::execAmovrm_vms

// INSQHILR: hand-written -- see palBoxLib/grains/ for palBox::execInsqhilr_vms

// INSQTILR: hand-written -- see palBoxLib/grains/ for palBox::execInsqtilr_vms

// INSQHIQR: hand-written -- see palBoxLib/grains/ for palBox::execInsqhiqr_vms

// INSQTIQR: hand-written -- see palBoxLib/grains/ for palBox::execInsqtiqr_vms

// REMQHILR: hand-written -- see palBoxLib/grains/ for palBox::execRemqhilr_vms

// REMQTILR: hand-written -- see palBoxLib/grains/ for palBox::execRemqtilr_vms

// REMQHIQR: hand-written -- see palBoxLib/grains/ for palBox::execRemqhiqr_vms

// REMQTIQR: hand-written -- see palBoxLib/grains/ for palBox::execRemqtiqr_vms

// GENTRAP: hand-written -- see palBoxLib/grains/ for palBox::execGentrap

// CLRFEN: hand-written -- see palBoxLib/grains/ for palBox::execClrfen

// HW_MFPR: hand-written -- see palBoxLib/grains/ for palBox::execHwMfpr

// HW_MTPR: hand-written -- see palBoxLib/grains/ for palBox::execHwMtpr

// HW_REI: hand-written -- see palBoxLib/grains/ for palBox::execHwRei

} // namespace palBox
