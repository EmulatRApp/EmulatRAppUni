# 2026-06-13 -- "Halt Button is IN, BOOT NOT POSSIBLE": TIG CPU Halt Register

## Summary

First real `boot` attempt past the SRM `>>>` prompt (every prior milestone
stopped *at* the prompt) hit a hard wall:

```
>>> b dqa1
Halt Button is IN, BOOT NOT POSSIBLE
```

Root cause: the SRM/PALcode reads the **TIG CPU0 Halt Register** at
**PA 0x801_3000_03C0** (bit 0 = halt-pending). EmulatR did not model that
register, so the read fell through `TsunamiChipset::read()` to the generic
`mmioRead` all-ones default, returning `0x00000000FFFFFFFF`. bit 0 = 1 made
the firmware believe the front-panel Halt button was latched IN, so it
printed "Halt Button is IN, AUTO_ACTION ignored" at powerup and refused
`boot`.

Fix: carve the per-CPU TIG Halt Registers (CPU0 +0x3C0, CPU1 +0x5C0) out of
the TIG-bus decode and return 0 ("button OUT"), mirroring AXPBox. Writes
(the PAL's W1C halt-clear) are absorbed.

## How it was confirmed (live, no rebuild)

```
>>> e pmem:801300003C0
pmem:      801300003C0 00000000FFFFFFFF      <- bit 0 set => halt pending
>>> d pmem:801300003C0 0
>>> e pmem:801300003C0
pmem:      801300003C0 00000000FFFFFFFF      <- re-reads FFFFFFFF => live MMIO,
                                                not RAM; no console workaround
```

## Dead-ends ruled out first (recorded so nobody re-walks them)

1. **apisrm reference `halt_switch_in()` -> `pal$halt_switch_in`.** The
   reference console source (boot.c:744, pc264.c:1689) reads an impure-region
   flag `PAL$HALT_SWITCH_IN` (offset 0x220), set only by the PAL handler
   `sys__int_hlt` and cleared at reset. This is NOT what the shipped V7.3-2
   binary uses:
   - The running `PAL_BASE` is 0x8000 (load 0x900000 -> reloc 0x600000 ->
     0x8000; confirmed by the interval-timer divert target 0x8680 in the
     boot log). `get_base` = `PAL_BASE - pal$pal_base(0x8000)` = 0, so the
     reference flag address collapses to absolute 0x220.
   - `e pmem:220` = 0; depositing 0 changed nothing. The impure flag is zero
     and irrelevant.
2. **A spurious halt interrupt.** To set the impure flag the PAL must enter
   `sys__int_hlt`, which requires ISUM EI[4] (IRQ_HLT = 16 = bit 37). EmulatR
   provably never stages it: `stageInterruptDivert` does `cpu.isum = mask`
   (single-bit overwrite) and the only call sites stage bits 33 (err) / 34
   (dev) / 35 (timer). `HW_MFPR(HW_ISUM)` returns the stored value. So
   `sys__int_hlt` is unreachable; the flag mechanism cannot be the cause.

Conclusion from the above: the V7.3-2 DS10 binary decides halt from a
**hardware register**, not the impure flag.

## How AXPBox handles it (the reference that boots)

AXPBox (ES40 fork, full Tsunami, boots real SRM) models the TIG halt
registers as first-class state, zeroed at reset:

```cpp
// axpbox/src/System.cpp
state.tig.HaltA = 0;                          // :88  CPU0
state.tig.HaltB = 0;                          // :89  CPU1
...
case 0x300003c0: return state.tig.HaltA;      // TIG+0x3C0  CPU0 Halt Register
case 0x300005c0: return state.tig.HaltB;      // TIG+0x5C0  CPU1 Halt Register
```

The SRM reads 0 -> halt not asserted -> boots. EmulatR returned all-ones for
the same read. The TIG-bus base for these control registers is
0x801_3000_0000 per `EV6_OSF_PC264_PAL.MAR` (authoritative confirmation that bit 0 =
halt-pending and the 0x3C0 / 0x5C0 per-CPU layout).

NOT in SRMConsole.cpp: that file is a host-side command / CSERVE-I/O helper,
not the halt decider. The decision is firmware-on-CPU reading chipset MMIO,
so the fix belongs in the Tsunami/TIG decode.

## The fix

`chipsetLib/TsunamiChipset.h` -- new constants + predicate beside the
existing `isTigFlashAddr` helpers:

```cpp
static constexpr uint64_t kTigHaltCpu0 = 0x801300003C0ULL;  // 0x801_3000_03C0
static constexpr uint64_t kTigHaltCpu1 = 0x801300005C0ULL;  // 0x801_3000_05C0
static constexpr bool isTigHaltReg(uint64_t pa) noexcept {
    return pa == kTigHaltCpu0 || pa == kTigHaltCpu1;
}
```

`chipsetLib/TsunamiChipset.cpp`:
- `read()`  -- ahead of the `kMMIO_Start` / `mmioRead` branch:
  `if (isTigHaltReg(pa)) return { BusStatus::Ok, 0 };`
- `write()` -- ahead of the same branch: absorb (return Ok), no latch.

Note: these halt PAs are ABOVE the `kTigFlash` window
(`kTigFlashBase = 0x801_0000_0000`, size 0x800_0000), so the flash route does
not cover them -- the all-ones value was the generic `mmioRead` default, not
the factory-0xFF flash.

`tests/chipsetLib/test_systembus_arbiter.cpp` -- new SUBCASE (CHECK only):
reads of 0x801_3000_03C0 / _05C0 return Ok + data 0; a W1C write is absorbed
and the register still reads 0.

This models a DS10 with the Halt button OUT (the normal running state). A
future enhancement could make the halt line a settable input; 0 is the
correct default and what unblocks boot.

## Validation (Tim-side: build + rerun)

1. Build; run the chipset suite -- the new SUBCASE must pass.
2. `launch_vms_boot.sh` cold boot; at `>>>`:
   - `e pmem:801300003C0` should now read `0000000000000000`.
   - `b dqa1` should proceed into the OpenVMS V8.2 bootstrap instead of
     refusing.

## Forward

This blocker was UPSTREAM of the floating-point wall. Once `boot` proceeds,
OpenVMS V8.2 (dqa1 = D:\isos\alpha082.iso) is expected to wall on VAX float
(opcode 0x15), which is entirely absent in the fBox -- see the FP coverage
map (journals/fBox_FP_Coverage_Map_20260610.md) and deferred FP build-out
ticket #43.

## References (authoritative, path-qualified)

All apisrm paths are under
`D:\EmulatR\Processor Support\Palcode\palcode\apisrm\apisrm\ref\`:

- `ev6_osf_pc264_pal.mar` -- TIG base 0x801_3000_0000 (lda 0x8013 ; sll #28);
  lines 1264-1289 "TIG Register Usage": `801.3000.03C0 = CPU 0 Halt Register
  bit 0`, `801.3000.05C0 = CPU 1 Halt Register bit 1`; 2-CPU halt-IPI
  (`xor WHAMI,#1`).
- `boot.c` -- line 744 `if (halt_switch_in()) { printf(msg_no_boot); ... }`
  (the boot refusal gate).
- `pc264.c` -- lines 1689-1695 reference `halt_switch_in()` (reads the impure
  `pal$halt_switch_in`, the path RULED OUT for the V7.3-2 binary).
- `generic_messages.c` -- lines 939-940, the two "Halt Button is IN" strings
  (`msg_no_auto_action`, `msg_no_boot`).
- `stub_halt_switch_in.c` -- the stub `halt_switch_in() { return 0; }`
  (intended cold-power-up state = halt OUT).

AXPBox (reference emulator):

- `D:\EmulatR\axpbox\src\System.cpp` -- lines 87-89 (`state.tig.FwWrite/HaltA/
  HaltB = 0`, reset) and the `tig_read`/`tig_write` switch (smir 0x40 ->
  FwWrite; halt 0x3C0/0x5C0; DEFAULT 0).  NOTE: the actual boot gate is smir
  (0x40), not the halt-IPI regs -- see RESOLUTION below.

EmulatR (corrected fix):

- `chipsetLib\TsunamiChipset.h` -- isTigControlReg + window consts (kTigSmir /
  kTigHaltCpu0 / kTigHaltCpu1).
- `chipsetLib\TsunamiChipset.cpp` -- TIG-control default 0 / absorb in
  read()/write(); console-armed trace (kTigTraceArmReg) + HALTPROBE.
- `main.cpp` -- EMULATR_TRACE_WINDOW window-only sink.
- `tests\chipsetLib\test_systembus_arbiter.cpp` -- smir + halt + window read 0.
- Memory: `project_tig_halt_register_boot_refusal`.

## RESOLUTION (2026-06-13, console-armed trace)

The first fix (per-CPU halt-IPI regs 0x3C0/0x5C0 -> 0) was the WRONG register:
it made those read 0 (and shook loose dqa0 enumeration) but `b dqa1` still
refused.  The console-armed trace instrumentation (kTigTraceArmReg
`e pmem:80130000FF8` + auto HALTPROBE) caught the actual gate in one run:

    HALTPROBE: TIG read pa=0x80130000040 w=8 v=0xffffffff

The front-panel Halt gate is **TIG+0x40 = smir** (AXPBox tig_read case
0x30000040 -> state.tig.FwWrite, reset 0).  EmulatR modeled NONE of the TIG
control registers, so smir fell through to the all-ones mmioRead default and
the firmware read it as "Halt Button is IN".  This also resolves the long
contradiction: the shipped V7.3-2 binary reads the **hardware smir register**,
NOT the apisrm-source `pal$halt_switch_in` impure flag -- that source
inference (and the EI[4] interrupt theory) were dead-ends.

FIX (landed, pending build): model the whole TIG control window
(0x801_3000_0000 .. +0x1000) to read 0 / absorb writes -- `isTigControlReg`
in TsunamiChipset.h, used in read()/write() ahead of the mmioRead branch
(trace-arm reg decoded first).  Mirrors AXPBox tig_read's DEFAULT 0.  Doctest
extended to smir + halt + window in test_systembus_arbiter.cpp.

Blanket-0 is a simplification (no read-back store / clr_* semantics).  The
faithful AXPBox-parity R/W model is task #11, to land once boot is validated
past the halt gate and the trace shows which TIG regs the OS install touches.

Validate: rebuild; at >>> `e pmem:80130000040` should read 0; `b dqa1` should
proceed into the OpenVMS bootstrap (expect the FP wall next).
