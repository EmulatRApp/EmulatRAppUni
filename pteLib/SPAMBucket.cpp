// ============================================================================
// pteLib/SPAMBucket.cpp -- bodies for one shard of the SPAM-associative TLB
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
// Bodies for the methods declared in pteLib/SPAMBucket.h.
//
// TBs are CPU-local and accessed single-threaded per CPU, so there is no
// synchronisation here: lookup is a plain O(Ways) scan, insert is a plain
// slot write.  Template instantiation for the default shape SPAMBucket<8>
// is at the bottom; additional sizes require their own
// `template class SPAMBucket<N>;` line.
//
// ============================================================================

#include "pteLib/SPAMBucket.h"

namespace pteLib {


// ---------------------------------------------------------------------------
// Read path -- an O(Ways) linear scan.
//
// The match predicate is split in two: TlbEntry::matches() does the cheap
// tag compare (VPN + ASN), and TlbEntry::isLiveUnder() applies the lazy-
// invalidation epoch check.  Both must pass for a hit.  matches() is
// cheaper so we run it first.
// ---------------------------------------------------------------------------
template <std::size_t Ways>
LookupOutcome SPAMBucket<Ways>::lookup(uint64_t         lookupVpn,
                                       coreLib::ASNType lookupAsn,
                                       EpochValue       liveGlobalEpoch,
                                       EpochValue       liveProcessEpoch)
    const noexcept
{
    for (std::size_t i = 0; i < Ways; ++i) {
        TlbEntry const& slot = m_slots[i];
        if (!slot.matches(lookupVpn, lookupAsn)) {
            continue;
        }
        if (!slot.isLiveUnder(liveGlobalEpoch, liveProcessEpoch)) {
            continue;
        }
        return LookupOutcome::makeHit(slot.pte);
    }
    return LookupOutcome::miss();
}


// ---------------------------------------------------------------------------
// Write path -- insert with round-robin replacement.
//
// First scan: claim a free (valid=false) slot if any exist.  Otherwise
// evict the slot at m_replaceCursor.  Cursor advances on every insert
// regardless of which path was taken, so the eviction pressure rotates
// even when free slots are still available.  This keeps the cursor's
// pattern predictable for tests and matches HRM 5.2.1's round-robin
// "writes to entry #0 as the first entry after chip reset".
// ---------------------------------------------------------------------------
template <std::size_t Ways>
std::size_t SPAMBucket<Ways>::insert(TlbEntry const& entry) noexcept
{
    std::size_t target = Ways;          // sentinel "no free slot found"
    for (std::size_t i = 0; i < Ways; ++i) {
        if (!m_slots[i].valid) {
            target = i;
            break;
        }
    }

    if (target == Ways) {
        // No free slot -- evict the round-robin victim.
        target = m_replaceCursor;
    }

    m_slots[target]  = entry;
    m_replaceCursor  = (m_replaceCursor + 1) % Ways;

    return target;
}


// ---------------------------------------------------------------------------
// Targeted invalidation -- TBIS / TBISI / TBISD path.
//
// Scans every slot rather than stopping at the first hit: GH expansion
// COULD produce overlapping mappings in theory, and clearing all of them
// is cheaper than reasoning about uniqueness here.  Realm is checked
// inline because TlbEntry::matches() does not filter on realm (it can't
// -- realm is in the tag, not the lookup args).
// ---------------------------------------------------------------------------
template <std::size_t Ways>
bool SPAMBucket<Ways>::invalidateMatching(uint64_t         lookupVpn,
                                          coreLib::ASNType lookupAsn,
                                          TlbRealm         realm) noexcept
{
    bool cleared = false;
    for (std::size_t i = 0; i < Ways; ++i) {
        TlbEntry& slot = m_slots[i];
        if (!slot.valid)              continue;
        if (slot.tag.realm != realm)  continue;
        if (!slot.matches(lookupVpn, lookupAsn)) continue;

        slot.invalidate();
        cleared = true;
        // Keep scanning -- see comment above.
    }
    return cleared;
}


// ---------------------------------------------------------------------------
// clear -- cold boot / snapshot restore.
//
// Drops every slot regardless of validity and resets the replacement
// cursor.  The manager's epoch counters are reset separately by the
// caller (manager.clear() does both).
// ---------------------------------------------------------------------------
template <std::size_t Ways>
void SPAMBucket<Ways>::clear() noexcept
{
    for (std::size_t i = 0; i < Ways; ++i) {
        m_slots[i].invalidate();
    }
    m_replaceCursor = 0;
}


// ---------------------------------------------------------------------------
// slotAt -- test/diagnostic snapshot of one slot.
// ---------------------------------------------------------------------------
template <std::size_t Ways>
TlbEntry SPAMBucket<Ways>::slotAt(std::size_t way) const noexcept
{
    return m_slots[way];
}


// ---------------------------------------------------------------------------
// any -- cheap "is the bucket non-empty?" for tests and trace formatters.
// ---------------------------------------------------------------------------
template <std::size_t Ways>
bool SPAMBucket<Ways>::any() const noexcept
{
    for (std::size_t i = 0; i < Ways; ++i) {
        if (m_slots[i].valid) return true;
    }
    return false;
}


// ===========================================================================
// Explicit template instantiations.  Required because the bodies above
// live in this TU; downstream code linking against SPAMBucket<N> for any
// N not listed here will get an LNK2019 (deliberate -- forces an
// intentional sizing decision).
//
// Add a new line for any additional shape exercised by tests or by a
// CpuState-resident manager.
// ===========================================================================
template class SPAMBucket<8>;     // production default (manager Ways=8)
template class SPAMBucket<4>;     // tests prefer the smaller shape


} // namespace pteLib
