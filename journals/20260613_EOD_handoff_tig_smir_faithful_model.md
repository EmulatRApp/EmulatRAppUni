# 2026-06-13 EOD -- Halt gate root-caused (smir) + faithful TIG-bus model

## Headline

First real `boot` attempt past `>>>` (every prior milestone stopped AT the
prompt).  `b dqa1` was refused "Halt Button is IN, BOOT NOT POSSIBLE".  Pinned
the gate to **smir = TIG+0x40** via console-armed tracing, and landed a
**faithful TIG-bus device model** (TsunamiTig) that makes smir read 0 = no
halt.  Pending build + boot validation; the FP/VAX-float wall (#43) is the
expected next blocker.

## The day, in order

1. **OpenVMS wired.** dqa1 ATAPI CD -> `D:\isos\alpha082.iso` (OpenVMS Alpha
   V8.2, ODS-2 confirmed) in `ds10_platform.win` (+ both build copies).
2. **Memory fix confirmed LIVE.** Banner now `1024 Meg of system memory` +
   `memory: using [System] memorySize from ini` -- the 64->1024 SSOT fix is
   engaged (top open thread closed).
3. **Firmware path fix.** `[ROM] firmwareImage` -> `firmware/ds10_v7_3.exe`
   (POST_BUILD deploys it to firmware/, ini said bare name -> SrmLoader
   "file not found").  3 ini copies.  Instance of ticket #42 (run-dir rooting).
4. **dqa0 enumerates** now (`show dev` lists DQA0 EMULATR VIRTUAL DISK) --
   shook loose during the rebuilds; the S4 probe gap appears closed.
5. **Halt-gate hunt** (the bulk):
   - WRONG first fix: per-CPU halt-IPI regs 0x3C0/0x5C0 -> 0 (AXPBox-parity).
     Correct hygiene (reg reads 0) but NOT the gate -- boot still refused.
   - Ruled out (cost real time, recorded): the apisrm-source
     `halt_switch_in()->pal$halt_switch_in` impure-flag path, and an EI[4]
     spurious-halt-interrupt theory.  The shipped V7.3-2 binary reads HW, not
     the source's impure flag.
   - CONSOLE-ARMED TRACE instrumentation: kTigTraceArmReg (`e pmem:80130000FF8`
     arms the retire window from the prompt) + auto HALTPROBE (logs nonzero
     TIG reads) + main.cpp EMULATR_TRACE_WINDOW window-only sink (no GB
     cold-boot stream).  One run pinned it:
         HALTPROBE: TIG read pa=0x80130000040 w=8 v=0xffffffff
   - ROOT CAUSE: **smir (TIG+0x40)** -- EmulatR modeled NO TIG-bus device
     registers, so smir fell to the all-ones mmioRead default and the firmware
     read it as "Halt Button is IN".
6. **Faithful TIG-bus model.** New `chipsetLib/TsunamiTig.h` -- clean-room from
   DEC sources (tsunami_io.c xtig addressing; pc264*.c intig/outtig sites;
   EV6_OSF_PC264_PAL.MAR halt regs; regatta logout `tig_smir`).  AXPBox was a
   cross-check oracle ONLY -- no copied registers or values.

## TsunamiTig design (with the review decisions)

- **smir (0x40)**: status; reads literal 0, write is a true no-op (NO backing
  store -- a stored W1C handing back nonzero would re-assert halt).  TODO
  defers the real thing: front-panel-halt + SMI/logout EVENT INJECTION.  (D2)
- **halt0/1 (0x3C0/0x5C0), ipcr0..3 (0xA00+), arb_ctrl (0x3800_0100)**: R/W
  storage.  ipcr carries the SMP limitation note (storage-only, no IPI
  injection -> ES40/ES45 secondary-CPU startup would stall).  (Add 2)
- **rev regs (0x3800_0140/0180/01C0)**: read-only 0 _PROVISIONAL -- XREF
  confirmed display/store-only (show_config_pc264.c:346, galaxy_pc264.c:373;
  no firmware gate on (val>>5)&7).  Not AXPBox's 0xfe.  (D1)
- **catch-all miss**: 0 / absorb, but gated by **EMULATR_TIG_TRACE** (cached
  getenv) so an unmodeled-but-touched reg surfaces LOUDLY in bring-up instead
  of hiding behind a plausible 0 (the exact failure mode that hid smir).  (Add 1)
- **Snapshot**: deferred but ASSERT-GUARDED -- TsunamiTig::isAtResetState() +
  spdlog::warn in Snapshot.cpp; proves "transient/0" or catches arb_ctrl loss
  loudly the day it trips.  (D3)
- Wired into TsunamiChipset (m_tig member/accessor/reset; read/write decode
  ahead of mmioRead, trace-arm reg first); replaced interim blanket-0
  isTigControlReg.  Doctest rewritten (smir no-store, halt/ipcr R/W fidelity,
  rev 0, default-0-not-NXM).

## Files changed (source)

- NEW chipsetLib/TsunamiTig.h
- chipsetLib/TsunamiChipset.h, .cpp (TIG model wiring; halt-IPI carve-out;
  console trace-arm reg + HALTPROBE)
- main.cpp (EMULATR_TRACE_WINDOW window-only DecListingSink)
- systemLib/Snapshot.cpp (TIG assert-guarded defer)
- tests/chipsetLib/test_systembus_arbiter.cpp (TIG subcase)
- ds10_platform.win + out/build/{release,relwithdebinfo}/ds10_platform.win
  (dqa1 -> alpha082.iso)
- config/EmulatrV4.ini + 2 build copies ([ROM] firmwareImage -> firmware/...)
- journals/20260613_halt_switch_tig_register.md (investigation) + this handoff

## Open tasks

- #5  halt-switch boot refusal -- root-caused + faithful fix landed; CLOSE when
      boot validated past the gate.
- #10 gate C970-LOADWATCH/STOREWATCH/DIVERT-REI MISMATCH/PCSAMPLE stderr behind
      env var(s) (hot-path: cached static bool).
- #11 faithful TIG model -- DONE (pending build).
- #43 (in memory.md) Floating-Point build-out -- the EXPECTED next wall once
      boot proceeds (VAX G/F float opcode 0x15 absent).

## Next session

Build -> chipset suite (TIG subcase) -> `EMULATR_TIG_TRACE=1 ./trace_halt.sh`.
At >>>: `e pmem:80130000040` should read 0; `b dqa1` should cross into the VMS
bootstrap.  If it proceeds, capture where VMS itself walls (expect FP).  If a
NEW TIG reg is touched, EMULATR_TIG_TRACE will name it.

## Addendum -- run-helper persistence (cmake regen casualty)

cmake cache regen wipes the build dir, deleting the run-helper scripts that
had only existed there.  Fix: the canonical scripts now live in source
`Emulatr/tools/` and are deployed to `run-dir/tools/` by CMake POST_BUILD
(added to `EMULATR_TOOL_FILES`, alongside mkdisk.py).  Each script anchors to
the run-dir ROOT via `RUN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"`, so from tools/
it still finds `Emulatr.exe`, `firmware/`, and the manifest.

- `tools/trace_halt.sh`      -- console-armed retire trace; sets
  EMULATR_TRACE_WINDOW=1 + EMULATR_TIG_TRACE=1 (canary).
- `tools/launch_vms_boot.sh` -- plain cold boot to >>> for `b dqa1`.
- `tools/build_test_run.sh`  -- cmake --build + chipset doctest + hand off.

New invocation (from the build dir): `./tools/trace_halt.sh` (or
`bash tools/trace_halt.sh` if the Windows copy drops the exec bit).  Files:
`tools/*.sh`, `CMakeLists.txt` (EMULATR_TOOL_FILES).  Stray `scripts/` dir from
an interim attempt is inert -- `rm` when convenient.

## Post-build run -- smir fixed but boot still refused; lead = clr_irq4

Rebuilt with TsunamiTig.  `e pmem:80130000040` now reads 0 (smir fixed), BUT
`b dqa1` still refused "Halt Button is IN, BOOT NOT POSSIBLE".

The EMULATR_TIG_TRACE canary caught the real thread:
    EMULATR_TIG_TRACE: unmodeled TIG WRITE pa=0x80130000440 off=0x440   (x4)
TIG+0x440 = **clr_irq4** (clear interrupt 4).  On PC264 **IRQ4 is the halt
line**, so the firmware sees the halt INTERRUPT pending, writes clr_irq4 to
clear it, our model no-ops the write -> it stays asserted -> halt persists.
So smir(0x40)=0 was necessary but NOT sufficient: the halt is an interrupt the
firmware cannot clear, not (only) a status read.

NEXT SESSION (the fix shape):
1. Model the clr_irq* family (TIG 0x400 clr_irq5 / 0x440 clr_irq4 / 0x480
   clr_pwr_flt_det / 0x4C0 clr_temp_warn / 0x500 clr_temp_fail) to actually
   CLEAR the matching interrupt source (write-1/write-any-to-clear).
2. Find what leaves IRQ4 (halt) ASSERTED -- almost certainly a Cchip
   DRIR/DIR bit (0x801_A000_02xx / 0x300), OUTSIDE the TIG window the canary
   covers.  Examine: `e pmem:801a0000300` (DRIR) / `e pmem:801a0000280` (DIR0)
   at >>>, or a PROPERLY-ARMED retire trace.
3. Wire clr_irq4 -> clear that source, and ensure nothing spuriously
   re-asserts the halt IRQ4.

TRACE-CAPTURE GOTCHA (recorded): the console-armed retire window only works
when the run has EMULATR_TRACE_WINDOW=1 (constructs the window-only
DecListingSink).  Use tools/trace_halt.sh (sets it) or
`EMULATR_TRACE_WINDOW=1 EMULATR_TIG_TRACE=1 ./Emulatr.exe ...`.  Arm with a
READ (`e pmem:80130000FF8`, returns 7FFFFFFFFFFFFFFF); a DEPOSIT of 0 to that
PA DISARMS.  Stop cleanly with `touch <run-dir>/EMULATR_STOP` to flush _srm.trc.
