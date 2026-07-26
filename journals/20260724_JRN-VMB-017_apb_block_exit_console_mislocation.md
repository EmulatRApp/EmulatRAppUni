<!--
EmulatR V5 -- Session Journal JRN-VMB-017
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-017 -- APB block ROOT-CAUSED: Option A diverts to a TB-flush leaf,
#                NOT sys__exit_console; restore_state never runs

    Doc id   : JRN-VMB-017
    Date     : 2026-07-24
    Status   : SEEMS A REASONABLE HYPOTHESIS -- NOT "WILL FIX".  Live
               session record (DS20, Mac native); the Sec 5 fix options are
               proposals only (discuss-first), not a commitment.  Mac-side
               companion to the repo's canonical JRN-VMB-017
               (20260724_JRN-VMB-017_exit_console_divert_target_rootcause.md).
    Relates  : JRN-VMB-016 (subsumes its 3.13/3.14 open question "why halt 0
               at 0x20000000"; the 3.14 machine-check hypothesis is DISPROVEN),
               JRN-VMB-004 (exit_console mechanism).  P-1 IN FORCE.
    Subject  : Why the faithful Option A handoff halts with code 0 at
               PC=0x20000000 without ever entering APB: the exit_console
               signature scan matched a bare TB-flush/return leaf at 0xa6cc.
               The REAL sys__exit_console is 0x13480.

--------------------------------------------------------------------------------
## 0. Executive summary (the answer)

APB (the primary bootstrap at VA 0x20000000) is never entered because
pal__restore_state never executes.  EmulatR's Option A CSERVE-START divert
targets 0xa6cc -- believed to be sys__exit_console -- but 0xa6cc is a NOP
inside a small generic TB-FLUSH LEAF (flush ITB/DTB/icache, hw_ret_stall(p23),
NO bsr pal__restore_state).  The divert therefore executes:

    flush ITB/DTB -> IC_FLUSH -> hw_ret_stall(p23)

with p23 = the CALL_PAL return PC that Option A itself installed (0x1ae39c,
the console C code after cserve(START)).  Control returns STRAIGHT to the
console as if the START call did nothing; the console concludes the "started"
CPU halted, prints "halted CPU 0 / halt code = 0 / PC = 20000000" from its
HWRPB-slot bookkeeping, and drops to P00>>>.  The CPU never leaves the
console world; 0x20000000 is never fetched (ITBPROBE armed on it: 0 events).

The bitter part: the CNS restart frame is FULLY POPULATED and correct.  The
console's boot path did everything right; EmulatR jumps into the wrong PAL
routine to consume it.

The real sys__exit_console is at PA 0x13480 (bsr r7 -> pal__restore_state at
0xe3a0), located and instruction-verified live this session.

--------------------------------------------------------------------------------
## 1. Live reproduction (DS20, Mac native, 2026-07-24)

Binary: out/build/relwithdebinfo/Emulatr (built 2026-07-23 16:56, all JRN-016
fixes in: DIVERT_PALSWAP, CSERVE_ROUTE, DELAYWARP, 2D_NOOP).  Full faithful
stack env (the run_ds20_bplus.sh defaults), driven headless over the console
TCP port.  NOTE Mac flow quirk: the ds20_v7_3.exe firmware stops at the LFU
"options firmware / standard console update" prompt; <return> -> UPD> ->
`exit` -> re-init (~4e9 cyc) -> P00>>>.

    b dqa0 -> "jumping to bootstrap code"
    CSERVE-START-A2: p23(r23)<-0x1ae39c divert->0xa6cc  cyc=5193631080
    DIVERT-PALSWAP#3 target=0xa6cd sde=1 swapped=1 R22now=0x8000060000001f04
    [CON COM1] halted CPU 0 / halt code = 0 / PC = 20000000  (+~2.4 ms)
    P00>>>

Reproduced twice in one process (second handoff cyc=10306489449, identical).

--------------------------------------------------------------------------------
## 2. Evidence chain (all live, this session)

E1. ITBPROBE (compile-gated EMULATR_BRINGUP_PROBES, default key 0x20000000,
    compiled into this binary): ZERO hit or miss events across the whole run.
    0x20000000 is never fetched -- not even attempted.  This kills the
    JRN-016 3.14 hypothesis (MCHK at boot0's first fetch): there IS no fetch.

E2. HALT-DIAG (EMULATR_HALT_DIAG=1): 0 events.  No EmulatR-side kFaultHalt.
    The "halted" message is guest console OUTPUT (bookkeeping), as 3.14 found.

E3. faults.log UNCAPPED (the file has always logged every fault; only the
    stderr echo caps at 64 -- the 3.14 "uncap" plan was already satisfied by
    reading the file).  Post-handoff faults are the console's NORMAL
    tiny-VA/VPTE DtbMissDouble traffic (same signature as powerup-era noise,
    e.g. va=0x90/0x6b8/0xffd50); no new fault class, no cascade, no MCHK
    around the handoff.  The handoff is CLEAN -- it just goes back to the
    console.

E4. SRM-examine of guest memory at the halt (console still live at >>>):
      PA 0x7088 (PT__IMPURE(p_temp=0x7000)) = 0x4200      <- impure/CNS base
      CNS frame @0x4200 (offsets per ev6_pal_impure.mar):
        +0x238 CNS__PTBR     @0x4438 = 0x3FF04000          <- OS page table PA
        +0x268 CNS__VPTB     @0x4468 = 0x2_0000_0000       <- OS self-map base
        +0x308 CNS__P_MISC   @0x4508 = 0x81F00  (<63>=0)   <- OS virtual mode
        +0x330 CNS__EXC_ADDR @0x4530 = 0x20000000          <- APB entry PC
      HWRPB per-CPU slot HWPCB @0x2180: KSP=0x200E0000, PTBR(+0x20)=0x1FF82.
    => The console populated the restart context COMPLETELY and CORRECTLY.
    (JRN-016 Sec 4 note: runtime impure base is 0x4200 on ds20_v7_3, not the
    def-file 0x5000.  p_temp=0x7000 stands.)

E5. Disassembly of the divert target (live memory, SRM examine -l):
      A6C0: 44000400   (alignment marker)
      A6C4: 47FF041F   nop
      A6C8: 47FF041F   nop
      A6CC: 47FF041F   nop            <- Option A's "exit_console" ENTRY
      A6D0: 77FF0310   hw_mtpr ITB_IA <- the pair the scan matched
      A6D4: 77FFA380   hw_mtpr DTB_IA
      A6E0: 77FF1310   hw_mtpr IC_FLUSH
      A6F4: 7BF7A000   hw_ret_stall (r23)
    A bare flush/return LEAF.  No bsr, no restore_state, no CNS access.
    (Sibling leaf at 0xa700 uses the invalidate-single scbds 0x02/0xA2.)

E6. Full PAL-region scan (PA 0x8000-0x16000 dumped via console examine,
    301 KB, pal_dump_8000.txt): exactly 4 ITB_IA/DTB_IA adjacent pairs:
      0xa6d0  -> the flush leaf above (what the scan found FIRST)
      0x132c0 -> inside sys__enter_console  (bsr r7 -> 0xdf80 @0x13280)
      0x134a0 -> inside sys__exit_console   (bsr r7 -> 0xe3a0 @0x13480)
      0x135a0 -> inside sys__reset (I_CTL=0x387 write @0x135cc, JRN-016 [R])
    NONE of the pairs is IMMEDIATELY preceded by the bsr: alignment NOPs (+ a
    0x44000400 marker) sit between.  The "entry = pair - 4" rule can never
    find the real entry in this PAL build.

E7. Callee verification:
      0xdf80: hw_ld r4,0x088(r21); hw_st r31,0(r4); hw_st r20,8(r4); ...
              = pal__save_state   (PT__IMPURE(p_temp) then stores)
      0xe3a0: hw_ld r1,0x088(r21) = pal__restore_state's documented first
              instruction (ev6_vms_pal.mar:6228, hw_ldq/p r1,PT__IMPURE(p_temp))
    => 0x13480 = sys__exit_console (bsr restore_state), 0x13280 =
       sys__enter_console (bsr save_state).  Symmetric, both verified.

--------------------------------------------------------------------------------
## 3. Why every prior observation now fits

- "Halt code 0 at PC=0x20000000, no ITBPROBE, no kFaultHalt" (3.13/3.14):
  the console prints its slot bookkeeping after START "returns"; nothing
  ever ran at 0x20000000.
- Option A "eliminated the 0xA crash" (3.12): with PALSWAP the flush leaf
  executes cleanly on the right bank and returns -- no crash, but also no
  restore.  The 0xA-gone result was real; the "exit_console runs clean"
  interpretation was not.
- The old Option A "stale R23 reset" (2.4): same leaf, but hw_ret_stall(p23)
  on garbage R23 -> reset.  Setting p23 turned the reset into a clean
  return-to-console.  Both behaviors were the LEAF's, never exit_console's.
- AXPBox needs no scan: it jumps to the hardcoded cfw_start (its 0x13781),
  which br's to the real exit_console.  EmulatR's scan-derived 0xa6cc was
  never that address (wrong region entirely; DS20's real one is 0x13480).

--------------------------------------------------------------------------------
## 4. The defect (code)

palBoxLib/grains/PalEntries.cpp, Option A block (~line 674-704):
  - scans [palBase, palBase+0x20000) for the FIRST insn pair matching
    (op 0x1D, scbd 0x03) then (op 0x1D, scbd 0xA3), mask 0xFC00FF00;
  - assumes `s_exitConsolePc = p - 4` ("bsr is one instruction before").
  Both assumptions are wrong: the first pair belongs to a flush leaf, and
  even at the right pair the bsr is ~8 slots earlier behind NOP padding.
  There was no disasm-verification step (the CSERVE-ROUTE scan got one after
  its own scan bug, 3.10; this scan never did).

--------------------------------------------------------------------------------
## 5. Fix options

FIX A (RECOMMENDED -- scan-free, uniform, faithful): route CSERVE 0x42
  through the guest sys__cserve dispatcher, exactly like 0x65 and the other
  routed funcs (CSERVE-ROUTE block, PalEntries.cpp ~1190): p23=g.pc+4 both
  banks, r16 intact (=0x42), divert to s_sysCservePc|1 with DIVERT_PALSWAP.
  The dispatcher's own `cmpeq r16,#0x42 / bne` chain reaches cfw_start ->
  `br sys__exit_console` -> 0x13480 -> bsr pal__restore_state.  The
  dispatcher is already located AND literal-verified (0x12d84, 3.10).  This
  retires the fragile exit_console scan entirely and completes the 3.4
  "cleaner generalization" (one mechanism for every pure-PAL CSERVE func).
  Edit shape: in case 0x42 kStartGuest, replace the pair-scan divert with
  the sys__cserve-route divert (or simply fall through to the shared
  routing path used by the default: case when EMULATR_CSERVE_ROUTE is on).

FIX B (if a direct divert is kept): harden the signature -- for each
  ITB_IA/DTB_IA pair, walk BACK over nops/markers (bounded, ~16 slots) and
  require a `bsr r7` (op 0x34, ra=7) with an in-PAL target; entry = that
  bsr's PA.  CAUTION: this matches BOTH enter_console (0x13280) and
  exit_console (0x13480); discriminating them needs the callee check
  (first insn of the bsr target: restore_state = hw_ld r1,0x88(r21) vs
  save_state = hw_ld r4,0x88(r21) + stores) -- i.e., more fragility.  Fix A
  avoids all of it.

Expected post-fix sequence (verification): b dqa0 -> CSERVE 0x42 ->
sys__cserve -> cfw_start -> exit_console 0x13480 -> restore_state 0xe3a0
loads CNS@0x4200 (EXC_ADDR=0x20000000, PTBR=0x3FF04000, VPTB=0x2_0000_0000,
p_misc virtual) -> hw_rei -> ITBPROBE MISS at 0x20000000 -> walk fills pfn
0x2de -> APB executes (next expected event: APB's first data load, VA
0x10000050 = HWRPB+0x50, per JRN-016 2.2).

--------------------------------------------------------------------------------
## 6. Key addresses (DS20 ds20_v7_3 runtime; adds to JRN-016 Sec 9)

  TB-flush leaf (wrong divert)   0xa6c0  (entry used: 0xa6cc = nop)
  sibling leaf (IS variants)     0xa700
  sys__enter_console             0x13280  (bsr r7 -> pal__save_state)
  pal__save_state                0xdf80   (hw_ld r4,0x88(r21); stores)
  sys__exit_console (REAL)       0x13480  (bsr r7 -> pal__restore_state)
  pal__restore_state             0xe3a0   (hw_ld r1,0x88(r21))
  sys__reset flush pair          0x135a0  (in sys__reset 0x13540)
  sys__cserve dispatcher         0x12d84  (verified, 3.10)
  impure/CNS base (runtime)      0x4200   (PT__IMPURE @PA 0x7088; NOT 0x5000)
  CNS boot ctx @halt             PTBR 0x3FF04000, VPTB 0x2_0000_0000,
                                 P_MISC 0x81F00, EXC_ADDR 0x20000000
  HWRPB slot HWPCB @0x2180       KSP 0x200E0000, PTBR(+0x20) 0x1FF82

Artifacts (Mac, out/build/relwithdebinfo): run_apb_probe_20260724.log,
console_transcript{,2,3}_20260724.txt, console_examine_20260724.txt,
pal_dump_8000.txt, logs/faults.log.

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first for the
fix (Sec 5); V5 only write target.
