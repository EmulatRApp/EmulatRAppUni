# DS20 first boot -- console-idle / secondary-CPU rendezvous stall (2026-06-18)

## Summary

First real DS20 boot (`ds20_v7_3.exe`, model=DS20, 4 GiB, release Jun-15
binary). Runs clean to 70B+ cycles but prints only the two earliest SRM
strings then HOT-idles forever, no `>>>`. Diagnosed without a debugger as
**console idle with the POST/init thread blocked -- most likely a
secondary-CPU rendezvous** (DS20 is dual-EV6; only CPU0 is modeled). This is
the DS20 dual-CPU bring-up task, the first non-DS10 machine.

## Console

    ASA EmulatR -- Alpha AXP (EV6 / 21264) Emulator   (EmulatR's own banner)
    *** keyboard not plugged in...                    (SRM)
    Flash ROM writes are disabled                     (SRM)
    <nothing further; no memory/eerom/SROM POST output, no prompt>

cycleCount keeps advancing -> hot idle, not a freeze. Tsunami-class so the
Jun-15 binary is faithful (the Titan/ALi chipset work is ES40/ES45 only).

## How it was diagnosed (run left live)

1. **Live PC from snapshots.** Parsed the periodic `.axpsnap` header to read
   `pc` at file offset 892, self-checked against `cycleCount` at offset 28 vs
   the filename. (Recipe banked in memory `reference_snapshot_pc_extraction`.)
   Sampling several snapshots showed PC bouncing across a small set:
   native scheduler `0x1ad6xx-0x1adbxx`, RSCC PAL delay `0xb744` (pal=1), and
   timer_check `~0x7da7c`. A loop, not a single-instruction hang.

2. **Static analysis of the decompressed image.** Built the host_decompressor
   oracle on `ds20_v7_3.exe` ->
   `tools/host_decompressor/out/decompressed_ds20_v7_3.bin`
   (base 0x8000, 3,119,616 bytes; DS10 is 3,197,952; only ~14% byte-identical,
   so addresses do NOT map between the two builds).

## Structure (DS20 VAs, base 0x8000)

- `0x1ae200` idle loop: `BSR v0,0x1ae208`; no-ready fall-through at `0x1ae204`
  = `CALL_PAL 0x08` (console idle/wait).
- `0x1ae208` dispatcher: `t2=*(v0)`, reads `*(t2+0x3c0)` / `*(t2+0x3c8)`
  (run-queue / dispatch object), calls `*(pv+8)(0,1)`, jumps to `0x1adabc`.
- `0x1adabc-0x1adb10` context-restore: LDQ full callee-saved set
  (s0-s5, FP, GP, SP, ra, pv) = thread context switch.
- `0x7d9d8` / loop `0x7da68` timer_check: deadline wait; repeatedly calls
  scheduler-yield `FUN_001ad928`, compares `[GP+8]` (now) vs `[GP+0x10]`
  (limit).
- `0x164ba0` = DATA (console-var descriptor: `"P%02d>>>"` default, `"prompt"`,
  `"P%02d%s"`, `"default"`, fn ptr `0xb65e0`). Ghidra mis-renders it as code.
  DS10's equivalent prompt is at `0x16fc10` -- shifted, confirming no reuse.

## Conclusion

Console reached idle (krn$_idle scheduler is healthy). The init/POST thread
blocked (no further POST strings => POST never finished); the idle thread runs
forever, so the console shell is never created and no prompt is emitted.
Leading cause: **secondary-CPU rendezvous**. V4's HWRPB builder correctly
advertises `cpuCount=1`, but the SRM self-builds its own HWRPB from hardware
probing, and `ds20_platform.json` already warns "keep cpuCount=1 to avoid SRM
secondary spin." The DS10 shell-wakeup blocker (UART interrupts) is already
fixed in this binary, so DS20 is stuck EARLIER than the shell read.

## Next

1. **DS10 control run** to `>>>` on this same binary (`run_fw.sh ds10`, drop
   `--mem` -> 1 GiB). If DS10 reaches the prompt and DS20 idles, the delta is
   provably DS20-specific in one run.
2. **DS20 dual-CPU bring-up:** find what the init thread waits on (secondary
   presence / IPI handshake) and either make the firmware see a true
   uniprocessor at that probe, or model CPU1 startup. Real task, not a patch.

## Gotchas

- Ghidra's Alpha decompiler is unreliable here (emits asm / garbage C); expect
  asm-level or dynamic tracing for the wait condition.
- The 70B-cycle auto snapshots are resume points but re-enter the same idle;
  snapshot once and iterate the fix rather than re-grinding.
- Linux/D: mount served STALE log views mid-session; the live PC/cycle were
  read via the Read tool (host view), not bash byte counts.
