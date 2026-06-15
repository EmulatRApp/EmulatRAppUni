// ============================================================================
// fp_softfloat_vs_host_bench.cpp -- cycles/op delta: host double vs SoftFloat
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
// PURPOSE: a "for grins" measurement of the per-operation cost delta between
//   the host hardware FP path (plain double -> scalar SSE2 addsd/mulsd/divsd/
//   sqrtsd) and the SoftFloat oracle (integer f64_add/mul/div/sqrt). It also
//   doubles as a live oracle-agreement check: under round-to-nearest with no
//   x87, no FMA contraction, and FTZ/DAZ clear, the two paths are bit-exact
//   for these correctly-rounded operations, so each operand is CHECKed equal.
//
// WHAT THIS MEASURES (and what it does NOT):
//   - Measures: cycles per op (via __rdtsc where available) or ns per op
//     (std::chrono fallback). This is the meaningful efficiency delta.
//   - Does NOT measure: retired host-instruction count or call depth. Those
//     are static properties, not observable portably at runtime. Get them by
//     disassembling the compiled object (see the dumpbin note at the bottom).
//
// CAVEATS: __rdtsc reads the constant reference clock, not retired uops; turbo
//   and frequency scaling perturb absolute cycles. Treat the RATIO as the
//   signal and the absolute numbers as indicative only. doctest CHECK only
//   (exceptions disabled in V4).
//
// [LOCATE] doctest include path and the test-registration / CMake wiring used
//   by the V4 test tree; this file links the vendored `softfloat` target.
// ============================================================================

#include "doctest.h"   // [LOCATE] adjust to the project's doctest include form

extern "C" {
#include "softfloat.h"   // vendored Berkeley SoftFloat (Release 3); float64_t = { uint64_t v; }
}

#include <cstdint>
#include <cstring>   // memcpy
#include <chrono>
#include <cmath>     // std::sqrt (host sqrt; see [CONFIRM] note below)
#include <initializer_list>   // braced-init range-for over the Op set (loop below)

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  #include <intrin.h>
  #define EMULATR_HAVE_RDTSC 1
#elif (defined(__x86_64__) || defined(__i386__))
  #include <x86intrin.h>
  #define EMULATR_HAVE_RDTSC 1
#else
  #define EMULATR_HAVE_RDTSC 0
#endif

namespace {

// Sink to defeat dead-code elimination: every result is written here.
volatile uint64_t g_sink = 0;

// ---- bit-cast helpers (no std::bit_cast dependency) ------------------------
inline auto toBits(double d) -> uint64_t {
    uint64_t u; std::memcpy(&u, &d, sizeof u); return u;
}
inline auto fromBits(uint64_t u) -> double {
    double d; std::memcpy(&d, &u, sizeof d); return d;
}
inline auto sfFromBits(uint64_t u) -> float64_t {
    float64_t f; f.v = u; return f;
}

// ---- tick source: reference cycles where available, else chrono ns ---------
inline auto tick() -> uint64_t {
#if EMULATR_HAVE_RDTSC
    return static_cast<uint64_t>(__rdtsc());
#else
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

// ---- deterministic operand generation in [1.0, 2.0) ------------------------
// Keeps every add/mul/div/sqrt result normal and finite, so host vs SoftFloat
// is bit-exact and the timing measures the normal path only.
inline auto nextUnitDouble(uint64_t& s) -> double {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;  // LCG
    uint64_t const mant = (s >> 11) & ((1ULL << 52) - 1);
    uint64_t const bits = (uint64_t(0x3FF) << 52) | mant;      // exponent 0 -> [1,2)
    return fromBits(bits);
}

constexpr int    kPairs    = 1024;   // distinct operand pairs
constexpr int    kReps     = 1500;   // inner repetitions for timing
constexpr int    kRounds   = 9;      // rounds; report the minimum (least noise)

struct Operands {
    double a[kPairs];
    double b[kPairs];   // b kept >= 1.0 so div/sqrt stay tame
    Operands() {
        uint64_t s = 0x9E3779B97F4A7C15ULL;
        for (int i = 0; i < kPairs; ++i) { a[i] = nextUnitDouble(s); b[i] = nextUnitDouble(s); }
    }
};

enum class Op { Add, Mul, Div, Sqrt };

inline auto opName(Op o) -> const char* {
    switch (o) {
        case Op::Add:  return "add";
        case Op::Mul:  return "mul";
        case Op::Div:  return "div";
        case Op::Sqrt: return "sqrt";
    }
    return "?";
}

// One timed host pass over all pairs, repeated kReps times. Returns ticks.
auto timeHost(Op o, const Operands& v) -> uint64_t {
    uint64_t best = UINT64_MAX;
    for (int r = 0; r < kRounds; ++r) {
        uint64_t const t0 = tick();
        uint64_t acc = 0;
        for (int rep = 0; rep < kReps; ++rep) {
            for (int i = 0; i < kPairs; ++i) {
                double out;
                switch (o) {
                    case Op::Add:  out = v.a[i] + v.b[i]; break;
                    case Op::Mul:  out = v.a[i] * v.b[i]; break;
                    case Op::Div:  out = v.a[i] / v.b[i]; break;
                    case Op::Sqrt: out = std::sqrt(v.a[i]); break;
                }
                acc ^= toBits(out);
            }
        }
        uint64_t const t1 = tick();
        g_sink ^= acc;
        if (t1 - t0 < best) best = t1 - t0;
    }
    return best;
}

// One timed SoftFloat pass, same shape.
auto timeSoft(Op o, const Operands& v) -> uint64_t {
    softfloat_roundingMode = softfloat_round_near_even;
    uint64_t best = UINT64_MAX;
    for (int r = 0; r < kRounds; ++r) {
        uint64_t const t0 = tick();
        uint64_t acc = 0;
        for (int rep = 0; rep < kReps; ++rep) {
            for (int i = 0; i < kPairs; ++i) {
                float64_t const fa = sfFromBits(toBits(v.a[i]));
                float64_t const fb = sfFromBits(toBits(v.b[i]));
                float64_t out;
                switch (o) {
                    case Op::Add:  out = f64_add(fa, fb); break;
                    case Op::Mul:  out = f64_mul(fa, fb); break;
                    case Op::Div:  out = f64_div(fa, fb); break;
                    case Op::Sqrt: out = f64_sqrt(fa);    break;
                }
                acc ^= out.v;
            }
        }
        uint64_t const t1 = tick();
        g_sink ^= acc;
        if (t1 - t0 < best) best = t1 - t0;
    }
    return best;
}

} // namespace

// NOTE on sqrt: this uses std::sqrt from <cmath>. Under MSVC /fp:precise or
// /fp:strict it lowers to inline scalar sqrtsd; verify it does not become a
// libm call on your toolchain. [CONFIRM] host sqrt is scalar SSE2 sqrtsd.

TEST_CASE("softfloat vs host double: oracle agreement (RNE, bit-exact)") {
    Operands v;
    softfloat_roundingMode = softfloat_round_near_even;

    int mismatches = 0;
    for (int i = 0; i < kPairs; ++i) {
        uint64_t const ab = toBits(v.a[i]);
        uint64_t const bb = toBits(v.b[i]);
        float64_t const fa = sfFromBits(ab);
        float64_t const fb = sfFromBits(bb);

        CHECK(toBits(v.a[i] + v.b[i]) == f64_add(fa, fb).v);
        CHECK(toBits(v.a[i] * v.b[i]) == f64_mul(fa, fb).v);
        CHECK(toBits(v.a[i] / v.b[i]) == f64_div(fa, fb).v);
        CHECK(toBits(std::sqrt(v.a[i])) == f64_sqrt(fa).v);
        (void)ab; (void)bb; (void)mismatches;
    }
}

TEST_CASE("softfloat vs host double: cycles/op delta (for grins)") {
    Operands v;
    double const span = double(kReps) * double(kPairs);

    for (Op o : { Op::Add, Op::Mul, Op::Div, Op::Sqrt }) {
        uint64_t const hostTicks = timeHost(o, v);
        uint64_t const softTicks = timeSoft(o, v);

        double const hostPer = double(hostTicks) / span;
        double const softPer = double(softTicks) / span;
        double const ratio   = (hostPer > 0.0) ? (softPer / hostPer) : 0.0;

#if EMULATR_HAVE_RDTSC
        const char* unit = "ref-cycles/op";
#else
        const char* unit = "ns/op";
#endif
        MESSAGE("op=" << opName(o)
                << "  host=" << hostPer << " " << unit
                << "  soft=" << softPer << " " << unit
                << "  ratio(soft/host)=" << ratio);

        // Harness sanity only -- no hard timing threshold (would be machine-flaky).
        CHECK(hostTicks > 0);
        CHECK(softTicks > 0);
    }
    // Touch the sink so the optimizer cannot drop the timed work.
    CHECK(g_sink == g_sink);
}

// ============================================================================
// Companion: instruction count + call depth (static, NOT measurable here)
// ----------------------------------------------------------------------------
// Build the SoftFloat object with your release flags, then:
//
//   MSVC:   dumpbin /disasm softfloat.dir\Release\f64_div.obj > f64_div.asm
//           (count mnemonics; follow `call` targets for depth into the
//            softfloat_* primitives -- approxRecip / roundPackToF64 / etc.)
//
//   GCC/Clang: objdump -d f64_div.o
//
// For the host side, divsd is a single instruction (~13-14 cycle latency on
// current x86-64), so the host "instruction count" is 1 and the meaningful
// comparison is the cycle ratio this test prints, not the static count.
// ============================================================================
