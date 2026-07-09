<!--
EmulatR V4 -- ES40 console COM2 backend gate: LANDED + VERIFIED, plus the
next frontier (2026-07-08)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
Purpose: session journal for the ES40 console-wiring fix. Records the root
cause (V4 backed only COM1/0x3F8 while the pc264 SRM consoles on COM2/0x2F8),
the capability-gated fix, the design rationale (why NOT SbAli), the in-guest
verification (banner lands in PuTTY), a data-fidelity bug found in the es45/
ds25 manifests, and the newly-exposed memory-test frontier. ASCII(128).
Discuss-before-code stands.
-->

# ES40 console COM2 backend gate -- landed, verified, next frontier (2026-07-08)

## TL;DR

The ES40 SRM console banner now streams to PuTTY. Root cause of the prior
blank console: V4 only `setBackend`d COM1 (ISA 0x3F8), but the ES40/pc264
firmware drives its primary console on COM2 (ISA 0x2F8), so every banner byte
was dropped at an unbacked UART. Fix: a new model-granular capability,
`PlatCap::ConsoleUartCom2`, that backs the 0x2F8 UART with the same host sink
COM1 gets. Verified in-guest: full SRM bring-up plus "Memory size 4096 MB /
testing memory". Boot now advances into the memory test, exposing the next
blocker (an access-violation, `kFaultAcv`, distinct from the benign DTB
misses the test also generates).

## The fix (2 files)

- `systemLib/PlatformCapabilities.h`
  - New enum bit `PlatCap::ConsoleUartCom2 = 1u << 7` -- names the console-
    wiring AXIS ("primary console on the second UART, 0x2F8"), not a per-model
    bit.
  - New `enum class ResolvedModel { Unknown, DS10, DS15, DS20, ES40, ES45 }`
    -- the derived runtime model identity.
  - New private helper `resolve(iniModel, manifestPlatform)` -- reconciles
    Channel A (ini [System] model) with Channel B (<stem>_platform.json
    "platform", which wins when present); token match, specific-first.
  - `derive()` now consumes the reserved `(void)model` hook: sets
    `ConsoleUartCom2` only for `ResolvedModel::ES40`. ES45/DS15 excluded
    pending confirmation of their console wiring.
- `systemLib/Machine.cpp`
  - After the capability latch (`m_caps = derive(...)`), gate the COM2
    backend: `if (platHas(PlatCap::ConsoleUartCom2)) com2().setBackend(
    &m_com1Backend);`. Placed AFTER the latch by necessity -- the pre-existing
    `com1().setBackend` at the top of the ctor runs before caps are latched, so
    reading `platHas()` there would gate nothing.
  - Updated the now-stale "INERT -- no call sites yet" comment on the latch to
    record this as the first call site.

## Design rationale (why NOT SbAli)

The first instinct was to gate on the south-bridge capability `SbAli`. Rejected
for two reasons:
1. It is inert for ES40: the `es40_v7_3_platform.json` manifest deliberately
   uses a Cypress `isa_bridge` STAND-IN ("real ES40 south bridge is ALi
   M1543C; EmulatR has no ALi model yet"), so `SbAli` never latches. Gating on
   it would silently back nothing.
2. It is the wrong axis: DS15 and ES45 share chipset AND south-bridge lineage,
   so a bit sourced from a shared hardware trait would sweep in models that do
   not console on 0x2F8. The console-wiring axis must be keyed to the DERIVED
   RUNTIME MODEL, at model granularity, with the single model comparison
   centralized in `derive()` (never a raw `model == "ES40"` at a call site, per
   the PlatformCapabilities contract).

The capability mask is a `uint32_t` (32-bit) -- a 64-bit was considered during
original design but changed to 32 in implementation; 7 of 32 bits used.

## Verification

- Run: `./Emulatr.exe --firmware firmware/es40_v7_3.exe --mem 4294967296
  --no-autoload --max-cycles 0x50000000 2> es40_hooka.log`
- `putty_console_p10023_20260708194234.log`: full console bring-up (idle PCB,
  heap, drivers, file system, hardware, timers, lower IPL, dead_eater/poll/
  timer/powerup, NVRAM) then "Memory size 4096 MB / testing memory".
- Gate fires because `es40_v7_3_platform.json` badges `"platform": "ES40"`, so
  `resolve()` returns `ResolvedModel::ES40`.

## Data-fidelity bug found (filed, NOT fixed here)

- `es45_v7_3_platform.json` badges `"platform": "ES40"` (should be ES45).
- `ds25_v7_3_platform.json` badges `"platform": "DS20"` (should be DS25).
The es45 mislabel defeats the ES45 exclusion: booting es45 firmware would
resolve to ES40 and back COM2 even though ES45 console wiring is unconfirmed.
The model-granular gate is only as trustworthy as the manifest badge.

## Next frontier -- the memory test

Boot now reaches the SRM memory test and faults. Decisive decode from a
targeted DIAG-PC run (`EMULATR_DIAG_PCLO=0x1b7dd4 EMULATR_DIAG_PCHI=0x1b7dd4
EMULATR_DIAG_CAP=40 ...`):

- Faulting instruction at PC `0x1b7dd4`: `enc=0xa4100000` = opcode 0x29 =
  `LDQ R0, 0(R16)` -- a probe load in the memory/bus sizing loop.
- `memAddr` (the faulting effective VA, per PipelineDriver.h emit site) walks
  Pchip0 space `0x801.xxxx.xxxx` -- bit<43>=1, the PCI memory/IO window (HRM
  Table 10-1), NOT main RAM.
- The DIAG head-40 sample shows `fault=5` on first touch of a region, then
  `fault=0` on retry, then the walk continues clean. IMPORTANT: `fault=5` is
  `kFaultDtbMiss` (BoxResult.h:114), NOT ACV -- these are NORMAL DTB misses
  being page-walked and refilled. The memory test is progressing healthily out
  to cyc ~248M.
- The REAL blocker is the guest console's "access violation fault" =
  `kFaultAcv` = 7 (BoxResult.h:116), at the SAME `LDQ R0,0(R16)` but for
  R16 = `0xFFFFFFFF_7F827F5F` (a sign-extended 32-bit VA near 2GB). It repeats
  with VA += 8 while R0 doubles (address-line walk). The SRM does not expect
  the ACV: it dumps call frames (chain 1B7DD4 <- 5B058 <- 5A6B0 <- 8B694 <-
  6211C <- 66148) and notes "breakpoint at PC 1b7710 desired, XDELTA not
  loaded".

### Hypotheses (UPDATED -- earlier guesses corrected)

- REJECTED: "translator hard-rejects the address" and "kseg VA<12:0> drop
  (Ev6Translator.h)". The DTB-miss-then-refill pattern shows the translate/
  fill path working for the Pchip0-space probes; the earlier framing does not
  fit the evidence.
- OPEN: why does R16 = 0x7F827F5F produce a fatal ACV where the 0x801.xxxx
  probes only DTB-miss? Is 0x7F827F5F a VA the SRM built that has no valid PTE
  / wrong protection, or is V4 mis-deriving ACV vs DTBM for that VA class?

### Next diagnostic (staged on task #6)

`grep -a "fault=7" es40_acv_pcwin.log` in the existing PC-window log to isolate
the ACV lines (memAddr, excAddr) directly; widen `PCLO..PCHI` to
`0x1b7000..0x1b8000` for the surrounding loop; dump R16 + M_CTL(SPE) at the
sticky fault to disambiguate "no PTE" vs "V4 mis-classifies the fault".

## Task ledger (this session)

- #1 Redirect ES40 COM2 to console backend -- DONE (via #3).
- #3 Add ConsoleUartCom2 capability + ResolvedModel enum, drop SbAli gate --
  DONE.
- #4 Build + ES40 boot-gate verify -- DONE (banner in PuTTY).
- #2 Investigate why ES40 firmware selects COM2 not COM1 -- OPEN (taskable).
- #5 Fix es45/ds25 mislabeled platform badges -- OPEN.
- #6 Diagnose ES40 memory-test ACV (fault=7, VA 0x7F827F5F) -- OPEN, recipe
  staged.

## Standing rules

Discuss-before-code; header + inline docs citing HRM/source + task id;
ASCII(128); surgical Edit; V0/V1/V2 + Processor Support read-only; bounded
trace tails only (the es40_com2_txready_diag.log is 5.2 GB -- never whole-file
grep); full suite + DS10 + DS20 + ES40 boot gate before any core/chipset
commit; EMULATR_BRINGUP_PROBES=OFF for release.
