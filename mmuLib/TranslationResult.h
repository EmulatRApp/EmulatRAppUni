// ============================================================================
// mmuLib/TranslationResult.h -- translator outcome enum and fault mapper
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
// The translator's output is a TranslationResult plus, on success, a
// physical address.  This header defines the enum and a small mapper
// that converts a non-Success TranslationResult into the matching
// kFault* constant from coreLib/BoxResult.h, so the MEM-stage drainer
// can convert in one line:
//
//   if (tr != TranslationResult::Success) {
//       result.faultCode = mmuLib::toFaultCode(tr);
//       return;
//   }
//
// Keep the enum and the mapper in this header (no .cpp) -- both are
// trivial enough that header-only avoids a translation-unit hop, and
// it lets the translator return TranslationResult by value without
// pulling in BoxResult.h transitively.
//
// ============================================================================

#ifndef MMULIB_TRANSLATIONRESULT_H
#define MMULIB_TRANSLATIONRESULT_H

#include <cstdint>

#include "coreLib/BoxResult.h"

namespace mmuLib {

// ---------------------------------------------------------------------------
// TranslationResult -- the outcome of a VA-to-PA translation attempt.
// ---------------------------------------------------------------------------
// Success indicates pa_out is valid; any other value indicates a
// translation fault that the MEM drainer must turn into a faultCode.
// NotKseg is internal to the kseg detector and never surfaces from
// the public translator entry points -- if you see it in the field
// something is wired wrong.
enum class TranslationResult : uint8_t {
    Success         = 0,
    NonCanonical    = 1,   // VA outside the canonical sign-extended window
    NotKseg         = 2,   // internal: kseg detector saw no match, keep walking
    DtbMiss         = 3,   // DTB lookup failed; PALcode refills
    ItbMiss         = 4,   // ITB lookup failed; instruction fetch translation failed
    AccessViolation = 5,   // mode or permission denied (no FOR/FOW/FOE bit set)
    FaultOnRead     = 6,   // PTE FOR bit set
    FaultOnWrite    = 7,   // PTE FOW bit set
    FaultOnExecute  = 8,   // PTE FOE bit set
    BusError        = 9,   // page-walk read of a PTE returned MEM error
    Unaligned       = 10,  // EA fails the access-size alignment requirement
};


// ---------------------------------------------------------------------------
// toFaultCode -- map a non-Success TranslationResult to a kFault* constant.
// ---------------------------------------------------------------------------
// Success and NotKseg are both mapped to kNoFault; the caller is
// expected to treat them as non-fault states (Success = pa_out is
// valid, NotKseg = the public entry point should never have surfaced
// this).  Every other variant lands on the matching memory-management
// fault constant in coreLib/BoxResult.h.
constexpr uint16_t toFaultCode(TranslationResult tr) noexcept
{
    switch (tr) {
        case TranslationResult::Success:         return coreLib::kNoFault;
        case TranslationResult::NotKseg:         return coreLib::kNoFault;
        case TranslationResult::NonCanonical:    return coreLib::kFaultNonCanonical;
        case TranslationResult::DtbMiss:         return coreLib::kFaultDtbMiss;
        case TranslationResult::ItbMiss:         return coreLib::kFaultItbMiss;
        case TranslationResult::AccessViolation: return coreLib::kFaultAcv;
        case TranslationResult::FaultOnRead:     return coreLib::kFaultFor;
        case TranslationResult::FaultOnWrite:    return coreLib::kFaultFow;
        case TranslationResult::FaultOnExecute:  return coreLib::kFaultFoe;
        case TranslationResult::BusError:        return coreLib::kFaultBusError;
        case TranslationResult::Unaligned:       return coreLib::kFaultUnaligned;
    }
    return coreLib::kFaultUnimplemented;   // unreachable; defensive default
}

} // namespace mmuLib

#endif // MMULIB_TRANSLATIONRESULT_H
