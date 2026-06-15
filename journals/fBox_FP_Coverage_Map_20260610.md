# fBox Floating-Point Coverage Map (V4)

Date: 2026-06-10
Scope: complete audit of Alpha FP instruction support in V4, opcodes
0x14 / 0x15 / 0x16 / 0x17, plus FPCR and FEN (FP-enable) handling.
Sources of truth: `grainFactoryLib/GrainMasterV4.tsv`,
`grainFactoryLib/codegen/handwritten.tsv`,
`grainFactoryLib/generated/GrainStubs.cpp`, `fBoxLib/grains/Float.cpp`.

Purpose: decide whether the storage / OS-install effort is gated on FP.
Verdict up front: it is. The fBox is an IEEE-T-only POC. VAX float and
ALL format conversions are absent; that blocks any real OpenVMS (or Tru64)
guest the moment it executes native FP, which is early and constant.

Status legend:
- IMPLEMENTED (shallow): a hand-written leaf executes the op, but
  trap-mode/rounding bits are ignored (host round-to-nearest only),
  IEEE traps are never raised, and signaling-NaN behavior is deferred.
- STUBBED: a dispatch-table row exists but routes to
  `logUnimplementedStub` -- it logs and does NOT compute the result
  (silently wrong, no trap).
- ABSENT: no dispatch-table row at all -> executing it takes a decode
  fault (OPCDEC / kFaultUnimplemented).

---

## Summary by opcode

### Opcode 0x14 -- ItFp (integer-register -> FP-register moves, SQRT)

| Mnemonic | Meaning                  | Status   |
|----------|--------------------------|----------|
| ITOFS    | int -> IEEE S (single)   | STUBBED  |
| ITOFT    | int -> IEEE T (double)   | STUBBED  |
| ITOFF    | int -> VAX F             | ABSENT   |
| SQRTF/G  | VAX square root          | ABSENT   |
| SQRTS/T  | IEEE square root         | ABSENT   |

### Opcode 0x15 -- FltVax (VAX F / G / D floating point)

ENTIRELY ABSENT. No rows in either TSV. This covers:
ADDF SUBF MULF DIVF (F single), ADDG SUBG MULG DIVG (G double),
CMPGEQ CMPGLT CMPGLE (G compare), and the VAX conversions
CVTGF CVTGD CVTGQ CVTQF CVTQG CVTDG.

This is the critical gap for OpenVMS: VMS Alpha uses VAX **G_float** as
its default double and **F_float** as default single. None of it exists.

### Opcode 0x16 -- FltIeee (IEEE S / T floating point)

| Group                         | Status              | Notes |
|-------------------------------|---------------------|-------|
| ADDT SUBT MULT DIVT (T double)| IMPLEMENTED (shallow)| all 16 trap-mode variants route to one round-nearest leaf each |
| CMPTEQ CMPTLT CMPTLE          | IMPLEMENTED (shallow)| ordered compares; NaN -> false |
| ADDS SUBS MULS DIVS (S single)| STUBBED             | logUnimplementedStub, all variants |
| CMPTUN (unordered compare)    | ABSENT              | |
| CVTTQ (T -> int)              | ABSENT              | heavy use |
| CVTQT (int -> T)              | ABSENT              | heavy use |
| CVTQS (int -> S)              | ABSENT              | |
| CVTTS (T -> S), CVTST (S -> T)| ABSENT              | double<->single |

### Opcode 0x17 -- FltLogical (sign ops, FPCR, FCMOV, int FP converts)

| Mnemonic                | Meaning                       | Status               |
|-------------------------|-------------------------------|----------------------|
| CPYS CPYSN CPYSE        | copy / copy-neg / copy-sign+exp| IMPLEMENTED          |
| MT_FPCR                 | move Ra -> FPCR               | IMPLEMENTED (storage)|
| MF_FPCR                 | move FPCR -> Rc               | IMPLEMENTED (storage)|
| CVTLQ (long -> quad)    | FP-reg integer reformat       | ABSENT               |
| CVTQL (quad -> long)    | FP-reg integer reformat       | ABSENT               |
| FCMOVEQ/NE/LT/GE/LE/GT  | FP conditional move           | ABSENT               |

### FP-enable (PAL path)

| Mnemonic   | Status      | Notes |
|------------|-------------|-------|
| MTPR_FEN   | IMPLEMENTED | palBox; sets CpuState.fen |
| MFPR_FEN   | IMPLEMENTED | palBox |
| CLRFEN     | IMPLEMENTED | palBox; FEN entry vector 0x200 |

So the FP-disabled trap gate works; what is missing is the arithmetic
behind it.

### Opcodes 0x20-0x27 -- FP loads / stores (all four formats)

| Mnemonic            | Meaning                  | Status      |
|---------------------|--------------------------|-------------|
| LDF LDG LDS LDT     | load F/G/S/T -> FP reg   | IMPLEMENTED |
| STF STG STS STT     | store FP reg -> F/G/S/T  | IMPLEMENTED |

Significant: FP DATA MOVEMENT works for all four formats, including the
VAX F/G memory layouts. So a guest can load/store G_float values; what
it cannot do is the VAX ARITHMETIC on them. VAX support is therefore
NOT 100% absent -- the load/store half exists, the compute half does not.

### Opcode 0x1C -- FP-register <-> integer-register moves (EV6 ext)

| Mnemonic | Meaning                  | Status      |
|----------|--------------------------|-------------|
| FTOIT    | FP T-format -> int reg   | IMPLEMENTED (eBox::execFtoit) |
| FTOIS    | FP S-format -> int reg   | ABSENT      |

(The inverse int -> FP moves ITOFS/ITOFT live in 0x14 and are STUBBED;
ITOFF is ABSENT -- see the 0x14 table above.)

### Opcodes 0x31-0x37 -- FP conditional branches

ENTIRELY ABSENT. No rows for FBEQ (0x31), FBLT (0x32), FBLE (0x33),
FBNE (0x35), FBGE (0x36), FBGT (0x37). Without these there is no
FP-conditional control flow -- any "compare then branch on FP result"
sequence takes a decode fault at the branch. Essential; must be added
alongside the compares.

---

## The whole implemented surface, in one list

T-format arithmetic ADDT/SUBT/MULT/DIVT (shallow), the three ordered
T-compares CMPTEQ/LT/LE (shallow), the sign ops CPYS/CPYSN/CPYSE, the
FP loads/stores LDF/LDG/LDS/LDT + STF/STG/STS/STT (all four formats),
the FPCR move pair MT/MF_FPCR (storage only), the FEN trio, and FTOIT
(eBox). Everything else is stubbed or absent.

SANITY CHECK (2026-06-10, verified against handwritten.tsv +
GrainStubs.cpp + GrainMasterV4.tsv -- authoritative, full surface):
 - WIRED/REAL: the list above (from handwritten.tsv FBOX block lines
   262-281 + eBox::execFtoit + palBox FEN trio).
 - STUBBED: exactly 6 FP leaf-bases -- ITOFS, ITOFT, ADDS, SUBS, MULS,
   DIVS (the only logUnimplementedStub FP calls in the generated file;
   one stub per leaf-base covers all 16 trap-mode variants).
 - ABSENT (no dispatch row -> decode fault): all VAX arithmetic
   (ADDF/SUBF/MULF/DIVF, ADDG/SUBG/MULG/DIVG), VAX compares
   (CMPGEQ/LT/LE), ALL conversions (CVTGQ/CVTQG/CVTGF/CVTGD/CVTDG/CVTQF
   + CVTTQ/CVTQT/CVTQS/CVTTS/CVTST + CVTLQ/CVTQL), SQRT[F/G/S/T],
   CMPTUN, FCMOVxx, FTOIS, and the FP branches FBEQ/FBNE/FBLT/FBLE/
   FBGE/FBGT.

KEY (2026-06-10, confirmed with Tim): the missing arithmetic is NOT
unwritten -- reference implementations exist in coreLib/proposed/
(alpha_fp_helpers_inl.h, alpha_fp_ieee_inl.h, alpha_SSE_fp_inl.h,
including VAX G-variants and some qualifier/rounding support). BUT
those headers are a PARTIAL V1 PORT and do NOT compile in V4 as-is:
 - alpha_SSE_fp_inl.h has missing dependencies (Tim, 2026-06-10).
 - alpha_fp_helpers_inl.h / alpha_fp_ieee_inl.h depend on
   fp_variant_core.h and alpha_fpcr_core.h, which do NOT EXIST anywhere
   in V4 (verified by Glob); they also reference Axp_Attributes_core.h
   (real file is lowercase axp_attributes_core.h -- resolves only by
   Windows case-insensitivity) and have a circular include between the
   two FP headers.
So do NOT #include the proposed/ headers. Plan: treat proposed/ as the
ALGORITHM REFERENCE and implement the build-out leaves NATIVELY against
V4 types -- the Float.cpp pattern already in use (std::bit_cast<double>
+ native host op + bit_cast back), with <cfenv> (fesetround /
feclearexcept / fetestexcept) for FPCR rounding and IEEE-trap delivery.
New grains then depend only on V4's CpuState.fpcr / InstructionGrain /
BoxResult, importing none of the broken V1 chain. (Reviving the V1 port
by writing fp_variant_core.h + alpha_fpcr_core.h + fixing the SSE deps
is the alternative -- more work, less benefit; skip unless a reason
appears.) So the build-out is: thin native grain leaves + handwritten.tsv
rows + instruction-qualifier (trap-mode/rounding) handling.

## Three depth caveats even where IMPLEMENTED

1. Rounding mode is host default (round-to-nearest); the trap-mode and
   /S /U /I /D encoding bits are parsed for tracing but not honored.
2. IEEE exception traps (INV/DZE/OVF/UNF/INE) are never raised.
3. MT_FPCR stores a rounding mode and trap-enable mask that nothing
   consults -- FPCR is storage-faithful but semantics-inert. (Host
   mxcsr<->fpcr merge helpers exist in `coreLib/Alpha_vector_fp_inl.h`
   but are not wired into the leaves.)

---

## Implications for booting / installing an OS off dqa0/dqb0

- The SRM console `boot` path and the primary bootstrap (APB) are
  essentially integer -> reaching "media read + bootstrap loaded" does
  NOT require FP. The dqDriver work is independent and not blocked here.
- The OS image itself is the wall. The instant the kernel/installer
  executes VAX float (VMS default) it hits ABSENT 0x15 -> decode fault.
  Even an all-IEEE guest (Tru64) needs the conversions (CVT*) which are
  all ABSENT, and single precision which is STUBBED.

Net: storage and FP are separable subsystems, but the **install goal is
gated on FP**, and FP is currently a POC.

---

## Recommended fBox build-out order (gating work for OS install)

1. Conversions first -- they unblock everything and are needed by both
   formats: CVTQT, CVTTQ, CVTQS, CVTTS, CVTST (0x16); CVTLQ, CVTQL
   (0x17). Without these even pure-IEEE code cannot move int<->float.
2. VAX G/F float (0x15) -- required for OpenVMS: ADDG/SUBG/MULG/DIVG,
   CMPGEQ/LT/LE, CVTGQ/CVTQG/CVTGF/CVTDG, and the F-format set. A head
   start exists: `coreLib/proposed/alpha_fp_helpers_inl.h` already has
   G-variant compare helpers (cmpEQ_G_variant, etc.), unwired.
3. IEEE single (S-format) -- promote ADDS/SUBS/MULS/DIVS from stub to
   real (share the T-format path, narrow to float).
4. SQRT (SQRTS/SQRTT, and VAX if needed), CMPTUN, FCMOVxx, ITOFx.
5. Real FPCR semantics -- drive host rounding from FPCR<DYN>, honor the
   trap-mode bits, and deliver IEEE traps. Required for correctness on
   code that sets rounding modes or relies on trap behavior.

Scope note (V4-shallow rule): items 1-3 are the minimum to get a guest
OS executing; 4-5 are correctness depth that real install/runtime will
demand but that can be staged behind a working first cut.
