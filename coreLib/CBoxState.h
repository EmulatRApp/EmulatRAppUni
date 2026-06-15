// ============================================================================
// coreLib/CBoxState.h -- EV6 Cbox CSR / IPR shadow state
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
// CBoxState models the Cbox CSR / IPR shadow state per the 21264/EV67
// Hardware Reference Manual section 5.4.  The Cbox is the external
// cache and system-interface unit; PALcode programs its configuration
// during reset and reads back its error state at runtime.
//
// Section 5.4 describes three logically separate shift chains:
//
//   WRITE_ONCE chain  -- 367 bits; loaded by SROM during hardware
//                        reset (after BiST), MSB-first.  Holds the
//                        static system-bus topology / Bcache config
//                        (SYS_CLK_RATIO, BC_SIZE, sysclk timings,
//                        etc).  NOT accessed via HW_MTPR -- the SROM
//                        boot path writes it through a dedicated
//                        hardware shift path.  Not modeled in V4 v1.
//
//   WRITE_MANY chain  -- 36 bits; loaded by PALcode at runtime via
//                        HW_MTPR HW_C_DATA instructions.  Six bits
//                        per write transaction (low 6 bits of opB);
//                        six writes load the full chain.  Holds the
//                        dynamic config PALcode adjusts post-reset.
//
//   ERROR_REG chain   -- 60 bits; read by PALcode via HW_MFPR
//                        HW_C_DATA in combination with HW_MTPR
//                        HW_C_SHFT.  Each MTPR to C_SHFT with the
//                        low bit set shifts 6 bits of ERROR_REG out
//                        into the visible C_DATA register; the
//                        subsequent HW_MFPR HW_C_DATA returns those
//                        6 bits.  Ten read transactions read the
//                        full chain.  Holds chip-error state -- on
//                        a clean run with no errors, all 60 bits
//                        should read zero.
//
// HW_C_DATA (scbd 0x2B, V4 enum HW_IPR::HW_C_DATA = 0x012B) is the
// 6-bit access window over BOTH chains, distinguished by direction:
//
//   HW_MTPR HW_C_DATA -> push into WRITE_MANY
//   HW_MFPR HW_C_DATA -> read from dataReg (loaded by C_SHFT trigger)
//
// HW_C_SHFT (scbd 0x2C, V4 enum HW_IPR::HW_C_SHFT = 0x012C) is the
// W1 (write-1-to-trigger) shift-control register.  Writing 1 to bit 0
// shifts 6 bits out of ERROR_REG into dataReg, making them available
// for the next HW_MFPR HW_C_DATA read.
//
// V4 defaults:
//
//   writeMany = 0  -- chain is empty; populated by PALcode writes
//   errorReg  = 0  -- "no errors found, BIST passed" -- safe default
//                     for a clean simulated boot
//   dataReg   = 0  -- C_DATA register starts cleared
//   shftCtrl  = 0  -- C_SHFT register starts cleared
//
// Refining errorReg later: real EV6 silicon initializes parts of
// ERROR_REG with chip-revision and BIST result bits at reset.  If
// PALcode reads back zeros and gets stuck waiting for a specific
// revision bit, the fix is to populate the corresponding bits of
// errorReg at Machine construction (or at HW reset emulation).
// The cbox_csr.log telemetry will surface the pattern PALcode
// expects, informing what bits to seed.
//
// Pipeline-restriction notes (from HRM Appendix D Restriction 30
// and the PVC tool source at Processor Support/Palcode/palcode/
// fwtools/fwtools/pvc/ev6_rest.c): real silicon requires the
// surrounding instruction stream around an MTPR to the Cbox CSR
// to follow strict icache-alignment + ibox-stall conventions to
// isolate external bus activity from the CSR update.  V4 ignores
// these pipeline scheduling constraints; the CSR effects are
// deterministic regardless of fetch-block alignment.
//
// ============================================================================

#ifndef CORELIB_CBOXSTATE_H
#define CORELIB_CBOXSTATE_H

#include <cstdint>

namespace coreLib {

struct CBoxState
{
    // ------------------------------------------------------------------
    // WRITE_MANY chain (36 bits, kept in low bits of writeMany).
    // ------------------------------------------------------------------
    // Populated by HW_MTPR HW_C_DATA: each write shifts the chain
    // left by 6 bits and ORs in the low 6 bits of the source GPR.
    // Mask to 36 bits on every push so the chain doesn't grow
    // unboundedly across many writes.
    uint64_t writeMany = 0;

    // ------------------------------------------------------------------
    // ERROR_REG chain (60 bits, kept in low bits of errorReg).
    // ------------------------------------------------------------------
    // Source for HW_MFPR HW_C_DATA reads, via HW_MTPR HW_C_SHFT
    // triggers.  V4 default = 0 (no errors).  Seeded with revision/
    // BIST bits later if PALcode reveals it expects specific patterns.
    uint64_t errorReg = 0;

    // ------------------------------------------------------------------
    // C_DATA register (6 bits, visible to HW_MFPR HW_C_DATA).
    // ------------------------------------------------------------------
    // Set by the most recent HW_MTPR HW_C_SHFT trigger from the
    // top 6 bits of errorReg.  Returned verbatim by HW_MFPR HW_C_DATA;
    // HW_MFPR does NOT auto-advance the chain (only C_SHFT writes do).
    uint8_t dataReg = 0;

    // ------------------------------------------------------------------
    // C_SHFT register shadow (1 bit observably; W1 per spec).
    // ------------------------------------------------------------------
    // Tracked for telemetry / inspection.  Functional effect is the
    // shiftErrorOut() call in HW_MTPR HW_C_SHFT when bit 0 is set;
    // this field records the most recent written value.
    uint8_t shftCtrl = 0;


    // ------------------------------------------------------------------
    // Helpers.
    // ------------------------------------------------------------------

    // Push the low 6 bits of `chunk6` into the WRITE_MANY chain.
    // Called from execHwMtpr HW_C_DATA.  Masks back to 36 bits so
    // long write sequences don't accumulate stray high bits.
    void pushWriteMany(uint64_t chunk6) noexcept
    {
        writeMany = ((writeMany << 6) | (chunk6 & 0x3FULL))
                  & 0xFFFFFFFFFULL;          // mask to 36 bits
    }

    // Shift 6 bits of ERROR_REG into dataReg.  Called from
    // execHwMtpr HW_C_SHFT when the low bit of opB is set.
    void shiftErrorOut() noexcept
    {
        dataReg  = static_cast<uint8_t>(errorReg & 0x3FULL);
        errorReg >>= 6;
    }
};

} // namespace coreLib

#endif // CORELIB_CBOXSTATE_H
