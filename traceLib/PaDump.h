// ============================================================================
// traceLib/PaDump.h -- physical-address disassembly dump facility
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
// PURPOSE
//
//   Dump raw bytes or pre-formatted disassembly for a span of guest
//   physical memory.  Used during bring-up to inspect firmware bytes
//   without capturing a full retire trace.  Inverse problem to the
//   compact retire trace: that trace shows what the firmware ran;
//   this dump shows what's THERE TO BE RUN.
//
//   The original motivating use case is the 0x6003ec PAL spin loop
//   discovered 2026-05-19: a six-instruction tight loop containing a
//   HW_MTPR whose IPR target we need to read out of the bytes.  Run
//   the emulator with `--dump-disasm 0x6003e0:32` to dump 32
//   instructions starting at PA 0x6003e0 immediately after the
//   firmware load completes, before the run begins.
//
// API
//
//   dumpPaRange(mem, pa, bytes, out)
//     Prints `bytes` bytes starting at physical address `pa` as a
//     hexdump: 16 bytes per row, address column on the left, ASCII
//     gutter on the right.  PA bounds enforced; out-of-range
//     addresses print "<oor>".
//
//   dumpDisasmAt(mem, pa, instructions, out)
//     Prints `instructions` 4-byte words starting at physical
//     address `pa` as a disassembly: PA column, 32-bit encoded
//     instruction, primary mnemonic, and operand string.  Stops
//     early if a read hits OutOfRange.
//
// COMPILE-TIME GATE
//
//   Two-level discipline mirroring CsrDiag.h:
//
//     1. CMake option EMULATR_PA_DUMP gates the entire facility.
//        Default ON for Debug and RelWithDebInfo, forced OFF in
//        Release via $<$<NOT:$<CONFIG:Release>>:...>.
//
//     2. The DUMP_PA / DUMP_DISASM macros expand to ((void)0) when
//        the macro is off, so call sites compile away entirely.
//        The free functions remain callable; AppOptions reaches them
//        directly when the flag is parsed.
//
//   The AppOptions --dump-disasm flag is parsed regardless of the
//   gate (so a Release build doesn't choke on the flag), but the
//   actual dump call is short-circuited when EMULATR_PA_DUMP is 0.
//
// USAGE
//
//   CLI:
//     Emulatr --firmware foo.exe --dump-disasm 0x6003e0:32 [...]
//
//   In-source (post-Phase 2):
//     #include "coreLib/PaDump.h"
//     DUMP_DISASM(memory, 0x6003e0, 32);   // to stderr
//
// REFERENCES
//
//   Companion facility:  coreLib/LogSubsystem.h
//   First use case:      project_srmloader_axpbox_model.md
//                        (decoding the cyc 200M / PC 0x6003ec spin)
//
// ============================================================================
//
// CHANGE HISTORY
//
//   2026-05-19  Initial commit -- header, .cpp, CMake guard, and the
//               --dump-disasm CLI flag.  No internal call sites; the
//               facility is invoked from main() during firmware-load
//               handoff so it never runs in the hot path.
// ============================================================================

#ifndef TRACELIB_PA_DUMP_H
#define TRACELIB_PA_DUMP_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace memoryLib { class GuestMemory; }

namespace traceLib {

// Print `bytes` bytes of guest physical memory starting at `pa` as a
// hexdump (16 bytes per row, address + hex + ASCII gutter).  Stops
// early on the first OutOfRange read.  Never throws.
void dumpPaRange(memoryLib::GuestMemory const& mem,
                 uint64_t                      pa,
                 std::size_t                   bytes,
                 std::ostream&                 out) noexcept;


// Print `instructions` 4-byte words as a primary-mnemonic disassembly
// starting at `pa`.  Each line is:
//
//   PA=0xHHHHHHHHHHHHHHHH  encoded=0xHHHHHHHH  <PRIMARY_MNEM>  <operands>
//
// Operand rendering uses traceLib::disassembleOperands so the
// per-format conventions match the retire trace.  Primary mnemonic
// is the group name (e.g., "INTA", "Bra", "HW_MTPR") rather than the
// per-leaf mnemonic; the latter requires walking the sub-table from
// the encoded bits, which the dispatcher does at execute time but
// this dump explicitly does not.  Sub-opcode is visible in the hex
// encoding for any reader who needs to disambiguate.
//
// Stops early on the first OutOfRange read.  Never throws.
void dumpDisasmAt(memoryLib::GuestMemory const& mem,
                  uint64_t                      pa,
                  std::size_t                   instructions,
                  std::ostream&                 out) noexcept;

} // namespace traceLib


// ============================================================================
// Public macros -- expand to ((void)0) when EMULATR_PA_DUMP is off so
// call sites compile away entirely.  Free functions remain reachable
// for AppOptions / main one-shot invocations.
// ============================================================================

#if EMULATR_PA_DUMP

#define DUMP_PA(mem, pa, bytes)       \
    ::traceLib::dumpPaRange((mem), (pa), (bytes), std::cerr)

#define DUMP_DISASM(mem, pa, count)   \
    ::traceLib::dumpDisasmAt((mem), (pa), (count), std::cerr)

#else // EMULATR_PA_DUMP

#define DUMP_PA(mem, pa, bytes)       ((void)0)
#define DUMP_DISASM(mem, pa, count)   ((void)0)

#endif // EMULATR_PA_DUMP

#endif // TRACELIB_PA_DUMP_H
