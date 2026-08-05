// ============================================================================
// coreLib/LogArtifactPath.h -- stem-keyed path builder for run-artifact logs.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// FILE 1: coreLib/LogArtifactPath.h
// FUNCTION: single owner of the run-artifact log NAME, so every diagnostic
//           file a run emits is keyed to that run's firmware stem.
// CHANGE (2026-07-31): NEW FILE.  Before this, three event logs each
//           hardcoded a fixed path -- logs/faults.log (FaultEventLog.cpp),
//           logs/unaligned.log (UnalignedEventLog.cpp), logs/cbox_csr.log
//           (CboxEventLog.cpp) -- and each separately called
//           create_directories("logs").  Two EmulatR instances sharing a run
//           directory therefore opened the SAME file with std::ios::trunc and
//           silently destroyed each other's diagnostics.  Concurrent
//           multi-instance execution is now a first-class workflow, so the
//           name gains the stem:
//               logs/<stem>_<purpose>.<ext>   e.g. logs/ds20_v7_3_faults.log
//           STEM-ONLY by architect decision (2026-07-31): no timestamp, so the
//           per-platform path stays stable and greppable across runs and the
//           directory does not accumulate one file per boot.
//           DETERMINISM: the stem is resolved from argv/env ONLY -- no clock,
//           no counter, no ordering dependence, so two runs of the same
//           firmware produce byte-identical artifact names.
//
// Stem precedence (highest first):
//   1. EMULATR_LOG_STEM      -- explicit operator override, for running two
//                               instances of the SAME platform side by side
//   2. setLogArtifactStem()  -- the --firmware basename, set once by main()
//   3. "emulatr"             -- fallback when no firmware was named
//
#ifndef CORELIB_LOGARTIFACTPATH_H
#define CORELIB_LOGARTIFACTPATH_H

#include <string>
#include <string_view>

namespace coreLib {

// Set the run's artifact stem.  Call ONCE from main() after the firmware path
// is settled and BEFORE any subsystem can emit.  An empty stem is ignored (the
// fallback stands); the EMULATR_LOG_STEM override still wins at resolve time.
void setLogArtifactStem(std::string_view stem) noexcept;

// The stem actually in use, after precedence.  Exposed so a banner or startup
// line can tell the operator which stem this instance owns.
std::string logArtifactStem();

// Build "logs/<stem>_<purpose>.<ext>" and ensure logs/ exists.  Callers open
// the returned path themselves; this performs no I/O beyond the directory
// create.  A null purpose/ext degrades to "log" rather than faulting.
std::string logArtifactPath(char const* purpose, char const* ext);

} // namespace coreLib

#endif // CORELIB_LOGARTIFACTPATH_H
