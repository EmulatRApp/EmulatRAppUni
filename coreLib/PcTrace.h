// ============================================================================
// coreLib/PcTrace.h -- EMULATR_PCTRACE forward retire-trace, armed at the
// console->VMB (primary bootstrap) handoff.  When the SRM console issues
// CSERVE START (cfw_start -> sys__exit_console) to leave console mode and
// enter the bootstrap at VA 0x20000000, this facility snapshots the arm-time
// processor context (PTBR/VPTB/p_misc) and records the next N retired PCs.
// It latches the first PC that drops back below the console top (0x0020_0000)
// -- the "bail" = the instruction where control is misdirected back into the
// console instead of executing boot0 -- and dumps the trajectory + bail
// context.  Purpose: distinguish "exit_console bails before any OS
// translation" from "boot0 translated under the wrong (console) PTBR instead
// of the boot table pfn 0x1ff82 (@ PA 0x3ff04000)".  See JRN-VMB-016.
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
// ============================================================================
//
// Header-only (C++17 inline globals): no .cpp, no CMake edit.  Compiled into
// any non-release config like the sibling EMULATR_DIAG_* facilities; inert
// (one bool load per retire) unless EMULATR_PCTRACE is set in the environment.
// Decoupled from CpuState (takes scalars) so it adds no include coupling.
//
//   EMULATR_PCTRACE      -- enable (presence only).
//   EMULATR_PCTRACE_N    -- forward instruction budget (default 4096, cap 16384).
//
#ifndef CORELIB_PCTRACE_H
#define CORELIB_PCTRACE_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace coreLib {

struct PcTraceEntry {
    uint64_t cyc;
    uint64_t pc;    // aligned
    uint32_t enc;
    uint8_t  pal;
    uint8_t  fault;
};

// VA below this = SRM console / PAL region (physical 1-1 during boot); a PC
// dropping here after the handoff arm is the console re-entry (misdirection).
inline constexpr uint64_t kPcTraceConsoleTop = 0x00200000ULL;
inline constexpr uint64_t kPcTraceCap        = 16384;
// pfn the boot PTBR should hold: the console-built page table at PA 0x3ff04000
// (0x1ff82 << 13 == 0x3ff04000 == the "initializing page table at 3ff04000"
// banner).  Arm-time ptbr != this => VA 0x20000000 would translate wrong.
inline constexpr uint64_t kPcTraceBootPtbrPfn = 0x1ff82ULL;

inline bool     g_pctraceEnabled =
    (std::getenv("EMULATR_PCTRACE") != nullptr);
inline uint64_t g_pctraceN = [] {
    char const* e = std::getenv("EMULATR_PCTRACE_N");
    uint64_t n = e ? std::strtoull(e, nullptr, 0) : uint64_t{4096};
    return n > kPcTraceCap ? kPcTraceCap : (n == 0 ? uint64_t{4096} : n);
}();
inline bool         g_pctraceArmed  = false;
inline bool         g_pctraceDumped = false;
inline uint64_t     g_pctraceArmPc  = 0;
inline uint64_t     g_pctraceIdx    = 0;
inline uint64_t     g_pctraceDumpAt = kPcTraceCap;   // dump when idx reaches this
inline uint64_t     g_pctraceBailPc = 0;             // first PC < console top
inline uint64_t     g_pctraceBailIdx = 0;
// arm-time snapshot (tests the boot-PTBR / 0x1ff82 hypothesis)
inline uint64_t     g_pctraceArmPtbr = 0, g_pctraceArmVptb = 0, g_pctraceArmPmisc = 0;
inline int          g_pctraceArmPal  = 0;
inline uint64_t     g_pctraceBailPtbr = 0;
inline PcTraceEntry g_pctraceBuf[kPcTraceCap];

inline void pctraceArm(uint64_t pc, uint64_t ptbr, uint64_t vptb,
                       uint64_t pmisc, int pal, uint64_t cyc) noexcept
{
    if (!g_pctraceEnabled || g_pctraceArmed || g_pctraceDumped) return;
    g_pctraceArmed   = true;
    g_pctraceArmPc   = pc & ~uint64_t{3};
    g_pctraceIdx     = 0;
    g_pctraceDumpAt  = g_pctraceN;
    g_pctraceArmPtbr = ptbr;
    g_pctraceArmVptb = vptb;
    g_pctraceArmPmisc = pmisc;
    g_pctraceArmPal  = pal;
    std::fprintf(stderr,
        "PCTRACE-ARM pc=0x%llx N=%llu ptbr=0x%llx (want boot pfn 0x1ff82) "
        "vptb=0x%llx pmisc=0x%llx pmisc<63>=%d pal=%d cyc=%llu\n",
        static_cast<unsigned long long>(g_pctraceArmPc),
        static_cast<unsigned long long>(g_pctraceN),
        static_cast<unsigned long long>(ptbr),
        static_cast<unsigned long long>(vptb),
        static_cast<unsigned long long>(pmisc),
        static_cast<int>((pmisc >> 63) & 1), pal,
        static_cast<unsigned long long>(cyc));
    std::fflush(stderr);
}

inline void pctraceDumpTrajectory() noexcept
{
    if (!g_pctraceEnabled || g_pctraceDumped) return;
    g_pctraceDumped = true;
    g_pctraceArmed  = false;
    std::fprintf(stderr,
        "PCTRACE-DUMP armPc=0x%llx captured=%llu bailPc=0x%llx bailIdx=%llu "
        "armPtbr=0x%llx bailPtbr=0x%llx (bootPtbrPfn 0x1ff82)\n",
        static_cast<unsigned long long>(g_pctraceArmPc),
        static_cast<unsigned long long>(g_pctraceIdx),
        static_cast<unsigned long long>(g_pctraceBailPc),
        static_cast<unsigned long long>(g_pctraceBailIdx),
        static_cast<unsigned long long>(g_pctraceArmPtbr),
        static_cast<unsigned long long>(g_pctraceBailPtbr));
    // Collapse consecutive identical aligned PCs into runs (loops compress).
    uint64_t const n = g_pctraceIdx < kPcTraceCap ? g_pctraceIdx : kPcTraceCap;
    uint64_t i = 0;
    while (i < n) {
        uint64_t j = i + 1;
        while (j < n && g_pctraceBuf[j].pc == g_pctraceBuf[i].pc) ++j;
        bool const isBail = (g_pctraceBailPc != 0 &&
                             g_pctraceBuf[i].pc == g_pctraceBailPc &&
                             i <= g_pctraceBailIdx && g_pctraceBailIdx < j);
        std::fprintf(stderr,
            "  PCTRACE[%5llu] pc=0x%08llx x%-4llu enc=0x%08x pal=%d fault=%d%s\n",
            static_cast<unsigned long long>(i),
            static_cast<unsigned long long>(g_pctraceBuf[i].pc),
            static_cast<unsigned long long>(j - i),
            static_cast<unsigned>(g_pctraceBuf[i].enc),
            static_cast<int>(g_pctraceBuf[i].pal),
            static_cast<int>(g_pctraceBuf[i].fault),
            isBail ? "   <== BAIL (console re-entry)" : "");
        i = j;
    }
    std::fflush(stderr);
}

inline void pctraceRecord(uint64_t cyc, uint64_t pc, uint32_t enc, int pal,
                          int fault, uint64_t ptbr) noexcept
{
    if (!g_pctraceArmed) return;
    uint64_t const apc = pc & ~uint64_t{3};
    if (g_pctraceIdx < kPcTraceCap) {
        g_pctraceBuf[g_pctraceIdx] = PcTraceEntry{
            cyc, apc, enc, static_cast<uint8_t>(pal),
            static_cast<uint8_t>(fault) };
    }
    // Latch the first console re-entry after the arm (skip idx 0 = the arm PC).
    if (g_pctraceBailPc == 0 && g_pctraceIdx > 0 && apc < kPcTraceConsoleTop) {
        g_pctraceBailPc  = apc;
        g_pctraceBailIdx = g_pctraceIdx;
        g_pctraceBailPtbr = ptbr;
        // Dump a focused tail past the bail rather than waiting for the full N.
        uint64_t const at = g_pctraceIdx + 128;
        if (at < g_pctraceDumpAt) g_pctraceDumpAt = at;
        std::fprintf(stderr,
            "PCTRACE-BAIL pc=0x%llx ptbr=0x%llx (arm 0x%llx) cyc=%llu idx=%llu\n",
            static_cast<unsigned long long>(apc),
            static_cast<unsigned long long>(ptbr),
            static_cast<unsigned long long>(g_pctraceArmPtbr),
            static_cast<unsigned long long>(cyc),
            static_cast<unsigned long long>(g_pctraceIdx));
        std::fflush(stderr);
    }
    ++g_pctraceIdx;
    if (g_pctraceIdx >= g_pctraceDumpAt || g_pctraceIdx >= g_pctraceN)
        pctraceDumpTrajectory();
}

}  // namespace coreLib

#endif  // CORELIB_PCTRACE_H
