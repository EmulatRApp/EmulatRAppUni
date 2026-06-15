// ============================================================================
// mmuLib/UnalignedEventLog.h -- forensic log of synthetic unaligned-fixup
// events.  Captures the cycle, PC, VA, width, and mode of every unaligned
// access that the translator silently fixed up rather than trapping.
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
// Purpose:
//
//   When CpuState::unalignTrapEnabled is false (the V4 v1 default),
//   the translator's alignment check returns Success on misaligned EAs
//   so that GuestMemory's memcpy-based read*/write* handles the
//   access at the byte offset.  Without telemetry, every such fixup
//   is invisible -- silently masking what may be a real bug (a base
//   register that was zeroed by an earlier failure, for example).
//
//   This header declares a tiny forensic log that captures one row
//   per fixup event to logs/unaligned.log (TSV, relative to CWD).
//   The retire-compact trace file's "cyc=" entries provide the
//   surrounding context; the unaligned log is the index of cycles
//   worth investigating.
//
// File format:
//
//   # EmulatR V4 unaligned-access fixup log
//   # cycle  pc  va  width  palMode
//   307     0x000000000000dbcc  0xffffffffffffffc0  8  1
//   ...
//
//   First two lines are header comments (start with '#').  Each
//   subsequent line is a tab-separated row.  Cycle is decimal; PC and
//   VA are hex with 0x prefix; width is decimal bytes (1/2/4/8);
//   palMode is 0 or 1.
//
// Logging policy:
//
//   First 16 events: SPDLOG_WARN with full context (loud, immediate
//   visibility on stderr / spdlog default sink).
//   After 16: silent in stderr but still written to the file.  Every
//   256 K events: one SPDLOG_INFO summary line so long-running runs
//   surface a "we are still fixing up" indicator.
//
//   File is truncated by resetUnalignedEventLog(), called from
//   Machine::run() at run start.  Subsequent emits within the same
//   run append.
//
// Cycle correlation workflow:
//
//   1. Identify a cycle of interest from logs/unaligned.log.
//   2. Grep the .trc file for "cyc=<that-cycle>" to get the offending
//      retire line with full register state.
//   3. Walk backward through the .trc to find the producer of the
//      unaligned base register.
//   4. Either fix the producer (root cause) or annotate the
//      unaligned log entry as "expected, by-design unaligned access".
//
// Thread safety:
//
//   std::mutex serialises file writes.  V4 v1 is single-threaded, but
//   the mutex is essentially free in the uncontended case and makes
//   future multi-CPU code safe by default.  The event counter is a
//   std::atomic<uint64_t> with relaxed ordering -- enough to dedupe
//   the loud-vs-quiet decision without paying for full sequential
//   consistency.
// ============================================================================

#ifndef MMULIB_UNALIGNED_EVENT_LOG_H
#define MMULIB_UNALIGNED_EVENT_LOG_H

#include <cstdint>

namespace mmuLib {

// Emit one entry in the unaligned-access fixup log.  Lazy-opens the
// underlying ofstream on first call after a reset.  Writes one TSV
// row plus the SPDLOG_WARN / SPDLOG_INFO surface message per the
// logging policy described in the header comment.  Never throws;
// I/O failures are silently tolerated (the fixup path proceeds).
void logUnalignedEvent(uint64_t cycle,
                       uint64_t pc,
                       uint64_t va,
                       uint8_t  width,
                       bool     palMode) noexcept;

// Total events logged since the last reset.  Useful for end-of-run
// summary dumps or comparing two runs.
uint64_t unalignedEventCount() noexcept;

// Truncate the underlying file, close it, zero the count.  Next emit
// reopens with std::ios::trunc and rewrites the header.  Called by
// Machine::run() so each run starts with a clean log.  Manual capture
// flows (tests, ad-hoc diagnostic runs) can call this directly to
// force a fresh log without invoking run().
void resetUnalignedEventLog() noexcept;

} // namespace mmuLib

#endif // MMULIB_UNALIGNED_EVENT_LOG_H
