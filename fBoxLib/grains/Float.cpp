// ============================================================================
// fBoxLib/grains/Float.cpp -- fBox IEEE T-format leaves and FP sign manip
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
// V4-shallow first cut for fBox: 10 hand-written leaves covering the
// minimum surface needed to exercise the FP regfile end-to-end through
// the pipeline driver.
//
//   FltLogical (opcode 0x17): CPYS, CPYSN, CPYSE
//   FltIeee    (opcode 0x16): ADDT, SUBT, MULT, DIVT,
//                             CMPTEQ, CMPTLT, CMPTLE
//
// Each base op (ADDT / SUBT / MULT / DIVT) covers all 16 trap-mode
// variants in the dispatch table -- the codegen routes every variant
// of leafBase X to fBox::execX.  The leaf does not honour the trap-
// mode encoding bits in this POC pass; rounding is host-default
// (round-to-nearest) and IEEE traps are never raised.  S_FpTrap on
// the grain is propagated for downstream tracing only.
//
// Bit-pattern uniform regfile contract:
//   c.opA, c.opB hold the 64 bits of FA, FB exactly as stored in
//   fpReg[].  std::bit_cast converts to double for arithmetic and
//   back to bits for the BoxResult write-out.  No reformat is done
//   here; T-format double precision IS the host double bit pattern.
//
// Compare result quirk (Alpha):
//   CMPTxx writes IEEE 2.0 (0x4000000000000000) on true, +0.0 on
//   false.  NaN operands produce false (host == / < / <= return
//   false on NaN; matches Alpha "ordered" semantics).  Signaling
//   NaN trap behaviour is deferred.
//
// Box routing:
//   regWriteIdx  = encoded[4:0]   (Fc -- destination FP register)
//   regWriteIsFp = true           (commits to fpReg[], not intReg[])
//
// MEM drain treats regWriteIsFp = true with no memEffect as a pure
// regfile write, so these leaves do not need to set memSize or any
// memory-related fields.
//
// ============================================================================


#include "coreLib/BoxResult.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/axp_attributes_core.h"
#include "fBoxLib/grains/FpFormat.h"
#include "fBoxLib/grains/FpExec.h"          // fpVariantFromEncoded + foldFpcrExc
#include "fpBoxLib/fp_backend.h"            // fpBox::FpExecCtx / FpResult / FpCompare
#include "fpBoxLib/fp_backend_active.h"     // fpBox::activeBackend()

#include <bit>
#include <cstdint>

#include "coreLib/CpuState.h"


namespace coreLib
{
    struct BoxResult;
}

namespace fBox {

using coreLib::BoxResult;
using coreLib::ExecCtx;
using coreLib::InstructionGrain;
using coreLib::kNoRegWrite;



// ----------------------------------------------------------------------------
// Helpers (file-internal).
// ----------------------------------------------------------------------------

// Fc field at encoded[4:0] -- the IEEE-format destination register.
[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint8_t fcIndex(InstructionGrain const& g) noexcept
{
    return static_cast<uint8_t>(g.encoded & 0x1Fu);
}

// IEEE double-precision sign bit and sign+exponent mask, used by
// the CPYS / CPYSN / CPYSE leaves.  Bit 63 is the sign; bits 62..52
// are the 11-bit exponent.
constexpr uint64_t kSignBit       = 0x8000000000000000ULL;
constexpr uint64_t kSignExpMask   = 0xFFF0000000000000ULL;

// Alpha compare-true encoding: IEEE +2.0, which is 0x4000000000000000.
// Compare-false is +0.0 = 0.
constexpr uint64_t kCmpTrueBits   = 0x4000000000000000ULL;
constexpr uint64_t kCmpFalseBits  = 0x0000000000000000ULL;


// Pack a BoxResult that commits a 64-bit value to FP register Fc and
// propagates the grain's semantic flag set.  Used by every leaf in
// this file.
[[nodiscard]] AXP_HOT AXP_FLATTEN
static BoxResult fpWrite(InstructionGrain const& g, uint64_t value) noexcept
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = fcIndex(g);
    r.regWriteIsFp = true;
    r.regWriteValue = value;
    return r;
}


#pragma region FltLogical sign manipulation

// ----------------------------------------------------------------------------
// CPYS Fc, Fa, Fb -- copy sign of Fa onto magnitude of Fb.
//
// Result bit 63    = Fa[63]
// Result bits 62:0 = Fb[62:0]
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCpys(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t const value = (c.opA & kSignBit) | (c.opB & ~kSignBit);
    return fpWrite(g, value);
}


// ----------------------------------------------------------------------------
// CPYSN Fc, Fa, Fb -- copy negated sign of Fa onto magnitude of Fb.
//
// Result bit 63    = ~Fa[63]
// Result bits 62:0 = Fb[62:0]
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCpysn(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t const flippedSign = (c.opA & kSignBit) ^ kSignBit;
    uint64_t const value       = flippedSign | (c.opB & ~kSignBit);
    return fpWrite(g, value);
}


// ----------------------------------------------------------------------------
// CPYSE Fc, Fa, Fb -- copy sign+exponent of Fa onto fraction of Fb.
//
// Result bits 63:52 = Fa[63:52]   (sign + 11-bit exponent)
// Result bits 51:0  = Fb[51:0]    (52-bit fraction)
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCpyse(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    uint64_t const value = (c.opA & kSignExpMask) | (c.opB & ~kSignExpMask);
    return fpWrite(g, value);
}

#pragma endregion FltLogical sign manipulation


#pragma region FltIeee T-format arithmetic

// ----------------------------------------------------------------------------
// ADDT Fc, Fa, Fb -- IEEE T-format addition.  Fc <- Fa + Fb.
// All 16 trap-mode / rounding variants share this leaf.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execAddt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().addT(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}


// ----------------------------------------------------------------------------
// SUBT Fc, Fa, Fb -- IEEE T-format subtraction.  Fc <- Fa - Fb.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execSubt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().subT(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}


// ----------------------------------------------------------------------------
// MULT Fc, Fa, Fb -- IEEE T-format multiplication.  Fc <- Fa * Fb.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMult(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().mulT(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}


// ----------------------------------------------------------------------------
// DIVT Fc, Fa, Fb -- IEEE T-format division.  Fc <- Fa / Fb.
//
// Division by zero yields IEEE infinity (signed by Fa); division of
// 0/0 yields IEEE NaN.  No host SIGFPE under MSVC default since the
// FP exception mask is on, so the leaf returns the IEEE-defined
// result without need for a guard.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execDivt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().divT(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}

#pragma endregion FltIeee T-format arithmetic


#pragma region FltIeee S-format arithmetic

// ----------------------------------------------------------------------------
// ADDS / SUBS / MULS / DIVS Fc, Fa, Fb -- IEEE S-format (single) arithmetic.
// The register image is the single's value as a 64-bit double; the backend
// narrows to float32, rounds ONCE (to single), and widens back -- avoiding the
// double-rounding bug of computing in double then re-rounding.  All 16 trap-
// mode variants share each leaf (qualifier decoded from the encoding).  These
// promote the former logUnimplementedStub single leaves to real arithmetic.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execAdds(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().addS(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}

AXP_HOT AXP_FLATTEN
auto execSubs(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().subS(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}

AXP_HOT AXP_FLATTEN
auto execMuls(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().mulS(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}

AXP_HOT AXP_FLATTEN
auto execDivs(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r = fpBox::activeBackend().divS(c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}

#pragma endregion FltIeee S-format arithmetic


#pragma region FltIeee T-format compares

// ----------------------------------------------------------------------------
// CMPTEQ Fc, Fa, Fb -- IEEE compare equal.
// Fc <- 2.0 (0x4000000000000000) if Fa == Fb, else 0.0.
// NaN operands: result is 0.0 (host == returns false).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCmpteq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r =
        fpBox::activeBackend().cmpT(fpBox::FpCompare::Eq, c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}


// ----------------------------------------------------------------------------
// CMPTLT Fc, Fa, Fb -- IEEE compare less than (signed, ordered).
// Fc <- 2.0 if Fa < Fb, else 0.0.  NaN operands: result is 0.0.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCmptlt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r =
        fpBox::activeBackend().cmpT(fpBox::FpCompare::Lt, c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}


// ----------------------------------------------------------------------------
// CMPTLE Fc, Fa, Fb -- IEEE compare less than or equal (signed, ordered).
// Fc <- 2.0 if Fa <= Fb, else 0.0.  NaN operands: result is 0.0.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCmptle(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    fpBox::FpExecCtx const ctx{ fpVariantFromEncoded(g.encoded), c.cpu->fpcr };
    fpBox::FpResult  const r =
        fpBox::activeBackend().cmpT(fpBox::FpCompare::Le, c.opA, c.opB, ctx);
    foldFpcrExc(c.cpu->fpcr, r.exc);
    return fpWrite(g, r.bits);
}

#pragma endregion FltIeee T-format compares


#pragma region FP load/store -- Mem-format (opcodes 0x20-0x27)

// ---------------------------------------------------------------------------
// Mem-format Alpha encoding for FP load/store:
//   bits[31:26]  primary opcode  (0x20 LDF .. 0x27 STT)
//   bits[25:21]  Fa (target on loads, source on stores)
//   bits[20:16]  Rb (integer EA base)
//   bits[15:0]   16-bit signed displacement
//
// EA = c.opB + sext_16(disp).  Rb is integer-side, so the EA fetch in
// the EX-stage reads c.opB from intReg[Rb].  Fa lives in fpReg[Fa] --
// on loads, the drainer commits regWriteValue to fpReg[regWriteIdx]
// because regWriteIsFp = true; on stores, the EX-stage reads c.opA
// from fpReg[Fa] (S_ReadsRa + S_ReadsFp on the grain).
//
// Format conversion:
//   Loads  -- drainer applies fBox::convertX_FloatingToRegister at the
//             memSize / S_VaxFp dispatch in MemDrainer::formatLoadValue.
//             Leaf only sets memAddr/memSize/regWriteIsFp; format is
//             implicit in size + semFlags.
//   Stores -- leaf does the inverse conversion (register -> memory)
//             before packing memData.  The drainer just writes the
//             bytes.
//
// Alignment is naturally aligned for S/F (4-byte) and T/G (8-byte);
// the drainer's translator checks this and raises kFaultUnaligned on
// violation, matching V1's executeLDS / executeSTS alignment rules.
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr int64_t memDispSext(InstructionGrain const& g) noexcept
{
    return static_cast<int64_t>(
        static_cast<int16_t>(g.encoded & 0xFFFFu));
}

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint8_t raIndex(InstructionGrain const& g) noexcept
{
    return static_cast<uint8_t>((g.encoded >> 21) & 0x1Fu);
}

}  // anonymous namespace


// ---------------------------------------------------------------------------
// LDF Fa, disp(Rb) -- VAX F_floating load (opcode 0x20).
// 32 bits at EA -> 64-bit register form (sign + 11-bit exp + frac).
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdf(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = true;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 4;
    r.memIsStore   = false;
    return r;
}


// ---------------------------------------------------------------------------
// LDG Fa, disp(Rb) -- VAX G_floating load (opcode 0x21).
// 64-bit memory -> 64-bit register form via word swap.
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdg(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = true;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 8;
    r.memIsStore   = false;
    return r;
}


// ---------------------------------------------------------------------------
// LDS Fa, disp(Rb) -- IEEE S_floating load (opcode 0x22).
// 32-bit IEEE single -> 64-bit register form (exponent expanded).
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLds(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = true;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 4;
    r.memIsStore   = false;
    return r;
}


// ---------------------------------------------------------------------------
// LDT Fa, disp(Rb) -- IEEE T_floating load (opcode 0x23).
// 64-bit IEEE double -> 64-bit register form (identity).
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags     = g.semFlags;
    r.regWriteIdx  = raIndex(g);
    r.regWriteIsFp = true;
    r.memAddr      = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memSize      = 8;
    r.memIsStore   = false;
    return r;
}


// ---------------------------------------------------------------------------
// STF Fa, disp(Rb) -- VAX F_floating store (opcode 0x24).
// 64-bit register form -> 32-bit memory (word-swapped VAX layout).
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStf(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = static_cast<uint64_t>(
                        fBox::convertF_FloatingToMemory(c.opA));
    r.memSize     = 4;
    r.memIsStore  = true;
    return r;
}


// ---------------------------------------------------------------------------
// STG Fa, disp(Rb) -- VAX G_floating store (opcode 0x25).
// 64-bit register form -> 64-bit memory (word-swapped VAX layout).
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStg(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = fBox::convertG_FloatingToMemory(c.opA);
    r.memSize     = 8;
    r.memIsStore  = true;
    return r;
}


// ---------------------------------------------------------------------------
// STS Fa, disp(Rb) -- IEEE S_floating store (opcode 0x26).
// 64-bit register form -> 32-bit IEEE single in memory.
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execSts(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = static_cast<uint64_t>(
                        fBox::convertS_FloatingToMemory(c.opA));
    r.memSize     = 4;
    r.memIsStore  = true;
    return r;
}


// ---------------------------------------------------------------------------
// STT Fa, disp(Rb) -- IEEE T_floating store (opcode 0x27).
// 64-bit register form -> 64-bit IEEE double in memory (identity).
// ---------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    r.memAddr     = c.opB + static_cast<uint64_t>(memDispSext(g));
    r.memData     = fBox::convertT_FloatingToMemory(c.opA);
    r.memSize     = 8;
    r.memIsStore  = true;
    return r;
}

#pragma endregion FP load/store -- Mem-format


#pragma region FPCR control register -- MF_FPCR / MT_FPCR

// ----------------------------------------------------------------------------
// MF_FPCR -- Move From Floating-point Control Register.
//
// FLTL opcode 0x17, function 0x025.
// Fc <- cpu.fpcr   (Fc is encoded[4:0])
//
// Reads the architectural FPCR into the destination FP register.  The
// FPCR holds dynamic rounding mode, IEEE exception flags, and trap
// enable bits per Alpha AARM Section 4.7.8.  V4 backing storage is
// CpuState::fpcr (coreLib/CpuState.h:110).
//
// V4-shallow first cut: this leaf only reads cpu.fpcr; it does NOT
// fold host FP exception state into the FPCR before reading.  When
// the SSE-library FP arithmetic path is wired (alpha_SSE_fp_inl.h +
// alpha_fpcr_core.h + fp_variant_core.h port), exception fold-in will
// happen at each arithmetic leaf via ArithmeticStatus::applyToFPCR,
// so the FPCR will already be up-to-date when MF_FPCR reads it.  No
// changes needed here at that time.
//
// First firmware hit: DS10 SRM cold boot at cyc 186.79M, PC 0xe121
// (SROM-resident PAL FP-context save during HWRPB / console init).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMfFpcr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    return fpWrite(g, c.cpu->fpcr);
}


// ----------------------------------------------------------------------------
// MT_FPCR -- Move To Floating-point Control Register.
//
// FLTL opcode 0x17, function 0x024.
// cpu.fpcr <- Fa   (Fa is encoded[25:21])
//
// Writes the architectural FPCR from the source FP register.  Direct
// CpuState mutation: leaves do not normally mutate CpuState directly,
// but IPR-class writes (HW_MTPR, MT_FPCR) are documented exceptions
// to that contract since the result is not register/memory effect
// material but processor-state material that the next instruction
// must observe.  See BoxResult.h drain-map commentary.
//
// V4-shallow first cut: writes the raw source value into cpu.fpcr.
// Does NOT enforce the writable-bits mask (some FPCR bits are read-
// only per AARM 4.7.8.1).  Does NOT honor variant suppression flags
// (the /S, /SU, /SUI etc. instruction-variant bits that filter which
// exception bits get set).  When the variant-decoder port lands
// (fp_variant_core.h), this leaf gains a single applyVariantMask
// call.  Until then, the firmware's PAL save/restore pattern works
// because it does MF_FPCR -> save -> ... -> MT_FPCR-back-the-saved-
// value, so the round-trip is lossless even without masking.
//
// Operand source: c.opA is Fa (the pipeline populates opA with the
// Fa-indexed FP register file value for FltL-format instructions).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMtFpcr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    c.cpu->fpcr = c.opA;

    BoxResult r;
    r.semFlags    = g.semFlags;
    r.regWriteIdx = kNoRegWrite;
    return r;
}

#pragma endregion FPCR control register


} // namespace fBox
