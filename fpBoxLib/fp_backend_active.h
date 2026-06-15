// ============================================================================
// fpBoxLib/fp_backend_active.h -- process-wide active FP execute backend
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
// FILE: fpBoxLib/fp_backend_active.h   (task #26, Phase C -- grain wiring)
//   The single seam through which the fBox FP leaves reach the FP execute
//   backend.  Leaves call fpBox::activeBackend() and see only IFpBackend, so
//   the concrete backend can change (SoftFloat reference today; a native
//   SSE2/AArch64 fast path later, selected by default with SoftFloat behind
//   EMULATR_FP_SOFTFLOAT) without touching any grain code.
// ============================================================================

#ifndef FPBOXLIB_FP_BACKEND_ACTIVE_H
#define FPBOXLIB_FP_BACKEND_ACTIVE_H

#include "fpBoxLib/fp_backend.h"

namespace fpBox {

// Returns a reference to the process-wide active FP execute backend.  Stable
// for the life of the process (function-static); never null.
IFpBackend& activeBackend() noexcept;

}  // namespace fpBox

#endif  // FPBOXLIB_FP_BACKEND_ACTIVE_H
