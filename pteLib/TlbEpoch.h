// ============================================================================
// pteLib/TlbEpoch.h -- lazy-invalidation epoch counters for the TLB
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
// Epoch-based lazy invalidation for the V4 TLB.  Two counters are kept by
// the SPAMShardManager and snapshotted into every TlbEntry at insert time.
// A flush is a single counter bump; entries become stale the instant their
// snapshot fails to match the live counter on the next lookup.
//
// Mapping from Alpha PALcode flush operations:
//
//   Operation         Action                                Lazy?
//   ----------------- ------------------------------------- -------------
//   TBIA              bump globalEpoch                      yes
//   TBIAP             bump processEpoch                     yes (ASM survives)
//   TBIS / TBISI /    locate matching entry, clear in-place no  (targeted)
//   TBISD
//
// Liveness predicate (applied at lookup time):
//
//   live(entry, mgr) =     (entry.globalEpoch  == mgr.globalEpoch)
//                      AND ( entry.asmGlobal
//                            OR entry.processEpoch == mgr.processEpoch )
//
// Concurrency model: TBs are CPU-local.  Each CpuState owns its own pair
// of SPAMShardManagers; no peer-CPU thread ever touches another CPU's TB
// internals.  Cross-CPU invalidation (in a future SMP build) goes through
// IPI machinery -- the target CPU processes invalidations on its own
// thread.  Therefore EpochCounter is plain integer, not std::atomic; the
// previous seqlock + atomic counter design was modelling a sharing pattern
// that does not exist by construction.
//
// Reference: Alpha Architecture Reference Manual 4.1.6 / 4.1.7 (TB Misses,
// Translation Buffer Management).
//
// ============================================================================

#ifndef PTELIB_TLB_EPOCH_H
#define PTELIB_TLB_EPOCH_H

#include <cstdint>

namespace pteLib {


// ---------------------------------------------------------------------------
// EpochValue -- plain integer captured into a TlbEntry at insert time.
// Comparison only; never bumped directly.  Distinct typedef so a stray
// `entry.processEpoch++` won't compile.
// ---------------------------------------------------------------------------
using EpochValue = uint64_t;

// Sentinel used in default-constructed (empty) entries.  Any insert with
// an EpochCounter that has been bumped at least once will read != 0, so
// a freshly-constructed entry's epoch is guaranteed to mismatch.
constexpr EpochValue kEpochZero = 0;


// ---------------------------------------------------------------------------
// EpochCounter -- the live, manager-side counter that flushes bump.
//
// Plain uint64_t wrapper.  Two operations:
//
//   bump()      -- advance the counter by one, return the *new* value.
//                  Used by TBIA / TBIAP handlers in the manager.
//
//   snapshot()  -- read the current value.  Used at insert (to stamp into
//                  the entry) and at lookup (to compare against the entry's
//                  stamp).
//
// The counter is wrap-around-safe in practice -- at one bump per 100 ns,
// wrapping a 64-bit counter takes ~58,000 years.  We do not handle wrap;
// if it ever matters, a manager reset zeroes both counters and clears
// every shard.
// ---------------------------------------------------------------------------
class EpochCounter {
public:
    EpochCounter() noexcept = default;

    // Advance the counter.  Returns the new value (post-increment).
    EpochValue bump() noexcept
    {
        return ++m_value;
    }

    // Read the current counter value.
    EpochValue snapshot() const noexcept
    {
        return m_value;
    }

    // Hard reset.  Used at boot / snapshot-restore; not called from
    // normal flush paths.
    void reset() noexcept
    {
        m_value = 0;
    }

private:
    uint64_t m_value = 0;
};


// ---------------------------------------------------------------------------
// TlbEpochSnapshot -- the pair of epoch values stamped into a TlbEntry at
// insert time.
// ---------------------------------------------------------------------------
struct TlbEpochSnapshot {
    EpochValue globalEpoch  = kEpochZero;
    EpochValue processEpoch = kEpochZero;

    constexpr TlbEpochSnapshot() noexcept = default;
    constexpr TlbEpochSnapshot(EpochValue g, EpochValue p) noexcept
        : globalEpoch(g), processEpoch(p)
    {}
};


// ---------------------------------------------------------------------------
// TlbEpochState -- the manager-side current counter pair.  Passed by const
// reference to the liveness predicate below.
//
// This is a thin holder rather than a full class because the manager owns
// the policy (when to bump globalEpoch vs processEpoch); this header just
// supplies the data carrier and the predicate.
// ---------------------------------------------------------------------------
struct TlbEpochState {
    EpochCounter globalEpoch;
    EpochCounter processEpoch;
};


// ---------------------------------------------------------------------------
// isEpochLive -- the central liveness predicate.
//
// `asmGlobal` argument is the entry's cached AlphaPte.bitASM() value.  An
// ASM=1 (global) mapping ignores the processEpoch arm of the test -- this
// is the Alpha "ASM survives TBIAP" rule (HRM 4.1.7).
//
// Constexpr so unit tests can verify the algebra at compile time.
// ---------------------------------------------------------------------------
constexpr bool isEpochLive(TlbEpochSnapshot entrySnapshot,
                           EpochValue       liveGlobalEpoch,
                           EpochValue       liveProcessEpoch,
                           bool             asmGlobal) noexcept
{
    if (entrySnapshot.globalEpoch != liveGlobalEpoch) {
        return false;                       // TBIA invalidates everything
    }
    if (asmGlobal) {
        return true;                        // ASM=1 survives TBIAP
    }
    return entrySnapshot.processEpoch == liveProcessEpoch;
}


} // namespace pteLib

#endif // PTELIB_TLB_EPOCH_H
