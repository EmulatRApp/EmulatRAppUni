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
#include "memoryLib/GuestMemory.h"

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
    // apisrm SWPCTX writes the new ASN to DTB_ASN0/DTB_ASN1 as part of
    // the swap (ev6_vms_callpal.mar:249-285; GATE-1 Q2 step 4).  The
    // DTB fill path tags entries with dtbAsn0/1 while lookups key on
    // cpu.asn -- without this install, every post-swap fill under a
    // nonzero ASN is tagged with the OLD ASN and can never hit (audit
    // PE-3, 2026-07-28; masked while all boot-era ASNs are 0).
    cpu.dtbAsn0    = static_cast<coreLib::ASNType>(src.asn);
    cpu.dtbAsn1    = static_cast<coreLib::ASNType>(src.asn);
    cpu.asten_sr   = src.asten_sr;
    // The HWPCB FEN quadword packs three architectural fields (apisrm
    // ev6_vms_pal_defs.mar:345-350): FEN<0>, PME<62>, DAT<63>.  Unpack
    // into their CpuState homes; the quad's other bits are MBZ.
    cpu.fen        = src.fen & 0x1ULL;
    cpu.pme        = (src.fen >> 62) & 0x1ULL;
    cpu.dat        = (src.fen >> 63) & 0x1ULL;
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
    // Repack the FEN quadword: FEN<0> | PME<62> | DAT<63> (see the
    // unpack note in loadCpuFromHwpcb).
    dst.fen      = (cpu.fen & 0x1ULL)
                 | ((cpu.pme & 0x1ULL) << 62)
                 | ((cpu.dat & 0x1ULL) << 63);
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

// ----------------------------------------------------------------------------
// Guest-physical HWPCB I/O (SPEC-SWPCTX-001 C2).
//
// ALL HWPCB access is PHYSICAL (brief Sec 4.2): these helpers go straight
// through GuestMemory quad accessors -- no DTB, no SPAM TB, no translation,
// exactly like apisrm's hw_ldq/p / hw_stq/p.  Alignment (PA<6:0> == 0) is
// the LEAF's contract to enforce (ILLOP per AARM 10-88 / apisrm
// ev6_vms_callpal.mar:199,461); helpers propagate MemStatus and do not
// re-check.
// ----------------------------------------------------------------------------

// Read the full 128-byte HWPCB image at physical address `pa`.
// Returns the first non-Ok MemStatus, or Ok when all 16 quads read.
[[nodiscard]] inline memoryLib::MemStatus
readHwpcbFromGuest(memoryLib::GuestMemory const& mem,
                   coreLib::PAType               pa,
                   Hwpcb&                        out) noexcept
{
    uint64_t* const q = reinterpret_cast<uint64_t*>(&out);
    for (unsigned i = 0; i < sizeof(Hwpcb) / 8; ++i) {
        memoryLib::MemStatus const st = mem.read8(pa + i * 8ULL, q[i]);
        if (st != memoryLib::MemStatus::Ok) return st;
    }
    return memoryLib::MemStatus::Ok;
}

// Write the SWPCTX SAVE SET into the old HWPCB at physical address `pa`.
//
// Field-set policy lives HERE, in one place (GATE-1 Q2): the save set is
// KSP + AST + CPC, apisrm's exact field set (ev6_vms_callpal.mar:426-433).
// ESP/SSP/USP are NOT touched by EV6 SWPCTX (AARM 10-90 Note): on
// processors without per-mode internal SPs, only the CURRENT mode's SP
// lives in a register -- SWPCTX runs in kernel mode, so only KSP swaps
// here; the other three are exchanged with the HWPCB at mode transitions
// by the guest PAL (hw_stq/p ... PCB__ESP/SSP/USP), which makes the
// HWPCB the live home of those fields between swaps.  Writing them from
// CpuState mirrors here clobbered the guest-maintained values with
// stale swap-in-era copies (audit PE-1, 2026-07-28).  CPC saves as a
// 32-BIT quantity (apisrm hw_stl/p -- the high half of HWPCB+0x40 is
// NOT ours to clobber).  PTBR is never saved; ASN save is UNPREDICTABLE
// (we do not); FEN/PME/DAT are maintained in the HWPCB by their own
// MTPR/CLRFEN flows (AARM txt:17679-17694), not by SWPCTX; UNQ/SCT
// belong to RD/WR_UNQ and PALcode (GATE-1 D4).
[[nodiscard]] inline memoryLib::MemStatus
writeHwpcbSaveSet(memoryLib::GuestMemory& mem,
                  coreLib::PAType         pa,
                  Hwpcb const&            src) noexcept
{
    using memoryLib::MemStatus;
    MemStatus st;
    if ((st = mem.write8(pa + 0x00, src.ksp)) != MemStatus::Ok) return st;
    if ((st = mem.write8(pa + 0x30, src.asten_sr)) != MemStatus::Ok) return st;
    // CPC longword: read-merge-write the low 32 bits of HWPCB+0x40.
    uint64_t old40 = 0;
    if ((st = mem.read8(pa + 0x40, old40)) != MemStatus::Ok) return st;
    uint64_t const merged = (old40 & 0xFFFFFFFF00000000ULL)
                          | (src.cc & 0xFFFFFFFFULL);
    return mem.write8(pa + 0x40, merged);
}

}  // namespace hwrpb
}  // namespace deviceLib

#endif  // EMULATR_DEVICELIB_HWPCB_CONTEXT_H
