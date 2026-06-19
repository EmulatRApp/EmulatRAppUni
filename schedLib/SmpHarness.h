// ============================================================================
// SmpHarness.h -- EmulatR V4 SMP agent/dispatcher scaffold (Qt-free core).
// ============================================================================
//
// PURPOSE
//   Host N symmetric execution agents (CPUs) plus I/O agents (storage hoses,
//   etc.) behind a deterministic dispatcher. Threading is the ARCHITECTURE
//   (agents are independent, message-passing units from day one); determinism
//   vs raw parallelism is a swappable EXECUTION DRIVER, not a rewrite. This is
//   the model converged on 2026-06-18: symmetric CPUs, agent-per-unit,
//   dispatcher-mediated, parallel-capable but deterministic-by-default during
//   bring-up so snapshot/resume and AXPBox trace-diff stay valid.
//
// THE DETERMINISM CONTRACT (the whole point -- read this)
//   An agent's step() runs concurrently with other agents under the threaded
//   driver. To keep threaded execution bit-identical to sequential:
//
//     1. During step(), an agent touches ONLY its own private state and its
//        own outbound effect queue. It MUST NOT read or write another agent's
//        state, nor shared memory/lock state, directly.
//     2. All cross-agent interaction (memory stores visible to others, lock
//        latch/release, interrupt delivery, inter-agent messages) is STAGED as
//        effects during step() and APPLIED by the dispatcher in syncPhase(),
//        single-threaded, in deterministic agent order.
//
//   Hold that contract and SequentialDriver and ThreadedDriver produce
//   identical final state -- proven by the determinism-equivalence test. Break
//   it (touch shared state inside step()) and threaded mode silently diverges.
//   This is exactly the staged-commit discipline V4 already uses; the harness
//   just makes the quantum boundary the publish point.
//
// EXPANSION
//   - Replace MockCpuAgent with the real AlphaCpuAgent (wrap the AlphaCPU
//     run loop; step() executes <= quantum instructions, stages memory/lock
//     ops as effects).
//   - Add StorageHoseAgent (one per SCSI bus): step() advances device state,
//     stages DMA-completion effects delivered in syncPhase so I/O completion
//     timing is on the logical clock, not host-thread-nondeterministic.
//   - The LockArbiter below is the LDx_L/STx_C cross-agent interlock, stubbed
//     with correct semantics; wire the real AlphaCPU lock_flag to it.
//
// No Qt. C++20. Threading primitives are std::*, never QThread.
// ============================================================================

#ifndef SCHEDLIB_SMP_HARNESS_H
#define SCHEDLIB_SMP_HARNESS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace emulatr::smp {

// ----------------------------------------------------------------------------
// Logical time. One shared cycle counter advanced by the dispatcher at each
// quantum boundary. Agents never advance it themselves.
// ----------------------------------------------------------------------------
using Tick    = std::uint64_t;   // logical cycle counter
using Quantum = std::uint32_t;   // logical units per step (1 = lock-step)
using AgentId = int;             // dense index, assigned at registration

class LogicalClock {
public:
    Tick now() const noexcept { return m_now; }
    void advance(Quantum q) noexcept { m_now += q; }
    void reset() noexcept { m_now = 0; }
private:
    Tick m_now{0};
};

// ----------------------------------------------------------------------------
// LockArbiter -- the LDx_L / STx_C cross-agent interlock, modeled as a
// dispatcher service resolved single-threaded in syncPhase. Under cooperative
// scheduling this is trivially correct (no real concurrency); under the
// threaded driver it is STILL resolved in the single-threaded sync phase, so
// it stays correct and deterministic without host atomics. This is the
// "synchronous lock-table" the cooperative model buys you.
//
// Semantics (granule = address key here; refine to cache-block granule when
// wiring the real CPU):
//   loadLocked(a, addr) : agent a arms a lock on addr.
//   storeCond (a, addr) : succeeds iff a's lock on addr is still valid; a
//                         successful SC clears the lock and returns true.
//   store     (a, addr) : a plain store breaks any OTHER agent's lock on addr
//                         (cross-CPU invalidation).
// ----------------------------------------------------------------------------
class LockArbiter {
public:
    void reset() noexcept { m_holder.clear(); }

    void loadLocked(AgentId a, std::uint64_t addr) {
        m_holder[addr] = a;                     // arm / re-arm
    }

    bool storeCond(AgentId a, std::uint64_t addr) {
        auto it = m_holder.find(addr);
        bool const ok = (it != m_holder.end() && it->second == a);
        if (ok) m_holder.erase(it);             // success consumes the lock
        return ok;
    }

    void store(AgentId /*a*/, std::uint64_t addr) {
        m_holder.erase(addr);                   // break any lock on this granule
    }

    bool isLocked(std::uint64_t addr) const {
        return m_holder.find(addr) != m_holder.end();
    }

private:
    std::unordered_map<std::uint64_t, AgentId> m_holder;
};

// ----------------------------------------------------------------------------
// Staged cross-agent effects. step() appends these to the agent's private
// queue; syncPhase() drains them in agent order. Extend this variant as the
// model grows (memory writes, IPI sends, DMA completions, console comm).
// ----------------------------------------------------------------------------
enum class EffectKind : std::uint8_t { LoadLocked, StoreCond, Store, Ipi };

struct Effect {
    EffectKind     kind{};
    AgentId        source{};       // who staged it
    AgentId        target{};       // for Ipi: destination CPU; else unused
    std::uint64_t  addr{};         // for lock/store ops
    std::uint64_t  value{};        // payload / SC value
    // Result channel (filled by syncPhase, read back by the agent next quantum):
    bool*          ackSuccess{nullptr};  // for StoreCond: where to write result
};

// ----------------------------------------------------------------------------
// IAgent -- a steppable unit. CPUs and I/O hoses both implement this. Symmetric
// by design: there is no privileged agent. "Primary" is a guest-elected
// property of guest state, never a host role baked into a particular agent.
// ----------------------------------------------------------------------------
class IAgent {
public:
    virtual ~IAgent() = default;

    virtual const char* name() const noexcept = 0;

    // Assigned once at registration. WHAMI/CPUID semantics hang off this.
    void        setId(AgentId id) noexcept { m_id = id; }
    AgentId     id() const noexcept { return m_id; }

    // Runnable = not halted/parked. A parked agent consumes no work but still
    // participates in the barrier (arrives without stepping) so the threaded
    // driver's participant count stays constant.
    virtual bool runnable() const noexcept = 0;

    // Advance up to `q` logical units. CONCURRENT under the threaded driver.
    // CONTRACT: touch only private state + m_outbox (via stage()). Returns the
    // number of logical units actually consumed (for future variable-cost work;
    // the scaffold drivers advance the clock by the fixed quantum regardless).
    virtual Quantum step(Quantum q) = 0;

    // Called by syncPhase (single-threaded) BEFORE effects are drained, to let
    // the agent read back results the previous syncPhase wrote (e.g. SC acks).
    // Default no-op.
    virtual void onSyncBegin() {}

    // Drained by the dispatcher in syncPhase. Cleared after draining.
    std::vector<Effect>&       outbox() noexcept { return m_outbox; }
    const std::vector<Effect>& outbox() const noexcept { return m_outbox; }

protected:
    // Helper for derived agents to stage an effect during step().
    void stage(const Effect& e) { m_outbox.push_back(e); }

private:
    AgentId             m_id{-1};
    std::vector<Effect> m_outbox;   // private per agent -> no contention in step()
};

// ----------------------------------------------------------------------------
// IExecutionDriver -- the swappable execution policy. Same agents, same
// syncPhase; only the question "do quanta run serially or in parallel" differs.
// This is the on/off toggle: SequentialDriver for deterministic tests and
// trace-diff; ThreadedDriver for profiling / future host parallelism.
// (Implementations in SmpDrivers.h.)
// ----------------------------------------------------------------------------
class Dispatcher;  // fwd

class IExecutionDriver {
public:
    virtual ~IExecutionDriver() = default;
    virtual const char* name() const noexcept = 0;
    // Run agents until clock reaches `untilTick`. Must call dispatcher.syncPhase()
    // and dispatcher.clock().advance(quantum) exactly once per quantum boundary,
    // with syncPhase executed single-threaded.
    virtual void run(Dispatcher& d, Tick untilTick) = 0;
};

// ----------------------------------------------------------------------------
// Dispatcher -- owns agents, the clock, the lock arbiter, and the active
// driver. Drives execution and applies staged effects deterministically.
// ----------------------------------------------------------------------------
class Dispatcher {
public:
    explicit Dispatcher(Quantum quantum = 1) : m_quantum(quantum) {}

    // Registration assigns a dense AgentId in call order. That order is the
    // deterministic tie-break used everywhere in syncPhase.
    AgentId addAgent(IAgent* a) {
        AgentId const id = static_cast<AgentId>(m_agents.size());
        a->setId(id);
        m_agents.push_back(a);
        return id;
    }

    // THE TOGGLE. Swap drivers between runs (or per test) with zero change to
    // agents or syncPhase.
    void setDriver(std::unique_ptr<IExecutionDriver> drv) { m_driver = std::move(drv); }
    IExecutionDriver* driver() const noexcept { return m_driver.get(); }

    void   setQuantum(Quantum q) noexcept { m_quantum = q; }
    Quantum quantum() const noexcept { return m_quantum; }

    LogicalClock&       clock() noexcept { return m_clock; }
    LockArbiter&        locks() noexcept { return m_lockArbiter; }
    std::vector<IAgent*>&       agents() noexcept { return m_agents; }
    const std::vector<IAgent*>& agents() const noexcept { return m_agents; }

    void run(Tick untilTick) {
        if (!m_driver || m_agents.empty()) return;
        m_driver->run(*this, untilTick);
    }

    // Apply all staged effects, single-threaded, in deterministic order:
    // agent index ascending, then submission order within an agent. This is
    // the only place shared state (lock arbiter, future memory/IPI) is mutated.
    void syncPhase() {
        for (IAgent* a : m_agents) a->onSyncBegin();
        for (IAgent* a : m_agents) {
            for (Effect& e : a->outbox()) applyEffect(e);
            a->outbox().clear();
        }
        // Future: deliver staged IPIs to target agents' interrupt latches here,
        // also in deterministic order, after all lock ops resolve.
    }

    void reset() {
        m_clock.reset();
        m_lockArbiter.reset();
        for (IAgent* a : m_agents) a->outbox().clear();
    }

private:
    void applyEffect(Effect& e) {
        switch (e.kind) {
        case EffectKind::LoadLocked:
            m_lockArbiter.loadLocked(e.source, e.addr);
            break;
        case EffectKind::StoreCond: {
            bool const ok = m_lockArbiter.storeCond(e.source, e.addr);
            if (e.ackSuccess) *e.ackSuccess = ok;
            break;
        }
        case EffectKind::Store:
            m_lockArbiter.store(e.source, e.addr);
            break;
        case EffectKind::Ipi:
            // Scaffold stub: wire to the Cchip IPREQ/IPINTR latch of e.target.
            break;
        }
    }

    std::vector<IAgent*>             m_agents;
    LogicalClock                     m_clock;
    LockArbiter                      m_lockArbiter;
    Quantum                          m_quantum;
    std::unique_ptr<IExecutionDriver> m_driver;
};

} // namespace emulatr::smp

#endif // SCHEDLIB_SMP_HARNESS_H
