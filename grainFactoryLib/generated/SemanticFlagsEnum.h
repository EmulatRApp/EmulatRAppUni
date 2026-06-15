// ============================================================================
// SemanticFlagsEnum.h -- generated for EmulatR V4
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
// Source:    grainFactoryLib/SemanticFlags.tsv
// Generator: grainFactoryLib/codegen/genGrains.py
//
// Edit the source TSV and re-run the generator.  Hand-edits to this
// file will be lost on the next codegen pass.
// ============================================================================


#pragma once

#include <cstdint>

namespace grainFactory {

// Bit-defined semantic flags carried on each InstructionGrain.
// One enum value per row in SemanticFlags.tsv; bit number is
// the authoritative source of truth for cross-module ABI.
enum class GrainSem : uint64_t {
    None         = 0ULL,

    // -- Format --
    S_PalFormat        = 1ULL <<  0,            // CALL_PAL encoding (opcode 0x00)
    S_BraFormat        = 1ULL <<  1,            // Branch encoding (opcodes 0x30..0x3F)
    S_MemFormat        = 1ULL <<  2,            // Memory format encoding
    S_OpFormat         = 1ULL <<  3,            // Integer operate format
    S_FpFormat         = 1ULL <<  4,            // Floating operate format
    S_HwFormat         = 1ULL <<  5,            // PALmode HW_xxx encoding (0x19,0x1B,0x1D,0x1E,0x1F)
    S_JmpFormat        = 1ULL <<  6,            // JMP/JSR/RET/JSR_COROUTINE (opcode 0x1A)
    S_MiscFormat       = 1ULL <<  7,            // MISC encoding (opcode 0x18)

    // -- Operand --
    S_ReadsRa          = 1ULL <<  8,            // Reads source register Ra
    S_ReadsRb          = 1ULL <<  9,            // Reads source register Rb
    S_WritesRc         = 1ULL << 10,            // Writes destination register Rc
    S_HasLit           = 1ULL << 11,            // IMM bit set; 8-bit literal replaces Rb
    S_WritesRa         = 1ULL << 22,            // Writes destination register Ra (loads; BSR/JSR write return PC)

    // -- Regfile --
    S_ReadsInt         = 1ULL << 12,            // Any source operand reads integer regfile
    S_ReadsFp          = 1ULL << 13,            // Any source operand reads FP regfile
    S_WritesInt        = 1ULL << 14,            // Destination is integer regfile
    S_WritesFp         = 1ULL << 15,            // Destination is FP regfile

    // -- Memory --
    S_Load             = 1ULL << 16,            // Produces a MemRead effect
    S_Store            = 1ULL << 17,            // Produces a MemWrite effect
    S_Locked           = 1ULL << 18,            // Load-locked / store-conditional
    S_PhysAddr         = 1ULL << 19,            // EA is physical (HW_LD / HW_ST hint)
    S_NoTbCheck        = 1ULL << 20,            // Suppress TB exceptions (HW_LD hint)
    S_Cacheable        = 1ULL << 21,            // Normal cacheable access (default; absence means uncached)

    // -- Control --
    S_ChangesPC        = 1ULL << 24,            // Grain may divert PC at EX
    S_Uncond           = 1ULL << 25,            // Unconditional control flow change
    S_Branch           = 1ULL << 26,            // PC-relative branch (BR,BSR,Bxx,FBxx)
    S_Indirect         = 1ULL << 27,            // Target from register (JMP/JSR/RET/JSR_COR)
    S_CallBased        = 1ULL << 28,            // Writes Ra with return address (BSR,JSR)
    S_RetBased         = 1ULL << 29,            // Uses RET prediction stack (RET,JSR_COR)

    // -- Privilege --
    S_Privileged       = 1ULL << 32,            // Raises OPCDEC if not in PAL mode
    S_PalEntry         = 1ULL << 33,            // CALL_PAL: enters PALcode at dispatch entry
    S_PalExit          = 1ULL << 34,            // HW_REI: exits PALcode and resumes EXC_ADDR
    S_OpcDec           = 1ULL << 35,            // Reserved/unimplemented; raises OPCDEC
    S_PalTru64         = 1ULL << 36,            // Valid under Tru64/UNIX (OSF/1) PAL personality
    S_PalVms           = 1ULL << 37,            // Valid under OpenVMS PAL personality
    S_PalIntrinsic     = 1ULL << 38,            // CALL_PAL function decoded inline; no PAL transfer (CSERVE etc.)
    S_PalLinux         = 1ULL << 39,            // Valid under Alpha Linux PAL personality (per AARM Table C-15 column 3)

    // -- FpTrap --
    S_FpTrap           = 1ULL << 40,            // May raise IEEE FP trap
    S_FpcrRead         = 1ULL << 41,            // Reads FPCR (MF_FPCR)
    S_FpcrWrite        = 1ULL << 42,            // Writes FPCR (MT_FPCR)
    S_VaxFp            = 1ULL << 43,            // VAX FP encoding (FLTV); unimplemented on 21264

    // -- Ipr --
    S_IprRead          = 1ULL << 48,            // HW_MFPR: reads internal processor register
    S_IprWrite         = 1ULL << 49,            // HW_MTPR: writes internal processor register

    // -- Barrier --
    S_Mb               = 1ULL << 50,            // Memory barrier (MISC 0x4000)
    S_Wmb              = 1ULL << 51,            // Write memory barrier (MISC 0x4400)
    S_TrapBarrier      = 1ULL << 52,            // TRAPB; serializes precise traps
    S_ExcBarrier       = 1ULL << 53,            // EXCB; serializes exceptions
    S_Imb              = 1ULL << 54,            // I-cache memory barrier (CALL_PAL IMB); invalidates traces

    // -- Hint --
    S_NoTrace          = 1ULL << 56,            // Excluded from trace cache (HW_REI, RPCC, IPR access)
    S_RpccLike         = 1ULL << 57,            // Reads cycle counter (RPCC); non-deterministic w.r.t. snapshot
    S_PrefetchOnly     = 1ULL << 58,            // No commit; prefetch hint only (FETCH, FETCH_M, ECB)
    S_WriteHint        = 1ULL << 59,            // Write hint (WH64)

};

// Bitwise operators on GrainSem.  enum class requires explicit
// definition before it can be used as a bit-flag set.
constexpr GrainSem operator|(GrainSem a, GrainSem b) {
    return static_cast<GrainSem>(
        static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}
constexpr GrainSem operator&(GrainSem a, GrainSem b) {
    return static_cast<GrainSem>(
        static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}
constexpr GrainSem operator^(GrainSem a, GrainSem b) {
    return static_cast<GrainSem>(
        static_cast<uint64_t>(a) ^ static_cast<uint64_t>(b));
}
constexpr GrainSem operator~(GrainSem a) {
    return static_cast<GrainSem>(~static_cast<uint64_t>(a));
}
constexpr GrainSem& operator|=(GrainSem& a, GrainSem b) {
    a = a | b;
    return a;
}
constexpr GrainSem& operator&=(GrainSem& a, GrainSem b) {
    a = a & b;
    return a;
}

// Predicates.
constexpr bool any(GrainSem a) {
    return static_cast<uint64_t>(a) != 0ULL;
}
constexpr bool has(GrainSem mask, GrainSem bit) {
    return any(mask & bit);
}

// Per-category masks; OR of every flag in the category.
constexpr GrainSem kFormatMask =
    GrainSem::S_PalFormat |
    GrainSem::S_BraFormat |
    GrainSem::S_MemFormat |
    GrainSem::S_OpFormat |
    GrainSem::S_FpFormat |
    GrainSem::S_HwFormat |
    GrainSem::S_JmpFormat |
    GrainSem::S_MiscFormat;
constexpr GrainSem kOperandMask =
    GrainSem::S_ReadsRa |
    GrainSem::S_ReadsRb |
    GrainSem::S_WritesRc |
    GrainSem::S_HasLit |
    GrainSem::S_WritesRa;
constexpr GrainSem kRegfileMask =
    GrainSem::S_ReadsInt |
    GrainSem::S_ReadsFp |
    GrainSem::S_WritesInt |
    GrainSem::S_WritesFp;
constexpr GrainSem kMemoryMask =
    GrainSem::S_Load |
    GrainSem::S_Store |
    GrainSem::S_Locked |
    GrainSem::S_PhysAddr |
    GrainSem::S_NoTbCheck |
    GrainSem::S_Cacheable;
constexpr GrainSem kControlMask =
    GrainSem::S_ChangesPC |
    GrainSem::S_Uncond |
    GrainSem::S_Branch |
    GrainSem::S_Indirect |
    GrainSem::S_CallBased |
    GrainSem::S_RetBased;
constexpr GrainSem kPrivilegeMask =
    GrainSem::S_Privileged |
    GrainSem::S_PalEntry |
    GrainSem::S_PalExit |
    GrainSem::S_OpcDec |
    GrainSem::S_PalTru64 |
    GrainSem::S_PalVms |
    GrainSem::S_PalIntrinsic |
    GrainSem::S_PalLinux;
constexpr GrainSem kFpTrapMask =
    GrainSem::S_FpTrap |
    GrainSem::S_FpcrRead |
    GrainSem::S_FpcrWrite |
    GrainSem::S_VaxFp;
constexpr GrainSem kIprMask = GrainSem::S_IprRead | GrainSem::S_IprWrite;
constexpr GrainSem kBarrierMask =
    GrainSem::S_Mb |
    GrainSem::S_Wmb |
    GrainSem::S_TrapBarrier |
    GrainSem::S_ExcBarrier |
    GrainSem::S_Imb;
constexpr GrainSem kHintMask =
    GrainSem::S_NoTrace |
    GrainSem::S_RpccLike |
    GrainSem::S_PrefetchOnly |
    GrainSem::S_WriteHint;

} // namespace grainFactory
