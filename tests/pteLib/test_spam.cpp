// ============================================================================
// tests/pteLib/test_spam.cpp -- doctest coverage for the SPAM TLB stack
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
// Behavioural pins for pteLib:
//
//   AlphaPte    -- bit accessors, mode-aware permission checks, PFN
//   TlbEpoch    -- isEpochLive() truth table (TBIA, TBIAP, ASM survives)
//   TlbEntry    -- matches() + isLiveUnder() composition
//   SPAMBucket  -- insert / lookup / invalidateMatching / replace cursor
//   SPAMShard   -- end-to-end TBIA, TBIAP-with-ASM-survival, TBIS
//
// Uses CHECK only (REQUIRE would fail compile -- exceptions disabled in V4).
//
// ============================================================================

#include "doctest.h"

#include "coreLib/VA_types.h"
#include "pteLib/AlphaPte.h"
#include "pteLib/SPAMBucket.h"
#include "pteLib/SPAMShardManager.h"
#include "pteLib/TlbEntry.h"
#include "pteLib/TlbEpoch.h"

#include <cstdint>

using coreLib::ASNType;
using coreLib::Mode_Privilege;
using pteLib::AlphaPte;
using pteLib::EpochValue;
using pteLib::SPAMBucket;
using pteLib::SPAMShardManager;
using pteLib::TlbEntry;
using pteLib::TlbEpochSnapshot;
using pteLib::TlbRealm;
using pteLib::TlbTag;
using pteLib::isEpochLive;


TEST_CASE("AlphaPte::makeValid composes the standard kernel-RX mapping")
{
    AlphaPte const p = AlphaPte::makeValid(/*pfn=*/0x4000,
                                           /*kre=*/true,
                                           /*kwe=*/false,
                                           /*ure=*/false,
                                           /*uwe=*/false,
                                           /*asm=*/true);
    CHECK(p.bitV());
    CHECK(p.bitASM());
    CHECK(p.bitKRE());
    CHECK_FALSE(p.bitKWE());
    CHECK_FALSE(p.bitURE());
    CHECK(p.pfn() == 0x4000);
}


TEST_CASE("AlphaPte::canRead respects fault-on-read regardless of mode bits")
{
    AlphaPte p;
    p.insert<pteLib::AlphaPteBits::kKRE, 1>(1);
    p.insert<pteLib::AlphaPteBits::kFOR, 1>(1);
    CHECK_FALSE(p.canRead(Mode_Privilege::Kernel));  // FOR vetoes KRE
}


TEST_CASE("AlphaPte::canWrite is per-mode")
{
    AlphaPte p;
    p.insert<pteLib::AlphaPteBits::kKWE, 1>(1);
    CHECK      (p.canWrite(Mode_Privilege::Kernel));
    CHECK_FALSE(p.canWrite(Mode_Privilege::User));
}


TEST_CASE("isEpochLive enforces the TBIA + TBIAP truth table")
{
    TlbEpochSnapshot const snap{/*global=*/3, /*process=*/7};

    SUBCASE("matching epochs -> live (non-ASM)") {
        CHECK(isEpochLive(snap, /*g=*/3, /*p=*/7, /*asm=*/false));
    }
    SUBCASE("global mismatch -> dead, even for ASM=1") {
        CHECK_FALSE(isEpochLive(snap, /*g=*/4, /*p=*/7, /*asm=*/true));
    }
    SUBCASE("process mismatch + ASM=0 -> dead (TBIAP killed it)") {
        CHECK_FALSE(isEpochLive(snap, /*g=*/3, /*p=*/8, /*asm=*/false));
    }
    SUBCASE("process mismatch + ASM=1 -> live (survives TBIAP)") {
        CHECK(isEpochLive(snap, /*g=*/3, /*p=*/8, /*asm=*/true));
    }
}


TEST_CASE("TlbEntry::matches checks VPN, ASN, and respects ASM=1")
{
    AlphaPte const ptePrivate = AlphaPte::makeValid(0x100, true, false,
                                                    false, false,
                                                    /*asm=*/false);
    AlphaPte const pteGlobal  = AlphaPte::makeValid(0x100, true, false,
                                                    false, false,
                                                    /*asm=*/true);

    TlbEpochSnapshot const snap{0, 0};

    TlbEntry const priv {TlbTag{/*vpn=*/0xCAFE, /*asn=*/7, TlbRealm::Dtb},
                         ptePrivate, snap};
    TlbEntry const global{TlbTag{/*vpn=*/0xBEEF, /*asn=*/7, TlbRealm::Dtb},
                          pteGlobal, snap};

    SUBCASE("VPN miss -> false") {
        CHECK_FALSE(priv.matches(0xC0FE, 7));
    }
    SUBCASE("ASN miss + private entry -> false") {
        CHECK_FALSE(priv.matches(0xCAFE, 9));
    }
    SUBCASE("ASN miss + ASM=1 entry -> true (ignored)") {
        CHECK(global.matches(0xBEEF, 99));
    }
}


TEST_CASE("SPAMBucket<4> round-robin replacement + targeted invalidate")
{
    SPAMBucket<4> bucket;

    auto makeEntry = [](uint64_t vpn, ASNType asn) {
        TlbTag tag{vpn, asn, TlbRealm::Dtb};
        AlphaPte pte = AlphaPte::makeValid(/*pfn=*/vpn);
        return TlbEntry{tag, pte, TlbEpochSnapshot{0, 0}};
    };

    bucket.insert(makeEntry(0x100, 1));
    bucket.insert(makeEntry(0x200, 1));
    bucket.insert(makeEntry(0x300, 1));
    bucket.insert(makeEntry(0x400, 1));
    CHECK(bucket.any());

    auto out = bucket.lookup(0x200, 1, /*g=*/0, /*p=*/0);
    CHECK(out.hit());
    CHECK(out.pte.pfn() == 0x200);

    SUBCASE("invalidateMatching clears just the targeted slot") {
        CHECK(bucket.invalidateMatching(0x200, 1, TlbRealm::Dtb));
        auto miss = bucket.lookup(0x200, 1, 0, 0);
        CHECK_FALSE(miss.hit());

        // Other slot still hits.
        auto still = bucket.lookup(0x300, 1, 0, 0);
        CHECK(still.hit());
    }

    SUBCASE("eviction starts at cursor when bucket is full") {
        // Inserting a fifth entry evicts the cursor's slot.  The cursor
        // started at 0 and advanced once per insert (4 -> wrap to 0),
        // so the next insert evicts slot 0 (originally vpn=0x100).
        bucket.insert(makeEntry(0x500, 1));
        auto evicted = bucket.lookup(0x100, 1, 0, 0);
        CHECK_FALSE(evicted.hit());
        auto fresh = bucket.lookup(0x500, 1, 0, 0);
        CHECK(fresh.hit());
    }
}


TEST_CASE("SPAMShardManager TBIA flushes every entry in O(1)")
{
    SPAMShardManager<8, 4> mgr;

    mgr.insert(TlbRealm::Dtb, /*va=*/0x10000, /*asn=*/3,
               AlphaPte::makeValid(0x100));
    mgr.insert(TlbRealm::Dtb, /*va=*/0x20000, /*asn=*/3,
               AlphaPte::makeValid(0x200));

    CHECK(mgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    CHECK(mgr.lookup(TlbRealm::Dtb, 0x20000, 3).isHit());

    EpochValue const before = mgr.globalEpoch();
    mgr.invalidateAll();
    CHECK(mgr.globalEpoch() == before + 1);

    CHECK_FALSE(mgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    CHECK_FALSE(mgr.lookup(TlbRealm::Dtb, 0x20000, 3).isHit());
}


TEST_CASE("SPAMShardManager TBIAP preserves ASM=1 entries")
{
    SPAMShardManager<8, 4> mgr;

    AlphaPte const privateMapping =
        AlphaPte::makeValid(0x100, true, false, false, false, /*asm=*/false);
    AlphaPte const globalMapping =
        AlphaPte::makeValid(0x200, true, false, false, false, /*asm=*/true);

    mgr.insert(TlbRealm::Dtb, /*va=*/0x10000, /*asn=*/3, privateMapping);
    mgr.insert(TlbRealm::Dtb, /*va=*/0x20000, /*asn=*/3, globalMapping);

    CHECK(mgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    CHECK(mgr.lookup(TlbRealm::Dtb, 0x20000, 3).isHit());

    mgr.invalidateAllProcess();

    // Private mapping is gone.
    CHECK_FALSE(mgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    // Global mapping survives TBIAP (HRM 4.1.7).
    CHECK(mgr.lookup(TlbRealm::Dtb, 0x20000, 3).isHit());
}


TEST_CASE("SPAMShardManager invalidateSingle is targeted")
{
    SPAMShardManager<8, 4> mgr;

    mgr.insert(TlbRealm::Dtb, /*va=*/0x10000, /*asn=*/3,
               AlphaPte::makeValid(0x100));
    mgr.insert(TlbRealm::Dtb, /*va=*/0x20000, /*asn=*/3,
               AlphaPte::makeValid(0x200));

    CHECK(mgr.invalidateSingle(TlbRealm::Dtb, 0x10000, 3));

    CHECK_FALSE(mgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    // The unrelated mapping is undisturbed.
    CHECK(mgr.lookup(TlbRealm::Dtb, 0x20000, 3).isHit());
}


TEST_CASE("SPAMShardManager clear resets epochs and drops slots")
{
    SPAMShardManager<8, 4> mgr;

    mgr.insert(TlbRealm::Dtb, /*va=*/0x10000, /*asn=*/3,
               AlphaPte::makeValid(0x100));
    mgr.invalidateAll();  // advance globalEpoch off zero
    CHECK(mgr.globalEpoch() != 0);

    mgr.clear();
    CHECK(mgr.globalEpoch()  == 0);
    CHECK(mgr.processEpoch() == 0);
    CHECK(mgr.occupancy()    == 0);
}
