// ============================================================================
// coreLib/IprFields.h -- per-IPR field accessors and bit-layout helpers
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
// Named accessors for the bit-level decomposition of EV6 IPRs that
// pack multiple architectural fields into a single register.  The
// HW_MFPR / HW_MTPR dispatch in palBoxLib calls these helpers instead
// of doing bit math inline, so the bit positions live in one place
// and the spec section is cited at the definition.
//
// Coverage today:
//
//   IER_CM   (HRM 5.2.8)   -- Interrupt Enable + Current Mode
//   PAL_BASE (HRM 5.2.13)  -- PALcode base physical address
//   VA_CTL bits live in VA_types.h (vaCtlIs* accessors)
//
// As additional packed IPRs come online (PCTX, EXC_SUM, MM_STAT,
// etc.), add their decomposition helpers here.
//
// All helpers are constexpr and pure.  No CpuState dependency; the
// caller passes the raw register value and gets the field-level
// view back.
//
// ============================================================================

#ifndef CORELIB_IPR_FIELDS_H
#define CORELIB_IPR_FIELDS_H

#include <cstdint>

#include "coreLib/VA_types.h"  // for Mode_Privilege

namespace coreLib {

// ---------------------------------------------------------------------------
// IER_CM (Interrupt Enable + Current Mode) -- HRM Section 5.2.8.
// ---------------------------------------------------------------------------
//
// Combined IPR scbd 0x010A (IER only) / 0x010B (IER + CM).  Bit layout:
//
//   bits [63:39]  IGN (reserved)
//   bits [38:33]  EIEN<5:0>   -- external interrupt enables (irq_h[0..5])
//   bit  [32]     SLEN        -- serial line interrupt enable
//   bit  [31]     CREN        -- corrected read error interrupt enable
//   bits [30:29]  PCEN<1:0>   -- performance counter interrupt enables
//   bits [28:14]  SIEN<15:1>  -- software interrupt enables
//   bit  [13]     ASTEN       -- AST interrupt enable (master gate)
//   bits [12:5]   IGN (reserved)
//   bits [4:3]    CM<1:0>     -- Current Processor Mode
//   bits [2:0]    IGN
//
// (An earlier revision of this block placed ASTEN at bit 2 "varies by
// edition" -- WRONG for EV6.  apisrm ev6_defs.mar :544-563 puts
// ASTEN__S = 0xD = bit 13; corrected 2026-07-31, SPEC-SIRR-AST-001
// Sec 8.2.  No code ever consumed the bad position.)
//
// CM at bits [4:3] is the 21264 layout (21164 had CM at [1:0]; the
// architecture moved it to make room for other field placements).
// V1 ref D:\EmulatR\EmulatRAppUni\palLib_ev6\Pal_Service.h line 4015-4020
// uses mask 0x18 = bits 3|4 to clear CM bits from the IER store.
//
// Storage convention: cpu.ier holds the register with CM bits cleared;
// cpu.mode holds the CM enum value extracted from bits [4:3].
// Reads of HW_IER_CM reconstruct the combined view by shifting cpu.mode
// back into position.

constexpr uint64_t kIerCmModeMask = uint64_t{0x18};  // bits 3 | 4
constexpr int      kIerCmModeShift = 3;

// Extract CM from a raw IER_CM data word.
[[nodiscard]] constexpr Mode_Privilege ierCmExtractMode(uint64_t value) noexcept
{
    return static_cast<Mode_Privilege>((value >> kIerCmModeShift) & uint64_t{0x3});
}

// Mask out CM bits, leaving only the IER portion of a raw IER_CM data
// word.  Used when storing into cpu.ier.
[[nodiscard]] constexpr uint64_t ierCmIerPortion(uint64_t value) noexcept
{
    return value & ~kIerCmModeMask;
}

// Compose a combined IER_CM data word from a stored IER value (with
// CM bits already cleared) and a Mode_Privilege CM value.  Used by
// HW_MFPR HW_IER_CM to return the combined view to the firmware.
[[nodiscard]] constexpr uint64_t ierCmCompose(uint64_t ierStorage,
                                              Mode_Privilege mode) noexcept
{
    return ierCmIerPortion(ierStorage)
         | ((static_cast<uint64_t>(mode) & uint64_t{0x3}) << kIerCmModeShift);
}


// ---------------------------------------------------------------------------
// SIRR / ISUM -- HRM Sections 5.2.10 / 5.2.11 (SPEC-SIRR-AST-001).
// ---------------------------------------------------------------------------
//
// Field positions per apisrm ev6_defs.mar (verified 2026-07-31, spec
// Sec 8.2):
//
//   SIRR: SIR<15:1> at [28:14]; all other bits reserved (never stored).
//   ISUM: EI [38:33], SL [32], CR [31], PC [30:29], SI [28:14],
//         ASTU [10], ASTS [9], ASTE [4], ASTK [3].
//
// ISUM is architecturally read-only and DERIVED.  EmulatR splits it:
// the HARDWARE causes (EI/SL/CR/PC, kIsumHwMask) are staged into
// cpu.isum by the divert paths; the SOFTWARE causes (SI + AST) are
// composed LIVE from SIRR/IER/PCTX state at read time -- the guest
// PAL clears SIRR and expects the next ISUM read to reflect it.
//
// Derivation (HRM 5.2.10 / Table 5-7):
//   SI[n]  = SIRR[n] AND IER.SIEN[n]      (both registers at [28:14])
//   AST[m] = ASTRR[m] AND ASTER[m] AND IER.ASTEN AND (CM >= m)
//     where m = 0 K, 1 E, 2 S, 3 U.  "CM >= m" is the less-privileged-
//     or-equal rule: a kernel AST is deliverable from any mode; a user
//     AST only while already in user mode (AARM Part II-A).
//
// ASTRR/ASTER storage: cpu.asten_sr, the HWPCB-layout composite --
// ASTSR (request) nibble at <7:4>, ASTEN (enable) nibble at <3:0>,
// K = LSB of each nibble (same order as the PCTX ASTRR[12:9] /
// ASTER[8:5] fields; see composePctx in PalEntries.cpp -- single
// source of truth, no separate IPR storage).

constexpr uint64_t kSirrSirMask = uint64_t{0x1FFFC000};  // SIR<15:1> at [28:14]
constexpr uint64_t kIerAstenBit = uint64_t{1} << 13;     // IER ASTEN (master gate)

// Staged-hardware portion of ISUM: EI [38:33] | SL [32] | CR [31] | PC [30:29].
constexpr uint64_t kIsumHwMask  = uint64_t{0x7FE0000000};

// ISUM AST summary bit positions, indexed by mode m = 0 K, 1 E, 2 S, 3 U.
constexpr uint64_t kIsumAstBit[4] = {
    uint64_t{1} << 3,    // ASTK
    uint64_t{1} << 4,    // ASTE
    uint64_t{1} << 9,    // ASTS
    uint64_t{1} << 10,   // ASTU
};

// Software (SI + AST) portion of ISUM, derived live per the rules
// above.  astenSr is the cpu.asten_sr composite (ASTSR<7:4> request,
// ASTEN<3:0> enable).  Pure; no CpuState dependency by convention --
// coreLib::pendingSoftInt (CpuState.h) is the state-taking wrapper.
[[nodiscard]] constexpr uint64_t composeIsumSw(uint64_t sirr,
                                               uint64_t ier,
                                               uint64_t astenSr,
                                               Mode_Privilege mode) noexcept
{
    uint64_t sw = sirr & ier & kSirrSirMask;
    if ((ier & kIerAstenBit) != 0) {
        // Per-mode pending = request AND enable, K at bit 0.
        uint64_t const pend = (astenSr >> 4) & astenSr & uint64_t{0xF};
        uint8_t  const cm   = static_cast<uint8_t>(mode);
        for (uint8_t m = 0; m <= cm && m < 4; ++m) {
            if ((pend >> m) & 1u) {
                sw |= kIsumAstBit[m];
            }
        }
    }
    return sw;
}

// Full ISUM view: staged hardware causes OR'd with the live software
// portion.  HW_MFPR HW_ISUM returns this (spec D2).
[[nodiscard]] constexpr uint64_t composeIsum(uint64_t stagedIsum,
                                             uint64_t sw) noexcept
{
    return (stagedIsum & kIsumHwMask) | sw;
}


// ---------------------------------------------------------------------------
// PAL_BASE -- HRM Section 5.2.13.
// ---------------------------------------------------------------------------
//
// PAL_BASE is a read/write IPR (scbd 0x0110) containing the base
// physical address of PALcode.  Bit layout:
//
//   bits [63:44]  RAZ/MBZ -- read as zero, must be zero on write
//   bits [43:15]  PAL_BASE physical address
//   bits [14:0]   RAZ/MBZ -- enforces 32 KiB alignment
//
// Reset value: zero (HRM: "contents are cleared by chip reset").
//
// V4 stores PAL_BASE in cpu.palBase.  Two places need the mask:
//
//   1.  HW_MTPR HW_PAL_BASE: incoming opB must be RAZ/MBZ-masked
//       before storage so a subsequent HW_MFPR reads architecturally
//       correct zeros in the reserved bit positions.
//   2.  CALL_PAL entry vector computation (Ev6EntryVectors.h
//       computeCallPalEntry) masks low 15 bits with kPalBaseAlignMask
//       for alignment.  That mask is the same as kPalBaseLowMask
//       below; the cross-reference is documented in Ev6EntryVectors.h.
//
// Note: V4's Step D PAL relocation writes palBase directly without
// going through HW_MTPR; that path also gets the mask via the same
// helper.

constexpr uint64_t kPalBaseAddrMask = uint64_t{0x000007FFFFFF8000};
constexpr uint64_t kPalBaseLowMask  = uint64_t{0x7FFF};            // 32 KiB align
constexpr uint64_t kPalBaseHighMask = ~uint64_t{0x00000FFFFFFFFFFF}; // bits 63:44

// Apply HRM 5.2.13 RAZ/MBZ rules to a candidate PAL_BASE value.
// Clears bits [63:44] and [14:0]; keeps bits [43:15].
[[nodiscard]] constexpr uint64_t palBaseSanitize(uint64_t value) noexcept
{
    return value & kPalBaseAddrMask;
}

// True if the candidate value is already RAZ/MBZ-clean.
[[nodiscard]] constexpr bool palBaseIsCanonical(uint64_t value) noexcept
{
    return (value & ~kPalBaseAddrMask) == 0;
}


// ---------------------------------------------------------------------------
// I_CTL (Ibox Control Register) -- HRM Section 5.2.14.
// ---------------------------------------------------------------------------
//
// IPR scbd 0x0111.  Contents cleared by chip reset.
//
// Field layout (per HRM 5.2.14 bit diagram):
//
//   bit  0       SPCE         -- System Performance Counter Enable
//   bits 2:1     IC_ENABLE    -- I-cache set enables (2 bits)
//   bits 5:3     SPE<2:0>     -- Super page mode enables (3 bits)
//   bits 7:6     SDE<1:0>     -- PAL shadow register enables
//                                  SDE<0>: R8-R11 + R24-R27 shadowed
//                                  SDE<1>: R4-R7  + R20-R23 shadowed (V4 supports)
//                                  Both may be set simultaneously
//   bits 9:8     SBE<1:0>     -- Stream Buffer Enable
//   bits 11:10   BP_MODE<1:0> -- Branch prediction mode
//   bit  12      HWE          -- Hardware Enable (allow PALRES in kernel)
//   bit  13      FBTP         -- Force Bad Tag Parity
//   bit  14      FBDP         -- Force Bad Data Parity
//   bit  15      VA_48        -- VA format: clear=43-bit, set=48-bit
//   bit  16      VA_FORM_32   -- IVA_FORM 32-bit layout
//   bit  17      SINGLE_ISSUE_L
//   bit  18      PCT0_EN      -- Performance counter 0 enable
//   bit  19      PCT1_EN      -- Performance counter 1 enable
//   bit  20      CALL_PAL_R23 -- 1: CALL_PAL linkage = R23 (use with SDE<1>)
//                                0: CALL_PAL linkage = R27 (use with SDE<0>)
//   bit  21      MCHK_EN      -- Machine check enable
//   bit  22      TB_MB_EN     -- TB-fill ordering / MB synthesis
//   bits 28:23   CHIP_ID<5:0> -- (RO) EV6 part revision; pass 1 = 0b000001
//   bits 29      RES (IGN)
//   bits 47:30   VPTB<47:30>  -- Virtual Page Table Base (I-stream side)
//   bits 63:48   SEXT(VPTB<47>) -- Sign extension of VPTB<47>
//
// V4's translator currently consults cpu.va_ctl + cpu.i_spe (separate
// duplicate fields) -- but the canonical state per HRM lives in I_CTL
// for the I-stream side.  Future cleanup should remove the duplicate
// fields and consume I_CTL bits directly via these accessors.
//
// V1 ref: D:\EmulatR\EmulatRAppUni\coreLib\global_RegisterMaster_hot.h
// struct I_Ctl with named bit accessors.  V4 mirrors the field set
// here with the V4 naming convention.

// Field positions
constexpr uint64_t kICtlSpceBit         = uint64_t{1} << 0;
constexpr uint64_t kICtlIcEnableMask    = uint64_t{0x3} << 1;
constexpr uint64_t kICtlSpeMask         = uint64_t{0x7} << 3;
constexpr uint64_t kICtlSdeMask         = uint64_t{0x3} << 6;
constexpr uint64_t kICtlSdeLowBit       = uint64_t{1} << 6;   // SDE<0>
constexpr uint64_t kICtlSdeHighBit      = uint64_t{1} << 7;   // SDE<1>
constexpr uint64_t kICtlSbeMask         = uint64_t{0x3} << 8;
constexpr uint64_t kICtlBpModeMask      = uint64_t{0x3} << 10;
constexpr uint64_t kICtlHweBit          = uint64_t{1} << 12;
constexpr uint64_t kICtlFbtpBit         = uint64_t{1} << 13;
constexpr uint64_t kICtlFbdpBit         = uint64_t{1} << 14;
constexpr uint64_t kICtlVa48Bit         = uint64_t{1} << 15;
constexpr uint64_t kICtlVaForm32Bit     = uint64_t{1} << 16;
constexpr uint64_t kICtlSingleIssueLBit = uint64_t{1} << 17;
constexpr uint64_t kICtlPct0EnBit       = uint64_t{1} << 18;
constexpr uint64_t kICtlPct1EnBit       = uint64_t{1} << 19;
constexpr uint64_t kICtlCallPalR23Bit   = uint64_t{1} << 20;
constexpr uint64_t kICtlMchkEnBit       = uint64_t{1} << 21;
constexpr uint64_t kICtlTbMbEnBit       = uint64_t{1} << 22;
constexpr uint64_t kICtlChipIdMask      = uint64_t{0x3F} << 23;
constexpr uint64_t kICtlVptbLowMask     = uint64_t{0x3FFFF} << 30;   // bits 47:30
// VPTB sign-extension lives in bits [63:48] mirroring bit 47.

// SDE accessors -- PAL shadow register enable
[[nodiscard]] constexpr bool iCtlSdeLow(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlSdeLowBit) != 0;     // R8-R11 + R24-R27 shadow
}

[[nodiscard]] constexpr bool iCtlSdeHigh(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlSdeHighBit) != 0;    // R4-R7 + R20-R23 shadow
}

[[nodiscard]] constexpr bool iCtlSdeEnabled(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlSdeMask) != 0;       // either bit
}

// CALL_PAL linkage register selection.
// HRM: when set, linkage = R23; clear, linkage = R27.  Should
// correspond to SDE so the linkage register is a PAL shadow.
[[nodiscard]] constexpr bool iCtlCallPalUsesR23(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlCallPalR23Bit) != 0;
}

// Returns the register index (R23 or R27) the firmware has chosen
// as the CALL_PAL linkage register, per CALL_PAL_R23 bit.
[[nodiscard]] constexpr uint8_t iCtlCallPalLinkageReg(uint64_t iCtl) noexcept
{
    return iCtlCallPalUsesR23(iCtl) ? uint8_t{23} : uint8_t{27};
}

// Superpage Mode Enable -- 3-bit field.
[[nodiscard]] constexpr uint8_t iCtlSpe(uint64_t iCtl) noexcept
{
    return static_cast<uint8_t>((iCtl >> 3) & uint64_t{0x7});
}

// VA format selectors.
[[nodiscard]] constexpr bool iCtlIsVa48(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlVa48Bit) != 0;
}

[[nodiscard]] constexpr bool iCtlIsVaForm32(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlVaForm32Bit) != 0;
}

// Machine check enable.
[[nodiscard]] constexpr bool iCtlMchkEn(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlMchkEnBit) != 0;
}

// Hardware enable (allow PALRES instructions in kernel mode).
[[nodiscard]] constexpr bool iCtlHweEnabled(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlHweBit) != 0;
}

// TB MB ordering enable.
[[nodiscard]] constexpr bool iCtlTbMbEn(uint64_t iCtl) noexcept
{
    return (iCtl & kICtlTbMbEnBit) != 0;
}

// Chip ID (read-only).  EV6 pass 1 = 0b000001.
[[nodiscard]] constexpr uint8_t iCtlChipId(uint64_t iCtl) noexcept
{
    return static_cast<uint8_t>((iCtl >> 23) & uint64_t{0x3F});
}

// I-stream VPTB.  Returns the full sign-extended virtual page table
// base address.  Bits 47:30 are stored; bits 63:48 are SEXT(bit 47).
[[nodiscard]] constexpr uint64_t iCtlVptb(uint64_t iCtl) noexcept
{
    uint64_t const lower = iCtl & kICtlVptbLowMask;
    // Sign-extend from bit 47: if bit 47 set, fill bits 63:48 with 1s.
    if ((iCtl & (uint64_t{1} << 47)) != 0) {
        return lower | uint64_t{0xFFFF000000000000};
    }
    return lower;
}

// VA_FORM / IVA_FORM value (HRM 5.1.5, Figures 5-5/5-6/5-7).  Returns the
// virtual address of the PTE that maps `faultVa` in the self-mapped page
// table.  There are THREE forms, selected by VA_CTL/I_CTL[VA_48] and
// [VA_FORM_32]; each places a different VPTB slice, a different VPN width,
// and (48-bit only) a sign-extension segment:
//
//   Fig 5-5  VA_48=0, VA_FORM_32=0  (43-bit OSF/VMS default)
//       [63:33]=VPTB[63:33]  [32:3]=VA[42:13]        [2:0]=0
//   Fig 5-6  VA_48=1, VA_FORM_32=0  (48-bit)  -- THREE fields
//       [63:43]=VPTB[63:43]  [42:38]=SEXT(VA[47])  [37:3]=VA[47:13]  [2:0]=0
//   Fig 5-7  VA_48=0, VA_FORM_32=1  (NT 32-bit)
//       [63:30]=VPTB[63:30]  [21:3]=VA[31:13]        [2:0]=0
//
// `vptb` is passed in its full register position (va_ctl for the D-side,
// iCtlVptb(i_ctl) for the I-side); this function applies the per-form VPTB
// mask, so control bits below the mask are stripped here -- do NOT pre-mask
// at the call site.  Applying the correct per-form VPTB mask also fixes the
// former [63:30] overlap into the VPN field (latent for VPTB<32:30> != 0).
//
// The 48-bit SEXT(VA[47]) segment is trace-confirmed only for VA[47]=0 (the
// ES40 4GB memory-fill self-map miss, cyc 250M, va=0x080103fb2000); the
// VA[47]=1 half is exercised by doctest vector V2, not yet by a live trace.
// PALcode reads VA_FORM (D-side, faulting VA) or IVA_FORM (I-side, EXC_ADDR)
// on a TB miss to locate and load the PTE.
[[nodiscard]] constexpr uint64_t computeVaForm(uint64_t vptb,
                                               uint64_t faultVa,
                                               bool     form32,
                                               bool     va48) noexcept
{
    if (form32) {
        // Fig 5-7: VPTB[63:30] : VA[31:13] -> bits[21:3].
        return (vptb & uint64_t{0xFFFFFFFFC0000000})
             | ((faultVa >> 10) & uint64_t{0x00003FFFF8});
    }
    if (va48) {
        // Fig 5-6: VPTB[63:43] : SEXT(VA[47])<42:38> : VA[47:13] -> bits[37:3].
        uint64_t const sext = ((faultVa >> 47) & uint64_t{1}) != 0
                                ? uint64_t{0x000007C000000000}   // bits[42:38] = 1
                                : uint64_t{0};
        return (vptb & uint64_t{0xFFFFF80000000000})
             | sext
             | ((faultVa >> 10) & uint64_t{0x0000003FFFFFFFF8});
    }
    // Fig 5-5: VPTB[63:33] : VA[42:13] -> bits[32:3].
    return (vptb & uint64_t{0xFFFFFFFE00000000})
         | ((faultVa >> 10) & uint64_t{0x00000001FFFFFFF8});
}

} // namespace coreLib

#endif // CORELIB_IPR_FIELDS_H
