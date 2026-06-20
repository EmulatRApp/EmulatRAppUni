// ============================================================================
// schedLib/AlphaCpuAgent.h -- IAgent bridge wrapping the real AlphaCPU.
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
// Phase 1 (2026-06-19): a THIN scheduling adapter.  The Dispatcher calls
// step(q), which advances the bound Machine by up to q logical cycles via
// Machine::stepCycle(i) -- the per-cycle kernel shared with the legacy
// Machine::run loop, so the dispatcher-driven and legacy boot paths run the
// IDENTICAL body.  The byte-identical-trace acceptance gate is therefore about
// the loop/clock/dispatch wiring, not the CPU kernel.
//
// CONTRACT (single agent, Phase 1): there is exactly one AlphaCpuAgent under
// SequentialDriver, so step() touches shared memory/MMIO DIRECTLY -- no
// Effect-staging.  With no concurrent observer a direct store and a staged-
// then-applied store are observationally identical and determinism_equivalence
// holds trivially.  The staging SEAMS (LL/SC retire, store commit, IPI write)
// live in the delegated kernel (MemDrainer / the b_irq divert) and perform the
// DIRECT operation today; Phase 3 inserts staging there when a second agent can
// observe a mid-quantum write.  They are seams, not stubs -- the direct path
// runs; a comment marks the insert point.
//
// CpuState is REFERENCED through the bound Machine in Phase 1, not owned.
// Owning a per-agent CpuState -- and re-homing bindCycleSource's raw pointer,
// the snapshot save/restore reach-ins, and the interval-timer fire-edge off the
// dispatcher's logical clock instead of m_cpu.cycleCount -- is the Phase-2 lift
// (see journals/20260619_alphacpuagent_phase1_design.md, landmines L1/L2).
//
// No Qt.  C++20.
// ============================================================================

#ifndef SCHEDLIB_ALPHA_CPU_AGENT_H
#define SCHEDLIB_ALPHA_CPU_AGENT_H

#include "schedLib/SmpHarness.h"
#include "coreLib/CpuState.h"   // Phase-2 T4: the agent now OWNS a CpuState

#include <cstdint>

// Bridge target; full type in systemLib/Machine.h, pulled in only by the .cpp
// so this header stays light and free of the chipset include graph.
namespace systemLib { class Machine; }

namespace emulatr::smp {

// ----------------------------------------------------------------------------
// AlphaCpuAgent -- one real Alpha CPU presented to the harness as an IAgent.
// Phase 1 wires exactly one (cpuId 0); id() (assigned by the Dispatcher) maps
// to WHAMI/CPUID once the per-agent CpuState lands in Phase 2.  "Primary" is a
// guest-elected property, never a host role baked into this agent.
// ----------------------------------------------------------------------------
class AlphaCpuAgent final : public IAgent {
public:
    explicit AlphaCpuAgent(systemLib::Machine& machine,
                           std::uint32_t       cpuId = 0) noexcept
        : m_machine(&machine), m_cpuId(cpuId)
    {
        // Phase-2 T4: this agent OWNS its CpuState; stamp its SMP slot from the
        // cpuId the Dispatcher assigned (closes the STEP-1b "slot 0" placeholder
        // -- agent0 => 0, so byte-identical today).
        m_cpuState.cpuSlot = cpuId;
    }

    const char* name() const noexcept override { return "alphacpu"; }

    // The CpuState this agent OWNS.  Machine::cpu() aliases agent0's via a
    // reference member (Phase-2 T4); under SMP each agent owns its own.
    coreLib::CpuState&       cpu()       noexcept { return m_cpuState; }
    coreLib::CpuState const& cpu() const noexcept { return m_cpuState; }

    // Reset ONLY the per-run scheduling state (NOT the owned CpuState, which the
    // Machine resets via resetToLoadedEntry through the m_cpu alias).  Called
    // when the persistent agent is reused for a fresh Machine::run.
    void resetForRun() noexcept { m_cycleIndex = 0; m_stopped = false; }

    // Runnable until the CPU halts.  Reads the bound Machine's CpuState
    // (referenced, not owned).  Defined in the .cpp where Machine is complete.
    bool runnable() const noexcept override;

    // Advance up to q logical cycles by calling Machine::stepCycle(i) once per
    // cycle; returns early (consuming fewer than q) if the CPU halts.  Direct
    // memory/MMIO -- single agent, Phase 1 (see the contract note above).
    Quantum step(Quantum q) override;

    std::uint32_t cpuId() const noexcept { return m_cpuId; }

private:
    systemLib::Machine* m_machine;       // bound shared machine (non-owning)
    std::uint32_t       m_cpuId;         // WHAMI/CPUID source (Phase 2)
    coreLib::CpuState   m_cpuState;      // Phase-2 T4: the CpuState this agent OWNS
    std::uint64_t       m_cycleIndex{0}; // mirrors legacy run()'s `i` cadence
    bool                m_stopped{false};// set when stepCycle() returns false
                                         // (CPU halt or stop-sentinel) -> the
                                         // driver's terminating condition
};

} // namespace emulatr::smp

#endif // SCHEDLIB_ALPHA_CPU_AGENT_H
