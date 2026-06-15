// ============================================================================
// coreLib/FaultEventLog.h -- forensic log of fault deliveries (OPCDEC,
// unimplemented, privileged, unaligned, ACV, bus error, ...).  One row per
// fault that PipelineDriver::retire delivers to PALcode, written to
// logs/faults.log so the demand list of missing / anomalous opcodes is
// reviewable offline instead of buried in the multi-GB .trc.
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
// Mirrors mmuLib/UnalignedEventLog: lazy-open ofstream, mutex-serialised
// writes, first-N loud on stderr then a periodic heartbeat, truncated by
// resetFaultEventLog() at run start.  The CALLER decides which fault codes
// to pass -- routine TB misses (kFaultDtbMiss / kFaultItbMiss) are
// high-volume paging events and are intentionally NOT logged (they would
// flood the file); everything else (the "gap" / anomaly faults) is.
//
// File format (TSV):
//   # EmulatR V4 fault-delivery log
//   # cycle  pc  encoded  opcode  faultCode  faultName  palMode
//   186581654  0x0000000000013154  0x47ff041f  0x11  3  kFaultUnimplemented  1
//
// Correlation workflow: take a cycle from logs/faults.log, grep the .trc
// for "cyc=<that>" to get the offending retire line + full register state.
// ============================================================================

#ifndef CORELIB_FAULT_EVENT_LOG_H
#define CORELIB_FAULT_EVENT_LOG_H

#include <cstdint>

namespace coreLib {

// Emit one row in the fault-delivery log.  Lazy-opens logs/faults.log on
// first call after a reset.  First N faults also emit SPDLOG_WARN for
// immediate visibility; never throws (I/O failures are tolerated).  The
// caller is responsible for filtering which fault codes to pass.
void logFaultEvent(uint64_t cycle,
                   uint64_t pc,
                   uint32_t encoded,
                   uint16_t faultCode,
                   bool     palMode) noexcept;

// Total fault rows logged since the last reset.
uint64_t faultEventCount() noexcept;

// Truncate + close the log and zero the count; the next emit reopens with
// std::ios::trunc and rewrites the header.  Called by Machine::run() so
// each run starts with a clean log.
void resetFaultEventLog() noexcept;

} // namespace coreLib

#endif // CORELIB_FAULT_EVENT_LOG_H
