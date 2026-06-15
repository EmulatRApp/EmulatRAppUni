// ============================================================================
// fBoxLib/grains/FpExec.h -- shared FP leaf helpers (qualifier + FPCR fold)
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
// ============================================================================
//
// FILE: fBoxLib/grains/FpExec.h   (task #26, Phase C -- grain wiring)
//   Two helpers every fBox FP arithmetic/convert leaf uses to drive the
//   IFpBackend seam with full qualifier support and to integrate results with
//   the FPCR:
//     (1) fpVariantFromEncoded -- decode the rounding/trap qualifier from the
//         raw operate function field, so all 16 trap-mode variants funnel
//         through ONE consolidated leaf per base op (the leaf is qualifier-
//         agnostic; the qualifier becomes data passed to the backend).
//     (2) foldFpcrExc -- fold the backend's raw IEEE exception flags into the
//         architectural FPCR sticky bits (FPCR<57:52>), the "FPCR hazard"
//         integration: a later MF_FPCR / FP-branch sees the conditions this op
//         produced.  Sticky bits are recorded independent of trap-enable state.
//
//   Why decode raw bits here instead of coreLib::decodeVariant(): that helper
//   expects a NORMALIZED 5-bit index whose layout does not match the raw Alpha
//   function field, and it has no callers.  The raw field layout is documented
//   and stable (grainFactoryLib/codegen/expandFpVariants.py), so we decode it
//   directly -- one source of truth, testable against the SoftFloat oracle.
// ============================================================================

#ifndef FBOXLIB_GRAINS_FPEXEC_H
#define FBOXLIB_GRAINS_FPEXEC_H

#include <cstdint>

#include "coreLib/alpha_fpcr_core.h"   // coreLib::ArithmeticStatus (FPCR fold)
#include "coreLib/fp_variant_core.h"   // coreLib::FPVariant / FpRoundingMode / FPTrapMode
#include "fpBoxLib/fp_backend.h"       // fpBox::FpExc

namespace fBox {

// ----------------------------------------------------------------------------
// fpVariantFromEncoded -- decode the FP qualifier from the raw operate function
// field.  Alpha operate (opcode 0x14 / 0x16) func = encoded[15:5]:
//   bits[7:6]  rounding : 0x0 chop (/C), 0x1 -inf (/M), 0x2 nearest, 0x3 dyn (/D)
//   bits[10:8] trap     : 0x0 default, 0x1 /U, 0x5 /SU, 0x7 /SUI
// (Authority: grainFactoryLib/codegen/expandFpVariants.py.)
// ----------------------------------------------------------------------------
[[nodiscard]] inline coreLib::FPVariant
fpVariantFromEncoded(uint32_t encoded) noexcept
{
    uint16_t const func = static_cast<uint16_t>((encoded >> 5) & 0x7FFu);
    uint8_t  const rnd  = static_cast<uint8_t>((func >> 6) & 0x3u);   // bits[7:6]
    uint8_t  const trp  = static_cast<uint8_t>((func >> 8) & 0x7u);   // bits[10:8]

    coreLib::FpRoundingMode rm;
    switch (rnd) {
        case 0x0: rm = coreLib::FpRoundingMode::RoundTowardZero; break;  // /C
        case 0x1: rm = coreLib::FpRoundingMode::RoundDown;       break;  // /M
        case 0x2: rm = coreLib::FpRoundingMode::RoundToNearest;  break;  // (normal)
        default:  rm = coreLib::FpRoundingMode::UseFPCR;         break;  // 0x3 = /D
    }

    coreLib::FPTrapMode tm = coreLib::FPTrapMode::None;
    bool inexactEnable = false;
    switch (trp) {
        case 0x0: tm = coreLib::FPTrapMode::None;      break;           // default
        case 0x1: tm = coreLib::FPTrapMode::Underflow; break;           // /U
        case 0x5: tm = coreLib::FPTrapMode::SU;        break;           // /SU
        case 0x7: tm = coreLib::FPTrapMode::SUI; inexactEnable = true; break;  // /SUI
        default:  tm = coreLib::FPTrapMode::None;      break;
    }

    return coreLib::FPVariant(rm, tm, inexactEnable);
}

// ----------------------------------------------------------------------------
// foldFpcrExc -- OR the backend's raw IEEE exception flags into FPCR<57:52>
// (sticky; 0->1 only) and recompute SUM.  Recorded regardless of trap-enable
// state per the arch ref; actual arithmetic-trap DELIVERY is deferred.
// ----------------------------------------------------------------------------
inline void foldFpcrExc(uint64_t& fpcr, fpBox::FpExc const& e) noexcept
{
    coreLib::ArithmeticStatus s;
    s.invalid     = e.inv;
    s.divByZero   = e.dze;
    s.overflow    = e.ovf;
    s.underflow   = e.unf;
    s.inexact     = e.ine;
    s.intOverflow = e.iov;
    s.applyToFpcr(fpcr);   // sets sticky bits + recomputes SUM (alpha_fpcr_core.h)
}

}  // namespace fBox

#endif  // FBOXLIB_GRAINS_FPEXEC_H
