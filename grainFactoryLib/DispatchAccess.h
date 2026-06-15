// ============================================================================
// grainFactoryLib/DispatchAccess.h -- function-style accessor for primary table
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
// The codegen-emitted DispatchTables.cpp defines
// grainFactory::g_primaryTable as a 64-entry const array.  Reaching
// that array directly from another translation unit via an extern
// declaration tripped over MSVC name mangling under /permissive-
// when the array bound was specified on one side and not the other,
// and produced spurious LNK2001 errors.
//
// This header papers over the issue by exposing a function-style
// accessor.  Callers (the pipeline driver, future tooling) call
// primaryEntry(op) and get a const reference to the dispatch entry.
// The function lives in DispatchAccess.cpp where the array is
// declared in the same translation unit, so the cross-TU symbol
// the linker resolves is the function -- not the array.
//
// The Misc / Pal lookup helpers re-declare the codegen-emitted
// sparse helpers from DispatchTables.cpp; they are stable function
// symbols and are exposed here for the same single-include
// convenience.
//
// ============================================================================

#ifndef GRAINFACTORYLIB_DISPATCHACCESS_H
#define GRAINFACTORYLIB_DISPATCHACCESS_H

#include <cstdint>

#include "coreLib/DispatchEntry.h"

namespace grainFactory {

// Returns the primary-table entry for the given primary opcode
// (encoded[31:26]).  primaryOp is masked to 6 bits internally.
coreLib::PrimaryEntry const& primaryEntry(uint8_t primaryOp) noexcept;

// Sparse helpers for the Misc and Pal dispatch kinds.  Defined in
// DispatchTables.cpp; re-declared here so DispatchAccess.h is the
// single include point for the dispatch surface.
coreLib::GrainEntry const* lookupMisc(uint32_t func) noexcept;
coreLib::GrainEntry const* lookupPalTru64(uint32_t func) noexcept;
coreLib::GrainEntry const* lookupPalVms(uint32_t func) noexcept;

} // namespace grainFactory

#endif // GRAINFACTORYLIB_DISPATCHACCESS_H
