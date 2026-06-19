// ============================================================================
// MockCpuAgent.h -- a stand-in symmetric CPU agent for harness bring-up.
// ============================================================================
//
// NOT the real AlphaCPU. It exercises the harness and proves the determinism
// contract before the real CPU is wired. It does just enough to be a meaningful
// subject:
//
//   - Private state: a work counter + a per-agent LCG ("instruction stream"),
//     so each agent has its own reproducible trajectory.
//   - A two-quantum LL/SC transaction against a SHARED granule (the LDx_L/STx_C
//     surface): stage load-locked in quantum K, store-conditional in quantum
//     K+1, observe the ack in K+2. Splitting LL and SC across sync boundaries
//     is what lets OTHER agents' ops interleave between them, so the arbiter
//     performs REAL mutual exclusion -- exactly one winner per contended round.
//   - step() touches ONLY private state + the outbox (never the arbiter), so
//     SequentialDriver and ThreadedDriver yield identical final state. That
//     equivalence is the proof the harness preserves determinism.
//
// Grow into the real thing: step() -> execute <= quantum Alpha instructions;
// LCG -> real fetch/decode/dispatch; stage real memory/IPI/DMA Effects; map
// id() -> WHAMI/CPUID.
// ============================================================================

#ifndef SCHEDLIB_MOCK_CPU_AGENT_H
#define SCHEDLIB_MOCK_CPU_AGENT_H

#include "SmpHarness.h"

#include <cstdint>
#include <string>
#include <vector>

namespace emulatr::smp {

class MockCpuAgent final : public IAgent {
public:
    MockCpuAgent(std::string label, std::uint64_t sharedLockAddr,
                 std::uint32_t lockEveryN = 3, std::uint64_t seed = 1)
        : m_label(std::move(label))
        , m_lockAddr(sharedLockAddr)
        , m_lockEveryN(lockEveryN ? lockEveryN : 1)
        , m_rng(seed) {}

    const char* name() const noexcept override { return m_label.c_str(); }
    bool runnable() const noexcept override { return !m_halted; }

    void halt()    noexcept { m_halted = true; }
    void release() noexcept { m_halted = false; }

    Quantum step(Quantum q) override {
        // Private "work": advance counter + churn private RNG. No shared state.
        m_counter += q;
        for (Quantum i = 0; i < q; ++i)
            m_rng = m_rng * 6364136223846793005ull + 1442695040888963407ull;
        ++m_callCount;

        // LL/SC transaction: advances at most one phase per step() call, so it
        // is independent of quantum width.
        switch (m_tx) {
        case Tx::Idle:
            if ((m_callCount % m_lockEveryN) == 0) {
                Effect ll{};
                ll.kind = EffectKind::LoadLocked; ll.source = id(); ll.addr = m_lockAddr;
                stage(ll);
                m_tx = Tx::AwaitSc;
            }
            break;

        case Tx::AwaitSc: {
            Effect sc{};
            sc.kind = EffectKind::StoreCond; sc.source = id(); sc.addr = m_lockAddr;
            sc.value = m_rng; sc.ackSuccess = &m_lastScOk;
            stage(sc);
            m_tx = Tx::AwaitAck;
            break;
        }

        case Tx::AwaitAck:
            // m_lastScOk was written by the syncPhase that drained the SC.
            if (m_lastScOk) ++m_grants;
            m_grantLog.push_back(m_lastScOk ? 1u : 0u);
            m_tx = Tx::Idle;
            break;
        }
        return q;
    }

    // Observability for tests and (later) the real snapshot serializer.
    std::uint64_t counter()  const noexcept { return m_counter; }
    std::uint64_t grants()   const noexcept { return m_grants; }
    std::uint64_t rngState() const noexcept { return m_rng; }
    const std::vector<std::uint8_t>& grantLog() const noexcept { return m_grantLog; }

private:
    enum class Tx : std::uint8_t { Idle, AwaitSc, AwaitAck };

    std::string                m_label;
    std::uint64_t              m_lockAddr;
    std::uint32_t              m_lockEveryN;

    std::uint64_t              m_counter{0};
    std::uint64_t              m_rng;
    std::uint64_t              m_callCount{0};
    std::uint64_t              m_grants{0};
    std::vector<std::uint8_t>  m_grantLog;

    bool                       m_halted{false};
    Tx                         m_tx{Tx::Idle};
    bool                       m_lastScOk{false};
};

} // namespace emulatr::smp

#endif // SCHEDLIB_MOCK_CPU_AGENT_H
