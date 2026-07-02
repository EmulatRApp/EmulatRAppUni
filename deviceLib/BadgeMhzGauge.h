#ifndef EMULATR_DEVICELIB_BADGEMHZGAUGE_H
#define EMULATR_DEVICELIB_BADGEMHZGAUGE_H
// ============================================================================
// BadgeMhzGauge.h -- live "effective MHz" readout for cosmetic console badging
// ----------------------------------------------------------------------------
// 2026-07-01 (Tim / Claude).  The SRM banner "AlphaServer DS20 100 MHz" prints
// a firmware compile-time constant (pc264.sdl HWRPB$_CC_FREQ = 100000000),
// surfaced because EmulatR forces the ISP path (0xBFFC => 0xCAFEBEEF) so the
// firmware skips its RPCC-vs-tick clock calibration.  This gauge lets the UART
// TX path rewrite that "100" with the effective machine speed *at the instant
// the banner streams out* -- MHz_eff = cycleCount / wall_seconds / 1e6, the
// warp-inflated guest-wall view from the WARP cycle-accounting scheme.
//
// SAFETY: this is EmulatR-side telemetry ONLY.  It is never written back into
// guest state.  The functional HWRPB cycle-counter-frequency field (PA 0x2070)
// that the firmware's delay/timeout loops key off stays untouched; only the
// bytes leaving the console are edited.  See the WARP Variants and
// Cycle-Accounting topic ("the gauge is an EmulatR-side readout, NOT a
// guest-visible register").
//
// The state is a single program-wide instance: inline functions have one
// definition under the ODR, so their function-local statics are shared across
// every translation unit that includes this header.  No .cpp, no globals to
// order-initialize.  Threading: the guest run loop both advances cycleCount
// and drives UART writes on the same thread, so effMhzNow() reads a coherent
// cycleCount with no synchronization.
// ============================================================================

#include <chrono>
#include <cstdint>

namespace deviceLib {
namespace badge {

// Run-start wall epoch.  Reference to the single shared instance.
inline std::chrono::steady_clock::time_point& runEpoch() noexcept
{
    static std::chrono::steady_clock::time_point epoch{};
    return epoch;
}

// Pointer to the live guest cycleCount (bound by main once Machine exists).
// Reference to the single shared instance; nullptr => gauge inactive.
inline std::uint64_t const*& cyclePtr() noexcept
{
    static std::uint64_t const* p = nullptr;
    return p;
}

// Bind the gauge to the live cycleCount counter.  Pass &mach.cpu().cycleCount.
inline void bindCycleCount(std::uint64_t const* cycleCount) noexcept
{
    cyclePtr() = cycleCount;
}

// Stamp the run-start epoch; call once immediately before mach.run().
inline void markRunStart() noexcept
{
    runEpoch() = std::chrono::steady_clock::now();
}

// Effective MHz now = cycleCount / wall_seconds / 1e6, rounded to nearest int.
// Returns 0 when unbound or before any measurable wall time has elapsed, so
// callers can treat 0 as "no reading" without a separate validity flag.
inline unsigned effMhzNow() noexcept
{
    std::uint64_t const* const cc = cyclePtr();
    if (cc == nullptr) {
        return 0u;
    }
    double const wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now()
                                      - runEpoch()).count();
    if (wall <= 0.0) {
        return 0u;
    }
    double const mhz = static_cast<double>(*cc) / wall / 1.0e6;
    if (mhz <= 0.0) {
        return 0u;
    }
    return static_cast<unsigned>(mhz + 0.5);   // round to nearest
}

}  // namespace badge
}  // namespace deviceLib

#endif  // EMULATR_DEVICELIB_BADGEMHZGAUGE_H
