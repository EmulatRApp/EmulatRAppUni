// ============================================================================
// Ev6EntryVectors.h -- canonical EV6 PALcode entry-vector offsets
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
// EV6/21264 PALcode entry-vector layout.  All offsets are relative to
// palBase (the value loaded into the PAL_BASE IPR via HW_MTPR; bits
// [14:0] must be zero -- palBase is 32K-aligned).
//
// The architectural address of any PALcode handler is:
//
//     handler_pa = (palBase & ~0x7FFFULL) | offset
//
// Source: D:\EmulatR\Processor Support\Palcode\palcode\palcode\palcode\src\
//         ev6_defs.mar (Digital reference)
//         alpha-21164-hw-ref-manual.pdf  (general EV5/EV6 layout)
//         21264ev6_hrm.pdf Section 6.8.1 (CALL_PAL entry calculation)
//
// Layout map:
//
//   0x000          (zero-page reserved -- not a vector)
//   0x100..0x780   Hardware exception vectors, 0x80 (128 byte) spacing
//                  Each handler stub is up to 32 instructions before
//                  the next vector.
//   0x2000..0x2FC0 Privileged CALL_PAL entries (functions 0x00-0x3F),
//                  0x40 (64 byte / 16 instruction) spacing per entry
//   0x3000..0x3FC0 Unprivileged CALL_PAL entries (functions 0x80-0xBF),
//                  also 0x40 spacing
//
// Compute formulae:
//
//   Privileged CALL_PAL func F (0x00-0x3F):
//     offset = 0x2000 + F * 0x40   ==   0x2000 | (F << 6)
//
//   Unprivileged CALL_PAL func F (0x80-0xBF):
//     offset = 0x3000 + (F & 0x3F) * 0x40   ==   0x3000 | ((F & 0x3F) << 6)
//
// Both forms are used by V4's execCallPalDispatch (palBoxLib/grains/
// PalEntries.cpp).  Spot-checked against ev6_defs.mar:
//   CALL_PAL_00 -> 0x2000  (HALT)
//   CALL_PAL_05 -> 0x2140  (SWPCTX)
//   CALL_PAL_29 -> 0x2A40  (MFPR_VPTB)
//   CALL_PAL_86 -> 0x3180  (IMB)
// ============================================================================

#ifndef EMULATR_CORELIB_EV6_ENTRY_VECTORS_H
#define EMULATR_CORELIB_EV6_ENTRY_VECTORS_H

#include <cstdint>

namespace coreLib {
namespace ev6 {

// ----------------------------------------------------------------------------
// Hardware exception entry vectors (0x100..0x780).
// Source: ev6_defs.mar.
//
// V4 status (as of this commit): hardware-trap delivery is NOT yet
// implemented.  When the pipeline raises a fault (kFaultUnaligned,
// kFaultUnimplemented, etc.) it currently terminates the run rather
// than diverting to the corresponding PALcode entry.  When trap
// delivery lands in pipelineLib, these constants are the divert-target
// table that maps fault-class -> entry-vector offset.
// ----------------------------------------------------------------------------
constexpr uint64_t kEntry_DTBM_DOUBLE_3  = 0x100;  // DTB miss, double, level 3
constexpr uint64_t kEntry_DTBM_DOUBLE_4  = 0x180;  // DTB miss, double, level 4
constexpr uint64_t kEntry_FEN            = 0x200;  // FP enable / FP disabled trap
constexpr uint64_t kEntry_UNALIGN        = 0x280;  // unaligned access trap
constexpr uint64_t kEntry_DTBM_SINGLE    = 0x300;  // DTB miss, single
constexpr uint64_t kEntry_DFAULT         = 0x380;  // data fault (access viol / FOR / FOW)
constexpr uint64_t kEntry_OPCDEC         = 0x400;  // illegal/reserved opcode
constexpr uint64_t kEntry_IACV           = 0x480;  // instruction access violation
constexpr uint64_t kEntry_MCHK           = 0x500;  // machine check
constexpr uint64_t kEntry_ITB_MISS       = 0x580;  // ITB miss
constexpr uint64_t kEntry_ARITH          = 0x600;  // arithmetic trap (FP, integer overflow)
constexpr uint64_t kEntry_INTERRUPT      = 0x680;  // external interrupt delivery
constexpr uint64_t kEntry_MT_FPCR        = 0x700;  // MT_FPCR instruction handler
constexpr uint64_t kEntry_RESET          = 0x780;  // reset entry (power-on, warm restart)

// Hardware-exception vector spacing: 128 bytes per handler stub.
constexpr uint64_t kHwExceptionVectorSpacing = 0x80;

// ----------------------------------------------------------------------------
// CALL_PAL dispatch bases and spacing.  Use these instead of hard-coded
// 0x2000/0x3000/0x40 in dispatch logic.
// Source: ev6_defs.mar (CALL_PAL_NN_ENTRY values), 21264 HRM Section 6.8.1.
// ----------------------------------------------------------------------------
constexpr uint64_t kCallPalPrivilegedBase   = 0x2000;
constexpr uint64_t kCallPalUnprivilegedBase = 0x3000;
constexpr uint64_t kCallPalEntrySpacing     = 0x40;
constexpr uint64_t kCallPalEntryShift       = 6;     // log2(kCallPalEntrySpacing)

// ----------------------------------------------------------------------------
// palBase alignment mask.  palBase is 32K-aligned (low 15 bits zero); a
// dispatch target is built by ORing the offset into the masked palBase.
// ----------------------------------------------------------------------------
constexpr uint64_t kPalBaseAlignMask = ~uint64_t{0x7FFF};

// ----------------------------------------------------------------------------
// Compute the absolute PA of a CALL_PAL entry given palBase and the
// 26-bit CALL_PAL function code (encoded[25:0]).  Bit 7 of the function
// code selects privileged (=0) vs unprivileged (=1) base.
// ----------------------------------------------------------------------------
constexpr uint64_t computeCallPalEntry(uint64_t palBase, uint32_t funcCode) noexcept {
    bool const isUnprivileged = (funcCode & 0x80u) != 0;
    uint64_t const base = isUnprivileged ? kCallPalUnprivilegedBase
                                          : kCallPalPrivilegedBase;
    uint64_t const idx = (funcCode & 0x3Fu);
    return (palBase & kPalBaseAlignMask) | base | (idx << kCallPalEntryShift);
}

// ----------------------------------------------------------------------------
// Compute the absolute PA of a hardware-exception entry given palBase
// and the offset constant (one of kEntry_*).
// ----------------------------------------------------------------------------
constexpr uint64_t computeHwExceptionEntry(uint64_t palBase, uint64_t entryOffset) noexcept {
    return (palBase & kPalBaseAlignMask) | entryOffset;
}

// ----------------------------------------------------------------------------
// EV6 / EV67 / EV68 / EV7 chip-ID values (the "chip ID" register field).
// Source: ev6_defs.mar (EV6__CHIP_ID_*).
// ----------------------------------------------------------------------------
namespace ChipId {
constexpr uint64_t Ev6_Pass1     = 0x0;
constexpr uint64_t Ev6_Pass2     = 0x1;
constexpr uint64_t Ev6_Pass2_2   = 0x2;
constexpr uint64_t Ev6_Pass2_3   = 0x3;
constexpr uint64_t Ev6_Pass3     = 0x4;
constexpr uint64_t Ev67_Pass1    = 0x8;
constexpr uint64_t Ev68_Pass1    = 0x10;
constexpr uint64_t Ev7_Pass1     = 0x18;
}  // namespace ChipId

// ----------------------------------------------------------------------------
// Verification: a few well-known entries computed at compile time.
// These match ev6_defs.mar's CALL_PAL_NN_ENTRY constants when palBase=0,
// and lock the formula against drift.
// ----------------------------------------------------------------------------
static_assert(computeCallPalEntry(0, 0x00) == 0x2000, "CALL_PAL HALT at 0x2000");
static_assert(computeCallPalEntry(0, 0x01) == 0x2040, "CALL_PAL CFLUSH at 0x2040");
static_assert(computeCallPalEntry(0, 0x05) == 0x2140, "CALL_PAL SWPCTX at 0x2140");
static_assert(computeCallPalEntry(0, 0x09) == 0x2240, "CALL_PAL CSERVE at 0x2240");
static_assert(computeCallPalEntry(0, 0x29) == 0x2A40, "CALL_PAL MFPR_VPTB at 0x2A40");
static_assert(computeCallPalEntry(0, 0x3F) == 0x2FC0, "CALL_PAL MFPR_WHAMI/RTI at 0x2FC0");
static_assert(computeCallPalEntry(0, 0x80) == 0x3000, "CALL_PAL BPT at 0x3000");
static_assert(computeCallPalEntry(0, 0x86) == 0x3180, "CALL_PAL IMB at 0x3180");
static_assert(computeCallPalEntry(0, 0xAA) == 0x3A80, "CALL_PAL GENTRAP at 0x3A80");
static_assert(computeCallPalEntry(0, 0xAE) == 0x3B80, "CALL_PAL CLRFEN at 0x3B80");

// Verify palBase-OR with a non-zero palBase preserves high bits.
static_assert(computeCallPalEntry(0x600000, 0x00) == 0x602000, "palBase 0x600000 + HALT -> 0x602000");
static_assert(computeCallPalEntry(0x600000, 0x29) == 0x602A40, "palBase 0x600000 + MFPR_VPTB -> 0x602A40 (matches V4 test)");
static_assert(computeCallPalEntry(0x600000, 0x86) == 0x603180, "palBase 0x600000 + IMB -> 0x603180 (matches V4 test)");

// Verify hardware exception entries.
static_assert(computeHwExceptionEntry(0, kEntry_RESET)    == 0x780, "RESET at palBase+0x780");
static_assert(computeHwExceptionEntry(0, kEntry_MCHK)     == 0x500, "MCHK at palBase+0x500");
static_assert(computeHwExceptionEntry(0, kEntry_OPCDEC)   == 0x400, "OPCDEC at palBase+0x400");
static_assert(computeHwExceptionEntry(0, kEntry_UNALIGN)  == 0x280, "UNALIGN at palBase+0x280");

// ----------------------------------------------------------------------------
// Sentinel offset returned by entryForFault() for fault codes that do
// NOT correspond to a PALcode entry vector (e.g., kFaultHalt, which
// terminates the emulator deliberately, and kNoFault).  Callers check
// for this before computing a divert target.
// ----------------------------------------------------------------------------
constexpr uint64_t kEntry_None = 0;

// ----------------------------------------------------------------------------
// Map a V4 fault code (coreLib/BoxResult.h kFault* constants) to the
// EV6 PALcode entry-vector offset that should receive control.  Used
// by the future trap-delivery path in pipelineLib: when the WB stage
// observes a non-zero faultCode, it consults this map, computes
// computeHwExceptionEntry(palBase, entryForFault(fc)), and diverts.
//
// V4 faults (BoxResult.h):
//   1  kFaultOpcDec        -> OPCDEC   (illegal/reserved opcode)
//   2  kFaultPrivileged    -> OPCDEC   (priv-only op from non-PAL is OPCDEC on EV6)
//   3  kFaultUnimplemented -> OPCDEC   (V4 internal stub == illegal opcode for guest)
//   4  kFaultUnaligned     -> UNALIGN
//   5  kFaultDtbMiss       -> DTBM_SINGLE   (DOUBLE_3/4 needs walker-state; defer)
//   6  kFaultItbMiss       -> ITB_MISS
//   7  kFaultAcv           -> DFAULT   (data access violation; instr ACV is IACV
//                                       and needs a separate fault code)
//   8  kFaultFor           -> DFAULT   (fault-on-read PTE)
//   9  kFaultFow           -> DFAULT   (fault-on-write PTE)
//  10  kFaultFoe           -> IACV     (fault-on-execute is an instruction fault)
//  11  kFaultBusError      -> MCHK     (bus errors deliver as machine check)
//  12  kFaultNonCanonical  -> DFAULT   (treat as access violation for now)
//  13  kFaultHalt          -> kEntry_None  (deliberate emulator stop, no divert)
//   0  kNoFault            -> kEntry_None  (no trap to deliver)
//
// Limitation: V4 currently does not distinguish data ACV from instruction ACV
// (one kFaultAcv covers both).  Once V4 grows kFaultIacv, swap that branch
// to IACV.  Same story for DTBM_DOUBLE_3 vs _4 (page-walk-depth specific).
// ----------------------------------------------------------------------------
constexpr uint64_t entryForFault(uint16_t faultCode) noexcept {
    // Magic numbers here intentionally mirror BoxResult.h's kFault* values.
    // Keeping the mapping in one switch (rather than including BoxResult.h)
    // avoids a circular include with the leaf-side fault declarations.
    switch (faultCode) {
        case  0: return kEntry_None;          // kNoFault
        case  1: return kEntry_OPCDEC;        // kFaultOpcDec
        case  2: return kEntry_OPCDEC;        // kFaultPrivileged
        case  3: return kEntry_OPCDEC;        // kFaultUnimplemented
        case  4: return kEntry_UNALIGN;       // kFaultUnaligned
        case  5: return kEntry_DTBM_SINGLE;   // kFaultDtbMiss
        case  6: return kEntry_ITB_MISS;      // kFaultItbMiss
        case  7: return kEntry_DFAULT;        // kFaultAcv  (TODO: split data/instr)
        case  8: return kEntry_DFAULT;        // kFaultFor
        case  9: return kEntry_DFAULT;        // kFaultFow
        case 10: return kEntry_IACV;          // kFaultFoe
        case 11: return kEntry_MCHK;          // kFaultBusError
        case 12: return kEntry_DFAULT;        // kFaultNonCanonical
        case 13: return kEntry_None;          // kFaultHalt (no divert; emulator stop)
        case 14: return kEntry_DTBM_DOUBLE_3; // kFaultDtbMissDouble (VPTE-load miss)
        default: return kEntry_OPCDEC;        // unknown -> safest is illegal-opcode
    }
}

// Compile-time spot-checks: V4's well-known fault codes route correctly.
static_assert(entryForFault(0)  == kEntry_None,        "kNoFault routes nowhere");
static_assert(entryForFault(1)  == kEntry_OPCDEC,      "kFaultOpcDec routes to OPCDEC");
static_assert(entryForFault(4)  == kEntry_UNALIGN,     "kFaultUnaligned routes to UNALIGN");
static_assert(entryForFault(5)  == kEntry_DTBM_SINGLE, "kFaultDtbMiss routes to DTBM_SINGLE");
static_assert(entryForFault(11) == kEntry_MCHK,        "kFaultBusError routes to MCHK");
static_assert(entryForFault(13) == kEntry_None,        "kFaultHalt routes nowhere (deliberate stop)");
static_assert(entryForFault(14) == kEntry_DTBM_DOUBLE_3, "kFaultDtbMissDouble routes to DTBM_DOUBLE_3");

}  // namespace ev6
}  // namespace coreLib

#endif  // EMULATR_CORELIB_EV6_ENTRY_VECTORS_H
