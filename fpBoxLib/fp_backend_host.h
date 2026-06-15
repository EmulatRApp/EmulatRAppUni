// ============================================================================
// fpBoxLib/fp_backend_host.h -- native (std::) host-FP fast-path backend
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
// PURPOSE (task #26, Phase D): the native host-FP fast path -- IEEE arithmetic
//   via std::bit_cast<double>/float + the host scalar SSE2/NEON FP unit, which
//   is far faster than SoftFloat.  Selected by activeBackend() ONLY when the
//   host path has been proven bit-for-bit identical to the SoftFloat oracle at
//   startup (fpBox::runHostFpSelfTest().ok); otherwise SoftFloat is used.
//
// STATUS: STUB.  It inherits SoftFloatBackend so the seam is REAL and every
//   result is already correct -- HostBackend can be selected today and behaves
//   exactly like SoftFloat.  The native speedup arrives incrementally:
//
//   TODO(fp-native): override the IEEE kernels (addT/subT/mulT/divT/sqrtT, the
//     S-format set, cmpT, and the IEEE conversions) with the host-double path
//     (std::bit_cast + host op, rounding driven from FPVariant/FPCR via <cfenv>,
//     flags harvested with feclearexcept/fetestexcept).  Each override MUST pass
//     the differential harness (host vs SoftFloat oracle, bit-equal) before it
//     ships -- that is what runHostFpSelfTest() / the guard exist to enforce.
//   VAX (addF/G ... cvtQG) is DELIBERATELY left on the inherited SoftFloat path:
//     it is not performance-critical and not yet oracle-validated.
// ============================================================================

#ifndef FPBOXLIB_FP_BACKEND_HOST_H
#define FPBOXLIB_FP_BACKEND_HOST_H

#include "fpBoxLib/fp_backend_softfloat.h"

namespace fpBox {

class HostBackend final : public SoftFloatBackend {
    // No overrides yet: inherits the (correct) SoftFloat implementations.
    // Native std:: kernels are added here per the TODO(fp-native) table above.
};

} // namespace fpBox

#endif // FPBOXLIB_FP_BACKEND_HOST_H
