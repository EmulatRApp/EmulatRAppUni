<!--
EmulatR V4 -- ES40 memtest ACV: next-steps (dynamic base-slice) + faithful
memory-init capability (Cchip memory-size CSR + NXM error path replacing
MCHK-on-probe).  2026-07-09.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix throughout.  Discuss-before-code stands.
Guiding principle for every item below: FAITHFUL implementation, not expedience.
[LOCATE] anchors are point-in-time; Cowork verifies against the live tree.
_PROVISIONAL marks any value not yet HRM-verified against real execution.
-->

# ES40 memtest ACV -- next steps + faithful memory-init capability (2026-07-09)

## TL;DR

Two separate work streams, do not conflate them:

- STREAM A (the active blocker): the SRM memory-test probe faults ACV because
  R16 carries a malformed kseg base -- 0xFFFFFFFF where 0xFFFFFC00 belongs.
  The Ghidra static trace is not viable (r27 procedure-value dispatch resolves
  only at runtime).  Replace it with a dynamic, deterministic value-birth watch
  in EmulatR and a decode-and-compare procedure.  This localizes the defect and
  tells us whether it is an EmulatR instruction-semantics bug, a bad memory
  load, or genuine SRM behavior.

- STREAM B (faithful memory-init capability, requested): implement the Cchip
  memory-size CSR so the firmware SIZES RAM by reading it, and route
  non-existent-memory accesses through the Cchip NXM error interrupt -- both
  replacing the current raw MCHK-on-probe.  This is the correct mechanism; it
  is worth doing on its own merits, but it does NOT clear the Stream A ACV (our
  probe targets installed RAM, not memory past the top).

Faithful-over-expedient decisions recorded explicitly below.  We do NOT adopt
AXPBox's expedient of NOP-ing the memory test.

---

## STREAM A -- ES40 memtest ACV: dynamic base-slice

### A.0 Confirmed state (carried forward)

- Fault: 51 x kFaultAcv at PC 0x1b7dd4, enc 0xa4100000 = LDQ R0,0(R16),
  pal=0 (native), VA = 0xFFFFFFFF_7F827F5F walking +8, R0 doubling
  (address-line test of installed RAM near the top of a ~2 GiB window).
- The VA is well-formed negative, matches NO superpage window, so it routes to
  the page-table walker; the walker has VPTB=0 (genuinely -- SRM never called
  MTPR_VPTB, VA_CTL=0x2), so the walk finds no leaf and delivers ACV.  V4's
  translator / VA_FORM / VA_CTL / superpage modeling all match the 21264A.
- Intended access is an SPE[1] kseg PHYSICAL probe: R16 should be
  0xFFFFFC00 | PA, i.e. 0xFFFFFC00_7F827F5F.  It is 0xFFFFFFFF_7F827F5F.
  The PA payload (low longword 0x7F827F5F) is intact and strides cleanly;
  ONLY the base high longword is wrong, identically on every probe.

### A.1 The sharpened lead (arithmetic, verified)

    kseg base (correct) = 0xFFFFFC00_00000000 = (-1) << 42
    corrupt base        = 0xFFFFFFFF_00000000 = (-1) << 32
    XOR delta           = 0x000003FF_00000000 = bits [41:32]

The corruption is EXACTLY a 10-bit shift / sign-extend width slip: a base built
as (-1) << 42 (or a mask sign-extended to bit 42) that came out (-1) << 32
(sign-extension stopping at bit 32).  This is the first hypothesis to test the
moment we capture the constructing instruction.  It is a lead, not a
conclusion -- the capture shows the actual instruction.

### A.2 Why Ghidra is out, and why dynamic wins

R16 arrives via r27 procedure-value dispatch (LDA r27,disp(r2); BSR;
BIS zero,r3,r16 down the chain).  The dispatch targets are computed at runtime,
so static dataflow cannot follow them.  The dynamic watch reads runtime values
after indirection is resolved -- it cannot be defeated by the same indirection
that blocks Ghidra.  EmulatR is single-threaded and deterministic, so a run can
be repeated to the same cycle and the slice narrowed backward one level at a
time.

### A.3 Capture spec -- WB value-birth watch (Cowork implements)

Gate: EMULATR_BRINGUP_PROBES (compile guard, zero-cost in release via
((void)0)); runtime mute knob separate from the compile guard.

Seam: the integer register writeback/commit point -- where BoxResult.regWriteValue
is applied to CpuState.intReg at WB.  [LOCATE] confirm the exact apply site in
the pipeline (PipelineDriver / MEM->WB apply); this is the correct seam because
side effects are traced at WB per V4's ordering contract.

Predicate (fire-once, or first N):
- P1 (anchor, most specific): result == 0xFFFFFFFF_7F827F5F.  Guaranteed to
  occur (it is R16 at the fault).  Catches the final assembly / the
  BIS zero,r3,r16 copy.
- P2 (base birth, the target): result[63:32] == 0xFFFFFFFF AND result[41:32]
  != 0 in an address-construction context, i.e. the corrupt base appearing in
  a register before the PA offset is merged.  P2 is the one that localizes the
  width slip; P1 backstops it.

On hit, log: { cyc, pc (mask PAL bit), raw encoding, opcode/func, Ra/Rb/Rc
indices, Ra/Rb input values, literal/displacement if present, shift amount if a
shift, result }.  Keep the record small and bounded.

Ordering: this probe is native-mode and pre-fault; if the base is instead
seeded far upstream in PAL/SROM init, P1/P2 still catch it, but A.4 recursion
may run several levels (bounded, not one-shot).

### A.4 Decision procedure -- decode and compare (this is the faithful check)

Decode the captured instruction against the Alpha ISA (21264/EV67 + AARM), then
compute by hand what the result SHOULD be from the captured inputs and compare
to what EmulatR produced.  Compare to the SPEC, not to what looks right -- the
whole point is to catch EmulatR mis-executing.

- MISMATCH -> localized EmulatR instruction-semantics bug.  Prime suspect per
  A.1: SLL/SRA shift-amount or LDAH/sign-extend width (42 vs 32).  Fix the
  leaf; full suite + DS10 + DS20 + ES40 boot gate before commit.  This is the
  faithful fix: correct the instruction, do not paper over the address.
- MATCH and it is a COMPUTED op -> EmulatR faithfully executed an op that
  genuinely yields the corrupt base, so its INPUT was already wrong.  Re-run
  (determinism -> same cycle) with the watch moved to the feeder register's
  birth.  Recurse one level at a time until the mismatch or a root constant
  appears.
- MATCH and it is a LOAD -> the base came from guest memory.  Check the source
  bytes in EmulatR's image at the load PA: correct there -> load / sign-extend
  bug; wrong there -> the decompressed image is corrupt, which ties to the
  prior decompressor-overlap findings (20260519_decompressor_pal_overlap_*).

### A.5 AXPBox -- optional corroboration only

Per the AXPBox profile, AXPBox reaches the console partly by NOP-ing the memory
test (LoadROM ROM speed patches) and by reporting memory size from a CSR, so it
may never execute this R16 construction.  Before relying on it as a witness,
grep System.cpp LoadROM for the ES40 SROM branch: is the memtest caller / the
0x1b7dd4 probe-table region among the NOPed ranges?
- Not patched -> AXPBox runs the construction; a differential trace at the A.3
  site confirms the divergence (AXPBox builds 0xFFFFFC00, EmulatR 0xFFFFFFFF).
- Patched -> AXPBox is blind here; the A.3/A.4 dynamic slice stands alone and
  loses nothing.
AXPBox reaching ">>>" is NOT evidence the address is fine -- witness only,
known-good-but-lossy.

---

## STREAM B -- faithful memory-init capability (CSR memSize + NXM, not MCHK+probe)

Requested: add the "CSR instead of MCHK+probe" capability.  This replaces two
current expedient/incorrect behaviors with the faithful chipset mechanism.

### B.1 The finding (from the AXPBox profile, sections 2.1 / 2.2 / 5)

- HRM/AXPBox: the firmware learns installed RAM by READING a Cchip memory-size
  CSR; it does not probe-until-fault to size memory.
- HRM: an access to non-existent memory sets Cchip MISC<NXM> + DRIR<63>,
  latches MISC<NXS> (failing source), and raises an ERROR INTERRUPT via the
  device-interrupt path -- NOT a direct CPU machine check.
- V4 today: raised a raw kFaultMachineCheck for the OOB access (task #72), and
  does not tie the size CSR to the configured memSize.  Both are unfaithful.
- AXPBox additionally NOP-s the memory-test routines to reach the console fast.
  FAITHFUL DECISION: we do NOT adopt the NOP.  We report size correctly and let
  the test run.

### B.2 Capability B1 -- Cchip memory-size CSR reports memSize

Behavior: the Cchip CSR read for the memory-size/presence register returns the
configured guest RAM size in the HRM encoding, so the firmware sizes memory by
reading rather than probing.

Seam: the Cchip CSR read path (TsunamiCchip csr read).  [LOCATE] the live
handler; AXPBox handles this as cchip_csr_read case 0x100 returning
(iNumMemoryBits - 23) << 12, with 0x140/0x180/0x1c0 = 0 (single array).

Encoding _PROVISIONAL (AXPBox-derived; HRM Ch.10 confirm offset + encoding +
per-array layout BEFORE binding):

    size CSR value = (log2(bytes) - 23) << 12
      512 MiB = 2^29 -> (29-23)<<12 = 0x6000
        1 GiB = 2^30 -> (30-23)<<12 = 0x7000
        2 GiB = 2^31 -> (31-23)<<12 = 0x8000   (this boot's likely config)
        4 GiB = 2^32 -> (32-23)<<12 = 0x9000
    secondary arrays (0x140/0x180/0x1c0) = 0  (single-array model)

CONFIRM before binding:
- The authoritative register offset and name in 21272 HRM Ch.10 (0x100 is cited
  as the Cchip memory-presence / MPD-class register; verify).
- Whether the value is per-array or aggregate, and the single-array convention.
- That memSize is sourced from the authoritative ini input (Model + memorySize),
  resolved through the platform JSON resolver, not hardcoded.

Faithful note: this is the CORRECT mechanism (the CSR is real hardware), and it
is more faithful than delivering an MCHK for a probe that should not happen.

### B.3 Capability B2 -- NXM error path replaces raw MCHK

Behavior: on an access to non-existent physical memory, set Cchip MISC<NXM> and
DRIR<63>, latch MISC<NXS> with the failing source, and raise the error
interrupt through the device-interrupt path.  Do NOT raise a raw
kFaultMachineCheck.

Seam: the physical-access OOB path that currently raises kFaultMachineCheck
(task #72).  [LOCATE] the raise site; re-route to the Cchip request state.

Dependency (sequencing): B2 needs error-interrupt DELIVERY to exist -- the
DRIR/DIM/error-interrupt machinery is part of the TsunamiChipset work (#71) and
the interrupt latch-vs-deliver restructure (#70).  Until that lands, B2 cannot
deliver.  Order: land B1 first (standalone, high-leverage); land B2 after or
alongside the #70/#71 interrupt path.

Faithful note: MISC bit layout (ITINTR/IPINTR/NXM/NXS) is partially derived
from AXPBox W1S/W1C masks; mark _PROVISIONAL and verify against HRM Ch.10
(section 10.2.2.3) before binding W1C/W1S semantics.

### B.4 Relationship to Stream A (do not conflate)

B1/B2 do NOT clear the Stream A ACV.  Our probe tests INSTALLED RAM (PA
~0x7F827F5F, near the top of a ~2 GiB window) via a malformed kseg base; the
fault is the bad base, not a probe past the memory top.  Even with the size CSR
correct, this probe still runs and still faults until the base is fixed.  There
is a possible indirect interaction (the SRM may derive the test RANGE from the
CSR-reported size), so having B1 correct makes the whole memtest phase faithful
-- but it is not the fix for the ACV.

---

## Recommended next steps (ordered, cheapest-decisive-first)

1. STREAM A -- wire the WB value-birth watch (A.3), run to the first ACV,
   capture the constructing instruction + inputs.  Cheapest, decisive, one
   probe.  [unblocks the ACV]
2. STREAM A -- decode-and-compare (A.4).  Branch: local semantics fix, or
   recurse one level, or inspect the memory/image source.
3. STREAM A -- in parallel and near-free: grep System.cpp LoadROM for ES40
   memtest NOPs (A.5) to know whether AXPBox can corroborate.
4. STREAM A -- land the localized fix per the A.4 outcome.  Gate: full suite +
   DS10 + DS20 + ES40 boot.
5. STREAM B1 -- Cchip memory-size CSR -> memSize (B.2), HRM Ch.10-confirmed
   encoding/offset.  Standalone, high-leverage, faithful.  Gate as core/chipset.
6. STREAM B2 -- NXM error path (B.3), sequenced with the #70/#71 interrupt
   delivery work; remove the raw MCHK-on-probe once the error interrupt
   delivers.

## Do-no-harm gate

Any commit touching core or chipset code requires the full test suite plus
DS10 and DS20 (and now ES40) boot green first.  This applies to every Stream A
leaf fix and every Stream B capability.

## Open / _PROVISIONAL items

- WB apply seam exact location (A.3) -- [LOCATE].
- Memory-size CSR offset/encoding/per-array layout (B.2) -- _PROVISIONAL, HRM
  Ch.10.
- OOB MCHK raise site (B.3) -- [LOCATE]; NXM/NXS/MISC bit layout _PROVISIONAL,
  HRM 10.2.2.3.
- Whether AXPBox runs the ES40 memtest (A.5) -- source check pending.

## References

Authoritative:
- Alpha 21264/EV67 HRM (VA_FORM 5.1.5, SPE 5.3.9); AARM (kseg, LDAH/SLL/SRA
  semantics); 21272 Tsunami/Typhoon HRM Ch.10 (Cchip CSRs, memory-size register,
  MISC/DRIR, NXM path 10.2.2).
EmulatR (as-audited 2026-07-09, [LOCATE]):
- mmuLib/Ev6Translator.h: applyTlbHit :212-232; translateData + Hook B ~:319-356;
  tryKsegTranslate ~:145-178; isCanonicalVA :106-108.
- coreLib/IprFields.h: computeVaForm :321-343.
- palBoxLib/grains/PalEntries.cpp: execMtprVptb_vms :784; HW_VA_CTL read :1354 /
  write :1810; HW_VA_FORM read :1408-1412.
- coreLib/BoxResult.h:104-135 (kFaultAcv=7, kFaultDtbMissDouble=14,
  kFaultNonCanonical=12, kFaultMachineCheck).
- pipelineLib/PipelineDriver.h:1175 (logFaultEvent retire path); MemDrainer.h
  :340-344.
- TsunamiChipset / TsunamiCchip CSR read/write (B.2/B.3) -- [LOCATE].
Reference implementation (witness only, known-good-but-lossy):
- AXPBox System.cpp: cchip_csr_read case 0x100 (memory size); LoadROM
  :1655-1662 (memtest NOPs); interrupt()/irq_h (error/device path).
Prior journals:
- 20260709_es40_memtest_acv_briefing.md, 20260709_es40_memtest_acv_analysis.md,
  20260709_es40_memtest_acv_vptb_verdict.md.
- Tsunami_HRM_vs_AXPBox_Profile.md sections 2.1 / 2.2 / 5 / 7.
- 20260519_decompressor_pal_overlap_findings.md (image-corruption cross-ref).
