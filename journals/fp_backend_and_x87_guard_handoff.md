<!--
EmulatR V4 -- FP Execute Backend Seam + Host-FP (x87) Guard
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-10
Relates to: task #26 (fBox FP build-out). Sits directly below the
just-landed coreLib/fp_variant_core.h (qualifier decode), which is
host-agnostic and stays unchanged.
Purpose: hand-off design plan for Cowork to (A) add a build/runtime guard
that refuses any host-FP path that is not single-rounded SSE2/NEON or
SoftFloat, and (B) stand up the FP execute backend seam so the arithmetic
kernels live behind one swappable interface (SoftFloat reference + optional
host fast paths). Implementation and all line-number verification happen in
Cowork against the live tree.
ASCII(128) only. Reconcile generated .cpp/.h headers with
docs/notes/ADR-0001-source-file-headers.md and header_cpp.txt.
-->

# EmulatR V4 -- FP Execute Backend Seam + x87 Guard (Cowork hand-off)

## 0. What this is and is not

A DESIGN PLAN, not generated code. It specifies two things: a guard that
keeps guest FP off any non-conforming host path, and the execute-backend
interface that the FP grains call into. Cowork turns it into diffs,
locates and verifies every seam and line number, confirms the values
marked below, and runs the MSVC build. Nothing here is bound until a
trace, a disassembly, or a spec read confirms the branch (house rule: no
fix before trace confirmation). Propose the located seams and the edit
shape back for approval before writing (discuss-before-code).

Markers used:
- `[LOCATE]`     -- a code seam Cowork must find in V4 and report back.
- `[CONFIRM]`    -- a value/behavior that must be confirmed (trace, HRM,
                    or oracle) before it gates anything.
- `_PROVISIONAL` -- a value acceptable for storage but not for decode
                    until verified.

## 1. Where this sits

`coreLib/fp_variant_core.h` (just ported, task #26) is the qualifier-decode
layer and is already host-agnostic: `deriveLocalFpcr` shapes an Alpha FPCR
value (not a host control word) and the decoders are pure bit math. This
hand-off covers the layer DIRECTLY BELOW it -- the execute backend that
consumes a decoded `FPVariant` plus the architectural FPCR and actually
performs the arithmetic -- plus the guard that fences the host-FP path.

The host dependency must live entirely inside the execute backend. Nothing
above the backend (decode, grain wiring, FPCR policy) may touch a host
control word (MXCSR / FPSR / FPCR-host) or a host FP type directly.

## 2. Two work items

- Item A: host-FP guard (compile-time tripwire + runtime self-test).
- Item B: FP execute backend seam (IFpBackend) with a SoftFloat reference
  implementation first.

Item A is small and can land first; it also protects Item B. Item B is the
substantive build-out.

---

## 3. Item A -- the host-FP (x87) guard

### 3.1 Framing

There is no "x87 platform" to detect at runtime. x87 is a CODEGEN PATH the
compiler can drop the FP arithmetic onto -- chiefly on 32-bit x86 builds,
or anywhere `long double` (80-bit on GCC/Clang x86) enters the guest FP
path, or under `/fp:fast` / `-ffast-math`. x87 cannot reproduce true
T-format results at the underflow/overflow boundary (80-bit exponent ->
double rounding even with 53-bit precision control), and its
spill-dependent intermediates are non-deterministic. So the guard's job is
not "am I on x87 hardware" but "is my build routing guest FP through
anything but single-rounded SSE2/NEON or SoftFloat."

### 3.2 Compile-time tripwire

In the execute-backend translation unit (`[LOCATE]` the backend dir; guard
prefix per the in-use convention). Fail the build unless a known-good host
FP path or the SoftFloat backend is selected:

    #if defined(_M_X64) || defined(__x86_64__) \
     || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) \
     || defined(__SSE2__) || defined(__aarch64__) \
     || defined(EMULATR_FP_SOFTFLOAT)
      // OK: SSE2 scalar, AArch64 scalar, or SoftFloat backend selected.
    #else
      #error "Guest FP may lower to x87 (no SSE2/NEON/SoftFloat). Refusing to build."
    #endif

    #if defined(__FAST_MATH__)
      #error "Guest FP requires strict semantics; /fp:fast / -ffast-math forbidden."
    #endif

On x64/MSVC this is belt-and-suspenders (x64 never emits x87) -- its value
is firing the day someone attempts a 32-bit build or a mis-flagged ARM
build. `[CONFIRM]` the exact macro set against the toolchains V4 actually
builds under (MSVC/VS2022 primarily); add MSVC-specific equivalents if the
GCC/Clang feature macros are not defined under cl.exe.

### 3.3 Convention guard: ban long double in the FP path

`long double` is 80-bit x87 on GCC/Clang x86 and is never correct for guest
FP. Add it to the lint set as a forbidden token within the FP backend and
kernel files (a greppable lint, since `/fp:fast` and FMA contraction are
not always macro-detectable). Also pin the FP files to strict/precise FP
and contraction-off in CMake for those targets (`[LOCATE]` the FP target):
MSVC `/fp:strict` (or `/fp:precise`), GCC/Clang `-ffp-contract=off`. FMA
contraction is a correctness hazard on every host because Alpha base FP
operate has no fused multiply-add (multiply and add round separately).

### 3.4 Runtime self-test (the real catch)

Macros cannot see `/fp:fast`, FMA contraction, or a stray host
flush-to-zero default. A startup self-test does. Preferred mechanism, since
it needs no hand-picked magic constant:

- Run a small fixed vector of ops (a handful covering add/mul/div/sqrt with
  results near the rounding boundary, plus one denormal case) through BOTH
  the SoftFloat oracle and the active host fast-path backend; assert
  bit-equality. Any divergence aborts startup with a clear diagnostic.

This makes the self-test backend-relative and oracle-defined (truth = the
oracle), and it doubles as the proof that an x86 build and an ARM build are
guest-bit-identical. For builds compiled WITHOUT the oracle, fall back to a
standalone double-rounding probe: a triple `(a,b,c)` whose correctly-rounded
double result differs from the 80-bit-then-rounded or FMA-contracted result.
The specific probe vector is `[CONFIRM]` -- do not bind a magic constant on
faith; derive it and validate it against the oracle before it is trusted.

`[LOCATE]` the emulator startup/init path where this self-test should run
(once, before any guest FP can execute).

---

## 4. Item B -- the FP execute backend seam

### 4.1 Principle

One interface, `IFpBackend`. Everything DECODED (`FPVariant`) and everything
POLICY (DNZ / UNDZ / UNFD denormal handling, trap-enable decisions) stays
ABOVE the backend. The backend only executes one op and reports the RAW
IEEE exception flags it produced; it does not apply Alpha denormal policy
and does not decide whether to trap. Mapping raw flags into the FPCR sticky
bits and the trap decision happen above, in the grain/FPCR layer.

### 4.2 Types and interface (hand-written; `auto X() -> Y` form)

Proposed `coreLib/fp_backend.h` (ADR-0001 header, include guard
`CORELIB_FP_BACKEND_H`, doctest CHECK only):

    #ifndef CORELIB_FP_BACKEND_H
    #define CORELIB_FP_BACKEND_H

    #include <cstdint>
    #include "coreLib/fp_variant_core.h"

    namespace coreLib {

    enum class FpCompare : uint8_t { Eq, Lt, Le, Un };  // CMPTEQ/LT/LE/UN

    // Raw exception bits one op produced, pre-policy.
    struct FpExc {
        bool inv{false};   // INV invalid operation
        bool dze{false};   // DZE divide by zero
        bool ovf{false};   // OVF overflow
        bool unf{false};   // UNF underflow
        bool ine{false};   // INE inexact
        bool iov{false};   // IOV integer-convert overflow (CVTxQ only)
    };

    // One op result: canonical Alpha register image + raw flags.
    struct FpResult { uint64_t bits{0}; FpExc exc{}; };

    // Execute context: decoded qualifier + architectural FPCR
    // (for DYN rounding and the denormal-policy bits read above).
    struct FpExecCtx { FPVariant variant{}; uint64_t fpcr{0}; };

    class IFpBackend {
    public:
        virtual ~IFpBackend() = default;

        // IEEE T-format (double).
        virtual auto addT (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto subT (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto mulT (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto divT (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto sqrtT(uint64_t a,             const FpExecCtx&) -> FpResult = 0;

        // IEEE S-format (single).
        virtual auto addS (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto subS (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto mulS (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto divS (uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;
        virtual auto sqrtS(uint64_t a,             const FpExecCtx&) -> FpResult = 0;

        // Compare (T). bits = Alpha true/false register value.
        virtual auto cmpT (FpCompare, uint64_t a, uint64_t b, const FpExecCtx&) -> FpResult = 0;

        // Conversions (the long tail; each its own rounding/flag case).
        virtual auto cvtTS(uint64_t a, const FpExecCtx&) -> FpResult = 0; // T -> S
        virtual auto cvtST(uint64_t a, const FpExecCtx&) -> FpResult = 0; // S -> T
        virtual auto cvtTQ(uint64_t a, const FpExecCtx&) -> FpResult = 0; // T -> i64 (IOV)
        virtual auto cvtQT(uint64_t a, const FpExecCtx&) -> FpResult = 0; // i64 -> T
        virtual auto cvtQS(uint64_t a, const FpExecCtx&) -> FpResult = 0; // i64 -> S
    };

    } // namespace coreLib
    #endif // CORELIB_FP_BACKEND_H

`[LOCATE]` whether an `ArithmeticStatus` type already exists (the dropped
V1 `commitLocalFpcr` note in fp_variant_core.h says "FP grains will fold
ArithmeticStatus into CpuState.fpcr directly"). If it does, reconcile
`FpExc` / `FpResult` with it -- they are the same concept and should not
duplicate. The grain layer folds `FpExc` into `CpuState.fpcr` after applying
denormal policy.

### 4.3 Rounding resolution (one shared helper)

Resolve effective rounding once, above or at the top of each kernel:
take `ctx.variant.getEffectiveRoundingMode()`; if it returns `UseFPCR`,
read FPCR DYN (bits 59:58 per the Alpha arch ref) to get the concrete mode.
KEEP the current fp_variant_core.h behavior where round-toward-+infinity is
reachable ONLY through the `UseFPCR` path, never from static bits: Alpha has
no static "/+inf" qualifier, so +inf occurs only via /D with DYN=11. Do not
"complete" the rounding enum with a static +inf case; it does not exist in
the ISA.

### 4.4 Host-divergent control hooks (internal to each backend)

These do NOT appear in IFpBackend -- that is the point of the seam. Each
backend implements them privately. The mapping across the three backends:

| Concern                | SoftFloat (oracle)              | x86-64 SSE2                 | AArch64                       |
|------------------------|---------------------------------|-----------------------------|-------------------------------|
| Set rounding           | softfloat_roundingMode arg      | MXCSR RC field              | FPCR.RMode field              |
| add/sub/mul/div/sqrt   | f64_add ... f64_sqrt            | plain double (addsd/mulsd)  | plain double (fadd/fmul)      |
| Harvest flags          | softfloat_exceptionFlags        | MXCSR status IE/ZE/OE/UE/PE | FPSR IOC/DZC/OFC/UFC/IXC      |
| Flag -> Alpha map      | direct                          | IE>INV ZE>DZE OE>OVF UE>UNF PE>INE | IOC>INV DZC>DZE OFC>OVF UFC>UNF IXC>INE |
| FTZ / denorm default   | explicit per flag               | clear MXCSR FTZ/DAZ         | clear FPCR.FZ                 |
| FMA contraction        | N/A (never fused)               | /fp:strict or precise       | -ffp-contract=off (critical)  |
| NaN canonical form     | remap softfloat NaN -> Alpha    | force Alpha canonical NaN   | FPCR.DN + remap -> Alpha      |
| IOV (CVTxQ overflow)   | detect in convert kernel        | detect in convert kernel    | detect in convert kernel      |

Note: the host fast paths use PLAIN `double` / `float`, not explicit
intrinsics -- the compiler lowers `a + b` to scalar SSE2 (x86) / scalar FP
(ARM). Reach for an intrinsic only if a specific kernel needs MXCSR/FPSR
control the compiler will not express; AVX/wide intrinsics are never used
(scalar guest ISA, one lane of useful work).

### 4.5 What must stay OUT of the backend

The sign/bit and FPCR-move instructions are PURE bit manipulation with no
rounding and no exceptions and must NOT route through any FP kernel:

- CPYS, CPYSN, CPYSE (copy sign / sign+exponent)
- FCMOVxx (floating conditional move)
- MF_FPCR, MT_FPCR

Routing these through a host-FP kernel is a classic way to accidentally
canonicalize a NaN or raise a spurious flag. They belong in the integer/bit
layer. `[LOCATE]` where they are currently handled and confirm they bypass
the backend.

## 5. Build order and phasing

- Phase 1  Land Item A (guard) + the IFpBackend header.
- Phase 2  SoftFloat backend as the REFERENCE oracle: implement every
           kernel against SoftFloat. Get bit-exact FPCR rounding and raw
           flags; pass known-vector tests. This is the truth source.
- Phase 3  Wire the FP grains to call IFpBackend (fold FpExc into
           CpuState.fpcr after denormal policy). Confirm the boot/console
           FP hot subset first (likely CPYS/CPYSN, the CVT family, maybe
           CMPT, a few ADDT/SUBT/MULT) -- `[CONFIRM]` by trace which ops
           the boot path exercises, build those correctly before the tail.
- Phase 4  Optional x86-64 SSE2 fast path, validated against the oracle by
           the differential harness; oracle stays as the self-test partner.
- Phase 5  AArch64 backend = a backend swap; the seam makes it mechanical.

SoftFloat-first is the recommendation because rounding-as-argument and
flags-as-data eliminate the MXCSR/FPSR juggling and the cross-host NaN/FTZ
divergence, and because Alpha's own software-completion model is itself a
soft reimplementation of IEEE.

## 6. SoftFloat sourcing

`[CONFIRM]` the SoftFloat library choice and license before vendoring.
Berkeley SoftFloat (Hauser) is the usual choice: permissive BSD-style
license (compatible with the project's licensing posture -- verify), pure
C, no Qt, std-only. Vendor it under the third-party convention `[LOCATE]`,
gate it behind `EMULATR_FP_SOFTFLOAT`, and keep Qt out of the FP path
entirely (std default; no named-seam Qt justification applies here).

## 7. FPCR-layout dependency (data fidelity)

The fold from `FpExc` into FPCR sticky bits depends on the FPCR bit layout,
which fp_variant_core.h's dropped-items note flags as PROVISIONAL in
`alpha_fpcr_core.h` (the reason `shouldRaiseFPTrap` / `getExceptionSummary`
were deferred). Therefore: the `FpExc` -> FPCR mapping and any trap decision
are `_PROVISIONAL` until the FPCR layout (EXC_MASK, the per-exception bit
positions, DYN field) is HRM-verified against the Alpha arch ref (FPCR bits:
INV<52> DZE<53> OVF<54> UNF<55> INE<56> IOV<57>; DYN<59:58>; the disable bits
<62:60,51:50> for software completion). Verify before binding the fold.

The backend itself (producing FpExc as booleans) is NOT blocked by this --
it can be built and oracle-tested now. Only the FPCR fold above it waits on
the layout verification.

## 8. Out of scope

- Trap delivery / arithmetic-trap frame construction (deferred with
  shouldRaiseFPTrap/getExceptionSummary until trap delivery is wired).
- VAX F/G/D arithmetic kernels: the VAX formats need bespoke pack/unpack and
  the reserved-operand / dirty-zero invalid trap. The SAME IFpBackend can be
  extended with addF/addG/... later; this hand-off scopes the IEEE S/T set
  plus conversions, which is the boot-relevant surface. Note VAX as a
  follow-on, do not build it here.
- AVX / busmaster / any wide-vector path -- not applicable to scalar guest FP.
- The integer CIX path (CTPOP/CTLZ/CTTZ, opcode 0x1C, the getbit64 OPCDEC
  vector-420 item) -- that is integer, a separate track, not FP.

## 9. Deliverables expected from Cowork

1. Item A landed: compile-time tripwire, long-double lint, FP-target strict/
   no-contract flags, runtime oracle-vs-host self-test (probe vector
   `[CONFIRM]`ed), located startup hook.
2. `coreLib/fp_backend.h` with the IFpBackend seam, FpExc/FpResult/FpExecCtx,
   ADR-0001 header, include guard, reconciled against any existing
   ArithmeticStatus.
3. SoftFloat reference backend implementing all kernels, with doctest CHECK
   tests against known IEEE vectors (per rounding mode, denormal, NaN,
   overflow/underflow, CVTTQ IOV).
4. The differential-test harness: oracle vs any host fast path, bit-equality
   over the vector set; the same harness proves cross-host identity.
5. The located seams from sections 3-4 (file + line + edit shape) proposed
   back before writing.

---

## Standing EmulatR V4 rules (apply to all implementation work)

### Workflow
- Discuss before code. Non-trivial changes are proposed first as prose with
  file paths, line numbers, and the concrete edit shape; wait for approval
  before editing. Applies even to one-line changes with architectural
  meaning. Exception: trivial typo/formatting fixes the user pointed out.
- Documentation at header and source line. Every source change updates a
  header block (rationale, date, behavior/bug addressed, in the
  "FILE N: ... FUNCTION: ... CHANGE: ..." style) and leaves an inline
  comment at the changed line referencing it. No anonymous changes.
- TODO discipline. Incomplete-on-purpose code is documented at both the file
  header (a named TODO table) and the call site (// TODO(<tag>): <summary>).
  Greppable tags; removed in the same edit that lands the wiring.
- Best-effort deterministic architecture. Single-threaded by default, no
  nondeterministic timing or race-prone shared state, predictable
  side-effect ordering. Name any determinism trade-off in the spec. (The FP
  backend is a determinism-critical surface: strict rounding, no x87, no FMA
  contraction, host-independent results via the oracle.)

### File / source conventions
- ASCII(128) only in all file content -- no smart quotes, em dashes,
  Unicode arrows, or box-drawing glyphs, in source, comments, docs, or
  generated artifacts. The MSVC pipeline expects unformatted ASCII(128).
- Copyright/attribution header on every generated source/header (and
  Markdown specs as an HTML comment), per
  docs/notes/ADR-0001-source-file-headers.md, template at
  docs/notes/templates/header_cpp.txt. Hard rule, no "small file" exemption.
- Include guards, never #pragma once. Pattern
  #ifndef <DIR>_<FILE>_H / #define / #endif (Qt MOC + pragma once under MSVC
  /permissive- causes LNK2001).
- Hex radix for all switch/case dispatch labels, never mixed; convert
  dec->hex by value (16 -> 0x10, not 0x16), never digit substitution.
- Prefer surgical Edit over whole-file rewrites in V4; treat V0/V1/V2 and
  Processor Support as read-only.

### C++ / build specifics
- doctest: CHECK only, never REQUIRE (exceptions disabled in V4).
- Never name an enum's printable helper toString (doctest ADL clash) -- use
  <typeName>Name(T) plus operator<<.
- Codegen vs hand-written differentiator: hand-written leaves use
  auto X() -> Y; codegen emits direct Y X(). Generated-header edits are lost
  on regen -- change genGrains.py, not the output. (IFpBackend and the
  backends are hand-written: auto X() -> Y form.)

### Logging / Qt
- Logging behind CMake compile guards (zero-cost ((void)0) in Release) with
  a runtime mute knob via LogSubsystem. Runtime trace toggles such as
  EMULATR_IIC_TRACE / EMULATR_GCT_WATCH are environment variables, NOT CMake
  compile guards.
- Qt surface stays minimal, used sparingly only at named seams (e.g.
  threading) with inline justification; defer to std whenever possible. The
  FP path is std-only; no Qt.

### Data fidelity
- Provisional FPCR-layout / IPR values are OK for C1 storage but never for
  C2 decode -- mark guessed values _PROVISIONAL and HRM-verify before any
  dispatch, decode, or FPCR fold matches them (silent corruption otherwise).

### Trace / debug discipline
- Multi-GB traces: bounded tails / gated windows only, never whole-file grep
  (times out, wedges the sandbox).
- Verify every file write via bash (wc -l / grep); prefer heredoc for large
  writes.

### Collaboration
- For analytically-heavy, open-ended work: claude.ai web for the analysis/
  design, Cowork for the edits. Claude web provides the instructional
  design; Cowork implements against the live tree.
