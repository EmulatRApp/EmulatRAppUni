// ============================================================================
// pteLib/AsnCensus.h -- ASN allocation + ASN-attributable TB miss profiler
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// WHY THIS EXISTS (2026-07-28, JRN-AUD-001 follow-up)
//
//   The post-audit DS20 rerun showed pre-banner TB double-misses rise 41%
//   (11,268 -> 15,864) while reaching the OpenVMS banner 144M cycles
//   EARLIER.  Two explanations were live and the logs could not separate
//   them:
//     (a) ASN churn -- a TB entry is keyed by VPN + ASN (the PFN is the
//         PAYLOAD, not part of the key), so every newly allocated ASN
//         makes the entire non-ASM working set unreachable and forces a
//         wave of compulsory refills.  Architectural, not a defect.
//     (b) a behavioural change from the PCTX/DTB_ASN corrections making
//         the PAL take different paths.
//
//   Distinguishing them needs one number nothing in the tree measures:
//   HOW MANY MISSES ARE ATTRIBUTABLE TO THE ASN TAG rather than to the
//   VPN simply never having been resident.  That is counter M3 below.
//
// WHAT IT MEASURES
//
//   A1  distinct ASN values ever installed, each with its first-use cycle
//       (via SWPCTX context load and HW_MTPR DTB_ASN0/1)
//   A2  TB fills per ASN, per realm
//   M1  COLD miss        -- this VPN has never been filled under ANY ASN
//   M2  ASN-DIFFERENT    -- this VPN has been filled before, but only
//                           under some OTHER ASN.  <-- the hypothesis
//   M3  SAME-ASN REFILL  -- this VPN was filled under THIS ASN already,
//                           so the miss is capacity or invalidation, and
//                           the ASN tag is NOT responsible.
//
// HONEST LIMITATION -- READ BEFORE QUOTING THE NUMBERS
//
//   The classifier is HISTORY-based, not residency-based: it records what
//   has ever been filled, and cannot observe capacity eviction inside the
//   shard array.  So M2 means "this VPN was previously filled under a
//   different ASN and never under this one" -- it is an UPPER BOUND on
//   ASN-attributable misses, because such an entry may also have been
//   evicted in the meantime.  M3 is likewise a lower bound on capacity
//   pressure.  The bound is still decisive for the question at hand: if
//   M2 is ~0 then ASN churn explains nothing, whatever the eviction rate.
//
// COST
//
//   Compiled out entirely unless EMULATR_BRINGUP_PROBES.  When compiled
//   in but not armed (env EMULATR_ASN_CENSUS unset) every entry point is
//   one relaxed bool load and a predicted-not-taken branch; no map is
//   allocated.  When armed it is a hash-map insert per fill and per miss
//   -- FAR too slow for a timing run, and that is intentional: this is a
//   census instrument, not a monitor.
// ============================================================================

#ifndef PTELIB_ASN_CENSUS_H
#define PTELIB_ASN_CENSUS_H

#include "coreLib/VA_types.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>

namespace pteLib {

// Realm is carried as a small int so this header does not depend on the
// TlbRealm enum's definition order.
enum class CensusRealm : uint8_t { Itb = 0, Dtb = 1 };

class AsnCensus {
public:
    // Armed once from the environment at first use.  A function-local
    // static keeps the check to one relaxed load on the hot path.
    static bool armed() noexcept
    {
        static bool const s_armed =
            (std::getenv("EMULATR_ASN_CENSUS") != nullptr);
        return s_armed;
    }

    // A1: an ASN was installed into the translation context.
    static void recordInstall(coreLib::ASNType asn, uint64_t cycle) noexcept
    {
        if (!armed()) return;
        auto& first = instance().m_firstUseCycle;
        if (first.find(asn) == first.end()) {
            first.emplace(asn, cycle);
        }
    }

    // A2: a TB entry was filled for (realm, vpn) under `asn`.
    static void recordFill(CensusRealm realm, uint64_t vpn,
                           coreLib::ASNType asn) noexcept
    {
        if (!armed()) return;
        AsnCensus& self = instance();
        ++self.m_fillsPerAsn[asn];
        self.m_history[key(realm, vpn)] |= asnBit(asn);
    }

    // M1/M2/M3: classify a lookup miss.  Call ONLY on the miss path.
    static void classifyMiss(CensusRealm realm, uint64_t vpn,
                             coreLib::ASNType asn) noexcept
    {
        if (!armed()) return;
        AsnCensus& self = instance();
        auto const it = self.m_history.find(key(realm, vpn));
        if (it == self.m_history.end()) {
            ++self.m_missCold;                  // never filled under any ASN
        } else if ((it->second & asnBit(asn)) != 0) {
            ++self.m_missSameAsn;               // capacity / invalidation
        } else {
            ++self.m_missAsnDifferent;          // THE hypothesis
        }
    }

    // Emit the census.  Safe to call when unarmed (prints nothing).
    static void report() noexcept
    {
        if (!armed()) return;
        AsnCensus const& self = instance();
        std::fprintf(stderr,
            "\n==== ASN CENSUS ====================================="
            "=======================\n");
        std::fprintf(stderr, "distinct ASNs installed : %zu\n",
                     self.m_firstUseCycle.size());
        for (auto const& kv : self.m_firstUseCycle) {
            auto const f = self.m_fillsPerAsn.find(kv.first);
            std::fprintf(stderr,
                "  ASN 0x%02x  first use cyc=%llu  fills=%llu\n",
                static_cast<unsigned>(kv.first),
                static_cast<unsigned long long>(kv.second),
                static_cast<unsigned long long>(
                    f == self.m_fillsPerAsn.end() ? 0ull : f->second));
        }
        unsigned long long const total =
            self.m_missCold + self.m_missAsnDifferent + self.m_missSameAsn;
        std::fprintf(stderr,
            "miss classification (%llu classified):\n", total);
        std::fprintf(stderr,
            "  M1 cold          (VPN never filled under any ASN) : %llu\n",
            static_cast<unsigned long long>(self.m_missCold));
        std::fprintf(stderr,
            "  M2 asn-different (filled before, other ASN only)  : %llu"
            "   <-- ASN-attributable (upper bound)\n",
            static_cast<unsigned long long>(self.m_missAsnDifferent));
        std::fprintf(stderr,
            "  M3 same-asn      (capacity / invalidation)        : %llu\n",
            static_cast<unsigned long long>(self.m_missSameAsn));
        std::fprintf(stderr, "distinct (realm,VPN) tracked : %zu\n",
                     self.m_history.size());
        std::fprintf(stderr,
            "NOTE: history-based -- M2 is an UPPER BOUND (cannot observe "
            "capacity eviction).\n");
        std::fprintf(stderr,
            "======================================================"
            "======================\n");
        std::fflush(stderr);
    }

private:
    AsnCensus() = default;

    static AsnCensus& instance() noexcept
    {
        static AsnCensus s_it;
        return s_it;
    }

    // Pack realm into the key's high bit; VPNs never reach bit 63.
    static constexpr uint64_t key(CensusRealm realm, uint64_t vpn) noexcept
    {
        return vpn | (realm == CensusRealm::Dtb ? (uint64_t{1} << 63) : 0ull);
    }

    // ASN is 8 bits architecturally (PCTX ASN<7:0>), so the set of ASNs a
    // VPN has been filled under fits in a 256-bit mask.  We only need
    // "has this VPN been seen under THIS asn", so a 64-bit mask over
    // asn % 64 plus the exact-set map would be needed for full fidelity;
    // in practice ASNs observed in a boot are few and small, so a direct
    // 256-bit mask (4 x uint64) is both exact and cheap.  Kept simple:
    // one uint64 per 64-ASN block, indexed by asn >> 6.
    struct AsnMask {
        uint64_t w[4] = { 0, 0, 0, 0 };
        AsnMask& operator|=(AsnMask const& o) noexcept
        {
            for (int i = 0; i < 4; ++i) { w[i] |= o.w[i]; }
            return *this;
        }
        constexpr uint64_t operator&(AsnMask const& o) const noexcept
        {
            return (w[0] & o.w[0]) | (w[1] & o.w[1])
                 | (w[2] & o.w[2]) | (w[3] & o.w[3]);
        }
    };

    static AsnMask asnBit(coreLib::ASNType asn) noexcept
    {
        AsnMask m;
        unsigned const a = static_cast<unsigned>(asn) & 0xFFu;
        m.w[a >> 6] = uint64_t{1} << (a & 63u);
        return m;
    }

    std::unordered_map<uint64_t, AsnMask>          m_history;
    std::map<coreLib::ASNType, uint64_t>           m_firstUseCycle;
    std::map<coreLib::ASNType, unsigned long long> m_fillsPerAsn;
    unsigned long long m_missCold          = 0;
    unsigned long long m_missAsnDifferent  = 0;
    unsigned long long m_missSameAsn       = 0;
};

}  // namespace pteLib

#endif  // PTELIB_ASN_CENSUS_H
