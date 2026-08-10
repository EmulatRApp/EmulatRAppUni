#pragma once
// ============================================================================
// coreLib/SupModeProbeRing.h -- P-SUP-3 CM-transition ring (JRN-SUPMODE-001
// Sec 13.7; REMOVE with the P-SUP probe family after readout).
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
// Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
// ASCII(128) only.  Hex radix.
//
// WHY: the Sec 13 retire trace exonerated the CHMS corridor -- the original
// Species-A ACCVIO predates every window we can afford, so the question is
// no longer "does the transition land" but "which transition left DCL at
// USER before the faulting write".  The stderr census cannot answer it (the
// 1024-row cap goes blind by cyc 2.9e9 -- Sec 13.4), so this keeps an
// UNCAPPED in-memory ring of the last 8 CM-write events, fed from the
// HW_CM / HW_IER_CM MTPR cases (PalEntries.cpp), and dumped by the ACV
// hook in MemDrainer.h ONLY at a Species-A-signature fault.  Zero stderr
// volume until the fault; ~6 stores per CM write when armed.
//
// Gate: EMULATR_PROBE_SUPMODE (same env as the P-SUP family; each TU holds
// its own static init of the gate, this header just stores).
// ============================================================================
#include <cstdint>
#include <cstdio>

namespace coreLib::supmode_probe {

struct CmEvent {
    uint64_t cyc;
    uint64_t pc;      // g.pc of the MTPR (raw, palmode bit intact)
    uint64_t opB;     // value written
    uint8_t  from;    // cpu.mode before
    uint8_t  to;      // cpu.mode after
    uint8_t  site;    // 0 = HW_CM, 1 = HW_IER_CM
};

inline CmEvent  g_ring[8] = {};
inline uint64_t g_ringN   = 0;   // total pushes; newest = g_ring[(g_ringN-1)&7]

// P-SUP-6 (JRN-SUPMODE-001 Sec 16.5b; REMOVE with the family): after a
// supervisor-involving CM write, the first NATIVE-mode retire is the
// REI landing -- for 1->2 that is the 411-cycle stub's ENTRY PC, for
// 2->3 the User resume PC.  push() arms this countdown; the per-retire
// consumer (DecListingSink::onCommit) prints one SUPMODE-LAND row and
// clears it.  Value = retires left to scan (the REI tail is <16).
inline int      g_landPending = 0;
inline uint8_t  g_landFrom    = 0;
inline uint8_t  g_landTo      = 0;

inline void push(uint64_t cyc, uint64_t pc, uint64_t opB,
                 uint8_t from, uint8_t to, uint8_t site) noexcept
{
    g_ring[g_ringN & 7] = CmEvent{ cyc, pc, opB, from, to, site };
    ++g_ringN;
    // P-SUP-4 (2026-08-10): every CM write INVOLVING Supervisor, on
    // stderr, effectively uncapped.  The P-SUP-3 ACV readout (runs
    // 124047 ACV#1/#2) shows the last 8 transitions before Species A
    // are pure 3<->0 ping-pong -- the discriminator is whether a ->2
    // write EVER happened for the process before its fault (never
    // entered vs entered-then-lost).  Mode-2 traffic is rare by
    // measurement, so this cannot flood; the 65536 guard is a
    // runaway backstop, not a census cap (the 1024-row cap is what
    // blinded Sec 10.1/11.1 -- do not repeat it).
    if (from == 2u || to == 2u) {
        g_landPending = 32;          // P-SUP-6 arm (Sec 16.5b)
        g_landFrom = from; g_landTo = to;
        static unsigned long s_sup2N = 0;
        if (s_sup2N < 65536u) {
            ++s_sup2N;
            std::fprintf(stderr,
                "SUPMODE-SUP2#%lu cyc=%llu pc=0x%llx %s opB=0x%llx "
                "mode %u->%u\n",
                s_sup2N,
                static_cast<unsigned long long>(cyc),
                static_cast<unsigned long long>(pc),
                site ? "ier_cm" : "cm",
                static_cast<unsigned long long>(opB),
                static_cast<unsigned>(from),
                static_cast<unsigned>(to));
            std::fflush(stderr);
        }
    }
}

} // namespace coreLib::supmode_probe
