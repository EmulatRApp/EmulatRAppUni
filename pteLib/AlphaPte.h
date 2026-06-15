// ============================================================================
// pteLib/AlphaPte.h -- canonical Alpha AXP / EV6 Page Table Entry
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
// This is the V4 port of V1 pteLib/AlphaPTE_Core.h.  V1 mechanics retained;
// Qt removed (quint64 -> uint64_t), traits collapsed (V4 is EV6-only),
// COW shim dropped, attribute macros replaced by standard noexcept.  The
// canonical (architectural) bit layout is unchanged.
//
// Reference: Alpha Architecture Reference Manual, 3rd Ed., Chapter 3
// "Memory Management" (canonical in-memory PTE) and DEC 21264 Hardware
// Reference Manual Sections 5.2.2 (ITB_PTE) and 5.3.2 (DTB_PTE) for
// the IPR-side layout that is decoded into this canonical form.
//
// Canonical (memory) PTE bit layout:
//
//   bit  0   V    Valid
//   bit  1   FOR  Fault On Read         (HRM:  ITB_PTE has only FOE)
//   bit  2   FOW  Fault On Write        (DTB_PTE only; ITB ignores)
//   bit  3   FOE  Fault On Execute      (ITB_PTE only; DTB ignores)
//   bit  4   ASM  Address Space Match
//   bits 6:5 GH   Granularity Hint (2 bits, encodes 8**GH base pages)
//   bit  8   KRE  Kernel Read Enable
//   bit  9   ERE  Executive Read Enable
//   bit 10   SRE  Supervisor Read Enable
//   bit 11   URE  User Read Enable
//   bit 12   KWE  Kernel Write Enable
//   bit 13   EWE  Executive Write Enable
//   bit 14   SWE  Supervisor Write Enable
//   bit 15   UWE  User Write Enable
//   bits 31:16 reserved / OS-software (V4 leaves untouched)
//   bits 59:32 PFN  Page Frame Number (28 bits, EV6 maximum)
//   bits 63:60 reserved
//
// The four-mode K/E/S/U set matches the architectural Mode_Privilege values
// in coreLib/VA_types.h (Kernel=0, Executive=1, Supervisor=2, User=3) so a
// register read of PS<CM> can be cast directly into the permission switch.
//
// IPR-format conversion (ITB_PTE / DTB_PTE / *_TEMP) is NOT handled here.
// That belongs in a translator-side adapter; the canonical PTE is what the
// TLB stores after the IPR has been decoded.
//
// ============================================================================

#ifndef PTELIB_ALPHA_PTE_H
#define PTELIB_ALPHA_PTE_H

#include <cstdint>

#include "coreLib/VA_types.h"   // Mode_Privilege

namespace pteLib {


// ---------------------------------------------------------------------------
// Architectural bit positions.  Single source of truth for the canonical
// in-memory PTE; ALL accessors in this header derive from these.
// ---------------------------------------------------------------------------
struct AlphaPteBits {
    static constexpr unsigned kV    = 0;
    static constexpr unsigned kFOR  = 1;
    static constexpr unsigned kFOW  = 2;
    static constexpr unsigned kFOE  = 3;
    static constexpr unsigned kASM  = 4;
    static constexpr unsigned kGH0  = 5;   // GH<0>
    static constexpr unsigned kGH1  = 6;   // GH<1>

    static constexpr unsigned kKRE  = 8;
    static constexpr unsigned kERE  = 9;
    static constexpr unsigned kSRE  = 10;
    static constexpr unsigned kURE  = 11;

    static constexpr unsigned kKWE  = 12;
    static constexpr unsigned kEWE  = 13;
    static constexpr unsigned kSWE  = 14;
    static constexpr unsigned kUWE  = 15;

    // PFN at bits 63:32 (32 bits).
    //
    // CHANGE 2026-05-28 (UART-wedge / Pchip I/O window fix): widened from
    // the previous 28 bits to 32.  The earlier 28-bit width assumed the
    // high PFN bits were only needed for >2 TB DRAM (none today), but the
    // I/O window itself requires them: PA bit 43 (the Tsunami chipset
    // window selector at PA 0x800_0000_0000) maps to PFN bit 30, which a
    // 28-bit PFN field truncates.  Symptom: every native LDx/STx to
    // 0x801_FC00_xxxx (Pchip0 dense I/O) translated to 0x001_FC00_xxxx
    // and fell into TsunamiChipset::reportNxm, wedging the firmware on
    // the COM1 LSR/RBR poll loop at PC 0x1c69e8.  Arithmetic match was
    // exact: intended PFN 0x400F_E000 -> 28-bit mask -> 0x000F_E000 ->
    // recomposed PA 0x1_FC00_03F8, matching the value observed in
    // reportNxm's pa argument.  See [[phase-b-verified-and-os-pal-takeover]]
    // session memory.
    //
    // EV6 silicon stores PFN at register bits 62:32 (31 bits, PA[43:13]).
    // The canonical AlphaPte uses the full bits 63:32 (32) so all
    // software-PTE conventions fit, including OS PAL personalities that
    // place a software flag in bit 63 of the in-memory PTE -- harmless
    // here because the HW_MTPR DTB_PTE register write masks bit 63 off
    // before V4 sees it.
    static constexpr unsigned kPfnLsb   = 32;
    static constexpr unsigned kPfnWidth = 32;
};


// ---------------------------------------------------------------------------
// Bit helpers -- compile-time correct, free of UB on width=64.
// ---------------------------------------------------------------------------
constexpr uint64_t pteMask(unsigned width) noexcept
{
    return (width == 64) ? ~uint64_t{0}
                         : ((uint64_t{1} << width) - uint64_t{1});
}

constexpr uint64_t pteBit(unsigned pos) noexcept
{
    return uint64_t{1} << pos;
}


// ---------------------------------------------------------------------------
// AlphaPte -- canonical 64-bit page table entry.
//
// Layout (bit positions above) is fixed by the architecture; no
// implementation-specific overlays live in this type.  The struct holds
// only the raw value so that an AlphaPte is trivially copyable, fits in
// a register, and can be byte-equal-compared.
// ---------------------------------------------------------------------------
struct AlphaPte {
    uint64_t raw;

    // -- Construction ------------------------------------------------------

    constexpr AlphaPte() noexcept : raw(0) {}
    constexpr explicit AlphaPte(uint64_t value) noexcept : raw(value) {}

    static constexpr AlphaPte fromRaw(uint64_t value) noexcept
    {
        return AlphaPte{value};
    }

    static constexpr AlphaPte makeInvalid() noexcept
    {
        return AlphaPte{};
    }

    // V1 ported helper: build a valid mapping with K/U read/write only.
    // Executive/Supervisor enables stay false; raise them via setBit()
    // if a four-mode policy is needed.
    static constexpr AlphaPte makeValid(uint64_t pfn,
                                        bool     kre = true,
                                        bool     kwe = false,
                                        bool     ure = false,
                                        bool     uwe = false,
                                        bool     asmFlag = false) noexcept
    {
        uint64_t r = pteBit(AlphaPteBits::kV);
        if (asmFlag) r |= pteBit(AlphaPteBits::kASM);
        if (kre)     r |= pteBit(AlphaPteBits::kKRE);
        if (kwe)     r |= pteBit(AlphaPteBits::kKWE);
        if (ure)     r |= pteBit(AlphaPteBits::kURE);
        if (uwe)     r |= pteBit(AlphaPteBits::kUWE);
        uint64_t const pfnMask = pteMask(AlphaPteBits::kPfnWidth);
        r |= (pfn & pfnMask) << AlphaPteBits::kPfnLsb;
        return AlphaPte{r};
    }

    constexpr uint64_t toRaw() const noexcept { return raw; }


    // -- Generic bit extract / insert -------------------------------------

    template <unsigned Start, unsigned Len>
    constexpr uint64_t extract() const noexcept
    {
        static_assert(Start + Len <= 64, "bit range exceeds 64-bit width");
        return (raw >> Start) & pteMask(Len);
    }

    template <unsigned Start, unsigned Len>
    constexpr void insert(uint64_t value) noexcept
    {
        static_assert(Start + Len <= 64, "bit range exceeds 64-bit width");
        uint64_t const mask = pteMask(Len) << Start;
        raw = (raw & ~mask) | ((value << Start) & mask);
    }

    constexpr bool testBit(unsigned pos) const noexcept
    {
        return ((raw >> pos) & uint64_t{1}) != 0;
    }


    // -- Single-bit architectural accessors -------------------------------

    constexpr bool bitV()   const noexcept { return testBit(AlphaPteBits::kV);   }
    constexpr bool bitFOR() const noexcept { return testBit(AlphaPteBits::kFOR); }
    constexpr bool bitFOW() const noexcept { return testBit(AlphaPteBits::kFOW); }
    constexpr bool bitFOE() const noexcept { return testBit(AlphaPteBits::kFOE); }
    constexpr bool bitASM() const noexcept { return testBit(AlphaPteBits::kASM); }

    constexpr bool bitKRE() const noexcept { return testBit(AlphaPteBits::kKRE); }
    constexpr bool bitERE() const noexcept { return testBit(AlphaPteBits::kERE); }
    constexpr bool bitSRE() const noexcept { return testBit(AlphaPteBits::kSRE); }
    constexpr bool bitURE() const noexcept { return testBit(AlphaPteBits::kURE); }

    constexpr bool bitKWE() const noexcept { return testBit(AlphaPteBits::kKWE); }
    constexpr bool bitEWE() const noexcept { return testBit(AlphaPteBits::kEWE); }
    constexpr bool bitSWE() const noexcept { return testBit(AlphaPteBits::kSWE); }
    constexpr bool bitUWE() const noexcept { return testBit(AlphaPteBits::kUWE); }

    constexpr bool isValid()  const noexcept { return bitV();   }
    constexpr bool isGlobal() const noexcept { return bitASM(); }


    // -- GH (Granularity Hint) -------------------------------------------
    // GH is PTE<6:5>, 2 bits.  Block size is 8**GH base pages.  If the
    // GH value is inconsistent across the block the behaviour is
    // UNPREDICTABLE (HRM 4.1).
    constexpr uint8_t gh() const noexcept
    {
        return static_cast<uint8_t>((raw >> AlphaPteBits::kGH0) & uint64_t{0x3});
    }

    constexpr bool hasGH() const noexcept { return gh() != 0; }

    constexpr void setGh(uint8_t value) noexcept
    {
        uint64_t const mask = uint64_t{0x3} << AlphaPteBits::kGH0;
        raw = (raw & ~mask)
            | ((static_cast<uint64_t>(value) & uint64_t{0x3})
               << AlphaPteBits::kGH0);
    }


    // -- PFN --------------------------------------------------------------

    constexpr uint64_t pfn() const noexcept
    {
        return extract<AlphaPteBits::kPfnLsb, AlphaPteBits::kPfnWidth>();
    }

    constexpr void setPfn(uint64_t value) noexcept
    {
        insert<AlphaPteBits::kPfnLsb, AlphaPteBits::kPfnWidth>(value);
    }


    // -- Setters ---------------------------------------------------------

    constexpr void setValid(bool v) noexcept
    {
        if (v) raw |=  pteBit(AlphaPteBits::kV);
        else   raw &= ~pteBit(AlphaPteBits::kV);
    }

    constexpr void setAsm(bool a) noexcept
    {
        if (a) raw |=  pteBit(AlphaPteBits::kASM);
        else   raw &= ~pteBit(AlphaPteBits::kASM);
    }

    constexpr void clear() noexcept { raw = 0; }


    // -- Fault-on-* convenience ------------------------------------------

    constexpr bool faultOnRead()    const noexcept { return bitFOR(); }
    constexpr bool faultOnWrite()   const noexcept { return bitFOW(); }
    constexpr bool faultOnExecute() const noexcept { return bitFOE(); }


    // -- Permission checks -----------------------------------------------
    //
    // Mode-aware read/write/execute, matching HRM 4.1.3 access rules:
    //   - FOR/FOW/FOE veto regardless of K/E/S/U enable bits
    //   - the per-mode enable bit must be set for the access mode
    //   - execute requires read (Alpha has no separate X enable)
    //
    constexpr bool canRead(coreLib::Mode_Privilege mode) const noexcept
    {
        if (bitFOR()) return false;
        switch (mode) {
            case coreLib::Mode_Privilege::Kernel:     return bitKRE();
            case coreLib::Mode_Privilege::Executive:  return bitERE();
            case coreLib::Mode_Privilege::Supervisor: return bitSRE();
            case coreLib::Mode_Privilege::User:       return bitURE();
        }
        return false;
    }

    constexpr bool canWrite(coreLib::Mode_Privilege mode) const noexcept
    {
        if (bitFOW()) return false;
        switch (mode) {
            case coreLib::Mode_Privilege::Kernel:     return bitKWE();
            case coreLib::Mode_Privilege::Executive:  return bitEWE();
            case coreLib::Mode_Privilege::Supervisor: return bitSWE();
            case coreLib::Mode_Privilege::User:       return bitUWE();
        }
        return false;
    }

    constexpr bool canExecute(coreLib::Mode_Privilege mode) const noexcept
    {
        if (bitFOE()) return false;
        return canRead(mode);
    }

    // Kernel-mode default overloads -- handy for the many code paths
    // that operate exclusively in PAL/Kernel.
    constexpr bool canRead()    const noexcept { return canRead(coreLib::Mode_Privilege::Kernel);    }
    constexpr bool canWrite()   const noexcept { return canWrite(coreLib::Mode_Privilege::Kernel);   }
    constexpr bool canExecute() const noexcept { return canExecute(coreLib::Mode_Privilege::Kernel); }


    // -- Equality (raw value compare) -----------------------------------
    friend constexpr bool operator==(AlphaPte a, AlphaPte b) noexcept
    {
        return a.raw == b.raw;
    }
    friend constexpr bool operator!=(AlphaPte a, AlphaPte b) noexcept
    {
        return a.raw != b.raw;
    }
};


// AlphaPte is intended to fit in a single 64-bit slot.  Catch the day
// somebody bolts an extra field on by mistake.
static_assert(sizeof(AlphaPte) == sizeof(uint64_t),
              "AlphaPte must remain a 64-bit wrapper");


} // namespace pteLib

#endif // PTELIB_ALPHA_PTE_H
