# JRN-VMB-004 -- CSERVE START (0x42) is the 0x20000000 boot-handoff root cause

    Doc id   : JRN-VMB-004
    Date     : 2026-07-22 (Mac native run; PC record synced)
    Status   : ROOT CAUSE CONFIRMED. Fix in progress (Option A default, B gated).
    Relates  : JRN-VMB-001/002/003 (ITB-miss frontier + stale-ITB hypothesis --
               now superseded: the stale ITB is a downstream symptom, not the
               cause). P-1 (faithful, no firmware patch) IN FORCE.
    Subject  : The firmware->OS handoff at VA 0x20000000 is CSERVE function 0x42
               (START). EmulatR stubs it as a no-op; the reference does
               br sys__exit_console (restore context + TBIA + restart).

## 1. Live reproduction (DS20, Mac)

Binary: out/build/relwithdebinfo/Emulatr (DIAG probes compiled in).
Run: EMULATR_PTBR_DIAG=1 EMULATR_ITBPROBE_VA=0x20000000, model=DS20, drive
`b dqa0` via console_drive.py. Result:

    CSERVE entry: func=66 (0x42) START  pc=0x1ae398 ... cyc=874809139
    [CON COM1] halted CPU 0
    [CON COM1] halt code = 0   PC = 20000000

    PTBR-DIAG (SWPCTX writes)   : 0
    ITBPROBE 0x20000000 hits    : 0

VMB ran (cyc 184M -> 874M setup) then `CSERVE START` fires immediately before the
halt. PTBR was never installed (SWPCTX -- the only cpu.ptbr writer -- never ran)
and 0x20000000 was never fetched/translated (ITBPROBE = 0). So it is NOT a
translation/stale-ITB problem: it is the RESTART/handoff that never executes.

## 2. Reference mechanism (apisrm, read-only)

- `ev6_vms_pc264_pal.mar:3956`  cfw_start: `br r31, sys__exit_console`
- `ev6_vms_pc264_pal.mar:4245`  sys__exit_console:
    bsr p7, pal__restore_state    ; restore PTBR/PC/PS/SP/GPRs from CNS save area
    hw_mtpr r31, EV6__ITB_IA       ; flush ITB  (clears any stale 0x20000000)
    hw_mtpr r31, EV6__DTB_IA       ; flush DTB
    ... set up + tear down 1:1 mapping, clear locks ...
    hw_mtpr r31, EV6__IC_FLUSH     ; flush icache
    hw_ret_stall (p23)             ; RESTART at restored PC (0x20000000)
- `ev6_vms_pal.mar:6228`  pal__restore_state: `hw_ldq/p r1, PT__IMPURE(p_temp)`
  then restores from the impure/CNS structure (CNS__PTBR, CNS__PC, CNS__PS,
  CNS__SP, GPRs, FPCR).
- `ref/boot.c`: the console boot command builds the 3-level page table
  (0x20000000 -> base_pfn), populates the per-CPU HWRPB slot + CNS save area
  with the restart context, then hands off via START.

## 3. EmulatR defect

`palBoxLib/grains/PalEntries.cpp` CSERVE dispatch, `case 0x42` (~line 628):

    case 0x42: {   // CSERVE$START -- start / release a secondary CPU
        // ... "no secondaries to start; return with nothing done."
        return r;                // R0 untouched
    }

Correct for an SMP-secondary start, WRONG for the primary boot transfer, which
routes through the same START -> exit_console path. The no-op strands the entire
handoff and explains every symptom (no PTBR install, no ITB flush, no restart,
HALT at 0x20000000). It is also why AXPBox/es40 boot VMS and V5 does not.

## 4. Fix plan

- Option A (default, most faithful): divert CSERVE START to the guest PAL
  exit_console; the real PAL runs restore_state + TBIA + hw_ret. EmulatR already
  supports CALL_PAL divert (see CSERVE 0x44: `r.divert=true; r.divertTarget`).
  Open item: obtain the guest exit_console / cfw_start PC in the loaded PAL image
  and confirm the guest exit_console path runs (ITB_IA/DTB_IA/hw_ret + CNS reads).
- Option B (gated behind an env flag, for testing/fallback): replicate
  exit_console in the C++ 0x42 handler -- read CNS via PT__IMPURE, install
  cpu.ptbr/pc/PS/SP, flush ITB+DTB, divert to CNS__PC.

## 5. Verification (post-fix)

Re-run the DS20 `b dqa0` reproduction with EMULATR_ITBPROBE_VA=0x20000000: expect
the 0x20000000 fetch to now MISS -> walk the new table -> fill PFN=base_pfn (not
0), boot0 to execute past 0x20000000, and PTBR-DIAG / restore to show a nonzero
PTBR install. Next wall after that is the OS device path (see the AXPbox/EmulatR
interface gap 3-way journal: SCSI or IDE-DMA+IRQ, keyboard, DMA).

## 6. UPDATE 2026-07-22 -- Option B landed + verified; downstream PTE issue

Option B implemented (PalEntries.cpp case 0x42, gated EMULATR_CSERVE_START_RESTART):
installs PTBR (0x1ff82 = PT @ 0x3ff04000) + VPTB (0x200000000, from hwrpb.vptb_va
+120, merged into VA_CTL/I_CTL) + ITB/DTB flush + divert to halt_pc. VERIFIED
live (DS20 b dqa0): boot0 at 0x20000000 now FETCHES (ITBPROBE MISS->walk->fill),
no longer an instant halt. The JRN-VMB-001/002/003 wall is broken.

DOWNSTREAM (new): the walk resolves VA 0x20000000 -> pfn 0x10000 / PA 0x20000000
(empty -> boot0 halts after 1 insn), NOT base_pfn 0x2DE / PA 0x5bc000. VPTB 0 ->
0x200000000 did not change the pfn, so the L3 PTE VMB built for 0x20000000 (walk
l1pt[0]->l2pt[0x40]->l3pt1[0], PT @ PA 0x3ff04000) physically holds pfn 0x10000.
Next: inspect that PTE physically. Option A (divert to guest exit_console) still TODO.
