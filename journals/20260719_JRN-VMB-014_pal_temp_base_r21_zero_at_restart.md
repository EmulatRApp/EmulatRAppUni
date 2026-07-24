<!--
EmulatR V4/V5 -- Session Journal JRN-VMB-014
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active tree (emulatrappuniv5,
         branch v5-tb).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic, Cowork).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Per docs/notes/ADR-0001-source-file-headers.md (Markdown header as HTML comment).
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-014 -- L1 root-cause hunt for the 0x20000000 boot-transfer wall: PA 0x98 = PT__WHAMI(r21) with r21 (the PALtemp/impure memory base) = 0. The console->OS restart reads the PALtemp region at absolute low memory and vectors into the RESET(0) entry.

    Doc id      : JRN-VMB-014
    Status      : OPEN -- SOURCE ANALYSIS COMPLETE, RUNTIME CONFIRMATION PENDING.
                  Advances JRN-VMB-013 lead L1 (the "HW_LD from PA 0x98 = 0" at
                  guest pc 0x8320). L1 is now identified in source: PA 0x98 is
                  PT__WHAMI, an offset into the in-MEMORY PALtemp region whose
                  base is register r21 (p_temp). An effective address of ABSOLUTE
                  0x98 means r21 = 0 at that point. Because the same reset/switch
                  flow bases ALL its PALtemp + impure + CNS accesses on r21, a
                  zero r21 is fatal and is the strong candidate for the RESET(0)
                  halt at exc_addr=0x20000000. NOT yet confirmed at runtime.
    Date        : 2026-07-19
    Model       : claude-opus-4-8 (Cowork). Device bridge to tim-hpz640, tree
                  D:\EmulatR\emulatrappuniv5 (branch v5-tb, HEAD 988dd6f).
                  Reference sources read natively over the bridge; emulator NOT
                  built/run this session (Cowork cloud sandbox cannot run it).
    Relates to  : JRN-VMB-013 (BOOTTRACE + leads L1-L4), JRN-VMB-012 (DTB fix,
                  the win that exposed this wall), JRN-VMB-003 (boot0 not reached).
    Method note : L1 chosen as "source hunt now" -- pure static reverse
                  engineering of the guest firmware (apisrm) + the emulator tree.
                  No emulator run was performed; all BOOTTRACE numbers quoted are
                  from JRN-VMB-013, not re-captured here.
    Encoding    : ASCII-128.  Hex radix.

---

## 1. Question inherited from JRN-VMB-013 (L1)

JRN-VMB-013 Sec 2/3 localized the 0x20000000 transfer failure to the PAL layer
and ranked L1 as: guest pc 0x8318-0x8324 executes HW_LD (physical, opcode 0x1b,
enc 0x6c845000) from PA 0x98 and gets R4 = 0; "if the PAL expects a non-zero
value there (a restart block field, a HWRPB pointer, a per-CPU slot), a 0 could
steer the transfer decision." This session traces PA 0x98's provenance.

## 2. CONFIRMED from source -- the transfer is a PAL restart, not a jump

The console->OS handoff is NOT a branch to 0x20000000. In the SRM console VMB
(apisrm/apisrm/ref/boot.c), the boot routine:

  - boot.c:~1447  slot->HWPCB.VMS_HWPCB.KSP[0] = 0x20000000 + (ptx/2)*PAGESIZE
  - boot.c:~1467  write_ipr(APR$K_KSP, ...KSP); write_ipr(APR$K_PTBR, ...PTBR);
                  write_ipr(APR$K_VPTB, hwrpb->VPTBR);
  - boot.c:~1470  write_pc( slot->HALT_PC );        // HALT_PC = 0x20000000
  - boot.c:1549   printf("jumping to bootstrap code\n");
  - boot.c:~1554  console_exit_use_tt();  return( msg_success );

So control reaches the OS by: set HALT_PC/KSP/PTBR/VPTB, exit the console, and let
the PAL "continue/restart" resume the halted primary CPU at HALT_PC via HW_REI.
The bootstrap image was read to PA 0x5bc000 and the page table maps VA 0x20000000
-> that image. ITBPROBE(0x20000000) firing 0x (JRN-VMB-013/012) proves the CPU
NEVER fetched at 0x20000000 -- it failed inside the restart, before the resume.

## 3. CONFIRMED from source -- halt reason 0 = RESET

The "halted CPU 0 / halt code = 0 / PC = 20000000" line is guest console output:

  - kernel.c:2860  pprintf("\nhalted CPU %d\n", id);
  - kernel.c:2849  pprintf("  halt code = %d\n  ", haltcode);
  - hwrpb.c:2114   hwrpb_load_halt(): 
       :2120  slot->HALT_PC  = impure->cns$exc_addr;   // -> 0x20000000
       :2125  slot->HALTCODE = impure->cns$hlt;         // -> 0
  - dp264_info.c:105 hlttxt[] table:  "RESET"=0, "HW_HALT(^P)"=1, KSP_INVAL=2,
       SCBB_INVAL=3, PTBR_INVAL=4, "SW_HALT(Halt inst)"=5, DBL_MCHK=6,
       MCHK_FROM_PAL=7, START=64, CALLBACK=66, MPSTART=68, UNKNOWN=0xFF.

Halt code 0 = RESET (NOT a HALT instruction, NOT KSP/SCBB/PTBR_INVAL). exc_addr
carries the intended resume PC (0x20000000) across the reset entry. So: the CPU
entered the PAL RESET vector during the restart while its saved PC was still the
un-executed 0x20000000. This matches ITBPROBE=0 exactly.

## 4. CONFIRMED from source -- PA 0x98 is PT__WHAMI in the PALtemp MEMORY region, based at r21

Offset 0x98 is a named slot in the PAL's in-memory scratch structures:

  - ev6_pal_temps.mar:49   PT__WHAMI  = ^x98      (PALtemp region offset)
  - ev6_pal_temps.mar:47   PT__IMPURE = ^x88      (adjacent -- impure base ptr)
  - ev6_pal_impure.mar:40  CNS__R17   = ^x98      (alt reading: saved-R17 in the
                                                   CNS/impure area)

Crucially, the PALtemp region is accessed as PHYSICAL MEMORY through a base
register the PALcode calls p_temp:

  - ev6_osf_pal.mar:777    ";  p21  = r21  address of pal temps in memory (p_temp)"

So p_temp IS general register r21. The reset/switch flow bases every PALtemp,
impure and CNS access on r21, e.g.:

  - ev6_osf_pal.mar:1173+  hw_ldq/p r25, PT__PTBR(p_temp);  ...PT__VPTB(p_temp)
  - ev6_osf_pal.mar:2214   hw_ldq/p p4,  PT__IMPURE(p_temp)   ; impure base
  - ev6_osf_pal.mar:2226   hw_ldq/p p4,  PT__WHAMI(p_temp)    ; get whami
                           mulq p4, ^x400, p6                 ; whami * 0x400
  - then  hw_stq/p r16, CNS__IER_CM(p4)  with p4 = the impure base just loaded.

The BOOTTRACE effective address was ABSOLUTE 0x98 (memAddr=0x98). PT__WHAMI(r21)
== 0x98 requires r21 == 0. Therefore at guest pc 0x8320 the PALtemp base r21 is
ZERO -- the PAL is reading the PALtemp region at physical 0 instead of its real
reserved base.

## 5. HYPOTHESIS (needs runtime confirmation) -- r21=0 is the transfer killer

If r21 = 0 through this flow, then the SAME instruction stream also does:
  PT__IMPURE(r21=0) -> load from PA 0x88 -> impure base = 0
  CNS__IER_CM(0), CNS__PTBR(0), CNS__KSP(0), ... -> loads/stores at ABSOLUTE low
  memory (0x88, 0x98, 0x228..0x268, 0x390..0x3c0) instead of the real impure
  area. The restart cannot reconstruct the OS context; it re-enters / loops the
  reset path and finally reports the RESET(0) halt with exc_addr still 0x20000000.

This unifies all three JRN-VMB-013 BOOTTRACE regions under ONE cause:
  - 0x8318 HW_LD PA 0x98 = 0     -> PT__WHAMI(r21=0).
  - 0xd280 repeating HW_MTPR burst + HW_REI (5+x) -> the reset/switch handler
    re-entering without progress (can't load real state through r21=0).
  - 0x117c0 bit-scan of mask R5  -> a whami/size/cpu-mask scan fed garbage.

IMPORTANT nuance (why this hid so long): for the PRIMARY CPU, whami == 0
legitimately. So PT__WHAMI reading 0 is "accidentally survivable" -- whami*0x400
= 0 either way. The load that LOOKS harmless (R4=0) is the visible symptom; the
FATAL sibling is PT__IMPURE(r21=0) in the same flow. JRN-VMB-013's instinct
("should hold a non-zero value") is right in spirit but points at the wrong slot:
the bug is not a missing value at 0x98, it is a zero BASE (r21) making 0x98
absolute.

## 6. Emulator-side trace (V5 tree) -- where r21 comes from

Grep of the V5 C++ tree (coreLib/cpuLib/palBoxLib/systemLib/iprLib):

  - There is NO emulator code seeding absolute PA 0x98 (the only 0x98 hit is
    chipsetLib/TsunamiDpr.h:207 put(0x98,0x25) -- an unrelated PCI temp). Correct:
    the guest firmware builds HWRPB/impure, so PA 0x98 has no emulator writer.
  - EmulatR keeps a HARDWARE PALtemp IPR array, coreLib/CpuState.h:510
    uint64_t palTemp[32], IPRs HW_PAL_TEMP_16..31 = 0x210..0x21F (HW_IPR.h). This
    is a DIFFERENT thing from r21: the guest reset flow reads its scratch from
    MEMORY at base r21 (hw_ldq/p), not from these IPRs. Do not conflate them.
  - r21 has a SHADOW: CpuState.h:124 "intShadow[4..7] -- shadows for R20, R21,
    R22, R23" (so R21's shadow = intShadow[5]). EV6 swaps R4-7/R20-23 (+R8-14,R25)
    to the shadow bank when PAL runs with shadow enable (SDE). If the console->OS
    restart runs in the shadow bank and R21's shadow is 0 (never loaded with the
    PALtemp base, or clobbered by a swap), r21 reads 0 EXACTLY where we see it.
    This dovetails with the project's existing shadow-swap history
    (20260622_shadow_bank_swap_case_count_briefing.md; the CLAUDE.md decompressor
    note "save/restore of SROM params R16-R21").
  - SROM/register seeding lives in systemLib/SrmLoader.h, systemLib/FirmwareLoader.h,
    systemLib/Machine.cpp, palBoxLib/grains/PalEntries.cpp (candidates for where
    r21 is/should be established). NOT yet read this session.

## 7. Next actions (ranked)

  N1 (runtime, decisive). Re-run the JRN-VMB-013 BOOTTRACE recipe but capture r21
     (and its shadow intShadow[5]) at pc 0x8320 AND at the last instruction before
     console_exit / the restart. Predicted: r21 = 0 at 0x8320. Then walk backward
     to the LAST pc where r21 held the real PALtemp base -- the instruction/PAL
     transition between the two is the defect (a shadow swap or a swpctx/swppal
     path that fails to carry r21).
       env: EMULATR_TRACE_ON_BOOTSTRAP=1 EMULATR_BOOTTRACE=1 EMULATR_DIAG_CAP=<n>
            (BOOTTRACE already prints destination reg writes; extend/point it at
             r21 reads, or add a one-shot REGWATCH on r21 == 0 with pc capture.)
  N2 (runtime, cheap corroboration). LOAD-WATCH physical addresses 0x88 and 0x98:
     confirm both are read in the same window (PT__IMPURE + PT__WHAMI), proving a
     shared zero base rather than an isolated 0x98 quirk. A STORE-WATCH on 0x88/
     0x98 should show NOTHING writes them (they are phantom targets of r21=0).
  N3 (source, emulator). Read systemLib/SrmLoader.h + FirmwareLoader.h +
     Machine.cpp + palBoxLib/grains/PalEntries.cpp for: (a) is R21 seeded with the
     PALtemp memory base at SROM handoff/powerup, and (b) does the EV6 shadow-bank
     swap preserve R20-R23 correctly on the PAL entry that runs the restart. This
     is where the fix will land IF N1 confirms r21=0 originates emulator-side.
  N4 (source, guest cross-check). Confirm on real silicon/SROM the contract that
     r21 is loaded with the PALtemp base at powerup (ev6_osf_pal.mar reset header,
     line 1129 "Jumped to by the SROM code on reset ... r15-r21 standard srom
     parameters") and is expected to persist to the console->OS restart. If the
     firmware itself is supposed to RELOAD r21 from an IPR before the restart and
     the emulator models that IPR as 0, the fix is IPR-side, not shadow-side.

## 8. Confidence + caveats

  - Sec 2, 3, 4 are CONFIRMED from the apisrm reference source and are independent
    of any run.
  - Sec 5 (r21=0 is THE cause) is a strong HYPOTHESIS: it is the only reading that
    makes memAddr=0x98 absolute AND unifies the three BOOTTRACE regions AND
    explains RESET(0). It is NOT yet confirmed at runtime; N1 settles it in one
    capture.
  - Alternative not excluded: 0x98 = CNS__R17 with a zero CNS/impure base (same
    root -- a zero base pointer, different named slot). N2 disambiguates: if 0x88
    (PT__IMPURE) is co-read, it is the PALtemp/r21 reading.
  - This session did NOT build or run the emulator (Cowork cloud sandbox cannot).
    All runtime numbers are inherited from JRN-VMB-013.

## 9. Standing rules

  ASCII-128; hex; surgical Edit; probes under EMULATR_* env guards; discuss before
  code (P-0). V5 is the only write target; V0/V1/V2/V4 and Processor Support are
  read-only.
