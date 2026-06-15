// ============================================================================
// coreLib/BoxResult.h -- per-grain effect bag for EmulatR V4
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
// BoxResult is the value object every leaf function returns.  It is the
// communication channel from grain execution down to the CPU's
// architectural state mutation, drained at successive pipeline stages:
//
//   EX  -- inspects divert + divertTarget; squashes IF/DE/GR slots and
//          sets new fetch PC if divert fires.
//   MEM -- applies memEffect (load fill, store publish); commits
//          regWriteValue into the appropriate regfile slot (selected
//          by regWriteIdx + regWriteIsFp); raises faultCode if the
//          memory access faulted.  Combining memory effect application
//          with regfile commit at MEM matches 21264 stage 6 (Dcache
//          Access) silicon behaviour and was profiled in V3 to save
//          cycles versus a separate WB-stage commit.
//
//          Load sign-extension policy at the MEM drain is inferred
//          from memSize and regWriteIsFp -- the leaf does not pack a
//          separate signedness bit:
//
//            memSize = 1, regWriteIsFp = false  -> LDBU; zero-extend
//            memSize = 2, regWriteIsFp = false  -> LDWU; zero-extend
//            memSize = 4, regWriteIsFp = false  -> LDL/LDL_L; sign-
//                                                   extend 32 -> 64
//            memSize = 8, regWriteIsFp = false  -> LDQ/LDQ_L/LDQ_U;
//                                                   no extension
//            memSize = 4, regWriteIsFp = true   -> LDS; IEEE-S reformat
//            memSize = 8, regWriteIsFp = true   -> LDT/LDF/LDG; FP
//                                                   format-specific
//
//          Locked-load (S_Locked + S_Load) sets the per-CPU
//          reservation after the fill; store-conditional (S_Locked +
//          S_Store) checks-and-clears the reservation before the
//          publish, writes regWriteValue = 1/0 to Ra per success.
//   WB  -- retires the slot; advances the CPU's architectural PC from
//          {slot.grain.pc, divertTarget}; on faultCode != 0, saves
//          slot.grain.pc to EXC_ADDR and enters PALcode.  WB does NOT
//          touch the regfile; that drain has already happened at MEM.
//
// Design contract:
//
//   The BoxResult is a flat POD; unused fields hold sentinel values.
//   The semFlags field is the authoritative predicate for "is this
//   field meaningful" -- a leaf that does not produce a memory effect
//   leaves memSize at kNoMemEffect AND does not set S_Load/S_Store in
//   semFlags.  The two are kept in sync; the codegen-emitted leaf
//   bodies establish that contract on every row.
//
//   The architectural PC of the producing slot is NOT carried on the
//   BoxResult.  It is grain.pc on the slot, accessible to MEM and WB
//   via the slot context.  divertTarget is the value the architectural
//   PC will become after WB retires, when divert fires.
//
//   No merge() operator.  In V4 each grain produces exactly one
//   BoxResult; there is nothing to merge across grains.  This
//   structurally precludes V3's merge-drops-divertTarget defect.
//
// Sizing:
//   At present the struct packs to ~48 bytes (one cache line is 64).
//   Fields are ordered to keep the 8-byte members contiguous so the
//   compiler does not insert padding between them.
//
// ============================================================================

#ifndef CORELIB_BOXRESULT_H
#define CORELIB_BOXRESULT_H

#include <cstdint>

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

namespace coreLib {

// Sentinel for "no register write" -- matches Alpha R31/F31 architectural
// zero-write convention.  A leaf that produces no register effect leaves
// regWriteIdx == kNoRegWrite; WB skips the commit when it sees this.
constexpr uint8_t kNoRegWrite = 31;

// Sentinel for "no memory effect" -- memSize == 0 means neither load
// nor store.  Otherwise valid sizes are 1, 2, 4, 8 (byte/word/long/quad).
constexpr uint8_t kNoMemEffect = 0;

// Fault code 0 means "no fault."  Non-zero values are PAL trap kinds
// per Alpha SRM; the exact numeric mapping is established as the trap
// delivery path lands (post-v1 for full coverage).  v1 uses the small
// set below.  Values 0-3 are the original opcode-dispatch faults; 4+
// covers memory-management faults consumed by the MEM-stage drainer
// when it converts a TranslationResult into a faultCode and short-
// circuits the regfile commit.
constexpr uint16_t kNoFault            = 0;
constexpr uint16_t kFaultOpcDec        = 1;  // unimplemented opcode (sub-table OPCDEC)
constexpr uint16_t kFaultPrivileged    = 2;  // privileged op outside PAL mode
constexpr uint16_t kFaultUnimplemented = 3;  // stub leaf body; replace with real impl

// Memory-management faults.  Raised by the MEM-stage drainer when the
// translator or the GuestMemory access reports failure; never set by
// the leaf at EX (the leaf only declares the access via memAddr /
// memSize / memIsStore).  PAL trap delivery at WB inspects faultCode
// and routes the trap to the matching SRM entry point.
constexpr uint16_t kFaultUnaligned     = 4;  // access size and EA disagree on alignment
constexpr uint16_t kFaultDtbMiss       = 5;  // DTB miss; PALcode page-walks and refills
constexpr uint16_t kFaultItbMiss       = 6;  // ITB miss; instruction-fetch translation failed
constexpr uint16_t kFaultAcv           = 7;  // access violation (mode / permission denied)
constexpr uint16_t kFaultFor           = 8;  // fault-on-read; PTE FOR bit set
constexpr uint16_t kFaultFow           = 9;  // fault-on-write; PTE FOW bit set
constexpr uint16_t kFaultFoe           = 10; // fault-on-execute; PTE FOE bit set
constexpr uint16_t kFaultBusError      = 11; // physical access failed at GuestMemory
constexpr uint16_t kFaultNonCanonical  = 12; // VA outside the canonical sign-extended window

// Halt request.  Raised by CALL_PAL HALT; the pipeline driver
// intercepts this at WB and stops the run cleanly.  Distinct from
// the trap-bearing fault codes above -- HALT is a graceful shutdown
// signal, not a fault to be delivered into PALcode.
constexpr uint16_t kFaultHalt          = 13;

// DTB double miss.  A VPTE-format HW_LD (the PALcode page-table-walk PTE
// fetch) that itself DTB-misses -- the page-table page is not mapped.  On
// EV6 this vectors to DTBM_DOUBLE, not DTBM_SINGLE; routing it to SINGLE
// makes the single-miss handler re-enter itself forever (its own VPTE load
// re-misses).  Both reference models (AXPBox virt2phys VPTE branch, SimH
// alpha_ev5_tlb) split single vs double this way.
constexpr uint16_t kFaultDtbMissDouble = 14;


struct BoxResult
{
    // ------------------------------------------------------------------
    // Semantic flag set
    // ------------------------------------------------------------------
    // Carried into the result for stage drain logic.  Initially copied
    // by the leaf from the grain's static flag set; the leaf may set
    // additional dynamic flags (e.g., S_FpTrap when a trap actually
    // fires) but should not contradict static flags inherited from
    // GrainMasterV4.tsv.
    grainFactory::GrainSem semFlags = grainFactory::GrainSem::None;

    // ------------------------------------------------------------------
    // Eight-byte members grouped contiguously (compiler packs 8-byte
    // fields naturally; mixing widths invites padding holes).
    // ------------------------------------------------------------------

    // Register write effect.  regWriteValue is the value to commit;
    // regWriteIdx selects the destination slot in the regfile chosen
    // by regWriteIsFp.  regWriteIdx == kNoRegWrite suppresses commit.
    // Commit happens at MEM, not WB (see drain map above).
    //
    // Per Alpha encoding convention each grain writes at most one
    // register: loads, BSR, JSR write Ra; Op-format writes Rc; STQ_C
    // writes Ra with the success indicator.  The flag set carries
    // S_WritesRa or S_WritesRc to disambiguate which encoding field
    // the index came from; the regfile commit at MEM does not need to
    // know that distinction since regWriteIdx is already resolved.
    uint64_t regWriteValue = 0;

    // Memory effect address.  Effective address for both loads and
    // stores; for HW_LD/HW_ST may be physical (S_PhysAddr in semFlags).
    uint64_t memAddr = 0;

    // Memory effect data.  Meaningful only for stores; loads receive
    // their fill at MEM and pack it into a register write effect.
    uint64_t memData = 0;

    // Translated physical address of the memory effect.  Set by the
    // MEM-stage drainer (applyMemEffect) after VA->PA translation; for
    // S_PhysAddr accesses (HW_LD/HW_ST/LDQP/STQP) it equals memAddr
    // because translation is bypassed.  Diagnostic-only: lets the retire
    // trace show the effective/virtual address (memAddr) alongside the
    // physical address actually accessed (memPhysAddr), so physical-vs-
    // translated addressing is auditable per instruction.  Zero when the
    // access faulted before PA resolution or no memory effect occurred.
    uint64_t memPhysAddr = 0;

    // Divert target.  When divert == true, the address WB advances
    // the architectural PC to.  For BR/BSR/Bxx: pc + 4 + sext(disp<<2).
    // For JMP/JSR/RET: target from Rb.  For CALL_PAL: the PAL dispatch
    // entry.  For HW_REI: the saved EXC_ADDR.
    uint64_t divertTarget = 0;

    // ------------------------------------------------------------------
    // Smaller fields packed at the tail
    // ------------------------------------------------------------------

    uint16_t faultCode    = kNoFault;
    uint8_t  regWriteIdx  = kNoRegWrite;
    uint8_t  memSize      = kNoMemEffect;   // 0, 1, 2, 4, or 8
    bool     regWriteIsFp = false;          // true selects FP regfile
    bool     memIsStore   = false;          // true is store, false is load
    bool     divert       = false;          // true requests PC redirect at EX
};

} // namespace coreLib

#endif // CORELIB_BOXRESULT_H
