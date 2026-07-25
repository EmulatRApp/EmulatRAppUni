<!--
EmulatR V5 -- Session Journal JRN-VMB-017
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-017 -- The 0x20000000 wall: Option A diverts to the WRONG PAL routine.
#               0xa6cc is a TB-flush stub; true sys__exit_console = 0x13480.

    Doc id   : JRN-VMB-017
    Date     : 2026-07-24
    Status   : ROOT CAUSE by STATIC ANALYSIS (host-decompressor oracle vs the
               live DS20 image + apisrm source).  NO CODE CHANGED.  Fix shape
               proposed below (discuss-first).  Runtime confirmation owed via
               the already-landed EMULATR_PCTRACE probe or a disasm dump.
    Relates  : JRN-VMB-016 (end-to-end chain; this closes its 3.13-3.16 open
               question), JRN-VMB-004 (subsumed).  P-1 faithful IN FORCE.
    Authority: DEC apisrm SRM/PALcode SOURCE + AARM/HRM rules (per VMB-016
               Sec 10.5 -- no silicon trace exists or will).  EmulatR is the
               PRIMARY Oracle; AXPBox secondary/corroborative only.

--------------------------------------------------------------------------------
## 0. Executive summary (the answer)

The halt-code-0 wall at PC=0x20000000 is NEITHER hypothesis (A) "restore_state
bails" NOR (B) "PTBR misdirection".  It is simpler and fully mechanical:

**Option A's signature scan diverts CSERVE START to PA 0xa6cc, which is NOT
sys__exit_console.  It is the tail of a TB-flush stub (MTPR_TBIA/IMB family)
in the base VMS PAL: NOP; ITB_IA; DTB_IA; IC_FLUSH; hw_ret_stall(p23).**
Because CSERVE-START-A2 first sets p23 = g.pc+4 (the console return PC), the
stub's terminating `hw_ret_stall(p23)` sends control STRAIGHT BACK to the
console.  pal__restore_state NEVER RUNS; the OS context deposited in the CNS
frame (restart PC 0x20000000, boot PTBR 0x1ff82, VPTB, KSP, IER_CM) is never
installed; VA 0x20000000 is never fetched.

The TRUE sys__exit_console in the DS20 v7_3 image is **PA 0x13480**
(`d0ffebc7` = bsr p7, disp -0x1439 -> 0xe3a0 = pal__restore_state).  The scan
missed it because the BUILT image pads NOPs and a buffer `bis r0,r0,r0`
between the bsr and the ITB_IA/DTB_IA pair, so the scan's `pair - 4 == bsr`
assumption never holds; the scan then took the FIRST pair in ascending order,
which is the base-PAL flush stub at 0xa6d0.

Fix: replace the pair-scan with a two-stage locator anchored on
pal__restore_state itself (Sec 5).  One code block in PalEntries.cpp; nothing
else in the faithful stack (2D_NOOP / DELAYWARP / CSERVE_ROUTE /
DIVERT_PALSWAP / p23 invariant) changes.

--------------------------------------------------------------------------------
## 1. Method (why this was decidable without a PC run)

- Built the DS20 decompressed image with the trusted native oracle:
  `tools/host_decompressor/oracle_lin firmware/ds20_v7_3.exe` ->
  WimC@0x2400, target=0x8000, output 0x2f9a00 bytes; built-in regression
  signature PASSED (hw_ret(R2) x3, hw_ret(R6) x0).  File offset 0 == PA 0x8000.
- Scanned that image with the EXACT mask/logic PalEntries.cpp uses
  (mask 0xFC00FF00, ITB_IA scbd 0x03 then DTB_IA scbd 0xA3 adjacent), plus
  independent locators for pal__restore_state and its callers.
- Read the deposit/restore contract in the apisrm SOURCE (boot.c,
  kernel_support.c, ps_driver.c, ev6_ipr_driver.c, ev6_vms_pal.mar,
  ev6_vms_pc264_pal.mar).

--------------------------------------------------------------------------------
## 2. The SRM handoff contract (source-authoritative; settles the A/B framing)

The console->OS transfer is a DEPOSIT + RESTORE, all in the firmware:

1. DEPOSIT (boot.c ~1467-1470, plain guest stores -- EmulatR runs these
   faithfully):
     write_ipr(APR$K_KSP,  slot->HWPCB.VMS_HWPCB.KSP);
     write_ipr(APR$K_PTBR, slot->HWPCB.VMS_HWPCB.PTBR);   ; = 0x1ff82
     write_ipr(APR$K_VPTB, hwrpb->VPTBR);                  ; = 0x200000000
     write_pc (slot->HALT_PC);                             ; = 0x20000000
   write_pc -> ps_driver.c ps_write -> `paltempcontext->cns$exc_addr = pc`.
   write_ipr -> ev6_ipr_driver.c xx_write -> `impurePtr->cns$ptbr/vptb/ksp`.
   Both compute the impure pointer from the SAME compile-time
   PAL$IMPURE_BASE the PAL uses.  The deposits are ORDINARY STORES into the
   CNS frame -- no CSERVE, no IPR traffic.  (This is why the console's later
   halt banner prints "PC = 20000000": kernel.c:1562 reads cns$exc_addr,
   i.e. the STALE, never-consumed deposit.  halt code 0 is likewise the
   stale reset-era cns$hlt.  The banner was never evidence the CPU reached
   0x20000000.)

2. RESTORE (ev6_vms_pc264_pal.mar:4245 sys__exit_console -> ev6_vms_pal.mar:
   6228 pal__restore_state): restore_state has **NO bail path and NO validity
   check**.  It unconditionally reinstates the CNS frame: PTBR, VPTB(+VA_CTL
   and I_CTL VPTB fields), KSP, SCBB, PCBB, M_CTL, IER_CM, SIRR, ASN, PAL_BASE,
   process context, all GPRs (incl. R23), p_misc, and **p23 <- cns$exc_addr**.
   Then the exit_console tail installs a DTB entry covering PAL base (all-RWE),
   clears p_misc<63> (1-1 off), unlocks, flushes, `hw_ret_stall(p23)`.
   => Had restore_state run, p23 would have been 0x20000000 and the fetch
   attempted.  Hypothesis (A) as stated ("no valid restart record -> bail")
   cannot happen; hypothesis (B) cannot happen either (PTBR restore is
   unconditional).  The only way to get the observed behavior is for
   restore_state NOT TO RUN -- which is exactly what the wrong divert does.

--------------------------------------------------------------------------------
## 3. Evidence (all from the oracle-decompressed live DS20 image)

Loose-mask adjacent ITB_IA/DTB_IA pairs (the scan's signature), full image:

    PA 0x0a6d0   prev = 47ff041f (NOP)   <- FIRST match; scan target-4 = 0xa6cc
    PA 0x132c0   prev = NOP              <- sys__enter_console's pair
    PA 0x134a0   prev = NOP              <- sys__exit_console's pair (real one)
    PA 0x135a0   prev = NOP              <- sys__reset's pair
    PA 0x1c030   prev = NOP                 (second PAL copy region)
    PA 0x1da90 / 0x20060 / 0x20220 = NOP    (second PAL copy mirrors)
    (+ 0x1bac18 / 0x1bb1b8 / 0x26e18c outside the PAL, data-preceded)

  ZERO pairs anywhere are preceded by a BSR.  The scan's `p - 4` can never
  land on `bsr p7, pal__restore_state`.  Also note: even skipping the 0xa6d0
  false positive, ascending order hits ENTER_console's pair (0x132c0) before
  exit_console's (0x134a0) -- a band-aid re-scan would divert into
  enter_console.  The pair signature is UNFIXABLY ambiguous.

The wrong target, PA 0xa6c0..0xa6f4 (what EmulatR currently executes):

    0xa6c0  44000400   bis r0,r0,r0
    0xa6c4  47ff041f   nop
    0xa6c8  47ff041f   nop
    0xa6cc  47ff041f   nop              <- CURRENT DIVERT TARGET (|1)
    0xa6d0  77ff0310   hw_mtpr r31, ITB_IA
    0xa6d4  77ffa380   hw_mtpr r31, DTB_IA
    0xa6d8  47ff041f   nop
    0xa6dc  47ff041f   nop
    0xa6e0  77ff1310   hw_mtpr r31, IC_FLUSH (scbd 0x13)
    0xa6e4  47ff041f   nop x3
    0xa6f0  f7e00000   bne r31, .        (pvc prediction-stack pad)
    0xa6f4  7bf7a000   hw_ret_stall(r23) <- RETURNS VIA p23 = console retPc

  This is the MTPR_TBIA / IMB-family flush leaf of the BASE VMS PAL (matches
  the ev6_vms_pal.mar edit history: "explicit icache flushes as ITB_IA ...
  Affects MTPR_TBIA, MTPR_TBIAP, IMB").  Diverting here = flush TBs + icache,
  then hw_ret_stall(p23).  A2 sets p23 = g.pc+4 (console).  QED.

The true sys__exit_console, PA 0x13480:

    0x13480  d0ffebc7   bsr r7(p7), disp=-5177 -> 0x0e3a0   <- ENTRY
    0x13484  nop x3
    0x13490  44000400   bis r0,r0,r0 (buffer)
    0x13494  nop x3
    0x134a0  77ff0310   hw_mtpr r31, ITB_IA
    0x134a4  77ffa380   hw_mtpr r31, DTB_IA

pal__restore_state located by its FIRST INSTRUCTION `hw_ldq/p r1,
PT__IMPURE(p_temp)` = enc 6c351088 (op 0x1B, ra=1, rb=21, disp=0x88):

    0x0e3a0  6c351088   <- restore_state, ACTIVE (first) PAL copy
    0x1d324  6c351088   <- second PAL copy (image carries two personalities)

`bsr p7` callers of restore_state (the exit_console entries):

    0x13480 -> 0x0e3a0   <- ACTIVE PAL copy: THE divert target (|1 = 0x13481)
    0x20200 -> 0x1d324   <- second copy's exit_console

Consistency locks: enter_console = 0x13280 (bsr p7 -> 0xdf80 = save_state#1);
its clear-vptb code = 0x1333c (matches the LIVE observation in JRN-VMB-016
2.1); source order enter(4106) < exit(4245) < reset(4325) == image order
0x13280 < 0x13480 < 0x13540 (sys__reset, known).  sys__cserve = 0x12d84
(disasm-confirmed in VMB-016 3.10) sits just below.  Everything coheres.

--------------------------------------------------------------------------------
## 4. Every prior observation, explained by the one defect

- "exit_console runs clean and returns to the console" (VMB-016 3.5): it was
  never exit_console; the stub returns via p23 by construction.
- p23 = 0x1ae39c (console) at the end; final PC 0x1adab0 (console): the A2
  p23 was never overwritten because restore_state never ran.
- ZERO ITBPROBE at 0x20000000; boot0 never fetched: hw_ret_stall went to the
  console, not to 0x20000000.
- Console banner "halt code = 0 / PC = 20000000": stale CNS frame read-back
  (Sec 2) -- cns$exc_addr still holds the un-consumed deposit; cns$hlt still 0.
  There was NO halt at 0x20000000, clean or otherwise.
- Only kFaultDtbMissDouble in the whole run, no MCHK/ACV (3.16): nothing
  abnormal ever executed; the "wall" is a quiet wrong turn, not a fault.
- The DIVERT-PALSWAP fix killing the 0xA crash remains real and necessary:
  the stub (and every guest-PAL divert) still needed the correct bank.  The
  shadow-swap fix is unaffected by this finding.
- AXPBox never had this problem because it hardcodes per-image addresses
  (vmspal_call_cserve: case 0x42 -> set_pc(0x13781) = the ES40 cfw_start).
  EmulatR's generic scan is the right idea with the wrong anchor.

--------------------------------------------------------------------------------
## 5. Fix shape (PalEntries.cpp, Option A scan block, ~lines 675-697)

Replace the ITB_IA/DTB_IA pair scan with a two-stage locator anchored on
pal__restore_state (unambiguous, padding-immune, personality-copy-aware):

  STAGE 1 -- find pal__restore_state: scan [palBase, palBase+0x20000) for
    (w >> 26) == 0x1B && ((w >> 21) & 31) == 1     ; hw_ld, ra=r1
                      && ((w >> 16) & 31) == 21    ; rb=p_temp(r21)
                      && (w & 0xFFF) == 0x88       ; disp = PT__IMPURE
    Take the FIRST (lowest) match = the active PAL copy.  (DS20: 0xe3a0;
    the 0x1d324 match is the second personality copy -- ignore.)
  STAGE 2 -- find its caller: scan the same window for
    (w >> 26) == 0x34 && ((w >> 21) & 31) == 7     ; bsr p7
    whose branch target (pc+4 + 4*sext21(disp)) == the STAGE-1 address.
    FIRST match = sys__exit_console entry.  (DS20: 0x13480.)
  Divert to (exitConsole | 1).  Keep the A2 p23 both-banks set and the
  DIVERT_PALSWAP path EXACTLY as they are (restore_state itself overwrites
  p23 from cns$exc_addr on the START path -- that is the faithful flow; the
  A2 p23 still matters for flush-leaf-style handlers that return via p23).
  Emit both located addresses in the CSERVE-START-A line and (cheap, high
  value) a CSERVE-START-A-DISASM of the first 4 words at the target --
  assert word0 decodes as `bsr p7` (0xD0E00000 mask 0xFFE00000); if not,
  log NOT-FOUND and no-op, exactly like today's scan-fail path.

Notes:
- Do NOT anchor on `bsr` adjacency to the flush pair (built image pads) and
  do NOT skip-count pairs (enter_console's pair precedes exit_console's).
- ES40/DS10 inherit the same locator untouched -- restore_state's first
  instruction is personality-invariant in the VMS PAL (GET/PT__IMPURE=0x88
  from ev6_pal_temps.mar), and each image's active copy is the lowest match.

--------------------------------------------------------------------------------
## 6. Predicted post-fix behavior + verification plan

With the divert at 0x13481 (and the stack: 2D_NOOP + DELAYWARP + CSERVE_ROUTE
+ DIVERT_PALSWAP + kStartGuest):

1. restore_state runs (PCTRACE will show 0x13480 -> 0xe3a0 -> long RESTORE_REG
   run), installs the deposited CNS frame: PTBR=0x1ff82, VPTB=0x200000000,
   KSP, IER_CM, p_misc<63> cleared by the tail, p23 <- 0x20000000.
2. hw_ret_stall(0x20000000): FIRST EVER ITBPROBE at VA 0x20000000 -> ITB miss
   -> guest ITB_MISS (p_misc<63>=0, virtual) -> VPTE load -> DTBM_DOUBLE_3 ->
   physical walk via PTBR 0x1ff82 -> l3 PTE 0x2de00001101 (proven correct,
   JRN-VMB-001) -> ITB fills pfn 0x2de -> boot0 EXECUTES at 0x20000000.
3. boot0's first data load (VA 0x10000050, the HWRPB window) now walks the
   same table -> pfn 0x1 -> proceeds (the VMB-016 2.2 sequence, this time in
   a coherent context).
RUN (PC): tools/build_emulatr.sh relwithdebinfo ; confirm the new locator in
the exe (grep -a -c 'CSERVE-START-A' out/build/relwithdebinfo/Emulatr.exe);
EMULATR_PCTRACE=1 tools/run_ds20_bplus.sh ; b dqa0.
READ: CSERVE-START-A must report exit_console=0x13480 (not 0xa6cc); PCTRACE
BAIL row should be GONE (no console re-entry below 0x200000 right after the
divert); expect either VMB/boot0 progress or the NEXT genuine wall (candidates:
IPL/IER_CM disposition, FEN, SCB dispatch into VMB -- all downstream of a
faithful restore).

Residual risks (honest): (a) the deposited cns$ptbr/vptb values were verified
only via the banner arithmetic (0x1ff82<<13 == 0x3ff04000) and source reading,
not a live CNS dump on the current build -- the PCTRACE ARM line's ptbr field
now becomes corroborating data; (b) restore_state's FPCR/floating block runs
mt_fpcr/itoft on the fBox POC -- watch for FP-side surprises; (c) the DTB
entry the tail installs (PAL-base all-RWE) must round-trip EmulatR's DTB
model (it uses hw_mtpr DTB_TAG0/1 + DTB_PTE0/1 pairs).

--------------------------------------------------------------------------------
## 7. Key addresses (DS20 ds20_v7_3, oracle-decompressed image, PA)

  WRONG divert (current)       0xa6cc  (NOP; stub tail hw_ret_stall(r23) @0xa6f4)
  flush-stub pair              0xa6d0/0xa6d4 (+ IC_FLUSH 0xa6e0)
  sys__cserve (confirmed live) 0x12d84
  sys__enter_console           0x13280  (bsr p7 -> save_state 0xdf80; pair 0x132c0;
                                         clear-vptb code 0x1333c)
  sys__exit_console (TRUE)     0x13480  (bsr p7 -> restore_state 0xe3a0; pair 0x134a0)
  sys__reset                   0x13540  (pair 0x135a0)
  pal__save_state #1           0x0df80
  pal__restore_state #1        0x0e3a0  (enc 6c351088 = hw_ldq/p r1,0x88(r21))
  second PAL copy              restore_state 0x1d324; exit_console 0x20200
  deposited restart PC         0x20000000 (cns$exc_addr)   boot PTBR pfn 0x1ff82
  boot page table              0x3ff04000                  boot0 leaf pfn 0x2de

--------------------------------------------------------------------------------
## 8. Provenance

Derived 2026-07-24 in the Cowork sandbox: oracle_lin (native, trusted -- see
tools/host_decompressor/README) run against firmware/ds20_v7_3.exe from the
live tree; scans by script over the oracle output; PAL/console source read
from Processor Support (apisrm ref).  The oracle's built-in byte signature
passed, arguing the mount reads were clean; per the standing sandbox caveat,
the disasm-dump assert in Sec 5 re-verifies the located target NATIVELY at
runtime before any divert fires.

================================================================================
## PART 2 -- LIVE VERIFICATION (fix landed + WALL BROKEN) and the NEXT root
##           cause: HW_LD/HW_ST must ignore EA low bits (invalid-PTBR halt)
================================================================================
    Date   : 2026-07-24 (same day, live DS20 PC run + sandbox static analysis)
    Status : Sec 5 locator LANDED and VERIFIED LIVE.  The 0x20000000 wall is
             BROKEN -- boot0/VMB executes ~42KB deep.  The NEXT wall (guest
             halt code 4 "invalid PTBR" at PC=0x2000a374) is ROOT-CAUSED to a
             single EmulatR defect and FIXED: execHwLd/execHwSt did not
             truncate the effective address (mBoxLib/grains/LoadStore.cpp).

### P2.1 Locator fix verified live (run_ds20_showdev_20260723_204013)

  CSERVE-START-A: palBase=0x8000 restore_state=0xe3a0 exit_console=0x13480
                  enc=0xd0ffebc7                      <- exactly as predicted
  PCTRACE: pal__restore_state RESTORE_REG run visible at 0xe55c..0xe5d8.
  ITBPROBE MISS then HIT va=0x20000000 pte=0x2de00000101 pfn=0x2de
                  pa=0x5bc000                          <- FIRST EVER boot0 fetch
  Boot0/VMB then EXECUTES ~0xa374 bytes deep (thousands of instructions,
  HWRPB reads working).  The Sec 0-5 analysis is CONFIRMED end to end.

### P2.2 The next wall, fully root-caused (all numbers reconcile)

Symptom: [CON COM1] "halted CPU 0 / halt code = 4 / invalid PTBR /
PC = 2000a374", preceded by "reportNxm: NXM pa=0x20003fe401" and (faults.log)
kFaultFor (fault=8) at pc=0x8321 va=0x200100400.

The halting VMB instruction (extracted from the boot media -- see P2.4):
  0x2000a374  2ea00000  ldq_u r21, 0(r0)   ; VMB reading the page-table window
                                           ; (VA 0x40100000 region) -- LEGAL.
Chain: that access single-misses -> ld_vpte @0x8320 (DTBM_SINGLE, loads via
VA_FORM) -> double miss -> DTBM_DOUBLE_3 self-map walk (guest PAL, walk body
at PA 0xc088):

  r25 = mem_p[p_temp+8]          ; PT_PTBR = 0x3ff04000 (pfn<<13 -- boot.c
                                 ; write_ipr(APR$K_PTBR) shifts by 13,
                                 ; ev6_ipr_driver.c:461)
  r26 = (r4<<31)>>51             ; field A = r4[32:20]  (raw BYTE offset)
  r25 = mem_p[r25 + r26]         ; L1-as-L2 read (self-map)
  r26 = (r4<<41)>>51             ; field B = r4[22:10]  (raw BYTE offset)
  r25 = mem_p[((r25>>32)<<13) + r26]  ; leaf PTE read
  DTB_TAG0/1 = r4 ; DTB_PTE0/1 = r25  ; install vpte mapping; hw_rei

THE POINT: fields A and B DELIBERATELY carry junk in bits <2:0> -- DEC wrote
the shift constants knowing EV6 HW_LD (quadword) IGNORES va<2:0>.  For the
fatal vpte r4=0x200100400 (VMB's VA 0x40100000 access):
  field A = 1        -> EmulatR read the L1 PTE at 0x3ff04001 (MISALIGNED;
                        real HW truncates to 0x3ff04000 = L1[0])
  misaligned read    -> G = 0x010001ff83000011 (L1[0]=0x0001ff8300001101
                        byte-shifted, next byte from L1[1])
  (G>>32)<<13        -> 0x10001ff<<13 = 0x20003fe000
  field B = 0x401    -> leaf PA = 0x20003fe000+0x401 = 0x20003fe401
                        == THE OBSERVED NXM, digit for digit.
  NXM read           -> all-ones "PTE" (async NXM returns ~0): V=1, FOR/FOW/
                        FOE=1, GH=3 -> installed as the vpte mapping
  retried ld_vpte    -> kFaultFor (faults.log fault=8 va=0x200100400)
  FOR during ld_vpte -> trap__ldvpte_dfault (ev6_vms_pal.mar:5701) ->
                        PT__HALT_CODE = HALT__PTBR_INVAL (4) -> enter_console
                        -> "invalid PTBR / PC = 2000a374".
The earlier walks (vpte 0x200080000 fetch, 0x200040000 HWRPB data) SUCCEEDED
only because their junk bits happened to be zero.  The guest page tables are
CORRECT throughout; no SMP involvement; CPU0-local.

### P2.3 THE FIX (landed 2026-07-24): truncate HW_LD/HW_ST effective addresses

mBoxLib/grains/LoadStore.cpp execHwLd (~425) + execHwSt (~443):
  r.memAddr = (c.opB + sext(disp12)) & ~uint64_t(memSize-1)
(quad: &~7, long: &~3), replacing the unmasked EA.  Comment block rewritten:
"no alignment trap" does NOT mean "use the unaligned EA" -- EV6 truncates.
With the mask, the fatal case reads L1[0] at 0x3ff04000 and the leaf at
0x3ff06000+(0x401&~7)=0x3ff06400 = the self-map entry -- exactly the intended
algebra.  GENERAL correctness fix: every guest-PAL walk whose VA bit patterns
put nonzero junk in the field low bits has been exposed to this; it plausibly
explains part of the historical tiny-VA DtbMissDouble cascades.

### P2.4 Boot-media extraction route (for future VMB disasm)

The attached DS20 boot media is vStorage/Alpha/dka0.vdisk (per the run-dir
ds20_v7_3_platform.json "media" key -- NOT disks/dqa0.img, which reads as
phantom zeros over the sandbox mount and is not the attached unit).  Boot
block (apisrm boot.c struct bb): u32 size@480 = 1226 blocks, u32 lbn@488 =
283169; VMB = 1226*512 bytes at lbn*512.  First words verified against the
JRN-VMB-016 2.2 boot0 disasm (d3800000 201f0001 203f001c 48010720).
tools/extract_vmb.sh automates this natively (it defaults to disks/dqa0.img
-- point it at the vdisk or pass the path as arg 1).

### P2.5 Predicted outcome + next run (superseded by PART 3 -- kept for the record)

With DIVERT locator (Sec 5) + HW_LD/HW_ST truncation (P2.3): VMB's page-table
window reads proceed; expect the bootstrap to continue past 0x2000a374 toward
SYSBOOT (or the next genuine wall -- candidates unchanged from VMB-016:
IPL/IER disposition, FP usage in VMB, device I/O for the system image).  RUN:
tools/build_emulatr.sh relwithdebinfo ; EMULATR_PCTRACE=1
tools/run_ds20_bplus.sh ; b dqa0.  Files touched this session:
palBoxLib/grains/PalEntries.cpp (Sec 5 locator), mBoxLib/grains/LoadStore.cpp
(P2.3 truncation), tools/run_ds20_showdev.sh (GNU-first stat -- Git Bash
"File: unbound variable" fix), tools/extract_vmb.sh (new), this journal.

================================================================================
## PART 3 -- FIVE WALLS FELL IN ONE DAY: from halt-at-arrival to OpenVMS APB
##           speaking on the console.  New frontier: %APB-F-NOIOVEC (PCI gate).
================================================================================
    Date   : 2026-07-24 (live DS20 PC runs, all verified same-day)
    Status : All five fixes LANDED + VERIFIED.  The boot now runs the OpenVMS
             Alpha Primary Bootstrap (APB) through its init sequence to the
             boot-adapter phase, where it fails FATALLY but CLEANLY:
             "%APB-F-NOIOVEC, Failed to create IOVEC" -- printed in readable
             text through the fully faithful callback path, then a clean
             return to the console.  FIRST OS-GENERATED CONSOLE OUTPUT in
             EmulatR's history.

### P3.1 The day's fix ledger (each verified live before the next was attacked)

  1. exit_console divert target (Sec 5)     -- 0xa6cc stub -> 0x13480 real.
     VERIFIED: restore_state runs, first-ever ITB fill at VA 0x20000000.
  2. HW_LD/HW_ST EA truncation (P2.3)       -- EV6 ignores va<2:0>/<1:0>.
     VERIFIED: NXM 0x20003fe401 GONE, kFaultFor GONE, VMB runs deep; the
     invalid-PTBR halt (code 4) cleared.
  3. WH64/FETCH_M/WH64EN wired (TSV rows, leafBase=FETCH -> mBox::execFetch).
     VERIFIED: OPCDEC at 0x20077bd0 GONE; the kernel-stack halt (code 2,
     PC=0) cleared; VMB completed and transferred to APB.
  4. CSERVE 0x43 CALLBACK routed to guest PAL (explicit no-op case removed).
     VERIFIED: the ~86-cyc infinite retry loop (585MB log) became 3 clean
     round trips (CALLBACK -> cfw_callback -> console cbip service ->
     exit_console START), plus IIC_WRITE x18 (OCP updates) flowing.
  5. p23 linkage written to the PAL'S VIEW of R23 only (both divert sites).
     ROOT: the both-banks write clobbered NATIVE R23 = the callback ABI's
     PUTS length (cb_puts: string bytes in saved R22, length in saved R23,
     "one to eight characters" per call) -> fwrite with a return-PC-sized
     length -> binary garbage on COM1.  VERIFIED: output now readable.
     (Ruled out first: the SDE-edge bank swap -- already correctly
     implemented at PalEntries.cpp HW_I_CTL, 2026-06-03.)

### P3.2 The new frontier: %APB-F-NOIOVEC, Failed to create IOVEC

WHAT IOVEC IS: APB (the VMS primary bootstrap read off the boot media) links
console, bootdriver, and secondary bootstrap through a small vector of I/O
entry points -- the IOVEC.  Building it is APB's boot-adapter phase: parse
the booted-device topology (dqa0.0.0.105.0 -- unit/hose/slot encoding),
allocate the boot adapter block (BTADP), select + init the matching
bootdriver, and vector its entry points.  The APB message table in the
extracted image (VA 0x2006bxxx-0x2006c1xx, counted strings) places NOIOVEC
exactly there: ... ALOBTADP ("ERROR allocating BTADP") -> NOIOVEC -> 
NOSUCHDRVR ("Failed to select boot driver") -> ... -> SYSBOOT-I-WLCM.
With -flags 0 the informational messages (APBVER, BOOTDEV, BOOTDRIV...) are
suppressed, which is why the fatal is the FIRST line seen.

EVIDENCE BANKED for the next session:
  - NOIOVEC message record VA 0x2006c080; referenced from APB's pointer
    table at VA 0x200623d8 (offset 0x623d8 in the extracted image) --
    disassemble the referencing check to name the exact failing call.
  - APB exits fatal -> console re-enters cleanly (CSERVE START from console
    pc 0x1ae398 observed post-message).  No EmulatR-side faults: the run's
    fault log is routine demand paging only (DtbMissDouble at the miss
    handlers).  This wall is a GUEST-VISIBLE resource/probe failure, not an
    execution defect.
  - VMB/APB image extraction route: vStorage/Alpha/dka0.vdisk (the manifest
    "media" -- NOT disks/dqa0.img), boot block u32 size@480=1226 blocks,
    u32 lbn@488=283169.  READS CLEANLY over the sandbox mount.

HYPOTHESIS (aligned with the standing deferred-work map): this is the
long-anticipated PCI-ENUMERATION GATE (memory.md Sec 5 / tickets #37-42:
"SYSBOOT> scaffold gated on PCI (#41) + multi-block ATAPI (#32)").  The SRM
console reads the disk fine with its own driver, but APB's bootdriver builds
its adapter picture from the PCI CONFIG topology encoded in the booted-device
string -- precisely what EmulatR does not yet model (no real bus walk, no
per-device config headers / BAR assignment).  SCSI NOTE: switching to or
finishing the SCSI stack would NOT dodge this -- dqa0 is IDE, and a SCSI
boot (dka0) would hit the SAME adapter-probe wall plus need an HBA model;
the substrate is the same either way.

NEXT PROBES (in order):
  1. Instrument PCI config-space accesses during the APB window (PA-watch on
     the Pchip config region + the existing UNHANDLED-write channel): what
     does APB actually probe between its last callback and the fatal?
  2. Audit the callback ledger for GETENV traffic (BOOTED_DEV et al.) in the
     same window -- confirms which topology inputs APB consumed.
  3. Disassemble APB around the 0x200623d8 reference to name the failing
     call (allocation vs probe vs driver-select).
Then scope tickets #37-42: config-space decode through the Pchip, IPciDevice
seam with per-device config headers (Cy82C693 IDE function first), BAR
assign/rebind, and wire the existing IDE model behind it.

### P3.3 Standing state (resume here)

Wrapper stack (run_ds20_bplus.sh): 2D_NOOP + DELAYWARP + CSERVE_ROUTE +
DIVERT_PALSWAP + CSERVE_START_MODE=guest -- all five of today's fixes are
unconditional code (truncation, hints, locator, p23) or ride this stack
(0x43 routing).  DS10/ES40: tier-1 fixes (truncation, hints) apply
unconditionally -- a regression pass to >>> is OWED; the handoff stack
propagates but needs per-image locator canary checks + their own downstream
walls (JRN-VMB-016 Sec 5).  Log hygiene owed: the CSERVE entry ledger is
unthrottled (585MB log during the 0x43 spin); throttle like the stub
announcer.  Files touched today (PC tree, uncommitted): PalEntries.cpp,
LoadStore.cpp, GrainMasterV4.tsv + generated/*, FloatVariants.cpp +
handwritten.tsv (regen), run_ds20_showdev.sh, tools/extract_vmb.sh (new),
this journal, memory.md.

### P3.4 NEXT-SESSION RUNBOOK -- implement-complete the PCI fabric (explicit)

This journal is the FRONTIER record, not the fabric design.  Tomorrow's
session should proceed in this exact order:

READ ORDER (before any code):
  1. This journal, PART 3 (frontier + evidence + probes).
  2. `journals/PCI_Fabric_Strata_and_BuildOrder_20260609.md` -- the
     authoritative build order for the fabric layers.
  3. `journals/PCI_Fabric_Section7_Proposal_20260609.md` -- the design
     proposal (config-space decode, device seam, BAR model).
  4. `journals/Device_Enumeration_Scaffold_Spec_20260607.md` -- the
     enumeration scaffold spec (IPciDevice seam sibling of IBlockMedia).
  5. `journals/tasks_20260612_boot_pci_deploy.md` -- tickets #37-42
     (scope + acceptance per ticket).
  6. `journals/20260630_pci_adapter_microcode_consumption.md` -- how SRM
     firmware consumes PCI adapter data (what must LOOK right).
  Reference for the chipset side: Tsunami/Typhoon HRM chapters on PCI
  config cycles via Pchip (`Processor Support\REFERENCE_INDEX.md` lookup
  table -> 21272 HRM), plus memory.md 2.4 (PA map: Pchip0 CSR
  0x801_8000_0000, Pchip1 0x803_8000_0000).

PHASE 0 -- PROBES FIRST (P3.2; half a session, prevents over-building):
  capture APB's actual config-space probe addresses + GETENV traffic +
  disassemble the NOIOVEC-referencing check (ptr table 0x200623d8).  The
  fabric is then built against APB'S DEMANDS, not the full HRM surface.

PHASE 1 -- MINIMUM BOOTABLE FABRIC (per the strata doc, scoped to dqa0):
  (a) Pchip config-cycle decode: route the config-space PA window through
      a bus-walk layer; empty slots answer master-abort all-ones.
  (b) IPciDevice seam: config-header (vendor/device/class/BAR registers)
      per modeled device; FIRST device = Cy82C693 south bridge with its
      IDE function (the dqa0 path), matching the slot encoding in
      `dqa0.0.0.105.0`.
  (c) BAR assign/rebind: firmware BAR writes rebind the device's
      MMIO/IO ranges dynamically (no more hardcoded bases).
  ACCEPTANCE: SRM `show config`/`show dev` still correct; `b dqa0` gets
  PAST `%APB-F-NOIOVEC` (APB selects the IDE bootdriver); expect the next
  frontier at bootdriver device init or multi-block ATAPI (#32).

PHASE 2 (after dqa0 boots further): NIC config stub (silences the DS10
  0xFFFF0000 UNHANDLED writes, CLAUDE.md deferred item), SCSI HBA model
  onto the same seam (scsiCoreLib exists -- see D:\EmulatR\
  scsiCoreLib_Functionality.md), ES40/DS10 fabric bring-up + regression.

STANDING RULES for that session: discuss-before-code per edit; probes
env-gated; V5 only; EmulatR primary Oracle; read memory.md Sec 1.0 first.
