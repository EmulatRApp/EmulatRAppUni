// ============================================================================
// CchipIntervalTimer.h -- Tsunami / Typhoon Cchip interval timer (Phase C)
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
// PURPOSE:
//   Implements the cycle-counted interval-timer fire predicate that
//   drives MISC<ITINTR<4+n>> assertion and the in-Cchip b_irq<2>
//   latch.  The predicate is a pure function of the CPU's cycle
//   counter; the IntervalTimer module owns NO mutable state -- the
//   counter itself is the counter, per Phase A design resolution.
//
// ALGORITHM:
//   Target tick rate is HRM-nominal 1024 Hz.  The cycle-target
//   `kCchipIntervalTimerCycles = profileAlphaClockHz / 1024` is
//   rounded to the nearest power of two via the constexpr
//   `roundLog2Nearest()` helper in Tsunami21272_CsrSpec.h, yielding
//   `kCchipTimerBit` and `kCchipTimerMask = (1 << bit) - 1`.
//
//   Fire predicate:
//     (cycleCount & kCchipTimerMask) == 0 && cycleCount != 0
//
//   That's one AND + compare-to-zero + AND + compare-to-zero --
//   branch-free hot-path test, single integer register pressure.
//   On a typical retire boundary the predicate adds ~2 cycles of
//   amortized cost; the timer fires once every ~2^20 cycles at
//   the ES45 profile, so the post-fire work is paid <= 1e-6 of
//   the time.
//
//   The `cycleCount != 0` guard suppresses a phantom fire at
//   cycle 0 -- the very first retire of a cold boot would
//   otherwise be misinterpreted as a tick boundary (low bits all
//   zero by virtue of the counter starting at 0, not by virtue
//   of a tick having occurred).
//
// EFFECTIVE TICK RATES:
//   ES45 (1 GHz):  bit 20 -> 1048576 cycles/tick -> ~953.7 Hz
//   ES40 (600 MHz): bit 19 ->  524288 cycles/tick -> ~1144.0 Hz
//   Both within ~12% of HRM 1024 Hz nominal.  OSF/1 PAL idle-wait
//   is rate-agnostic so the drift is not firmware-observable.
//
// CONSUMERS:
//   - systemLib/Machine.cpp:run() calls intervalTimerShouldFire()
//     after each step() and, on a true return, calls
//     chipsetLib::TsunamiCchip::fireIntervalTimer() to assert the
//     b_irq<2> latch and the corresponding MISC<ITINTR<4+n>> bits.
//   - The same Machine::run loop then polls
//     chipsetLib::TsunamiCchip::pendingIrq2(cpuId) per retire and,
//     when set, executes the divert recipe (savedPc <- pc | palMode,
//     pc <- palBase + kEntry_INTERRUPT (0x680), palMode <- true,
//     isum <- EI[2] = 1<<35 = IRQ_CLK).  The EI[2] cause bit is what
//     routes SRM to sys__int_clk; a prior EI[0] (1<<33 / IRQ_ERR)
//     mis-dispatched every tick to sys__int_err and the SRM clock
//     self-test reported "no timer interrupts" (fixed 2026-05-30,
//     journals/RCA_no_timer_interrupts_20260530.txt).
//
// REFERENCES:
//   - Phase A design notes:
//       D:\EmulatR\EmulatRAppUniV4\Emulatr\journals\CchipPhaseA_Design_Notes.md
//       Section 3 -- IntervalTimer Module.
//   - HRM:  Tsunami/Typhoon 21272 (EC-RE2CA-TE Rev 4.0, 21 Oct 1999),
//           Section 6.3.2 -- Interval Timer.
//   - Companion constants: Tsunami21272_CsrSpec.h, end-of-file
//     "INTERVAL TIMER PROFILE" block.
//   - Idle-wait hypothesis memory note:
//       project_idle_wait_interrupt_hypothesis.md
// ============================================================================
//
// CHANGE HISTORY:
//   2026-05-17  Initial Phase C deliverable.  Header-only inline
//               predicate; depends solely on the Phase A constants
//               in Tsunami21272_CsrSpec.h.  No internal state, no
//               cpp body; companion Cchip method
//               TsunamiCchip::fireIntervalTimer() is the side-
//               effectful half.
//
// ============================================================================

#ifndef CHIPSETLIB_CCHIP_INTERVAL_TIMER_H
#define CHIPSETLIB_CCHIP_INTERVAL_TIMER_H

#include <cstdint>

#include "chipsetLib/Tsunami21272_CsrSpec.h"

namespace chipsetLib {

// ----------------------------------------------------------------------------
// intervalTimerShouldFire -- pure cycle-driven tick predicate.
// ----------------------------------------------------------------------------
//
// Returns true on the cycles where the interval timer should fire:
//   - low `kCchipTimerBit` bits of cycleCount are all zero, AND
//   - cycleCount itself is non-zero (suppress phantom-fire at boot).
//
// Callers must invoke this once per retire boundary (the same point
// where the synthetic INTERRUPT injection is gated) and route a true
// return into TsunamiCchip::fireIntervalTimer().
//
// noexcept, constexpr-eligible call shape, no allocation.  Designed
// to inline at the call site for hot-path locality.
// ----------------------------------------------------------------------------
inline bool intervalTimerShouldFire(uint64_t cycleCount) noexcept
{
    using Tsunami21272::Spec::kCchipTimerMask;
    return cycleCount != 0 && (cycleCount & kCchipTimerMask) == 0;
}

} // namespace chipsetLib

#endif // CHIPSETLIB_CCHIP_INTERVAL_TIMER_H
