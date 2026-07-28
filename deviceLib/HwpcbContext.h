// ============================================================================
// HwpcbContext.h -- shuttle helpers between deviceLib::hwrpb::Hwpcb
//                   and coreLib::CpuState
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Two free functions that copy architectural process context between
// guest-memory-backed Hwpcb instances and the live CpuState.  Used by
// CALL_PAL SWPCTX (and any future trap-delivery path that saves/restores
// per-process state) so the field-by-field plumbing lives in exactly one
// place instead of being scattered across leaf bodies.
//
// Field correspondence (Hwpcb -- CpuState):
//
//     hwpcb.ksp       <->  cpu.ksp
//     hwpcb.esp       <->  cpu.esp
//     hwpcb.ssp       <->  cpu.ssp
//     hwpcb.usp       <->  cpu.usp
//     hwpcb.ptbr      <->  cpu.ptbr     (low 63 bits; bit 63 is per-
//                                        process physical-mode flag,
//                                        consumed by the loader)
//     hwpcb.asn       <->  cpu.asn
//     hwpcb.asten_sr  <->  cpu.asten_sr
//     hwpcb.fen       <->  cpu.fen
//     hwpcb.cc        <->  cpu.cycleCount
//     hwpcb.scratch[] <->  PALcode-private; not auto-shuttled
//
// PTBR<63> note: per palcode_dsgn_gde.txt section on swpctx, bit 63 of
// the new HWPCB's PTBR field is a "physical mode" flag -- if set, this
// process runs in physical-address mode until another CALL_PAL toggles
// it.  loadCpuFromHwpcb strips this bit from cpu.ptbr (so the bare PTBR
// field is a clean physical address) and the caller is expected to
// handle the physical-mode bit separately on CpuState (V4 does not yet
// expose a physModeProcess field; add when needed).
// ============================================================================

#ifndef EMULATR_DEVICELIB_HWPCB_CONTEXT_H
#define EMULATR_DEVICELIB_HWPCB_CONTEXT_H

#include "Hwrpb.h"
#include "coreLib/CpuState.h"

#include <cstdint>

namespace deviceLib {
namespace hwrpb {

// ----------------------------------------------------------------------------
// Load a new process context FROM an in-memory Hwpcb image INTO live
// CpuState.  Called by SWPCTX after reading the new HWPCB from guest
// physical memory at R16.
//
// PTBR<63> (the per-process physical-mode flag) is stripped from the
// stored cpu.ptbr value; the caller examines src.ptbr's high bit if it
// needs to track physical-mode-per-process state separately.
// ----------------------------------------------------------------------------
inline void loadCpuFromHwpcb(coreLib::CpuState& cpu, Hwpcb const& src) noexcept
{
    cpu.ksp        = src.ksp;
    cpu.esp        = src.esp;
    cpu.ssp        = src.ssp;
    cpu.usp        = src.usp;
    cpu.ptbr       = src.ptbr & ~(uint64_t{1} << 63);   // strip phys-mode flag
    cpu.asn        = static_cast<coreLib::ASNType>(src.asn);
    cpu.asten_sr   = src.asten_sr;
    cpu.fen        = src.fen;
    // Per-process PCC restore: route through ccOffset, NEVER raw cycleCount.
    // cycleCount is the system timebase (the value the Cchip interval timer
    // masks against); a context switch must NOT move it.  F-1 PACKED MODEL
    // (JRN-ISA-001 F-1): src.cc holds the process's Charged Process Cycles
    // -- (offset + counter) mod 2^32, a 32-bit quantity (AARM 10-88;
    // PCB__CPC).  The restore computes the new OFFSET FIELD exactly as
    // apisrm SWPCTX does (ev6_vms_callpal.mar:407-409):
    //     new offset = (CPC - current counter) mod 2^32
    // so that offset+counter resumes at the saved CPC while the raw
    // timebase stays monotonic.
    cpu.ccOffset   = (src.cc
                      - (cpu.cycleCount * coreLib::CpuState::kCcMultiplier))
                     & 0xFFFFFFFFULL;
    // src.scratch[] is PAL-private context the OS does not see; PALcode
    // is responsible for copying it into PT slots if its convention
    // expects that mirroring.
}

// ----------------------------------------------------------------------------
// Save the current CpuState process context INTO an Hwpcb image
// (typically about to be written back to guest memory at the OLD pcbb).
// Called by SWPCTX before installing a new context.
//
// Note: dst.ptbr receives the low 63 bits only.  If the caller wants
// PTBR<63> = 1 (physical mode), they must OR it in after this returns;
// CpuState does not currently track per-process physical mode.
// ----------------------------------------------------------------------------
inline void storeCpuToHwpcb(Hwpcb& dst, coreLib::CpuState const& cpu) noexcept
{
    dst.ksp      = cpu.ksp;
    dst.esp      = cpu.esp;
    dst.ssp      = cpu.ssp;
    dst.usp      = cpu.usp;
    dst.ptbr     = cpu.ptbr;
    dst.asn      = static_cast<uint64_t>(cpu.asn);
    dst.asten_sr = cpu.asten_sr;
    dst.fen      = cpu.fen;
    // Save the Charged Process Cycles -- (offset + counter) mod 2^32, the
    // 32-bit quantity the AARM stores at HWPCB_PCC (AARM 10-88; apisrm
    // saves it with a LONGWORD store, ev6_vms_callpal.mar:428) -- NOT the
    // packed 64-bit CC and NOT raw cycleCount.  Symmetric with the
    // ccOffset-based restore in loadCpuFromHwpcb: store-then-load
    // round-trips the process CPC exactly while leaving the system
    // timebase (raw cycleCount) untouched.
    dst.cc       = ((cpu.ccOffset & 0xFFFFFFFFULL)
                    + (cpu.cycleCount * coreLib::CpuState::kCcMultiplier))
                   & 0xFFFFFFFFULL;
    // dst.scratch[] is PAL-private; left at whatever the previous
    // contents were.  PALcode populates it explicitly if the personality
    // needs scratch state to survive across the SWPCTX.
}

}  // namespace hwrpb
}  // namespace deviceLib

#endif  // EMULATR_DEVICELIB_HWPCB_CONTEXT_H
