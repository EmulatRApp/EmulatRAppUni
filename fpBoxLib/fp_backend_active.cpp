// ============================================================================
// fpBoxLib/fp_backend_active.cpp -- active FP backend selection
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
// FILE: fpBoxLib/fp_backend_active.cpp   (task #26, Phase C -- grain wiring)
//   Owns the choice of concrete FP backend.  Today: the SoftFloat reference
//   (deterministic, bit-identical across hosts; also the x87-guard self-test
//   oracle in main.cpp).
//
// TODO TABLE:
//   TODO(fp-native): add a native SSE2/AArch64 backend and make it the default,
//     gating SoftFloat behind EMULATR_FP_SOFTFLOAT.  The native path must be
//     validated bit-for-bit against the SoftFloat oracle (the differential
//     harness in tests/fBoxLib/fp_softfloat_vs_host_bench.cpp) before it can
//     answer activeBackend().
// ============================================================================

#include "fpBoxLib/fp_backend_active.h"
#include "fpBoxLib/fp_backend_softfloat.h"
#include "fpBoxLib/fp_backend_host.h"
#include "fpBoxLib/fp_host_guard.h"

#include <cstdlib>   // std::getenv

namespace fpBox {

// One-time backend selection.  The result is captured in a function-static
// reference, so the hot path is a single virtual call through IFpBackend with
// NO per-op selection cost.  Decision inputs, in priority order:
//   1. EMULATR_FP_SOFTFLOAT (env, then compile macro) -> force the deterministic
//      SoftFloat reference.  The env knob is the differential-harness / debug
//      lever (A/B host vs oracle on the same binary).
//   2. Otherwise the native host path, BUT only if it is proven bit-for-bit
//      identical to the SoftFloat oracle on the fixed probe vector
//      (runHostFpSelfTest().ok).  A non-conforming host (x87 lowering, FMA
//      contraction, un-disable-able flush-to-zero) silently falls back to
//      SoftFloat instead of producing wrong guest FP.
// TODO(fp-settings, task #7): let an EmulatR QSettings element (fpBackend =
//   softfloat|native|auto) seed this, with the env override still winning.
IFpBackend& activeBackend() noexcept
{
    static IFpBackend& s_selected = []() -> IFpBackend& {
        static SoftFloatBackend s_softFloat;   // deterministic reference / oracle
        static HostBackend      s_host;        // native std:: fast path (stub today)

        if (std::getenv("EMULATR_FP_SOFTFLOAT") != nullptr) {
            return s_softFloat;
        }
#if defined(EMULATR_FP_SOFTFLOAT)
        return s_softFloat;
#else
        return runHostFpSelfTest().ok ? static_cast<IFpBackend&>(s_host)
                                      : static_cast<IFpBackend&>(s_softFloat);
#endif
    }();
    return s_selected;
}

}  // namespace fpBox
