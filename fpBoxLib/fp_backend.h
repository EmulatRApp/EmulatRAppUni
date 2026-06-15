// ============================================================================
// fpBoxLib/fp_backend.h -- IFpBackend seam: the FP execute interface
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
// PURPOSE (task #26, Phase B; design journals/fp_backend_and_x87_guard_handoff.md
//   section 4): one swappable interface for FP arithmetic. Everything DECODED
//   (FPVariant) and everything POLICY (denormal handling, trap-enable decisions)
//   stays ABOVE the backend. The backend executes ONE op and reports the RAW
//   IEEE exception flags it produced; it does NOT apply Alpha denormal policy and
//   does NOT decide whether to trap. The reference implementation is SoftFloat
//   (fp_backend_softfloat); optional host SSE2/AArch64 fast paths implement the
//   same interface and are validated against the oracle.
//
// WHAT STAYS ABOVE THE BACKEND (must NOT route through any FP kernel):
//   - CPYS / CPYSN / CPYSE (sign / sign+exp copy) -- pure bit manipulation.
//   - FCMOVxx (floating conditional move) -- pure bit/select, no arithmetic.
//   - MF_FPCR / MT_FPCR -- FPCR register moves, not arithmetic.
//   Routing any of these through a host-FP kernel risks canonicalizing a NaN or
//   raising a spurious flag. They belong in the integer/bit layer.
//
// DENORMAL POLICY STAYS ABOVE THE BACKEND (DNZ / UNDZ / UNFD):
//   The backend receives operands ALREADY adjusted for Alpha denormal policy and
//   returns raw IEEE flags; the grain/FPCR layer applies DNZ on inputs and
//   UNDZ/UNFD on outputs, then folds the raw flags into FPCR.
//   EDGE CASE TO PRESERVE (DNZ negative-denormal sqrt) -- record so it is not
//   lost: when DNZ (denormal-operands-to-zero) is in effect, a denormal INPUT is
//   flushed to a zero of the SAME SIGN before the op. For SQRT of a NEGATIVE
//   denormal this changes the result:
//     - WITHOUT DNZ: the operand is a tiny negative real -> sqrt -> Invalid
//       Operation (INV), result QNaN.
//     - WITH DNZ:    the operand becomes -0.0 -> IEEE sqrt(-0.0) = -0.0, which is
//       VALID (no INV).
//   So the policy layer MUST flush the negative denormal to -0.0 BEFORE calling
//   sqrtT/sqrtS; the backend, given -0.0, correctly returns -0.0 with no INV. The
//   backend never applies DNZ itself, so passing it the un-flushed tiny negative
//   would wrongly raise INV. (Mirror reasoning applies to any op whose validity
//   flips between "tiny signed value" and "signed zero".)
//
//   UNDERFLOW-RESULT POLICY (FPCR UNDZ/UNFD), also above the backend: the
//   backend returns the raw IEEE result (the denormal); the policy layer then,
//   per FPCR, keeps it, flushes it to +0.0 (UNDZ set), or routes the underflow
//   as a trap (UNFD=0). DNZ (denormal operands to zero) IS honored on EV6;
//   DNOD (bit 47) is NOT implemented on EV6 -- there is no DNOD path.
//
// DENORMAL-RESULT DECISION (task #26 sec 3.2, RESOLVED 2026-06-11): the backend
//   produces the CORRECT IEEE denormal; we do NOT model EV6's denormal-trap-to-
//   software-completion MECHANISM. Two DISTINCT justifications -- do not conflate:
//     - /S (software-completion) code with the OS handler installed (Tru64/VMS
//       trap-enabled images): the directly-produced denormal EXACTLY equals the
//       completion handler's result; only the trap's cycle/PAL bookkeeping is
//       skipped -- observably correct.
//     - non-/S (imprecise default) code: real EV6 runs NO completion handler --
//       it delivers an arithmetic trap (OS -> signal) or, per FPCR enables, the
//       non-trapping result. There is no "handler result" to match, so producing
//       a clean IEEE denormal is a DELIBERATE, DOCUMENTED more-correct-than-
//       silicon choice (HW would have trapped or flushed).
//   OBSERVABILITY BOUNDARY: the difference is visible only to a guest that
//   inspects the trap itself (a custom FP exception handler counting underflows,
//   or a diagnostic reading the trap PC) -- vanishingly rare outside FP-
//   conformance suites; invisible to an OS doing ordinary computation.
//
//   BACKEND-INDEPENDENT: all of the above (DNZ/UNDZ/UNFD) applies to whatever
//   FpResult the ACTIVE backend returns -- it is Alpha FPCR semantics, NOT a
//   SoftFloat property. SoftFloat-on-denormal is ONLY the guard-failure FALLBACK
//   (a host whose x87-guard denormal probe failed -- flush-to-zero it cannot
//   disable). In DEFAULT mode the SoftFloat divert is keyed on GUARD STATUS,
//   never on "a denormal was seen": on a guard-PASSING host denormals are
//   computed natively-correct, so the policy layer just applies UNDZ/DNZ on top
//   and NO SoftFloat divert occurs. [CONFIRM at wiring (#29/Phase E): the
//   predicate is guard status, not denormal-seen.]
//
// TRAP DELIVERY is out of scope here (deferred with shouldRaiseFPTrap /
//   getExceptionSummary until arithmetic-trap frame construction is wired); the
//   backend only produces the raw FpExc booleans.
// ============================================================================

#ifndef FPBOXLIB_FP_BACKEND_H
#define FPBOXLIB_FP_BACKEND_H

#include <cstdint>

#include "coreLib/fp_variant_core.h"   // coreLib::FPVariant (decoded qualifier)

namespace fpBox {

// IEEE T-format compare kinds (CMPTEQ / CMPTLT / CMPTLE / CMPTUN).
enum class FpCompare : uint8_t { Eq, Lt, Le, Un };

// Raw IEEE exception bits one op produced, PRE-policy (no Alpha denormal/trap
// shaping applied -- that happens above, when folding into the FPCR).
struct FpExc {
    bool inv{false};   // invalid operation
    bool dze{false};   // divide by zero
    bool ovf{false};   // overflow
    bool unf{false};   // underflow
    bool ine{false};   // inexact
    bool iov{false};   // integer-convert overflow (CVTxQ only)
};

// One op result: canonical Alpha FP-register image + the raw flags.
struct FpResult {
    uint64_t bits{0};
    FpExc    exc{};
};

// Execute context: the decoded qualifier (rounding/trap mode) plus the
// architectural FPCR (for DYN rounding and the denormal-policy bits the layer
// above reads). The backend uses it only to resolve the effective rounding mode.
struct FpExecCtx {
    coreLib::FPVariant variant{};
    uint64_t           fpcr{0};
};

// The FP execute interface. Operands and results are Alpha FP-register images
// (64-bit). For S-format ops the image is the Alpha single-in-register form
// (numerically the single's value); the backend handles the register<->single
// reformat internally so the result is single-rounded (no double rounding).
class IFpBackend {
public:
    virtual ~IFpBackend() = default;

    // IEEE T-format (double).
    virtual auto addT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto subT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto mulT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto divT (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto sqrtT(uint64_t a,             FpExecCtx const&) -> FpResult = 0;

    // IEEE S-format (single).
    virtual auto addS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto subS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto mulS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto divS (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto sqrtS(uint64_t a,             FpExecCtx const&) -> FpResult = 0;

    // Compare (T). bits = Alpha true/false register value (2.0 true, +0.0 false).
    virtual auto cmpT (FpCompare, uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;

    // Conversions (each its own rounding / flag case).
    virtual auto cvtTS(uint64_t a, FpExecCtx const&) -> FpResult = 0; // T  -> S
    virtual auto cvtST(uint64_t a, FpExecCtx const&) -> FpResult = 0; // S  -> T
    virtual auto cvtTQ(uint64_t a, FpExecCtx const&) -> FpResult = 0; // T  -> i64 (IOV)
    virtual auto cvtQT(uint64_t a, FpExecCtx const&) -> FpResult = 0; // i64 -> T
    virtual auto cvtQS(uint64_t a, FpExecCtx const&) -> FpResult = 0; // i64 -> S

    // ---- VAX F_floating / G_floating ----------------------------------------
    // Operands/results are Alpha VAX-format REGISTER images (FpFormat.h). Key
    // identity: a VAX register image reinterpreted as an IEEE double equals
    // 4 * the true VAX value (the F/G exponent rebias is exactly 2^2). So the
    // kernels are f64 ops on the register images with an exact power-of-2
    // rescale: add/sub direct, mul *0.25, div *4, sqrt *2. INV is raised for
    // VAX reserved operands / dirty zeros (exp==0 with frac!=0 or sign set) and,
    // for SQRT, a negative operand (AARM). F-format results are rounded to
    // single (23-bit) precision; G uses the full 52-bit fraction.
    virtual auto addF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto subF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto mulF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto divF (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto sqrtF(uint64_t a,             FpExecCtx const&) -> FpResult = 0;
    virtual auto addG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto subG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto mulG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto divG (uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto sqrtG(uint64_t a,             FpExecCtx const&) -> FpResult = 0;
    virtual auto cmpG (FpCompare, uint64_t a, uint64_t b, FpExecCtx const&) -> FpResult = 0;
    virtual auto cvtGF(uint64_t a, FpExecCtx const&) -> FpResult = 0; // G -> F
    virtual auto cvtGD(uint64_t a, FpExecCtx const&) -> FpResult = 0; // G -> D
    virtual auto cvtDG(uint64_t a, FpExecCtx const&) -> FpResult = 0; // D -> G
    virtual auto cvtGQ(uint64_t a, FpExecCtx const&) -> FpResult = 0; // G -> i64 (IOV)
    virtual auto cvtQF(uint64_t a, FpExecCtx const&) -> FpResult = 0; // i64 -> F
    virtual auto cvtQG(uint64_t a, FpExecCtx const&) -> FpResult = 0; // i64 -> G
};

} // namespace fpBox

#endif // FPBOXLIB_FP_BACKEND_H
