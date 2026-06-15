// ============================================================================
// mmuLib/Ev6Translator.h -- V4 EV6 VA-to-PA translator (kseg-first cut)
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
// Ev6Translator turns a virtual address into a physical address per
// 21264 / EV6 semantics.  The MEM-stage drainer calls one of three
// public entry points per memory effect packed onto a BoxResult by a
// leaf at EX:
//
//   translateData             -- data load / store, no alignment check
//   translateDataAligned      -- data load / store, alignment-checked
//   translateInstruction      -- instruction fetch (longword-aligned)
//
// Each returns a TranslationResult; on Success the caller's pa_out
// reference holds the translated physical address.  Any other value
// is a translation fault and the caller converts it to a faultCode
// via mmuLib::toFaultCode and short-circuits the regfile commit.
//
// Scope of this first cut:
//
//   The translator implements the parts of EV6 translation that do
//   NOT require a TLB or a page-table walker:
//
//     * PAL-mode physical bypass         (palMode -> PA = VA)
//     * Canonical VA window check        (43-bit or 48-bit per VA_CTL)
//     * Kseg / superpage mapping         (SPE[2] / SPE[1] / SPE[0])
//     * Alignment check                  (translateDataAligned only)
//
//   Anything that is NOT kseg and NOT a canonical / palmode bypass
//   returns DtbMiss (data) or ItbMiss (instruction).  This is enough
//   to run console / kernel-kseg test cases end-to-end through the
//   pipeline without page tables existing.  When the page walker
//   and TLB land they slot in between the kseg detector and the
//   miss return -- the public signatures here do not change.
//
// What this header is NOT:
//
//   It is not a class instance.  All entry points are static functions
//   that take a CpuState const& as the first parameter; per-CPU state
//   travels through CpuState, not through a translator object.  The
//   field-access pattern (cpu.va_ctl, cpu.mode, cpu.m_spe / cpu.i_spe)
//   is identical to what IprBank carried before the merge -- CpuState
//   absorbed those fields verbatim.
//
//   It does not call GuestMemory.  When the page walker arrives it
//   will need physical reads to fetch PTEs, but those reads go
//   through whatever GuestMemory accessor V4 ends up exposing -- not
//   through globals reached from inside the translator.
//
//   It does not classify faults beyond returning the right
//   TranslationResult variant.  The mapping from TranslationResult to
//   PAL trap kind happens in toFaultCode (mmuLib/TranslationResult.h)
//   and is consumed by the MEM-stage drainer.
//
// ============================================================================

#ifndef MMULIB_EV6TRANSLATOR_H
#define MMULIB_EV6TRANSLATOR_H

#include <cstdint>

#include "coreLib/CpuState.h"
#include "coreLib/VA_types.h"
#include "coreLib/axp_attributes_core.h"
#include "mmuLib/TranslationResult.h"
#include "mmuLib/UnalignedEventLog.h"
#include "pteLib/AlphaPte.h"
#include "pteLib/SPAMShardManager.h"   // C3: TLB lookup on CpuState managers

namespace mmuLib {

// EV6 physical address is 44 bits.  Anything above bit 43 is
// architecturally zero in a PA.
constexpr uint64_t kEv6PaWidth = 44;
constexpr uint64_t kEv6PaMask  = (1ULL << kEv6PaWidth) - 1ULL;

// Page shift for the EV6 base page size (8 KiB).  Variable page sizes
// are encoded by the GH bits in a PTE (SC_Type).  The kseg paths use
// the base 8 KiB shift; the page walker will consult GH per PTE when
// it lands.
constexpr uint64_t kEv6BasePageShift = 13;


// ---------------------------------------------------------------------------
// isCanonicalVA -- check the sign-extension of the top VA bits.
// ---------------------------------------------------------------------------
// VA_CTL bit 1 selects 48-bit VA mode when set; otherwise 43-bit VA
// mode is in effect.  In 43-bit mode VA<63:43> must be sign-extension
// of VA<42>.  In 48-bit mode VA<63:48> must be sign-extension of
// VA<47>.  Non-canonical addresses raise a translation fault before
// any TLB lookup.
AXP_HOT AXP_FLATTEN
constexpr bool isCanonicalVA(coreLib::VAType va, uint64_t va_ctl) noexcept
{
    const bool va48 = (va_ctl & 0x2ULL) != 0;
    const unsigned msb = va48 ? 47u : 42u;
    const uint64_t high = va >> msb;
    // Either all the bits at and above msb are 0, or all are 1.
    // Equivalently: (high == 0) or (high == ((1 << (64-msb)) - 1)).
    const uint64_t allOnes = (msb == 0) ? ~0ULL : (~0ULL >> msb);
    return (high == 0) || (high == allOnes);
}


// ---------------------------------------------------------------------------
// tryKsegTranslate -- detect and apply kseg superpage mapping.
// ---------------------------------------------------------------------------
// Returns Success with pa_out filled when VA falls in an enabled kseg
// region, NotKseg when the public translator should keep going, or
// AccessViolation when VA looks like kseg but mode is not Kernel
// (kseg is kernel-only on EV6).
//
// The three SPE bits on M_CTL (data) or I_CTL (instruction) gate
// independent superpage modes per the 21264 hardware reference:
//
//   SPE[2]  VA<47:46> == 0b10           -> PA<43:13> = VA<43:13>
//   SPE[1]  VA<47:41> == 0b1111110      -> PA<40:13> = VA<40:13>,
//                                          PA<43:41> = SEXT(VA<40>)
//   SPE[0]  VA<47:30> == 0x3FFFE         -> PA<29:13> = VA<29:13>,
//                                          PA<43:30> = 0
AXP_HOT AXP_FLATTEN
constexpr TranslationResult tryKsegTranslate(
    coreLib::VAType va,
    coreLib::Mode_Privilege mode,
    uint8_t spe,
    coreLib::PAType& pa_out) noexcept
{
    // Kseg is kernel-only.  Non-kernel access to a kseg-shaped VA
    // is an access violation; non-kseg-shaped VA from non-kernel
    // mode falls through to the page walk path (NotKseg).
    const bool nonKernel = (mode != coreLib::Mode_Privilege::Kernel);

    // SPE[2]: VA<47:46> == 2 (0b10).  Maps VA<43:13> -> PA<43:13>,
    // VA<45:44> are ignored.
    if ((spe & 0x4) && ((va >> 46) & 0x3) == 0x2) {
        if (nonKernel) {
            return TranslationResult::AccessViolation;
        }
        pa_out = va & 0x00000FFFFFFFE000ULL;   // PA<43:13>
        return TranslationResult::Success;
    }

    // SPE[1]: VA<47:41> == 0x7E (0b1111110).  Maps VA<40:13> ->
    // PA<40:13>, with PA<43:41> sign-extended from VA<40>.
    if ((spe & 0x2) && ((va >> 41) & 0x7F) == 0x7E) {
        if (nonKernel) {
            return TranslationResult::AccessViolation;
        }
        const uint64_t base = va & 0x000001FFFFFFE000ULL;   // VA<40:13>
        if (base & (1ULL << 40)) {
            pa_out = base | 0x00000E0000000000ULL;          // set PA<43:41>
        }
        else {
            pa_out = base;
        }
        return TranslationResult::Success;
    }

    // SPE[0]: VA<47:30> == 0x3FFFE.  Maps VA<29:13> -> PA<29:13>,
    // PA<43:30> = 0.
    if ((spe & 0x1) && ((va >> 30) & 0x3FFFF) == 0x3FFFE) {
        if (nonKernel) {
            return TranslationResult::AccessViolation;
        }
        pa_out = va & 0x000000003FFFE000ULL;   // PA<29:13>
        return TranslationResult::Success;
    }

    return TranslationResult::NotKseg;
}


// ---------------------------------------------------------------------------
// isAlignedFor -- size-class alignment check.
// ---------------------------------------------------------------------------
// accessSize is one of {1, 2, 4, 8}; a size of 0 is treated as
// always-aligned (the byte-granular path).  Returns true when va is
// naturally aligned to its access size.
AXP_HOT AXP_FLATTEN
constexpr bool isAlignedFor(coreLib::VAType va, uint8_t accessSize) noexcept
{
    if (accessSize <= 1) {
        return true;
    }
    const uint64_t mask = static_cast<uint64_t>(accessSize) - 1ULL;
    return (va & mask) == 0ULL;
}


// ---------------------------------------------------------------------------
// applyTlbHit -- finish a TLB-hit translation: permission check + PA compose.
// ---------------------------------------------------------------------------
// Applies the fault-on-* veto and the mode-enable permission check for the
// access kind, then composes PA = (PFN << 13) | page-offset.  Distinguishes
// FaultOnRead/Write/Execute (PTE FOx bit set) vs AccessViolation (per-mode
// enable bit clear).  ITB entries never carry FOE (HRM 5.2.2), so for fetches
// canExecute() reduces to the read-enable check (EV6 execute-gated-by-read).
AXP_HOT AXP_FLATTEN
inline TranslationResult applyTlbHit(
    pteLib::AlphaPte pte,
    coreLib::VAType va,
    coreLib::AccessKind access,
    coreLib::Mode_Privilege mode,
    coreLib::PAType& pa_out) noexcept
{
    switch (access) {
        case coreLib::AccessKind::DataRead:
            if (pte.faultOnRead())     return TranslationResult::FaultOnRead;
            if (!pte.canRead(mode))    return TranslationResult::AccessViolation;
            break;
        case coreLib::AccessKind::DataWrite:
            if (pte.faultOnWrite())    return TranslationResult::FaultOnWrite;
            if (!pte.canWrite(mode))   return TranslationResult::AccessViolation;
            break;
        case coreLib::AccessKind::Execute:
            if (pte.faultOnExecute())  return TranslationResult::FaultOnExecute;
            if (!pte.canExecute(mode)) return TranslationResult::AccessViolation;
            break;
    }

    constexpr uint64_t kOffsetMask = (1ULL << kEv6BasePageShift) - 1ULL;
    pa_out = ((pte.pfn() << kEv6BasePageShift) | (va & kOffsetMask)) & kEv6PaMask;
    return TranslationResult::Success;
}


// ---------------------------------------------------------------------------
// Ev6Translator -- public translator entry points.
// ---------------------------------------------------------------------------
struct Ev6Translator
{
    // -------------------------------------------------------------
    // translateData
    //
    //   Translate a data-stream VA.  No alignment check (caller is
    //   either operating on a byte-granular access or has already
    //   alignment-checked via translateDataAligned).
    //
    //   Path:
    //     1. PAL mode -> identity map and return Success
    //     2. VA_CTL physical-mode bit -> identity map and return
    //        Success
    //     3. Canonical VA window check
    //     4. Kseg detection
    //     5.TODO (page walk -- not yet implemented) -> DtbMiss
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static TranslationResult translateData(
        coreLib::CpuState const& cpu,
        coreLib::VAType va,
        coreLib::AccessKind access,
        coreLib::PAType& pa_out) noexcept
    {
        // C5 (2026-05-27): the blanket PAL-mode physical bypass is REMOVED.
        // EV6 PAL mode does NOT disable D-stream translation -- only the
        // explicitly-physical accesses (HW_LD / HW_ST / LDQP / STQP, tagged
        // S_PhysAddr) bypass, and MemDrainer::applyMemEffect already handles
        // those UPSTREAM (pa == va) before ever calling translateData.  So
        // every access that reaches this function is a NORMAL load/store and
        // must translate, PAL mode or not.  The old
        //     if (cpu.inPalMode()) { pa_out = va & kEv6PaMask; return Success; }
        // mis-mapped VMS S0 system-space kernel-stack VAs (0xFFFFFFFF_8xxxxxxx)
        // to unbacked high PAs (~17 TB), losing every kernel-stack push into
        // the chipset sink; those must DTB-translate (miss -> firmware DTB-miss
        // handler fills -> retry hits real DRAM).

        // C4: the VA_CTL[VA_48]=0 "physical mode" hack is REMOVED here.  It
        // was HRM-incorrect (VA_48 selects 43/48-bit FORMAT, not phys-vs-virt)
        // and shadowed the DTB.  Non-PAL, non-kseg data now always translates.

        // Canonical VA check.  Non-canonical addresses fault before
        // any TLB lookup.
        if (!isCanonicalVA(va, cpu.va_ctl)) {
            return TranslationResult::NonCanonical;
        }

#ifdef EMULATR_BOOTSTRAP_ITB_BYPASS
        // DEBUG ONLY: bypass ITB for the known reset/PAL entry page
        // until the PAL ITB-miss handler is verified.  Remove before
        // any code that depends on permission bits or ASN isolation.
        if (va >= kBootstrapVaLo && va < kBootstrapVaHi) {
            logBootstrapBypass(cpu.cycleCount, cpu.pcAddr(), va);
            pa_out = (va & kEv6PaMask);  // 44-bit, not 32
            return TranslationResult::Success;
        }
#endif
       

        // Kseg detection.  Returns Success with pa_out filled when
        // VA matches an enabled SPE region; NotKseg otherwise.
        coreLib::PAType ksegPa = 0;
        TranslationResult kr = tryKsegTranslate(
            va, cpu.mode, cpu.m_spe, ksegPa);
        if (kr == TranslationResult::Success) {
            pa_out = ksegPa & kEv6PaMask;
            return TranslationResult::Success;
        }
        if (kr == TranslationResult::AccessViolation) {
            return TranslationResult::AccessViolation;
        }

        // DTB lookup (C3).  On a live hit, permission-check and return the
        // composed PA.  On miss, DtbMiss -> the MEM drainer maps it to
        // kFaultDtbMiss and PALcode's DTB-miss vector refills the TB
        // (HW_MTPR DTB_TAG0/PTE0, wired in C2b) then retries the access.
        pteLib::LookupResult const r =
            cpu.dtbMgr.lookup(pteLib::TlbRealm::Dtb, va, cpu.asn);
        if (r.isHit()) {
            return applyTlbHit(r.pte, va, access, cpu.mode, pa_out);
        }
        return TranslationResult::DtbMiss;
    }


    // -------------------------------------------------------------
    // translateDataAligned
    //
    //   Same as translateData but performs the alignment check on
    //   VA before any other work.  accessSize is in bytes (1, 2, 4,
    //   or 8).  Returns Unaligned without consulting any other state
    //   when VA is not naturally aligned.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static TranslationResult translateDataAligned(
        coreLib::CpuState const& cpu,
        coreLib::VAType va,
        uint8_t accessSize,
        coreLib::AccessKind access,
        coreLib::PAType& pa_out) noexcept
    {
        // Alignment check is uniform across PAL / non-PAL modes (it runs
        // here, before translateData; a misaligned PAL-mode access trips the
        // same path as a misaligned kernel-mode LDQ).  When cpu.unalignTrapEnabled
        // is false (V4 v1 default), misalignment is silently fixed-up
        // by passing the byte-offset PA through to GuestMemory whose
        // memcpy-based read/write is alignment-agnostic on x64.  When
        // true, the EV6 UNALIGN trap fires as the architecture
        // specifies.  See CpuState::unalignTrapEnabled for rationale.
        //
        // Forensic telemetry: every fixup event (the !aligned && !trap
        // path) gets one row in logs/unaligned.log via
        // logUnalignedEvent.  The .trc file's "cyc=" entries provide
        // the surrounding context; this log is the index of cycles
        // worth investigating.
        if (!isAlignedFor(va, accessSize)) {
            if (cpu.unalignTrapEnabled) {
                return TranslationResult::Unaligned;
            }
            logUnalignedEvent(cpu.cycleCount, cpu.pcAddr(), va,
                              accessSize, cpu.inPalMode());
        }
        return translateData(cpu, va, access, pa_out);
    }


    // -------------------------------------------------------------
    // translateInstruction
    //
    //   Translate an instruction-stream VA.  Includes a 4-byte
    //   alignment check (Alpha instructions are longword-aligned)
    //   and consults the I-side super-page enables (i_spe) rather
    //   than the D-side (m_spe).
    //
    //   Path:
    //     1. Alignment check (4-byte)
    //     2. PAL mode -> PA = VA (PAL-mode PC<0> bit cleared)
    //     3. VA_CTL physical-mode bit -> identity map
    //     4. Canonical VA window check
    //     5. Kseg detection (i_spe)
    //     6. TODO (ITB walk -- not yet implemented) -> ItbMiss
    // -------------------------------------------------------------
    static TranslationResult translateInstruction(
        coreLib::CpuState const& cpu,
        coreLib::VAType va,
        coreLib::PAType& pa_out) noexcept
    {
        // CHANGE 2026-05-21 (PALmode == PC<0>): strip the mode bit before
        // the alignment check.  The instruction VA is cpu.pc, whose bit 0
        // is now the PALmode flag, not address -- checking the raw value
        // would fault every PAL-mode fetch as Unaligned.  Masking bit 0
        // still checks bit 1, so a genuinely misaligned PC (bit 1 set)
        // correctly faults.
        if (!isAlignedFor(va & ~uint64_t{1}, 4)) {
            return TranslationResult::Unaligned;
        }

        if (cpu.inPalMode()) {
            // PAL-mode fetch zeroes PC<0>; the bit is a PALmode flag
            // and is not part of the address.
            pa_out = (va & ~0x1ULL) & kEv6PaMask;
            return TranslationResult::Success;
        }

        // C4: VA_CTL[VA_48]=0 "physical mode" hack REMOVED (see translateData).
        // Non-PAL, non-kseg fetches now always translate through the ITB.

        if (!isCanonicalVA(va, cpu.va_ctl)) {
            return TranslationResult::NonCanonical;
        }

#ifdef EMULATR_BOOTSTRAP_ITB_BYPASS
        // DEBUG ONLY: bypass ITB for the known reset/PAL entry page
        // until the PAL ITB-miss handler is verified.  Remove before
        // any code that depends on permission bits or ASN isolation.
        if (va >= kBootstrapVaLo && va < kBootstrapVaHi) {
            logBootstrapBypass(cpu.cycleCount, cpu.pcAddr(), va);
            pa_out = (va & kEv6PaMask);  // 44-bit, not 32
            return TranslationResult::Success;
        }
#endif

        coreLib::PAType ksegPa = 0;
        TranslationResult kr = tryKsegTranslate(
            va, cpu.mode, cpu.i_spe, ksegPa);
        if (kr == TranslationResult::Success) {
            pa_out = ksegPa & kEv6PaMask;
            return TranslationResult::Success;
        }
        if (kr == TranslationResult::AccessViolation) {
            return TranslationResult::AccessViolation;
        }

        // ITB lookup (C3).  On a live hit, permission-check (Execute) and
        // return PA.  On miss, ItbMiss -> kFaultItbMiss -> PALcode ITB-miss
        // vector refills (HW_MTPR ITB_TAG/PTE) and retries the fetch.
        pteLib::LookupResult const r =
            cpu.itbMgr.lookup(pteLib::TlbRealm::Itb, va, cpu.asn);
        if (r.isHit()) {
            return applyTlbHit(r.pte, va, coreLib::AccessKind::Execute,
                               cpu.mode, pa_out);
        }
        return TranslationResult::ItbMiss;
    }
};



} // namespace mmuLib

#endif // MMULIB_EV6TRANSLATOR_H
