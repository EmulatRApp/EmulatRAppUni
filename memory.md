<!--
EmulatR -- Project Memory (V5, consolidated 2026-07-15)
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
ASCII(128) only.  Most recent / most active material first.
This file was consolidated from the 1683-line V4 log: durable architecture,
settled root causes, and ruled-out lists were KEPT; the day-by-day boot-bringup
narrative was compressed (full detail lives in the dated journals/, which came
across in the clone).  Read this first each session, then the TB brief, then
drill into journals only as needed.
-->

# EmulatR -- Project Memory

## -1. LATEST (2026-07-25): SCSI live gate PASSED; NOIOVEC is PROTOCOL-INDEPENDENT

- **JRN-SCSI-003 P1/P2 acceptance PASSED live:** DS20 `show config` shows
  "NCR 53C810" at slot 8; `show dev` shows pka0.7.0.8.0 + dka0..dka600 (7
  disks) -- the console's own pke driver enumerated the bus through the
  SCRIPTS engine + S1 DMA seam.  Boot READS off dka0 (APB, 1226 blocks) run
  clean through the HBA.  SCSI stack DONE for this frontier (P4 debts remain).
- **P3 RESULT (decisive): `b dka0` still dies %APB-F-NOIOVEC, and the
  resolver DIAG-PC footprint is BYTE-IDENTICAL to the IDE run** (752 unique
  PCs, PC-set diff EMPTY, same 4 CMOVEQ births, zero accept-region retires;
  JRN-SCSI-004 Sec 4).  GETENV delivered canonical "SCSI 0 8 0 0 0 0 0".
  REFUTES VMB-022's "APB predates IDE boot" as root cause: the 0xf3-tail
  gate (0x20096d14-0x20096e44) refuses ALL protocols identically -- the walk
  is a validate/probe pass whose execute mode never engages (VMB-021 f.4:
  token flag bits 10/11/12/14 x MODE R7=1 x sub-request r25=4).
- **A4 EXECUTED (JRN-SCSI-005): AXPBox ES40 BOOTS the SAME dka0.vdisk into
  OpenVMS V8.3** (same 627,712-byte APB; no NOIOVEC; disk read-only).
  Media + APB + SCSI stack ALL exonerated; **NOIOVEC = EMULATR ENVIRONMENT
  GAP** (something APB reads flips its resolver probe-only vs execute).
  A3p3 gate decoded from the halt snapshot: token bit 10 = probe-only
  entry; scanner pair -> BNE r0 exit -> CMOVEQ plants 0x158284 when result
  slot bits 27:3 stay 0.  Probe 4.1 (EmulatR-ES40 same-disk) EXECUTED ->
  BLOCKED by pre-existing #32 (ES40 console skips the partition/GCT/
  driver-init phase entirely; show dev = dva0 only; zero SCSI CSR traffic;
  manifest gained pka_53c810@slot3 + IDE dqa0 media emptied; ini restored
  to DS20).  NARROWING: the skipped phase = partition/GCT/driver init;
  VMB-019's GCT exoneration covered only the FAILING WINDOW, not an EARLY
  config-tree read caching the mode flag -- GCT content is BACK on the
  suspect list.  NEXT (JRN-SCSI-005 Sec 6): N1 EMULATR_DIAG_WREG=18 mode
  provenance on DS20; N2 EMULATR_PA_WATCH on GCT 0x3ff32000 whole-run;
  N3 #32 root cause.  Caller 0x2000e5d0 = VMS procedure-descriptor
  trampoline (static chase deep; prefer N1 dynamic).  AXPBox harness notes
  (working binary = axpbox/test/rom/axpbox.exe May-20; both local rebuilds
  BROKEN; telnet driver required; console-client disconnect KILLS it):
  JRN-SCSI-005 Sec 5-6.  User separately testing `b dqa1` (V8.2 CD).

## 0. Orientation (read first)

- **V5 is the active, writable development target** (`D:\EmulatR\EmulatRAppUniV5`,
  branch `v5-tb`). **V4 is FROZEN** (`D:\EmulatR\EmulatRAppUniV4\Emulatr`, tag
  `v4-frozen`) -- the correctness Oracle. Do NOT modify V4. All new work lands
  in V5. When the user says "the project," assume V5.
- **CONVENTION -- EmulatR is the PRIMARY Oracle.** AXPBox, SimH, and any other
  emulator are SECONDARY and supportive only (corroborate, or when EmulatR is
  not yet authoritative), never the primary authority over EmulatR. Any decision
  that would treat a non-EmulatR emulator as ground truth (its layout, format,
  naming, behavior) is DISCUSS-FIRST.
- **MILESTONE (2026-07-15):** SRM `>>>` reached on DS10, DS20, ES40 (default/ISP
  mode). That closed the V4 objective and gated the V5 Translation Buffer fork.
- **LIVE FRONTIER (2026-07-24): `%APB-F-NOIOVEC` -- the PCI-enumeration gate.**
  FIVE walls fell in one day (all root-caused, fixed, VERIFIED live; full record
  `journals/20260724_JRN-VMB-017_exit_console_divert_target_rootcause.md`):
  (1) the 0x20000000 halt-0 wall was a WRONG DIVERT TARGET -- the Option A scan
  hit a TB-flush stub at 0xa6cc, not sys__exit_console (=0x13480); two-stage
  locator anchored on pal__restore_state landed; (2) HW_LD/HW_ST must IGNORE
  EA low bits (EV6 truncates; the PAL walk fields carry deliberate junk) --
  mBoxLib/LoadStore.cpp fix cleared the invalid-PTBR halt; (3) WH64/FETCH_M/
  WH64EN wired as no-op hints (TSV leafBase=FETCH) -- cleared the OPCDEC ->
  kernel-stack halt in VMB's memory-clear loop; (4) CSERVE 0x43 CALLBACK
  no-op case REMOVED -> routes to guest cfw_callback; the OS<->console
  callback round trip (halt-33/cbip/START) runs faithfully; (5) p23 linkage
  now written to the PAL'S VIEW of R23 ONLY (native R23 = the callback ABI's
  PUTS length; the both-banks write was dumping binary garbage to COM1).
  RESULT: OpenVMS APB (primary bootstrap) RUNS and prints readable output --
  the first OS-generated console text in EmulatR's history -- then fails
  fatally at its boot-adapter phase: `%APB-F-NOIOVEC, Failed to create
  IOVEC`, clean return to console.  UPDATE (JRN-VMB-018/-019, later same
  day): the "KNOWN PCI gate" hypothesis is REFUTED for NOIOVEC -- PCI (zero
  config cycles in the failing window), boot_dev env var, the GCT walk
  (zero GCT reads), and the BOOT_DEV string are ALL EXONERATED (GETENV
  delivers the complete canonical 19-char "IDE 0 105 0 0 0 0 0" per apisrm
  filesys.c).  The failure is isolated to APB's INTERNAL pattern-matching
  VM: status 0x158284 is born at exactly ONE site (CMOVEQ @0x20096e58);
  the success stores (0x20097e30+) never execute.  Probe order + PCI #41
  plan: JRN-VMB-019 Sec 2/3.  DS10/ES40: fixes 2+3 apply unconditionally
  -- regression pass to `>>>` OWED.
- **SANDBOX MOUNT CAVEAT (load-bearing):** the Cowork Linux sandbox sees this
  repo over a FUSE mount that returns TRUNCATED reads and cannot unlink. The
  Read/Edit/Write file tools (host side) are GROUND TRUTH; bash `wc`/`grep` on
  the mount can serve stale/short phantoms. Run ALL git ops + integrity checks
  natively. A sandbox "modified"/truncated view is phantom until confirmed with
  native `git diff`. (This exact hazard produced a phantom "346 vs 450" line
  count on the TB brief this session.)
- Pre-2026-07-15 journals were authored in the V4 tree; some carry inconsistent
  `EmulatRAppUniV4`/`V5` path labels. Treat all `journals/...` references as
  RELATIVE to the current project root.

---

## 1. CURRENT FRONTIERS (two, parallel)

Two live frontiers run in parallel: **1.0 boot-correctness** (the DS20 OS handoff,
where the last several sessions have been) and **1.1-1.4 the TB acceleration tier**
(the strategic fork; brief is authoritative but not the recent-session focus).

### 1.0 Boot-correctness -- DS20 OpenVMS bootstrap (now INSIDE APB)

Authoritative journals (read in this order):
`journals/20260724_JRN-VMB-020_a1_a2_executed_apb_exonerated.md` --
CURRENT: probes A1 + A2 EXECUTED, both CLEAN.  A1: full-image snapshot
diff at the halt -- state tables, token stream, literals, message table
ALL byte-identical; only 499 bytes diverge, every one legitimate runtime
data.  A2: mechanical AARM replay-oracle over all 11,620 windowed retires
-- ZERO divergences (values, stores, EAs, branch directions, memory
consistency; all 313 xH shift-64 edge cases correct).  The search
exhausts LEGITIMATELY.  Frontier -> A3 (descriptor provenance) / A4
(AXPBox oracle diff).  ALSO: after NOIOVEC, APB HALTs @0x20003a38 and
EmulatR EXITS (HaltedClean) -- there is NO return to the SRM console
(earlier "clean return to console" was wrong); use the auto_halt snapshot
(KEPT: snapshots/auto_halt_1784955322_1842256525.axpsnap) for post-mortem
memory.  Scripted-console runbook + EMULATR_NO_PUTTY requirement
(Machine.cpp:312 hardcodes PuTTY autolaunch) in VMB-020 Sec 3.  Then
`journals/20260724_JRN-VMB-019_noiovec_string_exonerated_pci_plan.md` --
BOOT_DEV string EXONERATED (complete canonical 19-char form);
failure isolated to APB's internal pattern-VM (0x158284 born only at
CMOVEQ @0x20096e58); Track A probe order + Track B PCI #41 scope (B1 BAR
rebind + B2 IDSEL boundary first).  Then
`journals/20260724_JRN-VMB-018_apb_noiovec_investigation.md` (+ its
`_P2_apb_exe_static_analysis.md`: the decision module is a table-driven
path matcher; NOTE its Sec 2 GCT hypothesis and the P2/P4 truncated-string
finding are REFUTED by VMB-019), then
`journals/20260724_JRN-VMB-017_exit_console_divert_target_rootcause.md`
(the five 2026-07-24 fixes), then
`journals/20260722_JRN-VMB-016_0x20000000_wall_end_to_end_rootcause.md`
(the upstream chain + governing principle), `20260720_architecture_development_status.md`.

**2026-07-24 state (EOD):** `b dqa0` -> VMB completes -> APB runs, converses
with the console via faithful CSERVE 0x43 round trips (message chunks in
R22/R23 per the callback ABI), prints `%APB-F-NOIOVEC, Failed to create
IOVEC`, exits to console.  RULED OUT for NOIOVEC (JRN-VMB-018/-019): PCI
(zero config cycles in the failing window), boot_dev env var, the GCT walk
(zero GCT reads at failure time), string truncation (GETENV delivers the
full "IDE 0 105 0 0 0 0 0").  The failing search is APB-INTERNAL: the
request-code-0xf8 walk over a STATIC token stream inside the APB image;
both top-level invocations return the 0x158284 no-match sentinel.
A1 + A2 both EXECUTED CLEAN (VMB-020); A3 parts 1+2 DONE (VMB-021/-022):
the module is a BYTECODE VM; the walk matched "IDE","0","105" then
exited with the accept stores never reached.  DECISIVE (VMB-022): a
whole-boot DIAG-PC scan proves the resolver NEVER succeeds anywhere in
the run, and the APB image contains NO "IDE" AT ALL -- zero raw "IDE"
bytes, zero "IDE"-forming LDAH/LDA immediates; its COMPLETE protocol
set is {DVA_,RAID,SCSI,MSCP,FLOP -> type 0x11; MOP_,BOOT}.  LEADING
HYPOTHESIS: this APB (A13-03, TC-era driver names) PREDATES IDE boot;
%APB-F-NOIOVEC is its CORRECT answer to "IDE 0 105 0 0 0 0 0" --
EmulatR exonerated END-TO-END (console string canonical, database
intact, execution faithful, no-match semantically right).  JRN-019's
"IDE boot IS supported by this vintage" is REFUTED for this image.
NEXT (decisive, cheap): A4 = boot the SAME dka0.vdisk on AXPBox ES40
(also-NOIOVEC -> confirmed; boots -> resume VMB-021 Sec 3 gate map);
also enumerate the V8.2 ISO's APB keywords (if it has "IDE", that
system is the direct unblock).  If confirmed: options = newer media /
SCSI HBA model (REVERSES "SCSI not prerequisite") / MSCP / MOP.
**DIRECTION SET (user, 2026-07-24): PCI->SCSI virtual disk is NEXT.**
Design (discuss-first, awaiting approval): JRN-SCSI-001 -- NCR 53C810
(PKE; VID/DID 0x1000/0x0001; driver proven live in the DS20 console;
pke_driver.c + pke_script.mar = the contract; AXPBox Sym53C810 =
secondary reference; deviceLib/scsi/ target layer already exists; B1
BAR-rebind lands first).  User is separately testing `b dqa1` (V8.2
CD) on their own console.  Track B (PCI
#41) remains REQUIRED for SYSBOOT but is NO LONGER assumed to fix NOIOVEC.
Housekeeping owed: throttle the CSERVE entry ledger; DS10/ES40 regression
pass to `>>>`; promote DIVERT_PALSWAP toward engine default after soak.

**GOVERNING PRINCIPLE (2026-07-23, durable, generalizes past this bug):** the goal
is FAITHFUL INSTRUCTION EXECUTION -- EmulatR as an EV6 Oracle. Booting VMS is a SIDE
EFFECT of running the real firmware faithfully. Therefore every C++ no-op/stub of a
pure-PAL function is itself a FAITHFULNESS VIOLATION (EmulatR stops executing the
machine and substitutes a guess), NOT a neutral placeholder. The "tolerated no-op on
silicon" claim is the trap -- disproven by CSERVE 0x65, whose real side-effect the
console needs. Per-unhandled-func discipline: (1) INSTRUMENT the missing contract
(`EMULATR_CSERVE_AUDIT`); (2) CROSS-REF the guest `cfw_*` handler in apisrm source;
(3) CLOSE by ROUTING to the guest PAL (mirror-AXPBox, no C++ effect replication);
(4) VERIFY + record the contract. Generalizes to every intercepted CALL_PAL/IPR/
device path: run the real machine; instrument+document any substitution.

**The end-to-end chain (all root-caused):** decompressor -> POWERUP 0x8000 ->
`sys__reset` @0x13540 -> at 0x13654 `HW_MTPR r?,<scbd 0x2d>` (unassigned IPR)
faulted -> aborted `sys__reset` before `sys__reset_init` -> `p_temp`(r21) never
built (stayed decompressor scratch 0xf01) -> OS restart derefs garbage -> RESET(0)
at 0x20000000. Real silicon IGNORES unassigned-IPR writes; the fault was a KEPT
SCAFFOLD that reached `>>>` precisely BY skipping the real-HW init that builds p_temp.

**Fix stack (landed, all env-gated; tree uncommitted as of 2026-07-23):**
- FIX 1 `EMULATR_2D_NOOP=1` -- 0x2d MTPR -> no-op; `sys__reset_init` runs,
  `p_temp=0x7000` (NOT the PC264 def's 0xF000 -- trust runtime 0x7000). CONFIRMED.
- FIX 2 `EMULATR_DELAYWARP=1` -- general SUBQ-Rn-countdown-to-zero warp
  (PipelineDriver.h) collapses the 0x13e40/0x13e80/0x13ec0 Pchip1 settling delays
  the older warps missed. Reaches `>>>`. CONFIRMED.
- HANDOFF: CSERVE dispatch must RUN THE GUEST PAL, not C++-stub/replicate (proven
  by AXPBox, which boots VMS). `EMULATR_CSERVE_ROUTE=1` routes stubbed CSERVE funcs
  to guest `sys__cserve` (DS20 = 0x12d84, found by cmpeq-r16/bne signature scan);
  Option A (0x42 START -> guest `exit_console`) is the mirror-AXPBox default.
  Invariant per routed func: set p23(R23)=g.pc+4 in BOTH banks (intReg[23]+
  intShadow[7]), divert to guest handler (PALmode), NO C++ replication. Option B
  (C++ replicate restart, `EMULATR_CSERVE_START_MODE=cpp`) is a CONFIRMED DEAD END
  (seed PT__VPTB clobbered by console re-entry -> halt 0xA) -- `#if 0`'d out.
- SHADOW-BANK FIX (foundational) `EMULATR_DIVERT_PALSWAP=1` -- the C++ CSERVE
  intercept fires BEFORE PAL entry (palMode still 0, shadow bank not swapped), so a
  bare PC-divert let the guest handler read the ACTIVE p_misc (<63>=0 virtual)
  instead of the PAL-bank shadow (<63>=1 physical 1-1) -> DTB double-miss cascade ->
  PC=0. Fix: the WB no-fault divert path (PipelineDriver.h ~1610) now routes a
  mode-changing divert through `palModeEnter`/`palModeLeave` (SDE-gated), symmetric
  with the FAULT path + HW_REI. VERIFIED: eliminated BOTH the 0x65 cascade
  (54000->2 calls) AND the exit_console 0xA. EVERY divert-to-guest-PAL needs this.
  Candidate to promote to engine default (genuine correctness fix). (Also fixed an
  MSVC break: `memoryLib/GuestMemory.cpp` needed `<new>`+`<cstdlib>`.)

**STANDING WALL / RESUME POINT (EOD 2026-07-23):** with the fully faithful stack the
DS20 `b dqa0` handoff lands back at the ORIGINAL wall: **halt code 0 (RESET) at
PC=0x20000000, boot0 NOT fetched**. It is NOT EmulatR `kFaultHalt` (HALT-DIAG=0) --
the `[CON COM1]` line is GUEST console output = a GUEST-SIDE RESET. **The MCHK theory
is DISPROVEN** (re-read of the 17:36 faithful run: the ENTIRE run has ONLY
kFaultDtbMissDouble -- 5085 rows spanning past the 1.9417e9 handoff -- ZERO MCHK,
ACV, or OPCDEC; the reset is a CLEAN guest halt code 0, not a fault cascade; VA
0x20000000 is never even fetched). Root: `exit_console` restores the CONSOLE/CNS
context (resume PC=0x20000000) but NOT the OS-exec context (boot PTBR 0x1ff82 =
0x3ff04000 table / mode / IPL / VPTB self-map). AXPBox tolerates this by doing ALL
translation in C++ (never runs the guest miss handler); EmulatR runs the REAL
firmware and exposes the gap. TWO CANDIDATES the probe splits: (A) restore_state
bails to caller p23=console 0x1ae39c, 0x20000000 never attempted (p23/final-PC
0x1adab0 lean this way); (B) it HW_REIs toward 0x20000000 but PTBR stays the
console's (not 0x1ff82) -> wrong translation -> halt. **NEXT PROBE: `EMULATR_PCTRACE`
(landed 2026-07-23; coreLib/PcTrace.h + PalEntries case 0x42 arm + PipelineDriver
retire record + main.cpp +/-64-word GuestMemory windows). Arms at the exit_console
divert, snapshots PTBR(vs 0x1ff82)/VPTB/p_misc, records the next N retired PCs,
latches the first console re-entry (BAIL), dumps the collapsed trajectory + bail
context.** READ: PCTRACE-ARM ptbr==0x1ff82 -> disposition/mode bug (A); ==console ->
(B). RUN (PC): `tools/build_emulatr.sh relwithdebinfo` then
`EMULATR_PCTRACE=1 tools/run_ds20_bplus.sh` -> `b dqa0`. (The 17:36 run used the
STALE 17:02 exe built before the cyc-filter commit, so EMULATR_FAULT_CYCLO was not
in it -- a FRESH PC build is owed; a mac clang build confirmed the cyc-filter + the
PCTRACE symbols DO land.) Wrapper `tools/run_ds20_bplus.sh` defaults the full
faithful stack (2D_NOOP + DELAYWARP + CSERVE_ROUTE + DIVERT_PALSWAP). CAVEAT: do
NOT `u srm` in LFU (triggers a ~407e9-cyc mem re-init); plain LFU exit->n->>>> is
~30e9 cyc.

### 1.05 TB acceleration tier (strategic fork; brief authoritative)

Design brief (authoritative, read it): `journals/20260715_v5_tb_implementation_brief.md`.
Source records the brief promotes: `EmulatR_TB_Speculation_Record.txt`,
`EmulatR_TB_POC_Spec.txt`, `journals/jit_qualifying_ruleset.md`.

### 1.1 The lever hierarchy (decides which tool for a hot loop)

The diagnostic question for any hot loop: is it a SPIN or is it doing WORK?

- **SNAPSHOT eliminates cycles** -- restore committed state at entryPa; the
  cycles never execute. Load-phase substitution, nothing to do with TB. The
  entry snapshot is already built (see 3.7). Decompressor is the canonical case.
  Multi-order-of-magnitude.
- **WARP skips cycles** -- for a spin/busy-wait (reads a counter/IPR, compares,
  branches back, no other architectural side effect): recognize the shape,
  compute where it lands, jump the counter. The `krn$_micro_delay`/RSCC family.
- **TB cheapens cycles** -- removes repeated DECODE only (`(k-1)*n*D`); the
  executions `k*n*E` are UNTOUCHED. A constant-factor win (~1.5x if decode is a
  third of per-instruction cost); NOT an order-of-magnitude rescue. Only helps
  REAL-WORK loops. ComJIT extends it (native emission) but still executes cycles.

TB is the SMALLEST lever; it is warp's stable HOST (recognizer keys on TB
identity), not warp's replacement.

### 1.2 TB execution model (three routes, two passes)

- **Route 1 Oracle (V4 interpreter):** authority, always correct, the floor and
  the differential oracle. Everything deopts down to it.
- **Route 2 TB:** decode a straight-line run ONCE, dispatch many times; SAME
  executors as the Oracle (semantics executed, not re-expressed) -> faithful
  almost for free. **Register-state-as-property invariant:** a TB caches DECODE
  (opcode, ra/rb/rc indices, executor pointer), NEVER register values; grains
  read/write the shared register file across the boundary. **Cycle cost is
  decode-time grain data**, applied identically interpreted-or-dispatched, so a
  dispatched block advances the counter identically BY CONSTRUCTION. The
  terminating branch is the final IN-block grain, re-executed every pass,
  outcome recomputed from the live register file (usually loops to entry).
- **Route 3 ComJIT:** hot TB compiled to host code; FIRST tier that re-expresses
  semantics, so the ONLY tier the pure/impure inline/callout/terminate taxonomy
  applies to. Deferred until the invalidation substrate is proven against SRM.
- **Two passes are PER-ADDRESS, interleaved** (not two global runs): each PA
  transitions on its own first encounter -- built once, dispatched thereafter.
- **Eligibility scope (critical):** the JIT pure/impure ruleset governs Route 3
  ONLY. At Route 2 the RESIDUE IS EMPTY -- every instruction (loads, LD_L/STx_C,
  CALL_PAL) is a TB grain; only TERMINATORS matter, and they END blocks, they do
  not EXCLUDE them. Applying pure/impure to TB would disqualify the decompressor,
  memcpy/fill, and the LFU spin -> nothing to cache.
- **Split key (dispatch):** accelerator = (Virtual PC, ASN, PAL-mode); anchor =
  (Physical page, Page generation). Dispatch GATES ON THE ANCHOR (physical), so
  ASN recycle / aliasing / PTE remap all collapse to a generation-mismatch
  rebuild. **One shared invalidation substrate** for TB + ComJIT; prove it at
  the TB tier against the real SRM boot first.

### 1.3 First TB target -- the ES40 silicon LFU spin (a WARP case)

At LFU reset (`UPD> exit` -> "Initializing....") ES40 in silicon (REAL_HW) mode
hangs; ISP mode returns cleanly. Root-caused this session (2026-07-15) by
disassembly of the decompressed image (base 0x8000):

- The delay routine (entry ~0x6a468) gates on `platform()` at 0x8c1e0 (reads
  `*0xBFFC`, compares 0xCAFEBEEF -- the ISP-model sentinel): ISP -> early return
  (no wait); silicon -> a calibrated RSCC busy-wait. THAT single `beq` is the
  ISP-clean / silicon-hang divergence.
- The loop (0x6a4f8-0x6a520) is a working micro-delay: deadline = T_start +
  delta; spin while cur < deadline; wrap-guard exits if cur < T_start. Real ops:
  `cmplt r4,r0` (wrap-guard) at 0x6a514, `cmplt r4,r3` (deadline) at 0x6a51c.
  Registers: r4=cur, r0=T_start, r3=deadline. The instrumentation label
  "target=r0" is T_start, not the deadline.
- It IS a spin: RSCC read is `CALL_PAL 0x9d` via the stub at 0x1b78e8 -> PAL
  handler at 0xb740; then compare + branch-back; no other architectural side
  effect. Driven by an outer `krn$_sleep(2000ms)` issuing millions of ~2680-tick
  micro-delays. Bounded but ~10^10 cycles; at ~5 MHz that is tens of minutes.
- **Warp is the fix, not TB.** IDLEWARP does NOT engage (empirically warpTot=0
  through the hang -- it only fires on recognized idle/halt, not an active RSCC
  poll). RSCCWARP is quarantined (rewrites 0x3c970, corrupts state), hardcoded
  to a DIFFERENT pc (0x7c304), and its threshold 1<<20 wouldn't catch a
  2680-tick delay. The fix is a coherent deadline-warp: advance cycleCount to
  the deadline AND the interval-tick timebase together (the 0x3c970 bump is what
  quarantined the old family -- do it correctly). Instrumentation is landed
  (`EMULATR_RSCC_DIAG` in PalEntries.cpp / PipelineDriver.h / Machine.cpp;
  scripts `tools/run_es40_rscc_ab_{warp,nowarp}.sh`). Full handoff:
  `journals/20260715_es40_silicon_lfu_initialize_hang_HANDOFF.md`; RSCC-read is
  PAL-mediated so it straddles a CALL_PAL (a Route-3 TERMINATE) -- the recognizer
  keys on the compound (delay block + RSCC PAL stub).

### 1.4 Sequencing + open gates

- POC FIRST, off the V5 tree: branch `tb-poc` from `v4-frozen` (disposable
  measurement; deleted when Q1 economic / Q2 mechanical are answered). Do not
  commit the production TB struct/lifecycle until the k-amortization curve is in
  hand. Then Route-2 tier (physical-keyed store, per-CPU resolution front-end,
  reverse-dep slot), then ComJIT last. Section 12 of the brief is the step list.
- **Open _PROVISIONAL gates (resolve against primary sources, do not launder):**
  3.1 IMB-clean self-modification (Alpha has no I/D coherence -> is IMB-boundary
  invalidation faithful?); 3.2 async observer across MB (may MB relax on a
  uniprocessor guest?); 3.3 TLB shootdown IPR set + per-OS PAL handshake
  (SMP-only); 3.4 firmware entry premise (load at 0x8000). See brief Section 9.
- **V5 housekeeping owed:** `.axpsnap` -> `.snap` rename (see 3.7); sweep
  `tools/` for hardcoded `EmulatRAppUniV4` absolute paths (freeze-leak vector --
  make them self-locate); update `CLAUDE.md` to V5-active/V4-frozen; remove the
  stray `emulatrappuniv5_` clone.

---

## 2. DURABLE ARCHITECTURE (carries V4 -> V5)

### 2.1 CPU / dispatcher / determinism (load-bearing for TB)

- **AlphaCpuAgent + dispatcher (schedLib/) is the run path; the legacy
  `Machine::run` loop is DELETED.** Deterministic scheduling scaffold: `IAgent`,
  `Dispatcher` (logical clock + syncPhase), swappable `IExecutionDriver`
  (`SequentialDriver` oracle + `ThreadedDriver` std::barrier), `LockArbiter`.
  The `determinism_equivalence` doctest (Sequential == Threaded, bit-identical)
  is the acceptance fixed point -- a future determinism failure is the new code's
  bug, not the harness's. **This is the substrate the TB per-CPU front-end and
  the V4-vs-V5 diff gate stand on.**
- **Phase 1 (agent behind the dispatcher) + Phase 2 (CpuState ownership lift)
  CLOSED**, each landing byte-identical (gate `tools/phase1_dispatch_gate.sh`,
  now retired in favor of determinism_equivalence). CpuState lives in the agent;
  `cpuSlot` is the single which-CPU source; `kCpuStateVersion` at 9.
- **Phase 3 LL/SC cross-CPU interlock = THE cliff** (deferred): per-CPU
  lock_flag/lock_physical_address; any store by any agent clears others' flag on
  that granule; never yield mid-instruction but interleave at the LL/SC boundary;
  a contention micro-test is non-negotiable. Phases 4 (cross-CPU IPI, reuses
  Cchip work) / 5 (secondary rendezvous: real protocol is `start_secondary`
  outtig 0xC00028+id, NOT the 0xBFFC ISP flag) / 6 (determinism extension) follow.
  Design: `journals/20260618_smp_secondary_cpu_bringup_design.md`,
  `journals/20260619_phase2_task_ledger.md`.

### 2.2 Register / PAL / timing semantics

- **RSCC == m_cpu.cycleCount** (kCcMultiplier = 1, CpuState.h). A guest "wait
  until RSCC reaches target" is a wait on the emulator cycle counter.
- **HW_CC two-counter split:** the pipeline/trace counter (`cycleCount`, sim-
  only) is distinct from the architectural CC IPR (`ccOffset` + writable). A
  context switch (swpctx) routes per-process PCC through ccOffset and does NOT
  move the system timebase.
- **EV6 shadow registers (SDE):** R4-R7 and R20-R23 swap to shadow copies in
  PALmode when SDE=1. Relevant to any PAL divert/REI fidelity work.
- **PAL personality = OpenVMS**, not OSF (console image runs `ev6_vms_callpal.mar`,
  RSCC = ^x9d). VMS `sys__cserve` dispatches codes the OSF one no-ops (0x44
  MTPR_EXC_ADDR, 0x45 JUMP_TO_ARC, 0x46 IIC_WRITE, 0x65 MP_WORK_REQUEST).
- **HW_MTPR/MFPR encoding:** `hw_mtpr Rgpr,IPR` Ra=R31 Rb=Rgpr op 0x1D;
  `hw_mfpr Rgpr,IPR` Ra=Rgpr Rb=R31 op 0x19. bits15..8 = scbd selector, bits7..0
  = function/scoreboard mask (pipeline metadata, safely ignored). V4 stores
  `0x0100 + scbd` in HW_IPR to namespace off CALL_PAL codes.
- **PAL_TEMP:** raw scbd 0x40..0x5F -> HW_IPR 0x0200..0x021F (PT0..PT31).
  HW_PCTX (raw scbd 0x40) shadowed by PT0 (function-bit selector not decoded;
  fine until PALcode reads HW_PCTX directly).
- **HW_REI target:** encoding bit 12 selects STACKED (read excAddr) vs REGISTER
  (read Rb); low bit of target = resume palMode. `execHwRei` (PalEntries.cpp) is
  the reference reconciliation -- reads the PAL bit off the target, strips it for
  the PC, sets mode from that bit. Do NOT redesign it.
- **PC<0>/PALmode:** on EV6 pc<0> IS the PALmode flag; V4 holds mode in a
  separate `CpuState::palMode` bool kept as a strict mirror of pc&1.
- **CSERVE:** canonical namespace is #4 (SRM `$cserve_def`, pal_def.sdl:66-91):
  SET_HWE=8 ... HALT=64 WHAMI=65 START=66 CALLBACK=67 MTPR_EXC_ADDR=68 ...
  MP_WORK_REQUEST=101. The LAST defined is 101/0x65; 0x66/102 is UNDEFINED (its
  no-op is faithful). Note the 2026-07-07 "get_time at 0x66" was a regression --
  it wrote R0 with a TOY timestamp, shifting the ES40 SCB base and causing a
  first-tick halt; REMOVED 2026-07-08.

### 2.3 MMU / memory

- **GuestMemory is a sparse per-64KB-page pager.** Multi-byte accessors MUST
  split page-crossing reads/writes byte-wise (fixed commit 46151a0, 2026-07-02;
  an unaligned page-crossing store during the ES40 memory scan crashed the host).
- **Software TLB is faithful** (EV6 has no HW page-table walker). `Ev6Translator`
  harvest task (deferred): the emailed reference struct
  (`journals/ref_ev6Translation_struct_20260702.h`) supplies a 3-level walk,
  DTB/ITB PTE format converters, alignment-before-translation ordering, VA-form
  (43/48-bit) decode -- HARVEST-ONLY, not drop-in (foreign deps + its own kseg
  48-bit hardcode + permission-check gaps). Plan:
  `journals/20260702_ev6translator_harvest_task.md`.

### 2.4 Chipset (Tsunami/Typhoon; Titan; south bridges)

- **21272 = Tsunami** (Typhoon = its high-bw variant, same part); **21274 =
  Titan** (DS15/DS25/ES45). Titan shares the top-level PA map + Cchip/Dchip/TIG
  offsets with 21272 and REUSES those sub-chips; only new silicon = dual G/A-port
  PA-chip + AGP. Titan model landed (`chipsetLib/Titan21274_CsrSpec.h`,
  `TitanPchip.h`, `TitanChipset.h`) but is NOT yet an ISystemBus / lacks the
  device layer; `Machine.cpp` selection seam still hardcodes Tsunami.
- **South bridge is model-gated:** ALi M1543C (`chipsetLib/AliM1543C.h`) for
  ES40/ES45/DS25; Cypress CY82C693 for DS10/DS20 (default). Gate in
  `TsunamiChipset::wireDevices()`.
- **Tsunami PA map (as PALcode constructs it):** PCI0 mem 0x800_0000_0000;
  Pchip0 CSR 0x801_8000_0000; Cchip CSR 0x801_A000_0000; TIG 0x801_3000_0000;
  Pchip1 0x803_8000_0000. Key CSR offsets: TIG+0x40 = smir (halt-switch);
  TIG+0x3C0/0x5C0 = CPU0/CPU1 halt registers; Cchip+0x280 = DIR0; Pchip0+0x3C0 =
  PERROR. UART behind Pchip0 PCI I/O (R20=0x801fc000000 in dumps).
- **TsunamiTig** models smir (status-only read-0), halt/ipcr/arb_ctrl R/W; ipcr
  is storage-only (SMP-IPI gap). **Cchip IPI** wired: IPREQ->IPINTR->b_irq<3>.
- **Interval-timer / Cchip timing model is PARTIAL** (`fireIntervalTimer` TODO).
  Interval interrupts fire ~262k cycles apart in the ES40 LFU trace. Relevant to
  any RSCC/warp timing work.

### 2.5 HWRPB + platform identity (the firmware->OS hand-off contract)

- **HWRPB @ PA 0x2000** (single SRM-built copy; EmulatR's HwrpbBuilder is NOT
  used). Header: self-ptr@+0, id "HWRPB"@+8, SYSTYPE@+0x50 = 0x22 DEC_TSUNAMI
  (family-wide), SYSVAR@+0x58 (member = (SYSVAR>>10)&0x3F). **Per-CPU slot stride
  = 0x280** (AARM-canonical, corrected from a mistaken 0x400); slot fields run to
  Cycle Counter Frequency @+624. Region map (relative to 0x2000): TBB 0x2140;
  slots 0x2180; CTB 0x2680; CRB 0x27e0; MEMDSC/MDDT 0x2840; DSRDB 0x2ac0; GCT/FRU
  @ 0x3ff32000. `deviceLib/Hwrpb.h` has the kKeyValue offset layer + spec-true
  structs (static_asserts). Instruments: `EMULATR_HWRPB_SCAN`, `EMULATR_PA_WATCH`,
  `EMULATR_DUMP_PA` (planned). Detail: `journals/HWRPB_Region_Fidelity_and_Resume_20260624.md`,
  `journals/20260628_hwrpb_handoff_gates_plan.md`.
- **Platform identity = 3 channels** (keep aligned per firmware): A = chipset
  variant + IIC decode base (from ini `[System] model` via `kIicBaseByModel`:
  DS10=0xFFFF0000, DS20/DS20E=0xFFF80000; ES40 IIC base UNMAPPED/contested --
  likely ALi M1543C SMBus, not a fixed PCF8584 row); B = IIC device tree (from
  `<stem>_platform.json` via configureDevices); C = HWRPB system_type/variation
  (hardcoded DEC_TSUNAMI/0). ini `model` is a SEPARATE channel from
  firmware/manifest -- mismatch guard at `Machine.cpp` (P1 latch + P2 canary
  "platform latched: ..."). Boot canary is a greppable regression predictor.

### 2.6 Boot / firmware model

- SRM `.exe` is the COMPRESSED image; loaded two-stage (loadPa+sigOffset ->
  palBase+finalPC). The guest SROM decompressor self-runs every cold boot
  (~4M-cycle inflate at 0x60111c). `tools/host_decompressor/` is a byte-faithful
  native oracle of the DEC decompressor (trusted reference + `decompressed.rom`
  generator; strictly better than AXPBox which runs the guest decompressor). For
  ES40, decompressed image base = file_off + 0x8000 (RSCC stub landmark at
  0x1b78e8 confirms). `.rom` = the flash NVRAM backing (env + FRU), distinct from
  the `.exe`.

### 2.7 Snapshots / persistence

- **Level 1 snapshot landed** (CpuState + GuestMemory + chipset CSRs + SRM
  staging); auto-save every 10M cycles + on halt; autoload-newest at startup
  (`systemLib/Snapshot.{h,cpp}`, roundtrip test hive). Level 2 (in-flight
  pipeline state) deferred.
- **Entry snapshot (EMULATR_FAST_DECOMPRESS=snapshot):** mints
  `firmware/<stem>.axpsnap` at the init->console handoff (pc==entryPa, translated
  PA), restores it to SKIP the decompressor (restore-except-flash: current NVRAM
  wins). Gated ENTIRELY on the env var (main.cpp: `enableEntrySnapshot` +
  `tryRestoreEntrySnapshot`); `--no-autoload` does NOT block it but suppressing
  the env var does. The reset re-restore triggers on outtig(0xE00004) -- which is
  DOWNSTREAM of the LFU krn$_sleep hang, so it cannot rescue that hang.
- **Persistence layers:** flash ROM (emulated AMD-FSM, `<stem>.rom`) holds
  firmware + NVRAM env (serial at flash off 0x5f815); persisted only on clean
  exit (`~Machine::forceFlush`; SIGINT/SIGTERM -> `requestStop` flushes). HWRPB is
  RAM-only, rebuilt each boot, never persisted.
- **V5 NAMING (owed):** rename `.axpsnap` -> `.snap` (stem stays lowercase,
  mirrors `<firmware>.rom`). In V5, route BOTH stray hardcoded ".axpsnap"
  literals (main.cpp entry path ~295; Machine.cpp predig name ~1627) through the
  single `kSnapshotExtension` constant (Snapshot.h), set it to ".snap", and
  `mv` the existing file (rename, do not re-mint). Rationale: `.axpsnap` reads as
  the AXPBox (secondary Oracle, rejected Realm-2) format; this is a Realm-1
  EmulatR machine state at entryPa.

### 2.8 Floating point (GATES OS install)

fBox is an IEEE-T-only POC. Real: T-format arith (shallow), ordered T-compares,
CPYS, FP load/store all four formats, MT/MF_FPCR (storage), FEN trio, FTOIT.
STUBBED: ITOFS/ITOFT, ADDS/SUBS/MULS/DIVS. ABSENT (decode-fault): all
conversions, ALL VAX float 0x15 (the critical OpenVMS gap -- VMS defaults
G_float/F_float), SQRT, CMPTUN, FCMOVxx, FTOIS, FP branches. Rounding is host
round-to-nearest (trap-mode bits parsed but inert); IEEE traps never raised.
Build-out order: (1) conversions, (2) VAX G/F, (3) promote IEEE single stubs,
(4) SQRT/CMPTUN/FCMOVxx/ITOFx, (5) real FPCR semantics. Implement leaves NATIVELY
(bit_cast + host op + <cfenv>), per `Float.cpp`; `coreLib/proposed/` is ALGORITHM
REFERENCE only (does not compile in-tree). Map: `journals/fBox_FP_Coverage_Map_20260610.md`.

---

## 3. SETTLED FINDINGS + RULED-OUT (do not re-chase)

- **DS20 badge (SOLVED 2026-06-30):** "AlphaPC 264DP" mis-badge was a MISSING IIC
  discriminator device in the manifest. `get_sysvar` probes `iic_rcm_temp` @ IIC
  node 0x9e (KCRCM temp, DS20-only) -- absent -> member 1 -> 264DP. FIX: add node
  0x9e to `ds20_v7_3_platform.json` (runtime-loaded, no rebuild). RULED OUT:
  IIC-completion IRQ (bus runs POLLED, ENI never set), node 0x40 (OCP LED, on
  both models), the interface/open/interrupt theories. Pattern applies family-wide.
- **ES40 first-tick halt (SOLVED 2026-07-08):** CSERVE 0x66 get_time clobbered R0
  -> SCB base shifted by a timestamp -> null clock vector. FIX: delete case 0x66
  (falls through, R0 untouched). RULED OUT: superpage/kseg decode, "SCB never
  installed" (it is, at 0x28000), PuTTY gating, VPTB=0 double-miss (shared with
  DS20, survivable), PCI enumeration.
- **ES40 console health (2026-07-08):** SRM runs the full console-init banner but
  every byte goes to the UART at ISA base 0x2F8 (COM2) which V4 never
  `setBackend()`s -> blank PuTTY. A console-WIRING gap, NOT a firmware wedge. Fix:
  back the 0x2F8 port like 0x3F8. Non-Galaxy/non-Rawhide pc264 wires
  console_ttpb = com_devtab[0] to COM1.
- **ES40 silicon LFU "Initializing...." spin (root-caused 2026-07-15):** see 1.3
  -- a `platform()`-gated calibrated RSCC micro-delay; WARP target, not TB.
- **Memory size 64->1024 (SOLVED 2026-06-12):** SSOT plumbing bug (main.cpp used
  `--mem` default 64 not ini memorySize 1 GiB); AAR encoding was HRM-correct.
  `64M->asiz 0x3->AAR0 0x3009`, `1 GiB->0x7 ->0x7009`.
- **Halt-switch boot refusal (SOLVED 2026-06-13):** "Halt Button IN" = smir
  (TIG+0x40) fell to all-ones default (no TIG device modeled). FIX: faithful
  `TsunamiTig`. RULED OUT: `pal$halt_switch_in` impure flag, EI[4] interrupt.
- **ES40 SIGSEGV (SOLVED 2026-07-02):** GuestMemory page-crossing accessor overrun
  (see 2.3).
- **0xBFFC poll = ISP-model flag** (pc264 `platform()`), NOT a CPU1 rendezvous.
  `EMULATR_PLATFORM=isp` intercepts 0xBFFC->0xCAFEBEEF (reach >>> fast, skip
  real-HW timing); `=silicon` drops it (REAL_HW, runs the looping counters).

---

## 4. REFERENCE AUTHORITIES (machine-readable ground truth)

Read `Processor Support\REFERENCE_INDEX.md` first (manifest + "what to read for
X" table); do NOT glob the Processor Support tree. Start point for the source
tree: `Palcode\palcode\ROSETTA_STONE.md`.

Five canonical PALcode files (`Processor Support\Palcode\palcode\`):

| File | Authority for |
| ---- | ------------- |
| `srmconsole\EV6_DEFS.MAR` (= `apisrm\ref\ev6_defs.mar`) | IPR scbd codes, IPR bit-field layouts, exception/CALL_PAL entry-vector offsets, scoreboard masks, chip-IDs. RESET @0x780 on EV6 (0x0 on EV4/EV5); CALL_PAL bases 0x2000/0x3000 constant across generations. |
| `srmconsole\EV6_OSF_PAL.MAR` | Per-handler entry/function/exit contract for every CALL_PAL + hw-trap handler (OS-personality layer). The spec for how a PAL leaf must behave. |
| `srmconsole\EV6_OSF_PC264_PAL.MAR` | Tsunami chipset PALcode: the actual PA addresses / CSR offsets / access sequences vs Cchip/Dchip/Pchip/TIG (platform layer). |
| `apisrm\ref\ev6_huf_decom.m64` | The `hw_mtpr`/`hw_mfpr` assembler macros (operand-source convention). |
| `apisrm\ref\ev6_ipr_driver.c` | OSF/1 PALcode storage conventions (where each IPR value lives); console examine/deposit template. |

`coreLib/Ev6EntryVectors.h` encodes every entry vector with static_asserts locked
against EV6_DEFS.MAR (that file is the arbiter on a static_assert failure). PAL
personality note: the ES40 console runs the VMS variant (`ev6_vms_pc264_pal.mar`),
not OSF -- reconcile OSF-vs-VMS before trusting slot/comm-area/IPL layout.
`osf.h` (C master `palcode\include\osf.h`; SRM build `apisrm\ref\osfalpha_defs.mar`)
= chip-agnostic OS personality (VA/PTE 8KB 3-level, entIF/entInt/entMM, PCB
offsets, PS/IPL, exception frame). Provisional IPR/SCBD values: OK for storage,
NEVER for decode -- mark `_PROVISIONAL` and HRM-verify before any HW_MFPR/MTPR
dispatch matches them (silent PAL corruption otherwise).

---

## 5. DEFERRED WORK (consult before net-new work in the same area)

- **SMP Phases 3-6** (LL/SC cliff, IPI, rendezvous, determinism) -- see 2.1.
- **FP build-out (#43)** -- gates OS install; see 2.8.
- **PCI enumeration + on-board device models** -- SRM probes an un-enumerated
  on-board NIC (DE500/21143 tulip) -> all-ones BAR -> base 0xFFFF0000 ->
  TsunamiPchip UNHANDLED OUTER WRITE (non-fatal). Need a real PCI bus walk +
  dynamic BAR->range rebind (`IPciDevice` seam sibling of IBlockMedia). Tickets
  #37-42 (`tasks_20260612_boot_pci_deploy.md`); dq-boot -> SYSBOOT> needs this.
- **Ev6Translator harvest** -- see 2.3.
- **EV5 (21164) profile** -- parallel `coreLib/Ev5EntryVectors.h`; PT0..31 already
  provisioned. `dc21164.h` is the EV5 authority (no `dc21264.h` exists; EV6 uses
  ev6_defs.mar).
- **S_PalLinux codegen** -- `genGrains.py` doesn't emit `lookupPalLinux()` (~10
  lines). Generated-header edits are lost on regen -- change `genGrains.py`, not
  the output. Codegen leaves emit `Y X()`; hand-written use `auto X() -> Y`.
- **IPR field layouts** -- absorb EV6_DEFS.MAR sub-fields (PAL_BASE 32KB align
  mask is the one pre-OS correctness debt; EXC_ADDR field split; I_CTL CHIP_ID).
  Tier 3 = a `genGrains`-style parser -> `HW_IPR_Fields.h`.
- **Storage / dq-boot** -- IBlockMedia seam + ATAPI READ landed; IDE ~90%;
  Cy82C693 func1 enumerates dqa0/dqb0; ES40 now shows dqa/dqb via ALi M5229.
  SYSBOOT> scaffold gated on PCI (#41) + multi-block ATAPI (#32).

---

## 6. CONVENTIONS + BUILD (project-specific; complements global CLAUDE.md)

- Discuss-before-code for any non-trivial change (prose + file:line + edit shape,
  wait for approval); documentation at header + changed line (no anonymous
  changes); TODO discipline (greppable tag at header table + call site).
- ASCII(128) only in all file content (MSVC pipeline). Copyright header on every
  generated source/header (Markdown specs as an HTML comment) per ADR-0001.
  Include guards, never `#pragma once` (Qt MOC + /permissive- -> LNK2001). Hex
  radix for switch/case labels (convert dec->hex by VALUE: 16->0x10). Prefer
  surgical Edit over whole-file rewrites; V0/V1/V2 and Processor Support are
  read-only; V4 is FROZEN.
- doctest: `CHECK` only, never `REQUIRE` (exceptions disabled). Never name an
  enum printable helper `toString` (doctest ADL clash) -- use `<typeName>Name(T)`
  + operator<<.
- Build: root every build/run at `<project>/out/build/<config>` (release |
  relwithdebinfo | debug); `cd` there so relative paths resolve. `cmake` is not
  on bare-bash PATH -- build via `tools/build_emulatr.sh <config>` (sources
  `tools/vsenv.sh`/vcvars). Use relwithdebinfo/debug for diagnostics
  (`EMULATR_DIAG_*` compiled OUT of release). Confirm a facility is in the binary
  before a long run: `grep -a -c EMULATR_RSCC_DIAG out/build/<config>/Emulatr.exe`.
- Trace discipline: multi-GB traces -> bounded tails / gated windows only, never
  whole-file grep (times out, wedges the sandbox). Verify writes via bash
  (wc -l/grep) but remember the mount-truncation caveat -- host tools are truth.
- Output placement (EmulatR runs): run/console logs in `./logs`, execution/retire
  traces in `./traces`, named `purpose_YYYYMMDD_HHMMSS.ext`. EmulatR run/trace
  scripts live in `<project>/tools`.
- Collaboration: for analytically-heavy design, claude.ai web does the
  analysis/design and hands instructional changes to Cowork, which implements
  against the live tree.

---

## 7. JOURNAL INDEX (detail lives here; most load-bearing first)

- `journals/20260726_JRN-SCSI-027_io_stack_exonerated_loader_anchor.md` --
  NEXT SESSION STARTS HERE.  One-line brief: retire window UPSTREAM of
  pc 0x42790, three stages, ground truth in hand.  %SYSBOOT-F-LDFAIL
  decodes (architect, on real VMS) as %LOADER-E-BADIMGOFF -- facility
  0x13 = LOADER, so the bytes ARRIVE and do not parse.  THE WHOLE I/O
  STACK IS EXONERATED BY EVIDENCE: payload 68/68 FNV-matched vs
  dka0.vdisk (an image Charon boots into OpenVMS), DMA tiling 47/47
  exact cover with contiguous guest PAs, zero padding in the SYSBOOT
  window, and geometry closed by the driver's OWN block descriptor
  (0x000200).  Anchor captured by VALUE (addresses are not invariant,
  0x0013809A is): pc 0x42790 `LDQ r0,-0x10(r27)` LOADS the status from
  a linkage cell; 0x5ff0c/0x5e0bc/0xd150 just copy it up the return
  chain.  Verdict rule for the trace: ISD fields match the image but
  the compare fails -> 32-bit canonicalization lane (LDL/ADDL), EXTxH's
  genre; fields differ -> corruption upstream in memory.  Host-side
  ground truth: image header at LBA 697408 (EIHD maj 3, ISDOFF 296;
  EISDs +296/+332/+368, VBNs 2176/138/74).  ALSO: 3 instrument defects
  fixed (VACTL cap, WREG ignored DIAG_CYCLO, cmdTrace blind to data-OUT
  payloads) + the rule that a POST-HALT snapshot's page tables are the
  CONSOLE's, never valid for OS-era VAs.  Tools: scsi_read_diff.py,
  N810 trace w/ LBA+FNV+CDB, N810-MOVE tiling probe.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-026_vptb_desync_fixed_halt10_closed.md` --
  **HALT-10 CLOSED; DS20 NOW REACHES SYSBOOT.**  Root cause:
  execMtprVptb_vms shadowed EV6_VMS_CALLPAL.MAR:1524 INCOMPLETELY -- it
  did the VA_CTL + I_CTL merges but NOT `hw_stq/p r16, PT__VPTB(p_temp)`
  (PT__VPTB=^x0, EV6_PAL_TEMPS.MAR:33).  The guest's miss handlers +
  DTBM_DOUBLE_3 self-check read that CELL while VA_FORM formats from the
  IPRs -> guaranteed mismatch from the first OS MTPR_VPTB (pc 0x29dc4,
  R16=0xFFFFFEFC_00000000).  FIX = deferred memEffect, raw R16, 8B, with
  **S_PhysAddr|S_Store** (first cut omitted them -> Mbox translated
  p_temp as a VA and the wall MOVED to PC 0x29dc4) + hard-fault guard on
  bad p_temp.  V1 17 cases/2813 asserts, V3 497/500 (3 known drift), V2
  boot: halt-10 GONE, now `%SYSBOOT-F-LDFAIL ... status=0013809A`
  (reproduced 2x).  A4 AUDIT TABLE in Sec 5: PT__VPTB FIXED; PT__PTBR
  CLEAN (EMULATR_PTBR_DIAG fired 0x -- execSwpctx not on path, SWPCTX
  diverts to guest PAL); cpu.ptbr = LATENT TRAP (diag-only consumers,
  reads 0 in OS era); S_PhysAddr = class rule; PT__VA_CTL source +
  I_CTL width = documented residuals.  NEXT FRONTIER IS SCSI, not PAL:
  SYSBOOT dies on `VirtualDiskDevice: UNSUPPORTED opcode 0x15` =
  MODE SELECT(6) + `Ncr53C810 data-in 255 > available 36` -- it never
  reaches the file read.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-025_n4_vptb_era_ends_at_console_entry.md` --
  N4 DONE: the restore contract WORKS -- 228 writes install VPTB
  0x2_0000_0000 into BOTH VA_CTL (0xe5fd) and I_CTL (0xe7c1); MTPR
  dispatch and saved values are fine (suspects i/ii CLOSED).  The defect
  is a VPTB ERA that ENDS: last VPTB write cyc 1.8522e9, immediately
  followed by sys__enter_console (0x13351/0x13381: I_CTL vptb<-0,
  VA_CTL<-0x2 va48=1) with NO paired restore -- then ~313M cycles and
  thousands of writes ALL vptb=0, covering the whole OS era and the
  0x2a000 fetch that walls.  Also: 4 GARBAGE I_CTL VPTB writes
  (0xfffffefc...) at 0xdfd1/0xe001/0xe085/0xe091 right before the era
  ends.  NEXT (N5): DIAG 0xdf00-0xe100 + 0x13300-0x13400 gated
  CYCLO~1852246000 CYCHI~1852247200 -- names who calls that final
  enter_console (EmulatR divert that skips the restore vs faithful
  guest entry needing OS MTPR_VPTB).  Probe cap now tunable via
  EMULATR_VACTL_DIAG_N.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-024_n3_enter_console_named_vaform_vptb0.md` --
  N3 DONE + MAJOR CORRECTION: the 0x1333c clear = sys__enter_console
  (EV6_VMS_PC264_PAL.MAR:4638), and the "unpaired" final clear is the
  CRASH'S OWN halt path (trap__update_pcb_and_halt -> enter_console) --
  post-check, innocent; snapshot PT__VPTB=0 was post-mortem.  -023's
  VA_FORM exoneration WITHDRAWN: DIAG_WREG=4 on DTBM_DOUBLE_3 head shows
  ALL 17818 formatted PTE VAs carry VPTB=0 (bare va>>10 offsets) while
  PT__VPTB memory was correct -> the defect is cpu.va_ctl/i_ctl VPTB
  fields empty in VM mode.  Real PAL re-installs VPTB every callback
  exit via pal__restore_state MTPR of CNS__VA_CTL (= PT__VA_CTL |
  PT__VPTB merged at save).  N4: DIAG window 0xe55c-0xe700 + WREG=0
  shows the restored VA_CTL value -> decides dispatch-gap (annotated
  IPR index forms like <I_CTL ! ^x20>) vs bad-saved-value.
  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-023_pt_vptb_writer_watch.md` -- PA-WATCH
  0x7000: PT__VPTB is TOGGLED by the callback context swap -- 0x1333c
  (HW_MFPR IPR 0x1110; 42-bit mask; HW_ST r31 -> PT__VPTB) clears on
  callback ENTRY, 0xe558 restores 0x2_0000_0000 on EXIT (ra 0x62f6c).
  The FINAL clear (cyc 1885776941, stale ra=0x24d9c = data) is UNPAIRED
  -- no restore before the OS fetch at 0x2a000 -> crash1.  VA_FORM,
  timer diverts, and the cpp-seed all exonerated.  N3: identify IPR
  0x1110 + the 0x13300 routine; DIAG window 0x13300-0x13360 +
  0xe540-0xe580 names the unpaired caller; then pick fix altitude.
  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-022_halt10_va_form_dtbm_double3.md` -- N1
  DONE: halt-10 @ 0x2a000 decoded.  The code is OS-bootstrap (SYSBOOT
  territory: HWRPB re-checksum to +0x120=hwrpb$Q_CHKSUM), its epilogue
  cut at the page boundary.  Post-halt snapshot ptwalk: PTE for 0x2a000
  is VALID (PFN 0x36d, KRE, no FOE) and the missing code IS there --
  guest exonerated.  Halt 10 = VMS PAL DTBM_DOUBLE_3 rev-1.60 self-check
  crash1 (EV6_VMS_PAL.MAR ~1125): formatted-PTE-VA<63:33> != PT__VPTB.
  Chain: ITB miss -> PTE hw_ld/v double-miss -> check fails -> halt 0x0A.
  PRIME SUSPECT: EmulatR IVA_FORM/VA_FORM 43-vs-48-bit mode (VPTB =
  0x2_0000_0000, bit 33: 48-bit form drops it) -- the pre-named
  Ev6Translator-harvest gap.  NEXT: N2a static read of IVA_FORM impl vs
  HRM; N2b DIAG on palBase+0x300 window + WREG r26; N2c re-run V2.
  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-021_extxh_fix_landed_noiovec_dead.md` --
  **NOIOVEC ARC CLOSED**: EXTxH fix applied (architect-approved) --
  extwh/extlh/extqh now use the AARM byte_loc<5:0> shift; new
  tests/coreLib/test_byteops.cpp locks all 8 offsets + both idioms.
  V1 7/7, V3 492/495 (3 pre-existing drift only), V2: %APB-F-NOIOVEC
  GONE; APB accepts, does post-accept console I/O (18x CSERVE func 70),
  runs ~7.9s past the bootstrap jump, then NEW WALL: halt code 10
  (decimal, outside the 1..7 PAL table = software-posted) @ PC 0x2a000.
  NEXT (L2): N1 identify code at 0x2a000 (SYSBOOT?); N2 DIAG window on
  the pre-halt cycles; N3 decode the func-70 conversation.  Re-test
  OTHER old anomalies on the fixed binary (EXTxH corrupted every
  aligned-Rb H-idiom byte read guest-wide).  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-020_L1_ROOT_CAUSE_extxh_aligned_case.md` --
  **L1 ROOT CAUSE NAMED**: coreLib/alpha_int_byteops.h extwh/extlh/extqh
  return 0 for Rbv<2:0>=0; AARM Sec 4.6.1 (alpha_arch_ref.txt:9193)
  requires shift byte_loc<5:0> -- 64 truncates to 0 = PASS-THROUGH.
  Every pre-BWX byte read at addr==7 (mod 8) yields NUL: in
  "SCSI 0 8 0 0 0 0 0" the slot digit (offset 7) reads as 0 -> strtol
  "non-digit" -> walk sentinel -> %APB-F-NOIOVEC.  Q2 proof: byte-exact
  strtol start ptrs (DIAG_WREG=22) + PC-flow diff isolating the ONE
  divergent branch (0x2005e604).  Explains content-independence,
  IDE==SCSI footprint, AXPBox pass, probe==accept fail.  INSxH/MSKxH/
  EXTxL audited CORRECT; defect isolated to the 3 EXTxH.  FIX PROPOSED
  (shift = ((8-bytePos)*8) & 63) -- AWAITING ARCHITECT APPROVAL;
  verification plan V1-V3 in the journal.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-019_apisrm_source_grounding.md` -- SOURCE
  GROUNDING (architect pointer): booted_dev is built by file2dev
  (apisrm filesys.c:2812, sprintf "%s %d %d %d %d %d%s", numbers
  reversed, fd_table suffix) -> "SCSI 0 8 0 0 0 0 0" is BYTE-CANONICAL;
  console string builder exonerated for good.  "@wwid%d" = the SCSI3/
  fibre suffix = the grammar's wwid alternative (N = env var number).
  CORRECTION: CRB callback 0x22 = cbfunc$k_GET_ENV (open=0x10) -- the
  accept path issues another get_env, not open.  APB classify list
  {DVA,RAID,SCSI,MSCP,FLOP} lacks IDE -> IDE unbootable by this APB
  (VMB-022 source-grounded).  L1 residue: runtime-primed descriptor/
  position cells from get_env ANSWER transport; R4 reframed to answer
  BYTES (length/terminator), Q2 strtol DIAG run still decisive.
  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-018_p1b_death_site_field3_strtol.md` -- P1b
  DONE: the death site is field-3's numeric parse.  VM opcode dispatch =
  jump table [ctx-0x10]+4*(op-0xe4), code-base [ctx+0x8]; op 0xf3 ->
  0x20096040 -> custom strtol 0x2005e3a0 (base 10, +/- only, NO
  whitespace skip) returns 0 on a non-digit (cyc 1941882905).  Stream
  reads STOP at QW 0x99238 (field-3 operand never consumed).  Leading
  model: strtol starts at the separator space (descriptor +0x14 lags the
  whitespace scan); source string 0x2006aab8 is clean single-space.
  19f8-as-code WITHDRAWN (f8 encodings still open).  DECISIVE NEXT: Q2
  one DIAG boot over 0x2005e3a0-0x2005e6b0 names strtol's start offset;
  R4 AXPBox byte-exact boot_dev string.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-017_p1_gosub_tree_enumerated.md` -- P1 DONE:
  token low-9-bits = opcode (CMPULT 0x1f6 splits matcher/control at
  0x20095bb8); GOSUB = `f6 05|param16|disp16`, target = disp_VA+sext+2
  (cursor-proven).  TWO productions: A @ 0x9922c (8 fields + wwid
  alternative w/ terminal 81f5) and B @ 0x992b8 (VARIANT: f8-action pair
  replaces field 8).  Both failing walks used section-1/production-A only.
  Env rules live at 0x991d0+ (operand pool 0x2005c4xx; nibble tables
  0x99150+).  Verdict math: status 1..7 = structurally FAIL (bits[27:3]);
  only gosub stores or f8-action verdicts can accept; 19f8 targets
  0x20039278/0x20049278 = suspected IOVEC-builder action CODE (P1b debt:
  f8 encodings).  P2 = DIAG window 0x20096440-0x20096530 logs which rules
  get tried.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-016_n1_accept_mechanism_subwalk.md` -- N1'
  DONE: the resolver has exactly FOUR status stores; the ONLY success
  store is 0x20096524, guarded by "recursive sub-walk (BSR 0x200964fc ->
  0x20095840) returned non-sentinel".  The grammar is a recursive-descent
  VM (gosub records: 16-bit stream-relative or 32-bit cursor forms at
  0x200964a0-f8).  Accept = some sub-production returns non-sentinel; on
  EmulatR all 3 sub-walks returned the sentinel (trace-confirmed, own
  CMOVEQ each).  wwid terminals 0x2000e450/e470 = IOVEC param writers
  (dest +0x148/14c/150/154), no status effect.  NEXT: P1 static gosub-tree
  enumeration; P2 targeted DIAG window 0x20096440-0x20096530 (log sub-walk
  cursors); P3 AXPBox with "which gosub target returns non-sentinel".
  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-015_t3_grammar_ctx_object_decoded.md` -- T3 +
  grammar DONE: operand record = {u64 typeHeader, u64 handlerVA}, rel32 is
  END-of-record relative (+6).  Production fully mapped: ident (0x2000e140)
  + 7 field validators (e1e0/e260/e000/e2e0/e300/dfe0/e020).  The ctx
  0x20063820 is an OBJECT of the same format (classify e9d0, wrappers
  e890/e770, walk def0, msg f940/fb60, string buf 0x2006aab8) -- hence
  zero static call sites.  ALL main-production records are bit-10-SET
  (probe); the ONLY bit-10-CLEAR terminal (0x81f5 -> handler 0x2000e450)
  is in the wwid-alternative tail.  NEXT (N1'): decode 0x2000e450/e470 +
  the loop-exit 0x20096624->0x20096cbc; T4 via key-record inspection on a
  working boot.  Tool: tools/apb_stream_decode.py.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-014_t2_verdict_dataflow_decoded.md` -- T2/T2b
  DONE: fail sentinel = 0x158284 (cell 0x20065320), born at CMOVEQ
  0x20096e58 when status-longword (stack 0x28(r29)) bits[27:3]==0.  Status
  is stored per matching record (0x20096834) and KILLED by the record's
  own token flags (0x20096ae0); the flags word at 0x18(r29) IS the raw
  stream token (proven at 0x20096b18).  Token bits: 9=ext-byte,
  [16:11]=operand idx, 10=advance-record (probe-only), 12/13/14=class
  (14=link deref, NOT accept).  Boot_dev stream = 0x99216..0x99328 static;
  NO token in the whole image survives store-kill -> walk can never accept
  on this path regardless of string content; ident classify + field
  extraction PROVABLY WORK.  NEXT: N1 decode bit-10-clear token chains'
  exit path; N2 = T3 ctx/dispatch enumeration; N3 AXPBox with the specific
  "which status write is last" question.  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-013_t1_noiovec_branch_named.md` -- T1 DONE:
  the NOIOVEC-vs-accept branch is BLBS r0 @ 0x20003a10 (r0 = the
  0x2000def0 walk chain's return, ZAPNOT'd at 0x2000e844); not-taken ->
  byte loop + BSR 0x2000f940 -> 40x CRB puts; taken -> CRB callback 0x22
  via HWRPB+0xc0 (the open/IOVEC call APB never makes).  CORRECTS -012
  Sec 5.2: the walk runs TWICE (r18=0 probe @ 0x2000e974, r18=1 accept @
  0x2000e834, flag stored at ctx+0x18c) -- the ACCEPT PASS RUNS AND FAILS.
  Ident classify (0x2000e9d0, "SCSI" vs DVA/RAID/SCSI/MSCP/FLOP literals)
  SUCCEEDS.  Verdict compare operand: XOR vs static cell 0x20065320
  (0x20096514-20) = T2's new start point.  Tools: run_taskboot001_t1.sh +
  t1_apb_trace_analyze.py; hog note: 0x200098d0 bitmap loop eats 5e6
  retires (exclude via PCLO=0x2000a000).  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-011_crb_callback_conversation_decoded.md` --
  L1 FRONTIER: the CRB-window run captured the COMPLETE APB<->console
  conversation (86 calls, run_ds20_showdev_20260725_181452.log): get_env
  tty_dev->"0"; code 0x07 (undefined)->CBS$FAIL; set_term_int; get_env
  booted_osflags->"0"; get_env booted_dev AND boot_dev ->
  "SCSI 0 8 0 0 0 0 0" (= the JRN-VMB-021 ident+7-fields production); then
  40x getc/puts = the NOIOVEC message.  APB NEVER calls open/read -- the
  resolver judges ONLY that topology string.  NEXT (R4): get AXPBox's
  boot_dev/booted_dev strings for the same media and diff; (a) differ ->
  fix EmulatR's console-side string builder; (b) same -> pattern-VM
  comparison-site instrumentation with the known string.  Decoder tool:
  tools/crb_conversation_decode.py (one command re-derives the
  conversation from any CRB-window run).  LIVE FRONTIER.
- `journals/20260726_JRN-SCSI-010_l0_cause_named_cserve_start_mode_unset.md` --
  TASK-BOOT-001 Phase 1+2 CLOSED (acceptance MET 07-25 18:02): the evening
  L0 wall (halt 0 @ 0x20000000) was EMULATR_CSERVE_START_MODE unset in bare
  evening shells (CSERVE$START defaults OFF, PalEntries case 0x42); no code
  regression, ZERO code changed.  Proof: "CSERVE entry: func=66 (0x42)" x2
  with ZERO CSERVE-START-A lines in logs/pctrace_bootfail_20260725_172726
  .log; first run of NEW ./tools/run_taskboot001_phase1.sh reached
  %APB-F-NOIOVEC (run_ds20_showdev_20260725_180201.log).  L0 OPEN.
  Re-baseline PASS 18:09 (old window: 752 unique PCs EXACT vs JRN-SCSI-004;
  run_ds20_showdev_20260725_180914.log).  P1+P4 APPLIED + VERIFIED same
  evening (session autonomy): engine defaults now START=guest, ROUTE=on,
  DIVERT_PALSWAP=on (opt-out via =0/off) + two loud no-op tripwires;
  verified by a scrubbed-env boot with ONLY EMULATR_2D_NOOP=1 -> NOIOVEC
  (logs/bareboot_p1_20260725_204341.log).  CAVEAT: fully-bare DS20 boots
  still need EMULATR_2D_NOOP=1 -- without it p_temp is never built and
  the guest restore_state RESETS into the LFU (0x2d disposition = open
  architect decision, JRN-SCSI-010 Sec 5 Leg B).  LIVE FRONTIER.
- `journals/20260725_JRN-SCSI-006..009` -- NOIOVEC track continued: mode =
  arg4/r19 not r7 (-006); env-audit scope correction (-007); manifest vs
  discovery reconciliation + PREEDIT A/B staging (-008); the layered causal
  model L0/L1/L2 + ordered runbook R1-R5 (-009, READ FIRST for boot work).
- `journals/20260725_JRN-SCSI-005_a4_axpbox_boots_same_media.md` -- A4:
  AXPBox boots the same dka0.vdisk into OpenVMS V8.3 (no NOIOVEC) ->
  EmulatR ENVIRONMENT GAP confirmed; 0xf3-tail gate decoded (bit 10 =
  probe-only; CMOVEQ no-match birth); probe plan (EmulatR-ES40 control,
  r7-mode caller disasm, env diff); AXPBox harness ops notes.  LIVE
  FRONTIER.
- `journals/20260725_JRN-SCSI-004_p3_noiovec_scsi_identical_footprint.md` --
  live gate PASSED (pka0/dka0 enumerated via pke+SCRIPTS); P3 `b dka0` ->
  NOIOVEC with a BYTE-IDENTICAL resolver footprint vs IDE (752 PCs, diff
  empty); "APB predates IDE" REFUTED as root cause; 0xf3-tail gate is
  protocol-independent; A4 AXPBox same-media = top probe.  LIVE FRONTIER.
- `journals/20260725_JRN-SCSI-003_implementation_p0_p2_landed.md` -- SCSI
  IMPLEMENTATION LANDED (S1 seams: Pchip unregister APIs + chipset bulk-DMA
  helpers + real tulip rebind; Ncr53C810 HBA with SCRIPTS engine;
  VirtualDiskDevice; manifest scsi_disk type; DS20 slot 8 entry).  Unit
  green: full SCRIPTS INQUIRY/READ(10) transactions pass; 487-case suite
  clean except 3 PRE-EXISTING drift failures (ide_wiring + 2x mmio_csc).
  OWED: live `show dev` gate (pka0/dka0), then P3 `b dka0` NOIOVEC retest.
  ACTIVE WORK ITEM.
- `journals/20260724_JRN-SCSI-002_io_stack_architecture_map.md` -- the
  CPU-to-SCSI-disk I/O stack: faithful layer map (outbound PIO, inbound
  DMA + interrupts, SCSI bus, guest software stack), EmulatR status per
  layer, and the gap/seam ledger G-A..G-G (deepest: NO bus-master/DMA
  seam exists; PciMemRange 16-bit squeeze; BAR rebind; SCSI bus; device
  snapshot participation).  Read WITH SCSI-001/-003.
- `journals/20260724_JRN-SCSI-001_pci_scsi_hba_design.md` -- PCI SCSI HBA +
  virtual disk design (NCR 53C810/PKE recommended; console driver PROVEN
  present in DS20 v7.3-2; pke_driver.c+pke_script.mar = contract; AXPBox
  Sym53C810 = secondary reference; B1 BAR-rebind prerequisite; phases
  P0-P4 with open questions Q1-Q5).  ACTIVE WORK ITEM (user-directed
  2026-07-24), awaiting design approval.
- `journals/20260724_JRN-VMB-022_a3p2_no_ide_keyword_in_apb.md` -- NOIOVEC
  part 6: whole-boot scan (resolver never succeeds); APB has NO "IDE" keyword
  anywhere; hypothesis = this APB predates IDE boot, NOIOVEC is correct;
  EmulatR exonerated end-to-end; A4 AXPBox same-media test is decisive.
  LIVE FRONTIER.
- `journals/20260724_JRN-VMB-021_a3_walk_transcript_grammar_decoded.md` -- NOIOVEC
  part 5: full walk transcript; grammar decoded (production EXISTS: ident+7
  fields); walk dies at field 2 with handler PDSCs never called; bottleneck =
  the 0xf3-tail mode/flag gate.
- `journals/20260724_JRN-VMB-020_a1_a2_executed_apb_exonerated.md` -- NOIOVEC
  part 4: A1 (snapshot diff) + A2 (AARM replay oracle) both CLEAN; halt-exits-
  process correction; scripted-console runbook; frontier -> A3/A4.
- `journals/20260724_JRN-VMB-019_noiovec_string_exonerated_pci_plan.md` -- NOIOVEC
  part 3: BOOT_DEV string + PCI exonerated; failure isolated to APB's internal
  pattern-VM (0x158284 @0x20096e58); Track A probes + Track B PCI #41 scope.
- `journals/20260724_JRN-VMB-018_apb_noiovec_investigation.md` +
  `20260724_JRN-VMB-018_P2_apb_exe_static_analysis.md` -- NOIOVEC parts 1-2:
  callback/GETENV decode + APB.EXE static analysis (their GCT and
  truncated-string hypotheses are REFUTED by VMB-019).
- `journals/20260724_JRN-VMB-017_exit_console_divert_target_rootcause.md` -- the
  five 2026-07-24 fixes (divert locator, HW_LD/ST EA truncation, WH64/FETCH
  hints, CSERVE 0x43 routing, p23 PAL-view).
- `journals/20260722_JRN-VMB-016_0x20000000_wall_end_to_end_rootcause.md` -- the
  DS20 OS-handoff wall: END-TO-END root cause + fix stack (2D_NOOP/DELAYWARP/CSERVE
  routing/DIVERT_PALSWAP) + governing principle (Sec 3.7) + EOD resume (upstream chain).
- `journals/20260722_JRN-VMB-004_cserve_start_boot_handoff_root_cause.md` -- CSERVE
  START (0x42) = the handoff (symptom-level, subsumed by VMB-016).
- `journals/20260720_architecture_development_status.md` -- timeline-free system
  snapshot (ARCH-STATUS-001; readiness table, execution model, gaps).
- `journals/20260722_AXPbox_EmulatR_Interface_Gap_3way.md` -- AXPBox/EmulatR device
  + handoff interface gaps.
- `journals/20260720_network_adapter_de500_support_options.md` -- DE500/21143 NIC
  design (NET-ADAPTER-001).
- `journals/20260719_JRN-VMB-013/014` -- boot-transfer wall + the (corrected) r21/
  p_temp hypothesis; VMB-016 supersedes the r21=0 read (r21 is 0xf01 scratch).
- `journals/20260715_v5_tb_implementation_brief.md` -- the V5 TB brief (current).
- `journals/20260715_es40_silicon_lfu_initialize_hang_HANDOFF.md` -- LFU spin.
- `journals/20260713_es40_lfu_rscc_warp_instrumentation_spec.md` -- RSCC_DIAG A/B.
- `journals/jit_qualifying_ruleset.md` -- Route-3 eligibility ruleset.
- `journals/20260619_phase2_task_ledger.md` + `20260618_smp_secondary_cpu_bringup_design.md` -- SMP.
- `journals/HWRPB_Region_Fidelity_and_Resume_20260624.md` + `20260628_hwrpb_handoff_gates_plan.md` -- HWRPB.
- `journals/20260702_ev6translator_harvest_task.md` -- MMU harvest.
- `journals/fBox_FP_Coverage_Map_20260610.md` -- FP audit.
- `journals/20260616_titan_21274_interface.md` -- Titan chipset.
- `journals/Snapshots_Design_Notes.md` -- snapshot design.
- `Processor Support\Palcode\palcode\ROSETTA_STONE.md` + `Processor Support\REFERENCE_INDEX.md` -- reference navigation.

The `journals/` directory holds the full dated record (boot-bringup blow-by-blow,
retracted theories, EOD handoffs) that this file deliberately compresses.

EmulatR Version should match Help & Manual Version.  We should create a scaffold that when we generate - publish documentation, 
it updates a C++ header that is included in the build. 
One version, one source of truth. The emulatr-doc-release skill already maintains versionbuild in 
the H&M .hmxp project file as the documentation's version authority. If the UART banner ("Alpha Emulator Console V4.0-0") carries 
its own hardcoded string, that's two owners for one fact — the exact pattern the SSOT rules exist to prevent, same family as the 
kSnapshotExtension single-constant rename on the housekeeping list. 
The clean shape: 
one kEmulatrVersion constant (or a build-time-generated version header) that the UART banner, 
--version output, log headers, and any About surface all consume; 
the release workflow then bumps one place and the H&M versionbuild tracks it (or is generated from it) at release time. 
Also worth deciding while you're in there: the banner says V4.0-0 while the active tree is 
V5 — per the file-naming convention the version lives in headers and trees, not names, but a user-facing banner 
claiming V4 from a V5 build is a real mismatch, not a naming-convention question.