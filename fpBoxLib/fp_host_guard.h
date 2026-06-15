// ============================================================================
// fpBoxLib/fp_host_guard.h -- host floating-point safety self-test (x87 guard)
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
// PURPOSE (task #26, Phase A; design journals/fp_backend_and_x87_guard_handoff.md):
//   Prove at startup that this build's HOST floating-point path is single-rounded
//   and x87/FMA/flush-to-zero free, by checking it bit-for-bit against the
//   SoftFloat oracle on a fixed probe vector. A build whose host FP diverges
//   would make every guest FP result silently wrong -- a correctness catastrophe
//   -- so such a build must be unrunnable.
//
// MECHANISM vs POLICY (deliberate split, Tim 2026-06-10):
//   - runHostFpSelfTest() is PURE DETECTION: it never prints and never aborts.
//     It returns a structured result carrying the first divergence (op, operands,
//     host bits, oracle bits). Safe to call from a doctest (CHECK the result).
//   - enforceHostFpSafeOrAbort() is the EMULATOR STARTUP POLICY: it runs the
//     detector and, on failure, writes a flushed stderr diagnostic and
//     std::abort()s. NEVER call this from a test (it would kill the test binary).
//
// The compile-time tripwire (refusing x87 / -ffast-math builds) lives in the .cpp.
// ============================================================================

#ifndef FPBOXLIB_FP_HOST_GUARD_H
#define FPBOXLIB_FP_HOST_GUARD_H

#include <cstdint>

namespace fpBox {

// Result of the host-FP-vs-oracle self-test. ok == true means every probe
// agreed bit-for-bit. On failure the remaining fields name the FIRST divergence
// so the caller's diagnostic can be specific.
struct FpGuardResult {
    bool        ok        {true};
    char const* op        {nullptr};   // "add" / "mul" / "div" / "sqrt" (static)
    uint64_t    aBits     {0};         // operand A (IEEE double bit pattern)
    uint64_t    bBits     {0};         // operand B (0 for sqrt)
    uint64_t    hostBits  {0};         // host double result bits
    uint64_t    oracleBits{0};         // SoftFloat result bits
};

// Pure detection -- compares the host double path against the SoftFloat oracle
// on a fixed probe vector (normals near rounding boundaries + subnormal cases
// that catch flush-to-zero). Never prints, never aborts. Returns the result.
auto runHostFpSelfTest() -> FpGuardResult;

// Startup policy -- run the self-test; on failure emit a flushed stderr
// diagnostic naming the divergence and std::abort(). Call once at startup,
// before any guest FP can execute.
void enforceHostFpSafeOrAbort();

} // namespace fpBox

#endif // FPBOXLIB_FP_HOST_GUARD_H
