// ============================================================================
// smp_harness_tests.cpp -- DOCTest coverage for the SMP harness scaffold.
// ============================================================================
//
// The crown-jewel test is `determinism_equivalence`: the SAME workload run
// under SequentialDriver and under ThreadedDriver must produce bit-identical
// final state. If that ever fails, an agent is violating the determinism
// contract (touching shared state inside step()).
//
// Build: see CMakeLists.txt. Define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in
// exactly one TU (here).
// ============================================================================

// The doctest main is provided ONCE by tests/main.cpp (the Emulatr_tests
// target). Do NOT define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here -- a second
// definition is a duplicate-main (LNK2005) link error.
#include "doctest.h"
#include "schedLib/SmpHarness.h"
#include "schedLib/SmpDrivers.h"
#include "schedLib/MockCpuAgent.h"

#include <memory>
#include <vector>

namespace emulatr::smp
{
    class MockCpuAgent;
}

using namespace emulatr::smp;

namespace {

// Build a fixed scenario: `nCpus` mock CPUs contending on one granule, each
// with a distinct seed so trajectories differ but are reproducible.
struct Scenario {
    std::vector<std::unique_ptr<MockCpuAgent>> cpus;
    Dispatcher dispatcher;

    explicit Scenario(int nCpus, Quantum quantum)
        : dispatcher(quantum)
    {
        for (int i = 0; i < nCpus; ++i) {
            cpus.push_back(std::make_unique<MockCpuAgent>(
                "cpu" + std::to_string(i),
                /*sharedLockAddr*/ 0xBFFC,
                /*lockEveryN*/ 3,
                /*seed*/ 0x1000ull + i));
            dispatcher.addAgent(cpus.back().get());
        }
    }
};

// Capture the full observable state so two runs can be compared bit-for-bit.
struct Fingerprint {
    std::vector<std::uint64_t> counters;
    std::vector<std::uint64_t> grants;
    std::vector<std::uint64_t> rng;
    std::vector<std::vector<std::uint8_t>> grantLogs;

    bool operator==(const Fingerprint& o) const {
        return counters == o.counters && grants == o.grants
            && rng == o.rng && grantLogs == o.grantLogs;
    }
};

Fingerprint snapshot(const Scenario& s) {
    Fingerprint f;
    for (const auto& c : s.cpus) {
        f.counters.push_back(c->counter());
        f.grants.push_back(c->grants());
        f.rng.push_back(c->rngState());
        f.grantLogs.push_back(c->grantLog());
    }
    return f;
}

} // namespace

// ----------------------------------------------------------------------------
TEST_CASE("lock_arbiter_semantics") {
    LockArbiter arb;

    // Plain LL then SC by the same agent on the same granule: SC succeeds.
    arb.loadLocked(0, 0x100);
    CHECK(arb.storeCond(0, 0x100) == true);
    // Lock consumed by the successful SC: a second SC fails.
    CHECK(arb.storeCond(0, 0x100) == false);

    // Cross-agent invalidation: agent 1's store breaks agent 0's lock.
    arb.loadLocked(0, 0x200);
    arb.store(1, 0x200);                 // breaks 0's lock on this granule
    CHECK(arb.storeCond(0, 0x200) == false);

    // Mutual exclusion under interleave: 0 LLs, 1 LLs (overwrites), then both
    // SC -> only the current holder (1) wins.
    arb.loadLocked(0, 0x300);
    arb.loadLocked(1, 0x300);            // 1 is now the holder
    CHECK(arb.storeCond(0, 0x300) == false);
    CHECK(arb.storeCond(1, 0x300) == true);
}

// ----------------------------------------------------------------------------
// THE crown jewel: sequential and threaded execution are bit-identical.
TEST_CASE("determinism_equivalence") {
    constexpr int     kCpus    = 4;
    constexpr Quantum kQuantum = 1;      // lock-step: tightest interleave
    constexpr Tick    kUntil   = 5000;

    Scenario seq(kCpus, kQuantum);
    seq.dispatcher.setDriver(std::make_unique<SequentialDriver>());
    seq.dispatcher.run(kUntil);
    Fingerprint fSeq = snapshot(seq);

    Scenario thr(kCpus, kQuantum);
    thr.dispatcher.setDriver(std::make_unique<ThreadedDriver>());
    thr.dispatcher.run(kUntil);
    Fingerprint fThr = snapshot(thr);

    CHECK(fSeq == fThr);                 // identical final state across drivers

    // Sanity: the lock path was actually exercised (some grants happened).
    std::uint64_t total = 0;
    for (auto g : fSeq.grants) total += g;
    CHECK(total > 0);
}

// ----------------------------------------------------------------------------
// Same dispatcher, swap the driver between runs -- the on/off toggle.
TEST_CASE("driver_toggle") {
    Scenario s(2, /*quantum*/ 8);

    s.dispatcher.setDriver(std::make_unique<SequentialDriver>());
    CHECK(std::string(s.dispatcher.driver()->name()) == "sequential");
    s.dispatcher.run(800);
    auto afterSeq = snapshot(s);

    s.dispatcher.setDriver(std::make_unique<ThreadedDriver>());
    CHECK(std::string(s.dispatcher.driver()->name()) == "threaded");
    s.dispatcher.run(1600);              // continue from current clock to 1600
    auto afterThr = snapshot(s);

    // Work advanced after the second run.
    CHECK(afterThr.counters[0] > afterSeq.counters[0]);
}

// ----------------------------------------------------------------------------
// A parked (halted) agent consumes no work and does NOT deadlock the threaded
// barrier (it still arrives each quantum, just skips step()).
TEST_CASE("parked_agent_no_deadlock") {
    Scenario s(3, /*quantum*/ 1);
    s.cpus[1]->halt();                   // park CPU1

    s.dispatcher.setDriver(std::make_unique<ThreadedDriver>());
    s.dispatcher.run(2000);              // must terminate, not hang

    CHECK(s.cpus[0]->counter() > 0);
    CHECK(s.cpus[1]->counter() == 0);    // parked: no progress
    CHECK(s.cpus[2]->counter() > 0);

    // Release and run more: CPU1 now advances.
    s.cpus[1]->release();
    s.dispatcher.run(4000);
    CHECK(s.cpus[1]->counter() > 0);
}

// ----------------------------------------------------------------------------
// Determinism holds across quantum widths too (same driver, different quantum,
// run to the same logical tick -> identical state, because the sync boundary
// math is quantum-agnostic for this agent).
TEST_CASE("determinism_across_quantum_sequential") {
    auto runWith = [](Quantum q) {
        Scenario s(3, q);
        s.dispatcher.setDriver(std::make_unique<SequentialDriver>());
        s.dispatcher.run(2400);
        return snapshot(s);
    };
    // NOTE: counters track total ticks, so they match; grant LOGS depend on the
    // per-call transaction cadence, which is call-count based -- so this checks
    // counter/rng equivalence specifically. (Grant cadence vs quantum is a
    // documented modeling choice of the mock, not a harness property.)
    auto a = runWith(1);
    auto b = runWith(4);
    CHECK(a.counters == b.counters);
    CHECK(a.rng == b.rng);
}
