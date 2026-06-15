// ============================================================================
// fpBoxLib/fp_backend_softfloat.h -- SoftFloat reference implementation of IFpBackend
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
// The deterministic, bit-identical-across-hosts FP oracle (task #26, Phase B).
// Rounding is passed as data (set on SoftFloat's global before each op) and
// exception flags are harvested as data -- no host control word (MXCSR/FPSR)
// is touched, so results are identical on every host. This is the truth source
// the optional host fast-path backends are validated against.
// ============================================================================

#ifndef FPBOXLIB_FP_BACKEND_SOFTFLOAT_H
#define FPBOXLIB_FP_BACKEND_SOFTFLOAT_H

#include "fpBoxLib/fp_backend.h"

namespace fpBox {

// NOTE: not `final` -- HostBackend (fp_backend_host.h) inherits this as its stub
// base so the native fast-path seam is real while it delegates to SoftFloat.
class SoftFloatBackend : public IFpBackend {
public:
    auto addT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto subT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto mulT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto divT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto sqrtT(uint64_t a,             FpExecCtx const&) -> FpResult override;

    auto addS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto subS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto mulS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto divS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto sqrtS(uint64_t a,             FpExecCtx const&) -> FpResult override;

    auto cmpT (FpCompare, uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;

    auto cvtTS(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtST(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtTQ(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtQT(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtQS(uint64_t a, FpExecCtx const&) -> FpResult override;

    // VAX F/G float (see fp_backend.h for the register-image scaling identity).
    auto addF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto subF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto mulF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto divF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto sqrtF(uint64_t a,             FpExecCtx const&) -> FpResult override;
    auto addG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto subG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto mulG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto divG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto sqrtG(uint64_t a,             FpExecCtx const&) -> FpResult override;
    auto cmpG (FpCompare, uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult override;
    auto cvtGF(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtGD(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtDG(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtGQ(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtQF(uint64_t a, FpExecCtx const&) -> FpResult override;
    auto cvtQG(uint64_t a, FpExecCtx const&) -> FpResult override;
};

} // namespace fpBox

#endif // FPBOXLIB_FP_BACKEND_SOFTFLOAT_H
