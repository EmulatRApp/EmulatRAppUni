// ============================================================================
// coreLib/HW_IPR.h -- Internal Processor Register index enumeration
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
// Numeric indices for all Alpha EV6 internal processor registers,
// consumed by HW_MFPR / HW_MTPR instruction dispatch and (eventually)
// CALL_PAL IPR handlers.  Ported from V1 coreLib/HW_IPR.h with the Qt
// dependency dropped (quint16 -> uint16_t).
//
// TWO NAMESPACES:
//
//   1. PALcode-visible IPRs (0x0000 .. 0x00FF) -- function codes for
//      CALL_PAL MTPR / MFPR instructions.  The OS executes CALL_PAL
//      with one of these codes; the PAL handler at the corresponding
//      vector performs the IPR access.  Numbering per the OSF/1 PAL
//      specification, not by hardware.  PAL_MFPR / PAL_MTPR enums.
//
//   2. EV6 Hardware IPRs (0x0100 .. 0x02FF) -- actual hardware
//      register indices encoded in the scbd field of HW_MFPR /
//      HW_MTPR instructions.  Only PALcode (running in PAL mode) can
//      execute these.  Numbering per the 21264 Hardware Reference
//      Manual.
//
//      The 0x0100 offset is an emulator convention to avoid numeric
//      collisions with the PALcode-visible range.  The instruction
//      decoder adds 0x0100 to the raw scbd field; the PAL_TEMP range
//      (raw scbd 0x40..0x57) instead adds 0x01C0 so PT0..PT23 land
//      at 0x0200..0x0217.
//
// PAL_TEMP REGISTERS:
//
//   The EV6 provides 24 PAL temporary registers (PT0..PT23) accessible
//   only via HW_MFPR / HW_MTPR in PAL mode.  Backbone of PALcode --
//   every PAL entry saves GPRs to PAL_TEMPs, every PAL exit restores
//   them.  PALcode convention (not hardware) assigns specific PAL_TEMPs
//   to architectural state exposed via CALL_PAL handlers; OSF/1
//   assignments documented inline at PT0..PT11.
//
//   The OS never accesses PAL_TEMPs directly; it uses CALL_PAL MFPR /
//   MTPR with PALcode-visible codes (e.g., MFPR_PTBR = 0x0015) and
//   the PAL handler translates that to HW_MFPR from the appropriate
//   PAL_TEMP.
//
// REFERENCES:
//   Alpha 21264 Hardware Reference Manual (EC-RE2CA-TE), Table 5-4
//   Alpha Architecture Handbook, Section 6.5 (CALL_PAL)
//   OSF/1 PALcode Specification (PAL_TEMP assignments)
//
// V4 SCOPE NOTE:
//   The full enum is ported for forward compatibility, but v1's
//   HW_MFPR / HW_MTPR leaves only switch over the subset that maps
//   to fields currently in CpuState.  Selectors that are not
//   CpuState-backed return kFaultUnimplemented; adding storage
//   later is a CpuState extension plus a switch case.
//
// ============================================================================

#ifndef CORELIB_HW_IPR_H
#define CORELIB_HW_IPR_H

#include <cstdint>

namespace coreLib {

// ---------------------------------------------------------------------------
// PAL_MFPR / PAL_MTPR -- PALcode-visible IPR function codes (CALL_PAL).
// ---------------------------------------------------------------------------
// The OS executes CALL_PAL with one of these codes; the PAL handler
// performs the IPR access.  Not consumed by HW_MFPR / HW_MTPR; ported
// here so future CALL_PAL handlers have the canonical name set.
enum PAL_MFPR : uint16_t {
    MFPR_ASN     = 0x0006,
    MFPR_IPL     = 0x000E,
    MFPR_MCES    = 0x0010,
    MFPR_PCBB    = 0x0012,
    MFPR_PRBR    = 0x0013,
    MFPR_PTBR    = 0x0015,
    MFPR_SCBB    = 0x0016,
    MFPR_SISR    = 0x0019,
    MFPR_TBCHK   = 0x001A,
    MFPR_ESP     = 0x001E,
    MFPR_SSP     = 0x0020,
    MFPR_USP     = 0x0022,
    MFPR_VPTB    = 0x0029,
    MFPR_SYSPTBR = 0x0032,
    MFPR_VIRBND  = 0x0030,
    MFPR_WHAMI   = 0x003F,
};

enum PAL_MTPR : uint16_t {
    MTPR_IPIR    = 0x000D,
    MTPR_IPL     = 0x000E,
    MTPR_MCES    = 0x0011,
    MTPR_PRBR    = 0x0014,
    MTPR_SCBB    = 0x0017,
    MTPR_SIRR    = 0x0018,
    MTPR_TBIA    = 0x001B,
    MTPR_TBIAP   = 0x001C,
    MTPR_TBIS    = 0x001D,
    MTPR_ESP     = 0x001F,
    MTPR_SSP     = 0x0021,
    MTPR_USP     = 0x0023,
    MTPR_VPTB    = 0x002A,
    MTPR_PERFMON = 0x002B,
    MTPR_DATFX   = 0x002E,
    MTPR_VIRBND  = 0x0031,
    MTPR_SYSPTBR = 0x0033,
};


// ---------------------------------------------------------------------------
// HW_IPR -- EV6 hardware IPR indices for HW_MFPR / HW_MTPR.
// ---------------------------------------------------------------------------
// Raw scbd value from instruction encoding plus 0x0100 offset
// (0x01C0 for PAL_TEMPs).
enum HW_IPR : uint16_t {

    // -----------------------------------------------------------------
    // IBox -- ITB management, exception state, control
    // -----------------------------------------------------------------
    HW_ITB_TAG      = 0x0100,   // ITB Tag staging register (write-only).
    HW_ITB_PTE      = 0x0101,   // ITB PTE register (write triggers fill).
    HW_ITB_IAP      = 0x0102,   // ITB Invalidate All Process (action).
    HW_ITB_IA       = 0x0103,   // ITB Invalidate All (action).
    HW_ITB_IS       = 0x0104,   // ITB Invalidate Single (action).
    HW_ITB_PTE_TEMP_PROVISIONAL = 0x0105,
                                // ITB PTE read-back / staging (HRM 5.2.3).
                                // SCBD PROVISIONAL -- _PROVISIONAL suffix is a
                                // tripwire: do NOT bind HW_MFPR/MTPR decode to
                                // this until verified vs HRM Table 5-2, then
                                // drop the suffix.
    HW_EXC_ADDR     = 0x0106,   // Exception Address; read = saved PC,
                                // write = HW_REI return target.  Bit[0]
                                // = PAL mode of interrupted context.
    HW_IVA_FORM     = 0x0107,   // Instruction VA Format (read-only).
    HW_CM           = 0x0109,   // Current Mode (PS<CM>); 00=K,01=E,10=S,11=U.
    HW_IER          = 0x010A,   // Interrupt Enable Register.
    HW_IER_CM       = 0x010B,   // IER + CM combined write.
    HW_SIRR         = 0x010C,   // Software Interrupt Request Register.
    HW_ISUM         = 0x010D,   // Interrupt Summary (read-only).
    HW_INT_CLR      = 0x010E,   // Hardware Interrupt Clear (write-only).
    HW_EXC_SUM      = 0x010F,   // Exception Summary; FP exception flags.

    HW_PAL_BASE     = 0x0110,   // PAL Base Address.
    HW_I_CTL        = 0x0111,   // IBox Control Register.
    HW_IC_FLUSH_ASM = 0x0112,   // I-cache Flush ASM (action).
    HW_IC_FLUSH     = 0x0113,   // I-cache Flush All (action).
    HW_PCTR_CTL     = 0x0114,   // Performance Counter Control.
    HW_CLR_MAP      = 0x0115,   // Clear Virtual Address Map (action).
    HW_I_STAT       = 0x0116,   // IBox Status (read-only).
    HW_SLEEP        = 0x0117,   // Sleep (action).

    // -----------------------------------------------------------------
    // MBox -- DTB management, MM status, D-cache control
    // -----------------------------------------------------------------
    HW_DTB_TAG0     = 0x0120,   // DTB Tag staging register, bank 0.
    HW_DTB_PTE0     = 0x0121,   // DTB PTE, bank 0 (write triggers fill).
    HW_DTB_PTE_TEMP_PROVISIONAL = 0x0122,
                                // DTB PTE read-back / staging (HRM 5.3.3).
                                // SCBD PROVISIONAL -- _PROVISIONAL suffix is a
                                // tripwire: do NOT bind HW_MFPR/MTPR decode to
                                // this until verified vs HRM Table 5-3, then
                                // drop the suffix.
    HW_DTB_IAP      = 0x01A2,   // DTB Invalidate All Process (action).
    HW_DTB_IA       = 0x01A3,   // DTB Invalidate All (action).
    HW_DTB_IS0      = 0x0124,   // DTB Invalidate Single, bank 0 (action).
    HW_DTB_ASN0     = 0x0125,   // DTB ASN, bank 0.
    HW_DTB_ALTMODE  = 0x0126,   // DTB Alternate Mode for LDx_L / STx_C.
    HW_MM_STAT      = 0x0127,   // Memory Management Status (read-only).
    HW_M_CTL        = 0x0128,   // MBox Control Register.
    HW_DC_CTL       = 0x0129,   // D-cache Control Register.
    HW_DC_STAT      = 0x012A,   // D-cache Status (read-only).
    HW_C_DATA       = 0x012B,
    HW_C_SHFT       = 0x012C,

    // -----------------------------------------------------------------
    // Process Context (packed register)
    // -----------------------------------------------------------------
    HW_PCTX         = 0x0140,   // ASN + FPE + PPCE packed.

    // -----------------------------------------------------------------
    // DTB Bank 1 (dual-bank)
    // -----------------------------------------------------------------
    HW_DTB_TAG1     = 0x01A0,   // DTB Tag staging register, bank 1.
    HW_DTB_PTE1     = 0x01A1,   // DTB PTE, bank 1 (write triggers fill).
    HW_DTB_IS1      = 0x01A4,   // DTB Invalidate Single, bank 1.
    HW_DTB_ASN1     = 0x01A5,   // DTB ASN, bank 1.

    // -----------------------------------------------------------------
    // CBox / Misc
    // -----------------------------------------------------------------
    HW_CC           = 0x01C0,   // Cycle Counter; bits[31:0] count, [63:32] offset.
    HW_CC_CTL       = 0x01C1,   // Cycle Counter Control (offset + enable).
    HW_VA           = 0x01C2,   // Virtual Address (faulting; read-only).
    HW_VA_FORM      = 0x01C3,   // VA Format (read-only, computed).
    HW_VA_CTL       = 0x01C4,   // VA Control (write-only, packed).

    // -----------------------------------------------------------------
    // PAL Temporary Registers PT0..PT31 (HW_PAL_TEMP_n)
    // -----------------------------------------------------------------
    // EV6/21264 architecturally has 24 PAL temps (PT0..PT23, raw scbd
    // 0x40..0x57).  EV5/21164 had 32 (PT0..PT31, raw scbd 0x40..0x5F).
    // SRM .exe builds carry EV5-vintage code that addresses PT24..PT31
    // even when running on EV6 hardware; observed in the 2026-05-09
    // overnight trace at PC 0x12f88 (hw_mtpr Rx, 0x5F = PT31).
    //
    // Provisioning the full 32-entry range makes V4 forward-compatible
    // with both vintages.  PT24..PT31 are CpuState-backed identically to
    // PT0..PT23; the OSF/1 PALcode convention does not assign meaning to
    // them, so they are pure scratch.
    HW_PAL_TEMP_0   = 0x0200,   // PT0  (OSF/1: KSP)
    HW_PAL_TEMP_1   = 0x0201,
    HW_PAL_TEMP_2   = 0x0202,   // PT2  (OSF/1: PTBR)
    HW_PAL_TEMP_3   = 0x0203,
    HW_PAL_TEMP_4   = 0x0204,   // PT4  (OSF/1: PCBB)
    HW_PAL_TEMP_5   = 0x0205,   // PT5  (OSF/1: PRBR)
    HW_PAL_TEMP_6   = 0x0206,   // PT6  (OSF/1: SCBB)
    HW_PAL_TEMP_7   = 0x0207,   // PT7  (OSF/1: USP)
    HW_PAL_TEMP_8   = 0x0208,   // PT8  (OSF/1: SSP)
    HW_PAL_TEMP_9   = 0x0209,   // PT9  (OSF/1: ESP)
    HW_PAL_TEMP_10  = 0x020A,   // PT10 (OSF/1: VIRBND)
    HW_PAL_TEMP_11  = 0x020B,   // PT11 (OSF/1: SYSPTBR)
    HW_PAL_TEMP_12  = 0x020C,
    HW_PAL_TEMP_13  = 0x020D,
    HW_PAL_TEMP_14  = 0x020E,
    HW_PAL_TEMP_15  = 0x020F,
    HW_PAL_TEMP_16  = 0x0210,
    HW_PAL_TEMP_17  = 0x0211,
    HW_PAL_TEMP_18  = 0x0212,
    HW_PAL_TEMP_19  = 0x0213,
    HW_PAL_TEMP_20  = 0x0214,
    HW_PAL_TEMP_21  = 0x0215,
    HW_PAL_TEMP_22  = 0x0216,
    HW_PAL_TEMP_23  = 0x0217,
    HW_PAL_TEMP_24  = 0x0218,   // PT24..PT31: EV5-vintage scratch
    HW_PAL_TEMP_25  = 0x0219,
    HW_PAL_TEMP_26  = 0x021A,
    HW_PAL_TEMP_27  = 0x021B,
    HW_PAL_TEMP_28  = 0x021C,
    HW_PAL_TEMP_29  = 0x021D,
    HW_PAL_TEMP_30  = 0x021E,
    HW_PAL_TEMP_31  = 0x021F,
};


// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

// True when ipr is in the PAL_TEMP range (PT0..PT31).
// V4 provisions the full EV5-vintage 32-entry range; EV6/21264 PALcode
// only addresses PT0..PT23 but the SRM .exe used during bring-up
// references PT31 (raw scbd 0x5F), so the range is widened uniformly.
constexpr bool isPalTemp(HW_IPR ipr) noexcept
{
    return (ipr >= HW_PAL_TEMP_0 && ipr <= HW_PAL_TEMP_31);
}

// Array index 0..31 within the PAL_TEMP range.  Caller's responsibility
// to verify isPalTemp first.
constexpr int palTempIndex(HW_IPR ipr) noexcept
{
    return static_cast<int>(ipr) - static_cast<int>(HW_PAL_TEMP_0);
}

} // namespace coreLib

#endif // CORELIB_HW_IPR_H
