// ============================================================================
// systemLib/CpuStateDump.h -- post-mortem dumpers for CpuState and StopReason
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
// Formatted human-readable dump of CpuState and a one-line summary of
// StopReason.  Lives in systemLib (not a separate diagLib) per the
// fabric design discussion -- the orchestration layer owns its own
// post-mortem rendering.  No Qt, no spdlog -- ostream only.
//
// Output shape (dumpCpuState):
//
//   PC       = 0xXXXXXXXXXXXXXXXX  palMode = false  halted = true   cycles = NNN
//   lastFault= NNN ("MNEMONIC")    excAddr = 0xXXXXXXXXXXXXXXXX
//
//   Integer regs:
//     R00=0x... R01=0x... R02=0x... R03=0x...
//     R04=0x... R05=0x... R06=0x... R07=0x...
//     ...
//     R28=0x... R29=0x... R30=0x... R31=0x...
//
//   FP regs:
//     F00=0x... F01=0x... F02=0x... F03=0x...
//     ...
//     F28=0x... F29=0x... F30=0x... F31=0x...
//
//   IPRs:
//     ptbr=0x... asn=0x... va_ctl=0x... i_ctl=0x... m_ctl=0x...
//     i_spe=0x... m_spe=0x... mode=Kernel mm_stat=0x... intrFlag=0x...
//     reservedCacheLine=0x... hasReservation=false
//
// Output shape (dumpStopReason):
//
//   Stop reason: HaltedClean  |  OpcDecFault at PC=0x... encoded=0x... primary=0xZZ
//
// Both functions write to a caller-supplied std::ostream so tests can
// capture into a stringstream and main can route to std::cout.
//
// ============================================================================

#ifndef SYSTEMLIB_CPUSTATEDUMP_H
#define SYSTEMLIB_CPUSTATEDUMP_H

#include <iosfwd>

#include "systemLib/StopReason.h"

namespace coreLib {
struct CpuState;
}

namespace systemLib {

void dumpCpuState  (coreLib::CpuState const& cpu, std::ostream& os);
void dumpStopReason(StopReason reason,
                    coreLib::CpuState const& cpu,
                    std::ostream& os);

} // namespace systemLib

#endif // SYSTEMLIB_CPUSTATEDUMP_H
