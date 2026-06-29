# EmulatR — Project Memory

Auto-loaded session memory.  Read this and the project `CLAUDE.md`
first to come up to speed; then drill into specific journals only
as needed.  Most recent entries at top.

Keep this file tight.  Move historical detail into dated journals
(`EmulatRAppUniV4/Emulatr/journals/YYYY-MM-DD*.md`) and reference
them from here.

---

## 2026-06-25 -- EMULATR_HWRPB_SCAN instrument BUILT (Mac) + macOS build/launcher hardening

**Headline: resumed the HWRPB-region work on the Mac. Built the `EMULATR_HWRPB_SCAN`
guest-memory probe (resume-journal §7a) into `systemLib/Machine.{h,cpp}`; suite 472/472
green. LIVE DUMP CAPTURED -> HWRPB @ PA 0x2000, badge pinned to SYSVAR=0x405=member 1 @
PA 0x2058; two-HWRPB question RESOLVED. Got the macOS build + launcher working. Caught a
decimal/hex error in the resume journal. ALL UNCOMMITTED.** Full writeup:
`journals/20260625_hwrpb_scan_instrument_and_mac_build.md` (§7 = live dump results).

- **LIVE DUMP RESULT (the big finding).** DS20 cold boot, `set sys_serial_num HEAX1PEER` +
  `touch EMULATR_HWRPB_SCAN`: Scan B found **exactly ONE** HWRPB, at **PA 0x2000**, carrying
  our serial -> it is the **SRM-built** one (NOT EmulatR's `HwrpbBuilder`, which is "fixed at
  PA 0" per FirmwareDeviceManager.h:175 and is NOT present/used). Header is spec-conformant:
  self-ptr@+0=0x2000, id="HWRPB", rev 14, size 0xb80, page 8K, serial@+0x40, **SYSTYPE@+0x50
  = 0x22 = DEC_TSUNAMI (CORRECT, family-wide DS10/DS20/ES40), SYSVAR@+0x58 = 0x405 ->
  member (SYSVAR>>10)&0x3F = 1 = "AlphaPC 264DP" = THE BADGE**. (DS20 = member 6 => SYSVAR
  0x1805.) CONSEQUENCE: patching EmulatR's HwrpbBuilder would NOT fix the badge; the decision
  is firmware-internal (get_sysvar). The 8 non-HWRPB serial hits (console echo/history bufs,
  env-var store @0x30e20, heap copy @0x3ff4bf58) all correctly rejected -> disambiguation works.
- **HWRPB REGION MAP (top-level directory) decoded from the 0x2000 header** -- journal §9.
  All section pointers are RELATIVE to base 0x2000 (proven: fru_offset 0x3ff30000 -> abs
  0x3ff32000 = the GCT/FRU anchor). PAs: TBB 0x2140; per-CPU slots 0x2180 (count 2, **size
  0x280**) slot1 0x2400; CTB 0x2680 (1 x 0x160); CRB 0x27e0; MEMDSC/MDDT 0x2840; DSRDB 0x2ac0;
  CDB 0x38880; FRU/GCT 0x3ff32000. Inline sections pack contiguously inside the 0xb80 block
  (0x2000..0x2b80). **DIVERGENCE to verify:** live SLOT size 0x280 vs our `Hwrpb.h` PerCpuSlot
  static_assert 0x400 (AARM-canonical) -- reconcile vs apisrm hwrpb_def. Internal sub-struct
  fields NOT yet dumped (next pass).

- **`EMULATR_HWRPB_SCAN` instrument (the session deliverable).** Sentinel-gated one-shot
  (resolved in `run()`, polled in `systemTick()` beside the stop sentinel -- the journal's
  "Machine.cpp:1058" trigger line is STALE post P2-T2 split). Two scans of the sparse
  allocated pages (`GuestMemory::forEachPage`): (A) PATTERN `EMULATR_SCAN_PATTERN` (default
  `HEAX1PEER`) -- per hit `H`, dump `[H-64,H+192)`, validate base `B=H-64` via self-pointer
  (`read8(B)==B`) + id (`read8(B+8)==0x0000004250525748`="HWRPB"); (B) SIGNATURE -- serial-
  independent walk for that self-pointer/id pair, finds the HWRPB even if `sys_serial_num`
  doesn't propagate AND reports EVERY HWRPB (resolves the two-HWRPB question). Env-gated,
  zero cost off. USE: `set sys_serial_num HEAX1PEER` at `>>>`, then `touch EMULATR_HWRPB_SCAN`;
  dump -> stderr/`fw_ds20_<ts>.out`.
- **SPEC GROUND TRUTH + JOURNAL CORRECTION.** Per `deviceLib/Hwrpb.h` static_asserts:
  serial@+64, self-ptr@+0, id@+8, **SYSTYPE@+80 (0x50), SYSVAR@+88 (0x58)**. The 2026-06-24
  resume journal's "SYSVAR/SYSTYPE at +0x80/+0x88" is a decimal/hex slip -- the NEXT step
  (`EMULATR_PA_WATCH`) must target `base+0x58` for SYSVAR, not 0x88.
- **macOS build working + Release option added.** `scripts/build_mac.sh [Release|
  RelWithDebInfo|Debug]` (default RelWithDebInfo, back-compat); Release->`out/build/mac-release`.
  RelWithDebInfo already has -DNDEBUG, so Release ~= -O3-vs-O2 only (modest). Binary is bare
  `Emulatr` (Mach-O x86_64), not `.exe`.
- **`tools/run_fw.sh` macOS-portable (real bug fixed).** (1) binary name `.exe`-or-bare;
  (2) **`sed -i` was GNU-only and silently no-op'd the `model=` line on BSD/macOS sed** ->
  replaced with temp-file + POSIX `[[:space:]]`. This had caused a 32-min "hang": DS20
  firmware ran on an `model=ES40` chipset (Cypress vs ALi) because the model never flipped.
  Now `./run_fw.sh ds20` echoes `model : DS20`. (`tools/run_fw.sh` is the CMake-deployed one;
  root + tests copies are stale.)
- **PuTTY on Mac:** off by default; even on it's Windows-pinned (`putty.exe`, hardcoded
  `d:/...` sessionlog at `SRMConsoleDevice.cpp:656`) + needs XQuartz. Use `nc localhost 10023`.
- **PERSISTENCE MACHINERY (resolved + dumpbin-proven).** 3 layers: (1) flash ROM = emulated
  AMD-FSM flash (`chipsetLib/FlashRom.cpp`), written by BOTH `update srm` AND `set` via
  0x5555/0x2AAA, backing `ds20_v7_3.rom`, persisted ONLY on clean exit (`~Machine::forceFlush`);
  (2) NVRAM env INSIDE that flash (serial at flash off 0x5f815); (3) HWRPB = RAM-only, built
  each boot, NEVER persisted. Dumpbin of both firmware files: `.exe` has ZERO HWRPB/serial
  (it's the COMPRESSED image), `.rom` has the serial (NVRAM) but ZERO HWRPB. => LFU/`update
  srm` writes FIRMWARE to flash, NOT the HWRPB; HWRPB+SYSVAR are a DRAM event at from_init.
  `--firmware ds20_v7_3.rom` MIS-ROUTES (main.cpp routes `.rom`->loadDecompressedRom, but the
  file is the flash backing) -- approach dropped. Instruments: `EMULATR_FLASH_TRACE` (flash
  writes) vs `EMULATR_PA_WATCH` (DRAM/HWRPB).
- **GRACEFUL-EXIT FLUSH (LANDED + verified).** SIGINT/SIGTERM previously killed the process
  before `~Machine::forceFlush` -> `update srm`/`set` LOST. Added: `Machine::requestStop()` +
  `m_stopRequested` atomic (async-signal-safe store ONLY) polled every tick in `systemTick()`;
  `main.cpp` `extern "C"` handler (SIGINT+SIGTERM) -> requestStop, 2nd signal = SIG_DFL
  force-quit; try/catch around `mach.run()` so an exception can't bypass the flush. Portable
  via `std::signal` (no #ifdef). Suite 472/472. Verified: `kill -INT` -> "stop requested
  (signal) -- clean exit" -> destructor flush. (No CMake change -- main.cpp already in build.)
- **STATIC ANALYSIS (option C, journal §11).** Built the byte-faithful DECOMPRESSED DS20
  image via `tools/host_decompressor` (clang: `cc -O2 -o oracle src/oracle.c src/inflate.c`;
  `./oracle firmware/ds20_v7_3.exe out/decompressed_ds20_v7_3.bin`; target=0x8000, sig OK).
  **ADDR MAP: runtime VA = file_off + 0x8000** -> any runtime PC disassembles at PC-0x8000
  (the image is the disassembly substrate; the .bin is regenerable, NOT committed). Banner
  table @ 0x153cd8 (stride 0x2c, base 0x153cac=member0) fully decoded: member1->264DP
  (0x19a6c8), member2->DS20; SYSVAR 0x405->member1 confirmed. Strings: "Defaulting system
  type to AlphaPC 264DP" @0x19ad90, "Error determining system type, SYSVAR=%x" @0x19adc0,
  iic_ocp0 @0x17a3c0/0x1a0218 (no debug symbols). **STRONG INFERENCE: get_sysvar hits its
  DEFAULT path (can't ID the DS20 -> falls back to 264DP).** Static code-location HIT THE
  GP-RELATIVE WALL (4 methods, all 0; addresses are computed gp-relative, never stored as
  literals) = the journal's EXHAUSTED route; don't relitigate.
- **NEXT (cheap + decisive):** (1) CONSOLE CAPTURE `plink -raw -P 10023 localhost | tee
  console.log` and grep boot for "Defaulting system type"/"Error determining system type,
  SYSVAR=0x.." -- the firmware names its own path (near-definitive, no disasm). (2) capture
  get_sysvar PC via PA-watch, disassemble the decompressed image at PC-0x8000.
- **(earlier next, still valid):** build `EMULATR_PA_WATCH=0x2058` store-watch in `pipelineLib/MemDrainer.h`
  (beside EMULATR_SYSVAR_WATCH), ARM BEFORE a COLD boot (SYSVAR is written during cold init,
  before `>>>`), capture the writing PC -> Ghidra `get_sysvar`/`build_dsrdb` to learn WHY
  member 1 (NOT a blind patch) -> then map CTB/CRB/MEMDSC/DSRDB/FRU from the 0x2000 header's
  +160/+184/+192/+200/+216/+312 offsets -> boot-time validator. CAVEAT: confirm PA 0x2000 is
  stable across cold boots (or have PA_WATCH self-locate the header, then watch base+0x58).

---

## 2026-06-20 -- PHASE 2 CLOSED (P2-T1 .. T6) + fixes; suite 459/459, boot byte-identical

**Headline: CLOSED PHASE 2 -- the full AlphaCpuAgent ownership lift (T1..T6).
CpuState now lives in the agent; the dispatcher path is THE run path (legacy loop
DELETED); the system clock + trace sinks are dispatcher-level shared.  Every step
landed byte-identical (Emulatr_tests 459/459 throughout; phase1_dispatch_gate.sh
byte-identical until T6 retired it).** The durable per-step record is
`EmulatRAppUniV4/Emulatr/journals/20260619_phase2_task_ledger.md` (read it FIRST
when resuming; the APPLIED records + the Phase-2-CLOSED / Phase-3-NEXT handoff are
at the end).

- **P2-T1 (committed): trace cpuId tag + cyc->rpcc rename.** DecListingSink emits
  `cpu=<n>` and renames cyc=/cycle= -> `rpcc=` on every line (RET/INS/DEC-listing
  `cNN` column/REG/FRG/HEARTBEAT/PAL_ENTRY/PAL_EXIT/RUN_END); BreakpointSink same
  cyc->rpcc on BP_OPEN/CLOSE/IPR/PT/CBX_SNAP/BRK records (CKPT/summary/console
  prose left); analyze_retire_trace.py regex tolerant; test pins updated.
- **P2-T2 (committed): split Machine::stepCycle into cpuKernel(cpu) +
  systemTick(now).** cpuKernel = PipelineDriver::step only; systemTick = sentinel
  + snapshots + evalDeviceIrqs + IDLEWARP + interval-timer FIRE/DELIVER + b_irq
  diverts + synthetic inject (once/quantum); statics moved. Split BY CURRENT
  ownership; DELIVER (pendingIrq2(0)) + snapshot-on-PC (m_cpu.pc) stay in
  systemTick, flagged STEP-4 SMP seams. Gate byte-identical.
- **P2-T3a (committed): decouple systemNow() onto m_systemClock.** Advanced by the
  RAW per-step retire-cycle delta (D-1a) in stepCycle; IDLEWARP system-clock-
  primary; resync m_systemClock=cycleCount on resetToLoadedEntry + restoreSrmStaging
  (the dormant-arm reset/autoload path the boot gate never hits). Gate byte-identical.
- **P2-T3b (committed): global retire ordinal.** Leading `ord=` field (DEC `o<n>` column) on every trace line,
  from a per-onCommit m_retireOrdinal stamped into LookbackEntry.ordinal via
  freezeRecord. Under the single-coalesced-sink design this IS the dispatcher
  global retire order (NOT cycleCount, NOT rpcc). Gate stays byte-identical (gate
  runs without --trace). MUST re-mint stored golden + AXPBox once in this commit
  -- only AFTER T3a's gate proved the clock byte-identical (laundering hazard).
- **Fixes (committed with T1..T3a):** HwpcbContext (swpctx) routes per-process PCC
  through ccOffset, not raw cycleCount (load: ccOffset=src.cc-cycleCount; store:
  dst.cc=cycleCount+ccOffset) -- a context switch no longer moves the system
  timebase, and swpctx drops out of the cycleCount-write set. Test console binds
  EPHEMERAL port (EMULATR_CONSOLE_PORT=0 honored in makeCom1Cfg; set in
  tests/main.cpp) -- ends the fixed-10023 collision. test_semanticflags DispatchKind
  canonical count 18->19 (codegen-grown enum; tripwire fired correctly -- it was
  the real "1 failed" all run, masked by port/memsize log red herrings).
- **P2-T4 (committed): CpuState ownership into AlphaCpuAgent.** Agent owns
  m_cpuState (cpuSlot from id()); Machine holds a persistent by-value m_agent0 and
  m_cpu is a CpuState& ALIAS into it (so ~all m_cpu.<field> sites compile
  unchanged); safe because Machine is non-copy/non-move-constructible;
  bindCycleSource/L1 unchanged (m_agent0 address-stable). Dispatch reuses m_agent0.
- **P2-T5 (committed): whami -> cpuSlot unification.** Both PAL WHAMI sites
  (CSERVE$WHAMI, MFPR_WHAMI) read c.cpu->cpuSlot; removed the dormant mis-typed
  mCpuId/cpuId()/setCpuId(); kCpuStateVersion 8->9 (POD blob shrank; pre-v9 snaps
  rejected). cpuSlot is now the SINGLE which-CPU source. (Corrected a T4 note:
  cpuSlot IS serialized -- Snapshot.cpp:122 writes the whole CpuState as a POD blob.)
- **P2-T6 (committed): dispatcher is the SOLE run path; Phase 2 CLOSED.** Deleted
  the EMULATR_DISPATCH env gate + the legacy Machine::run loop. phase1_dispatch_
  gate.sh is RETIRED (no legacy oracle to diff); determinism_equivalence (schedLib)
  is the sole acceptance gate now.
- **NEXT = PHASE 3: LL/SC cross-CPU interlock (THE cliff).** Per-CPU lock_flag/
  lock_physical_address; any store by any agent clears other CPUs' flag on that
  granule; real LockArbiter backing; a non-negotiable contention micro-test; never
  yield MID-INSTRUCTION but interleave at the LL/SC boundary.  See the ledger's
  Phase-2-CLOSED handoff + journals/20260618_smp_secondary_cpu_bringup_design.md.
- **Open investigation: map the primary-CPU HWRPB** (Phase-5 SMP-hook prereq) by
  profiling the PA write-set during primary-CPU SRM init, identifying the HWRPB by
  its self-describing header (self-PA + checksum + offset table) not by size; see
  `journals/20260620_smp_levers_and_hrm_mp_wiring_map.md` (also: 3-lever
  disambiguation + AARM 27.4 cold-boot 10-step spine).
- **Caution recurred:** the bash mount served a STALE-TRUNCATED phantom of
  Machine.cpp/DecListingSink.cpp mid-verify; host Read/Grep is ground truth
  (the known D: mount hazard). Builds + git are client-side.

---

## 2026-06-19 -- SMP harness validated + AlphaCpuAgent Phase 1 + EMULATR_PLATFORM lever

**Headline: the deterministic CPU-scheduling scaffold is wired and validated;
the real CPU now steps behind it (single agent); a real 2nd CPU is still
deferred.** This is the live frontier and supersedes the 06-17 ES40 "next."

- **SMP harness GREEN (the new fixed point).** `schedLib/` doctest
  `smp_harness_tests.cpp` is in `EMULATR_TEST_SOURCES`:
  `determinism_equivalence` passes -- **Sequential == Threaded, bit-identical
  (1666 lock grants identical across 5 runs)** -- plus `parked_agent_no_deadlock`
  and `lock_arbiter_semantics`. The harness is now the verification fixed point:
  a future determinism failure is the NEW AGENT's bug, not the harness's.
- **AlphaCpuAgent Phase 1 -- step 1 GATE-PROVEN.** `Machine::run`'s per-cycle
  loop body was lifted verbatim into a re-entrant `Machine::stepCycle(uint64_t i)`
  (`systemLib/Machine.{h,cpp}`); proven a PURE relocation -- post-extraction DS20
  cold boot is byte-identical to the pre-extraction baseline (only PuTTY's own
  log-header timestamp differs). `stopSentinel` promoted to `m_stopSentinel`.
- **AlphaCpuAgent wired live behind a flag.** `schedLib/AlphaCpuAgent.{h,cpp}`
  (a THIN scheduling adapter: `step(q)` calls `stepCycle` up to q times; CpuState
  REFERENCED through the bound Machine, not owned). The dispatcher path is gated
  in `Machine.cpp:1017` by `EMULATR_DISPATCH` (unset => legacy loop = default).
  One `AlphaCpuAgent` (cpuId 0) under `SequentialDriver`. **Step-3 gate PASSED
  (2026-06-19):** `tools/phase1_dispatch_gate.sh ds20 0x40000000` -- dispatcher
  boot byte-identical to legacy (every `cyc=`/`pc=`/`fault=` matches; only
  wall-clock millis + the ephemeral TCP client port differ, both normalized).
  **Phase 1 is CLOSED.** Next = Phase 2 CpuState ownership-lift.
- **P2 / EMULATR_PLATFORM lever LANDED** (`pipelineLib/MemDrainer.h`,
  `applyLoadEffect`): replaces the temporary `EMULATR_CPU1_ALIVE` hack. Unset or
  `isp` => read-intercept PA `0xBFFC` -> return `0xCAFEBEEF` -> firmware resolves
  `ISP_MODEL` -> reaches `>>>` (out-of-box default unchanged). `=silicon` => no
  intercept -> `REAL_HW` (the REAL_HW-readiness probe path). Read-intercept, NOT
  a deposit (offset 0x3FFC is overwritten by the self-decompressor).
- **Phase 1 contract / staging seams:** single agent => `step()` touches shared
  memory/MMIO DIRECTLY (no Effect-staging); with no concurrent observer, direct
  vs staged-then-applied are observationally identical so equivalence holds
  trivially. The staging SEAMS (LL/SC retire, store commit, IPI write) live in
  the delegated kernel and run the direct op today; Phase 3 inserts staging there
  when a 2nd agent can observe a mid-quantum write.
- **2nd real CPU = explicitly DEFERRED** (Q2 decision, gap journal): starting a
  real CPU1 now is out of order (rendezvous responder = Phase 5, gated on
  Phases 1-3) and the wrong goal for the live frontier (single-CPU `>>>` then OS
  boot, both uniprocessor). Phase 2 = lift `CpuState` ownership INTO the agent
  (the real prerequisite for CPU1) -- discuss-before-code, not started.
- **Open cleanups (P0):** delete the two `$<$<CONFIG:...>:/EHsc->` lines in
  `CMakeLists.txt` (~689-690) and wire a `CpuState` into `tests/fBoxLib/
  test_float.cpp`'s `ExecCtx{}` (FP grains went FPCR-aware -> bare ctx derefs
  `c.cpu==nullptr`) for a fully-green `Emulatr_tests`.
- **House-keeping:** builds + git are CLIENT-SIDE (sandbox can't touch `.git`);
  D: mount can serve stale cached copies -- validate via the Read (host) view.
  The Titan/ALi (06-16/17) chipset commit is still separate/pending.
- **Remaining deliverables map** for this actionable: see the new SMP section in
  this file's deferred list + `journals/20260618_smp_secondary_cpu_bringup_design.md`
  (Phases 0-6) and `journals/20260619_next_steps_alphacpuagent_gap.md` (P0-P3).

---

## 2026-06-18 -- Cchip IPI delivery + DS20 root cause (ISP-model flag) + SMP scaffold staged

- **Cchip IPI delivery wired** (`chipsetLib/TsunamiCchip.h`, `Machine.cpp`):
  the previously-TODO `IPREQ -> IPINTR -> b_irq<3>` path is live. Per-CPU
  `m_pendingIrq3` latch; IPREQ->IPINTR fold into the `miscWriteW1C` CAS loop
  (skips the ABT/ABW gate); `b_irq<3>` bounded by `m_cpuCount`. `Machine::run`
  IPI divert selects IER bit 36 (interprocessor EIEN) -> `EI[3]`. Values vs
  PC264 **OSF** PAL (`ev6_osf_pc264_pal.mar`): IRQ_IP=8, EIEN bit 33 -> bit 36.
  (Re-confirm against the VMS console PAL -- see below.)
- **DS20 cold-boot root-caused** (`pipelineLib/MemDrainer.h`): the busy-poll on
  PA `0xBFFC` for `0xCAFEBEEF` is the firmware's **ISP-MODEL (pre-silicon sim)
  detection flag** per apisrm `pc264.c platform()` -- **NOT a secondary-CPU
  rendezvous**. No writer in the image (harness-deposited). This RESOLVES the
  open "is the 0xBFFC poll a CPU1 rendezvous?" question: it is not, so the SMP
  work (Goal B) does NOT touch the DS20 boot hang (Goal A = claim ISP_MODEL).
  DS20 still idles at "wall-2" (empty event/participation queue), not `>>>`.
- **PAL personality wrinkle:** RSCC/CALL_PAL decode shows the CONSOLE image runs
  the **VMS** personality (`ev6_vms_callpal.mar`, `PAL_FUNC__RSCC=^x9D`), not the
  assumed OSF/Tru64. This determines PCS slot layout, comm-area format, WHAMI,
  and IPL numbers -- reconcile OSF-vs-VMS before trusting Phase 5 layout / the
  IPI IPL constants (the IPI work used the OSF PC264 table).
- **SMP scaffold staged** (`schedLib/`): header-only, Qt-free C++20 -- `IAgent`,
  `Dispatcher` (logical clock + deterministic `syncPhase`), swappable
  `IExecutionDriver` (`SequentialDriver` deterministic oracle + `ThreadedDriver`
  `std::barrier`-synced), `LockArbiter`, `MockCpuAgent`. (Validated green the
  next day -- see 06-19. NOTE: `schedLib/README.md` was written "NOT IN THE
  BUILD"; it has since been added to `EMULATR_TEST_SOURCES`.)

---

## 2026-06-17 -- ES40 runnable: ALi M1543C south bridge + model-gated wiring

**Headline: created the south-bridge interface needed to run ES40 firmware.**
ES40 is Tsunami (no new chipset), but its south bridge is the **ALi M1543C**,
NOT the Cypress CY82C693 (DS10/DS20). Built the ALi model + a model-gated
selection so the working DS10/DS20 path is untouched.

- **`chipsetLib/AliM1543C.h`** (NEW, g++ compile+run verified): func0 PCI-ISA
  bridge (vendor 0x10B9, device 0x1533, class 0x060100, header 0x80) +
  `AliPciFunctionStub` for companions (M5229 IDE 0x5229 / M5237 USB / M7101
  PMU). Mirrors the Cy82C693IsaBridge contract (IPciDeviceHandler +
  IIoPortHandler, 256B config, RO-protected IDs, store-through). Ported from
  `Processor Support/ALi M1543_Datasheet.{pdf,txt}` (now indexed). Faithful:
  identity/enumeration/config store-through. STUBBED: IRQ-steering field
  effects (idx 0x48-0x4B PIRQ), companion internals.
- **Model-gated south-bridge wiring** in `TsunamiChipset::wireDevices()`:
  `isAliPlatform(model)` (ES40/ES45/DS25) -> ALi, else Cypress (default). Added
  `AliM1543C m_ali` member + include. DS10/DS20 path byte-identical.
- **`Machine.cpp:317`** now constructs the chipset from the ini model string
  (`m_chipset(m_settings.system.model, m_settings.system.cpuCount, memSize)`)
  via the existing model-string ctor -- this populates m_model so the gate
  works. DS10 -> Tsunami/1: identical to before. **VERIFY: build DS10 first to
  confirm no regression before the ES40 run.**
- **Platform manifests** (2026-06-16): es40/ds20 `.json`+`.win` (Tsunami,
  runnable), ds25/es45 `.json` (Titan-staged). es40_platform uses the Cypress
  as a STAND-IN comment but the gate now wires the real ALi when model=ES40.
- **CMake**: AliM1543C.h added to sources; es40_v7_3.exe in firmware copy list;
  es40/ds20 .win manifests deployed.
- **REFERENCE_INDEX §3.1**: ALi M1543C datasheet + the LFU `<platform>_diskN`
  option-card firmware trees (KZPSA/KGPSA/DEFPA/KZPDC etc. -- gold for the
  deferred PCI-enum work; `*fw.txt` give device->revision maps).
- **Found-file note:** ds10srm.sys (2006) != palcode-ds10.rom (2005): same
  size, different SHA = two DS10 SRM builds.
- **ES40 run recipe:** EmulatrV4.ini [System] model=ES40, cpuCount=1; [ROM]
  firmwareImage=firmware/es40_v7_3.exe. Watch HALTPROBE/smir. KNOWN RISKS the
  run will expose: ALi config-init divergence (only identity is faithful so
  far), Pchip1/dual-hose (ES40 is dual-Pchip; EmulatR mirrors all-ones),
  system-type DS10-tuning, and the halt source possibly being RMC/OCP not smir.
- Authored without a local MSVC/Qt compile (AliM1543C.h verified standalone).
- **H&M docs:** rewrote 7 Help&amp;Manual topics (H&amp;M/HMDocs/Topics) as properly
  formatted, cross-linked reference pages: Chipset, SouthBridge, Cypress,
  CY82C693, Tsunami/Typhoon, DECchip 21274 (Titan), ALi M1543C. (The ALi/Titan
  topics had been raw-markdown dumps; now styled with headings/tables/See-Also.)
- **NEXT (2026-06-17/18):** begin ES40 testing. Recipe: EmulatrV4.ini
  [System] model=ES40 cpuCount=1; [ROM] firmwareImage=firmware/es40_v7_3.exe.
  Build+smoke-test DS10 FIRST (shared wireDevices/Machine.cpp ctor changed).
  Watch HALTPROBE/smir. Most-likely first divergence = ALi config/IRQ-routing
  init (only identity faithful so far), ahead of the halt check.
- **Session env note:** the Linux bash mount served STALE/truncated copies of
  Write-tool-edited files all session (TsunamiVariant.h, H&amp;M topics). The
  Read tool (Windows side) is ground truth and confirmed every file correct on
  disk. If a future session sees a git/compile mismatch, suspect mount lag, not
  the files.

---

## 2026-06-16 -- Titan (21274) chipset interface + adjacent implementation

**Headline: identified the ES45/DS25/DS15 chipset, corrected a taxonomy bug,
and landed an adjacent Titan model reusing the Tsunami sub-chips.** Prompted by
profiling the v7.3 firmware images + the found `palcode-ds10.rom` (full DS10 SRM
console, not bare PALcode) and `pc264srm.sys` (PC264/DS20 SRM) -- both decompress
with the existing `tools/host_decompressor` oracle (byte-identical es45 check).
See `Analysis/firmware_v7_3_profile/` and journal
`journals/20260616_titan_21274_interface.md` (flagged for H&M incorporation).

- **Taxonomy fix:** 21272 = Tsunami (Typhoon = its high-bw variant, NOT a
  separate part); **21274 = Titan** (DS15/DS25/ES45). `TsunamiVariant.h` had
  Typhoon mislabelled "21274" with DS25/ES45 mapped to it. Corrected:
  `ChipsetVariant::Titan` added; `variantFromModel` DS15/DS25/ES45 -> Titan;
  binding test updated. **DS10/Tsunami path untouched.**
- **Key insight:** Titan shares the top-level PA map with the 21272 (Cchip
  0x801_A000_0000, Pchip0 0x801_8000_0000, Pchip1 0x803_8000_0000, hose h<<33)
  and the Cchip/Dchip/TIG offsets (MISC +0x80, DIR0-3 +0x280/2C0/680/6C0 per
  ES45 Appendix D). So Titan **reuses TsunamiCchip/Dchip/Tig unchanged**; the
  only new silicon is the **dual G/A-port PA-chip + AGP** + richer error set.
- **Landed (chipsetLib/):** `Titan21274_CsrSpec.h` (authoritative constants,
  static_assert+g++ verified), `TitanPchip.h` (dual-port PA-chip, compile+run
  verified), `TitanChipset.h` (scaffold composing reused sub-chips). Added to
  CMake. Authoritative source = Linux `core_titan.c/.h` + ES45 EK-ES450-SV
  Appendix D (both now in Processor Support; REFERENCE_INDEX §3.1 updated).
- **STUBBED (explicit TODOs):** `TODO(titan-pci-route)` per-hose PCI MEM/IO/config
  (identical to 21272 -> delegate to Tsunami routing); `TODO(titan-isystembus)`
  TitanChipset is not yet an ISystemBus and lacks the device layer (mirror
  `TsunamiChipset::wireDevices()` 1:1); `TODO-verify` Titan DREV/MISC.REV values.
- **Halt-button note:** chipset-invariant -- Titan SMIR/halt regs share Tsunami
  offsets via the reused TsunamiTig, so the halt-button behavior + the ipcr
  storage-only SMP-IPI gap are identical. Imminent DS20/ES40/PC264 firmware
  testing needs NO Titan; only ES45/DS25/DS15 do.
- **Selection seam:** `Machine.cpp:317` still hardcodes Tsunami; a Titan boot
  needs a branch there + the titan-isystembus completion.
- **Authored without a local MSVC/Qt compile** -- self-contained parts verified
  with g++; TitanChipset needs Tim's build loop. NOTE: the Linux mount served a
  stale truncated `TsunamiVariant.h` to bash during the session; the Read tool
  (Windows side) confirmed the on-disk file is complete/correct.

---

## Current state -- 2026-06-13 (end of session)

**Headline: first `boot` attempt past `>>>` -- halt gate root-caused (smir)
+ faithful TIG-bus model landed.** `b dqa1` was refused "Halt Button is IN,
BOOT NOT POSSIBLE". Pinned the gate to **smir = TIG+0x40** and built a faithful
TIG-bus device model. Pending build + boot validation; the FP/VAX-float wall
(#43) is the expected next blocker.

**Landings:**
- **OpenVMS wired:** dqa1 ATAPI CD -> `D:\isos\alpha082.iso` (OpenVMS Alpha
  V8.2) in `ds10_platform.win`.
- **Memory fix LIVE:** banner `1024 Meg`; the 64->1024 SSOT fix is engaged
  (top open thread closed). **dqa0 now enumerates** (`show dev` lists DQA0).
- **Firmware path fix:** `[ROM] firmwareImage` -> `firmware/ds10_v7_3.exe`
  (POST_BUILD deploys to firmware/; ini had the bare name). Instance of #42.
- **Halt gate (the hunt):** first fix (per-CPU halt-IPI 0x3C0/0x5C0 -> 0) was
  the WRONG register. Built console-armed trace tooling -- kTigTraceArmReg
  (`e pmem:80130000FF8` arms the retire window from the prompt) + auto
  HALTPROBE + main.cpp `EMULATR_TRACE_WINDOW` window-only sink -- which pinned
  `HALTPROBE: TIG read pa=0x80130000040 v=0xffffffff` = **smir**. EmulatR
  modeled NO TIG-bus device registers; smir fell to the all-ones default ->
  firmware saw "Halt IN". (The apisrm-source `pal$halt_switch_in` impure-flag
  and EI[4]-interrupt theories were dead-ends; the V7.3-2 binary reads HW smir.)
- **Faithful TIG-bus model:** new `chipsetLib/TsunamiTig.h` (clean-room from
  DEC sources: tsunami_io.c xtig, pc264*.c intig/outtig, EV6_OSF_PC264_PAL.MAR,
  regatta logout tig_smir; AXPBox cross-check only). smir status-only
  read-0/no-store; halt/ipcr/arb_ctrl R/W; rev regs read-0 _PROVISIONAL
  (display-only, XREF-confirmed); catch-all 0/absorb gated by
  `EMULATR_TIG_TRACE` (bring-up canary). Snapshot deferred but assert-guarded
  (`TsunamiTig::isAtResetState()` + warn). Wired into TsunamiChipset (m_tig);
  doctest updated. ipcr documented storage-only (no IPI -> SMP secondary stall).
- **Tooling persistence:** run helpers moved to source `tools/` (trace_halt.sh,
  launch_vms_boot.sh, build_test_run.sh), anchored to run-dir root (../), added
  to CMake `EMULATR_TOOL_FILES` POST_BUILD -> deployed to run-dir/tools/ so a
  cmake regen no longer wipes them.

**Open tasks:** #5 halt fix (close when boot validated past gate); #10 gate
C970-LOADWATCH/STOREWATCH/DIVERT-REI MISMATCH/PCSAMPLE behind env (cached
static bool); #11 faithful TIG model DONE; #43 (below) FP build-out = next wall.

**Next session:** build -> chipset suite (TIG subcase) ->
`EMULATR_TIG_TRACE=1 ./tools/trace_halt.sh`. At >>>: `e pmem:80130000040`
should read 0; `b dqa1` should cross into the VMS bootstrap (capture where VMS
walls -- expect FP). Full writeup:
`EmulatRAppUniV4/Emulatr/journals/20260613_EOD_handoff_tig_smir_faithful_model.md`
and `journals/20260613_halt_switch_tig_register.md`.

---

## Current state -- 2026-06-12 (end of session)

**Latest (pm) — storage-media seam + enabled flag.** Landed the
IBlockMedia byte-sourcing seam (Phase A, #33, APPROVED): the ATA disk and
ATAPI CD no longer open files — both route reads/writes through an
`IBlockMedia` (`MediaStatus` enum). New files: `deviceLib/scsi/
IBlockMedia.h`, `FileBlockMedia.h` (512 RW disk / 2048 RO ISO),
`BlockMediaFactory.h` (media_kind: `image|iso`→FileBlockMedia,
`host`→Phase B, unknown→**FAIL CLOSED**), `tests/deviceLib/
MockBlockMedia.h`. Drives refactored (VirtualIsoDevice + Cy82C693Ide hold
`unique_ptr<IBlockMedia>` → **move-only**; chipset `setDiskMedia`/
`setCdMedia`). ATAPI READ path (#31) implemented: READ CAPACITY(10),
READ(6/10/12), READ TOC, single 2048-byte burst (multi-block = trace-
gated **#32**). Manifest now OS-suffixed (#35): `ds10_platform.win` /
`.linux`, deployed by the CMake POST_BUILD copy (fixed). Per-target
**`enabled` flag** — absent = enabled; a disabled ALTERNATE may share a
channel/unit (dqa1 = Tru64 ISO enabled + a disabled host-passthrough
alternate on the same `(0,1)` slot). Also added **create_if_missing**
(auto-provisions a blank sparse image of `size` on first boot, never
overwrites, never for RO), the `tools/mkdisk.py` generator +
`config/disk_types.json` catalog + extended `dec_disk_media_types.tsv`
(storage-only VMS/SRM prefixes), all POST_BUILD-copied into the run dir.

**PROVEN end-to-end at runtime (19:29).** Cold boot loads the manifest
clean and emits `Storage: created blank disk image 'dqa0.img'
(4294967296 bytes)` → `attached ATA disk 'dqa0.img' to IDE ch0 unit0`
and `attached ATAPI CD 'D:\isos\tru64v5.iso' to IDE ch0 unit1` (once the
real ISO was at that path; factory had fail-closed while absent). Tests:
**454 / 5999 / 0**. Next gate: the dq boot ACCEPTANCE trace — `boot
dqa1` (Tru64 installer) → installs onto dqa0.img → `boot dqa0`;
synchronous IIoPortHandler path retained (no IoSeam migration). **#34
HostOpticalMedia** (Win `\\.\X:` / Linux `/dev/sr0`, `host:N` resolver)
is the Phase B passthrough drop-in. Writeups:
`journals/20260612_EOD_handoff_iblockmedia_storage_media.md`,
`journals/CHECKPOINT_2026-06-12.md`.

**QUEUED — boot-to-OS + PCI + deployment (tickets #37–#42, 2026-06-12).**
Source: `tasks_20260612_boot_pci_deploy.md` (dq-boot + PCI + docs planning
session). **TOMORROW: begin discussions + scaffold the PCI work (#41).**
Ticket scheme continues from #36 (CSERVE). Two trace-gated planning tracks
already opened in the live tracker (A0–A3 boot path; B1–B3 + #7 PCI); the
B-track items below are the same PCI work, now framed as **#41 = "PCI
COMPLETE"** because the SRM console probe was enough to *enumerate* dqa but
VMB/SYSBOOT and OS adapter-init traverse PCI deeper than what's written.

- **#37 H&M doc — Create Disk.** Document `create_if_missing`,
  `tools/mkdisk.py`, the `disk_types.json` / `dec_disk_media_types.tsv`
  catalog, bare sparse-image semantics (no partition table / disklabel —
  guest install writes those), cross-platform sparseness. USER (or DEV)
  hive. Deliverable `Create_Disk.xml` — STATUS: draft for review.
- **#38 H&M doc — Boot Disk.** SRM `boot` cmd, device names
  (dqa0/dqa1/dka0/ewa0), env vars (`bootdef_dev`, `boot_osflags`,
  `auto_action`), boot flags (VMS `-fl root,flags`; conversational bit 1 →
  SYSBOOT>), VMB→SYSBOOT.EXE→SYSBOOT> flow, install-from-CD example. Tru64
  flags [CONFIRM]. Deliverable `Boot_Disk.xml` — STATUS: draft for review.
- **#39 SYSBOOT> scaffold (OpenVMS conversational boot).** `boot dqa0
  -fl 0,1` must reach `SYSBOOT>` + SYSGEN SET/SHOW/CONTINUE. **HARD DEP on
  #41** (VMB/SYSBOOT/early-exec traverse PCI to reach the boot device) and
  on **#32** (SYSBOOT.EXE is many blocks). Accept: cold trace reaches
  `SYSBOOT>` from clean `>>>` and accepts SET/CONTINUE.
- **#40 Tru64 boot-gate investigation (parallel).** Pin the OSF/1 analog
  of the SYSBOOT> gate (interactive/single-user boot flags + kernel PCI/
  device-config path). Tru64 has no SYSGEN prompt; analog = interactive
  boot to single-user shell. Output: short note pinning flags + device-
  config dep; feeds #41 acceptance. No code until the gate is pinned.
- **#41 PCI interface — COMPLETE (blocks #39).** Trace-first, NOT yet
  written to the depth the OS bootstrap needs:
    (a) `EMULATR_PCI_CFG_TRACE` capture (BAR-sizing 0xFFFFFFFF probes,
        read-back, final BAR-assign writes per device) BEFORE rebind code
        — hook already exists `TsunamiPchip.h:1113-1132`;
    (b) `IPciDevice` config/BAR seam (sibling of IBlockMedia): uniform
        config-space header + BAR-decode decl (Cypress IDE = legacy no
        relocatable BARs; DE500 = 2 BARs io 0x80 / mem 0x80);
    (c) **S2 dynamic BAR→range rebind** (the known gap): a BAR write must
        re-point bus routing; static registration bypasses it today
        (`TsunamiPchip.h:413/449`). Match SRM's exact write sequence;
    (d) **#7 DE500 tulip** enumerates with BARs assigned, GCT entry correct
        and does not perturb the GCT the boot path depends on.
  Accept: a BAR write observably rebinds decode (doctest + trace); DE500
  enumerates; OS bootstrap's PCI traversal reaches the disk.
- **#42 Run-dir rooting + staging (Setup Factory 9 prep).** Everything the
  running program touches is staged below the run dir and resolved relative
  to the executable — nothing absolute or install-time. AUDIT all
  files/facilities (platform manifests `.win`/`.linux`, `disk_types.json`,
  platform JSON, `dec_disk_media_types.tsv`, firmware `ds10_v7_3.exe`, ini
  `EmulatrV4.ini`, disk-image dir, snapshots dir, `mkdisk.py`+catalog, Qt
  runtime: platform plugin + `qt.conf` + DLLs/.so). RESOLVE every path via
  one `resolveRuntimePath(relative)` helper anchored to
  `QCoreApplication::applicationDirPath()` (never CWD, never hardcoded);
  all QSettings refs flow through it. STAGE via extended CMake POST_BUILD +
  emit a staged-file inventory (the manifest Setup Factory 9 packages).
  FAIL LOUD on a missing dep at startup (same discipline as media-factory
  fail-closed). Accept: app runs from a copied run dir on a clean machine
  (no source tree, arbitrary CWD), Windows + Mint; inventory matches what
  the app actually opens (no unlisted dependency).

**Earlier (am) — memory size fix.** DS10 `64 Meg` → `1024 Meg of system
memory`. Root cause was an **SSOT value-plumbing bug**, not a memory-
model defect: `main.cpp` built `Machine` with `opts.memSize` (the
`--mem` CLI default, 64 MiB) instead of the ini's `[System] memorySize`
(1 GiB). `computeAAR` was already HRM-correct; it just got the wrong
input. `64M → asiz 0x3 → AAR0 0x3009`; `1 GiB → asiz 0x7 → 0x7009`.

**Fix (coded):** `AppOptions.h` adds `bool memSizeSet`; `AppOptions.cpp`
sets it in the `--mem` arm; `main.cpp` uses
`settings.system.memorySizeBytes` when `--mem` wasn't given (CLI > ini
preserved). Engage proof on stderr: `memory: using [System] memorySize
from ini: 1073741824 bytes`.

**M2 (landed):** consistency doctest in `test_ticket02_aar_encoding.cpp`
round-trips the AAR total via the firmware's own formula
(`2^(ASIZ+3) MB`, `&7` mask, from `memconfig_pc264.c`); asserts
64 MB→`0x3009`, 1 GiB→`0x7009`. **M3 (deferred):** platform mem-ctrl
sub-block / fail-closed Typhoon-Titan — needs the not-yet-built platform
identity record; Cowork task #10.

**OPEN — confirm live (do first):** the last RelWithDebInfo run still
printed `64 Meg` (stale binary / fallback not engaged). Cold-boot with
`EMULATR_NO_AUTOLOAD=1` (size is set at chipset reset; snapshots won't
show it) and verify the stderr ini line, the `1024 Meg` banner, and
`show config`/`show memory` → `Array 0: 1024 MB`, `AAR0=0x7009`. Cheap
pre-check: run the test suite for the M2 cases. After confirming,
**re-mint snapshots** (old `coldgct`/`oemsnap` have 64 MB baked in).
`show memory` on DS10 needs NO SPD modeling (purely AAR-derived).

**Also this day (IDE/S4):** CY82C693 func1 enumerates (vendor 0x1080 /
dev 0xC693), legacy 0x1F0/0x170 route to `m_ide` (no Cypress shadow),
407 suite green; cold boot reached `>>>`, `update srm`→7.3-1 OK,
`sys_serial_num=test123` persisted. Pending at `>>>`: `show device` for
`dqa0` + boot-time metric.

**Full writeup:** `EmulatRAppUniV4/Emulatr/journals/20260612_EOD_handoff_memory_size_64_to_1024.md`

> NOTE: this file had drifted — its prior "Current state" was 2026-05-11.
> The intervening work (≈May 12 – June 12) lives in dated journals under
> `EmulatRAppUniV4/Emulatr/journals/`; the most substantial recent EOD
> handoffs are `20260607_EOD_handoff_S4_ide_dqa0_probe_gap.md` and
> `20260608_EOD_handoff_bootspeed_superio_ssot.md`. Consult those plus
> today's for the current chipset/boot/IDE state.

---

## Earlier state -- 2026-05-11 (end of session)

**Headline:** Three landings in one day on the chase from the MTPR
fix to the first real chipset-touch breakpoint.

1. **HW_CC two-counter split.**  The MTPR fix unmasked a latent
   conflation: `execHwMtpr HW_CC` was writing the sim-side
   `cpu.cycleCount` (the per-retire trace counter) instead of the
   architectural CC IPR.  Pre-fix it was invisible because every MTPR
   wrote zero.  Post-fix, guest writes to HW_CC slammed the trace
   counter back near zero mid-run -- explaining the bizarre
   `cycleCount=0x133` observed at PC `0xdbcc` after 178M+ retires.
   Split into two independent counters: pipeline counter is sim-only,
   architectural CC is `cpu.ccOffset` + writable.  Landed in
   `coreLib/CpuState.h` plus the `execHwMfpr/execHwMtpr HW_CC` arms.

2. **UNALIGN emulation R20-zero pattern identified (fix deferred).**
   With the MTPR fix in place, the post-fix run reaches the OSF/1
   PALcode UNALIGN emulation routine at PC `0xdb64..0xdbcc`, which
   takes an unaligned HW_LD at PC `0x12704`, dispatches through the
   UNALIGN vector at palBase+0x280, then BR's to the emulation body.
   At cycle 303 it loads `R20 = 0x600000` (palBase) via HW_LD; at
   cycle 305 the AND at PC `0xdbc4` zeroes R20; the STQ at PC `0xdbcc`
   then stores zero to `R20 + sign_extend_16(-64)` = `0x7ffffffffc0`
   (top-of-PA), which Tsunami claims as default sink.  Two open
   questions before a fix: decode the instruction encoding at `0xdbc4`
   and `0xdbcc` to confirm Rb=R20 and what the AND is masking.  Memo
   in `spaces/.../memory/project_unaligned_emulation_top_of_pa.md`.

3. **Level 1 snapshot save/restore LANDED.**  Boot-safe snapshot
   subsystem implemented end-to-end with mandatory roundtrip test
   hive (V1's lack of one was the explicit motivation).  Captures
   `CpuState`, `GuestMemory` bytes, Tsunami Cchip/Dchip/Pchip CSR
   storage (incl. atomic DIM/IIC/DRIR), and SRM firmware staging
   (descriptor + payload bytes + load PA + relocation one-shot).
   Auto-saves every 10M cycles to `./snapshots/auto_<ts>_<cyc>.axpsnap`
   plus one `auto_halt_*.axpsnap` on any halt; auto-loads newest
   `*.axpsnap` at startup.  `Machine::run()` now loops on `step()` to
   inject the save hooks; test-fixture path disables auto-save via
   `Machine::setAutoSnapshotEnabled(false)`.  Motivation: 5h to reach
   the HW_CC bug today + many weeks of downstream bugs ahead -- the
   per-restore payoff begins immediately.  Files: `systemLib/Snapshot.h`,
   `systemLib/Snapshot.cpp`, `tests/systemLib/test_snapshot_roundtrip.cpp`;
   chipset accessors on the three sub-chip headers; data() accessors
   on `GuestMemory`; restore hook and snapshot config on `Machine`.

**Status of next run:** awaiting fresh build with all three landings.
The UNALIGN R20-zero will likely still fire (no fix yet), but auto-save
on halt will now capture the breakpoint state for instant restart.

**Hope for tomorrow:** decode the instruction word at `0xdbc4` and
`0xdbcc` to identify Rb's actual register, then either fix the
PALcode reading path or recognize the AND as intentional and find
the real Rb's last writer.  With Level 1 snapshots, each iteration
becomes milliseconds instead of minutes.

**Full writeups:** `EmulatRAppUniV4/Emulatr/journals/2026-05-10_palBase_anomaly.md`
(yesterday's MTPR fix);
`EmulatRAppUniV4/Emulatr/journals/Snapshots_Design_Notes.md`
(snapshot design memo, updated to reflect Level 1 landed).

---

## Recent journals (most recent first)

| Date | File | Topic |
| ---- | ---- | ----- |
| 2026-06-20 | `journals/20260620_smp_levers_and_hrm_mp_wiring_map.md` | SMP lever disambiguation (EMULATR_DISPATCH / IExecutionDriver / EMULATR_PLATFORM) + AARM 27.8.1.1 MP-boot wiring map -> Phase 3/4/5 checklist (new: RXRDY/TXRDY per-CPU comm area, PE/BB_WATCH); why CPU1 is structurally unbootable today |
| 2026-06-19 | `journals/20260619_phase2_task_ledger.md` | Phase 2 OPEN TASK LEDGER (durable): P2-T1 finish STEP 1b sink emit + re-baseline; T2 stepCycle split; T3 clock decouple + global retire ordinal; T4 CpuState ownership + cpuSlot from id(); T5 whami-cpuid reconciliation; T6 STEP 5 flip+delete. Read FIRST when resuming Phase 2 |
| 2026-06-19 | `journals/20260619_alphacpuagent_phase2_ownership_lift_design.md` | Phase 2 DESIGN (discuss-before-code): split stepCycle into cpuKernel(cpu)+systemTick(now); re-home L1/L2 + flash/snapshot cadence off m_cpu.cycleCount to a system LogicalClock; CpuState ownership into agent; STEP 1-4 each gated by phase1_dispatch_gate.sh |
| 2026-06-19 | `journals/20260619_phase2_logging_policy_addendum.md` | Phase 2 logging addendum (web review, REVIEWED+ACCEPTED w/ reconciliation): single dispatcher-owned trace + spdlog sinks, per-CPU = DATA tag (cpuId) not destination; STEP 1 adds cpuId tag only, global retire ordinal deferred to STEP 2-3; dispatch gate needs no re-baseline (tag cancels) |
| 2026-06-19 | `journals/20260619_alphacpuagent_phase1_design.md` | Phase 1 design + COMPLETE record (relocation gate-proven; agent live behind EMULATR_DISPATCH; step-3 byte-identical) |
| 2026-06-19 | `journals/20260619_next_steps_alphacpuagent_gap.md` | P0-P3 plan + AlphaCpuAgent integration gap (re-entrant stepper, per-CPU vs shared split, effect completeness, LL/SC FSM, MMIO); Q2 "real CPU1 today? NO" |
| 2026-06-19 | `journals/20260619_alphacpuagent_phase1_design.md` | AlphaCpuAgent Phase 1 design (quantum==retire; landmines L1 bindCycleSource ptr, L2 interval-timer fire-edge); the CpuState-ownership Phase 2 lift |
| 2026-06-19 | `journals/CHECKPOINT_2026-06-19.md` | Day log: SMP harness validated; EMULATR_PLATFORM lever; stepCycle relocation gate-proven; agent wired behind EMULATR_DISPATCH; commit handed off |
| 2026-06-18 | `journals/20260618_smp_secondary_cpu_bringup_design.md` | Phased CPU1 bring-up design (Phases 0-6 + prerequisites P1-P4); chipset ~80% SMP-ready; Phase 3 LL/SC interlock = the cliff |
| 2026-06-18 | `journals/20260618_ds20_root_cause_cpu1_rendezvous.md` | DS20 0xBFFC poll = ISP-model flag (not rendezvous); Goal A vs Goal B split |
| 2026-06-18 | `journals/20260618_cchip_ipi_wiring_design.md` | Cchip IPREQ->IPINTR->b_irq<3> IPI delivery wiring |
| 2026-06-18 | `journals/20260618_ds20_first_boot_console_idle.md` | DS20 first boot reaches console-idle / wall-2 |
| 2026-06-13 (EOD) | `journals/20260613_EOD_handoff_tig_smir_faithful_model.md` | First `boot` past >>> attempted; halt gate = smir (TIG+0x40); faithful TsunamiTig model; console-armed trace tooling; OpenVMS V8.2 wired; firmware-path fix; pending boot validation -> FP wall |
| 2026-06-13 | `journals/20260613_halt_switch_tig_register.md` | Halt-switch boot-refusal investigation: ruled out pal$halt_switch_in/EI[4]; HALTPROBE pinned smir; TIG-bus model fix + AXPBox cross-check |
| 2026-06-12 (pm) | `journals/20260612_EOD_handoff_iblockmedia_storage_media.md` | IBlockMedia seam (Phase A #33); ATAPI READ path (#31); FileBlockMedia/MockBlockMedia/factory; `.win`/`.linux` manifest (#35); per-target `enabled` flag; 449 green |
| 2026-06-12 | `journals/20260612_dq_ew_driver_requirements_review.md` | dq_driver/ew_driver SRM-driver support requirements (IDE ~90% done; DE500/21143 tulip = config stub, needs Dc21143Tulip #28) |
| 2026-06-12 (am) | `journals/20260612_EOD_handoff_memory_size_64_to_1024.md` | 64→1024 MB SSOT plumbing fix; M2 AAR-consistency doctest; M3 deferred; live confirmation pending |
| 2026-06-08 | `journals/20260608_EOD_handoff_bootspeed_superio_ssot.md` | dqa0 confirmed; memory_test=none (39 min gone); SuperIO diagnosed; SSOT slices A/B wired |
| 2026-06-07 | `journals/20260607_EOD_handoff_S4_ide_dqa0_probe_gap.md` | S4 IDE / dqa0 probe gap |
| (May 12 – Jun 6) | `journals/` (many dated files) | gap not individually indexed here — see the journals directory |
| 2026-05-11 | `journals/Snapshots_Design_Notes.md` (updated) | Level 1 snapshot landed; HW_CC two-counter split; UNALIGN emulation R20-zero pattern identified |
| 2026-05-10 | `journals/2026-05-10_palBase_anomaly.md` | HW_MTPR Ra/Rb bug root cause + fix + PAL_TEMP extension |
| 2026-05-09 | (no journal; in-session notes in `journals/Snapshots_Design_Notes.md`) | Snapshot design stub deferred until SRM reaches `>>>`; PA->Tsunami wiring landed; 178M-cycle trace captured |
| 2026-05-08 | `journals/2026-05-08.md` | Retire-compact trace stream + `analyze_retire_trace.py` + overnight run setup |
| 2026-05-05 | `journals/2026-05-05.md` | Earlier V4 milestones |

---

## Deferred / planned work (also in project CLAUDE.md)

- **SMP / second-CPU bring-up -- IN PROGRESS (Phase 1), the live frontier.**
  Goal B = two real Alphas under an SMP guest. Two framings, same work:
  the 06-18 design (`journals/20260618_smp_secondary_cpu_bringup_design.md`)
  lays out Phases 0-6; the 06-19 gap journal
  (`journals/20260619_next_steps_alphacpuagent_gap.md`) reframes the near-term
  steps as P0-P3. REMAINING DELIVERABLES:
  - **P0 cleanups (small, do first):** drop the two `/EHsc-` lines in
    `CMakeLists.txt` (~689-690) + wire a `CpuState` into `test_float.cpp`'s
    `ExecCtx` for a fully-green `Emulatr_tests`.
  - **Phase 1 finish:** DONE (2026-06-19). Step-3 gate PASSED via
    `tools/phase1_dispatch_gate.sh` -- dispatcher boot byte-identical to legacy.
    The gate harness + auto-deploy CMake (GLOB tools/*.sh|*.py) also landed.
  - **Phase 2 -- CpuState ownership-lift (the real CPU1 prerequisite):**
    DESIGN DRAFTED 2026-06-19 (`journals/20260619_alphacpuagent_phase2_
    ownership_lift_design.md`), awaiting review. Core: `m_cpu.cycleCount` is
    overloaded as BOTH per-CPU PCC AND the system timebase; split it. Split
    `stepCycle` into `cpuKernel(cpu)` (per-agent) + `systemTick(now)` (once per
    quantum, dispatcher-level) -- this kills the "single-agent-by-shape"
    double-fire and is the real CPU1 prerequisite. Re-home L1 (RTC
    `bindCycleSource` @346), L2 (`intervalTimerShouldFire` @1292/1310), IDLEWARP
    (@1294), flash flush (@1318), snapshot cadence/naming off `m_cpu.cycleCount`
    onto a system `LogicalClock` (already in SmpHarness.h). STEP 1-4: clock-first
    (1-3) then ownership (4), each gated by `phase1_dispatch_gate.sh`.
    **DECISIONS LOCKED (Tim, 2026-06-19):** D-1 policy P-A APPROVED (running CPU
    PCC == system clock; parked frozen; RPCC through running agent) PAIRED with
    D-1a the BINDING INVARIANT (system clock advances by per-step retire cycle
    DELTA, not per-iteration/per-quantum -- "P-A + per-iteration clock" silently
    fails STEP 3). D-2: legacy-loop deletion NOT folded in -- it stays the gate's
    oracle through Phase 2; flip+delete is a SEPARATE post-acceptance one-variable
    commit (STEP 5). CpuState -> agent; GuestMemory/chipset/LockArbiter = shared.
    **STEP 1a APPLIED (2026-06-19, UNBUILT -- client build + gate pending):**
    added `Machine::systemNow()` (returns m_cpu.cycleCount today, pure
    indirection) in Machine.h; routed interval-timer, flash debounce, snapshot
    cadence+naming through it; marked L1 RTC bind + IDLEWARP write seams for
    STEP 3. Tim's call: `m_injectInterruptCycle` compare (@1587, debug arm) ADDED
    to the inventory as L2b and routed through systemNow() in 1a too (dormant ->
    gate can't catch a post-STEP-3 wrong-clock read, so fix now; level-triggered
    >= one-shot => IDLEWARP-safe). STEP 1b (cpuId trace tag in DecListingSink +
    one-time golden/AXPBox re-baseline) HELD until 1a green -- acceptance contract
    captured in the logging addendum; draft strings after (they touch the same
    sink seam 1a refactors). Dispatch gate needs NO re-baseline (tag cancels).
    **STEP 1a GATE PASSED + COMMITTED (2026-06-19):** `phase1_dispatch_gate.sh
    ds20 0x40000000` byte-identical; both paths Stop=MaxCyclesExceeded PC=0x1ad930
    fault=5. systemNow() refactor proven pure.
    **STEP 1b STRUCT+POPULATION APPLIED (2026-06-19, sink emit + re-baseline
    PENDING):** Option A chosen (cpuId()/CpuType is the processor MODEL enum, NOT
    an SMP slot, and dormant -- so a dedicated slot field, not cpuId()). Landed:
    `CpuState.cpuSlot` (uint32_t, default 0); snapshot `kCpuStateVersion` 7->8
    (CpuState POD grew; pre-v8 rejected); `LookbackEntry.cpuId`; `freezeRecord`
    signature gains the slot param (sole compiler-enforced population path) +
    onCommit passes `postCommitCpu.cpuSlot`. e.cpuId is CAPTURED but NOT yet
    EMITTED -> compiles, boot byte-identical. Addendum contract corrected (slot
    from cpuSlot). **OPEN Phase-2 task ledger:
    `journals/20260619_phase2_task_ledger.md`** -- read it for the enumerated
    next steps (STEP 1b sink wiring + re-baseline; STEP 2-5; the whami-cpuid
    reconciliation).
  - **Phase 3 -- LL/SC cross-CPU interlock (THE cliff):** per-CPU
    `lock_flag`/`lock_physical_address`; any store (STx, STx_C, or DMA) by any
    agent clears other CPUs' lock_flag on that granule; LockArbiter gets real
    backing. Dedicated contention micro-test is non-negotiable (both-win =
    corruption; livelock = looks like the current hang). LL/SC split FSM:
    never yield MID-INSTRUCTION (the plan's "never yield between LL and SC" was
    imprecise -- other agents MUST interleave at that boundary to detect
    contention).
  - **Phase 4 -- cross-CPU IPI:** index the `b_irq<3>` divert per-CPU
    (`pendingIrq3(scheduledCpu)`); apply P4-confirmed EI bit/IPL/rank. Reuses
    the landed Cchip IPI work. (Re-confirm constants against the VMS console PAL.)
  - **Phase 5 -- secondary rendezvous responder (GATED on P1/P2/P3 measurements
    + Phases 1-4):** HWRPB PCS slot modeling, per-CPU console comm area,
    presence-detect (platform-manifest-gated), secondary release + check-in.
    Capture the REAL secondary protocol (`start_secondary` @ pc264.c:333 =
    `outtig 0xC00028+id`; `entry.c secondary_start @425`) -- NOT the 0xBFFC ISP
    flag. A partial flag-flip deadlocks mid-protocol.
  - **Phase 6 -- determinism extension:** serialize all N CPU contexts + the
    scheduler interleave cursor for bit-identical snapshot/resume; bump snapshot
    version. AXPBox is suspect as an SMP oracle (verify it executes CPU1 first).
  - **Threaded-path caveats (Phase 3+):** MMIO read mutates shared chipset
    state (read-clears, FIFO pops) -- serialize through the dispatcher or make
    the chipset thread-safe under `ThreadedDriver`; run that path under
    ThreadSanitizer (equivalence proves "same result," TSan proves "not luck").
- **Floating-Point build-out -- GATES OS INSTALL (#43, raised 2026-06-13).**
  The fBox is an IEEE-T-only POC and MUST be completed before any guest OS
  executes native FP -- which is early and constant. Full audit:
  `journals/fBox_FP_Coverage_Map_20260610.md`. State today: only T-format
  arith (ADDT/SUBT/MULT/DIVT, shallow), ordered T-compares, CPYS family,
  FP load/store for all four formats, MT/MF_FPCR (storage only), the FEN
  trio, and FTOIT are real. STUBBED (logs, computes nothing): ITOFS/ITOFT,
  ADDS/SUBS/MULS/DIVS. ABSENT (decode-faults): ALL conversions (CVTQT/
  CVTTQ/CVTQS/CVTTS/CVTST/CVTLQ/CVTQL), ALL VAX float 0x15 (ADDG/SUBG/
  MULG/DIVG + F-set + VAX converts -- the critical OpenVMS gap, since VMS
  uses G_float/F_float by default), SQRT[F/G/S/T], CMPTUN, FCMOVxx, FTOIS,
  and the FP branches FBEQ/FBNE/FBLT/FBLE/FBGE/FBGT. Three depth caveats
  even where IMPLEMENTED: rounding is host round-to-nearest (trap-mode/
  /S/U/I/D bits parsed but ignored), IEEE traps never raised, FPCR is
  storage-faithful but semantics-inert.
  IMPLEMENTATION NOTE: the `coreLib/proposed/` headers are a PARTIAL V1
  port and do NOT compile in V4 (missing fp_variant_core.h /
  alpha_fpcr_core.h, broken SSE deps, circular includes) -- treat them as
  ALGORITHM REFERENCE only; implement leaves NATIVELY against V4 types
  (bit_cast + host op + `<cfenv>` for rounding/traps), per the existing
  `Float.cpp` pattern. Recommended build-out order (from the map):
  (1) conversions first (unblock int<->float for both formats);
  (2) VAX G/F float 0x15 (OpenVMS); (3) promote IEEE single ADDS/SUBS/
  MULS/DIVS from stub; (4) SQRT, CMPTUN, FCMOVxx, ITOFx; (5) real FPCR
  semantics (DYN rounding, trap-mode, IEEE-trap delivery). Items 1-3 are
  the minimum to get a guest OS running; 4-5 are correctness depth stageable
  behind a first cut. Storage/dq-boot work is independent (integer-only to
  "media read + bootstrap loaded"); the install GOAL is what FP gates.
- **Snapshots Level 1 LANDED 2026-05-11.**  Boot-safe save/restore
  (CpuState + memory + chipset CSRs + SRM staging) with auto-save
  every 10M cycles and on halt, autoload-newest at startup.  Design
  memo updated in `journals/Snapshots_Design_Notes.md`.  Level 2
  (cycle-accurate pipeline state: in-flight slots, store queues,
  branch predictors) remains deferred until a debugging need
  actually demands it; Level 1 restart semantics ("next fetch
  boundary") cover everything observed so far.
- **EV5 (21164) emulator profile** — eventual; would need a parallel
  `coreLib/Ev5EntryVectors.h`.  PT0..31 storage already provisioned as
  of 2026-05-10 (V4 is now forward-compatible with EV5-vintage PAL_TEMPs).
- **`S_PalLinux` codegen extension** — `genGrains.py` doesn't yet emit
  `lookupPalLinux()` dispatch.  ~10-line mechanical extension.
- **Task #36: MMIO transaction instrumentation in `TsunamiChipset`** —
  atomic counters + verbose-flag for definitive per-region access counts.
  Worth doing once we know real MMIO traffic is happening (post-fix runs
  should surface it).
- **`HW_PCTX` disambiguator** — currently shadowed by HW_PAL_TEMP_0
  because V4 doesn't decode the function-bit selector that distinguishes
  raw scbd 0x40 PT0 vs HW_PCTX.  Acceptable until PALcode actually
  accesses HW_PCTX directly (rare).

---

## Project quick-reference

| Version | Path | Access |
| ------- | ---- | ------ |
| **V4 (current)** | `D:\EmulatR\EmulatRAppUniV4` | read/write |
| V0 (sources) | `D:\EmulatR\EmulatRAppUniV3` | read-only |
| V1 | `D:\EmulatR\EmulatRAppUni` | read-only |
| V2 (POC) | `D:\EmulatR\EmulatrPOC` | read-only |
| Reference docs | `D:\EmulatR\Processor Support` | read-only |
| Trace storage | `D:\EmulatR\traces` (moved from X:\traces 2026-05-10) | read/write |

Always read `Processor Support\REFERENCE_INDEX.md` before opening
PDFs — it's the manifest with a "what should I read for X?" lookup.
Don't glob the Processor Support tree on every question.

---

## Key recurring patterns / conventions

- **PALcode HW_MTPR / HW_MFPR encoding** (post-2026-05-10 fix):
  - `hw_mtpr Rgpr, IPR`: Ra=R31 (unused), Rb=Rgpr (source), opcode 0x1D
  - `hw_mfpr Rgpr, IPR`: Ra=Rgpr (destination), Rb=R31 (unused), opcode 0x19
  - Authority: `Processor Support\Palcode\palcode\apisrm\apisrm\ref\ev6_huf_decom.m64`
- **HW_MTPR encoding bit layout**:
  - bits 31..26 = opcode (`0x1D` for MTPR, `0x19` for MFPR)
  - bits 25..21 = Ra
  - bits 20..16 = Rb
  - bits 15..8  = scbd (IPR selector, e.g. `EV6__PAL_BASE = 0x10`)
  - bits 7..0   = function bits / scoreboard mask (e.g. `EV6_SCB__PAL_BASE = 0x10`)
  - V4 reads bits 15..8 only; the function field is real-hardware
    pipeline-ordering metadata and is safely ignored at our level.
- **PAL_TEMP namespace**: raw scbd 0x40..0x5F → HW_IPR 0x0200..0x021F
  (PT0..PT31).  HW_PCTX (raw scbd 0x40) is shadowed; see deferred list.
- **HW_REI target**: bit 12 of encoding selects STACKED (read excAddr)
  vs REGISTER (read Rb).  Low bit of target = resume palMode.
- **Boot model**: SRM .exe loaded via two-stage path
  (loadPa+sigOffset → palBase+finalPC).  See `systemLib/SrmLoader.cpp`
  and the bootstrap comment in `palBoxLib/grains/PalEntries.cpp`.

---

## Canonical references for EV6 IPRs and PALcode entry vectors

### Start here: `Palcode\palcode\ROSETTA_STONE.md`

For a comprehensive subsystem-by-subsystem index of the full Digital
source tree at `D:\EmulatR\Processor Support\Palcode\palcode\`, read
`Palcode\palcode\ROSETTA_STONE.md` (created 2026-05-10).  It catalogs
SCSI / SCS / MSCP / CIPCA / INET / IPR / PALcode / HWRPB / PCI /
chipsets / TGA graphics / console shell / diagnostics / build tooling,
maps each V4 component to its Digital-source authority, and lists the
five canonical authority files in one place.  The detailed sections
below remain the deep dives for specific topics; use the Rosetta Stone
when you need to navigate the tree or aren't sure where to look.

### `EV6_DEFS.MAR` — authoritative IPR layout + exception/CALL_PAL vectors

Location: `D:\EmulatR\Processor Support\Palcode\palcode\srmconsole\EV6_DEFS.MAR`
(and an identical copy under `apisrm\ref\`).

This is Digital's own canonical layout file used to build the SRM .exe.
**The authoritative source** for any of the following:
- IPR scbd codes (HW_MTPR / HW_MFPR selectors)
- IPR sub-field bit layouts (start / width / mask per field)
- Hardware exception entry-vector offsets (palBase + 0x100..0x780)
- CALL_PAL entry-vector offsets (palBase + 0x2000..0x3FC0, privileged
  and unprivileged ranges)
- Chip-ID constants for the HW_I_CTL chip_id field

If a future session needs an architectural offset and isn't sure where
to find it, **read EV6_DEFS.MAR first**.  V4's `coreLib/Ev6EntryVectors.h`
already encodes every entry vector with compile-time `static_assert`s
that lock the values against this file; if a static_assert fails after
an edit, EV6_DEFS.MAR is the arbiter.

Four families of defines, all in VAX-MACRO syntax (`^x` = hex prefix):

| Define family | What it is | Example |
| ------------- | ---------- | ------- |
| `EV6__<NAME>` | IPR scbd (selector for bits 15..8 of MTPR/MFPR) | `EV6__PAL_BASE = ^x10` |
| `EV6__<NAME>__<FIELD>__S/V/M` | Field layout: start bit, width in bits, mask | `EV6__PAL_BASE__PAL_BASE__S = ^xf` (bit 15), `__V = ^x1d` (29 bits), `__M = ^x1fffffff` |
| `EV6_SCB__<NAME>` | Scoreboard bit mask (bits 7..0 of MTPR function field) | `EV6_SCB__CC = ^x20` (bit 5 of lane-1L) |
| `EV6__<NAME>_ENTRY` | Trap / CALL_PAL entry vector offsets from palBase | `EV6__INTERRUPT_ENTRY = ^x680`, `EV6__CALL_PAL_00_ENTRY = ^x2000` |
| `EV6__CHIP_ID_*` | Chip identification constants for HW_I_CTL chip_id field | `EV6__CHIP_ID_EV6_PASS3 = ^x4` |

### Cross-reference: V4 vs canonical scbd codes (verified 2026-05-10)

All V4 IPR codes match `EV6_DEFS.MAR`.  V4 stores `0x0100 + scbd` in
the `HW_IPR` enum to namespace away from PALcode-visible CALL_PAL
codes.  Spot-checks:

| V4 `HW_IPR` | scbd | canonical name |
| ----------- | ---- | -------------- |
| `HW_ITB_TAG` 0x0100 | 0x00 | `EV6__ITB_TAG` |
| `HW_ITB_PTE` 0x0101 | 0x01 | `EV6__ITB_PTE` |
| `HW_EXC_ADDR` 0x0106 | 0x06 | `EV6__EXC_ADDR` |
| `HW_PAL_BASE` 0x0110 | 0x10 | `EV6__PAL_BASE` |
| `HW_I_CTL` 0x0111 | 0x11 | `EV6__I_CTL` |
| `HW_CC` 0x01C0 | 0xC0 | `EV6__CC` |
| `HW_VA` 0x01C2 | 0xC2 | `EV6__VA` |
| `HW_VA_FORM` 0x01C3 | 0xC3 | `EV6__VA_FORM` |
| `HW_VA_CTL` 0x01C4 | 0xC4 | `EV6__VA_CTL` |

Note: V4 keeps a flat scbd-only enum.  EV6_DEFS reveals far richer
information per IPR (sub-field layout) that V4 doesn't yet exploit.

### Three-tier plan to absorb the rest of EV6_DEFS.MAR

**Tier 1 (~20 min, do anytime):** Create
`Processor Support\EV6_IPR_LAYOUTS.md` — a hand-curated cross-reference
table linking each `HW_IPR.h` enum to its `EV6__*` canonical name with
a one-line field summary.  Append a pointer to it in `HW_IPR.h`'s
file header.  Lowest blast radius; pure documentation.

**Tier 2 (~1 hr per IPR family, do on-demand):** Port specific
IPR field layouts into a companion header
`coreLib/HW_IPR_Fields.h` with constexpr shift/mask constants.  Apply
in the leaf to validate or mask writes.  Highest-priority candidates:

- **`HW_PAL_BASE`**: 32 KB alignment mask.  V1 had this:
  `pal_base = value & 0x00000FFFFFFF8000ULL`  (bits 43:15).
  V4 doesn't yet mask.  Currently harmless (SRM only writes
  aligned values), but Linux/Tru64 context-switch paths *will*
  exercise unaligned-write tolerance.
- **`HW_EXC_ADDR`**: bit 0 = PAL-mode flag, bit 1 = reserved-zero,
  bits 47:2 = PC, bits 63:48 = sign extension of bit 47.  Currently
  V4 stores whatever is written without separating fields; HW_REI
  recovers the palmode bit from the low bit of the target value.
- **`HW_I_CTL`**: many sub-fields (SPCE, IC_EN, BP_MODE, CHIP_ID,
  VPTB, ...).  HW_MFPR HW_I_CTL is how PALcode reads CHIP_ID to
  identify EV6 pass-level — port at least the CHIP_ID field so we
  return a defensible value when SRM probes it.

**Tier 3 (long-term, mechanical):** Python script that parses
`EV6_DEFS.MAR` (regular grammar:
`EV6__<NAME>[__<FIELD>][__<S|V|M>] = ^x<HEX>`) and emits
`generated/HW_IPR_Fields.h` with constexpr definitions for every
IPR field.  ~500 lines of generated definitions, eliminates hand-typing
risk entirely.  Worth doing once we're past `>>>` and want to harden
IPR semantics for OS bring-up.

### Practical bottom line

EV6 scoreboard bits (`EV6_SCB__*`) are pipeline-implementation
metadata — V4 ignores them by design and stays correct.

PAL_BASE alignment masking is the one "low-grade correctness debt"
worth fixing pre-OS-boot.  Everything else is opportunistic.

### `ev6_ipr_driver.c` — SRM console's IPR examine/deposit driver

Location: `D:\EmulatR\Processor Support\Palcode\palcode\apisrm\apisrm\ref\ev6_ipr_driver.c`.

Companion reference to `EV6_DEFS.MAR`.  This is the C source for the
SRM Console's `ipr:` device driver — what services the user when they
type `e ipr:i_ctl` or `d ipr:scbb <value>` at the `>>>` prompt.  Two
core routines:

- **`ipr_read(fp, size, len, buf)`** — big `switch(ipr_num)` on
  `APR$K_*` constants (the PALcode-visible CALL_PAL function codes,
  *not* the raw scbd).  Each arm shows the canonical source of that
  IPR's value: saved process context (`pctx`), per-CPU impure region
  (`impurePtr->cns$*`), or a HW_MFPR for hardware-direct registers.
- **`ipr_write(...)`** — mirror, with the destination-side equivalents
  plus longjmp-based reserved-operand-exception handling.

Why this matters for V4:

- Documents which IPRs the OS expects to be examinable/depositable via
  console (a much smaller set than the full HW_IPR enum).
- Shows OSF/1 PALcode storage conventions (e.g., PCBB/PRBR/PTBR/SCBB
  stored as page numbers in `cns$*` fields, shifted at access time;
  KSP/ESP/SSP/USP's mode-aware "current vs saved" logic).
- Tells us where each IPR semantically lives — handy when V4 grows a
  console-style accessor or implements `entryForFault` paths that need
  to read/write architectural state.

When V4 eventually adds a console-style examine/deposit harness (post-
`>>>`), the per-IPR case dispatch in this file is the structural
template.  Until then, it's reference material for OSF/1 PALcode
storage conventions.

### `EV6_OSF_PAL.MAR` — OSF/1 PALcode implementation specification

Location: `D:\EmulatR\Processor Support\Palcode\palcode\srmconsole\EV6_OSF_PAL.MAR`
(mirrors at `apisrm\ref\ev6_osf_pal.mar` and `palcode\palcode\src\ev6_osf_pal.mar`).
Companion defs file: `EV6_OSF_PAL_DEFS.MAR` in the same directory.

This is Digital's actual OSF/1 PALcode source — every CALL_PAL handler
and every hardware-exception handler implemented end-to-end.  **The
specification for how V4 must behave** at each PAL entry point.

Each handler is delimited by `;+` ... `;-` comment blocks following a
uniform contract:

```
;+
; <HANDLER_NAME>
;
; Entry:
;	<reg conventions: a0/a1/a2 = r16/r17/r18, p23 = saved PC, etc.>
;
; Function:
;	<prose description of what the handler must accomplish>
;
; Exit:                     (or "Exit state:" / "Exit conditions:")
;	<registers/IPRs/memory state the handler must produce>
;-
```

Examples worth knowing:

- `CALL_PAL__SWPCTX` at line 6107 — entry contract: `r16` is new PCB
  address, `p23` is post-CALL_PAL PC.  Exit contract: `v0` (r0)
  receives the old PCBB.
- `pal__restore_state` at line 5258 — richer "Current state" /
  "Exit state" pattern documenting shadow-mode and PALtemp invariants
- `FEN` hardware trap at line 1773 — "Vectored into via hardware trap
  on floating disable fault."  Builds stack frame with r16=fen-code,
  r17/r18 unpredictable, then vectors via entIF.

When V4 implements (or stubs) a CALL_PAL leaf in
`palBoxLib/grains/PalEntries.cpp`, the corresponding `;+` block in
this file is **the authority** for what registers and IPRs the leaf
must consume and produce.

### Reading guide: PALcode "block groups" and "fetch blocks"

Two EV6-microarchitectural patterns annotated throughout the file
that V4 can ignore functionally but should recognize when reading:

- **Buffer block N** comments — groups of 4 sequential `hw_stq/p`
  stores that share a store-buffer slot.  Real EV6 merges 4-store
  groups into a single write-buffer entry; PALcode emits stores
  in 4-tuples to maximize merging.  V4 retires in order and the
  stores are atomic, so the grouping is irrelevant to correctness.
- **`ALIGN_FETCH_BLOCK <^x47FF041F>` macro** — pads with NOPs to
  align the next instruction to a 16-byte (4-instruction) fetch
  quantum.  Used before sensitive HW_MTPRs that need to live in
  their own fetch block.  V4 doesn't fetch in quanta; ignore.
- **"1st block" / "5 fetch blocks for FPE"** comments — pipeline-
  scheduling constraints on EV6.  Same deal: V4 emulates retirement-
  order semantics, not fetch-block scheduling.

Practical tip when reading the file: any `bis r31, r31, r31` is
a deliberate NOP (scoreboard or fetch-block padding), not real work.

### `EV6_OSF_PC264_PAL.MAR` — Tsunami-specific PALcode (PC264 platform)

Location: `D:\EmulatR\Processor Support\Palcode\palcode\srmconsole\EV6_OSF_PC264_PAL.MAR`
(mirror at `apisrm\ref\ev6_osf_pc264_pal.mar`).

Platform-specific PALcode for a 21264 + Tsunami (21272) system.  This is
the system PALcode layer that sits above the OS-personality
`EV6_OSF_PAL.MAR` and knows about the actual chipset.  **Authoritative
for what PALcode does against the Tsunami chipset.**

Tsunami PA address map as constructed by PALcode:

| Region | Base PA | Built by |
| ------ | ------- | -------- |
| PCI 0 Memory | `0x800_0000_0000` | `lda p5, ^x800` then `sll p5, #32` |
| Pchip 0 CSRs | `0x801_8000_0000` | `GET_32CONS p5, ^x80180` then `sll p5, #24` |
| Cchip CSRs | `0x801_A000_0000` | `lda p4, ^x801A` / `zapnot p4, #3` / `sll p4, #28` |
| TIG (interrupt gateway) | `0x801_3000_0000` | `lda p7, ^x8013` / `zapnot p7, #3` / `sll p7, #28` |

(Pchip 1 CSRs are conventionally at `0x803_8000_0000` per HRM, though
the PC264 single-Pchip path doesn't always reference them.)

CSR offsets actually used by PALcode from `sys__int_clk` (line 1185):

| Offset | From base | Purpose |
| ------ | --------- | ------- |
| `0x3C0` | Pchip0 | PERROR (read for TA error, write to clear) |
| `0x280` | Cchip | DIR 0 (interrupt direction; bit 62 = P0_ERR) |
| `0x3C0` | TIG | CPU 0 Halt Register (bit 0 = halt-pending) |
| `0x5C0` | TIG | CPU 1 Halt Register (bit 1 = halt-pending) |

Error-capture state (`pal__capture_tsunami_errors` region, line 1477):
PALcode reads four chipset error registers into per-CPU impure region:

- `CNS__CCHIP_DIRX` — Cchip DIRx (which CPU's interrupt is active)
- `CNS__CCHIP_MISC` — Cchip MISC
- `CNS__PCHIP0_ERR` — Pchip 0 PERROR
- `CNS__PCHIP1_ERR` — Pchip 1 PERROR

Pass-1 Tsunami silicon workarounds documented inline (lines 1196-1240
for PIO retry counter overflow; lines 1242+ for spinlock lockout) —
V4 doesn't need to replicate these, but they hint at which CSR
sequences a real SRM might exercise.

**Practical use for V4**: cross-check `TsunamiChipset` address-decode
ranges against this file's bases.  When SRM eventually does the same
PA construction, V4's MMIO hooks should fire at exactly these
addresses.  Particularly relevant for Tomorrow's chipset-breakpoint
hunt: expect early reads to be Cchip CSR + small offset (CSC
identification) and Pchip0 sparse-mem (device probing).

### Reference-doc bottom line

Five files in `Processor Support\Palcode\palcode\` are the canonical
authorities for EV6 PALcode-level questions:

| File | Authority for |
| ---- | ------------- |
| `srmconsole\EV6_DEFS.MAR` (or `apisrm\ref\ev6_defs.mar`) | IPR scbd codes, IPR bit-field layouts, exception/CALL_PAL entry vectors, scoreboard masks, chip-IDs |
| `srmconsole\EV6_OSF_PAL.MAR` | **Per-handler implementation spec** — entry/function/exit contract for every CALL_PAL and every hardware-trap handler (OS-personality layer) |
| `srmconsole\EV6_OSF_PC264_PAL.MAR` | **Tsunami chipset PALcode interface** — actual PA addresses, CSR offsets, and access sequences PALcode uses against Cchip/Dchip/Pchip/TIG (platform layer) |
| `apisrm\ref\ev6_huf_decom.m64` | The `hw_mtpr` / `hw_mfpr` assembler macros (operand-source convention) |
| `apisrm\ref\ev6_ipr_driver.c` | OSF/1 PALcode storage conventions (where each IPR's value semantically lives, console examine/deposit semantics) |

The HRM PDFs are paginated references for *humans*; these five files
are the machine-readable ground truth for what the SRM .exe actually
expects.

---

## `osf.h` — OSF/1 PALcode personality contract

18 `osf.h` files in the tree (16 MILO platforms + master include +
sample_pal).  Unlike `cserve.h` (4 incompatible namespaces) or
`dc21X64.h` (chip-specific), **`osf.h` content is essentially
identical everywhere** — OSF/1 is the OS personality, chip-agnostic.
Defines: VA/PTE layout (8KB page, 3-level page table), entIF/entInt/
entMM kind constants, PCB field offsets, PS / IPL layout, OSF/1
exception stack frame.

| Layer | Canonical file |
| ----- | -------------- |
| C-syntax master | `palcode\palcode\palcode\include\osf.h` |
| MACRO-syntax (SRM build) | `apisrm\apisrm\ref\osfalpha_defs.mar` |
| EV6 chip extension | `apisrm\apisrm\ref\ev6_osfalpha_defs.mar` |

**For V4: the SRM .exe is built against `osfalpha_defs.mar` — that's
the constraint to match.**  The C-syntax `osf.h` is the same content
in an easier-to-read form (use it when grepping or comparing against
V4's C++).  Both have identical PCB field offsets, PTE bit layout,
and fault/interrupt/MM classification codes.

V4 status: not currently blocking — trap-delivery and address-
translation paths that would consume these constants are deferred.
Full constants table and V4 cross-reference are in
`Palcode\palcode\ROSETTA_STONE.md` — the **`osf.h`** section.

---

## Per-chip headers (`dc21064.h` / `dc21164.h`) — and the EV6 gap

MILO tree contains C-style per-chip headers documenting IPR maps,
PT register names, and PALcode entry-vector offsets:

| Header | Files | Covers | Canonical copy |
| ------ | ----- | ------ | -------------- |
| `dc21064.h` | 11 | 21064 (EV4) | `milo-sources-*\tools\include\dc21064.h` |
| `dc21164.h` | 8 | 21164 (EV5) | `milo-sources-*\tools\include\dc21164.h` |
| `dc21264.h` | **0** | (none — MILO predates EV6) | — |

The `tools\include\` copy is canonical in each case; per-platform
copies are stripped subsets.  **For V4 (EV6 target) the authority is
`apisrm\apisrm\ref\ev6_defs.mar`, NOT a `dc*.h` file** — there is no
`dc21264.h` in this tree.  Notable entry-vector evolution: RESET sits
at `0x0000` on EV4/EV5 but `0x0780` on EV6.  CALL_PAL bases
(`0x2000`/`0x3000`) are constant across all three generations — the
only piece of the layout that PALcode can rely on across chip
transitions.

When (if) V4 grows an EV4 or EV5 emulator profile, these headers are
the canonical authorities for that chip's IPR/entry-vector specs.
Full per-chip entry-vector tables and rationale are in
`Palcode\palcode\ROSETTA_STONE.md` — the **Per-chip headers** section.

---

## CSERVE namespace — canonical record for V4

CALL_PAL function `0x09` is the "CSERVE" console/firmware service
intrinsic.  Function dispatch happens inside the handler via R16's
low byte.  The Digital source tree contains **four different
incompatible CSERVE numbering schemes**:

| Namespace | Where | Range | Era |
| --------- | ----- | ----- | --- |
| 1 — MILO EV4 | `Palcode\palcode\milo-sources-*\palcode\{avanti,eb64*,eb66*,mikasa,noname,p2k}\cserve.h` | 0x01..0x0F | 21064 |
| 2 — MILO EV5 | `Palcode\palcode\milo-sources-*\palcode\{lx164,sx164,pc164,eb164,miata,takara,sample_pal}\cserve.h` | 0x01..0x20 | 21164 |
| 3 — MILO master include | `Palcode\palcode\palcode\palcode\include\cserve.h` | 0x01..0x22 | 21164+ |
| **4 — SRM ($cserve_def)** | **`Palcode\palcode\apisrm\apisrm\ref\pal_def.sdl` lines 66-91** | **8..101** | **EV6/SRM** |

**V4's canonical record is Namespace 4.**  Reasoning: V4 boots an
EV6 SRM .exe built from the apisrm tree, and that binary will issue
CSERVE calls using `$cserve_def` codes (SET_HWE=8, HALT=64,
WHAMI=65, START=66, CALLBACK=67, MTPR_EXC_ADDR=68, etc.).  The MILO
`cserve.h` namespaces target a different binary (the Linux/Alpha
MILO bootloader) and don't match.

**V4 status:** `palBoxLib\grains\PalEntries.cpp::execCserve` currently
implements a **fifth scheme** inherited from V1 (GETC=0x01, PUTC=0x02,
env vars at 0x20+, TOY clock at 0x30+, etc.) that matches none of the
Digital namespaces.  When SRM starts issuing CSERVE, V4 will hit the
`default:` arm and halt on `kFaultUnimplemented`.  Migration to
Namespace 4 codes is deferred until SRM actually issues a CSERVE call
in trace (none yet observed in the 178M-cycle pre-fix run).

Full per-function table and a "which namespace is this source using?"
diagnostic are in `Palcode\palcode\ROSETTA_STONE.md` — the **CSERVE**
section.

---

## Open threads worth checking next session

1. **Confirm the 64→1024 MB fix live (TOP PRIORITY).**  The last
   RelWithDebInfo run still printed `64 Meg`.  Cold-boot
   (`EMULATR_NO_AUTOLOAD=1`) and verify the stderr ini line, the
   `1024 Meg` banner, and `show config`/`show memory` →
   `Array 0: 1024 MB`, `AAR0=0x7009`.  See
   `journals/20260612_EOD_handoff_memory_size_64_to_1024.md`.
2. **`show device` for dqa0** + boot-time metric (IDE/S4 close-out).
3. **Floating-Point build-out (#43)** -- gates OS install; fBox is an
   IEEE-T POC. Start with conversions, then VAX G/F. See the Deferred
   section above and `journals/fBox_FP_Coverage_Map_20260610.md`.
4. **Tsunami chipset breakpoints**: which exact CSR offsets does SRM
   read first?  Cchip CSC is the typical first probe (revision +
   chipset ID).  Be ready to interpret the values SRM is writing.

---

## SESSION 2026-06-24 (PC) -- Platform latch landed; HWRPB-region fidelity opened; HANDOFF TO MAC

**CONTEXT MOVE / SINGLE SOURCE OF TRUTH:** This file (`memory.md`) and the project
`CLAUDE.md` were REHOMED from `D:\EmulatR\` (which is OUTSIDE git) into the GIT REPO
ROOT (`EmulatRAppUniV4\Emulatr\`) so they are version-controlled and the Mac can
`git pull` them. The old `D:\EmulatR\memory.md` / `D:\EmulatR\CLAUDE.md` are now
SUPERSEDED -- delete them or treat as stale. The repo-root copies are canonical.

**LANDED THIS SESSION** (some committed earlier in the day -- verify with `git log`;
the rest still UNCOMMITTED, commit before the Mac pulls):
- **P1 latch** (`systemLib/Machine.cpp`): ini `[System] model` vs manifest `platform`
  agreement assert -> `spdlog::error("PLATFORM MISMATCH ...")` on disagreement.
- **P2 canary** (`Machine.cpp`): one line per boot --
  `platform latched: model=.. manifest=.. usedDefault=.. ocp40=Y/N ocp42=Y/N iic_acks=[..]`
  -- deterministic predictor of the banner; greppable regression canary.
- **P3** (`systemLib/PlatformConfig.cpp` validate): warn if `platform=="DS20"` lacks
  OCP 0x40/0x42, or has 0x4E (would force DS20E).
- `ds10_v7_3_platform.json`: `"platform"` label fixed `DS20` -> `DS10` (the whole file
  is a clone of ds20 -- DEVICE TREE still needs a real DS10 review, flagged).
- `tools/run_fw.sh`: refreshes `<name>_v7_3_platform.json` next to the exe each run
  (CMake POST_BUILD copy only re-runs on relink, so edits could go stale).
- `tools/vsenv.sh`: loads MSVC+cmake into the current bash on Windows (vswhere ->
  vcvars64 via a temp .bat) so the bash toolchain works without a VS Developer
  prompt; no-op (warn) on mac/linux. SOURCE it: `source tools/vsenv.sh`.
- `journals/Platform_Interface_Contract_and_Latch_Plan_20260624.md`: the THREE-CHANNEL
  platform-identity model (A = chipset variant + IIC decode base, from ini model via
  `kIicBaseByModel` DS10=0xFFFF0000/DS20=0xFFF80000; B = IIC device tree, from
  `<stem>_platform.json` via `configureDevices()`; C = HWRPB `system_type/variation`,
  HARDCODED `DEC_TSUNAMI`/`0`) plus the P0-P6 plan.
- Diagnostic levers (env-gated, ZERO cost when unset): `EMULATR_IIC_TRACE` (IIC bus
  transactions), `EMULATR_SYSVAR_WATCH` (`pipelineLib/MemDrainer.h` store-watch for the
  SYSVAR member value + banner-name bytes).
- `tools/ghidra_scripts/DumpSysvarFns.java`: decompiles functions that reference the
  dsrdb banner table / SYSVAR decision strings (Java, not Jython -- this Ghidra has no
  Python provider; a stray `.py` in the script dir throws "no bundle type").

**BANNER / SYSVAR INVESTIGATION -- CONCLUSION** (full detail in the resume journal):
- DS20 firmware reaches `P00>>>` but badges **"AlphaPC 264DP 100 MHz"** not
  **"AlphaServer DS20"** (and "100 MHz" is wrong for an EV6 DS20 -- an RPCC-calibration
  symptom in the same family of issues).
- PROVEN ON THE WIRE (`EMULATR_IIC_TRACE`): OCP at 0x40/0x42 ACKs the probe, 0x4E
  correctly NAKs, firmware runs `sable_ocp_init` (drives the LCD) => `fopen("iic_ocp0")`
  SUCCEEDS. FRU at 0xA2/0xA4 reads `DEC`/`DS20`/`EV6`. **The IIC/OCP/FRU device layer is
  CORRECT; the badge is NOT an emulation gap in the IIC bus.**
- The badge is firmware-internal: `get_sysvar`/`build_dsrdb` pick a SysType member ->
  table at firmware VA **0x153cd8** = {264DP = default/member 1, DS20 = member 6,
  DS20E = member 8}; decision strings **0x19ad90** "Defaulting...264DP",
  **0x19adc0** "Error...SYSVAR=%x".
- THREE extraction routes EXHAUSTED: (1) SYSVAR store-watch x3 (size 8; then 2/4/8;
  value<=0xFFFF) never fired -> SYSVAR is not a simple catchable low-value store;
  (2) Ghidra string/table XREF -> ZERO code refs (decision code un-analyzed, reached
  via GP-relative/computed addressing); (3) `DumpSysvarFns.java` -> only data->string
  refs, no code function. Static RE here is real manual work, disproportionate alone.

**REFRAME (the point):** the HWRPB and the structures it anchors (per-CPU slots, CTB,
CRB, MEMDSC, DSRDB, GCT/FRU @ **0x3ff32000**) are the FIRMWARE->OS HAND-OFF CONTRACT --
the CRITICAL SECTION for booting an OS. The 264DP badge is the FIRST DETECTED DIVERGENCE
in that contract, NOT cosmetic. Next work = a spec-validated, uncompromisingly precise
map of the HWRPB region. Methodology + instrument designs + step-by-step MAC RESUME PLAN:
  **`journals/HWRPB_Region_Fidelity_and_Resume_20260624.md`**

**CRITICAL CAVEAT -- COWORK SANDBOX MOUNT IS UNRELIABLE FOR THIS REPO:** the Cowork
Linux sandbox sees the Windows repo over a FUSE mount that returns TRUNCATED reads and
cannot unlink files. This session: `.gitignore` and `ds20`/`ds25_v7_3_platform.json`
appeared truncated / spuriously "modified" in the sandbox git view (identical blob hash
on two distinct files = phantom; OCP entries intact), and an in-sandbox `git stash`
corrupted the index once. RULE: run ALL git ops + integrity checks NATIVELY (Windows
now, Mac tomorrow). Do NOT commit a truncated manifest -- verify with native `git diff`.

**OPEN BLOCKERS (downstream of the badge):**
- `0x1ad770` kFaultDtbMiss spin = firmware idling at an INTERACTIVE console prompt (LFU
  "enter device / hit <return>") -- drive PuTTY to progress; NOT a hang.
- PCI: firmware probes an un-enumerated on-board NIC (DE500/21143) -> all-ones BAR ->
  base 0xFFFF0000 -> `TsunamiPchip` UNHANDLED OUTER WRITE. Non-fatal; see deferred-work.

---

## 2026-06-28 -- HWRPB hand-off: TWO-GATE plan (consuming/preparing for OS boot)

Full plan: `journals/20260628_hwrpb_handoff_gates_plan.md`. The bifurcation is **TEMPORAL**:
the OS never consumes the `>>>` image, it consumes the HWRPB as it stands at the `boot`
hand-off. So we need TWO gates, and we only have the first:
- **Gate A -- `P00>>>` (HAVE IT):** `EMULATR_HWRPB_SCAN` -> console-idle snapshot
  (HWRPB @ PA 0x2000, single SRM-built copy; top-level directory mapped in the 0625 journal §9).
- **Gate B -- `boot` hand-off (NEXT):** reuse `EMULATR_PA_WATCH` pointed at the per-CPU slot
  bootstrap-in-progress/state field (PerCpuSlot `state` @ slot+0x080); when the SRM sets BIP
  + transfers to the secondary bootstrap, fire the SAME region dump = the "consuming" snapshot.

SEQUENCE: **(1)** deepen Gate A to field-by-field via a new `EMULATR_DUMP_PA=<pa>[:<len>]`
extension to `scanGuestForHwrpb()` in `systemLib/Machine.cpp` (the `hexdump` lambda is already
in that function). **MEMDSC/MDDT @ PA 0x2840 FIRST** -- most boot-critical (OS reads it to map
physical memory). Decode `MemoryDescriptor` per `deviceLib/Hwrpb.h`: checksum+0, reserved+8,
cluster_count+16, cluster[3]@+24 (each 56B: start_pfn/pfn_count/test_count/bitmap_va/bitmap_pa/
bitmap_checksum/usage). ASSERT Sigma(pfn_count) x 0x2000 (8 KB pages) == configured 4 GiB --
wrong clusters = OS can't map memory = silent boot fail. Then per-CPU slot 0 (resolve live
stride **0x280** vs spec **0x400**), CTB/CRB, DSRDB. **(2)** stand up Gate B. **(3)** diff
A<->B = the prep-for-OS-boot delta = golden image. **(4)** boot-time HWRPB validator (extends
the P1/P2 latch). IMMEDIATE when code resumes: confirm the configured-RAM accessor on
Machine/GuestMemory, then add `EMULATR_DUMP_PA` + the MEMDSC decoder. The 264DP/SYSVAR badge
is ONE PARALLEL ledger item, NOT the OS-boot critical path. NO instrument code landed today
(plan/journal/memory only). Also this session: fixed the ini `model = ES40` -> `DS20` mismatch
that was silently booting DS20 firmware on an ES40 chipset (platform now latches DS20/DS20).
