// ============================================================================
// pteLib/TlbEntry.h -- TLB tag + payload record for ITB / DTB caches
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
// A TlbEntry pairs an architectural AlphaPte (from pteLib/AlphaPte.h) with
// the tag fields the translator needs to *look up* that PTE:
//
//   - vpn    : virtual page number  (VA >> kAlphaPageShift)
//   - asn    : address space number (ASNType from coreLib/VA_types.h)
//   - realm  : which TB this entry belongs to (ITB vs DTB)
//   - gh     : granularity hint     (block size for super-page matching)
//   - epoch  : monotonic stamp used for lazy invalidation (epoch sweep)
//   - pte    : the canonical AlphaPte payload
//
// The SPAM (Set-Partitioned Associative Memory) manager port from V1 stores
// these records in a fixed number of buckets; the matches() helper below is
// the inner-loop predicate.  All fields are POD and the record is trivially
// copyable so a bucket can be cleared by zeroing.
//
// EV6 page geometry:
//   - Base page is 8 KiB  -> kAlphaPageShift = 13
//   - Granularity Hint extends a block to 8**GH pages:
//       GH=0 -> 8 KiB, GH=1 -> 64 KiB, GH=2 -> 512 KiB, GH=3 -> 4 MiB.
//     A match on a GH>0 entry ignores the low (3*GH) bits of the VPN.
//   - ASM=1 mappings match any ASN (global mapping).
//
// Reference: HRM 4.1.5 "Translation Buffer" and HRM 4.1.4 "Granularity Hint".
//
// ============================================================================

#ifndef PTELIB_TLB_ENTRY_H
#define PTELIB_TLB_ENTRY_H

#include <cstdint>

#include "coreLib/VA_types.h"   // ASNType
#include "pteLib/AlphaPte.h"
#include "pteLib/TlbEpoch.h"    // TlbEpochSnapshot, EpochValue, isEpochLive

namespace pteLib {


// ---------------------------------------------------------------------------
// EV6 architectural page geometry.  Centralised here because the TLB key
// derivation (vpn = va >> shift) and the GH adjustment both need it.  When
// the codebase grows a coreLib/PageGeometry.h, move these constants there
// and have this header forward to it.
// ---------------------------------------------------------------------------
constexpr unsigned kAlphaPageShift = 13;                    // 8 KiB base page
constexpr uint64_t kAlphaPageSize  = uint64_t{1} << kAlphaPageShift;
constexpr uint64_t kAlphaPageMask  = kAlphaPageSize - uint64_t{1};


// ---------------------------------------------------------------------------
// Which TB the entry belongs to.  EV6 keeps ITB and DTB physically separate
// so an I-side miss must not look at D-side entries and vice versa.  The
// realm is part of the lookup key.
// ---------------------------------------------------------------------------
enum class TlbRealm : uint8_t {
    Itb = 0,    // Instruction TB (HRM 5.2.x)
    Dtb = 1     // Data TB        (HRM 5.3.x)
};


// ---------------------------------------------------------------------------
// Compute the VPN-mask that applies after Granularity Hint expansion.
// GH=0 leaves vpn untouched; GH=N drops the low 3*N bits.  Used by both
// insertion (key normalisation) and lookup (match predicate).
// ---------------------------------------------------------------------------
constexpr uint64_t vpnMaskForGh(uint8_t gh) noexcept
{
    uint64_t const drop = uint64_t{3} * static_cast<uint64_t>(gh & 0x3);
    return ~((uint64_t{1} << drop) - uint64_t{1});
}


// ---------------------------------------------------------------------------
// VA -> VPN.  Mechanically shifts the byte address right by the page-shift.
// ---------------------------------------------------------------------------
constexpr uint64_t vaToVpn(uint64_t va) noexcept
{
    return va >> kAlphaPageShift;
}


// ---------------------------------------------------------------------------
// TlbTag -- the lookup key.
//
// The tag is *not* an architectural register; it is the V4 emulator's
// internal representation of "which line in the TB does this lookup hit?"
// Layout is chosen for cheap equality comparison and predictable hashing.
// ---------------------------------------------------------------------------
struct TlbTag {
    uint64_t         vpn   = 0;       // virtual page number, normalised by GH
    coreLib::ASNType asn   = 0;       // process ASN (ignored when entry.asmGlobal=true)
    TlbRealm         realm = TlbRealm::Itb;
    uint8_t          gh    = 0;       // 0..3 (block size encoded as 8**gh base pages)

    constexpr TlbTag() noexcept = default;

    constexpr TlbTag(uint64_t         vpn_,
                     coreLib::ASNType asn_,
                     TlbRealm         realm_,
                     uint8_t          gh_ = 0) noexcept
        : vpn(vpn_ & vpnMaskForGh(gh_)),
          asn(asn_),
          realm(realm_),
          gh(gh_)
    {}

    friend constexpr bool operator==(TlbTag a, TlbTag b) noexcept
    {
        return a.vpn   == b.vpn
            && a.asn   == b.asn
            && a.realm == b.realm
            && a.gh    == b.gh;
    }
    friend constexpr bool operator!=(TlbTag a, TlbTag b) noexcept
    {
        return !(a == b);
    }
};


// ---------------------------------------------------------------------------
// TlbEntry -- one populated line of the TB.
//
// Holds the lookup tag, the AlphaPte payload, and bookkeeping the V4 SPAM
// manager will need (epoch, asmGlobal cache, valid flag).
// ---------------------------------------------------------------------------
struct TlbEntry {
    TlbTag           tag;
    AlphaPte         pte;
    TlbEpochSnapshot epochs;        // global + process epoch captured at insert
    bool             valid     = false; // false = empty slot, ignore the rest
    bool             asmGlobal = false; // cached copy of pte.bitASM()

    constexpr TlbEntry() noexcept = default;

    constexpr TlbEntry(TlbTag           tag_,
                       AlphaPte         pte_,
                       TlbEpochSnapshot epochs_) noexcept
        : tag(tag_),
          pte(pte_),
          epochs(epochs_),
          valid(true),
          asmGlobal(pte_.bitASM())
    {}

    // Cheap "is this slot eligible" predicate.  The caller checks realm
    // separately because a single bucket only ever holds one realm; this
    // routine is for inside-the-bucket scans.  Epoch liveness is a
    // *separate* check (isLiveUnder below) so a hot lookup path can
    // short-circuit on the cheap VPN/ASN test before touching the epoch
    // pair.
    constexpr bool matches(uint64_t lookupVpn, coreLib::ASNType lookupAsn) const noexcept
    {
        if (!valid) return false;
        if ((lookupVpn & vpnMaskForGh(gh())) != tag.vpn) return false;
        if (asmGlobal) return true;                   // ASM=1: ignore ASN
        return tag.asn == lookupAsn;
    }

    // Lazy-invalidation predicate.  Delegates to isEpochLive() in
    // TlbEpoch.h so the rule lives in one place.  Caller passes a
    // *snapshot* of the manager's two counters (taken once at the top
    // of the lookup path) rather than passing the live counters in,
    // which would re-read atomics inside the inner loop.
    constexpr bool isLiveUnder(EpochValue liveGlobalEpoch,
                               EpochValue liveProcessEpoch) const noexcept
    {
        return isEpochLive(epochs, liveGlobalEpoch, liveProcessEpoch, asmGlobal);
    }

    constexpr uint8_t  gh()        const noexcept { return tag.gh;    }
    constexpr TlbRealm realm()     const noexcept { return tag.realm; }
    constexpr uint64_t pfn()       const noexcept { return pte.pfn(); }
    constexpr uint64_t physAddrOf(uint64_t va) const noexcept
    {
        // Re-add the page offset; GH expansions keep the same low-bit
        // semantics because PFN already addresses the *base* of the block.
        return (pte.pfn() << kAlphaPageShift) | (va & kAlphaPageMask);
    }

    // Targeted invalidation: TBIS / TBISI / TBISD locate this entry by
    // VA + ASN and clear it in place.  The epoch sweep cannot handle
    // single-VPN flushes so this path is necessary for correctness.
    constexpr void invalidate() noexcept
    {
        valid     = false;
        asmGlobal = false;
        tag       = TlbTag{};
        pte       = AlphaPte{};
        epochs    = TlbEpochSnapshot{};
    }
};


static_assert(sizeof(TlbEntry) <= 64,
              "TlbEntry should fit in one cache line for SPAM bucket density");


// ---------------------------------------------------------------------------
// Free-function key hash -- cheap mix suitable for set-associative
// dispatch.  Not cryptographic; only needs to spread VPN bits across the
// bucket index.  Kept in this header so unit tests can pin the function.
// ---------------------------------------------------------------------------
constexpr uint64_t tlbKeyHash(TlbTag tag) noexcept
{
    uint64_t h = tag.vpn;
    h ^= (h >> 21);
    h *= uint64_t{0x9E3779B97F4A7C15};   // golden-ratio mix constant
    h ^= static_cast<uint64_t>(tag.asn) << 1;
    h ^= static_cast<uint64_t>(tag.realm) << 17;
    h ^= static_cast<uint64_t>(tag.gh) << 19;
    return h;
}


} // namespace pteLib

#endif // PTELIB_TLB_ENTRY_H
