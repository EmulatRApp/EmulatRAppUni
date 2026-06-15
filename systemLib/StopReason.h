// ============================================================================
// systemLib/StopReason.h -- post-run classification of why the CPU stopped
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
// StopReason is the small enum Machine::run returns and the post-mortem
// uses to render a one-line summary.  It is derived from the post-run
// CpuState by inspecting cpu.halted + cpu.lastFaultCode, plus whether
// the run loop fell off the end of maxCycles.  No new architectural
// state -- pure derivation.
//
// Bucketing (BoxResult fault code -> StopReason):
//
//   kFaultHalt                       -> HaltedClean
//   kFaultOpcDec                     -> OpcDecFault
//   kFaultUnaligned                  -> UnalignedFault
//   kFaultAcv                        -> AcvFault
//   kFaultDtbMiss / ItbMiss / For /
//   Fow / Foe / BusError / NonCanon  -> MemFault
//   (cpu.halted == false after run) -> MaxCyclesExceeded
//
// New BoxResult fault codes added in the future map to MemFault by
// default unless they warrant their own bucket.
//
// ============================================================================

#ifndef SYSTEMLIB_STOPREASON_H
#define SYSTEMLIB_STOPREASON_H

#include <cstdint>
#include <ostream>

namespace systemLib {

enum class StopReason : uint8_t
{
    HaltedClean        = 0,    // CALL_PAL HALT retired cleanly
    OpcDecFault        = 1,    // unimplemented opcode dispatched OPCDEC
    UnalignedFault     = 2,    // EA + access size disagreed
    AcvFault           = 3,    // access control violation
    MemFault           = 4,    // catch-all for translator / memory faults
    MaxCyclesExceeded  = 5,    // run loop exited from maxCycles, not halted
};


// Lower-bound printable name; used by CpuStateDump and main's exit
// summary.  Lifetime is static; safe to embed in formatted output.
//
// Named stopReasonName rather than toString to avoid clashing with
// doctest's own toString overload set under ADL: doctest's CHECK
// expands to `toString(arg)`, and when the argument is a StopReason
// ADL would find a free `toString(StopReason)` here ahead of doctest's
// templated `toString<T>`, returning const char* instead of
// doctest::String.  That mismatch makes doctest's stringifyBinaryExpr
// body `String + op + String` collapse into pointer arithmetic at
// compile time.  The name is the only thing doctest cares about; the
// rename is the cure.
constexpr char const* stopReasonName(StopReason r) noexcept
{
    switch (r) {
        case StopReason::HaltedClean:        return "HaltedClean";
        case StopReason::OpcDecFault:        return "OpcDecFault";
        case StopReason::UnalignedFault:     return "UnalignedFault";
        case StopReason::AcvFault:           return "AcvFault";
        case StopReason::MemFault:           return "MemFault";
        case StopReason::MaxCyclesExceeded:  return "MaxCyclesExceeded";
    }
    return "<invalid>";
}


// ostream insertion -- doctest's StringMaker uses operator<< when
// has_insertion_operator<T>::value is true, which this overload makes
// the case for StopReason.  CHECK assertion failures now print
// "HaltedClean" etc. instead of an integer.
inline std::ostream& operator<<(std::ostream& os, StopReason r)
{
    return os << stopReasonName(r);
}

} // namespace systemLib

#endif // SYSTEMLIB_STOPREASON_H
