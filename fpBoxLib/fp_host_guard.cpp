// ============================================================================
// fpBoxLib/fp_host_guard.cpp -- host floating-point safety self-test (impl)
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
// FILE: fpBoxLib/fp_host_guard.cpp   (task #26, Phase A; 2026-06-10)
//   Compile-time tripwire + runtime host-FP-vs-SoftFloat-oracle self-test.
//   Pinned /fp:strict (MSVC) / -ffp-contract=off (GCC/Clang) by fpBoxLib's
//   CMakeLists so the host `double` arithmetic below is single-rounded; the
//   self-test then proves that at runtime against the integer-exact oracle.
// ============================================================================

#include "fpBoxLib/fp_host_guard.h"

// ----------------------------------------------------------------------------
// Compile-time tripwire. Macros cannot see MSVC /fp:fast or FMA contraction
// (the runtime self-test catches those), but they CAN refuse a build that may
// lower guest FP onto x87 (32-bit x86 without SSE2) or that enabled GCC/Clang
// fast-math. See journals/fp_backend_and_x87_guard_handoff.md section 3.2.
// ----------------------------------------------------------------------------
#if defined(_M_X64) || defined(__x86_64__) \
 || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) \
 || defined(__SSE2__) || defined(__aarch64__) \
 || defined(EMULATR_FP_SOFTFLOAT)
  // OK: scalar SSE2 (x86), scalar FP (AArch64), or the SoftFloat backend.
#else
  #error "Guest FP may lower to x87 (no SSE2/NEON/SoftFloat). Refusing to build."
#endif

#if defined(__FAST_MATH__)
  #error "Guest FP requires strict semantics; /fp:fast / -ffast-math forbidden."
#endif

extern "C" {
#include "softfloat.h"   // vendored Berkeley SoftFloat 3e (float64_t = { uint64_t v; })
}

#include <cmath>     // std::sqrt (scalar sqrtsd under strict FP)
#include <cstdio>    // std::fprintf / std::fflush
#include <cstdlib>   // std::abort
#include <cstring>   // std::memcpy

namespace fpBox {
namespace {

// memcpy bit-casts (no std::bit_cast dependency; matches the bench file).
inline auto toBits(double d) -> uint64_t { uint64_t u; std::memcpy(&u, &d, sizeof u); return u; }
inline auto fromBits(uint64_t u) -> double { double d; std::memcpy(&d, &u, sizeof d); return d; }

enum class Op : uint8_t { Add, Mul, Div, Sqrt };

struct Probe { Op op; char const* name; uint64_t a; uint64_t b; };

// Fixed probe vector. Operands are INPUTS only -- correctness is defined by the
// oracle at runtime, so no hand-picked "expected" result is bound on faith.
// Normals near a rounding boundary catch x87 double-rounding / FMA contraction;
// the subnormal-producing cases catch host flush-to-zero (DAZ/FTZ).
constexpr Probe kProbes[] = {
    { Op::Add,  "add",  0x3FF8000000000000ULL, 0x3FF4000000000000ULL }, // 1.5 + 1.25
    { Op::Mul,  "mul",  0x3FF921FB54442D18ULL, 0x3FF5BF0A8B145769ULL }, // ~1.5708 * ~1.359
    { Op::Div,  "div",  0x3FF0000000000000ULL, 0x4008000000000000ULL }, // 1.0 / 3.0 (rounds)
    { Op::Sqrt, "sqrt", 0x4000000000000000ULL, 0ULL },                  // sqrt(2.0) (rounds)
    { Op::Sqrt, "sqrt", 0x4008000000000000ULL, 0ULL },                  // sqrt(3.0) (rounds)
    { Op::Mul,  "mul",  0x0010000000000000ULL, 0x3FE0000000000000ULL }, // DBL_MIN * 0.5 -> subnormal (FTZ catch)
    { Op::Div,  "div",  0x0010000000000000ULL, 0x4000000000000000ULL }, // DBL_MIN / 2.0 -> subnormal (FTZ catch)
};

} // namespace

auto runHostFpSelfTest() -> FpGuardResult
{
    softfloat_roundingMode = softfloat_round_near_even;   // match host SSE2 default (RNE)

    for (Probe const& p : kProbes) {
        double const   a = fromBits(p.a);
        double const   b = fromBits(p.b);
        float64_t      fa; fa.v = p.a;
        float64_t      fb; fb.v = p.b;

        double    hostD;
        float64_t oracle;
        switch (p.op) {
            case Op::Add:  hostD = a + b;         oracle = f64_add(fa, fb); break;
            case Op::Mul:  hostD = a * b;         oracle = f64_mul(fa, fb); break;
            case Op::Div:  hostD = a / b;         oracle = f64_div(fa, fb); break;
            case Op::Sqrt: hostD = std::sqrt(a);  oracle = f64_sqrt(fa);    break;
        }

        uint64_t const hostBits = toBits(hostD);
        if (hostBits != oracle.v) {
            return FpGuardResult{ false, p.name, p.a, p.b, hostBits, oracle.v };
        }
    }
    return FpGuardResult{};   // ok == true
}

void enforceHostFpSafeOrAbort()
{
    FpGuardResult const r = runHostFpSelfTest();
    if (r.ok) {
        return;
    }

    // Flush BEFORE abort: on MSVC an abort() can truncate a not-yet-flushed
    // stream, and a guard that aborts silently is worse than one that does not.
    std::fprintf(stderr,
        "FATAL: host floating-point self-test FAILED -- guest FP would be silently "
        "WRONG, refusing to run.\n"
        "  op=%s a=0x%016llx b=0x%016llx host=0x%016llx oracle=0x%016llx\n"
        "  The host FP path diverges from the SoftFloat oracle (x87 double-rounding, "
        "FMA contraction, or flush-to-zero). Build the FP path strict / SSE2.\n",
        r.op ? r.op : "?",
        static_cast<unsigned long long>(r.aBits),
        static_cast<unsigned long long>(r.bBits),
        static_cast<unsigned long long>(r.hostBits),
        static_cast<unsigned long long>(r.oracleBits));
    std::fflush(stderr);
    std::abort();
}

} // namespace fpBox
