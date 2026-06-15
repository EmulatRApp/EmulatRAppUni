// ============================================================================
// pteLib/SPAMShardManager.cpp -- bodies for the TLB front-end
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
// Bodies for the methods declared in pteLib/SPAMShardManager.h.
//
// Granularity-Hint probe policy (lookup path):
//
//   tlbKeyHash() mixes the GH field into the shard index, so the same
//   VPN with two different GH values lands in two different shards.  The
//   manager therefore probes shards for each plausible GH (0..3) until
//   one returns a hit.  GH=0 is the overwhelmingly common case, so the
//   inner loop short-circuits early.
//
//   This is the SIMPLEST correct policy.  A future optimisation is to
//   drop GH from the hash and let one bucket scan handle all GH values
//   in a single probe.  That changes tlbKeyHash() and the tag layout,
//   which is a scaffold-breaking change deferred to a later checkpoint.
//
// Bulk invalidation (TBIA / TBIAP):
//
//   ONE counter bump on the corresponding EpochCounter.  No bucket scan
//   is performed -- stale entries are detected lazily on the next
//   lookup that walks them, via isEpochLive() in TlbEpoch.h.  This is
//   the entire reason for the two-counter design.
//
// Targeted invalidation (TBIS / TBISI / TBISD):
//
//   Hashes to ONE shard, then calls bucket.invalidateMatching().  The
//   ASN argument matches both ASM=0 entries (by ASN) and ASM=1 entries
//   (because TlbEntry::matches() short-circuits on asmGlobal).  Range
//   over the four GH values for the same reason as lookup.
//
// ============================================================================

#include "pteLib/SPAMShardManager.h"

namespace pteLib {


// ---------------------------------------------------------------------------
// lookup -- snapshot epochs once, probe four GH variants.
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
LookupResult SPAMShardManager<Shards, Ways>::lookup(
    TlbRealm         realm,
    uint64_t         va,
    coreLib::ASNType asn) const noexcept
{
    EpochValue const liveGlobal  = m_epochs.globalEpoch.snapshot();
    EpochValue const liveProcess = m_epochs.processEpoch.snapshot();

    uint64_t const rawVpn = vaToVpn(va);

    // Probe GH=0 first (the common 8 KiB-page path), then escalate to
    // GH=1/2/3 for super-pages.  Each iteration normalises the VPN
    // through the tag constructor so super-page tags compare correctly
    // against entries that were inserted with the same GH.
    for (uint8_t gh = 0; gh <= 3; ++gh) {
        TlbTag const probe{rawVpn, asn, realm, gh};
        std::size_t const shardIdx = shardIndexOf(probe);

        // Shard selection uses the GH-normalised probe (entries are sharded
        // by their GH-normalised VPN), but the MATCH must see the RAW vpn:
        // TlbEntry::matches() re-masks by the *entry's own* GH.  Passing the
        // probe's GH-masked vpn here would, on a shard-index collision (only
        // 16 shards -> 4-bit mask), let a small-GH entry spuriously match a
        // larger-GH probe whose mask had erased the distinguishing low VPN
        // bits -- e.g. a GH=1 probe of VPN 0x301 masks to 0x300 and hits a
        // GH=0 identity entry for 0x300.  rawVpn closes that cross-GH match.
        LookupOutcome const out =
            m_shards[shardIdx].lookup(rawVpn,
                                      asn,
                                      liveGlobal,
                                      liveProcess);
        if (out.hit()) {
            return LookupResult::fromHit(out.pte, realm);
        }
    }

    return LookupResult::miss(realm);
}


// ---------------------------------------------------------------------------
// insert -- stamp current epoch pair, route to the correct shard.
//
// Caller passes the GH the new entry should carry; the TlbTag constructor
// normalises the VPN against vpnMaskForGh(gh) so lookups for any VA
// inside the GH block hit this entry.
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
void SPAMShardManager<Shards, Ways>::insert(
    TlbRealm         realm,
    uint64_t         va,
    coreLib::ASNType asn,
    AlphaPte         pte,
    uint8_t          gh) noexcept
{
    EpochValue const liveGlobal  = m_epochs.globalEpoch.snapshot();
    EpochValue const liveProcess = m_epochs.processEpoch.snapshot();

    TlbTag const tag{vaToVpn(va), asn, realm, gh};
    TlbEpochSnapshot const snapshot{liveGlobal, liveProcess};
    TlbEntry const entry{tag, pte, snapshot};

    std::size_t const shardIdx = shardIndexOf(tag);
    (void)m_shards[shardIdx].insert(entry);
}


// ---------------------------------------------------------------------------
// invalidateAll -- TBIA.  One counter bump on globalEpoch flushes every
// entry in both realms, ASM and non-ASM alike, in O(1).
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
void SPAMShardManager<Shards, Ways>::invalidateAll() noexcept
{
    (void)m_epochs.globalEpoch.bump();
}


// ---------------------------------------------------------------------------
// invalidateAllProcess -- TBIAP.  Bumps processEpoch only.  ASM=1
// entries survive because isEpochLive() short-circuits the process-epoch
// check when entry.asmGlobal is true (HRM 4.1.7).
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
void SPAMShardManager<Shards, Ways>::invalidateAllProcess() noexcept
{
    (void)m_epochs.processEpoch.bump();
}


// ---------------------------------------------------------------------------
// invalidateSingle -- TBIS / TBISI / TBISD.  Probes the four GH shards
// (same set the lookup path uses) and invalidates any matching slot in
// each.  Returns true iff anything was cleared.
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
bool SPAMShardManager<Shards, Ways>::invalidateSingle(
    TlbRealm         realm,
    uint64_t         va,
    coreLib::ASNType asn) noexcept
{
    uint64_t const rawVpn = vaToVpn(va);
    bool cleared = false;

    for (uint8_t gh = 0; gh <= 3; ++gh) {
        TlbTag const probe{rawVpn, asn, realm, gh};
        std::size_t const shardIdx = shardIndexOf(probe);
        // Same rule as lookup(): shard-select with the GH-normalised probe,
        // but match on the RAW vpn so matches() masks by the entry's own GH
        // and a cross-GH shard collision cannot invalidate the wrong entry.
        if (m_shards[shardIdx].invalidateMatching(rawVpn, asn, realm)) {
            cleared = true;
        }
    }

    return cleared;
}


// ---------------------------------------------------------------------------
// clear -- cold boot / snapshot restore.  Resets both epoch counters and
// drops every slot.  Caller is expected to ensure no concurrent access
// (it's a quiescent operation by design).
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
void SPAMShardManager<Shards, Ways>::clear() noexcept
{
    m_epochs.globalEpoch.reset();
    m_epochs.processEpoch.reset();
    for (std::size_t i = 0; i < Shards; ++i) {
        m_shards[i].clear();
    }
}


// ---------------------------------------------------------------------------
// occupancy -- O(Shards * Ways) walk for tests and trace formatters.
// Production code never calls this; it would defeat the lazy-invalidation
// design's whole purpose.
// ---------------------------------------------------------------------------
template <std::size_t Shards, std::size_t Ways>
std::size_t SPAMShardManager<Shards, Ways>::occupancy() const noexcept
{
    EpochValue const liveGlobal  = m_epochs.globalEpoch.snapshot();
    EpochValue const liveProcess = m_epochs.processEpoch.snapshot();

    std::size_t count = 0;
    for (std::size_t s = 0; s < Shards; ++s) {
        for (std::size_t w = 0; w < Ways; ++w) {
            TlbEntry const slot = m_shards[s].slotAt(w);
            if (!slot.valid) continue;
            // Don't count slots that are valid-by-flag but stale-by-epoch;
            // they'll be reused on the next insert.
            if (!slot.isLiveUnder(liveGlobal, liveProcess)) continue;
            ++count;
        }
    }
    return count;
}


// ===========================================================================
// Explicit template instantiations.  See the comment in SPAMBucket.cpp
// for the rationale and the procedure for adding more shapes.
// ===========================================================================
template class SPAMShardManager<16, 8>;   // CpuState-resident ITB/DTB (128 slots)
template class SPAMShardManager<32, 8>;   // original default
template class SPAMShardManager<8,  4>;   // tests prefer the smaller shape


} // namespace pteLib
