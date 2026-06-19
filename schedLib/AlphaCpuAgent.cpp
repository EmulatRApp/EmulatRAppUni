// ============================================================================
// schedLib/AlphaCpuAgent.cpp -- IAgent bridge wrapping the real AlphaCPU.
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
// Phase 1: a thin scheduling adapter over Machine::stepCycle -- the per-cycle
// kernel shared with the legacy Machine::run loop.  See AlphaCpuAgent.h for the
// contract and journals/20260619_alphacpuagent_phase1_design.md for the design.
// ============================================================================

#include "schedLib/AlphaCpuAgent.h"

#include "systemLib/Machine.h"

namespace emulatr::smp {

// Runnable until the CPU halts or a stop is requested (stepCycle returned false
// -- CPU halt or the graceful-stop sentinel).  m_stopped unifies both so the
// driver's "no agent runnable -> terminate" condition matches legacy run()'s
// break-on-(stepCycle==false).
bool AlphaCpuAgent::runnable() const noexcept
{
    return !m_stopped && !m_machine->cpu().halted;
}

// Advance up to q logical cycles by calling Machine::stepCycle(i) once per cycle.
// The cycle ordinal m_cycleIndex reproduces legacy run()'s `i` EXACTLY (starts at
// 0 on a freshly-constructed agent, increments once per stepCycle call), so the
// dispatcher-driven boot issues the identical stepCycle(i) sequence as the legacy
// loop.  stepCycle returns false to BREAK (halt / stop sentinel); we latch
// m_stopped and return the number of cycles actually consumed.  Direct memory /
// MMIO -- single agent, Phase 1 (no Effect-staging; see the header contract).
Quantum AlphaCpuAgent::step(Quantum q)
{
    for (Quantum k = 0; k < q; ++k) {
        if (!m_machine->stepCycle(m_cycleIndex++)) {
            m_stopped = true;
            return k;
        }
    }
    return q;
}

} // namespace emulatr::smp
