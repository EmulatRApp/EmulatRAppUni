<!--
EmulatR -- Architecture / Development Status (single-document synthesis)
Project: EmulatR -- Alpha AXP / EV6 (21264) full-system emulator.  V5 active tree.
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic, Cowork).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
ASCII(128) only.  Hex radix.  Timelines intentionally omitted -- this is a
"where we are architecturally" snapshot, synthesized from memory.md, the TB
brief, the JRN-VMB boot journals, and the SRM-conformance + NET-ADAPTER work.
-->

# EmulatR -- Architecture Development Status

    Doc id  : ARCH-STATUS-001
    Purpose : One-document, timeline-free picture of the system's architectural
              state -- what is faithful, what is partial, what is deferred, and
              where the live frontier is.  Not a roadmap; a status snapshot.
    Scope   : V5 (branch v5-tb).  V4 is the frozen correctness Oracle.
    Sources : memory.md; 20260715_v5_tb_implementation_brief.md; JRN-VMB-012/013/
              014; NET-ADAPTER-001; SRM-conformance register D1-D16.

---

## 1. What EmulatR is

A faithful, full-system emulator of the DEC/Compaq Alpha **21264 / EV6** (and
EV67) running the real SRM console firmware and, as the goal, real guest
operating systems (OpenVMS, Tru64). It is an interpreter-first design whose
governing principle is that **EmulatR itself is the primary Oracle** -- AXPBox,
SimH and others are corroborative only, never ground truth.

Target machines are the Tsunami/Typhoon-class boxes -- **DS10, DS20, ES40** (and
the Titan-class DS25/ES45 as an experimental second chipset).

## 2. Version posture

  - **V4 = FROZEN Oracle** (tag `v4-frozen`). The reference interpreter; every
    V5 acceleration must diff bit-identical against it. Not modified.
  - **V5 = active** (branch `v5-tb`). V5 = V4 + a decode-amortizing Translation
    Buffer (TB) tier, POC-first. All new work lands here.

## 3. Top-line status

**The V4 objective is met: SRM reaches the `>>>` prompt on DS10, DS20, and ES40**
(default/ISP mode). That milestone gated the V5 TB fork. Two architectural
frontiers are now live in parallel:

  1. **Execution-acceleration frontier** -- the TB tier (and the lever hierarchy
     around it) to make faithful execution fast enough to reach an OS.
  2. **Boot-correctness frontier** -- getting a guest past the SRM handoff. The
     VMB now loads and reaches the OS hand-off, but the transfer to the system-
     software entry does not yet fire (Sec 9).

### Subsystem readiness at a glance

| Subsystem | State | Note |
|---|---|---|
| CPU core (EV6 integer/PAL) | FAITHFUL | Oracle interpreter; SRM-exercised |
| Determinism scaffold (agent/dispatcher) | LANDED | Sequential==Threaded gate |
| MMU / software TLB | FAITHFUL | fully-assoc fix landed (Sec 6) |
| Tsunami/Typhoon 21272 chipset | FAITHFUL (core) | interval-timer PARTIAL |
| Titan 21274 chipset | MODEL-ONLY | CSR model landed, not yet wired |
| HWRPB / firmware handoff | FAITHFUL | @0x2000, slot stride 0x280 |
| SRM boot to `>>>` | REACHED | DS10/DS20/ES40 |
| OS hand-off (0x20000000) | BLOCKED | live frontier (Sec 9) |
| Snapshots (L1) + entry snapshot | LANDED | L2 pipeline-state deferred |
| Floating point (fBox) | POC | VAX G/F absent -- gates OS install |
| PCI enumeration / on-board devices | GAP | 1 device vs 9 on silicon |
| Ethernet (DE500 / 21143) | NOT MODELED | design done (NET-ADAPTER-001) |
| SCSI / storage (IDE/ATAPI) | ~90% | dq-boot gated on PCI + multiblock |
| SMP (>1 CPU) | SCAFFOLDED | LL/SC cliff deferred; cpuCount=1 |
| Translation Buffer (TB) tier | DESIGN + POC | authoritative brief written |

## 4. Execution model

The distinguishing architecture. Three execution routes and a lever hierarchy.

**Three routes, two passes (per physical address, interleaved):**
  - **Route 1 -- Oracle interpreter (V4):** always correct; the floor and the
    differential oracle. Everything de-opts down to it.
  - **Route 2 -- TB:** decode a straight-line run ONCE, dispatch it many times,
    using the SAME executors as the Oracle. Key invariant: a TB caches DECODE
    (opcode + register indices + executor pointer + per-instruction cycle cost),
    **never register values** -- grains read/write the shared register file across
    the boundary, so a dispatched block advances the cycle counter identically by
    construction. At Route 2 the residue is empty (every instruction is a grain;
    only terminators end blocks). Faithful almost for free.
  - **Route 3 -- ComJIT:** hot TBs compiled to host code. First tier that re-
    expresses semantics; deferred until the shared invalidation substrate is
    proven at the TB tier against the real SRM boot.

**Dispatch key:** accelerator = (Virtual PC, ASN, PAL-mode); anchor = (physical
page, page generation). Dispatch gates on the physical anchor, so ASN recycle /
aliasing / PTE remap all collapse to a generation-mismatch rebuild -- one shared
invalidation substrate for TB and ComJIT.

**Lever hierarchy (which tool for a hot loop -- is it a SPIN or is it WORK?):**
  - **SNAPSHOT eliminates cycles** (restore committed state; they never execute)
    -- the decompressor is the canonical case; multi-order-of-magnitude.
  - **WARP skips cycles** (recognize a busy-wait shape, compute where it lands,
    jump the counter) -- the RSCC / micro-delay family.
  - **TB cheapens cycles** (removes repeated DECODE only; ~1.5x, not a rescue) --
    the smallest lever, and warp's stable host, not its replacement.

**Faithfulness modes:** "**silicon**" (REAL_HW: faithful, all warps off -- the
mode that exposes real timing behavior like the ES40 LFU hang) vs "**ISP**"
(intercepts the 0xBFFC platform sentinel to skip real-HW timing and reach `>>>`
fast). Faithfulness is self-verifying via the end-of-run **WARP-ACCOUNTING**
line (`warp_cyc=0`, `K=1.000` == pure silicon). Not an ini key -- it is the
absence of the warp env levers.

**Determinism substrate:** the run path is `AlphaCpuAgent` behind a `Dispatcher`
(logical clock + syncPhase) with a swappable execution driver (Sequential oracle
/ Threaded barrier). The `determinism_equivalence` doctest (Sequential ==
Threaded, bit-identical) is the acceptance fixed point the TB and V4-vs-V5 diff
gate stand on. The legacy `Machine::run` loop is deleted.

## 5. CPU / PAL semantics

  - **PAL personality is per-image:** the ES40 console runs the **OpenVMS** PAL
    variant (`ev6_vms_pc264_pal.mar`, RSCC = CALL_PAL 0x9d); OSF layout must be
    reconciled before trusting slot/comm-area details on other paths.
  - **RSCC == cycleCount** (kCcMultiplier = 1): a guest "wait until RSCC reaches
    target" is a wait on the emulator cycle counter. The HW_CC IPR (ccOffset,
    writable, per-process via swpctx) is split from the sim-only pipeline counter.
  - **EV6 shadow registers (SDE):** R4-R7 and R20-R23 swap to shadow copies in
    PAL mode when SDE=1 -- load-bearing for PAL divert/REI fidelity (and the
    prime suspect in the current boot-handoff bug, Sec 9).
  - **IPR / PAL_TEMP encoding** is settled (HW_MTPR/MFPR operand convention;
    PAL_TEMP raw scbd 0x40..0x5F -> HW_IPR 0x200..0x21F); **HW_REI** target
    decode (stacked vs register, PAL-bit off the low bit) is a reference
    reconciliation not to be redesigned. `PC<0>` is the PAL-mode flag.
  - **CSERVE** namespace is settled (SRM `$cserve_def`; last defined 101/0x65;
    0x66 is undefined and its no-op is faithful -- a prior "get_time at 0x66" was
    a regression, removed).

## 6. MMU / memory

  - **GuestMemory** is a sparse per-64KB-page pager; multi-byte accessors split
    page-crossing accesses byte-wise (a page-crossing store overrun was fixed).
  - **Software TLB is faithful** (EV6 has no hardware page-table walker). The DTB/
    ITB are 128-entry **fully associative** -- modeled now as `SPAMShardManager
    <2,64>` (2 shards x 64 ways = 128 slots), which fixed a conflict-eviction
    thrash that previously mis-modeled them as 16-way set-associative (JRN-VMB-012).
  - **Deferred:** the `Ev6Translator` harvest -- a reference 3-level walk + PTE-
    format converters + alignment-before-translation ordering + VA-form decode,
    harvest-only (foreign deps + its own gaps).

## 7. Chipset / platform

  - **21272 = Tsunami** (Typhoon = its high-bandwidth variant, same part) drives
    DS10/DS20/ES40 and is faithful at the core: the PA map (PCI0 mem
    0x800_0000_0000; Pchip0 CSR 0x801_8000_0000; Cchip 0x801_A000_0000; TIG
    0x801_3000_0000), TIG smir/halt registers, and a wired Cchip IPI. The
    **interval-timer / Cchip timing model is PARTIAL** (`fireIntervalTimer` TODO)
    -- relevant to all RSCC/warp timing work and to the timer-unification spec.
  - **21274 = Titan** (DS15/DS25/ES45) is a SEPARATE chipset (dual discrete
    Pchips, relocated Pchip1, AGP). The CSR model has landed (`Titan21274_*`) but
    is not yet an `ISystemBus` and lacks the device layer; model selection still
    hardcodes Tsunami.
  - **South bridge is model-gated:** ALi M1543C for ES40/ES45/DS25; Cypress
    CY82C693 for DS10/DS20. (Note: SRM-conformance found the real DS10 on-board
    IDE is the ALi M1543C, so the Cypress default is a DS10 fidelity gap -- D4.)
  - **Platform identity is a 3-channel contract** kept aligned per firmware:
    (A) chipset variant + IIC decode base from ini `[System] model`; (B) IIC
    device tree from `<stem>_platform.json`; (C) HWRPB system_type/variation. The
    ini `model` is a separate channel, guarded by a boot canary ("platform
    latched: model=... manifest=... usedDefault=...") that doubles as a
    regression predictor. Platform manifests are the SSOT (JSON, runtime-loaded).

## 8. HWRPB / firmware -> OS hand-off contract

The SRM builds a single HWRPB at **PA 0x2000** (EmulatR's own HwrpbBuilder is not
used). Settled layout: SYSTYPE@+0x50 = 0x22 DEC_TSUNAMI; per-CPU slot stride
**0x280** (AARM-canonical); region map through GCT/FRU @ 0x3ff32000. `deviceLib/
Hwrpb.h` carries the spec-true structs with static_asserts. This is the contract
the OS bootstrap consumes -- and the structures the current boot frontier reads.

## 9. Boot pipeline and the LIVE frontier

**Pipeline:** SROM load (compressed `.exe`, two-stage load) -> guest self-
decompress (~4M-cycle inflate; a byte-faithful native `host_decompressor` oracle
exists) -> PAL takeover -> SRM console banner -> `>>>` -> `boot dqa0` -> VMB loads
and builds HWRPB / page tables -> **hand-off to the OS at VA 0x20000000**.

**The wall (JRN-VMB-013/014):** the VMB completes, prints "jumping to bootstrap
code", and the CPU **halts with reason 0 (RESET) at exc_addr = 0x20000000** --
it never fetches there (ITBPROBE for 0x20000000 fires zero times). The transfer
is not a jump: the console sets HALT_PC/KSP/PTBR/VPTB, exits, and the PAL restart
is supposed to HW_REI into 0x20000000 -- and that restart is where it fails.

**Root-cause hypothesis (source-confirmed, runtime-unconfirmed):** the PAL restart
reads its in-memory PALtemp/impure region through base register **r21 (`p_temp`)**,
and the flagged `HW_LD from PA 0x98` is `PT__WHAMI(r21)` with r21 = 0 -- i.e. the
PALtemp base is zero at the restart, so every `PT__IMPURE`/`CNS__*` access lands
at absolute low memory and the flow falls into the RESET vector instead of
REI-ing to 0x20000000. The prime emulator-side suspect is the EV6 shadow-bank
swap zeroing r21's shadow (Sec 5) on the PAL entry that runs the restart. One
runtime capture (r21 at the handoff) confirms or refutes it.

**Separate ES40 sub-frontier:** in silicon mode, LFU `exit` -> "Initializing...."
hangs on a `platform()`-gated calibrated RSCC micro-delay (a genuine spin, ~10^10
cycles). This is a **WARP** target (a coherent deadline-warp that advances
cycleCount and the interval-tick timebase together), not a TB or correctness bug;
ISP mode skips it. It is also the first intended TB/warp recognition target.

## 10. Devices

**Modeled and exercised:** 16550 UART (COM1/COM2; polled console path faithful),
PCF8584 IIC (polled), SMC37C669 SuperIO, TIG/Cchip/Pchip CSRs, IDE/ATAPI (Cypress
func1 enumerates dqa0/dqb0; ES40 via ALi M5229; IDE ~90%, ATAPI READ landed),
VGA/keyboard, floppy FDC. Storage rides an `IBlockMedia` seam.

**Reference-staged / partial:** SCSI (NCR/Symbios 53C8xx defs + driver as the
register-model authority; QLogic ISP1020/KZPBA as the future regime-3 reference).

**Known gaps (measured against real DS10 via the SRM-conformance kit):**
  - **PCI enumeration** -- EmulatR enumerates ~1 on-board device where silicon
    shows 9 (ewa/ewb, dqa/dqb, pka/pkb, pga/pgb, vga). Needs a real PCI bus walk +
    dynamic BAR->range rebind (`IPciDevice` seam). This is the single biggest
    device-fidelity item and it blocks the SYSBOOT> path.
  - **Ethernet** -- not modeled. The on-board NIC is the DE500-BA = **DECchip
    21143** ("TULIP"), SRM `ewa`/`ewb`. Its absence is the concrete device behind
    the standing TsunamiPchip UNHANDLED OUTER WRITE at PA 0x800_FFFF_0000 (the
    firmware pokes the un-enumerated NIC's CSR9 SROM). Design + implementation
    shape (21143 core + thin board/EEPROM wrapper, enumeration+SROM-MAC stub
    first) is in NET-ADAPTER-001.
  - **Fibre (FCA-2684), environmental sensors, clock-MHz banner (reports 8 MHz vs
    617), and a handful of default mismatches** -- catalogued as D1-D16 in the SRM
    conformance register.

## 11. Floating point

fBox is an **IEEE-T-only POC**. Present: T-format arithmetic (shallow), ordered
T-compares, CPYS, FP load/store, MT/MF_FPCR (storage), FEN trio, FTOIT. **Absent
(decode-fault): all conversions, ALL VAX float (0x15) -- the critical OpenVMS gap,
since VMS defaults to G_float/F_float -- SQRT, CMPTUN, FCMOVxx, FP branches.**
This is the item that **gates OS install**. Build-out order and native-leaf
strategy are mapped.

## 12. Snapshots / persistence

**Level 1 snapshot landed** (CpuState + GuestMemory + chipset CSRs + SRM staging;
auto-save + autoload-newest). An **entry snapshot** (env-gated) can skip the
decompressor by restoring at the init->console handoff. **Persistence:** an
emulated AMD-FSM flash ROM holds firmware + NVRAM env, persisted only on clean
exit; the HWRPB is RAM-only and rebuilt every boot. (Note: a diag-flash / "flash
writes disabled" run configuration is why SRM env "doesn't persist" in some test
captures -- a config axis, not a defect; SRM-conformance D7.) Level 2 (in-flight
pipeline state) is deferred.

## 13. SMP

The determinism scaffold (agent + dispatcher) is the SMP substrate and is landed;
CpuState ownership lives in the agent. **Phase 3 -- the LL/SC cross-CPU interlock
-- is the identified cliff and is deferred** (per-CPU lock_flag/granule, interleave
only at the LL/SC boundary). Secondary-CPU rendezvous, cross-CPU IPI (reuses the
Cchip work), and determinism extension follow. Boots run single-CPU (cpuCount=1)
deliberately -- advertising >1 CPU while one executes makes SRM/GCT spin.

## 14. Tooling / diagnostics (architecturally relevant)

Env-gated, zero-cost-off instrumentation is a first-class part of the design:
retire-trace facilities (the RETIRE_COMPACT `.trc` firehose, the BOOTTRACE marker-
armed trace, the BreakpointSink gated `_break.trc` with full-GPR per-retire
records), `EMULATR_DIAG_*` windows, `EMULATR_RSCC_DIAG`, the WARP-ACCOUNTING
summary, and the retire-PC profiler -- all compiled OUT of release, present in
relwithdebinfo/debug. External corroboration uses **AXPBox** (21143/DEC21143.cpp,
etc.) strictly as a secondary oracle. A black-box **SRM differential-conformance
kit** compares the console against real-hardware golden captures (the D1-D16
register).

## 15. Deferred / open architectural fronts (consolidated)

  - **TB tier build-out** (POC -> Route-2 physical-keyed store -> shared
    invalidation substrate -> ComJIT) -- the acceleration frontier.
  - **OS hand-off (r21/PALtemp) fix** -- the boot-correctness frontier (Sec 9).
  - **PCI bus walk + on-board device models** (NIC, fibre) -- unblocks SYSBOOT>.
  - **Floating point build-out** (conversions, VAX G/F) -- gates OS install.
  - **SMP Phases 3-6** (LL/SC cliff onward).
  - **Interval-timer / Cchip timing completion** + coherent deadline-warp.
  - **Titan 21274 device layer** (make it a real ISystemBus).
  - **Ev6Translator harvest; EV5 (21164) profile; IPR field-layout absorption.**

## 16. One-paragraph summary

EmulatR is a faithful EV6 full-system interpreter that now brings the real SRM
console to `>>>` on DS10/DS20/ES40, with a settled CPU/PAL/MMU/chipset core, a
landed deterministic multi-agent scheduling substrate, Level-1 snapshots, and a
faithful (fully-associative) software TLB. Two frontiers are open: an execution-
acceleration tier (the decode-amortizing Translation Buffer, plus the snapshot/
warp lever hierarchy) to make faithful execution fast enough to run an OS; and
boot-correctness past the SRM hand-off, currently walled at the PAL restart into
VA 0x20000000 (working hypothesis: a zero PALtemp-base register r21 at the
restart). The largest device-fidelity gaps -- full PCI enumeration and the DE500/
21143 Ethernet -- are scoped with designs in hand, and floating-point build-out
(VAX G/F) remains the gate on actually installing a guest OS.
