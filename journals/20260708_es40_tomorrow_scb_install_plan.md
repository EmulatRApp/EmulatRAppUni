<!--
EmulatR V4 -- ES40 SCB Vector-Install: Next-Session Plan (2026-07-08)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: concrete next-session plan to resolve the ES40 console-PAL null-dispatch
blocker (task #29).  ASCII(128) only.  Discuss-before-code stands.
-->

# ES40 SCB Vector-Install -- Next-Session Plan (2026-07-08)

## State at hand-off (end of 2026-07-07)

ES40 boots to "lowering IPL" (cyc ~1.239B).  After the first interval-timer tick the
console PAL dispatch at 0xda50 loads a service-routine PC from an SCB-style vector table
[ [R21+0x170]=0x1038000 + [R21+0x158]=0x600 ] = [0x1038600] = 0 and HW_RETs to PC 0 ->
halt.  Interrupt DELIVERY is faithful; this is the RETURN/dispatch.

Empirical: EMULATR_GMEM_WATCH range [0x1038000,0x1039000) = ZERO stores over the whole boot.
The vector table is never written at that PA.

Already ruled out (do NOT re-chase):
- SPE derivation at the MTPR seam: already correct (PalEntries.cpp 1711/1729).
- VA<45:44> mask in SPE[2]: already correct (Ev6Translator.h:152).
- So the web analysis's pre-staged fixes B1 and B2 are no-ops; do not land them.
- offset-drop asymmetry in tryKsegTranslate is REAL but NOT this halt's cause (page-base
  watch empty); tracked separately (task #31), verify-exercised before fixing.

Open branch: Q2a (install step never runs -- LEAD) vs Q2b-far (kseg store mistranslates
to a page far from 0x1038000, which the page range watch would miss).

## Step 1 -- split Q2a vs Q2b-far (one instrumented boot)

Need to catch the vector-install store wherever its PA lands.  The range watch only covers
the target page, so add a VALUE-keyed store probe (analysis I1):

  1a. EMULATR_VECWATCH_VAL=0x7170 : in the GuestMemory store sink (next to the GMEM-WATCH
      hooks, memoryLib/GuestMemory.cpp gmemDiagOnStore) OR the MemDrainer store branch,
      log one line (cyc, storePC if available, storePA, value, size) for any store whose
      (value & ~3) == (v & ~3).  Env-gated, zero-cost when unset.
  1b. Key it two ways, cheapest first:
      - Value = 0x1038000 (KNOWN): catches whoever computes/caches the SCB base into the
        impure at [R21+0x170].  That routine is the SCB setup; its neighborhood should also
        install the vector.  No Ghidra needed.
      - Value = CLK_ISR (from Ghidra, the console clock service-routine native entry PC that
        SHOULD sit at 0x1038600): catches the install store itself, wherever it lands.
  1c. Boot with the fast ES40 flags (SPINSKIP on) to the halt.

  What it means: value 0x1038000 was written as a quadword to PA 0x7170. That's 
  the impure save-area cache — [R21+0x170]. Since 0x7170 - 0x170 = 0x7000, this pins R21's 
  impure base at PA 0x7000 (low memory), and confirms [R21+0x170] = 0x1038000 is set exactly 
  as the fatal ISR later reads it back. So the SCB base pointer is computed and cached correctly. 
  This is precisely the "SCB base cached" milestone the plan's Step 2 bisect anchors on.

  Read:
  - Store of CLK_ISR fires at PA X != 0x1038600 -> Q2b-far CONFIRMED.  Decode storeVA vs X
    against the HRM SPE rules; the delta names the translator defect (likely a mode/subfield
    the SCB VA hits that common accesses do not).  Then, and only then, design the surgical
    translator fix + doctest + DS10/DS20 gate.
  - Neither value ever stored anywhere -> Q2a CONFIRMED.  Go to Step 2.

## Step 2 -- Q2a bisect (if the vector is never installed)

The divergence is upstream in console init, between "SCB base cached (0x1038000 stored to
impure+0x170)" and "lowering IPL".  The install must occur in that span.

  2a. From the 0x1038000 value-key run (Step 1b), get the cycle where 0x1038000 is cached
      into the impure.  Snapshot there and at "lowering IPL".
  2b. In a DIAG-PC / --trace window between them, find the routine that should compute
      CLK_ISR and store it to the SCB, and confirm whether V4 executed it or diverged
      (wrong branch, swallowed fault, or a service V4 no-ops).
  2c. Prime suspects (project history):
      - A CSERVE function code V4 no-ops that the real PC264 (OpenVMS) PAL implements.
        Audit execCserve VMS dispatch: 0x44 MTPR_EXC_ADDR (still a no-op -- separate PC<0>
        briefing), 0x45 JUMP_TO_ARC, 0x46 IIC_WRITE, 0x65 MP_WORK_REQUEST.
      - A HWRPB / config-tree value (model, memorySize, SCB layout) V4 reports differently,
        making the console skip or mis-place the install.  Cross-check build_config_tree /
        build_power_hw (the 2026-07-05 authoritative spine).
  2d. Name the seam only after 2b identifies the diverging instruction; then design the fix.

## Side items (log; not on the P00>>> critical path)

- Task #31: kseg offset-drop asymmetry (tryKsegTranslate drops VA<12:0>, applyTlbHit keeps
  it).  Verify whether any kseg-with-offset access is exercised; if so, extend the three SPE
  masks to bit 0 (or OR (va & pageOffsetMask) at translateData:308).  DS10/DS20 + full suite
  gate.  Real bug, but latent.
- ASCII(128) cleanup: execHwMtpr HW_VA_CTL/HW_CC comment block has a non-ASCII glyph
  (analysis sec 10); fix in a separate hygiene edit.
- Release gate unchanged: EmulatrTest (task #25) SRM-boot-to-P00>>> across all models must be
  green, and the git package (task #26) follows P00>>>, NOT before.

## Standing rules
Discuss-before-code; header + inline docs; ASCII(128) only; doctest CHECK only; verify writes
via bash; bounded trace tails; treat V0/V1/V2 + Processor Support read-only; run full suite +
DS10 + DS20 boot-to-P00>>> before any core/chipset commit.

## Diagnostics
The goal: get ES40 past the post-first-tick halt. The console PAL takes an interval-timer interrupt, and on return it loads its service-routine PC from an SCB-style vector at physical 0x1038600 — which reads back zero, so it HW_REI's to PC 0 and halts. Interrupt delivery is already faithful; the bug is the return dispatch reading a null vector.
What we've nailed down:

The SCB base (0x1038000) is computed and cached — once, to the impure at PA 0x7170 (cyc 1238996192).
The base is console_phys_base (0x1010000) + 0x28000 — so it's a real, deliberately-computed address, not garbage.
Yet the physical SCB page is never written (the range watch over [0x1038000,0x1039000) saw zero stores across the whole boot).
