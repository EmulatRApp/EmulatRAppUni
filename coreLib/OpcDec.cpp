// ============================================================================
// coreLib/OpcDec.cpp -- OPCDEC executor for unmapped (op, sub) pairs
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
// execOpcDec is the leaf reached when an instruction's primary or
// sub-decode field hits an unmapped slot in the dispatch tables.
// kOpcDecEntry (defined in DispatchTables.cpp) carries a function
// pointer to this leaf, and every empty slot in every dense sub-table
// references that sentinel.  The Misc and Pal sparse lookup helpers
// fall through to the same sentinel on default.
//
// The leaf does no architectural mutation -- it returns a BoxResult
// with faultCode = kFaultOpcDec.  WB delivers the fault by saving the
// producing slot's grain.pc to EXC_ADDR and entering PALcode at the
// OPCDEC dispatch entry; the architectural register file and memory
// remain untouched until PALcode handles the trap.
//
// ============================================================================

#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

namespace coreLib {

AXP_HOT
BoxResult execOpcDec(InstructionGrain const&         g,
                     [[maybe_unused]] ExecCtx const& c) noexcept
{
    BoxResult r;
    r.semFlags  = g.semFlags | grainFactory::GrainSem::S_OpcDec;
    r.faultCode = kFaultOpcDec;
    return r;
}

} // namespace coreLib
