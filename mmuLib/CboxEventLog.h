// ============================================================================
// mmuLib/CboxEventLog.h -- forensic log of CBox CSR (HW_C_DATA / HW_C_SHFT)
// access events.  Captures cycle, PC, op (read / write), value, and the
// resulting cBoxCsr shadow state.
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Purpose:
//
//   HW_C_DATA (scbd 0x2B, canonical EV6__DATA in EV6_DEFS.MAR) is the
//   CBox CSR access window -- a 6-bit slot into a serial shift register
//   that holds the CBox cache-configuration value.  Pre-2026-05-12,
//   V4 silent-zeroed both HW_MFPR and HW_MTPR on this IPR, which
//   caused the OS PAL to spin forever at PC 0x12678..0x12704 waiting
//   for a CBox configuration readback that never produced the
//   expected bits.  CpuState now models cBoxCsr as a real serial
//   shift register; this log captures every read / write so the
//   sequence is recoverable for analysis.
//
//   The Memory.MD and Snapshots_Design_Notes journals at
//   D:\EmulatR have additional context.  Telemetry is the
//   diagnostic chase Tim asked for under the "side-effects file"
//   logging pattern -- mirror of mmuLib::UnalignedEventLog.
//
// File format:
//
//   # EmulatR V4 CBox CSR access log
//   # cycle  pc  op  value  cBoxCsr_after
//   192000010  0x000000000001A48C  W  0x000000000000003F  0x000000000000003F
//   192000018  0x000000000001A4A4  W  0x000000000000001A  0x0000000000000FDA
//   192000026  0x00000000000126B0  R  0x000000000000001A  0x000000000000003F
//   ...
//
//   op is 'W' (MTPR) or 'R' (MFPR).  value on write is the 6-bit
//   chunk being pushed; on read is the 6-bit chunk returned.
//   cBoxCsr_after is the shadow's state immediately after the
//   access -- useful for tracking write/read sequencing.
//
// Logging policy:
//
//   First 32 events: SPDLOG_WARN with full context (loud, immediate
//   visibility -- the read/write sequence around boot is the
//   diagnostic gold here).  After 32: silent in stderr but still
//   written to the file.  Every 64 K events: one SPDLOG_INFO
//   summary line.
//
//   File is truncated by resetCboxEventLog() called from
//   Machine::run() at run start.
// ============================================================================

#ifndef MMULIB_CBOX_EVENT_LOG_H
#define MMULIB_CBOX_EVENT_LOG_H

#include <cstdint>

namespace mmuLib {

// Op codes for the log entries.
enum class CboxOp : uint8_t {
    Read = 0,   // HW_MFPR HW_C_DATA / HW_C_SHFT
    Write = 1,  // HW_MTPR HW_C_DATA / HW_C_SHFT
};

// Emit one entry.  ipr is the V4 HW_IPR enum value (HW_C_DATA = 0x012B
// or HW_C_SHFT = 0x012C).  value on write is the source operand; on
// read is the value returned to the destination GPR.  cBoxCsrAfter is
// CpuState::cBoxCsr immediately after the access.  Never throws;
// I/O failures are silently tolerated.
void logCboxEvent(uint64_t cycle,
                  uint64_t pc,
                  CboxOp   op,
                  uint16_t ipr,
                  uint64_t value,
                  uint64_t cBoxCsrAfter) noexcept;

// Total events logged since the last reset.
uint64_t cboxEventCount() noexcept;

// Truncate the underlying file, close it, zero the count.  Called by
// Machine::run() so each run starts with a clean log.
void resetCboxEventLog() noexcept;

} // namespace mmuLib

#endif // MMULIB_CBOX_EVENT_LOG_H
