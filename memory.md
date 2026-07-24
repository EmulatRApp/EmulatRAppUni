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
- **LIVE FRONTIER (2026-07-23):** DS20 OpenVMS boot-handoff. The SRM->OS transfer
  at VA `0x20000000` is now driven by a FULLY FAITHFUL path (no C++ stub/replica);
  standing wall is a guest-side RESET at boot0 entry (OS IPR context gap). See 1.0
  + `journals/20260722_JRN-VMB-016_0x20000000_wall_end_to_end_rootcause.md`.
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

### 1.0 Boot-correctness -- the DS20 OS handoff at VA 0x20000000

Authoritative journal (read it):
`journals/20260722_JRN-VMB-016_0x20000000_wall_end_to_end_rootcause.md` (end-to-end
root cause + fix stack + EOD resume). Also `20260722_JRN-VMB-004` (CSERVE START =
the handoff), `20260720_architecture_development_status.md` (system snapshot).

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
the line is `[CON COM1]` GUEST console output = a GUEST-SIDE RESET, almost certainly
an MCHK at boot0's first fetch because `exit_console` restores the CONSOLE/CNS
context (resume PC=0x20000000) but NOT the OS-exec context (OS PTBR 0x1ff82/mode/
IPL/VPTB self-map). AXPBox tolerates this by doing ALL translation in C++ (never
runs the guest miss handler); EmulatR runs the REAL firmware miss handler and so
exposes the OS-context gap (real silicon would MCHK too). NEXT PROBE: UNCAP the
`FaultEventLog` (caps at 64, all consumed by cyc 1.21B in powerup, hiding the
cyc-1.9B handoff faults) so the boot0-entry fault->MCHK->reset chain is visible and
names WHICH IPR is wrong after exit_console. SECONDARY: verify EmulatR models
`I_CTL[SPE]` superpage. Wrapper: `tools/run_ds20_bplus.sh` (defaults the full
faithful stack: 2D_NOOP + DELAYWARP + CSERVE_ROUTE + DIVERT_PALSWAP). CAVEAT: do
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

- `journals/20260722_JRN-VMB-016_0x20000000_wall_end_to_end_rootcause.md` -- the
  DS20 OS-handoff wall: END-TO-END root cause + fix stack (2D_NOOP/DELAYWARP/CSERVE
  routing/DIVERT_PALSWAP) + governing principle (Sec 3.7) + EOD resume. LIVE FRONTIER.
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
