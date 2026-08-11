// ============================================================================
// coreLib/InstructionGrain.h -- per-slot decoded Alpha instruction
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
// InstructionGrain is the per-slot decoded form of a 32-bit Alpha
// instruction word.  It is built by the DE stage from the dispatch
// tables and rides the slot through every stage until WB retires.
//
// Design contract:
//
//   The grain is a POD: trivially copyable, no virtual table, no
//   destructor work.  All state is plain data.  The leaf function
//   pointer (execFn) is the only behaviour entry point and is set at
//   decode by joining (primaryOpcode, sub-decode field) against the
//   codegen-emitted dispatch tables.
//
//   semFlags is the contract between the grain and the pipeline.
//   Stages consult flag bits rather than switching on opcode; see
//   SemanticFlags.tsv and the generated SemanticFlagsEnum.h for the
//   flag vocabulary.  semFlags is a static property of the grain
//   resolved at decode; per-execution dynamic flags (e.g., did an
//   FP trap actually fire) live on BoxResult.
//
//   pc is the slot's PC -- the address this grain was fetched from.
//   It is immutable through stages.  This is NOT the architectural
//   PC of the CPU (which is a separate register, advanced only at
//   WB).  The architectural PC after retirement is computed by WB
//   from {grain.pc, BoxResult.divertTarget, semFlags}.
//
// Sizing:
//   At present the struct packs to 32 bytes (4 x 8-byte fields plus
//   a few small tail fields padded to 8).  The slot composes a grain
//   plus a BoxResult plus stage tracking; staying compact matters for
//   cache pressure with many slots in flight.
//
// ============================================================================

#ifndef CORELIB_INSTRUCTIONGRAIN_H
#define CORELIB_INSTRUCTIONGRAIN_H

#include <cstdint>

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "grainFactoryLib/generated/DispatchKinds.h"

namespace coreLib {

// Forward declarations break the dependency cycle between InstructionGrain,
// BoxResult, and ExecCtx.  GrainFn is a function pointer type; only the
// names of the parameter and return types need to be visible here.
struct InstructionGrain;
struct BoxResult;
struct ExecCtx;


// Functional box that owns the leaf for a grain.  Lowered from the
// box column in GrainMasterV4.tsv (Ibox, Ebox, Fbox, Mbox, Cbox,
// PalBox).  Carried as a small enum on the grain so the Ibox can
// schedule slots to clusters without re-decoding.
enum class Box : uint8_t {
    Ibox,
    Ebox,
    Fbox,
    Mbox,
    Cbox,
    PalBox,
};


// Returns the canonical name of a Box value (for trace formatting).
constexpr char const* boxName(Box b)
{
    switch (b) {
        case Box::Ibox:   return "Ibox";
        case Box::Ebox:   return "Ebox";
        case Box::Fbox:   return "Fbox";
        case Box::Mbox:   return "Mbox";
        case Box::Cbox:   return "Cbox";
        case Box::PalBox: return "PalBox";
    }
    return "<invalid>";
}


// Type of every leaf function emitted from GrainMasterV4.tsv.
//
// A leaf is a pure function from (grain, ctx) to BoxResult.  It reads
// register state from ctx, computes the instruction's effect, and
// packs the effect into the returned BoxResult.  It does NOT mutate
// architectural state directly: register writes are applied by WB
// from regWriteIdx + regWriteValue, memory effects are applied by
// MEM from memEffect fields, PC redirection is applied at end-of-EX
// from divertTarget.
//
// This is the single indirect call between the dispatch table and
// the per-instruction implementation; it is the seam a future trace
// cache or JIT specializer would inline through.
using GrainFn = BoxResult (*)(InstructionGrain const& grain,
                              ExecCtx const&          ctx);


struct InstructionGrain
{
    // ------------------------------------------------------------------
    // Eight-byte members grouped contiguously
    // ------------------------------------------------------------------

    // Address this instruction was fetched from -- the slot's PC.
    // Set at IF, immutable thereafter.  Read by leaves at EX (branch
    // target math, return-PC writes), by MEM on fault (EXC_ADDR
    // population), and by WB on retire (next-PC computation).  This
    // is NOT the architectural PC of the CPU.
    uint64_t pc = 0;

    // Static dispatch information, resolved at DE from the primary
    // and per-kind sub-tables.  Calling execFn(grain, ctx) at EX
    // invokes the leaf for this opcode.  Never null after a clean
    // decode; unimplemented opcodes resolve to an OPCDEC leaf rather
    // than nullptr so the call site never has to null-check.
    GrainFn execFn = nullptr;

    // Semantic flag set, resolved at DE by joining the dispatch
    // tables against SemanticFlags.tsv.  Pipeline stages consult
    // this for every cross-cutting decision: bypass routing at GR,
    // divert detection at EX, memory drain at MEM, regfile commit
    // at WB.  See generated SemanticFlagsEnum.h for the vocabulary.
    grainFactory::GrainSem semFlags = grainFactory::GrainSem::None;

    // ------------------------------------------------------------------
    // Smaller fields packed at the tail
    // ------------------------------------------------------------------

    // Raw 32-bit instruction word.  Leaves extract operand fields
    // (Ra, Rb, Rc, disp, lit, IMM bit) from this directly rather
    // than carrying parsed copies on the grain; the encoding is the
    // single source of truth for those fields.
    uint32_t encoded = 0;

    // Functional-box routing hint.
    Box box = Box::Ebox;

    // Primary opcode, encoded[31:26].  Copied here for trace
    // formatters that want the opcode without re-extracting from
    // the encoded word; consult `encoded` for sub-decode bits.
    uint8_t primaryOp = 0;

    // Two padding bytes; reserved for future per-grain state if a
    // need surfaces (e.g., a per-grain dispatch-kind copy for trace
    // formatters).  Currently unused.
    uint8_t reserved0 = 0;
    uint8_t reserved1 = 0;
};

} // namespace coreLib

#endif // CORELIB_INSTRUCTIONGRAIN_H
