// ============================================================================
// pteLib/Ev6PteFormat.h -- ITB_PTE / DTB_PTE IPR format -> canonical AlphaPte
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
// The EV6 ITB_PTE and DTB_PTE *register* formats (written by HW_MTPR during
// software TB fill) differ from the canonical in-memory PTE layout that
// pteLib/AlphaPte.h stores.  This header decodes the register format into a
// canonical AlphaPte for SPAMShardManager::insert().
//
// Authority: bit offsets are HRM-AUTHORITATIVE, confirmed by Tim 2026-05-20
// from the 21264/EV67 HRM (ITB_PTE Sec 5.2.2, DTB_PTE Sec 5.3.2).  AXPBox
// (src/AlphaCPU.cpp add_tb_d / add_tb_i) was a cross-check and agrees on the
// PFN positions, but the HRM offsets are the spec of record.  V1's converter
// is NOT a reference -- it had the PFN at the wrong bit for DTB and its own
// comments contradicted its code.
//
// IMPORTANT difference between the two register formats:
//   * ITB_PTE holds PFN (PA[43:13]) at its NATURAL position [43:13].
//   * DTB_PTE holds PFN (PA[43:13]) UP at register bits [62:32].
//   That PFN relocation is the sole structural difference; the low-bit
//   control/permission positions coincide with canonical AlphaPte bits.
//
// DTB_PTE0 / DTB_PTE1 register bit layout (HRM Sec 5.3.2, authoritative):
//
//   bits 62:32  PA[43:13]  -- PFN; canonical PFN = reg >> 32
//   bit  15     UWE   bit 14  SWE   bit 13  EWE   bit 12  KWE
//   bit  11     URE   bit 10  SRE   bit  9  ERE   bit  8  KRE
//   bit   7     IGN
//   bits 6:5    GH<1:0>
//   bit   4     ASM
//   bit   3     IGN   <-- NOT FOE.  The DTB register has no fault-on-execute.
//   bit   2     FOW
//   bit   1     FOR
//   bit   0     IGN
//
// ITB_PTE register bit layout (HRM Sec 5.2.2, authoritative):
//
//   bits 43:13  PFN  (PA[43:13], natural position); canonical PFN = reg >> 13
//   bit  12     IGN
//   bit  11     URE   bit 10  SRE   bit  9  ERE   bit  8  KRE   (read only)
//   bit   7     IGN
//   bits 6:5    GH<1:0>
//   bit   4     ASM
//   bits 3:0    IGN  (no FOR/FOW/FOE on the I-side ITB_PTE register)
//
// The retained low-bit fields land at the SAME positions as the canonical
// AlphaPte (FOR=1, FOW=2, FOE=3, ASM=4, GH=6:5, KRE..UWE=8:15), so they are
// copied straight across; only the PFN field is relocated per the difference
// noted above.
//
// PFN width note: canonical AlphaPte PFN is 32 bits ([63:32]); the register
// PA[43:13] is 31 bits.  The earlier 28-bit canonical width assumed the
// high PFN bits were only needed for >2 TB DRAM and could be left clipped.
// CORRECTED 2026-05-28: the Tsunami chipset I/O window also needs the high
// PFN bits -- specifically PA bit 43 (= PFN bit 30) which selects the
// chipset/Pchip MMIO window at PA 0x800_0000_0000.  Clipping PFN to 28
// bits stripped bit 30, mapping every native LDx/STx of 0x801_FC00_xxxx
// (Pchip0 dense I/O, including COM1 LSR/RBR at offsets 0x3FD/0x3F8) down
// to 0x001_FC00_xxxx and into TsunamiChipset::reportNxm.  The widened
// 32-bit field covers all 31 silicon PA[43:13] bits plus the bit-63
// software-flag slot some OS PALs use in the in-memory PTE.
//
// A TB fill installs a valid translation by construction -- PALcode only
// reaches HW_MTPR ITB_PTE/DTB_PTE after confirming V in the memory PTE -- so
// both decoders set the canonical V bit unconditionally.
//
// ============================================================================

#ifndef PTELIB_EV6_PTE_FORMAT_H
#define PTELIB_EV6_PTE_FORMAT_H

#include <cstdint>

#include "pteLib/AlphaPte.h"

namespace pteLib {


// Shared low-16 field masks (positions identical to canonical AlphaPte).
//   DTB: FOR[1], FOW[2], ASM[4], GH[6:5], KRE..UWE[8:15].  Bits 0, 3, 7 are
//        IGN -- in particular bit 3 is NOT FOE on the DTB register.
//   ITB: ASM[4], GH[6:5], KRE..URE[8:11] (read enables only).
constexpr uint64_t kDtbPteLowFieldMask = uint64_t{0xFF76}; // 1,2,4,5,6,8..15
constexpr uint64_t kItbPteLowFieldMask = uint64_t{0x0F70}; // 4,5,6,8..11


// Decode a DTB_PTE0/DTB_PTE1 register value into a canonical AlphaPte.
[[nodiscard]] inline AlphaPte canonicalFromDtbPte(uint64_t reg) noexcept
{
    uint64_t const pfnMask = (uint64_t{1} << AlphaPteBits::kPfnWidth) - 1;
    uint64_t const pfn     = (reg >> 32) & pfnMask;          // PA[43:13] at [62:32]
    uint64_t const low     = reg & kDtbPteLowFieldMask;      // FOR,FOW,ASM,GH,R/W

    AlphaPte p;
    p.raw = low
          | (pfn << AlphaPteBits::kPfnLsb)
          | pteBit(AlphaPteBits::kV);                        // valid by construction
    return p;
}


// Decode an ITB_PTE register value into a canonical AlphaPte.
[[nodiscard]] inline AlphaPte canonicalFromItbPte(uint64_t reg) noexcept
{
    uint64_t const pfnMask = (uint64_t{1} << AlphaPteBits::kPfnWidth) - 1;
    uint64_t const pfn     = (reg >> 13) & pfnMask;          // PA[43:13] natural pos
    uint64_t const low     = reg & kItbPteLowFieldMask;      // ASM,GH,read enables

    AlphaPte p;
    p.raw = low
          | (pfn << AlphaPteBits::kPfnLsb)
          | pteBit(AlphaPteBits::kV);                        // valid by construction
    return p;
}


} // namespace pteLib

#endif // PTELIB_EV6_PTE_FORMAT_H
