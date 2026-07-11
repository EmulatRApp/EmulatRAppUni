<!--
EmulatR V4 -- ES40 memtest ACV: next-session plan.  AXPBox corroboration
analysis (in-tree source audited) + the dynamic diagnostic to localize the
failing memory test.  2026-07-10 (for the next session).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience.  [LOCATE] = point-in-time; verify
against the live tree.  _PROVISIONAL = not yet HRM-verified.
-->

# ES40 memtest ACV -- next-session plan + AXPBox corroboration (2026-07-10)

## Where we are (one paragraph)

Root cause of the ES40 SRM memory-test ACV is ESTABLISHED and V4 is CORRECT:
the memtest probe (LDQ R0,0(R16); TRAPB; RET at PC 0x1b7dd4) is handed a
MALFORMED kseg address in R16.  Firmware tests physical RAM through SPE[1]
kseg superpages (no VPTB), so R16 should be 0xFFFFFC00 | PA =
0xFFFFFC00_7F827F5F; it is 0xFFFFFFFF_7F827F5F.  The PA payload (0x7F827F5F,
~2.1 GiB, installed RAM) is intact; ONLY the base high longword is wrong,
identically on every probe.  Because VA[47:41]=0x7F (SPE[1] needs 0x7E) it
misses every superpage, routes to the page-table walker, which has VPTB=0
(genuinely -- SRM never set it; it hand-loads DTB + uses superpages), finds no
leaf, and delivers ACV.  Full proof + source audit: journals/
20260709_es40_memtest_acv_vptb_verdict.md.  The defect is SRM-firmware address
construction, not a V4 fix.

## SUPERSEDED (2026-07-10): mechanism corrected by the overnight trace

The "OR-merge base|PA" mechanism and the "(-1)<<42 vs (-1)<<32 shift slip /
SLL-SRA shift-amount or LDAH width" lead below are BOTH DEAD.  The overnight
trace proves the address is formed by a plain SUBQ (0x08C308) from a running
fill pointer that is missing bits [41:32]=0x3FF, with all ops faithful and the
0x3FF never born in any register.  The intended probe rides the SPE[1] kseg
superpage (VA[47:41]=0x7E); the corrupt 0x7F misses it.  Read
20260710_es40_memtest_acv_trace_corrected_mechanism.md for the corrected
mechanism, the live-code SPE verification, the re-pointed A.3 targets (watch the
fill-pointer BASE at 0x05AFA0, not R16), and the fix-shape decision tree.  The
sections below are kept for history only.

## The sharpened lead (verified arithmetic)

    kseg base (correct) = 0xFFFFFC00_00000000 = (-1) << 42
    corrupt base        = 0xFFFFFFFF_00000000 = (-1) << 32
    XOR delta           = 0x000003FF_00000000 = bits [41:32]

A clean 10-bit width slip: a base built as (-1)<<42 (or sign-extended to bit
42) came out (-1)<<32 (sign-extension stopping at bit 32).  First hypothesis
to test at the constructing instruction: a SLL/SRA shift amount or an
LDAH/sign-extend width of 42 vs 32.  A lead, not a conclusion -- the capture
shows the actual instruction.

## AXPBox corroboration analysis (in-tree source, audited 2026-07-10)

Source present at D:/EmulatR/axpbox/src (reference only; witness, not oracle).

FINDING -- AXPBox NOPs the ES40 memory test, so it CANNOT witness this bug.
System.cpp:1653-1662:

    printf("%%SYM-I-PATCHROM: Patching ROM for speed.\n");
    WriteMem(U64(0x8bb78), 32, 0xe7e00000, 0); // memory test (aa)
    WriteMem(U64(0x8bc0c), 32, 0xe7e00000, 0); // memory test (bb)
    WriteMem(U64(0x8bc94), 32, 0xe7e00000, 0); // memory test (00)

0xe7e00000 = BEQ R31,+0 (R31 always 0 -> always "branch to next" = a NOP).
(axpbox-1.1.2 additionally NOPs 0x14248/0x14288/0x142c8 and 0x68320 -- more
speed patches; the three 0x8bxxx entries are the labeled "memory test" ones.)
AXPBox patches three memory-test routines in the 0x8bxxx region -- the SAME
region as our EmulatR fault-chain caller 0x8B694 (frame chain 1B7DD4 <- 5B058
<- 5A6B0 <- 8B694 <- 6211C <- 66148).  So AXPBox skips the memory test, never
executes the R16 construction, and reaches >>> without ever building the
address.  Per the plan's A.5:
  - AXPBox is PATCHED here -> it is BLIND to the R16 construction.  The
    dynamic value-birth slice (below) stands alone and loses nothing.
  - AXPBox reaching >>> is NOT evidence the address is correct.  It is the
    textbook "known-good-but-lossy" witness -- it cheats past the exact code
    we need to observe.

FAITHFUL DECISION (recorded): we do NOT adopt AXPBox's NOP-the-memtest
expedient.  We localize and fix the real construction, and (Stream B) report
memory size correctly so the test runs faithfully.

SECONDARY (Stream B reference): AXPBox reports installed RAM from a Cchip CSR,
not by probing.  System.cpp:1331-1347, cchip_csr_read case 0x100:
    return ((u64)(iNumMemoryBits - 23) << 12); // size
iNumMemoryBits from config (memory.bits, default 27).  This is the reference
encoding for the faithful memory-size CSR (Stream B1); HRM Ch.10 confirms
offset/encoding/per-array layout before we bind it.

## Next steps to diagnose the failing test (Stream A -- do first)

Ghidra static trace is OUT: R16 arrives via r27 procedure-value dispatch
(runtime-resolved), which static dataflow cannot follow.  Use a DYNAMIC
value-birth watch; EmulatR is deterministic so a run repeats to the same cycle
and we narrow backward one level at a time.

### A.3 -- WB value-birth watch (seam CONFIRMED)

Seam: the UNIFIED integer-register commit at pipelineLib/MemDrainer.h:205-242
-- guards on S_WritesRa | S_WritesRc and applies
`cpu.intReg[r.regWriteIdx] = r.regWriteValue` at :240 for EVERY committing op
(the operate-format instruction that builds R16 commits here, not just loads).
A commented-out "spurious regfile commit" diagnostic already sits here to model
the probe on.  Everything needed is in scope: cyc = cpu.cycleCount; pc/encoded
= slot.grain; Rc/result = r.regWriteIdx / r.regWriteValue; Ra/Rb inputs by
decoding encoded and reading cpu.intReg[...] (in-order commit, sources not yet
clobbered).

Gate under EMULATR_BRINGUP_PROBES (already compiling in the CLI diag build),
capped/fire-once.  Predicates:
  - P1 (backstop): result == 0xFFFFFFFF_7F827F5F.  Catches the final assembly /
    BIS zero,r3,r16 copy.  Guaranteed to fire (it is R16 at the fault).
  - P2 (target): result[63:32] == 0xFFFFFFFF AND result[41:32] != 0 in an
    address-construction context -- the corrupt BASE appearing before the PA
    offset is merged.  P2 localizes the width slip; P1 backstops it.
Log per hit: { cyc, pc(mask PAL bit), encoded, opcode/func, Ra/Rb/Rc idx,
Ra/Rb input values, literal/disp, shift amount, result }.

### A.4 -- decode-and-compare against the ISA (the faithful check)

Decode the captured instruction (21264/EV67 + AARM), compute by hand what the
result SHOULD be from the captured inputs, compare to what EmulatR produced.
Compare to the SPEC, not to what looks right.
  - MISMATCH -> localized EmulatR instruction-semantics bug (prime suspect per
    the 10-bit slip: SLL/SRA shift amount, or LDAH/sign-extend width 42 vs 32).
    Fix the leaf.  Gate: full suite + DS10 + DS20 + ES40 boot before commit.
  - MATCH + COMPUTED op -> EmulatR executed faithfully; its INPUT was already
    wrong.  Re-run (deterministic -> same cycle) with the watch on the feeder
    register's birth; recurse one level at a time to the mismatch or root
    constant.
  - MATCH + LOAD -> base came from guest memory.  Check the image bytes at the
    load PA: correct there -> load/sign-extend bug; wrong there -> corrupt
    decompressed image (ties to 20260519_decompressor_pal_overlap_findings).

### A.5 -- AXPBox corroboration: DONE (above)

AXPBox is patched/blind here; no differential trace is available.  The A.3/A.4
dynamic slice is authoritative and self-sufficient.

### Backstop -- overnight RETIRE_COMPACT trace

The overnight decoded trace goes to G:/traces via EMULATR_RETIRE_TRACE_DIR
(NOT EMULATR_TRACE_DIR -- DecListingSink.cpp:216; the retire firehose opens on
--trace mask 0x80 regardless of ARM).  Launch:
  EMULATR_RETIRE_TRACE_DIR=G:/traces MAXCYC=0x12000000 ARM=none HEADLESS=1 \
    bash tools/run_es40_trace.sh rebuild
If present, window it around cyc 282090581 and grep PCs 0x1b7dd4 / 0x5b058 /
0x5a6b0 (bounded tails only; multi-GB).  The A.3 watch is preferred; the trace
is context/cross-check.

## Stream B (faithful memory-init) -- separate, does NOT clear the ACV

Tracked in task #11.  B1: Cchip memory-size CSR returns configured memSize
(AXPBox case 0x100 = (log2(bytes)-23)<<12; 2 GiB -> 0x8000) so firmware sizes
by reading, not probing; verify HRM Ch.10 offset/encoding, source memSize from
the ini via the platform-JSON resolver.  B2: on non-existent-memory access set
Cchip MISC<NXM> + DRIR<63>, latch MISC<NXS>, raise the error interrupt via the
device path -- replacing the raw kFaultMachineCheck (task #72); sequence after
the #70/#71 interrupt-delivery work.  These do NOT fix Stream A (our probe hits
installed RAM via a bad base, not memory past the top).

## Recommended order (cheapest-decisive-first)

1. Wire the A.3 value-birth watch (seam MemDrainer.h:205-242); run to the
   first ACV; capture the constructing instruction + inputs.  [unblocks]
2. A.4 decode-and-compare; branch (local fix / recurse / image check).
3. Land the localized fix per A.4.  Gate: suite + DS10 + DS20 + ES40 boot.
4. Stream B1 (Cchip memSize CSR), HRM-confirmed.  5. Stream B2 with #70/#71.

## Do-no-harm gate

Any core/chipset commit requires full suite + DS10 + DS20 + ES40 boot green.
Applies to every Stream A leaf fix and every Stream B capability.

## References

Authoritative: Alpha 21264/EV67 HRM (VA_FORM 5.1.5, SPE 5.3.9), 21264A Spec
Rev 1.1 (VA_CTL 5.1.4 VPTB[63:30]; SPE 5.3.9); AARM (kseg, LDAH/SLL/SRA);
21272 Tsunami HRM Ch.10 (Cchip CSRs, memory-size reg, MISC/DRIR/NXM 10.2.2).
EmulatR (as-audited): mmuLib/Ev6Translator.h applyTlbHit :212-232, computeVaForm
coreLib/IprFields.h:321-343; palBoxLib/grains/PalEntries.cpp execMtprVptb_vms
:784, HW_VA_CTL read :1354/write :1810, HW_VA_FORM :1408; MEMDIAG-MTPR :1629;
pipelineLib/MemDrainer.h:205-242 (WB commit -- A.3 seam).
AXPBox (in-tree, witness only): axpbox-1.1.2/src/System.cpp LoadROM :1569,
memtest NOPs :1656-1662 (0x8bxxx labeled memory test; 0x14xxx/0x68320 speed),
cchip_csr_read case 0x100 :1344-1347.
Prior journals: 20260709_es40_memtest_acv_briefing.md, _analysis.md,
_vptb_verdict.md; 20260709_es40_memtest_next_steps_and_meminit_csr.md.
Memory: [[es40-srm-boot-status]], [[emulatr-es40-diag-knobs]],
[[deliver-bash-as-scripts]], [[verify-webchat-claims-vs-live-tree]].
Tasks: #6 (root cause), #10 (A.3 watch), #11 (Stream B).
