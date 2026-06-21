// ============================================================================
// Ev6Pc264PalDefs.h -- PC264/DS-series EV6 PALcode linkage-area offsets
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
// Named constants for the PC264 (DS10/DS20-family) console/PAL linkage area,
// mirroring DEC's own structure definition file (clean-room: transcribed from
// the SDL constant table, no derived layout invented here):
//
//   Source: D:\EmulatR\Processor Support\Palcode\palcode\srmconsole\
//           EV6_PC264_PAL_DEFS.SDL  (module $pal_def)
//   Cross-ref usage sites (read-only reference):
//     apisrm/apisrm/ref/pc264.c          start_secondary / halt_switch_in
//     srmconsole/EV6_VMS_PC264_PAL.MAR   secondary restart + sys__int_hlt
//
// ----------------------------------------------------------------------------
// BASE RESOLUTION -- READ THIS BEFORE DEREFERENCING ANY OFFSET
// ----------------------------------------------------------------------------
// These are OFFSETS, not absolute physical addresses.  In the PALcode they are
// indexed off a base register produced by the `get_base` macro
// (e.g. `hw_stq/p p20, pal$halt_switch_in(p4)` with p4 = get_base), NOT off the
// PAL_BASE IPR (`palBase`) that Ev6EntryVectors.h uses.  Do NOT assume
// `palBase + offset`.
//
// Candidate base (cold-boot console personality): the SRM console image is
// loaded VERBATIM to guest PA 0x0 (systemLib/SrmLoader.cpp), so the linkage
// base is plausibly PA 0x0 -> e.g. PAL$HALT_SWITCH_IN at PA 0x220.  This is a
// CANDIDATE, not confirmed: PALcode relocates palBase during boot (0x8000 ->
// OS-PAL 0x600000 -> 0x8000), and `get_base` may track a moving base.  Per the
// project IPR/SCBD discipline (provisional OK for storage, NEVER for decode),
// PIN the real base with a STORE-WATCH on the start_secondary write
// (PAL$CPU0_START_BASE[id]) or the sys__int_hlt write (PAL$HALT_SWITCH_IN)
// before EmulatR reads/seeds any of these locations.
//
// ----------------------------------------------------------------------------
// LAYOUT (offsets within the linkage area; sizes as declared in the SDL)
// ----------------------------------------------------------------------------
//   0x0100  CPU0_START_BASE   per-CPU secondary-start entry slot, CPU 0
//   0x0108  CPU1_START_BASE   per-CPU secondary-start entry slot, CPU 1
//                             (indexed: CPU_START_BASE[id] = 0x100 + id*8)
//   0x0200  PRIMARY           primary-CPU id / primary marker
//   0x0210  CALLBACK          pending console callback flag
//   0x0220  HALT_SWITCH_IN    software halt-switch flag (set by sys__int_hlt on
//                             a HALT IRQ<4>; read+cleared by console
//                             halt_switch_in(); gates `boot`).  NOTE: distinct
//                             from the smir HARDWARE register (TIG+0x40); see
//                             chipsetLib/TsunamiTig.h.  EmulatR should drive the
//                             hardware event and let PAL set this flag -- do NOT
//                             write this software flag directly.
//   0x2000  HWRPB_BASE        HWRPB image base (linkage-relative)
//   0x4000  IMPURE_BASE       PAL impure area base (common + per-CPU specific)
//   0x6000  LOGOUT_BASE       machine-check logout area base
//   0x7000  TEMPS_BASE        PAL temps area base
//   0x8000  PAL_BASE          PAL section base (linkage-relative)
//   0x18000 OSFPAL_BASE       OSF/1 PAL section base
//   0x24000 CONSOLE_BASE      console section base
// ============================================================================
#ifndef EMULATR_CORELIB_EV6_PC264_PAL_DEFS_H
#define EMULATR_CORELIB_EV6_PC264_PAL_DEFS_H

#include <cstdint>

namespace coreLib {
namespace ev6 {
namespace pc264 {

// ----------------------------------------------------------------------------
// Low PAL linkage slots (per-CPU start, primary/callback, halt-switch flag).
// ----------------------------------------------------------------------------
constexpr uint64_t kCpu0StartBase   = 0x100;   // PAL$CPU0_START_BASE
constexpr uint64_t kCpu1StartBase   = 0x108;   // PAL$CPU1_START_BASE
constexpr uint64_t kCpuStartStride  = 0x008;   // CPU_START_BASE[id] = base + id*stride
constexpr uint64_t kPrimary         = 0x200;   // PAL$PRIMARY
constexpr uint64_t kCallback        = 0x210;   // PAL$CALLBACK
constexpr uint64_t kHaltSwitchIn    = 0x220;   // PAL$HALT_SWITCH_IN (software flag)

// Per-CPU secondary-start entry slot for CPU `id` (linkage-relative offset).
// Mirrors apisrm pc264.c: ((uint64*)PAL$CPU0_START_BASE)[id] = address.
constexpr uint64_t cpuStartSlotOffset(unsigned id) noexcept {
    return kCpu0StartBase + static_cast<uint64_t>(id) * kCpuStartStride;
}

// ----------------------------------------------------------------------------
// Region bases / sizes (linkage-relative).
// ----------------------------------------------------------------------------
constexpr uint64_t kHwrpbBase           = 0x2000;   // PAL$HWRPB_BASE
constexpr uint64_t kImpureBase          = 0x4000;   // PAL$IMPURE_BASE
constexpr uint64_t kImpureCommonSize    = 0x200;    // PAL$IMPURE_COMMON_SIZE
constexpr uint64_t kImpureSpecificSize  = 0x600;    // PAL$IMPURE_SPECIFIC_SIZE
constexpr uint64_t kLogoutBase          = 0x6000;   // PAL$LOGOUT_BASE
constexpr uint64_t kLogoutSpecificSize  = 0x400;    // PAL$LOGOUT_SPECIFIC_SIZE
constexpr uint64_t kTempsBase           = 0x7000;   // PAL$TEMPS_BASE
constexpr uint64_t kTempsSpecificSize   = 0x200;    // PAL$TEMPS_SPECIFIC_SIZE
constexpr uint64_t kPalBase             = 0x8000;   // PAL$PAL_BASE
constexpr uint64_t kOsfPalBase          = 0x18000;  // PAL$OSFPAL_BASE
constexpr uint64_t kConsoleBase         = 0x24000;  // PAL$CONSOLE_BASE

// ----------------------------------------------------------------------------
// MP work-request codes (PAL$ MP$*) used by the restart / halt signalling path
// (apisrm console <-> PAL).  CSERVE function codes are NOT duplicated here --
// they are owned by palBoxLib/grains/PalEntries.cpp (execCserve); this header
// covers only the linkage-area layout the SDL adds on top of CSERVE.
// ----------------------------------------------------------------------------
constexpr uint64_t kMpRestart   = 1;   // MP$RESTART
constexpr uint64_t kMpHalt      = 2;   // MP$HALT
constexpr uint64_t kMpControlP  = 3;   // MP$CONTROL_P
constexpr uint64_t kMpBreak     = 4;   // MP$BREAK

// Self-documenting checks against the SDL constant table.
static_assert(kCpu1StartBase == cpuStartSlotOffset(1),
              "CPU1 start slot must be CPU0 base + one stride");
static_assert(kHaltSwitchIn == 0x220, "PAL$HALT_SWITCH_IN offset (SDL)");
static_assert(kPalBase == 0x8000,     "PAL$PAL_BASE offset (SDL)");

} // namespace pc264
} // namespace ev6
} // namespace coreLib

#endif // EMULATR_CORELIB_EV6_PC264_PAL_DEFS_H
