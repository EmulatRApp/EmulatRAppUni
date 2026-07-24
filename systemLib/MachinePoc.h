// ============================================================================
// systemLib/MachinePoc.h -- flat fast-tier executor (scaffold, off by default)
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
// MachinePoc is the SECOND execution tier: a flat, functional executor
// (the GSEA "Grain-Stalk" fast path) that runs over the SAME
// coreLib::CpuState and memoryLib::GuestMemory the Oracle (systemLib::
// Machine + PipelineDriver) owns.  It is NOT the Oracle and does NOT
// model the per-cycle pipeline: the moment it computes an architectural
// result it leaves the cycle-accurate tier.  Its role is a validated
// fast path, gated by the slow Oracle, per the tiered-execution audit
// (project doc: claude/20260722_GSEA_POC_tiered_exec_audit_reconstruction).
//
// SCAFFOLD STATUS: this header compiles UNCONDITIONALLY (so it cannot
// bit-rot), but the real handler wiring is gated behind EMULATR_FAST_TIER
// and is presently stubbed.  Nothing here is called by the default build.
// This file exists to fix the SEAM shape before any real wiring lands --
// it is where the reframed GATE-1 (state-sharing disassembly probe) runs.
//
// Design contracts (from the two-path design section of the audit doc):
//
//   1. ONE state, two executors.  MachinePoc binds to references of the
//      Oracle's CpuState + GuestMemory.  No copy, no serialization at the
//      tier boundary -- a handoff is just a return of control.  The
//      unified state is CpuState itself; there is deliberately NO separate
//      "flat vector" / POD mirror (that would re-introduce reconciliation
//      and buys nothing over CpuState's already-contiguous regfile).
//
//   2. COARSE boundary, never per-instruction virtual.  run() executes a
//      tight internal loop and returns a TierExit REASON when it hits
//      something it must defer (fault, PAL transfer, IRQ, region exit,
//      halt, budget).  The orchestrator reads the reason and either keeps
//      running on this tier or falls back to the Oracle (Machine).  The
//      IExecTier virtual fires per-RUN, not per-grain.
//
//   3. Oracle stays authoritative.  MachinePoc adapts to CpuState; the
//      Oracle is never distorted for the fast tier's convenience.  The
//      fast tier only runs where the Oracle is GREEN -- enforced by the
//      region allowlist (allowRegion), NOT a global on/off.  Secondary
//      boot is off-limits until the slow tier boots it.
//
//   4. --verify is the payoff.  Because both tiers mutate the same
//      CpuState, a differential is a memcmp of the architectural
//      projection (regs, pc, IPRs, memory write-set); a divergence is
//      provably the tier difference and nothing else.  First cut diffs at
//      the boot milestones Snapshot already captures (coarse-to-fine),
//      which sidesteps per-region rollback.
//
//   5. Guard discipline.  Real execution is behind EMULATR_FAST_TIER
//      (CMake compile guard, OFF by default) AND a runtime dispatch flag
//      on the orchestrator.  Compiled-but-uncalled, same posture as the
//      EMULATR_EV6_BPRED branch predictor move.
//
// Known FP divergence (audit finding): the GSEA handler set computes FP
// through host double/float, NOT berkeley-softfloat-3 as the Oracle does.
// Any FP-bearing path WILL diverge under --verify until FP is either
// routed through SoftFloat or classed as a fidelity boundary (TierExit::
// Defer).  See TODO(fp-strategy).
//
// Header TODO table (greppable; each entry is removed in the same edit
// that lands its wiring):
//
//   TODO(state-probe)  GATE-1: bind two hot handlers to CpuState, read
//                      release disassembly, confirm base+displacement and
//                      no per-cycle bookkeeping leaks in.  Blocks all wiring.
//   TODO(handlers)     Regenerate the GSEA handler set against V5 CpuState
//                      accessors (intReg/fpReg/pc/IPR members) + include here.
//   TODO(fp-strategy)  Decide SoftFloat-swap vs FP-as-Defer before any FP
//                      path is validated.  Deterministic --verify tripwire.
//   TODO(stalk)        Pipeline B (captured-loop replay) hook; interacts
//                      with SpinSkip -- census loop lengths at fast-tier
//                      speed before retiring any WARP.
//   TODO(verify)       Wire the memcmp differential against Machine at the
//                      Snapshot milestone boundaries.
//   TODO(orchestrator) Tier gate that owns the region allowlist + runtime
//                      dispatch flag + fallback to Machine on Defer/Fault.
//
// ============================================================================

#ifndef SYSTEMLIB_MACHINEPOC_H
#define SYSTEMLIB_MACHINEPOC_H

#include <cstdint>
#include <ostream>

#include "systemLib/StopReason.h"   // reuse the Oracle's stop taxonomy

// Forward declarations -- MachinePoc holds non-owning references only.
// Full types come in with the handler wiring (TODO(handlers)); the
// scaffold does not dereference their internals yet.
namespace coreLib   { struct CpuState; }
namespace memoryLib { class  GuestMemory; }

namespace systemLib {

// ---------------------------------------------------------------------------
// TierExit -- why the fast tier returned control to the orchestrator.
// ---------------------------------------------------------------------------
// Superset of the handoff reasons.  The orchestrator maps the terminal
// ones onto systemLib::StopReason for the post-mortem (see toStopReason);
// the non-terminal ones (Continued / RegionExit / Defer / PalTransfer /
// Irq) mean "hand back to the Oracle or re-enter, do not stop".
enum class TierExit : uint8_t
{
    Continued    = 0,   // budget slice done, still runnable on this tier
    RegionExit   = 1,   // PC left the active allowlisted region
    Defer        = 2,   // hit an instruction class the fast tier will not do
    PalTransfer  = 3,   // CALL_PAL / HW_REI / trap delivery -- Oracle takes it
    Irq          = 4,   // interrupt pending -- Oracle delivers it
    Fault        = 5,   // architectural fault raised (restart in Oracle)
    Halted       = 6,   // CALL_PAL HALT retired
    MaxCycles    = 7,   // budget exhausted
};

constexpr char const* tierExitName(TierExit e) noexcept
{
    switch (e) {
        case TierExit::Continued:   return "Continued";
        case TierExit::RegionExit:  return "RegionExit";
        case TierExit::Defer:       return "Defer";
        case TierExit::PalTransfer: return "PalTransfer";
        case TierExit::Irq:         return "Irq";
        case TierExit::Fault:       return "Fault";
        case TierExit::Halted:      return "Halted";
        case TierExit::MaxCycles:   return "MaxCycles";
    }
    return "<invalid>";
}

inline std::ostream& operator<<(std::ostream& os, TierExit e)
{
    return os << tierExitName(e);
}

// Terminal-reason projection onto the Oracle's StopReason taxonomy.  Only
// meaningful for Halted / MaxCycles / Fault; the non-terminal reasons have
// no StopReason and return MaxCyclesExceeded as a benign placeholder (the
// orchestrator never stops on them).
constexpr StopReason toStopReason(TierExit e) noexcept
{
    switch (e) {
        case TierExit::Halted:    return StopReason::HaltedClean;
        case TierExit::Fault:     return StopReason::MemFault;        // refine per fault code
        case TierExit::MaxCycles: return StopReason::MaxCyclesExceeded;
        default:                  return StopReason::MaxCyclesExceeded;
    }
}

// ---------------------------------------------------------------------------
// RunBudget -- how far a single run() slice is permitted to go.
// ---------------------------------------------------------------------------
struct RunBudget
{
    uint64_t maxCycles = 0;   // 0 == unbounded within the region
    uint64_t regionLo  = 0;   // inclusive PA/PC lower bound of the active region
    uint64_t regionHi  = 0;   // exclusive upper bound; 0,0 == no region gate
};

// ---------------------------------------------------------------------------
// IExecTier -- coarse execution-tier contract.
// ---------------------------------------------------------------------------
// Both the Oracle (via a future ClassicTier adapter over Machine::cpuKernel)
// and MachinePoc implement this.  The virtual fires per run() slice, not per
// guest instruction, so its cost is negligible against the tight inner loop
// each tier runs internally.
class IExecTier
{
public:
    virtual ~IExecTier() = default;

    // Execute over the shared state until a TierExit reason arises.
    // cpu and mem are the SAME instances the Oracle owns.
    virtual TierExit run(coreLib::CpuState&      cpu,
                         memoryLib::GuestMemory& mem,
                         RunBudget const&        budget) noexcept = 0;

    // Human-readable tier label for logs / --verify banners.
    virtual char const* name() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// MachinePoc -- the flat fast tier.
// ---------------------------------------------------------------------------
class MachinePoc final : public IExecTier
{
public:
    // Non-owning bind to the Oracle's shared state.  MachinePoc never
    // allocates or copies CpuState / GuestMemory -- it is a view + a loop.
    MachinePoc(coreLib::CpuState&      cpu,
               memoryLib::GuestMemory& mem) noexcept
        : m_cpu(&cpu), m_mem(&mem) {}

    MachinePoc(MachinePoc const&)            = delete;
    MachinePoc& operator=(MachinePoc const&) = delete;

    // Allow the fast tier to run in the PA/PC half-open range [lo, hi).
    // The orchestrator sets this ONLY for regions the Oracle validates
    // green; outside them run() returns RegionExit immediately.
    void allowRegion(uint64_t lo, uint64_t hi) noexcept
    {
        m_regionLo = lo;
        m_regionHi = hi;
    }

    char const* name() const noexcept override { return "MachinePoc"; }

    // Coarse execution slice.  See the design contracts in the file header.
    TierExit run(coreLib::CpuState&      cpu,
                 memoryLib::GuestMemory& mem,
                 RunBudget const&        budget) noexcept override
    {
#ifdef EMULATR_FAST_TIER
        // -------------------------------------------------------------------
        // Real fast-tier loop goes here (Pipeline A; Pipeline B for stalks).
        // Shape, once TODO(handlers) lands:
        //
        //   while (!cpu.halted) {
        //       if (budget.maxCycles && slice >= budget.maxCycles)
        //           return TierExit::MaxCycles;
        //       u64 pc = cpu.pc & ~1ULL;
        //       if (regionGated(pc)) return TierExit::RegionExit;
        //       u32 raw = fetch(cpu, mem, pc);                 // + IFetchOverride
        //       Grain g = encode(raw, cpu.pc & 1);
        //       Handler h = resolve(raw, cpu.pc & 1);          // per-PC cached
        //       h(g, cpu, mem);                                // mutates shared state
        //       if (isPalTransfer(raw)) return TierExit::PalTransfer;
        //       if (cpu /* raised fault */) return TierExit::Fault;
        //       cpu.pc = nextPc;  cpu.pc &= ~1ULL;  // pc<0>-canonical mode bit
        //       ++slice;
        //       if (irqPending(cpu)) return TierExit::Irq;
        //   }
        //   return TierExit::Halted;
        //
        // GATE-1 is the FIRST thing to prove inside this block: bind two hot
        // handlers to cpu.intReg[...] and read the release disassembly --
        // confirm base+displacement addressing and that no per-cycle
        // bookkeeping (BoxResult populate/apply/trace) leaks into the loop.
        // -------------------------------------------------------------------
        (void)cpu; (void)mem; (void)budget;
        return TierExit::Defer;   // TODO(handlers): replace with the loop above
#else
        // Fast tier compiled out: always defer to the Oracle.
        (void)cpu; (void)mem; (void)budget;
        return TierExit::Defer;
#endif
    }

private:
    // regionGated: true when pc lies OUTSIDE the active allowlist window.
    [[nodiscard]] bool regionGated(uint64_t pc) const noexcept
    {
        if (m_regionHi == 0) return false;          // no gate configured
        return (pc < m_regionLo) || (pc >= m_regionHi);
    }

    coreLib::CpuState*      m_cpu      = nullptr;   // non-owning: Oracle's state
    memoryLib::GuestMemory* m_mem      = nullptr;   // non-owning: Oracle's memory
    uint64_t                m_regionLo = 0;
    uint64_t                m_regionHi = 0;         // 0 == region gate disabled
};

} // namespace systemLib

#endif // SYSTEMLIB_MACHINEPOC_H
