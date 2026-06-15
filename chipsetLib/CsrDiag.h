// ============================================================================
// CsrDiag.h -- compile-time-gated diagnostic sink for chipset CSR accesses
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
// PURPOSE:
//   Provides a single canonical sink for "CSR access" diagnostic events
//   in the chipset layer (TsunamiCchip, TsunamiDchip, TsunamiPchip).
//   Every read and write of a chipset CSR narrates itself uniformly --
//   chip name, register name, R/W direction, raw value, MMIO offset,
//   originating CPU id, and the pipeline cycle count -- so a future
//   firmware boot trace tells us exactly which registers were touched,
//   in what order, by which CPU, with what data.
//
// TWO-LEVEL GATING:
//
//   1.  COMPILE-TIME ON/OFF (zero overhead when off).
//       Controlled by the CMake-managed preprocessor define
//       `EMULATR_CHIPSET_DIAG`.  Default ON for `Debug` and
//       `RelWithDebInfo`; default OFF for `Release`.  Override with
//       `cmake -DEMULATR_CHIPSET_DIAG=ON|OFF`.
//
//       When OFF, the `CSR_LOG_R` / `CSR_LOG_W` macros expand to
//       `((void)0)`, so the call site evaluates none of its arguments
//       and the compiler discards the call entirely.  Verify with
//       `objdump -d` on a Release build of one chipset TU once the
//       Phase B refactor lands.
//
//   2.  RUNTIME ENABLE (live tunable when compiled-on; DEFAULT SILENT).
//       When the macro is on, the sink consults the global
//       `g_csrDiagMuted` bool (relaxed load) on every call; it defaults
//       MUTED, so the call returns immediately with no fprintf unless
//       explicitly enabled.  Un-muted at startup by `initCsrDiagFromEnv()`
//       when `EMULATR_CHIPSET_DIAG_ON=1` is in the environment, or
//       programmatically via `setCsrDiagMuted(false)` (e.g., from an
//       AppOptions CLI flag).  Opt-IN, per the diagnostics-default-off
//       rule (2026-06-10): no output unless the run requests it.
//
//   Two-level discipline: the macro is the on/off (zero cost in
//   release); the runtime mute is the volume (live tunable when on).
//   The same pattern can be reused for future diagnostic streams
//   (Pchip CSR sub-streams, MMU walker, etc.) by reusing this header.
//
// DESIGN NOTES:
//   The sink takes its string arguments as `char const*` so the
//   compiler can place them in `.rodata` -- the call site becomes a
//   single load + jmp when on, and is elided entirely when off.
//   Format strings are NOT exposed at the call site: the sink owns
//   the format, so call-site additions cannot accidentally change
//   the log format and break downstream parsers.
//
//   The sink writes to stderr today.  A future redirect to a
//   dedicated stream (e.g., `chipset_csr.log` in the trace dir) is
//   a single-file change in CsrDiag.cpp without touching call sites.
//
// USAGE (Phase B onward):
//
//   #include "chipsetLib/CsrDiag.h"
//
//   uint64_t TsunamiCchip::read(uint64_t offset, int cpuId) const {
//       switch (offset) {
//       case Cchip::MISC:
//           CSR_LOG_R("Cchip", "MISC", m_misc.load(...), offset,
//                     cpuId, m_cycleCount);
//           return m_misc.load(...);
//       ...
//       }
//   }
//
// REFERENCES:
//   Design rationale and phasing:
//     D:\EmulatR\EmulatRAppUniV4\Emulatr\journals\CchipPhaseA_Design_Notes.md
//   Header convention:
//     D:\EmulatR\EmulatRAppUniV4\Emulatr\docs\notes\ADR-0001-source-file-headers.md
// ============================================================================
//
// CHANGE HISTORY:
//   2026-05-14  Initial commit -- Phase A scaffolding for the uniform
//               chipset CSR surface.  Companion to Tsunami21272_CsrSpec.h.
//               No call sites yet; Phase B fills the read/write switches
//               in TsunamiCchip.h / TsunamiDchip.h / TsunamiPchip.h with
//               CSR_LOG_R / CSR_LOG_W invocations.
//
// ============================================================================

#ifndef CHIPSETLIB_CSR_DIAG_H
#define CHIPSETLIB_CSR_DIAG_H

#include <cstdint>

namespace chipsetLib {

#ifdef EMULATR_CHIPSET_DIAG

// ----------------------------------------------------------------------------
// Runtime mute -- consulted on every csrLogAccess() call when the macro is
// on.  Atomic-free relaxed load is intentional: muting is a tuning knob and
// a torn read at a flip boundary cannot mis-emit dangerously.  Use
// `setCsrDiagMuted(true)` from the CLI flag path or `initCsrDiagFromEnv()`
// from main() startup to set it.
// ----------------------------------------------------------------------------
extern bool g_csrDiagMuted;

// Read EMULATR_CHIPSET_DIAG_ON=1 from the process environment; if present
// and non-empty, ENABLE output (g_csrDiagMuted=false).  Default is muted
// (diagnostics-default-off rule).  Idempotent; safe to call from main()
// before AppOptions parsing.
void initCsrDiagFromEnv() noexcept;

// Programmatic mute toggle -- wired in from AppOptions CLI flag when added.
inline void setCsrDiagMuted(bool muted) noexcept { g_csrDiagMuted = muted; }
inline bool csrDiagMuted()              noexcept { return g_csrDiagMuted; }

// ----------------------------------------------------------------------------
// The canonical access sink.  Defined in CsrDiag.cpp.  Call sites should
// reach this via the CSR_LOG_R / CSR_LOG_W macros below, never directly,
// so the macro can expand to a no-op when the feature is compiled off.
// ----------------------------------------------------------------------------
void csrLogAccess(char const* chipName,
                  char const* regName,
                  bool        isWrite,
                  uint64_t    rawValue,
                  uint64_t    offset,
                  int         cpuId,
                  uint64_t    cycleCount) noexcept;

#define CSR_LOG_R(chip, name, val, off, cpu, cyc) \
    ::chipsetLib::csrLogAccess((chip), (name), false, (val), (off), (cpu), (cyc))

#define CSR_LOG_W(chip, name, val, off, cpu, cyc) \
    ::chipsetLib::csrLogAccess((chip), (name), true,  (val), (off), (cpu), (cyc))

#else // EMULATR_CHIPSET_DIAG

// Both macros vanish entirely.  Arguments are not evaluated; the call site
// becomes a single ((void)0) the compiler discards.
#define CSR_LOG_R(chip, name, val, off, cpu, cyc) ((void)0)
#define CSR_LOG_W(chip, name, val, off, cpu, cyc) ((void)0)

// Stub the runtime knobs so AppOptions can call them unconditionally
// without an #ifdef at the call site.  Inline + empty bodies compile away.
inline void initCsrDiagFromEnv()        noexcept {}
inline void setCsrDiagMuted(bool)       noexcept {}
inline bool csrDiagMuted()              noexcept { return true; }

#endif // EMULATR_CHIPSET_DIAG

} // namespace chipsetLib

#endif // CHIPSETLIB_CSR_DIAG_H
