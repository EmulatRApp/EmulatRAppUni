# Alpha ISA Instruction-Coverage Matrix (V4)

Date: 2026-06-10
Scope: complete audit of EVERY Alpha primary opcode (0x00-0x3F) and the
function-code groups, classifying each instruction REAL / STUBBED / ABSENT.
Companion to the FP-only deep dive in fBox_FP_Coverage_Map_20260610.md.

Sources of truth (codegen):
- grainFactoryLib/GrainMasterV4.tsv      -- the dispatch UNIVERSE (376 rows).
- grainFactoryLib/codegen/handwritten.tsv -- REAL (hand-written) leaves.
- grainFactoryLib/generated/GrainStubs.cpp -- STUBBED leaves (logUnimplementedStub).

Classification model:
- REAL    = leaf has a row in the master TSV AND a hand-written body.
- STUBBED = leaf has a row in the master TSV but routes to logUnimplementedStub
            (logs once, computes NOTHING -> silently wrong result, no trap).
- ABSENT  = no row in the master TSV -> executing it takes a decode fault
            (OPCDEC / kFaultUnimplemented).
Note: FP arithmetic leaf-bases collapse all 16 trap-mode variants to one leaf.

VERIFIED TOTALS: 376 master rows -> 256 distinct leaves = 245 REAL + 11 STUBBED.
The 11 stubs are the ENTIRE in-dispatch unimplemented frontier; every other gap
is an ABSENT opcode (no row).

================================================================================
## HEADLINE

The integer, memory, control-flow, and PAL surfaces are essentially COMPLETE.
Every gap of consequence is in FLOATING POINT, plus a short list of
overflow-trapping integer variants and write-hint ops. The 11 stubs are:
6 FP (ITOFS, ITOFT, ADDS, SUBS, MULS, DIVS) and 5 PAL (SWPCTX, REMQHILR,
REMQTILR, REMQHIQR, REMQTIQR).

================================================================================
## COVERAGE BY PRIMARY OPCODE

0x00  CALL_PAL .............. REAL (99 funcs) + STUBBED (5) -- see PAL section
0x01-0x07  (reserved/PALRES) . NO INSTRUCTIONS in the Alpha ISA -- correctly empty,
                               NOT a gap.
0x08  LDA ................... REAL      0x09 LDAH .. REAL
0x0A  LDBU .................. REAL      0x0B LDQ_U . REAL
0x0C  LDWU .................. REAL      0x0D STW ... REAL
0x0E  STB ................... REAL      0x0F STQ_U . REAL
0x10  INTA (add/cmp/scaled) . REAL (18 leaves)  -- GAP: /V overflow variants (below)
0x11  INTL (logical/cmov) ... REAL (16 leaves)  -- COMPLETE
0x12  INTS (shift/msk/ins/ext) REAL (26 leaves) -- COMPLETE
0x13  INTM (multiply) ....... REAL (MULL,MULQ,UMULH) -- GAP: MULL/V, MULQ/V
0x14  ITFP (int->fp, sqrt) .. STUBBED (ITOFS,ITOFT) -- GAP: ITOFF, SQRT[F/G/S/T]
0x15  FLTV (VAX F/G/D float) . ABSENT ENTIRELY -- no rows (see FP block)
0x16  FLTI (IEEE S/T float) . REAL (ADDT/SUBT/MULT/DIVT, CMPTEQ/LT/LE) +
                               STUBBED (ADDS/SUBS/MULS/DIVS) -- GAP: CVT*, CMPTUN
0x17  FLTL (sign/fpcr/cvt) .. REAL (CPYS/CPYSN/CPYSE, MT/MF_FPCR)
                               -- GAP: CVTLQ, CVTQL, FCMOVxx
0x18  MISC .................. REAL (TRAPB,EXCB,MB,WMB,FETCH,RC,RPCC,RS,ECB)
                               -- GAP: FETCH_M, WH64, WH64EN
0x19  HW_MFPR ............... REAL
0x1A  JSR/JMP/RET/COROUTINE . REAL (all 4) -- COMPLETE
0x1B  HW_LD ................. REAL
0x1C  FPTI/BWX/CIX/MVI ...... REAL (19 leaves: CTLZ/CTPOP/CTTZ, MIN/MAX*, PERR,
                               PK*/UNPK*, SEXTB/SEXTW, FTOIT) -- GAP: FTOIS
0x1D  HW_MTPR ............... REAL
0x1E  HW_REI ................ REAL
0x1F  HW_ST ................. REAL
0x20-0x23  LDF/LDG/LDS/LDT ... REAL (all four FP-format loads)
0x24-0x27  STF/STG/STS/STT ... REAL (all four FP-format stores)
0x28  LDL .. REAL   0x29 LDQ .. REAL   0x2A LDL_L . REAL   0x2B LDQ_L . REAL
0x2C  STL .. REAL   0x2D STQ .. REAL   0x2E STL_C . REAL   0x2F STQ_C . REAL
0x30  BR ... REAL   0x34 BSR .. REAL
0x31  FBEQ . ABSENT  0x32 FBLT . ABSENT  0x33 FBLE . ABSENT
0x35  FBNE . ABSENT  0x36 FBGE . ABSENT  0x37 FBGT . ABSENT
0x38  BLBC . REAL   0x39 BEQ .. REAL   0x3A BLT .. REAL   0x3B BLE .. REAL
0x3C  BLBS . REAL   0x3D BNE .. REAL   0x3E BGE .. REAL   0x3F BGT .. REAL

================================================================================
## THE COMPLETE GAP LIST (everything not REAL)

### A. Floating point -- the dominant gap (detail in fBox_FP_Coverage_Map)
STUBBED:
  ITOFS, ITOFT (0x14 int->fp moves)
  ADDS, SUBS, MULS, DIVS (0x16 IEEE single, all 16 variants each)
ABSENT:
  0x15 VAX float ENTIRELY: ADDF/SUBF/MULF/DIVF, ADDG/SUBG/MULG/DIVG,
       CMPGEQ/CMPGLT/CMPGLE, CVTGF/CVTGD/CVTGQ/CVTQF/CVTQG/CVTDG
  0x14: ITOFF, SQRTF, SQRTG, SQRTS, SQRTT
  0x16: CMPTUN, CVTTQ, CVTQT, CVTQS, CVTTS, CVTST
  0x17: CVTLQ, CVTQL, FCMOVEQ/FCMOVNE/FCMOVLT/FCMOVGE/FCMOVLE/FCMOVGT
  0x1C: FTOIS
  FP branches: FBEQ, FBLT, FBLE, FBNE, FBGE, FBGT (0x31/32/33/35/36/37)
REAL (for reference): ADDT/SUBT/MULT/DIVT, CMPTEQ/LT/LE, CPYS/CPYSN/CPYSE,
  MT/MF_FPCR (storage-only), LDF/LDG/LDS/LDT, STF/STG/STS/STT, FTOIT, FEN trio.

### B. Integer overflow-trapping variants (ABSENT)
  0x10: ADDL/V, ADDQ/V, SUBL/V, SUBQ/V
  0x13: MULL/V, MULQ/V
Compilers emit these for languages with checked/overflow-trapping arithmetic;
OS and runtime code can use them. Medium priority -- share the base-op body,
add the V-suffix trap-on-signed-overflow path.

### C. Memory/cache hint ops (ABSENT)
  0x18: WH64 (write-hint-64), WH64EN, FETCH_M
WH64 is common in optimized zero/copy loops (allocate-without-read). These are
architecturally HINTS and may be implemented as NO-OPS, but they MUST have a
dispatch row or the guest decode-faults on them. Cheap, high-value-to-avoid-
surprise: add as no-op grains.

### D. PAL (CALL_PAL, opcode 0x00)
STUBBED:
  SWPCTX (0x05)  -- privileged context switch. Used by a running OS scheduler
                    on every process switch; needed once the OS is multitasking
                    (not necessarily during the earliest boot). HIGH for OS run.
  REMQHILR (0xA6), REMQTILR (0xA7), REMQHIQR (0xA8), REMQTIQR (0xA9)
                 -- self-relative remove-queue WITH-RESIDUAL reverse variants.
                    Rare; the forward INSQ*/REMQ* and non-R queue ops are REAL.
ABSENT (no row): CALL_PAL slots 0x28, 0x2C, 0x2F, 0x3B -- sparse privileged
  slots; identify against the AARM PALcode map only if a fault lands there.

================================================================================
## PRIORITY FOR THE OS-BOOT / INSTALL PATH

P0 (install will hit almost immediately):
  - VAX G/F float (0x15) + VAX conversions -- OpenVMS default float format.
  - IEEE conversions CVTQT/CVTTQ/CVTQS/CVTTS/CVTST + CVTLQ/CVTQL -- int<->float
    movement, pervasive in any compiled code.
  - FP branches FBEQ..FBGT -- no FP-conditional control flow without them.
  - Promote S-format stubs (ADDS/SUBS/MULS/DIVS) + ITOFS/ITOFT to real.

P1 (very likely during install/runtime):
  - FCMOVxx (optimized FP), SQRT*, CMPTUN, FTOIS.
  - WH64/WH64EN/FETCH_M as no-ops (avoid decode-fault surprise in copy loops).
  - SWPCTX (once the OS schedules processes).

P2 (defer until a fault proves need):
  - Integer /V overflow variants, REMQ*R reverse-residual queue ops,
    sparse CALL_PAL slots 0x28/0x2C/0x2F/0x3B.
  - Real FPCR rounding + IEEE trap delivery (correctness depth; foundation
    already staged in coreLib/fp_variant_core.h + alpha_fpcr_core.h).

================================================================================
## BOTTOM LINE

Outside floating point, V4's instruction coverage is complete enough for an OS:
all integer ALU/shift/byte-manipulation, all loads/stores, all integer branches
and jumps, the full HW_ IPR access set, and 99 of 104 CALL_PAL functions are
REAL. The blockers to running a guest OS are concentrated exactly where we
expected -- the FP build-out (task #26) -- plus a cheap no-op pass for the
write-hint ops and, for a multitasking OS, SWPCTX. The /V integer variants are
the only non-FP arithmetic gap and are a bounded follow-on.
