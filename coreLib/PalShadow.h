// ============================================================================
// coreLib/PalShadow.h -- EV6 PAL shadow register swap helper
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
// EV6 / 21264 implements 8 PAL shadow registers that are exchanged with
// the architectural registers R4-R7 and R20-R23 on every transition
// into or out of PAL mode, gated by I_CTL[SDE] = I_CTL bit 7.  This
// header provides:
//
//   * kSdeBit            -- mask isolating I_CTL[SDE]
//   * swapPalShadowRegs  -- unconditional swap of the 8 registers
//   * sdeEnabled         -- predicate testing I_CTL[SDE]
//   * palModeEnter       -- raises palMode and swaps if SDE is set
//   * palModeLeave       -- lowers palMode and swaps if SDE was set
//
// Source: Alpha 21264/EV6 Hardware Reference Manual Section 6.6
// "PALshadow Registers" and Section 5.4.1 I_CTL field layout.
//
// Caller discipline: every site that mutates cpu.palMode in a way that
// could change its value must route through palModeEnter / palModeLeave
// rather than assigning palMode directly.  Sites that explicitly want
// to bypass the swap (cold-boot reset, snapshot restore) call
// swapPalShadowRegs and set palMode directly with comments explaining
// why.
//
// ============================================================================

#ifndef CORELIB_PAL_SHADOW_H
#define CORELIB_PAL_SHADOW_H

#include <algorithm>
#include <cstdint>

#include "coreLib/CpuState.h"
#include "coreLib/IprFields.h"

namespace coreLib {

// PAL shadow enable bit lives in I_CTL[7] = SDE<1> per HRM 5.2.14.
// V4 supports the SDE<1> shadow set (R4-R7 + R20-R23 swap); see
// IprFields.h iCtlSdeHigh() for the canonical accessor.  SDE<0>
// (R8-R11 + R24-R27 swap) is not currently implemented; the comment
// in palModeEnter notes that limitation.
//
// Historical: kSdeBit was a local constant; replaced by the
// IprFields.h accessor to keep all I_CTL field positions in one
// HRM-cited place.  kSdeBit kept as a back-compat alias.
constexpr uint64_t kSdeBit = kICtlSdeHighBit;


// Unconditional swap of R4-R7 and R20-R23 with their PAL shadow copies.
// Caller is responsible for gating on iCtlSdeHigh(cpu.i_ctl) when the
// swap should only occur for shadow-enabled transitions.
inline void swapPalShadowRegs(CpuState& cpu) noexcept
{
    for (int i = 0; i < 4; ++i) {
        std::swap(cpu.intReg[4 + i],  cpu.intShadow[i]);
    }
    for (int i = 0; i < 4; ++i) {
        std::swap(cpu.intReg[20 + i], cpu.intShadow[4 + i]);
    }
}


// Predicate: is the PAL shadow swap currently enabled?  Equivalent
// to "I_CTL[SDE<1>] is set."  Forwards to the canonical IprFields.h
// accessor; defined inline so it inlines cleanly at the hot-path
// callsites in palModeEnter / palModeLeave.
//
// SDE<0> (R8-R11 + R24-R27 shadow set) is not currently consulted;
// when V4 needs that, add a parallel swap path and gate it on
// iCtlSdeLow(cpu.i_ctl).
[[nodiscard]] inline bool sdeEnabled(CpuState const& cpu) noexcept
{
    return iCtlSdeHigh(cpu.i_ctl);
}


// Transition into PAL mode.  If palMode was already true, no-op
// (the swap is symmetric and shouldn't fire twice for one entry).
// If SDE is set when transitioning, swap before raising palMode.
//
// The swap-before-raise order is deliberate: the shadow exchange
// publishes the new register view atomically with the mode change
// from the perspective of the next instruction fetch.
inline void palModeEnter(CpuState& cpu) noexcept
{
    // CHANGE 2026-05-21: PALmode == PC<0>.  Mode lives in the PC's low
    // bit; the SDE shadow swap is unchanged (swap-before-raise).
    if (cpu.inPalMode()) return;
    if (sdeEnabled(cpu)) {
        swapPalShadowRegs(cpu);
    }
    cpu.pc |= uint64_t{1};   // raise PALmode (was cpu.palMode = true)
}


// Transition out of PAL mode.  If palMode was already false, no-op.
// If SDE is set when transitioning, swap after lowering palMode.
//
// The swap-after-lower order mirrors palModeEnter's contract: the
// register view is restored to its pre-PAL state at the moment the
// next instruction fetch sees palMode = false.
inline void palModeLeave(CpuState& cpu) noexcept
{
    // CHANGE 2026-05-21: PALmode == PC<0> (swap-after-lower preserved).
    if (!cpu.inPalMode()) return;
    cpu.pc &= ~uint64_t{1};   // lower PALmode (was cpu.palMode = false)
    if (sdeEnabled(cpu)) {
        swapPalShadowRegs(cpu);
    }
}


// Generic transition helper.  Sets palMode to `target`, performing
// a shadow swap when the value changes and SDE is set.  Useful for
// HW_REI which computes its post-instruction palMode from the
// resume-target's low bit rather than always raising or always
// lowering.
inline void setPalMode(CpuState& cpu, bool target) noexcept
{
    if (cpu.inPalMode() == target) return;
    if (target) {
        palModeEnter(cpu);
    } else {
        palModeLeave(cpu);
    }
}

} // namespace coreLib

#endif // CORELIB_PAL_SHADOW_H
