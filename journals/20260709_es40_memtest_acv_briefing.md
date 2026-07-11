<!--
EmulatR V4 -- ES40 memory-test ACV on 0xFFFFFFFF_7F827F5F: web-variant
analysis briefing (2026-07-09)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: hand-off to the claude.ai web variant to produce the DESIGN analysis
for the ES40 SRM memory-test access-violation. The web variant decides whether
V4's Ev6Translator is wrongly denying a seg1/kernel VA the SRM page tables map,
or whether 0xFFFFFFFF_7F827F5F is a genuine wild pointer from upstream.
Implementation/capture happens in Cowork against the live tree. ASCII(128).
Discuss-before-code stands.
-->

# ES40 memory-test ACV on 0xFFFFFFFF_7F827F5F -- analysis briefing (2026-07-09)

## What this document is for

The web variant's deliverable is a DESIGN analysis, not generated code. Decide
the root cause of the ES40 SRM memory-test access violation and specify the
next capture that proves it. Cowork (live file access) runs the capture,
verifies line numbers and the build, and lands any fix as diffs.

Treat all excerpts here as a point-in-time snapshot; Cowork is the source of
truth for current file state.

## Where the boot is (context)

The ES40 console-wiring gap is CLOSED (COM2/0x2F8 now backed via
PlatCap::ConsoleUartCom2; banner streams to PuTTY). Boot now advances through
the SRM memory test and wedges on a repeating access violation, then spins in
an IER-poll at PC 0xec80 to the max-cycle cap (~1.34B). This ACV is the current
boot blocker.

## Confirmed facts (fresh faults.log, 2026-07-09 run)

Source: logs/faults.log, written by coreLib::logFaultEvent at the retire path
(pipelineLib/PipelineDriver.h:1175). The caller logs all fault codes EXCEPT the
high-volume kFaultDtbMiss / kFaultItbMiss, so ACV and DtbMissDouble both land.

Census for the run:
- 4138 x kFaultDtbMissDouble (14) -- memory-test paging churn, HANDLED (boot
  reaches the ACVs).
- 51 x kFaultAcv (7) -- the blocker.
- 1 x kFaultUnimplemented (3) -- an unimplemented HW_MTPR (opcode 0x1d) at PC
  0x13f45, cyc 248653133, VA 0x3fe000. Separate anomaly; NOT the blocker.

The 51 ACV rows are identical in shape (first three shown):

    cyc         pc        enc         op   code name       pal va
    282090581   0x1b7dd4  0xa4100000  0x29 7    kFaultAcv  0   0xffffffff7f827f5f
    285816499   0x1b7dd4  0xa4100000  0x29 7    kFaultAcv  0   0xffffffff7f827f67
    287619419   0x1b7dd4  0xa4100000  0x29 7    kFaultAcv  0   0xffffffff7f827f6f

- Instruction: enc 0xa4100000 = opcode 0x29 = LDQ R0, 0(R16). A probe load.
- pal = 0: the faulting load runs in NATIVE mode (not PAL).
- VA walks 0xFFFFFFFF_7F827F5F, +8 each fault, while R0 doubles across the
  console dumps (4, 8, 0x10, 0x20 ...) -- a deliberate address-line memory
  test, not a random/garbage pointer.

The rows immediately BEFORE the first ACV are the PAL double-miss handler
walking the page table for this VA:

    282090480  0x8321  ... 14 kFaultDtbMissDouble 1 0x7ffffdfe098
    282090581  0x1b7dd4 ... 7  kFaultAcv          0 0xffffffff7f827f5f

PC 0x8321 (pal=1) is the PAL DTBM-double handler; 0x7ffffdfe098 is the
self-mapped PTE VA for the data VA (level walk). The chain is:

    LDQ R0,0(R16), R16=0xFFFFFFFF_7F827F5F  (native, memtest)
      -> DTB miss -> PAL walk (PC 0x8321) -> PTE page itself misses
         (kFaultDtbMissDouble, VA 0x7ffffdfe098)
      -> walk resolves to no-valid / protection-deny
      -> delivered as kFaultAcv(7) to the native LDQ
      -> guest prints "access violation fault"

## Decoded address region: 0xFFFFFFFF_7F827F5F

- Canonical NEGATIVE / system-space VA: sign-extended from bit 47 (bits 63:48 =
  0xFFFF = bit47 = 1). Well-formed -- rules out kFaultNonCanonical (12). Upper
  (kernel/system) half of the 48-bit Alpha VA space.
- Signed value ~ -2.007 GB (0x807D80A1 below the top of the 64-bit space); the
  low longword 0x7F827F5F sits ~8 MB below the 2 GB (0x80000000) line.
- NOT in any EV6 superpage (kseg) window, so it REQUIRES page-table translation
  (consistent with the DTB-miss -> PAL-walk observed):
  - SPE[2] needs VA<47:46> = 10; this VA has 11.
  - SPE[1] needs VA<47:41> = 0x7E; this VA has 0x7F.
  - SPE[0] needs VA<47:30> all-ones; this VA has bit31 = 0.
- NOT a physical Pchip address. Yesterday's "PChip wall" was PHYSICAL space
  (PA 0x801.xxxx.xxxx, bit<43>, Pchip0 PCI window) touched by HANDLED DTB
  misses during the memtest. This ACV is a VIRTUAL system-space address. Open
  question: does the page-table walk for this VA resolve toward a Pchip PA? If
  so, the two walls are the same wall seen from the VA side.

## Decompiled ROM: the faulting instruction is a memory PROBE

Disassembled from tools/host_decompressor/out/es40_decompressed.bin. The image
loads at runtime = fileoffset + 0x8000 (LDQ R0,0(R16) sits at file offset
0x1afdd4 -> runtime 0x1b7dd4). PC 0x1b7dd4 is NOT the memtest loop body; it is a
leaf entry in a table of memory-access PRIMITIVES, each of the form
"<access>; TRAPB; RET":

    0x1b7d80  LDBU r0,0(r16); TRAPB; RET      probe-read byte
    0x1b7d8c  LDWU r0,0(r16); TRAPB; RET      probe-read word
    0x1b7dd4  LDQ  r0,0(r16); TRAPB; RET      probe-read quadword  <-- faulting
    0x1b7de0  LDQ/LDQ + STQ/STQ; RET          copy 16 bytes r16->r17
    0x1b7df8  LDQ x4 + STQ x4; RET            copy 32 bytes
    0x1b7e20  STB r17,0(r16); TRAPB; RET      probe-write byte
    0x1b7e2c  STW r17,0(r16); TRAPB; RET      probe-write word

(0x63ff4000 = TRAPB, the trap barrier that forces the access fault to report
synchronously before RET.) So the console calls 0x1b7dd4 to PROBE memory at
R16 = 0xFFFFFFFF_7F827F5F. A probe expects one of two outcomes: valid data
(memory present) or a machine-check / bus-error (memory absent / non-responding
PA). An ACCESS VIOLATION is the surprise -- ACV means "no valid translation /
protection denied," which should not be the verdict for a probe of a mapped
test window. This strengthens the translator-bug side, but does not settle it.

R16 origin: the direct callers (0x5b058, 0x5a6b0 frames) dispatch through r27
procedure-value descriptors (LDA r27,disp(r2); BSR) and pass the address down
(BIS zero,r3,r16), so 0xFFFFFFFF_7F827F5F is computed deeper in the chain. Trace
it in tools/host_decompressor/out/decompiled_src (Ghidra), not raw disassembly.

## The decision to make

Is V4's D-stream translator (mmuLib/Ev6Translator) WRONGLY denying a seg1/
kernel VA that the SRM's page tables legitimately map (translator/protection
bug), OR is 0xFFFFFFFF_7F827F5F a GENUINE wild pointer the SRM built upstream
(in which case the real defect is earlier, and the ACV is V4 behaving
correctly)?

Weigh:
- FOR "translator bug": the +8 / R0-doubling stride is a deliberate address-
  line test -- the SRM INTENDS this VA as testable RAM. A memory test does not
  knowingly walk an unmapped kernel pointer.
- FOR "wild pointer / correct ACV": if the SRM's page tables genuinely have no
  valid, kernel-readable PTE for this VA, ACV is the architecturally correct
  delivery, and the bug is whatever computed R16.

## Evidence needed to decide (the next capture -- Cowork will run it)

At the first ACV (cyc 282090581), dump the D-stream translation decision for
VA 0xFFFFFFFF_7F827F5F:
- Does the PAL walk find a PTE at all, and what are its PTE<valid>, PTE<KRE>,
  PTE<KWE>, fault-on-read bits?
- What PFN / PA does the PTE point to (does it land in RAM, or in the Pchip PA
  window)?
- What exactly makes V4 choose kFaultAcv over kFaultDtbMissDouble here -- the
  protection check (mode/permission) or a translate-path classification?

TOOL GAP: no confirmed per-translation trace knob emits the PTE + protection
bits. The web variant should specify the minimal instrumentation (a gated
Ev6Translator diagnostic keyed on VA 0xFFFFFFFF_7F827F5F, or on cyc 282090581)
that Cowork can add behind a compile guard.

## Out of scope (do NOT chase here)

- The 4138 kFaultDtbMissDouble -- memtest paging churn, handled; not the
  blocker.
- The lone kFaultUnimplemented HW_MTPR (opcode 0x1d, PC 0x13f45) -- log it,
  do not chase unless it proves upstream-causal to R16.
- The post-ACV IER-poll spin at PC 0xec80 -- a downstream symptom; revisit only
  after the ACV is resolved.

## Standing rules (apply to any implementation work)

Discuss-before-code; header + inline docs citing HRM/source + task id;
ASCII(128); surgical Edit; V0/V1/V2 + Processor Support read-only; bounded
trace tails only (the es40_com2_txready_diag.log is 5.2 GB -- never whole-file
grep); doctest CHECK only; full suite + DS10 + DS20 + ES40 boot gate before any
core/chipset commit. Analysis in claude.ai web; capture/edits in Cowork.

## Reference

- logs/faults.log (2026-07-09 run) -- the 51 ACV rows + the PAL double-miss
  walk preceding the first ACV.
- putty_console_p10023_20260709152242.log -- 51 guest "access violation"
  dumps, PC 0x1B7DD4, VA 0xFFFFFFFF_7F827F5F walking.
- pipelineLib/PipelineDriver.h:1175 -- logFaultEvent retire-path call site and
  the fault-code filter.
- coreLib/BoxResult.h:104-135 -- fault-code enum (kFaultAcv=7,
  kFaultDtbMissDouble=14, kFaultNonCanonical=12).
- Standing memory: [[es40-srm-boot-status]], [[emulatr-es40-diag-knobs]].
