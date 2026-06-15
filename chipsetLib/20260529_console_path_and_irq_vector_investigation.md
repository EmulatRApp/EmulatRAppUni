<!--
EmulatR V4 -- Console Output Path + Interrupt Vector Investigation
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-05-29
Purpose: hand-off from the claude.ai web variant to Cowork. INVESTIGATION
ONLY -- gather facts, report back, do NOT land edits. The web variant
decides the edits after Cowork reports. ASCII(128) only.
-->

# Cowork investigation: why the run reaches the >>> cusp but never prints

## What this is

Investigation hand-off, not a change request. Run the steps, report the
structured findings at the end, and stop before editing. The web variant
turns the findings into a diff proposal (discuss-before-code).

Treat the file paths and any line numbers here as a point-in-time
snapshot. Cowork is the source of truth for current file state; verify
every path and re-derive line numbers locally.

## Symptom (point-in-time, from the 2026-05-29 21:01 run console)

The run is well past the boot halt and servicing a periodic interval
timer interrupt (MISC<ITINTR> CPU0, ~2^20-cycle cadence), making forward
progress (savedPc varies per tick: 0x775a0, 0x1c6788). It appears to be
right at the cusp of the SRM `>>>` prompt but the prompt never appears.
Two open questions block the diagnosis.

Repeated noise: `TsunamiPchip: I/O read unhandled port 0x0080`, ~16 reads
per timer tick. Per the 21272 HRM, port 0x80 is NOT a Tsunami register --
it is downstream PCI/ISA I/O space the Pchip forwards. On real hardware
port 0x80 is the ISA DMA-page / POST / I/O-delay register. So these reads
are almost certainly delay padding (the inb(0x80) idiom) around some real
ISA access, not the device of interest. The device of interest is the
console UART, which lives behind Pchip -> PCI -> PCI-ISA bridge -> SuperIO
-> 16550 at COM1.

## Part A -- Interrupt vector routing (possible real bug, or stale log label)

The divert log reads `target=0x8680 (palBase + 0x100)`. Note 7.4 in
`Tsunami_HRM_vs_AXPBox_Profile.md` flagged `palBase + 0x100` as
DTBM_DOUBLE_MISS_3, which is WRONG for a hardware interrupt; the EV6
INTERRUPT entry should be 0x680. The low 15 bits of 0x8680 are 0x680, so
one fact resolves everything: is palBase 32K-aligned?

A1. Report current palBase (from CpuState, the latest *.axpsnap, or a
    --dump path). State whether it is 32K-aligned (bits<14:0> == 0).
    - palBase == 0x8000 (aligned) => divert offset must be 0x680 = correct
      INTERRUPT vector; the `(palBase + 0x100)` text is a STALE LOG LITERAL
      and routing is fine.
    - palBase low-15 == 0x580 (misaligned) => offset is still 0x100 = wrong
      DTBM_DOUBLE_MISS_3 vector AND palBase violates EV6 alignment (two
      problems).

A2. Locate the interval-timer divert call site (per prior notes:
    `Machine.cpp::stageInterruptDivert`, the code that emits the
    `interval-timer divert[...]` line). Report the literal offset it adds
    to palBase (0x100 or 0x680?) and whether that offset is a symbol from
    `coreLib::ev6::` or a hardcoded magic number.

A3. Confirm the canonical INTERRUPT entry offset in
    `coreLib/Ev6EntryVectors.h` (expected 0x680; NOT 0x100 =
    DTBM_DOUBLE_MISS_3). Note whether the divert site already uses that
    symbol or a magic number.

Verdict to return: "vector OK, log label stale" OR "vector wrong, offset
still 0x100" OR "vector wrong + palBase misaligned".

## Part B -- Console output path fork (the >>> blocker)

There are two ways SRM emits the prompt. The project already wires one of
them: CSERVE PUTS -> ConsoleManager -> StdoutConsoleBackend on OPA0
(see `palBoxLib/grains/PalEntries.cpp` CSERVE leaf,
`deviceLib/ConsoleManager.h`, `deviceLib/global_ConsoleManager.h`).

  Path 1 (CSERVE PUTS): PALcode-mediated console. Already handled -- bytes
  go to stdout. If the prompt uses this and we do not see it, the bug is
  in the PUTS / ConsoleManager path, not a missing device.

  Path 2 (direct 16550 UART): SRM banging the UART at COM1 directly --
  write char to THR, poll LSR for THRE (transmit holding register empty),
  repeat. Needs a UART model behind the bridge; none exists yet.

Failure hypothesis for Path 2: an unclaimed PCI I/O read master-aborts and
returns all-ones, so an LSR read returns 0xFF, which has THRE (bit 5) and
TEMT (bit 6) SET. SRM reads "transmitter ready," writes the char, the
write lands on an unowned port, and the byte vanishes. SRM believes it
printed `>>>` and proceeds -- which matches "at the cusp, never seen,"
and matches the forward-progress signal (savedPc varies, so SRM is NOT
stuck in a TX-ready spin; if the LSR returned 0x00 it would spin in one
poll loop every tick, which it does not).

Standard 16550 register map (PC-compatible COM1 base 0x3F8; VERIFY the
actual base against the platform / SRM console source -- may differ):
  base+0  THR(w) / RBR(r) / DLL(DLAB=1)   -> 0x3F8
  base+1  IER / DLM                        -> 0x3F9
  base+2  IIR(r) / FCR(w)                  -> 0x3FA
  base+3  LCR                              -> 0x3FB
  base+4  MCR                              -> 0x3FC
  base+5  LSR (bit0 DR, bit5 THRE, bit6 TEMT) -> 0x3FD
  base+6  MSR                              -> 0x3FE
  base+7  SCR                              -> 0x3FF

B1. Capture ONE gated trace window (~2^20 cycles spanning a single
    interval-timer divert, near the current run frontier where it sits at
    the >>> cusp). Bounded window ONLY -- never whole-file grep a
    multi-GB trace (it wedges the sandbox). Resume from the newest
    *.axpsnap and trace forward a bounded span if that is the cleanest way
    to land on the cusp.

    From that window report ALL I/O-space port accesses, and call out
    specifically:
    - Direct UART: any writes to THR (0x3F8) and reads of LSR (0x3FD),
      plus IER/IIR/FCR/LCR/MCR (0x3F9-0x3FC). What value does the LSR read
      return?
    - CSERVE PUTS: is the PUTS leaf entered (CSERVE function 0x02 / the
      PUTS dispatch in PalEntries.cpp) with the prompt buffer? Does
      ConsoleManager / StdoutConsoleBackend actually receive bytes?
    - SuperIO config: accesses to a config index/data port pair preceding
      serial use (commonly 0x2E/0x2F; platform-specific -- verify). If the
      SuperIO must be configured before the UART responds and that config
      is unmodeled, the UART never appears.
    - The instruction PC(s) issuing the port-0x80 reads, and the port
      accessed immediately before and after each (confirms 0x80 is delay
      padding around a real access).

B2. Return the fork as a single verdict:
    "console path = CSERVE PUTS (ConsoleManager receiving: yes/no)" OR
    "console path = direct 0x3F8 UART (LSR read returns 0xNN)".

## Report back (structured -- this is the whole deliverable)

A. Vector: palBase value + aligned?; divert offset literal + symbol vs
   magic number; Ev6EntryVectors INTERRUPT value; vector verdict.
B. Console: fork verdict; LSR return value (if Path 2); PUTS reaching
   ConsoleManager (if Path 1); any SuperIO config-port accesses observed.
C. The per-tick I/O port list from the gated window, with the port-0x80
   neighbors and issuing PC(s).

Do NOT edit yet. The web variant proposes the diffs from these findings.

## Anticipated edits (destination only -- for context, not to land now)

- Vector wrong: route the divert through the `coreLib::ev6::` INTERRUPT
  symbol (0x680), mark _PROVISIONAL until cross-checked against
  EV6_DEFS.MAR; if also misaligned, fix the palBase seed.
- Vector OK but label stale: correct the divert log literal only.
- Console = direct UART with all-ones LSR: add a minimal 16550 model
  behind the bridge whose THR drains to the SAME ConsoleManager / stdout
  sink PUTS already uses; plus a PCI master-abort default for unclaimed
  I/O reads (return all-ones) and gate the unhandled-port log.
- Console = CSERVE PUTS but no bytes out: debug the PUTS / ConsoleManager
  path.
- Port 0x80 regardless: benign master-abort default + rate-limit / gate
  the warning (high-frequency line per trace discipline).
- Cosmetic: CSR logger prints cyc=0 / cpu=-1 -- thread the real cycle/CPU
  into the CSR log path so reads correlate with divert cycles.

## Conventions for any follow-up edit

Gated/bounded trace windows only; verify file writes via bash
(wc -l / grep); ASCII(128) only; include guards (not #pragma once);
doctest CHECK only; no toString helper (use <type>Name + operator<<);
header block + inline line comment on every change; _PROVISIONAL on any
guessed scbd / offset / bit until HRM/defs-verified; surgical Edit over
rewrite; discuss-before-code (propose the diff before landing).
