// ============================================================================
// SmpDrivers.h -- the two swappable execution drivers (the on/off toggle).
// ============================================================================
//
// SequentialDriver  : all agents step in index order on ONE host thread, then
//                     syncPhase, then advance. Fully deterministic. Use for
//                     bring-up, snapshot/resume validation, AXPBox trace-diff,
//                     and the determinism-equivalence test.
//
// ThreadedDriver    : each agent steps on its OWN host thread; threads
//                     rendezvous at a std::barrier each quantum, whose
//                     completion runs syncPhase single-threaded, then releases.
//                     Real host parallelism, but determinism is PRESERVED
//                     because (a) step() touches only private state per the
//                     contract, and (b) all shared-state mutation happens in
//                     the single-threaded barrier completion. Use for profiling
//                     and future throughput.
//
// Both drivers call syncPhase exactly once per quantum and advance the clock by
// the dispatcher quantum. Swap them via Dispatcher::setDriver with zero change
// to agents. If a determinism test ever fails under ThreadedDriver but passes
// under SequentialDriver, an agent is violating the contract (touching shared
// state inside step()) -- that is the bug the equivalence test exists to catch.
//
// No Qt. C++20 (<barrier>, <thread>/<jthread>).
// ============================================================================

#ifndef SCHEDLIB_SMP_DRIVERS_H
#define SCHEDLIB_SMP_DRIVERS_H

#include "SmpHarness.h"

#include <atomic>
#include <barrier>
#include <thread>
#include <utility>
#include <vector>

namespace emulatr::smp {

// ----------------------------------------------------------------------------
// joining_thread -- std::jthread where the toolchain provides it (MSVC, recent
// libc++), else a minimal auto-joining std::thread wrapper.  ThreadedDriver
// uses only emplace + join-on-scope-exit (no stop_token), so the fallback is
// sufficient.  The Windows build stays on std::jthread, byte-identical; only
// toolchains whose libc++ lacks std::jthread (e.g. Apple clang 14) fall back.
// ----------------------------------------------------------------------------
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
using joining_thread = std::jthread;
#else
class joining_thread {
public:
    joining_thread() noexcept = default;
    template <class F, class... Args>
    explicit joining_thread(F&& f, Args&&... args)
        : t_(std::forward<F>(f), std::forward<Args>(args)...) {}
    joining_thread(joining_thread&&) noexcept            = default;
    joining_thread& operator=(joining_thread&& o) noexcept {
        if (t_.joinable()) t_.join();
        t_ = std::move(o.t_);
        return *this;
    }
    joining_thread(const joining_thread&)            = delete;
    joining_thread& operator=(const joining_thread&) = delete;
    ~joining_thread() { if (t_.joinable()) t_.join(); }
private:
    std::thread t_;
};
#endif

// ----------------------------------------------------------------------------
// SequentialDriver -- deterministic, single host thread. The reference model.
// ----------------------------------------------------------------------------
class SequentialDriver final : public IExecutionDriver {
public:
    const char* name() const noexcept override { return "sequential"; }

    void run(Dispatcher& d, Tick untilTick) override {
        while (d.clock().now() < untilTick) {
            bool anyRunnable = false;
            for (IAgent* a : d.agents())
                if (a->runnable()) {
                    anyRunnable = true;
                    a->step(d.quantum());
                }
            // The correct terminating condition for a driver: when no agent can
            // run (all halted/parked) there is no more work -- stop, do not spin
            // the clock to untilTick.  This is what lets a dispatcher-driven boot
            // match legacy run()'s break-on-halt.
            if (!anyRunnable) break;
            d.syncPhase();
            d.clock().advance(d.quantum());
        }
    }
};

// ----------------------------------------------------------------------------
// ThreadedDriver -- one host thread per agent, barrier-synchronized per quantum.
//
// Invariant that keeps it deterministic and deadlock-free: every agent thread
// performs the SAME number of arrive_and_wait() calls. A non-runnable (parked)
// agent still arrives at the barrier each quantum; it just skips step(). The
// barrier participant count is therefore constant for the whole run.
//
// The barrier completion function runs on exactly one arriving thread AFTER all
// have arrived and BEFORE any are released -- the perfect single-threaded slot
// for syncPhase + clock advance + termination check. It must be noexcept;
// syncPhase is pure deterministic state-shuffling and is not expected to throw.
// ----------------------------------------------------------------------------
class ThreadedDriver final : public IExecutionDriver {
public:
    const char* name() const noexcept override { return "threaded"; }

    void run(Dispatcher& d, Tick untilTick) override {
        auto& agents = d.agents();
        if (agents.empty()) return;

        std::atomic<bool> done{false};

        // Completion: runs single-threaded at each barrier point.
        auto onBarrier = [&d, untilTick, &done]() noexcept {
            d.syncPhase();
            d.clock().advance(d.quantum());
            // Terminate at the cycle cap OR when no agent can still run (all
            // halted/parked) -- the correct terminating condition for a driver.
            bool anyRunnable = false;
            for (IAgent* a : d.agents())
                if (a->runnable()) { anyRunnable = true; break; }
            if (d.clock().now() >= untilTick || !anyRunnable)
                done.store(true, std::memory_order_relaxed);
        };

        std::barrier sync(static_cast<std::ptrdiff_t>(agents.size()), onBarrier);

        std::vector<joining_thread> threads;
        threads.reserve(agents.size());
        for (IAgent* a : agents) {
            threads.emplace_back([a, &d, &sync, &done]() {
                for (;;) {
                    if (done.load(std::memory_order_relaxed)) break;
                    if (a->runnable())
                        a->step(d.quantum());      // CONCURRENT, private state only
                    sync.arrive_and_wait();        // -> completion runs syncPhase
                }
            });
        }
        // jthreads join on scope exit.
    }
};

} // namespace emulatr::smp

#endif // SCHEDLIB_SMP_DRIVERS_H
