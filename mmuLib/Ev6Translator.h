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
//
// CHANGE HISTORY
//   FILE 1: mmuLib/Ev6Translator.h
//   FUNCTION: translateInstruction (ITB lookup hit/miss return)
//   CHANGE 2026-07-18 (JRN-VMB-003, ITBPROBE): restored this file from the
//     intact 564-line good copy -- the active-hive copy had been truncated at
//     the translateInstruction declaration (body absent, no include-guard
//     #endif) and would not compile -- then added the Step 1b hit-path and
//     miss-path ITBPROBE at the ITB lookup return.  The probe is
//     EMULATR_BRINGUP_PROBES-gated, keyed to EMULATR_ITBPROBE_VA (default
//     0x20000000, the VMB system-software entry, PC<0>-masked), capped at 16,
//     and emits the JRN-VMB-003 Sec 6 discriminator fields
//     (pte/valid/pfn/foe/res/pa/ZEROPFN) plus a one-shot ARMED line.  It is
//     observe-only and zero-cost when the flag is off.  Bug addressed: the VA
//     0x20000000 boot fetch resolves as an ITB HIT with PFN 0 and NO miss
//     fires; this probe splits H-A (a stale / invalid entry being HIT) from a
//     wrongly-filled PFN-0 entry, per the Sec 6 decision table.
//
// ============================================================================

#ifndef MMULIB_EV6TRANSLATOR_H
#define MMULIB_EV6TRANSLATOR_H

#include <cstdint>
#include <cstdio>   // EMULATR_BRINGUP_PROBES ACVPROBE Hook A
#include <cstdlib>  // EMULATR_BRINGUP_PROBES getenv/strtoull for Hook A floor

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
            TranslationResult const hit =
                applyTlbHit(r.pte, va, access, cpu.mode, pa_out);
#if defined(EMULATR_BRINGUP_PROBES)
            // ACVPROBE Hook B (2026-07-09): the ES40 memtest ACV (task: ES40
            // memtest ACV, PC 0x1b7dd4 LDQ probe on VA 0xFFFFFFFF_7F827F5F) is
            // raised on a TLB HIT whose installed PTE denies kernel read
            // (applyTlbHit !pte.canRead at this same file's :222), NOT on a
            // miss -- so Hook A below (miss-only) cannot observe it.  Dump the
            // resident PTE + permission decode across the memtest sweep window
            // to split (A) canRead/mode miseval [valid=1,kre=1 yet ACV] from
            // (B/C) a deny PTE the PAL installed [valid=0 or kre=0].  Keyed to
            // the sweep VA range, capped at 40, zero-cost when the flag is off.
            if (hit == TranslationResult::AccessViolation
             || hit == TranslationResult::FaultOnRead) {
                static unsigned long s_acvB = 0;
                bool const keyed = (va >= 0xFFFFFFFF7F827000ULL
                                 &&  va <  0xFFFFFFFF7F829000ULL);
                if (keyed && s_acvB < 40) { ++s_acvB;
                    // vptb/vactl added 2026-07-09 for the VPTB-propagation
                    // check (web-variant analysis D1): cpu.vptb is what
                    // MTPR_VPTB stored; VA_FORM's base is va_ctl<63:43>
                    // (computeVaForm masks va_ctl & 0xFFFFF80000000000).
                    // vptb != 0 while va_ctl<63:43> == 0 confirms the
                    // stranded-base defect that yields the 0x7ffffdfe098
                    // (VPTB=0) self-map fetch.
                    std::fprintf(stderr,
                        "ACVPROBE HOOKB cyc=%llu va=%016llx mode=%d res=%d "
                        "pte=%016llx valid=%d kre=%d for=%d pfn=%llx "
                        "vptb=%016llx vactl=%016llx\n",
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<unsigned long long>(va),
                        static_cast<int>(cpu.mode),
                        static_cast<int>(hit),
                        static_cast<unsigned long long>(r.pte.raw),
                        static_cast<int>(r.pte.isValid()),
                        static_cast<int>(r.pte.bitKRE()),
                        static_cast<int>(r.pte.faultOnRead()),
                        static_cast<unsigned long long>(r.pte.pfn()),
                        static_cast<unsigned long long>(cpu.vptb),
                        static_cast<unsigned long long>(cpu.va_ctl));
                    std::fflush(stderr);
                }
            }
#endif
            return hit;   // ACVPROBE Hook B wraps this hit-path return
        }
#if defined(EMULATR_BRINGUP_PROBES)
        // ACVPROBE Hook A: the SPE regime at the Dstream miss that feeds the ACV.
        // spe_shape is the RAW SPE-window match, independent of cpu.m_spe, so it
        // distinguishes H1 (VA is superpage-shaped but m_spe lacks the bit) from
        // H4 (VA matches no SPE window).  Fires for kernel-mode misses (the
        // console 1-1 regime) OR any superpage-shaped VA, capped.
        {
            unsigned const s2 = (((va >> 46) & 0x3ULL)   == 0x2ULL)   ? 1u : 0u;
            unsigned const s1 = (((va >> 41) & 0x7FULL)  == 0x7EULL)  ? 1u : 0u;
            unsigned const s0 = (((va >> 30) & 0x3FFFFULL)== 0x3FFFEULL)? 1u : 0u;
            bool const kern = (cpu.mode == coreLib::Mode_Privilege::Kernel);
            // Re-gated (2026-07-05) to the ~248M ACV window: earlier misses
            // exhausted the 40-print cap long before the console's 1-1 ACV.
            // Floor overridable via EMULATR_HOOKA_CYC_FLOOR (default 248000000).
            static unsigned long long const s_acvAFloor = []() -> unsigned long long {
                char const* e = std::getenv("EMULATR_HOOKA_CYC_FLOOR");
                return (e && *e) ? std::strtoull(e, nullptr, 0) : 248000000ULL;
            }();
            static unsigned long s_acvA = 0;
            if ((kern || s2 || s1 || s0) && cpu.cycleCount >= s_acvAFloor
                && s_acvA < 40) { ++s_acvA;
                std::fprintf(stderr,
                    "ACVPROBE HOOKA cyc=%llu va=%016llx mode=%d va_ctl=%llx "
                    "m_spe=%u i_spe=%u shape=S2:%u,S1:%u,S0:%u canon=%d\n",
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(va),
                    static_cast<int>(cpu.mode),
                    static_cast<unsigned long long>(cpu.va_ctl),
                    static_cast<unsigned>(cpu.m_spe),
                    static_cast<unsigned>(cpu.i_spe),
                    s2, s1, s0,
                    static_cast<int>(isCanonicalVA(va, cpu.va_ctl)));
            }
        }
        // ACVPROBE Hook C (2026-07-05): pointer-root capture.  When
        // EMULATR_HOOKA_VA is set and the faulting VA matches EXACTLY, dump the
        // issuing PC + full GPR file.  Identifies the base register (va = base +
        // small offset) and records its VALUE -- exactly 0 (NULL) vs
        // garbage-nonzero forks the diagnosis.  Follow with a PC-gate ring dump
        // on ISSUING-pc for the base register's last-writer provenance.
        {
            static unsigned long long const s_hookaVa = []() -> unsigned long long {
                char const* e = std::getenv("EMULATR_HOOKA_VA");
                return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL;
            }();
            static unsigned long s_hookC = 0;
            if (s_hookaVa != 0
                && static_cast<unsigned long long>(va) == s_hookaVa
                && s_hookC < 8) { ++s_hookC;
                std::fprintf(stderr,
                    "ACVPROBE HOOKC cyc=%llu ISSUING-pc=%016llx va=%016llx mode=%d\n",
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(cpu.pcAddr()),
                    static_cast<unsigned long long>(va),
                    static_cast<int>(cpu.mode));
                for (int r = 0; r < 32; r += 4) {
                    std::fprintf(stderr,
                        "  R%02d=%016llx R%02d=%016llx R%02d=%016llx R%02d=%016llx\n",
                        r,   static_cast<unsigned long long>(cpu.intReg[r]),
                        r+1, static_cast<unsigned long long>(cpu.intReg[r+1]),
                        r+2, static_cast<unsigned long long>(cpu.intReg[r+2]),
                        r+3, static_cast<unsigned long long>(cpu.intReg[r+3]));
                }
                std::fflush(stderr);
            }
        }
#endif
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
#if defined(EMULATR_BRINGUP_PROBES)
        // ITBPROBE key (2026-07-18, JRN-VMB-003): computed once.  Default
        // 0x20000000 (the VMB system-software entry, Sec 3); override with
        // EMULATR_ITBPROBE_VA to key a different fetch site.  Bit 0 (PALmode
        // PC<0>) is masked so the key compares against the address, not mode.
        static uint64_t const s_itbProbeKey = []() noexcept -> uint64_t {
            char const* e = std::getenv("EMULATR_ITBPROBE_VA");
            uint64_t v = (e != nullptr && *e != '\0')
                       ? std::strtoull(e, nullptr, 0)
                       : 0x20000000ULL;
            return v & ~uint64_t{1};
        }();
        bool const s_itbProbeKeyed = ((va & ~uint64_t{1}) == s_itbProbeKey);
#endif
        if (r.isHit()) {
            TranslationResult const hit =
                applyTlbHit(r.pte, va, coreLib::AccessKind::Execute,
                            cpu.mode, pa_out);   // ITBPROBE: pa_out set here
#if defined(EMULATR_BRINGUP_PROBES)
            // ITBPROBE hit-path (JRN-VMB-003 Sec 6): dump the resident ITB PTE
            // + composed PA for the keyed VA.  ZEROPFN flags the PFN-0 case
            // that the observed 0x20000000 halt exhibits; valid/pfn/foe split
            // H-A (stale/invalid entry HIT) from a wrongly-filled PFN-0 entry.
            if (s_itbProbeKeyed) {
                static unsigned s_itbHit = 0;
                if (s_itbHit < 16) {
                    if (s_itbHit == 0) {
                        std::fprintf(stderr,
                            "ITBPROBE ARMED va=%016llx cap=16\n",
                            static_cast<unsigned long long>(s_itbProbeKey));
                    }
                    ++s_itbHit;
                    unsigned const zeropfn = (r.pte.pfn() == 0) ? 1u : 0u;
                    std::fprintf(stderr,
                        "ITBPROBE HIT n=%u cyc=%llu pal=%d va=%016llx mode=%d "
                        "pte=%016llx valid=%d pfn=%llx foe=%d res=%d "
                        "pa=%016llx ZEROPFN=%u\n",
                        s_itbHit,
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<int>(cpu.inPalMode()),
                        static_cast<unsigned long long>(va),
                        static_cast<int>(cpu.mode),
                        static_cast<unsigned long long>(r.pte.raw),
                        static_cast<int>(r.pte.isValid()),
                        static_cast<unsigned long long>(r.pte.pfn()),
                        static_cast<int>(r.pte.faultOnExecute()),
                        static_cast<int>(hit),
                        static_cast<unsigned long long>(pa_out),
                        zeropfn);
                    std::fflush(stderr);
                }
            }
#endif
            return hit;   // ITBPROBE hit-path wraps this return
        }
#if defined(EMULATR_BRINGUP_PROBES)
        // ITBPROBE miss-path (JRN-VMB-003 Sec 4): a ZERO count of this line
        // across a boot is the no-miss proof.  Keyed + capped identically.
        if (s_itbProbeKeyed) {
            static unsigned s_itbMiss = 0;
            if (s_itbMiss < 16) {
                ++s_itbMiss;
                std::fprintf(stderr,
                    "ITBPROBE MISS n=%u cyc=%llu pal=%d va=%016llx mode=%d "
                    "asn=%u\n",
                    s_itbMiss,
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<int>(cpu.inPalMode()),
                    static_cast<unsigned long long>(va),
                    static_cast<int>(cpu.mode),
                    static_cast<unsigned>(cpu.asn));
                std::fflush(stderr);
            }
        }
#endif
        return TranslationResult::ItbMiss;
    }
};



} // namespace mmuLib

#endif // MMULIB_EV6TRANSLATOR_H
