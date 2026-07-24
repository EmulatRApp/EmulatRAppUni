<!--
EmulatR V5 -- Session Journal JRN-VMB-016
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-016 -- The 0x20000000 OS-boot wall: END-TO-END root cause + fix plan

    Doc id   : JRN-VMB-016
    Date     : 2026-07-22
    Status   : ROOT CAUSE COMPLETE, END TO END. Two concrete fixes identified,
               not yet landed. All diagnosis reproduced live (DS20, Mac native).
    Relates  : JRN-VMB-004 (CSERVE START = the handoff; symptom-level, now
               subsumed), JRN-VMB-013 (boot-transfer wall), JRN-VMB-014
               (r21=0 / p_temp -- corrected here: r21 is 0xf01 scratch, real
               p_temp=0x7000). P-1 (faithful, no firmware patch) IN FORCE.
    Subject  : Why EmulatR halts at PC=0x20000000 (halt code 0) trying to boot
               OpenVMS on DS20, traced from the CSERVE-START handoff all the way
               back to a deliberately-faulting unassigned-IPR write in sys__reset,
               and the platform (ISP vs silicon) lever that gates it.
    Memory   : emulatr5-cserve-start-boot-handoff, emulatr5-axpbox-handoff-mechanism.

--------------------------------------------------------------------------------
## 0. Executive summary (the answer)

EmulatR boots the DS20 SRM to `>>>` but HALTS at PC=`0x20000000` (halt code 0 =
RESET) when it tries to transfer to OpenVMS. The transfer is a PAL restart
(CSERVE START -> sys__exit_console -> pal__restore_state -> HW_REI to the OS at
`0x20000000`). `restore_state` reconstructs the OS context by dereferencing
`p_temp` (= register r21) -> `PT__IMPURE(p_temp)` -> the impure/CNS save frame.

The restart reads garbage because **`p_temp` is never established**. `p_temp` is
built by `sys__reset_init` (the PAL power-up CPU init), but EmulatR never runs
that code: at PC `0x13654` `sys__reset` executes `HW_MTPR r?,<scbd 0x2d>` (an
UNASSIGNED IPR write, encoded `0x77e72d40`) and EmulatR raises
`kFaultUnimplemented`, trapping to the OPCDEC vector `0x8400` and ABORTING
`sys__reset` before `sys__reset_init`. Real silicon IGNORES writes to unassigned
IPRs (no fault). So the fault is the defect; it is a deliberate SCAFFOLD (see
Sec 5) that lets the SRM reach `>>>` precisely BY skipping the real-HW init --
which is exactly what drops `p_temp`.

Making `0x2d` a no-op (`EMULATR_2D_NOOP=1`, already present) fixes p_temp
(sys__reset_init runs, `p_temp=0x7000`), but exposes the NEXT skipped step: a
CALIBRATED SETTLING DELAY loop at `0x13e40` (a `SUBQ Rn`-countdown after
programming Pchip1's DMA-window CSRs) that the existing WARP knobs do not
recognize, so EmulatR grinds ~15M+ iterations and never reaches `>>>`.

**Two fixes boot DS20 VMS:** (1) default `0x2d` to no-op; (2) warp the `0x13e40`
countdown delay (a DS20 analog of Task #8's ES40 RSCC spin), ideally a GENERAL
`SUBQ Rn`-to-zero warp. Then `sys__reset` completes to `>>>` and, with a valid
`p_temp`, the CSERVE-START handoff (Option A) resolves `0x20000000 -> pfn 0x2de`.

--------------------------------------------------------------------------------
## 1. Symptom (live, DS20 Mac)

`b dqa0` -> console: "block 0 valid boot block / reading 1226 blocks / bootstrap
code read in / jumping to bootstrap code" -> then `halted CPU 0 / halt code = 0 /
PC = 20000000`. ITBPROBE(0x20000000)=MISS then the ITB fills pfn `0x10000`
(identity, = VA>>13) instead of the real boot image pfn `0x2de` (PA 0x5bc000).
The page table VMB built is CORRECT (physical PTWALK: l3 PTE = 0x2de00001101).

--------------------------------------------------------------------------------
## 2. The end-to-end causal chain

```
[E] EmulatR firmware entry = decompressor at pc=0x900000 (same as AXPBox)
      |  decompressor inflates the console/PAL image; runs to ~cyc 182M
      v
[D] decompressor final hw_ret @0x60079c -> POWERUP vector 0x8000 -> br sys__reset
      |  (the exit IS routed to the PAL power-up -- not the console entry)
      v
[R] sys__reset @0x13540 runs: flush ITB/DTB (0x135a0=ITB_IA, 0x135a4=DTB_IA),
      |  write i_ctl/m_ctl (0x135cc=0x387), ... reaches 0x13654
      v
[F] 0x13654: HW_MTPR r?,<scbd 0x2d> (enc 0x77e72d40) -> EmulatR kFaultUnimplemented
      |  -> TRAP to OPCDEC vector 0x8400 (excAddr=0x13655)  *** ABORT ***
      v
[X] sys__reset_init NEVER RUNS -> p_temp (r21), PT__WHAMI, PT__IMPURE, the impure
      |  frame @0x5000-ish, and PAL temps @0x7000 are NEVER built
      v
[G] r21 stays decompressor scratch = 0xf01 (a `LDA R21,0xf01(R31)` at 0x600594)
      |  -> at the OS restart, PT__IMPURE(0xf01) reads garbage
      v
[H] CSERVE START -> sys__exit_console -> restore_state reads OS context from
      |  absolute low memory -> RESET(0) at PC=0x20000000  *** THE WALL ***
```
The console still reaches `>>>` because it uses the 1-1 PHYSICAL path (guest PAL
p_misc<63> in shadow r22) and never dereferences `p_temp`; only the OS restart
needs it. (p_misc/physical-mode 1-1 mapping is the reason the miss-walk returned
the identity pfn -- see [C] below.)

--------------------------------------------------------------------------------
## 3. Evidence (per link, all live)

- [E][D] DIAG-PC from cyc 0: pc=0x900000 first; the decompressor spins to ~182M;
  its last instr 0x60079c (hw_ret) -> 0x8000 -> 0x8004 (br) -> 0x13540.
- [R][F] DIAG-PC window 0x13540..: the reset flushes TBs + writes ctl regs, then
  at 0x13654 enc=0x77e72d40, next retire is pc=0x8400 with excAddr=0x13655 =
  the OPCDEC trap. (FaultEventLog also shows FAULT[0] cyc~182468822 pc=0x13655
  encoded=0x77e72d40 op=0x1d fault=3 kFaultUnimplemented.)
- [X][G] EMULATR_DIAG_WREG=21 over 0..260M and 180..260M (WMIN gate): r21 is
  NEVER written 0x7000 (nor 0xF000) on the default path. With EMULATR_2D_NOOP=1
  the fault is gone (fault=0 at 0x13654), sys__reset continues, and r21 IS set:
  pc=0x138b8 LDAH r21,0 ; pc=0x138bc LDA r21,0x7000(r21) => p_temp=0x7000.
- [C] p_misc / physical 1-1: the guest EV6 TB-miss handlers branch
  `blt p_misc, ...1to1` on p_misc<63> (PAL shadow r22). Console/VMB run physical
  (r22<63>=1); while set, DTBM_SINGLE/DOUBLE_3/ITB_MISS return pfn=VA>>13
  (identity). sys__exit_console's tail clears it (`bic p_misc,#1<<63`,
  ev6_vms_pc264_pal.mar:4274). Because sys__reset_init/exit_console never
  complete faithfully, 1-1 stays on -> identity pfn. (Ref-confirmed;
  ev6_alpha_defs.mar:40 p_misc=22, ev6_vms_pal.mar:983/1361/4347.)

--------------------------------------------------------------------------------
## 4. What sys__reset_init builds (the p_temp / impure layout)

ev6_vms_pc264_pal.mar (sys__reset :4315 -> sys__reset_init :4475 -> :4610):
```
GET_32CONS p_temp, PAL__TEMPS_BASE, r31  ; p_temp = PAL__TEMPS_BASE
addq       p_temp, r1, p_temp            ; + whami*PAL__TEMPS_SPECIFIC_SIZE (0 primary)
GET_32CONS r2, PAL__IMPURE_BASE, r31      ; impure base
... hw_stq/p r0, PT__WHAMI(p_temp)        ; store whami@+0x98
    hw_stq/p r2, PT__IMPURE(p_temp)       ; LINK impure base@+0x88
```
Runtime-observed value for DS20 ds20_v7_3 is **PAL__TEMPS_BASE = 0x7000** (LDAH
r21,0 ; LDA r21,0x7000). NOTE: the PC264 def files (ev6_pc264_pal_impure.mar)
list PAL__TEMPS_BASE=0xF000 / PAL__IMPURE_BASE=0x5000 -- 0xF000 is the WRONG
variant for DS20; trust the runtime 0x7000. 0x7000 is BELOW palBase 0x8000 =>
free low DRAM, NO image overlap. Verified PA 0x7000-region (and the earlier
0xF000) is DATA, executed 0x over 300M cyc (DIAG_PCLO/PCHI). The reserved band
is HWRPB@0x2000 (populated), impure@~0x5000, paltemps@0x7000 -- all distinct,
all free writable DRAM (below the Tsunami MMIO gate 0x80000000000, Machine.cpp:88).

CNS/PT offsets (ev6_pal_impure.mar / ev6_pal_temps.mar): PT__IMPURE=0x88,
PT__WHAMI=0x98; CNS__PTBR=0x238, KSP=0x250, VPTB=0x268, P_MISC=0x308,
VA_CTL=0x328, EXC_ADDR=0x330(restart PC), I_CTL=0x360, M_CTL=0x390.

--------------------------------------------------------------------------------
## 5. The platform lever -- ISP (engineering/virtual) vs silicon (realHW)

MemDrainer.h:581-608. EMULATR_PLATFORM env:
- unset / "isp" (DEFAULT): EmulatR READ-INTERCEPTS PA 0xBFFC -> 0xCAFEBEEF, so the
  firmware's platform() (apisrm pc264.c reads *(int32*)0xBFFC) returns ISP_MODEL.
  ISP_MODEL routes the SRM down PRE-SILICON SIMULATOR paths -- it SKIPS the
  real-HW timing/probe/IDE steps V4 does not model, so the SRM reaches `>>>`.
- "silicon": no intercept -> REAL_HW path -> "currently unmodeled -> stalls".

So ISP mode and the 0x2d fault are the SAME strategy (skip the real-HW init).
"beefcake" == 0xCAFEBEEF. This is the architect's engineering(virtual) vs realHW
distinction. The 0x2d fault is documented in-code as a KEPT SCAFFOLD
(PalEntries.cpp:2475-2498, EMULATR_2D_NOOP flag; 2026-07-06 rollback notes):
no-op'ing 0x2d routes DS10->0x13d38 device poll, ES40->0x1b7dd4 MMIO DtbMiss,
DS20->the 0x13e40 settling delay (this journal). The fault was kept ONLY because
it currently gets DS10/DS20 to `>>>` -- at the cost of never building p_temp.

--------------------------------------------------------------------------------
## 6. The 0x13e40 settling delay (the 2D_NOOP downstream)

DIAG-PC window 0x13e00-0x13f80, EMULATR_2D_NOOP=1:
```
0x13e14-20: HW_ST -> PA 0x803_8000_0000/40/80/c0  (Pchip1 WSBA0-3 DMA windows)
0x13e2c   : LDAH R12,0xe5      ┐  R12 ~= 0xE4E1C0 (~15,000,000)
0x13e30   : LDA  R12,-0x1e40(R12) ┘
0x13e40   : SUBQ R12,#1,R12    ┐
0x13e44   : BEQ  R12, exit     │  pure countdown spin (no device read inside)
0x13e48   : BR   -12 (back)    ┘
```
Re-armed across the WSBA programming; EMULATR_IDLEWARP/UDELAYWARP/TICKWARP/
RSCCWARP target OTHER loops (RSCC 0x7c304 etc.) and do NOT recognize 0x13e40, so
2D_NOOP + warps still stalled here (confirmed: 236 PCSAMPLEs at 0x13exx, ended
spinning at 0xb740 -- likely a further settle). This is a calibrated hardware
settling delay after chipset (Pchip1) programming, the DS20 analog of Task #8.

--------------------------------------------------------------------------------
## 7. The two fixes (concrete)

FIX 1 -- default 0x2d to a no-op (faithful; silicon ignores unassigned-IPR
  writes). Today gated behind EMULATR_2D_NOOP (PalEntries.cpp:2489, MTPR path;
  also 1958, MFPR path). Making it the default lets sys__reset_init run and build
  p_temp=0x7000 + impure. CAVEAT: other platforms' 0x2d downstream differs
  (DS10 0x13d38, ES40 0x1b7dd4) -- keep the no-op DS20-scoped OR fix those first.

FIX 2 -- warp the 0x13e40 calibrated settling-delay loop. Prefer a GENERAL
  SUBQ-Rn-countdown-to-zero warp (recognize the SUBQ Rn / BEQ Rn / BR-back idiom
  and fast-forward Rn to 0 + advance cycleCount by the skipped count into
  warpCycles) so it also covers 0xb740 and siblings, rather than a one-off PC.
  Model on the existing warp sites (PipelineDriver.h ~286-405, EMULATR_*WARP).

RESULT expected: sys__reset completes to `>>>`; with valid p_temp the CSERVE
START handoff (Option A, EMULATR_CSERVE_START_MODE=guest -- divert to the guest
sys__exit_console; scan signature ITB_IA 0x77FF0300 / DTB_IA 0x77FFA300, mask
0xFC00FF00) runs restore_state faithfully -> resolves 0x20000000 -> pfn 0x2de ->
boot0 executes real code.

--------------------------------------------------------------------------------
## 8. Debug knobs + probes used (all gated/removable)

- EMULATR_2D_NOOP=1        : flip the 0x2d MTPR to a no-op (existing).
- EMULATR_PLATFORM=silicon : realHW path (no 0xBFFC intercept) (existing).
- EMULATR_IDLEWARP / UDELAYWARP / TICKWARP / RSCCWARP : delay-loop warps (existing).
- EMULATR_DIAG_WREG=<n> [+ EMULATR_DIAG_WMIN] : register last-writer trace.
  NEW this session: now honors EMULATR_DIAG_PCLO/PCHI to gate the WREG trace by
  PC region (PipelineDriver.h). Used to isolate r21 writers.
- EMULATR_DIAG_PCLO/PCHI + CYCLO/CYCHI + CAP : PC-window instruction trace (DIAG-PC).
- EMULATR_CSERVE_START_MODE = guest|cpp|off : Option A (divert to guest
  exit_console) / Option B (C++ replicate) / no-op. PalEntries.cpp case 0x42.
- EMULATR_PTEMP_PROBE=1    : MemDrainer -- r21 both banks on double-miss + a
  one-shot PALtemp-region dump (0xF000/0x5000 read via bus.read).
- Reference-oracle scripts: tools/axpbox_ptemp/ (build+run headless AXPBox with a
  PTEMP-REF probe on vmspal_call_cserve -- see that README; macOS AXPBox
  decompressor hangs, run on PC). AXPBox reimplements the VMS PAL in C++ with
  coherent p21=p_temp; SimH has no Alpha target.

--------------------------------------------------------------------------------
## 9. Key addresses / values (DS20 ds20_v7_3, EmulatR runtime)

  decompressor entry     pc=0x900000
  POWERUP vector         0x8000  (br sys__reset)
  sys__reset             0x13540
  0x2d fault instr       0x13654  enc=0x77e72d40 (HW_MTPR scbd 0x2d)
  OPCDEC trap vector     0x8400
  sys__reset_init p_temp 0x138b8/0x138bc  -> r21 = 0x7000
  0x13e40 settle delay   0x13e40..0x13e48  R12~=0xE4E1C0  (Pchip1 WSBA @0x803_8000_0000)
  next settle spin       0xb740 (post-0x13e40, uninvestigated)
  PAL temps base         0x7000     impure base ~0x5000     HWRPB 0x2000 (slot 0x2180)
  r21 scratch (broken)   0xf01  (LDA R21,0xf01(R31) @0x600594, decompressor)
  OS entry / restart PC  0x20000000  -> correct pfn 0x2de (PA 0x5bc000)
  platform flag PA       0xBFFC -> 0xCAFEBEEF (ISP_MODEL)
  Tsunami MMIO gate      >= 0x80000000000 ; Pchip1 base 0x803_8000_0000

--------------------------------------------------------------------------------
## 10. Sequencing / open items

1. Implement FIX 2 (general SUBQ-countdown delay-warp) -- lowest-friction, covers
   0x13e40 + 0xb740 + siblings. Verify sys__reset reaches `>>>` with 2D_NOOP.
2. Implement FIX 1 (default 0x2d no-op, DS20-scoped or after DS10/ES40 downstream).
3. Re-test the CSERVE-START handoff (Option A) with a valid p_temp: expect
   0x20000000 -> pfn 0x2de, boot0 real code. Finalize Option A as the faithful
   default; demote Option B/probes.
4. Clean up gated probes (A+RECON, PTEMP-F000, DIAG_WREG PC-gate) once landed.
5. GROUND TRUTH: a genuine DS/ES silicon trace is NOT available and will not be.
   The authority is therefore the DEC apisrm PALcode/console SOURCE under
   Processor Support (the actual firmware EmulatR runs) plus the AARM/HRM
   architectural rules -- both already used throughout this analysis and
   sufficient to validate the two fixes: (a) p_temp=0x7000 matches the source's
   GET_32CONS p_temp,PAL__TEMPS_BASE (runtime-confirmed); (b) HW_MTPR to an
   unassigned IPR index is architecturally a no-op (AARM), so FIX 1 is faithful
   by rule, not by trace; (c) the 0x13e40 countdown is a pure elapsed-time delay,
   so warping it (FIX 2) reproduces the delay's ONLY effect (time passes) without
   needing silicon's exact counts. VALIDATION = boot PROGRESSION against the
   firmware's own intent (reaches >>>, resolves 0x20000000 -> pfn 0x2de), not a
   silicon diff. (The 2026-07-16 ds20_full_srm_trc.rar is an EmulatR self-trace on
   the ISP default path -- r21=0xf01 -- a useful "before" snapshot, NOT silicon.)

Standing rules: P-1 faithful (no firmware patch); ASCII/hex; surgical Edit;
probes under EMULATR_* env guards; V5 the only write target; V0/V1/V2/V4 +
Processor Support read-only. Both trees (PC mount = record, Mac local) synced.

================================================================================
## PART 2 -- LIVE PC RESULTS (2026-07-22 evening): wall BROKEN, boot0 executes,
##          residual root-caused, faithful fix in flight
================================================================================

### 2.0 The wall is BROKEN
Full stack on the PC via new wrapper `tools/run_ds20_bplus.sh` (exports
EMULATR_2D_NOOP=1 + EMULATR_DELAYWARP=1 + CSERVE mode, execs run_ds20_showdev.sh
-> PuTTY + dqa0 media). Reaches P00>>> at ~88 MHz effective (DELAYWARP collapses
the 0x13e40/0x13e80/0x13ec0 settling delays, ~990M cyc). `b dqa0` -> valid boot
block, reads 1226 blocks, full VMB banner (base=5bc000 image_bytes=99400, init
HWRPB@2000, page table@3ff04000, "jumping to bootstrap code"). RESULT: went from
RESET(0) at 0x20000000 to boot0 EXECUTING (halt PC advanced 0x20000000 ->
0x20000010). CAVEAT: do NOT `u srm` in LFU -- update+exit triggers a ~407e9-cyc
memory re-init spin; plain LFU exit->n->>>> re-inits in ~30e9 cyc.

### 2.1 B+ (Option B, C++ replicate) residual = SEED CLOBBERED
EMULATR_PA_WATCH=0x7000 caught it: B+ seeds PT__VPTB(p_temp=0x7000)=0x200000000,
and ~480 cyc later a `PA-WATCH STORE v=0x0 pc=0x1333c ra=0x62f6c` ZEROES it, so
the guest DTBM_DOUBLE_3 VPTB self-test reads 0 -> crash1 = halt code 0xA. pc=0x1333c
decodes EXACTLY to ev6_vms_pc264_pal.mar:4162-4173 = sys__enter_console's "clear
vptb, switch VA_CTL to 48-bit 1-1 console mode". Caller 0x62f6c = compiled SRM
CONSOLE C code (LDQ r27,0x30(r2); BSR r26 -> 0x1ae328, the console image region).
=> After B+ diverts to 0x20000000, control does NOT stay in OS boot0 -- it lands
back in the SRM console, which runs enter_console and wipes PT__VPTB. HARD PROOF
that C++ PC-divert + PAL-temp seeding cannot hold the CPU in OS virtual mode.

### 2.2 boot0 disasm + the REAL fault (VA 0x10000000 = HWRPB)
boot0 @ VA 0x20000000 (PA 0x5bc000, leafPfn 0x2de) is REAL VMB/APB code:
  0x20000000 d3800000 bsr r28,0x20000004   ; PIC: r28 = own PC
  0x20000004 201f0001 lda r0,1
  0x20000008 203f001c lda r1,28
  0x2000000c 48010720 sll r0,r1,r0          ; r0 = 1<<28 = 0x10000000
  0x20000010 a4800050 ldq r4,0x50(r0)       ; LOAD VA 0x10000050  <- deterministic exit PC
So the exit PC 0x20000010 is boot0's FIRST DATA access. PTWALK BOOT0DATA proved
VA 0x10000000 IS MAPPED -> l3[0]pte=0x100001101 leafPfn=0x1 leafPa=0x2000 = the
HWRPB, valid kernel R/W (V+KRE+KWE). So boot0 reads HWRPB+0x50 -- legitimate; the
console's page table is COMPLETE and CORRECT. Cycle-ordered sequence: seed intact
-> ITBPROBE MISS then HIT va=0x20000000 pfn=2de (code fetch RESOLVED, guest
double-miss passed the VPTB check) -> boot0 runs 4 instrs -> ldq DTB-misses VA
0x10000000 -> 386 cyc later enter_console clears PT__VPTB -> data double-miss reads
0 -> 0xA. CONCLUSION: not a missing map; a MODE gap. B+ transfers the PC but NOT
the execution mode/IPL/PS, so boot0's data-miss (in an inconsistent context) is
pulled back into the console.

### 2.3 How AXPBox / SimH do it (the reference)
AXPBox (axpbox-1.1.2/src/AlphaCPU_vmspal.cpp, vmspal_call_cserve): runs the REAL
guest PALcode. FIRST line `p23 = state.pc` (save CALL_PAL return PC into linkage
reg R23), THEN switch(r16): case 0x42 (CSERVE START) -> `set_pc(0x13781)` = jump
to guest cfw_start (= br sys__exit_console) and let the firmware execute the full
faithful exit (restore_state -> mode/vptb/IPL/TBIA -> hw_ret_stall(p23)). ZERO C++
context replication. SimH has NO Alpha/EV6 target -- not a reference. => the
faithful path is AXPBox's: route CSERVE START to the guest cfw_start and run the
real PAL; do NOT C++-replicate.

### 2.4 THE FIX (in flight): Option A mirrors AXPBox exactly
PalEntries.cpp case 0x42, s_startMode==kStartGuest (EMULATR_CSERVE_START_MODE=guest,
now the wrapper default): (1) set p23 -- `intReg[23]=intShadow[7]=g.pc+4` (CALL_PAL
return PC, both banks vs the shadow swap), exactly like AXPBox's `p23=state.pc`;
(2) divert to the guest sys__exit_console entry (located by the ITB_IA/DTB_IA
signature scan, == cfw_start's br target); (3) NO C++ context seeding -- the real
PAL sets mode/vptb/IPL. Emits `CSERVE-START-A2: mirror-axpbox p23<-... divert->...`.
Previously Option A set excAddr/divertTarget but NOT p23, so exit_console's
terminating `hw_ret_stall(p23)` landed on stale R23 -> reset. Expected outcomes of
the test run: boot0 continues past 0x20000010 (win) | returns to P00>>> (exit_console
resumed console ctx; then watch if the console itself re-jumps to boot0) | new halt.

### 2.5 Fix stack status
- FIX 1 (EMULATR_2D_NOOP=1): sys__reset_init runs, p_temp=0x7000. CONFIRMED.
- FIX 2 (EMULATR_DELAYWARP=1, PipelineDriver.h general SUBQ-countdown warp):
  collapses the settling delays, reaches >>>. CONFIRMED.
- HANDOFF (CSERVE START): Option A (mirror-AXPBox) in flight; Option B (C++ B+)
  proven a dead end (seed clobbered by console re-entry -- section 2.1).
- Diagnostics to remove once Option A lands: CSERVE-DISASM dump, PTWALK
  BOOT0DATA/BOOT0VPTE, CSERVE-START-BPLUS seeding, PA_WATCH default in wrapper.

Cross-refs: memory files emulatr5-cserve-start-boot-handoff.md (running detail) +
emulatr5-axpbox-handoff-mechanism.md (AXPBox CSERVE dispatch).

================================================================================
## PART 3 -- ARCHITECTURAL MODEL: CSERVE dispatch must RUN THE GUEST PAL
##          (the durable lesson; supersedes the "C++ replica" approach)
================================================================================

### 3.1 The core architectural finding
The OS-boot handoff is NOT one intercept -- it is a SEQUENCE of CALL_PAL CSERVE
functions the SRM console issues, each of which on real hardware runs a GUEST PAL
handler. EmulatR's mistake was intercepting CSERVE functions in C++ and either
stubbing them or replicating their effects. That fights the PAL state machine and
loses side-effects. The CORRECT, faithful model (proven by AXPBox, which boots VMS)
is: **route each CSERVE function to its guest PAL handler and let the real firmware
execute.** AXPBox does this literally -- vmspal_call_cserve() is just `p23=state.pc`
then a switch(r16) of `set_pc(<guest PAL handler addr>)`. Zero effect replication.

### 3.2 The guest CSERVE dispatcher (DS20 ev6_vms_pc264_pal.mar)
sys__cserve (line 3852) is the guest dispatcher: it reads r16 (function code) and
branches to the per-function handler. Handlers seen so far:
  cfw_start            :3956  = `br sys__exit_console`      (0x42 START)
  cfw_mp_work_request  :4017  saves r18 -> CNS__WORK_REQUEST (0x65 MP_WORK_REQUEST)
The CNS__WORK_REQUEST slot (checked at :886) is the console<->CPU work mailbox: the
console POSTS a work item (r18) via 0x65, a CPU picks it up. Stub 0x65 => item never
saved => coordination never completes => console re-inits.

AXPBox r16 -> guest PAL addr map (ES40 layout; DS20 addrs differ, locate by scan):
  0x12->0x12e21  0x13->0x12f95  0x14->0x13115  0x15->0x131c1  0x40->0x13249
  0x42->0x13781(cfw_start)  0x43->0x13261  0x44->set_pc(r17)  0x45->0x13289
  0x65->0x132bd(cfw_mp_work_request)  0x66->0x133e9  0x3e->0x1344d
  0x10/0x11 = hw_ldl/hw_stl inline; 0x41 = hw_ldq p21+0x98 (WHAMI) inline.

### 3.3 EmulatR's current CSERVE handling vs the target
EmulatR (PalEntries.cpp) implements some funcs in C++ (0x41 WHAMI, 0x3F GET_BASE,
0x40 HALT, the PUTS/GETS/GET_ENV console-I/O ones) and STUBS the rest ("CSERVE
Defaulted - UnImplemented"), incl. 0x65. Only 0x42 (START) now routes to the guest
PAL (Option A / kStartGuest). The I/O funcs (PUTS/GETS/GET_ENV) legitimately need
host interaction and MAY stay C++ (or route to guest -- the guest PUTS drives the
emulated UART, so routing likely also works). But the PURE-PAL coordination funcs
(START, MP_WORK_REQUEST, and the rest AXPBox set_pc's) MUST run the guest PAL.

### 3.4 The invariant when routing a CSERVE func to the guest PAL
Mirror AXPBox exactly for EACH routed func:
  1. Set p23 (R23) = CALL_PAL return PC (= g.pc + 4). Set BOTH banks (intReg[23] +
     intShadow[7]) to survive the PAL-shadow swap around the divert. This is the
     PAL linkage reg; the guest handler's terminating hw_ret/hw_rei uses it.
  2. Divert PC to the guest handler entry (PALmode, PC<0>=1).
  3. Do NOT replicate the handler's effects in C++; the guest PAL does mode/vptb/
     IPL/CNS-frame/work-mailbox itself.
Locate DS20 runtime addrs by signature scan (as Option A does for exit_console via
the ITB_IA/DTB_IA pair). CLEANER GENERALIZATION: scan for sys__cserve ENTRY once and
route ALL un-implemented CSERVE funcs there with r16 intact -- the guest dispatches
on r16 to the right handler, so one divert covers every pure-PAL function.

### 3.5 Evidence chain that forced this model
- Option B (C++ replicate restart): boot0 fetch resolved BUT the seed (PT__VPTB) was
  clobbered by the guest's own console re-entry -> halt 0xA. C++ effect-replication
  cannot hold the machine in OS mode (Part 2.1-2.2).
- Option A (route 0x42 to guest exit_console, mirror AXPBox p23): 0xA crash GONE,
  exit_console runs clean and returns to the console; the console then loops
  CSERVE 0x65 MP_WORK_REQUEST which EmulatR STUBS -> re-init (Part 2.4). The next
  stubbed pure-PAL func became the next wall -- which is the whole point: the fix
  is to STOP stubbing pure-PAL CSERVE funcs and run the guest PAL for all of them.
- AXPBox (runs real DEC PAL for every CSERVE func) boots VMS; QEMU runs its own
  palcode (boots Linux) so it is NOT a handoff reference, only an EV6-semantics one.

### 3.6 Next implementation step (well-scoped)
Route CSERVE 0x65 (MP_WORK_REQUEST) to the guest PAL, mirroring the 0x42 pattern:
either (a) locate cfw_mp_work_request by scanning for `hw_stq/p r18,
CNS__WORK_REQUEST(p4)`, or (b) preferred -- locate sys__cserve entry and route ALL
currently-stubbed CSERVE funcs there (r16 intact), so the guest dispatcher handles
0x65 and every future one uniformly. Same p23 + divert + no-replication invariant.
Then iterate: run b dqa0, see the next guest-PAL requirement the console needs.

### 3.7 GOVERNING PRINCIPLE (2026-07-23) -- faithful execution IS the goal; boot is a side effect
The objective is NOT to reach >>> or boot VMS for its own sake. It is FAITHFUL
INSTRUCTION EXECUTION -- to make EmulatR a referenceable ORACLE for the Alpha/EV6
architecture. Secondary boot is a SIDE EFFECT of executing the real firmware
faithfully. This reframes the CSERVE work: every C++ no-op/stub of a pure-PAL
function is itself a FAITHFULNESS VIOLATION (EmulatR stops executing the machine
and substitutes a guess), NOT a neutral placeholder. The "tolerated no-op on
silicon" claim was the trap -- disproven by 0x65 (the console needs the real
cfw_mp_work_request side-effect). So the discipline, applied per unhandled func:
  1. INSTRUMENT the default: case to CAPTURE THE MISSING CONTRACT
     (EMULATR_CSERVE_AUDIT=1 -> "CSERVE-CONTRACT-MISSING: func R16/R17/R18/R0
     callerPc.." one per func + periodic). The capture ARBITRATES between
     under-implementing (stubbing a needed func -- the 0x65 bug) and
     over-implementing (behavior silicon lacks).
  2. CROSS-REFERENCE the captured inputs to the guest cfw_* handler in the apisrm
     source to document the func's contract (inputs -> side-effects).
  3. CLOSE it by ROUTING to the guest PAL (mirror-AXPBox: p23=g.pc+4 both banks +
     divert, no C++ effect replication) -- that IS the faithful execution.
  4. VERIFY the guest handler's effects; record the contract as durable oracle
     documentation (a growing CSERVE contract table).
Each pass yields BOTH a closed divergence AND a referenceable account of what
CSERVE func N does and why. The deliverable is the verified account, not "it
booted." This principle generalizes beyond CSERVE to every intercepted CALL_PAL /
IPR / device path: prefer running the real machine; instrument+document any place
EmulatR currently substitutes.

Status: step 1 (capture) implemented -- default: case emits CSERVE-CONTRACT-MISSING
under EMULATR_CSERVE_AUDIT=1 (wrapper default on). Next run records the CSERVE
contract set the DS20 b-dqa0 path exercises; then close each per steps 2-4.

### 3.8 CSERVE contract table (captured, DS20 b dqa0 -- 2026-07-23)
Capture run (run_ds20_showdev_20260723_122330): the SOLE unhandled CSERVE func on
the DS20 b-dqa0 path is 0x65. Three calls total (not a giant loop):
  call#1 cyc=1.17e9 (pre-boot)   R16=0x65 R17=0x0 R18=0x1 R0=0x1 caller=0x1ad938
  call#2 cyc=1.17e9 (pre-boot)   R16=0x65 R17=0x1 R18=0x1 R0=0x1 caller=0x1ad938
  [CSERVE-START-A2 exit_console handoff @cyc 2.10e9]
  call#3 cyc=2.10e9 (post-handoff) R16=0x65 R17=0x0 R18=0x1 R0=0x1 caller=0x1ad938

CONTRACT -- func 0x65 CSERVE$MP_WORK_REQUEST:
  Inputs : R16=0x65 (func), R17=index/selector (0 or 1), R18=work item (=0x1),
           R0=0x1.  Caller = SRM console pc 0x1ad938.
  Guest handler = cfw_mp_work_request (ev6_vms_pc264_pal.mar:4017), reached via the
    full sys__cserve dispatch (compiled because reference_platform=0 AND
    pc264_system=1; both confirmed -- default .iif ndf =0, no override; pc264 PAL).
  Effect : saves R18 -> CNS__WORK_REQUEST(p4) (and, for R18==mp$restart, primary-CPU
    restart logic).  The console polls CNS__WORK_REQUEST (:886) to pick up the item.
  Faithful action = RUN cfw_mp_work_request (route to guest PAL).  EmulatR's no-op
    drops R18 -> console never sees the work -> cold re-init loop.  VERDICT: no-op is
    a faithfulness VIOLATION; CLOSE by routing.

### 3.9 Planned close for 0x65 (routing implementation)
Route the default: case to the guest sys__cserve dispatcher (r16 intact), mirror-
AXPBox invariant: p23(R23)=g.pc+4 in BOTH banks (intReg[23]+intShadow[7]), divert
to sys__cserve entry (PALmode PC<0>=1), NO C++ replication.  Locate sys__cserve by
signature scan = a run of >=8 consecutive `cmpeq r16,#lit,r0 ; bne r0,disp` pairs
(cmpeq r16 lit form: (insn & 0xFFE01FFF)==0x420015A0; bne r0: opcode 0x39, Ra=0) --
the compiled full dispatch table; its start is the entry.  Gate behind
EMULATR_CSERVE_ROUTE for A/B vs the no-op.  Faithful for ALL funcs: dispatches 0x65,
no-ops genuinely-unknown codes exactly as the guest's trailing hw_ret(p23) does.

### 3.10 ROUTING VERIFIED FAITHFUL -> exposes the SMP secondary-CPU-restart wall (2026-07-23)
Implemented + ran (EMULATR_CSERVE_ROUTE=1).  Scan bug fixed first: BNE opcode is 0x3D,
I had 0x39 (=BEQ) -> initial NOT-FOUND.  Refined scan (require cmpeq r16,#0x65 in the
>=8 cmpeq-r16/bne run) + a CSERVE-ROUTE-DISASM dump CONFIRMED sys__cserve=0x12d84 is the
REAL dispatch: literals read 0x10,0x11,0x12,0x13,0x40(halt),0x41(whami),0x42(start),
0x43(callback),... = the cserve func codes.  (My "should be < exit_console 0xa6cc"
assumption was WRONG -- PAL is not laid out in source order.)  So the divert is correct
and 0x65 -> cfw_mp_work_request runs the real handler.  RESULT: instead of proceeding,
0x65 loops tens of thousands of times (call#53248+) and the run dies at MaxCycles with
PC=0x0 fault=7 (kFaultAcv) -- WORSE than the no-op's clean re-init.  ROOT (this is the
"secondary boot" question, confirmed):
 - kernel.c:1351 `for(i=0;i<MAX_PROCESSOR_ID;i++) cserve(MP_WORK_REQUEST,i,MP$RESTART)`.
   MAX_PROCESSOR_ID is COMPILE-TIME (=2 for dual-capable DS20), NOT a runtime probe --
   so the firmware posts MP$RESTART to CPU 0 AND CPU 1 unconditionally; reporting 1 CPU
   does NOT stop it (manifest's cpuCount=1 note is necessary but insufficient).
 - cfw_mp_work_request (ev6_vms_pc264_pal.mar:4017): for MP$RESTART, primary posting to
   itself -> 20$ (skip).  For a SECONDARY (r17=1): save R18->CNS__WORK_REQUEST(impure of
   CPU r17) + GENERATE A HALT INTERRUPT to that CPU via the TIG per-CPU halt registers:
   p7=0x801_3000_0000; CPU0 halt=0x3C0, CPU1 halt=0x5C0 (hw_stq/p p5,0x5C0(p7)).
 - EmulatR runs 1 CPU (UP).
CORRECTIONS (2026-07-23, empirical -- TWO hypotheses in this section were DISPROVEN;
recorded so the trail is honest):
 (A) "TIG mis-routes the 0x5C0 write to CPU0" -- WRONG.  TsunamiTig.h already models the
     per-CPU halt regs as a harmless LATCH (kHaltCpu1: m_halt[1]=v; return; -- no IRQ
     raised, no CPU0 disturbance).  The 0x5C0 write is absorbed exactly like real UP HW.
 (B) "primary-detection guard fails -> primary self-restarts" -- WRONG.  Probe
     (CSERVE-ROUTE-PRIM) shows PT__WHAMI=0, pal$primary[@0x200]=0, cpuSlot=0, and
     nonzero_console_base defaults 0 so get_base=0 -> the check reads mem[0x200]=0.  So
     cmpeq WHAMI,pal$primary and cmpeq r17,pal$primary are 0==0 -> r17=0 DOES skip to 20$.
     Not a self-restart.
ACTUAL ROOT (PCSAMPLE, direct): with routing ON the machine SPINS in a tight console loop
at pc 0x1ad5e0-0x1ad5ec + a PAL routine 0x81e80-0x81fb8, periodically re-posting 0x65.
That is the console's SECONDARY-CPU RENDEZVOUS WAIT: routing POSTS the MP$RESTART work
(saves CNS__WORK_REQUEST + latches m_halt), so the console then WAITS for the secondary
to come online; the no-op dropped the post so no wait engaged (=why no-op "worked").  On
UP the secondary never answers -> infinite spin -> eventual PC=0.  So it is NOT the TIG
delivery and NOT the primary guard -- it is the console C-level secondary rendezvous
(kernel.c/powerup.c) keyed off cpu_present/cpu_available.
VERDICT: CSERVE routing is FAITHFUL and stays (opt-in EMULATR_CSERVE_ROUTE; default OFF
again so the emulator still reaches >>> for other work).  The faithful fix is the SMP-
SECONDARY-RENDEZVOUS substrate = the user's cpuCount=n gate: make the firmware's
cpu_present/cpu_available reflect the running cpuCount so the console does NOT wait on
absent secondaries (UP), and later model N CPU agents so present secondaries consume
their CNS__WORK_REQUEST + halt-IRQ and boot (true SMP).  NEXT (focused session): trace
the console's secondary-wait in kernel.c/powerup.c (what pc 0x1ad5e0 + PAL 0x81e80 poll)
and find where cpu_present is built from the hardware probe; make it = cpuCount.  This is
a substrate lift, not a CSERVE tweak.

### 3.11 DEFINITIVE ROOT (2026-07-23): the CSERVE->guest-PAL divert FIGHTS THE SHADOW BANK
The "SMP rendezvous" read of the PCSAMPLE (3.10) was ALSO wrong -- the FAULT log is the
truth: right after a routed 0x65 during POWERUP the guest takes fault=14 (DtbMissDouble)
at the DTB-miss handler (pc~0x8321) on tiny VAs (0x90/0x10/0x6b8...), cascading to PC=0
ACV (palMode=0). Mode probe CSERVE-ROUTE-MODE at the intercept:
  palMode=0 (NOT in PAL mode) ; p_misc(active R22)=0x..1b3d40 <63>=0 (VIRTUAL) ;
  R22 SHADOW=0x8000..081f00 <63>=1 (PHYSICAL 1-1) ; vptb=0x0.
=> EmulatR intercepts CALL_PAL CSERVE in C++ BEFORE the PAL entry (palMode still 0, shadow
bank NOT swapped in).  The divert jumps to the guest PAL (sys__cserve|1) but the guest then
reads the ACTIVE p_misc (<63>=0 virtual) instead of the PAL-bank/shadow (<63>=1 physical) a
real CALL_PAL entry would have swapped in.  So the guest miss handler does NOT take the
`blt p_misc,..1to1` identity branch -- it WALKS the self-map, but vptb=0 during powerup ->
VPTE at low addrs -> DtbMissDouble cascade -> PC=0.  This is the DOCUMENTED "EmulatR
intercept fights the shadow bank" issue (memory emulatr5-axpbox-handoff-mechanism).  Option
A (0x42->exit_console) only "works" because exit_console's early path tolerates it; same
latent bug.  FAITHFUL FIX (architectural): the CSERVE(->guest-PAL) divert must replicate the
CALL_PAL PAL-ENTRY semantics EmulatR's C++ intercept bypasses -- enter PAL mode + SWAP the
PAL shadow regs (R4-R7, R20-R23 <-> intShadow[]) so the guest handler runs in the correct
bank (p_misc<63>=1 physical during powerup), as hardware/AXPBox do before executing PAL.
GENERALIZATION: EVERY divert-to-guest-PAL (CSERVE routing AND Option A) needs faithful
PAL-entry, not a bare PC set.  NEXT (fresh session): find EmulatR's swapPalShadowRegs /
PAL-entry path and have the divert invoke it (or set the active shadow regs) so the guest
sees the PAL bank; then re-test -- the powerup 0x65 should identity-map, not cascade.
Foundational; do it carefully.  (Prior wrong reads this session, kept for honesty: TIG
mis-route; primary-guard-fail; SMP-rendezvous-wait.  The FAULT+MODE probes settled it.)

### 3.12 FIX LANDED + VERIFIED (2026-07-23): divert path now swaps the PAL shadow bank
FIX: PipelineDriver.h WB no-fault divert path (~line 1610) now routes a divert that CHANGES
PAL mode through coreLib::palModeEnter (native->PAL) / palModeLeave (PAL->native), SDE-gated,
symmetric with the FAULT path's palModeEnter and HW_REI's palModeLeave.  Prior code did a bare
`cpu.pc = divertTarget` -> raised PALmode (PC<0>) WITHOUT the R4-7/R20-23 shadow swap.  Gated
EMULATR_DIVERT_PALSWAP (default OFF for A/B; correctness fix -> make default once soaked).
Also fixed a latent MSVC build break: memoryLib/GuestMemory.cpp missing <new> (std::bad_alloc)
+ <cstdlib> (std::calloc) -- clang pulled them in transitively, MSVC (C2039/C2065) did not.
VERIFIED LIVE (DS20, EMULATR_CSERVE_ROUTE=1 EMULATR_DIVERT_PALSWAP=1):
  - Powerup 0x65 route: DIVERT-PALSWAP#1 nowPal=0 targetPal=1 target=0x12d85(sys__cserve|1)
    sde=1 swapped=1 R22now=0x8000..081f00 (<63>=1 PHYSICAL).  CSERVE-ROUTE count=2 (NOT 54000),
    PC=0 fault=7 cascade count=0.  The routed guest cfw_mp_work_request runs on the correct
    bank -> miss handler identity-maps -> reaches P00>>>.  CASCADE ELIMINATED.
  - b dqa0 handoff: DIVERT-PALSWAP#3 target=0xa6cd(exit_console|1) swapped=1.  halt code = 0
    (NOT 0xA) at PC=0x20000000.  THE 0xA CRASH WAS THE WRONG-BANK ARTIFACT -- gone.
NET: the shadow-swap fix is foundational + correct; it cleaned up BOTH the 0x65 routing cascade
AND the exit_console 0xA.  Now on the FAITHFUL Option A path we land at the ORIGINAL 0x20000000
wall = halt code 0 (RESET) at boot0 entry -- boot0 does NOT execute.  ROOT (unchanged from the
very start): exit_console restores the CONSOLE/CNS context (resume PC=0x20000000) but NOT the
OS-boot execution context (OS PTBR/mode), so boot0 halts at entry.  This is strictly better than
B+ (which hand-installed OS state, ran boot0 4 instrs, then crashed 0xA) because everything
underneath is now faithful.  NEXT DIAGNOSTIC: (a) re-test B+ (CSERVE_START_MODE=cpp) WITH the
swap fix -- its 0xA may have been partly the same wrong-bank artifact; see if boot0 now advances
past 0x20000010; (b) if B+ still 0xA's, finish on the faithful Option A base: understand why
exit_console's restore_state resumes at 0x20000000 without OS mode (CNS-frame-vs-HWRPB-slot).

### 3.13 STATE AT EOD 2026-07-23 (resume here tomorrow)
DONE today: (1) SHADOW-BANK FIX landed+verified (PipelineDriver.h WB divert path swaps via
palModeEnter/Leave, EMULATR_DIVERT_PALSWAP) -- cleaned up the 0x65 cascade AND the exit_console
0xA.  (2) B+ (Option B / kStartCpp) re-tested WITH swap -> STILL 0xA (seed-clobber, not bank);
CONFIRMED DEAD END -> COMMENTED OUT (#if 0) in PalEntries.cpp case 0x42.  (3) Wrapper
run_ds20_bplus.sh now DEFAULTS EMULATR_CSERVE_ROUTE=1 + EMULATR_DIVERT_PALSWAP=1 (full faithful
stack).  (4) MSVC build fix: GuestMemory.cpp +<new>/<cstdlib>.
CLEAN FAITHFUL RESULT (run 162922, Option A + routing + swap, Option B gone -- BPLUS/PTWALK=0):
b dqa0 -> "jumping to bootstrap code" -> CSERVE-START-A2 -> DIVERT-PALSWAP#3 target=0xa6cd
swapped=1 -> halted CPU 0, halt code=0, PC=0x20000000.  NO ITBPROBE on 0x20000000 (boot0 NOT
fetched -- the CPU halts AS IT ARRIVES at boot0 entry).  = the ORIGINAL 0x20000000 wall, now
reached via the FULLY FAITHFUL path (no stub, no C++ replicate).  The 0x65 route (2x, no cascade)
+ exit_console both run the real guest PAL on the correct bank.
RESUME TOMORROW: the halt-0-at-0x20000000 is a SEPARATE deeper issue (the OS-exec context the
handoff should establish -- mode/PTBR/IPL -- isn't right, so arrival at boot0 entry is treated as
a halt, not a fetch).  NEXT PROBE: capture CPU state at the halt -- PTBR (want OS 0x1ff82 not
console), p_misc<63>, palMode, VPTB, and the halt REASON (what set halt code 0) at PC=0x20000000.
That splits: console-PTBR/wrong-mode vs an explicit guest HALT.  Also decide: promote
EMULATR_DIVERT_PALSWAP to the ENGINE default (it is a genuine correctness fix) for the corrected
release; keep the diagnostic probes (DIVERT-PALSWAP debug, CSERVE-ROUTE-MODE/PRIM, CSERVE audit)
env-gated.  Files touched today: PipelineDriver.h (swap+debug), PalEntries.cpp (Option B #if 0,
probes), GuestMemory.cpp (includes), tools/run_ds20_bplus.sh (defaults).

### 3.14 HALT IS GUEST-SIDE RESET, not EmulatR kFaultHalt (2026-07-23 late)
Enhanced HALT-DIAG (PTBR/p_misc/mode at kFaultHalt) added + run on a FRESH build (17:02) with
EMULATR_HALT_DIAG=1 default: HALT-DIAG count = 0.  So the 0x20000000 halt does NOT go through
EmulatR's kFaultHalt path.  The "halt code = 0 / PC = 20000000" line is prefixed [CON COM1] =
GUEST SRM console output.  => it is a GUEST-SIDE RESET (halt code 0 = RESET): exit_console
completes + lands PC at 0x20000000, then the CPU RESETS there (control returns to the SRM console,
which prints it).  Almost certainly a MACHINE-CHECK at boot0's first fetch: the guest ITB-miss
handler runs (EmulatR faithfully) but the OS IPR context is not fully established -> double-fault
-> MCHK -> RESET.  THE DEPARTURE FROM AXPBOX (user Q): AXPBox does ALL translation in C++
(virt2phys: SPE superpage then PTBR walk; TB miss = C++ vmspal_ent_dtbm_*), NEVER runs the guest
miss handler -> tolerant.  EmulatR runs the REAL firmware miss handler -> exposes the OS-context
gap (real silicon would MCHK too).  See memory emulatr5-axpbox-handoff-mechanism (THE DEPARTURE).
NEXT PROBE (staged decision, tomorrow): UNCAP / cyc-filter the FaultEventLog (caps at 64, all used
by cyc 1.21B in powerup, hiding the cyc-1.9B handoff faults) so the boot0-entry fault->MCHK->reset
chain is visible -- that names WHICH IPR is wrong after exit_console (PTBR 0x1ff82? VPTB self-map?
PS/mode/IPL?).  SECONDARY: verify EmulatR models I_CTL[SPE] superpage.  This is the last, well-
scoped guest-reset question; everything upstream (CSERVE routing, shadow swap, exit_console) is now
faithful + correct.

### 3.15 PROBE PREP (2026-07-23 late): FaultEventLog is NOT file-capped; cyc-filter landed
CORRECTION to 3.14's premise: the FaultEventLog is NOT "capped at 64".  The 64
(coreLib/FaultEventLog.cpp kLoudThreshold) gates only STDERR loudness -- the FILE
logs/faults.log receives EVERY fault except routine single kFaultDtbMiss/kFaultItbMiss
(the demand/anomaly set: OPCDEC, unimplemented, DtbMissDouble, ACV, bus error...).
So the handoff faults were on disk all along; 3.14 was reading stderr (only the first
64, all powerup).  Confirmed on the CURRENT Mac logs/faults.log (a STALE Jul-22
pre-shadow-fix B+ run, NOT the current faithful path): 16,987 rows, ALL
kFaultDtbMissDouble at pc=0x8321 encoded=0x6c845000 palMode=1, VAs marching
0x200407a48/..a50/..a58 (stride 8) -- the VPTB-self-map walk cascade of the OLD
wrong-bank path (matches 3.11), to cyc ~8.7e11.  The file truncates each run, so it
holds only the most-recent run's faults; a FRESH run on the current build is needed
for the faithful-path answer.

CHANGE LANDED (coreLib/FaultEventLog.cpp, env-gated, zero-cost off; syntax-verified
c++17, NOT yet integration-built -- build+run on PC):
  EMULATR_FAULT_LOUD=<n>              -- raise/lower the stderr loud threshold (dflt 64).
  EMULATR_FAULT_CYCLO / _CYCHI=<cyc>  -- ALSO emit any fault with cyc in [LO,HI] loud,
                                         regardless of threshold (the "cyc-filter").
Rationale: makes the cyc~1.9B handoff-window faults visible on the console without a
16k-row file grep, and independent of the powerup burst that eats the first 64.

NEXT (PC, focused): (1) build relwithdebinfo; grep-confirm EMULATR_FAULT_CYCLO in the
exe.  (2) run tools/run_ds20_bplus.sh (full faithful stack: 2D_NOOP+DELAYWARP+
CSERVE_ROUTE+DIVERT_PALSWAP) WITH EMULATR_FAULT_CYCLO/_CYCHI bracketing the
CSERVE-START-A2 handoff cyc (~2.1e9 per 3.8; widen to e.g. 1.9e9..2.3e9) + b dqa0.
(3) read the loud FAULT[...] lines AND the fresh logs/faults.log tail: the first
non-DtbMiss fault AFTER the exit_console divert (DIVERT-PALSWAP#3 target=0xa6cd) names
the boot0-entry fault -> which IPR is wrong (PTBR 0x1ff82 vs console? VPTB self-map?
PS/mode/IPL?).  SECONDARY: verify I_CTL[SPE] superpage modeled.  Everything upstream
(CSERVE routing, shadow swap, exit_console) is faithful; this is the last guest-reset
question.  Files touched: coreLib/FaultEventLog.cpp (+<cstdlib>, FaultLoudCfg struct,
loud-gate).
