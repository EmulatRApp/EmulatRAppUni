// ============================================================================
// systemLib/CpuStateDump.cpp -- post-mortem CpuState and StopReason rendering
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "systemLib/CpuStateDump.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"

#include <iomanip>
#include <ostream>


namespace systemLib {

namespace {

// One-line printable name for the architectural mode field.  Mirrors
// Mode_Privilege values defined in coreLib/VA_types.h.
char const* modeName(coreLib::Mode_Privilege m) noexcept
{
    switch (m) {
        case coreLib::Mode_Privilege::Kernel:     return "Kernel";
        case coreLib::Mode_Privilege::Executive:  return "Executive";
        case coreLib::Mode_Privilege::Supervisor: return "Supervisor";
        case coreLib::Mode_Privilege::User:       return "User";
    }
    return "<invalid>";
}


// One-line printable for a fault code.  Returns "kNoFault" when the
// CPU was running normally; otherwise the kFault* constant name.
char const* faultName(uint16_t code) noexcept
{
    switch (code) {
        case coreLib::kNoFault:           return "kNoFault";
        case coreLib::kFaultOpcDec:       return "kFaultOpcDec";
        case coreLib::kFaultPrivileged:   return "kFaultPrivileged";
        case coreLib::kFaultUnimplemented:return "kFaultUnimplemented";
        case coreLib::kFaultUnaligned:    return "kFaultUnaligned";
        case coreLib::kFaultDtbMiss:      return "kFaultDtbMiss";
        case coreLib::kFaultItbMiss:      return "kFaultItbMiss";
        case coreLib::kFaultAcv:          return "kFaultAcv";
        case coreLib::kFaultFor:          return "kFaultFor";
        case coreLib::kFaultFow:          return "kFaultFow";
        case coreLib::kFaultFoe:          return "kFaultFoe";
        case coreLib::kFaultBusError:     return "kFaultBusError";
        case coreLib::kFaultNonCanonical: return "kFaultNonCanonical";
        case coreLib::kFaultHalt:         return "kFaultHalt";
        default:                          return "<unknown-fault>";
    }
}


// Print one register row of four entries (R00=... R01=... R02=... R03=...).
// Used by both intReg and fpReg dumps.
void printRegRow(std::ostream& os, char prefix,
                 uint64_t const* regs, int firstIdx) noexcept
{
    os << "   ";
    for (int j = 0; j < 4; ++j) {
        int const i = firstIdx + j;
        os << ' ' << prefix
           << std::setw(2) << std::setfill('0') << std::dec << i
           << "=0x"
           << std::setw(16) << std::setfill('0') << std::hex << regs[i];
    }
    os << '\n' << std::dec;
}

} // anonymous namespace


void dumpCpuState(coreLib::CpuState const& cpu, std::ostream& os)
{
    auto const flags = os.flags();

    // ------------------------------------------------------------------
    // Header line.
    // ------------------------------------------------------------------
    os << "PC       = 0x" << std::setw(16) << std::setfill('0') << std::hex
       << cpu.pcAddr()
       << "  palMode = " << std::dec << (cpu.inPalMode() ? "true " : "false")
       << "  halted = " << (cpu.halted ? "true " : "false")
       << "  cycles = " << cpu.cycleCount
       << '\n';

    os << "lastFault= " << std::dec << cpu.lastFaultCode
       << " (" << faultName(cpu.lastFaultCode) << ")"
       << "    excAddr = 0x"
       << std::setw(16) << std::setfill('0') << std::hex << cpu.excAddr
       << "\n\n";

    // ------------------------------------------------------------------
    // Integer regfile.
    // ------------------------------------------------------------------
    os << "Integer regs:\n";
    for (int i = 0; i < 32; i += 4)
        printRegRow(os, 'R', cpu.intReg, i);
    os << '\n';

    // ------------------------------------------------------------------
    // FP regfile.
    // ------------------------------------------------------------------
    os << "FP regs:\n";
    for (int i = 0; i < 32; i += 4)
        printRegRow(os, 'F', cpu.fpReg, i);
    os << '\n';

    // ------------------------------------------------------------------
    // IPRs (selected; not exhaustive).
    // ------------------------------------------------------------------
    os << "IPRs:\n";
    os << std::hex;
    os << "    ptbr=0x"   << cpu.ptbr
       << "  asn=0x"      << static_cast<uint32_t>(cpu.asn)
       << "  va_ctl=0x"   << cpu.va_ctl
       << "  i_ctl=0x"    << cpu.i_ctl
       << "  m_ctl=0x"    << cpu.m_ctl
       << '\n';
    os << "    palBase=0x" << cpu.palBase
       << "  excAddr=0x"   << cpu.excAddr
       << '\n';
    os << "    i_spe=0x"  << static_cast<uint32_t>(cpu.i_spe)
       << "  m_spe=0x"    << static_cast<uint32_t>(cpu.m_spe)
       << "  mode=" << std::dec << modeName(cpu.mode)
       << "  mm_stat=0x"  << std::hex << cpu.mm_stat
       << "  intrFlag=0x" << cpu.intrFlag
       << '\n';
    os << "    reservedCacheLine=0x" << cpu.reservedCacheLine
       << "  hasReservation=" << std::dec << (cpu.hasReservation ? "true" : "false")
       << '\n';

    os.flags(flags);
}


void dumpStopReason(StopReason reason,
                    coreLib::CpuState const& cpu,
                    std::ostream& os)
{
    auto const flags = os.flags();

    os << "Stop reason: " << stopReasonName(reason);

    if (reason != StopReason::HaltedClean) {
        // For non-clean stops, surface the exception PC and the fault
        // code; an OpcDecFault dump that tells you the PC saves the
        // first round-trip when chasing a missing leaf.
        os << "  at PC=0x" << std::hex << std::setw(16)
           << std::setfill('0') << cpu.excAddr
           << "  fault=" << std::dec << cpu.lastFaultCode
           << " (" << faultName(cpu.lastFaultCode) << ")";
    }
    os << "\n";

    os.flags(flags);
}

} // namespace systemLib
