<!--
EmulatR V4 -- ES40 REACH-PROMPT: console is HEALTHY, output lands on an UNBACKED
UART port (2026-07-08)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: capture the decisive finding that the ES40 SRM console runs to near-
completion but every byte is dropped because V4 leaves the console UART port
(0x2F8) unbacked.  Establishes the authoritative console-wiring answer (COM1 vs
COM2), the PCI-I/O window fact, and the one-line-scope fix.  ASCII(128).
Discuss-before-code stands.
-->

# ES40 REACH-PROMPT -- console healthy, output lands on an unbacked port (2026-07-08)

## TL;DR

After the CSERVE-0x66 SCB fix, the ES40 SRM is **healthy and streams its full
console-init banner**, but every character is written to the UART at ISA base
`0x2F8` -- a port V4 never `setBackend()`s -- so PuTTY stays blank.  This is a
console-WIRING gap, not a firmware wedge.  The earlier `R16=0x80000d0000000000`
ACV at the retire cap is INCIDENTAL: the console keeps emitting characters right
up to `--max-cycles`.  Fix scope: give ES40's `0x2F8` console port the same
backend COM1 (`0x3F8`) already gets.

## Decisive evidence (es40_uartwatch.log; HOOKA_VA + UARTWATCH + SPINSKIP run)

Reconstructed COM2-THR (`0x2f8`) byte stream, ~1000 writes, all DROPPED:

    starting console on CPU 0..
    initialized idle PCB..
    initializing semaphores..
    initializing heap..  inital heap 240c0..
    memory low limit = 60e00  heap = 240c0, 17fc0..
    initializing driver structures..
    initializing idle process PID..
    initializing file system..
    initializing hardware..
    initializing timer data structures..
    lowering IPL..
    create dead_eater..  create poll..  create timer..  create powerup..
    access NVRAM..  M...

That is the normal SRM console bring-up running to near-completion (idle PCB,
heap, drivers, file system, hardware, timers, IPL lower, the dead_eater/poll/
timer/powerup processes, NVRAM access).  The firmware is NOT stuck.

- Every CSERVE register dump carries `R20 = 0x00000801fc000000` = the Pchip0 PCI
  I/O window base (HRM Table 10-1, `801.FC00.0000`).  The console UART is accessed
  at `PCI-I/O-base + 0x2f8` (`0x801fc0002f8`).  T. Peer's PCI instinct was right
  about the PATH: the UART sits behind Pchip0 PCI I/O.  The ADDRESSING is correct
  -- it is not the blocker.
- `THRWATCH port=COM2 base=0x2f8 val=0x.. ch='..'` fires for every banner byte;
  `writeTHR` on the `0x2f8` object has `m_backend==nullptr`, so `putChar` is never
  called.  V4 only `setBackend`s the `0x3F8` object (`Machine.cpp:417`); the
  `0x2F8` object stays unwired (`TsunamiChipset.h:863`).

## Authoritative console-wiring answer (COM1 vs COM2)

`Processor Support/.../ref/combo_driver.c`, `combott_init()`:

- `com_devtab[] = { {"tta0", COM1, COM1_VECTOR}, {"tta1", COM2, COM2_VECTOR} };`
  Struct field 2 is `port` = the UART base macro.
- Non-Galaxy / non-Rawhide path (== ES40/pc264) wires the primary console:
  `console_ttpb = com_devtab[0].ttpb;`  (source comment: "wire it to COM1";
  "primary console is now on line").
- Console writes: `combott_txsend(port,c) -> combo_outportb(cp->port + THR, c)`.
  So the console byte address = `com_devtab[0].port + THR`.
- ISA bases (`smcc669_def.h`): `COM1_BASE 0x3F8`, `COM2_BASE 0x2F8` (standard).

Reconciliation: the source wires console to `com_devtab[0]`, and V4 empirically
sees the banner at `0x2F8`.  Therefore, in the ES40 firmware build,
`com_devtab[0].port` resolves to `0x2F8` -- i.e. the ES40 primary console UART is
the port ISA-decoded at `0x2F8`.  (The `COM1`/`COM2` devtab macros are NOT defined
in `apisrm/ref`; they come from the pc264/ES40 platform header at build time --
the single loose end to confirm.)  V4 backs `0x3F8` but the firmware drives
`0x2F8` => blank PuTTY.

## RULED OUT / SETTLED -- do NOT re-chase

- **CSERVE 0x66 as a no-op is CORRECT.**  `pal.mar sys__cserve` `hw_ret`s undefined
  codes with R0 untouched; V4 now does exactly that (`CSERVE Defaulted -
  UnImplemented`).  The repeated `0x66` calls are benign, not the console path.
- **`R16=0x80000d0000000000` ACV is INCIDENTAL, not the blocker.**  The console
  streams characters continuously up to the retire cap; the run ends on
  MaxCyclesExceeded, not a halt.  Bits<43:0> of that value have bit<43> set (PIO
  per HRM), bit 63 set makes it non-canonical -- park it; revisit only if it
  survives the backend fix.
- **PCI addressing is NOT wrong.**  Console UART reached via correct Pchip0 PCI
  I/O base `0x801fc000000`.

## Fix (proposed; discuss-before-code)

Give the ES40 console UART object at `0x2F8` the same console/PuTTY backend the
`0x3F8` object gets (`Machine.cpp:417` `com1().setBackend`).  Options: (a) for
model=ES40, `setBackend` on the `0x2F8` port; or (b) resolve the console port
from the platform manifest / firmware console-base rather than hard-coding COM1.
Prefer (b) for faithfulness once the pc264 `COM1` macro value is confirmed;
(a) is the minimal visibility unblock to see the banner land.

## Reproduction

`EMULATR_HOOKA_VA=0x80000d0000000000 EMULATR_UARTWATCH=1 EMULATR_2D_NOOP=1
EMULATR_SPINSKIP=1 ./Emulatr.exe --firmware firmware/es40_v7_3.exe
--mem 4294967296 --no-autoload --max-cycles 0x50000000 2> es40_uartwatch.log`
-> grep `THRWATCH port=COM2` and reconstruct `ch=` bytes.

## Reference

- `20260708_es40_scb_base_mismatch_root.md` -- the SCB fix (this session).
- `20260708_es40_reach_prompt_frontier.md` -- prior hand-off (superseded on the
  "COM2 spin" framing: it is not a spin, it is dropped output).
- HRM Table 10-1 (`Processor Support/tsunami_typhoon_21272_hrm.txt:16340`) --
  system address map; bit<43>=1 selects PIO; `801.FC00.0000` = Pchip0 PCI I/O.

## Standing rules

Discuss-before-code; header + inline docs citing HRM/source + task id; ASCII(128);
surgical Edit; V0/V1/V2 + Processor Support read-only; bounded trace tails only;
full suite + DS10 + DS20 + ES40 boot gate before any core/chipset commit;
`EMULATR_BRINGUP_PROBES=OFF` for release.
