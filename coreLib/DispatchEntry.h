// ============================================================================
// coreLib/DispatchEntry.h -- per-leaf and per-primary dispatch entry types
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
// Type vocabulary for the codegen-emitted dispatch tables in
// grainFactoryLib/generated/DispatchTables.cpp.  GrainEntry describes
// one populated (primaryOpcode, subDecode) entry; PrimaryEntry
// describes one of the 64 primary-opcode slots.
//
// Hand-written rather than codegen because the struct layouts are
// architectural and stable; only the table contents are TSV-derived.
// If GrainEntry or PrimaryEntry need to change, this header is the
// single edit point and the codegen picks it up automatically.
//
// ============================================================================

#ifndef CORELIB_DISPATCHENTRY_H
#define CORELIB_DISPATCHENTRY_H


#include <cstdint>

#include "coreLib/InstructionGrain.h"   // GrainFn, Box; transitively pulls in
                                        // SemanticFlagsEnum.h and DispatchKinds.h
#include "coreLib/axp_attributes_core.h"

namespace coreLib {

// Forward declaration of the OPCDEC executor leaf.  Lives in
// coreLib/OpcDec.cpp (TBD).  Empty slots in every dense dispatch
// table reference this leaf via kOpcDecEntry, and the sparse Misc
// and Pal lookup helpers fall through to it on default.  An
// unmapped (op, sub) pair therefore raises an OPCDEC trap rather
// than dispatching into garbage.
AXP_HOT BoxResult execOpcDec(InstructionGrain const& g,
                             ExecCtx const&          c) noexcept;


// Per-leaf dispatch descriptor.  Each populated (primaryOpcode,
// subDecode) pair in GrainMasterV4.tsv contributes one GrainEntry to
// the dispatch tables.  The fn pointer is the codegen-named leaf in
// the appropriate box namespace (eBox::execAddl, mBox::execLdq, ...);
// flags carry the semantic flag set for the entry; mnemonic is a
// codegen-emitted literal used by trace formatters.
struct GrainEntry
{
    GrainFn                  fn;        // executor leaf function pointer
    grainFactory::GrainSem   semFlags;  // semantic flag set for this entry
    Box                      box;       // routing hint
    char const*              mnemonic;  // literal for trace formatting
};


// Per-primary-opcode dispatch descriptor.  The primary table holds
// 64 of these, indexed by the 6-bit primary opcode field [31:26].
//
// kind tags how the dispatcher reads the secondary decode field:
//
//   Direct, HwMfpr, HwLd, HwMtpr, HwRei, HwSt
//     The primary opcode IS the operation.  `direct` carries the
//     leaf; `subTable` is null and `subTableLen` is 0.
//
//   IntArith, IntLogical, IntShift, IntMul, ItFp, FltIeee,
//   FltLogical, JmpClass, FpTiExt
//     The dispatcher reads kind-specific bits from the encoding,
//     indexes into `subTable`, and bounds-checks against
//     `subTableLen`.  `direct` holds kOpcDecEntry as a sentinel;
//     it is never read for these kinds.
//
//   Misc
//     The dispatcher invokes lookupMisc(func) to handle the sparse
//     16-bit function field.
//
//   Pal
//     The dispatcher invokes lookupPalTru64(func) or
//     lookupPalVms(func) depending on the CPU's PAL personality.
//
//   Reserved
//     The primary opcode is unimplemented (e.g., 0x01..0x07 or 0x15
//     on 21264).  `direct` is kOpcDecEntry; dispatch raises OPCDEC.
struct PrimaryEntry
{
    grainFactory::DispatchKind  kind;
    GrainEntry                  direct;       // valid for Direct/HwXxx; OPCDEC otherwise
    GrainEntry const*           subTable;     // valid for sub-table kinds; null otherwise
    uint16_t                    subTableLen;  // sub-table size for bounds check; 0 when unused
    char const*                 mnemonic;     // primary-opcode group name (e.g., "INTA")
};


// OPCDEC sentinel.  Defined in DispatchTables.cpp.  Empty slots of
// every dense sub-table reference this single entry, so an unmapped
// (op, sub) pair always reaches the OPCDEC executor.
extern GrainEntry const kOpcDecEntry;

} // namespace coreLib

#endif // CORELIB_DISPATCHENTRY_H
