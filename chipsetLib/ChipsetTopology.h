// ============================================================================
// ChipsetTopology.h -- Tsunami/Typhoon chipset topology SSOT (T-TOPO)
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE:
//   Single source of truth (SSOT) for the LATCHED chipset topology facts from
//   which every presence/population CSR bit derives -- so CSC/DSC/PCTL stop
//   fabricating them independently (the CSC<3:0> CPU-mask vs BC<1:0> collision
//   the 2026-07-07 HRM faithfulness audit flagged).
//
//   T-TOPO.1 (this file) introduces the FACTS and refactors the ONE consumer
//   where the facts already produce today's on-wire value byte-identically: the
//   Cchip CSC reset.  DSC and PCTL derive from these facts when their ENCODING is
//   corrected in their own tasks (DSC #29, PCTL #27); refactoring them now would
//   only return today's constants (no SSOT value).
//
//   These are RO-from-pins facts on real silicon (HRM 10.2.2.1 / 10.2.4.1 /
//   10.2.5.4) -- latched once at reset, never mutated by a CSR write.
//   HRM Rev 4.0 field map (the derivation TARGETS, landed per-register later):
//     CSC 0x000 : BC<1:0>, C0CFP<2>, C1CFP<3>, P1P<14>
//     DSC 0x800 : BC<1:0>, CxCFP<5:2>, P1P<6>   (byte replicated x8)
//     PCTL 0x300: PID<47:46> (0/1), RPP<45> (remote hose present)
//
//   Design: journals/20260711_T_TOPO_design_shape_for_review.md.
//   ASCII(128); hex radix; include guards (not #pragma once).
// ============================================================================
#ifndef CHIPSETLIB_CHIPSETTOPOLOGY_H
#define CHIPSETLIB_CHIPSETTOPOLOGY_H

#include <cstdint>
#include "TsunamiVariant.h"   // ChipsetVariant

// ChipsetTopology -- latched topology facts.  bit n of cpuPresentMask = CPU
// slot n populated (n in [0, kMaxCpus)).
struct ChipsetTopology
{
    static constexpr int kMaxCpus = 4;

    ChipsetVariant variant        = ChipsetVariant::Tsunami;
    uint8_t        cpuPresentMask = 0x1;    // <3:0> populated CPU slots
    bool           pchip1Present  = false;  // hose 1 populated (T-TOPO.2 plumbs from the manifest)
    uint8_t        bcConfig       = 0x1;    // BC<1:0> base configuration

    // Build from the chipset's existing inputs.  cpuCount is clamped to
    // [0, kMaxCpus]; cpuPresentMask is the contiguous low mask (1<<n)-1, matching
    // the prior CSC reset loop `for (i<cpuCount) m_csc |= (1<<i)`.
    static ChipsetTopology make(ChipsetVariant v, int cpuCount,
                                bool pchip1Present = false) noexcept
    {
        ChipsetTopology t;
        t.variant        = v;
        int n            = (cpuCount < 0) ? 0 : (cpuCount > kMaxCpus ? kMaxCpus : cpuCount);
        t.cpuPresentMask = static_cast<uint8_t>((1u << n) - 1u);
        t.pchip1Present  = pchip1Present;
        t.bcConfig       = (v == ChipsetVariant::Typhoon) ? 0x3u : 0x1u;
        return t;
    }

    // CSC 0x000 topology bits -- CURRENT (byte-identical) encoding.
    // This reproduces today's on-wire value: the CPU-present bitmask in <3:0>
    // OR the BC<1:0> base config -- i.e. the audit's collision, preserved on
    // purpose so this refactor is provably byte-identical.
    // TODO(#30): replace with the HRM layout BC<1:0> | C0CFP<2> | C1CFP<3> |
    //   (pchip1Present ? P1P<14> : 0), AFTER a firmware-trace verify -- the <3:0>
    //   CPU mask may be what the SRM actually reads despite not matching the HRM
    //   field names.  Do NOT change this encoding without that evidence.
    uint64_t cscTopoBits() const noexcept
    {
        return static_cast<uint64_t>(cpuPresentMask)
             | static_cast<uint64_t>(bcConfig);
    }
};

#endif  // CHIPSETLIB_CHIPSETTOPOLOGY_H
