// ============================================================================
// pteLib/SPAMShardManager.h -- TLB front-end (ITB/DTB cache with epoch sweep)
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
// SPAMShardManager = the V4 TLB front-end.  Holds:
//
//   - an array of SPAMBucket<W> shards,
//   - the two-counter TlbEpochState (global + process epochs),
//   - a small kHash mix to pick a shard from a tag.
//
// Public surface mirrors the architectural operations the translator and
// the ITB/DTB IPR write handlers need:
//
//   lookup              -- hot path; one per memory access
//   insert              -- TLB-miss handler / HW_MTPR ITB_PTE / DTB_PTE
//   invalidateAll       -- TBIA  (HRM 4.1.6)
//   invalidateAllProcess-- TBIAP (HRM 4.1.6; ASM=1 survives, HRM 4.1.7)
//   invalidateSingle    -- TBIS / TBISI / TBISD  (targeted; HRM 4.1.6)
//   clear               -- cold boot / snapshot restore
//
// The realm (ITB vs DTB) is part of the lookup tag, so this class CAN
// service both realms in a single instance.  The translator may instead
// instantiate two managers (one per realm) if per-realm flush semantics
// or independent sizing matter.  That choice is the integrator's; this
// header stays neutral.
//
// Concurrency:
//
//   TBs are CPU-local.  Each CpuState owns its own pair of managers; no
//   peer-CPU thread touches another CPU's manager.  Cross-CPU invalidation
//   (in a future SMP build) goes through IPI machinery -- the target CPU
//   processes invalidations on its own thread.  The manager therefore
//   carries no synchronisation primitives: epoch counters are plain
//   uint64_t, bucket access is single-threaded by construction.
//
// Sizing:
//
//   Default is kShards = 32 shards x kWays = 8 = 256 slots, enough to
//   model both architectural TBs (128 entries each).  Tune via the
//   non-type template parameters when instantiating for ITB-only or
//   DTB-only deployments.
//
// ============================================================================

#ifndef PTELIB_SPAM_SHARD_MANAGER_H
#define PTELIB_SPAM_SHARD_MANAGER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "coreLib/VA_types.h"      // ASNType
#include "pteLib/AlphaPte.h"
#include "pteLib/SPAMBucket.h"
#include "pteLib/TlbEntry.h"
#include "pteLib/TlbEpoch.h"

namespace pteLib {


// ---------------------------------------------------------------------------
// LookupResult -- manager-level lookup outcome.
//
// Carries the AlphaPte payload, a flag for hit/miss, and the realm/way
// where the hit came from (for trace / debug only; the production hot path
// reads only `pte` and `hit`).
// ---------------------------------------------------------------------------
struct LookupResult {
    AlphaPte     pte;
    bool         hit = false;
    TlbRealm     realm = TlbRealm::Dtb;

    constexpr bool isHit() const noexcept { return hit; }

    static constexpr LookupResult miss(TlbRealm r) noexcept
    {
        LookupResult x;
        x.realm = r;
        return x;
    }
    static constexpr LookupResult fromHit(AlphaPte p, TlbRealm r) noexcept
    {
        LookupResult x;
        x.pte   = p;
        x.hit   = true;
        x.realm = r;
        return x;
    }
};


// ---------------------------------------------------------------------------
// SPAMShardManager<Shards, Ways> -- the manager itself.
//
// Shards must be a power of two so the shard-index computation is a cheap
// mask rather than a modulo.  Ways is the per-bucket width.
// ---------------------------------------------------------------------------
template <std::size_t Shards = 32, std::size_t Ways = 8>
class SPAMShardManager {
public:
    static_assert(Shards > 0 && (Shards & (Shards - 1)) == 0,
                  "Shards must be a power of two");
    static_assert(Ways   > 0,
                  "Ways must be positive");

    static constexpr std::size_t kShards = Shards;
    static constexpr std::size_t kWays   = Ways;
    static constexpr std::size_t kShardMask = Shards - 1;

    using Bucket = SPAMBucket<Ways>;

    SPAMShardManager() noexcept = default;

    // Copy and move are defaulted.  The manager is plain data with no
    // ownership of external state; CpuState can be copied normally for
    // snapshot save/restore.


    // -- Read path ------------------------------------------------------
    //
    // Look up `va` in the realm.  Snapshots the two manager epochs once
    // at the top, then dispatches to a single bucket.  Returns a
    // LookupResult that is hit() iff a tag-and-epoch match was found.
    //
    // This is the per-memory-access entry point; the translator drives it.
    LookupResult lookup(TlbRealm         realm,
                        uint64_t         va,
                        coreLib::ASNType asn) const noexcept;


    // -- Write path -----------------------------------------------------
    //
    // Install a freshly-translated PTE.  Caller has decoded the IPR and
    // built a canonical AlphaPte; this method stamps the current epoch
    // pair into the entry and routes it to the correct shard.
    void insert(TlbRealm         realm,
                uint64_t         va,
                coreLib::ASNType asn,
                AlphaPte         pte,
                uint8_t          gh = 0) noexcept;


    // -- Bulk invalidation ---------------------------------------------
    //
    // TBIA -- invalidate every entry, both realms, ASM and non-ASM alike.
    // One counter bump; no bucket touched.
    void invalidateAll() noexcept;

    // TBIAP -- invalidate every non-ASM entry in both realms.  ASM=1
    // entries survive (HRM 4.1.7).  One counter bump; no bucket touched.
    void invalidateAllProcess() noexcept;


    // -- Targeted invalidation -----------------------------------------
    //
    // TBIS / TBISI / TBISD -- locate the single (realm, va, asn) entry
    // and clear it in place.  Returns true if anything was cleared
    // (mainly for trace -- production code does not branch on this).
    bool invalidateSingle(TlbRealm         realm,
                          uint64_t         va,
                          coreLib::ASNType asn) noexcept;


    // -- Reset ---------------------------------------------------------
    //
    // Drop every slot and zero the epoch counters.  Used at cold boot
    // and at snapshot restore; not used in the normal flush path.
    void clear() noexcept;


    // -- Introspection --------------------------------------------------
    //
    // Live epoch counters (for trace formatters and test assertions).
    // Production lookup() does not call these directly; it snapshots
    // internally.
    EpochValue globalEpoch()  const noexcept { return m_epochs.globalEpoch.snapshot();  }
    EpochValue processEpoch() const noexcept { return m_epochs.processEpoch.snapshot(); }

    // Count occupied slots across all shards.  O(Shards * Ways); for
    // tests and diagnostic dumps only.
    std::size_t occupancy() const noexcept;


private:
    // Pick a shard from a tag.  Power-of-two mask, no modulo.
    constexpr std::size_t shardIndexOf(TlbTag tag) const noexcept
    {
        return static_cast<std::size_t>(tlbKeyHash(tag) & kShardMask);
    }

    TlbEpochState              m_epochs{};
    std::array<Bucket, Shards> m_shards{};
};


// ---------------------------------------------------------------------------
// Convenience alias.  Most translator-side code should use this and not
// re-spell the template parameters.
// ---------------------------------------------------------------------------
using DefaultShardManager = SPAMShardManager<32, 8>;


} // namespace pteLib

#endif // PTELIB_SPAM_SHARD_MANAGER_H
