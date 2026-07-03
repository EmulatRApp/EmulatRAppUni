<!--
EmulatR V4 -- ES40 first-boot review: IIC bus UNMAPPED (no kIicBaseByModel row)
+ concurrent-console (DS10/DS20/ES40 side-by-side) enablement.
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree.
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-07-02.  ASCII(128) only.
Source run: RelWithDebInfo/traces/20260702-111843_es40_{console.out,machine.log,srm.trc}
-->

# ES40 First Boot: IIC Bus UNMAPPED + Concurrent-Console Enablement

**Date:** 2026-07-02
**Status:** ROOT GAP IDENTIFIED (ES40 IIC base). Fix path is known and matches the
method that located the DS20 base. Concurrent-console blockers identified + one source
fix applied (per-instance PuTTY sessionlog).

---

## 1. ES40 run outcome (the first genuine cold boot)

Launcher: the (older) `run_es40_srm_trace_full.sh`, RelWithDebInfo, cold, port 10023.
Run reached ~cyc 282M then was stopped by the operator (pre-banner).

**What worked -- ES40 boots DEEP:**
- Platform latched correctly: `platform latched: model=ES40 manifest=ES40 usedDefault=0
  ocp40=N ocp42=N iic_acks=[ 0x70 0x72 0xA2 0xA4 0xC0 ]`. The ES40 manifest loaded; NO
  silent DS10-bus fallback (watch-out #2 CLEARED).
- SRM ran through decompression into real console init (COM2 mirror): initializing
  semaphores / heap / driver structures / idle process PID / file system / hardware /
  timer data structures; lowering IPL; create dead_eater/poll/timer/powerup; **access
  NVRAM**; `Memory size 4096 MB`; `testing memory`. Comparable depth to DS20 pre-banner.
- `get_sysvar` ran: `GMEM-WATCH(0x2058)` shows the base SYSVAR seed **v=0x5** land at
  ~cyc 282M (then the retire-arm fired). SYSTYPE/HWRPB build path reached.

**Where it stopped:** right after the SYSVAR=0x5 seed and BEFORE the discriminator IIC
poke -- so NO member store (0x405/0x1805), no banner, no P00>>>. Crucially, no
`UNHANDLED OUTER WRITE` appeared yet (see Section 3).

**Secondary observations (handled, non-fatal):**
- 65x `kFaultDtbMissDouble @ pc=0x8321` (~cyc 249M) -- double DTB miss, delivered,
  boot continued. (pc 0x8321 is the same landmark DS20's 250M cap once hit.)
- 17x unaligned 8-byte load fixups `@ pc=0x5afac` (va ~0x3fc12000, ~cyc 282M).

---

## 2. ROOT GAP: ES40 IIC controller is UNMAPPED

Console line 17:
```
TsunamiChipset: no proven IIC base for model 'ES40' -- IIC left UNMAPPED.
First poke -> UNHANDLED OUTER WRITE will surface the real base
(the signal that located DS20); add a proven kIicBaseByModel row when known.
```

Source: `chipsetLib/TsunamiChipset.h:690-729`. The IIC (PCF8584) controller is mapped
from a per-model table:
```
static constexpr struct { char const* model; uint64_t base; } kIicBaseByModel[] = {
    { "DS10",  0xFFFF0000ULL },   // proven: iic_write_csr     [2026-06-03]
    { "DS20",  0xFFF80000ULL },   // proven: writeb@0x1ade60   [2026-06-22]
    { "DS20E", 0xFFF80000ULL },   // shares DS20 chassis/IIC map (defensive)
};
```
Find-or-fail: no ES40 row -> `iicBase == nullptr` -> the IIC MMIO range is NOT
registered (`registerPciMemRange` is skipped). ES40 is NOT in the `iicBaseRequired`
hard-stop set (DS10/DS20/DS20E), so it is allowed to run with an unmapped bus rather
than abort.

**KEY FACT: the IIC base is NOT chipset-fixed.** DS10 = 0xFFFF0000 and DS20 = 0xFFF80000
differ even though both are Tsunami 21272. The base is the address the platform firmware
actually pokes (a BAR-masked PCI-mem address), so ES40's cannot be assumed equal to
DS20's -- it must be measured/derived for ES40.

**Consequence for the badge:** this is the ES40 analog of the DS20 badge bug, one layer
DEEPER. DS20 had a MAPPED IIC bus missing a discriminator NODE (0x9e). ES40 has NO IIC
bus mapped at all, so `get_sysvar`'s discriminator `fopen` has no controller to reach ->
the ES40 SysType member can never resolve, and the manifest's IIC nodes
(0x70/0x72/0xA2/0xA4/0xC0) are moot until the base exists. The IicPcf8584 device model
itself is fine (proven on DS20); ES40 only lacks the BASE + the discriminator node.

---

## 3. Fix path -- implement ES40 IIC support (same method that located DS20)

The DS20 base was found empirically: the firmware's first IIC poke hit the unmapped
range and emitted `UNHANDLED OUTER WRITE offset=0x...` -- that PA IS the base. The ES40
run stopped just before that poke, so the signal has not been captured yet.

**Step 1 -- MEASURE the ES40 IIC base.** Re-run ES40 and let it continue PAST ~cyc
282-290M (do NOT stop it; the discriminator poke is imminent right after the 0x5 seed).
Grep the console/machine log for `UNHANDLED OUTER WRITE` -- the offset/PA it pokes is
the ES40 IIC base. (Reaching it may need the LFU/console driven, like DS20; or simply a
longer unattended run into the get_sysvar window.) Corroborate against the ES40 firmware
disasm / apisrm pc264 `iic_write_csr` if the empirical value is ambiguous.

**Step 2 -- ADD the kIicBaseByModel row.** In `TsunamiChipset.h:691`, add
`{ "ES40", 0x<measured>ULL },  // proven: <site> [2026-07-..]`. Optionally add ES40 to
`iicBaseRequired` once proven so a future regression hard-stops instead of silently
unmapping. Rebuild (RelWithDebInfo + TRACE_HOOKS).

**Step 3 -- ADD the ES40 discriminator node to the manifest.** With the bus mapped,
determine which IIC node ES40's `get_sysvar` probes (trace the fopen, as with DS20's
0x9e), and add it to `es40_v7_3_platform.json` `iic_devices`. Then the deterministic
path selects the ES40 member.

**Step 4 -- VERIFY.** Cold boot: `GMEM-WATCH(0x2058)` final store should show the ES40
member word (NOT 0x405 = 264DP fallback), and the banner should read
"Compaq AlphaServer ES40".

---

## 4. Concurrent consoles (DS10 + DS20 + ES40 at once) -- blockers + fixes

GOAL: run three models side by side, each PuTTY console updating independently.

**Blocker A -- port collision (FIXED in tooling).** `makeCom1Cfg` (Machine.cpp:303-324)
takes the listen port from ini [SRMConsole] port (default 10023), overridable by env
`EMULATR_CONSOLE_PORT`; the PuTTY auto-launch uses that SAME port (SRMConsoleDevice.cpp:
652 `-P <m_config.port>`). The older per-model scripts all defaulted to 10023, so a
second instance's listener failed to bind and its PuTTY dialed the wrong server. The new
`tools/run_srm_trace_full.sh` assigns a per-model default port:
`10023 + offset` -> **ds10=10023, ds20=10024, ds25=10025, es40=10026, es45=10027**.
Three distinct listeners, three PuTTYs each dialing their own port.

**Blocker B -- shared hardcoded PuTTY sessionlog (FIXED in source, this journal).**
SRMConsoleDevice.cpp:656 launched PuTTY with a HARDCODED, shared logfile
`d:/emulatr/traces/app_output_&Y&M&D&T.log` -- (a) identical for every instance (three
PuTTYs fighting over one file), and (b) an absolute `d:/emulatr/traces/` path that does
not exist on this tree (D:\EmulatR\EmulatRAppUniV4\Emulatr), which can make PuTTY choke.
FIX: made the sessionlog PER-INSTANCE and run-dir-relative:
`traces/putty_console_p<port>_&Y&M&D&T.log`. It lands in the run dir's existing traces/
(the launcher mkdir's it; PuTTY inherits the emulator's CWD = run dir) and the port
disambiguates instances. PuTTY still expands the &Y&M&D&T date/time tokens.

**Per-instance isolation already in place (tooling):** the launcher gives each model its
own diag flash (`<model>_diag_flash.rom`) and model-tagged, timestamped logs
(`<ts>_<model>_{dec,machine,console}`), so nothing else collides.

**Blocker C -- the 78 GB retire .trc (operational).** The `RETIRE_COMPACT --trace`
channel dumped EVERY retired instruction for the whole run (the 2M-instr GMEM-TRACE-ARM
window gates a different sink, NOT this channel) -> the .trc was ~78.9 GB. Three of those
concurrently would destroy the disk, and they collide on the shared `<ts>_srm.trc`
name in EMULATR_RETIRE_TRACE_DIR. For concurrent / console-watching runs use **`ARM=none`**
(unsets EMULATR_TRACE_WINDOW/RETIRE_TRACE_DIR -> no .trc) and rely on the model-tagged
`machine.log` for the diff. The .trc-not-windowed behavior is a separate trace-infra bug
to fix later (the RETIRE_COMPACT channel should honor the arm countdown).

**Concurrent recipe (three consoles, no giant .trc):**
```
ARM=none ./tools/run_ds10_trace.sh &   # console TCP 10023
ARM=none ./tools/run_ds20_trace.sh &   # console TCP 10024
ARM=none ./tools/run_es40_trace.sh &   # console TCP 10026
```
Each auto-launches its own PuTTY on its own port with its own sessionlog. (Diff later
with `tools/diff_traces.py <ds20 machine.log> <es40 machine.log>`.)

---

## 5. Address landmarks confirmed for ES40 (add to the checkpoint reference)

- SYSVAR base seed store: PA 0x2058, v=0x5 (~cyc 282M) -- get_sysvar reached.
- Fault-storm spin: pc=0x8321 (kFaultDtbMissDouble) ~cyc 249M.
- Unaligned-load site: pc=0x5afac (8-byte, va ~0x3fc12000) ~cyc 282M.
- IIC base: UNKNOWN pending the UNHANDLED OUTER WRITE capture (Section 3).
These match the DS20 shared-path expectation in
`journals/ES40_significant_address_regions_checkpoint_ref_20260702.md`.

---

## 6. CORRECTION + AUTHORITATIVE RESOLUTION (2026-07-02 pm, 2nd run)

**Correction to Section 1:** ES40 does NOT "stop pre-banner" -- it **DETERMINISTICALLY
HANGS**. The 2nd run (131501) is byte-identical to the 1st and the machine.log tail shows
a tight runaway loop at pc=0x5afac:
```
0x5afa8 ADDQ R01,#1,R01 ; 0x5afac STQ R16,0(R00) ; 0x5afb0 CMPULT R01,R09,R18
0x5afb4 LDA R00,8(R00)  ; 0x5afb8 BNE R18,->0x5afa8
```
A memory-fill loop: destination R00 climbs unbounded (observed 0xc03de0b9, far past the
4 GB of RAM) storing R16 forever because the count R09 is garbage. That is the unalign
storm; every run halts at the identical cycle because it is a deterministic hang, killed
by the operator. Consequence: NO `UNHANDLED OUTER WRITE` (the hang precedes the IIC poke),
and ES40 never drives COM1 -> blank PuTTY while DS10/DS20 progress. **So the dynamic
"catch the IIC base via UNHANDLED OUTER WRITE" plan (Section 3) is BLOCKED.**

**AUTHORITATIVE SOURCE RESOLUTION (supersedes the empirical plan):** the apisrm SRM source
settles the IIC base directly. `ref/pc264_io.c`:
```
1229  int iic_write_csr(...) { outmemb(0, 0xfff80000 | addr, data); inportb(0,0x80); ... }
1246  char iic_read_csr(...) { data = inmemb(0, 0xfff80000 | addr); inportb(0,0x80); ... }
1282  iic_chip_reset(): outmemb(0, 0xfff80001, 0x80); outmemb(0, 0xfff80000, 0x5b); ...
```
The IIC controller (PCF8584) is HARDCODED at **0xFFF80000** (S0=base, S1=base|1), chip
select toggled by an I/O read of port 0x80. `pc264_io.c` is the SHARED pc264-family I/O
module (CLIPPER compiles under `#if(CLIPPER||PC264)`), so **ES40's IIC base == DS20's ==
0xFFF80000**. No measurement needed; no gp-relative disasm wall.

**FIX APPLIED (chipsetLib/TsunamiChipset.h):** added `{ "ES40", 0xFFF80000ULL }` to
`kIicBaseByModel` (comment cites pc264_io.c:1229), added `ES40` to `iicBaseRequired` (now
proven -> a future regression hard-stops instead of silently unmapping), and corrected the
stale "default to DS10 base" TODO. Needs a RelWithDebInfo rebuild.

**OPEN (verify after rebuild):** does mapping the IIC at 0xFFF80000 clear the 0x5afac
runaway loop? Two outcomes: (a) the loop's garbage bound came (directly or via early
iic_reset/iic_chip_reset) from the unmapped IIC returning all-ones -> mapping fixes the
hang and ES40 advances toward the badge/banner; (b) the hang is a SEPARATE under-modeled
register (memory-config / Cchip CSC strap -- note "Memory size 4096 MB / testing memory"
just precedes it) -> ES40 still hangs and we trace the R09/R00 data flow at 0x5afac next
(disassemble es40_v7_3.exe backward from 0x5afac). Re-run ES40 (rebuilt) and check: no
more "IIC left UNMAPPED"; does it get past 0x5afac; does GMEM-WATCH(0x2058) reach a member
store; does COM1/PuTTY come alive.
