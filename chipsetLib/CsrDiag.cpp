// ============================================================================
// CsrDiag.cpp -- chipset CSR access diagnostic sink (body)
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
// CHANGE HISTORY:
//   2026-05-14  Initial commit -- Phase A scaffolding.  Pairs with
//               CsrDiag.h.  Implementation guarded by
//               `EMULATR_CHIPSET_DIAG`; entire TU compiles to an empty
//               object when the macro is off, so no link-time cost in
//               Release builds.
//
// ============================================================================

#include "chipsetLib/CsrDiag.h"

#ifdef EMULATR_CHIPSET_DIAG

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace chipsetLib {

// ----------------------------------------------------------------------------
// Runtime enable storage.  Default-TRUE (MUTED): even when the macro is compiled
// in (Debug/RelWithDebInfo), the sink stays SILENT until EXPLICITLY enabled per
// run -- the diagnostics-default-off rule (2026-06-10): no diagnostic output
// unless the execution requests it.  initCsrDiagFromEnv() un-mutes on the opt-in
// env var; AppOptions (once wired) can also enable it from a CLI flag at startup.
// ----------------------------------------------------------------------------
bool g_csrDiagMuted = true;

void initCsrDiagFromEnv() noexcept
{
    // Opt-IN: set EMULATR_CHIPSET_DIAG_ON to any non-empty value other than "0"
    // or "false" to ENABLE CSR diagnostic output for this run; absent -> silent.
    // (Inverted 2026-06-10 from the old EMULATR_CHIPSET_DIAG_OFF opt-out, per the
    // diagnostics-default-off rule.)  Trivial parse -- a developer tuning knob.
    char const* env = std::getenv("EMULATR_CHIPSET_DIAG_ON");
    if (env == nullptr || env[0] == '\0') {
        return;
    }
    if (std::strcmp(env, "0") == 0 || std::strcmp(env, "false") == 0) {
        return;
    }
    g_csrDiagMuted = false;
}

// ----------------------------------------------------------------------------
// The sink itself.  One canonical format so downstream log parsing has a
// single line shape to match against.  Field order chosen for grep ease
// (chip and register name come first; cycle last for timeline sort with
// `sort -k7,7n`).  fprintf to stderr today; redirect to a dedicated log
// file is a single-line change here without touching call sites.
//
// Hot path discipline:
//   - Relaxed bool read of g_csrDiagMuted; no atomic ordering needed --
//     mute is a tuning toggle, not a correctness predicate.
//   - Single fprintf call, no concatenation, no temporary strings.
//   - No allocation on the emit path.
// ----------------------------------------------------------------------------
void csrLogAccess(char const* chipName,
                  char const* regName,
                  bool        isWrite,
                  uint64_t    rawValue,
                  uint64_t    offset,
                  int         cpuId,
                  uint64_t    cycleCount) noexcept
{
    if (g_csrDiagMuted) {
        return;
    }

    // Format:
    //   CSR <chip>:<reg> <R|W> off=0x... val=0x... cpu=<n> cyc=<n>
    //
    // Examples (Phase B+ output, illustrative):
    //   CSR Cchip:MISC R off=0x00000080 val=0x0000001000000000 cpu=0 cyc=178096580
    //   CSR Cchip:DRIR R off=0x00000300 val=0x0000000000000000 cpu=0 cyc=178096612
    //   CSR Pchip:PCTL W off=0x00000300 val=0x0000000000000048 cpu=0 cyc=4194420
    std::fprintf(stderr,
                 "CSR %s:%s %c off=0x%08llx val=0x%016llx cpu=%d cyc=%llu\n",
                 chipName,
                 regName,
                 isWrite ? 'W' : 'R',
                 static_cast<unsigned long long>(offset),
                 static_cast<unsigned long long>(rawValue),
                 cpuId,
                 static_cast<unsigned long long>(cycleCount));
}

} // namespace chipsetLib

#endif // EMULATR_CHIPSET_DIAG
