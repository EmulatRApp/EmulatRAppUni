// ============================================================================
// pteLib/SPAMBucket.h -- one shard of the SPAM-associative TLB
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
// SPAMBucket = one shard of the Set-Partitioned Associative Memory used by
// SPAMShardManager.  A bucket is W-way set-associative; the manager picks
// the bucket by hashing the lookup tag, then the bucket does an O(W) linear
// scan of its slots.
//
// Concurrency model:
//
//   TBs are CPU-local (each CpuState owns its own pair of managers).  No
//   peer-CPU thread ever touches another CPU's bucket internals.  Cross-CPU
//   invalidation (in a future SMP build) goes through IPI machinery -- the
//   target CPU processes invalidations on its own thread, at which point
//   the access is single-threaded again.  Therefore the bucket carries no
//   synchronisation primitives; lookup is a plain scan, insert is a plain
//   slot write.  Standard C++ trivially-copyable POD-like type.
//
// Replacement policy:
//
//   Round-robin replacement (cursor advances on every insert).  Matches
//   HRM 5.2.1 "the specific ITB entry that is written is determined by a
//   round-robin mechanism; the mechanism writes to entry #0 as the first
//   entry after chip reset" -- this is the architectural model, not a
//   scaffold simplification.  Future NRU/PLRU policies can swap in
//   without changing the public surface.
//
// Targeted invalidation:
//
//   TBIS / TBISI / TBISD reach this layer (via the manager) and clear one
//   slot in place.  Bulk invalidation (TBIA / TBIAP) does NOT touch buckets
//   -- the epoch sweep in TlbEpoch.h handles those in a single counter bump.
//
// ============================================================================

#ifndef PTELIB_SPAM_BUCKET_H
#define PTELIB_SPAM_BUCKET_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "pteLib/TlbEntry.h"
#include "pteLib/TlbEpoch.h"

namespace pteLib {


// ---------------------------------------------------------------------------
// LookupOutcome -- bucket-scan result.  hit() implies entry was both
// tag-matched AND epoch-live.  Callers should not look at `pte` unless
// hit() is true.
// ---------------------------------------------------------------------------
struct LookupOutcome {
    AlphaPte pte;
    bool     found = false;

    constexpr bool hit() const noexcept { return found; }

    static constexpr LookupOutcome miss() noexcept { return LookupOutcome{}; }
    static constexpr LookupOutcome makeHit(AlphaPte p) noexcept
    {
        LookupOutcome r;
        r.pte   = p;
        r.found = true;
        return r;
    }
};


// ---------------------------------------------------------------------------
// SPAMBucket<Ways> -- one shard.
//
// Template parameter is the way-count (slots per bucket).  Default 8
// matches V1 and the architectural set-associativity of an EV6 TB
// way-group (HRM 4.2).
// ---------------------------------------------------------------------------
template <std::size_t Ways = 8>
class SPAMBucket {
public:
    static_assert(Ways > 0 && Ways <= 64, "Ways must be in 1..64");

    static constexpr std::size_t kWays = Ways;

    SPAMBucket() noexcept = default;

    // Copy and move are defaulted.  The bucket is plain data with no
    // ownership of external state; CpuState can be copied normally for
    // snapshot save/restore.

    // -- Read path -------------------------------------------------------
    //
    // Look up an entry by VPN + ASN, with the lazy-invalidation predicate
    // applied against the supplied live-epoch pair.  Caller obtains the
    // live epochs ONCE at the top of the access, then passes them down.
    LookupOutcome lookup(uint64_t         lookupVpn,
                         coreLib::ASNType lookupAsn,
                         EpochValue       liveGlobalEpoch,
                         EpochValue       liveProcessEpoch) const noexcept;

    // -- Write path ------------------------------------------------------
    //
    // Insert an entry into a free slot, or evict via the round-robin
    // cursor.  Returns the slot index that was used (for debug/tracing).
    std::size_t insert(TlbEntry const& entry) noexcept;

    // Targeted invalidation -- TBIS path.  Scans the bucket for a slot
    // whose tag matches AND whose realm matches; if found, clears it in
    // place.  Returns true if anything was cleared.
    bool invalidateMatching(uint64_t         lookupVpn,
                            coreLib::ASNType lookupAsn,
                            TlbRealm         realm) noexcept;

    // Drop every slot.  Used by manager.clear() at cold boot or snapshot
    // restore; not used by the per-flush hot path (epoch sweep handles that).
    void clear() noexcept;


    // -- Introspection / test hooks --------------------------------------
    //
    // Allows tests to assert "slot N is now occupied" or "slot N's epoch
    // matches the global epoch".  Not part of the production read path;
    // production code uses lookup() exclusively.
    constexpr std::size_t ways() const noexcept { return kWays; }

    TlbEntry slotAt(std::size_t way) const noexcept;     // body in .cpp

    // True iff at least one slot is valid.  Cheap O(W) scan; for tests.
    bool any() const noexcept;

private:
    // Round-robin replacement cursor.  Wraps modulo Ways.
    std::size_t m_replaceCursor{0};

    // Slot array.  Default-constructed entries have valid=false.
    std::array<TlbEntry, Ways> m_slots{};
};


// ---------------------------------------------------------------------------
// Convenience alias for the default-shape bucket.  Most code should use
// this rather than spelling the template argument every time.
// ---------------------------------------------------------------------------
using DefaultBucket = SPAMBucket<8>;


} // namespace pteLib

#endif // PTELIB_SPAM_BUCKET_H
