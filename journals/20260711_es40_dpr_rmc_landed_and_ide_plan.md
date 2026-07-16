<!--
EmulatR V4 -- ES40 RMC DPR landed + validated; ES40 IDE plan for tomorrow.
Session journal, 2026-07-11.  ASCII(128) only; hex radix.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
-->

# ES40 RMC DPR landed + validated; ES40 IDE plan (2026-07-11)

## Result of the day

The ES40 now completes a CLEAN SRM cold boot to P00>>> with NO
"*** Error - TIG load failure ***" and NO "*** Error - Flash SROM invalid ***".
Both errors are cleared by modeling the RMC Dual-Port RAM (task #25).  Live boot
(console V4.0-0): powerup -> Memory size 4096 MB -> testing memory -> probe I/O
(hose 0/1 PCI, PCI-to-ISA bridge, IDE) -> starting drivers -> entering idle loop
-> GCT/FRU at 3fc30000 -> Memory Testing and Configuration Status (4x 1024Mb,
4-Way interleave, 4096 MB) -> "AlphaServer ES40 Console V7.3-2, built Feb 27
2007" -> P00>>>.

## What landed (task #25, DONE)

New device: chipsetLib/TsunamiDpr.h -- the RMC (Remote Management Console)
Dual-Port RAM.  16 KB dual-port SRAM, host PA base 0x801_1000_0000, byte at
base + (index << 6) over the TIG bus.  Full RMC command mailbox ported
(FRU / OCP / flash update + secondary-CPU start).  Wired into TsunamiChipset
read/write, gated on m_hasRmcDpr (variant == Typhoon => ES40 only); DS10/DS20
(Tsunami) leave the window unclaimed, so they remain byte-identical.

Wiring edits: TsunamiChipset.h (include, member m_dpr + m_hasRmcDpr, both ctor
init-lists, reset(), accessors) and TsunamiChipset.cpp (variant-ctor init-list +
two guarded window decodes in read()/write()).

The two error clears, mechanism:
  - TIG load failure  -> DPR ram[0xDA] = 0xAA.  EK-ES240-SV.A01 Table C-1:
    "Indicates TIG finished loading its code (0xAA indicates done)".
  - Flash SROM invalid -> DPR ram[0x3401] = 8.  Table C-1: "Flash SROM is valid
    flag; 8 = valid, 0 = invalid".  This was a predicted BONUS clear and it
    REVISES the earlier "faithful-headless" framing for the SROM error: it is now
    RESOLVED via faithful RMC modeling, not merely tolerated.

## HRM provenance (the day's other win)

Two authoritative Compaq/ALi documents were located and used, upgrading values
from provisional to HRM-verified:

  1. Compaq AlphaServer ES40 Service Guide, EK-ES240-SV.A01 (es240sva.txt),
     Appendix C "DPR Address Layout" Table C-1 (+ App D/E).  This is the
     authoritative DPR field map -- more authoritative than AXPBox's CDPR (our
     original oracle) and than the unshipped DEC clipper_dpr_def.sdl.  Every DPR
     offset was verified against it.  One correction it surfaced: the I2C-done
     "0xBA = finished" flag lives at location 0xD9, not 0xBA as AXPBox had it
     (corrected in TsunamiDpr.h).

  2. ALi M1543C Desktop Southbridge Preliminary Datasheet v1.10
     (M1543C_Desktop_Southbridge.txt) -- the authoritative HRM for the ES40 south
     bridge and its built-in IDE.  Reserved for tomorrow (see below).

## Do-no-harm

ES40 boots clean to P00 (validated live).  DS10/DS20 leave the DPR window
unclaimed (gate variant != Typhoon) -> byte-identical.  Build clean, no errors.

## Commit status

The working tree index was found CORRUPT (stale .git/index.lock; ~60 FALSE
staged deletions of traceLib/ + tools/*; phantom "AD ./" and unmerged "l"
entries).  A safe repair+commit+push script was written for Tim's MINGW shell:
/d/EmulatR/commit_es40_dpr.sh -- it clears the lock, rebuilds the index from HEAD
(cancelling the false deletions without touching any working file), stages the
real diff, HARD-GUARDS against deleting any traceLib/ or tools/ source, then
commits and pushes.  Pending Tim's run.

## Tomorrow: add IDE to the ES40 (task #32)

Symptom: at P00>>>, `show dev` lists only dva0 (floppy); dqa0/dqa1 are NOT
enumerated, even though the probe reports "bus 0, slot 5 -- dqa -- Cypress
82C693 IDE" and a dqa device is mapped.  DS10/DS20 DO enumerate dqa (prior
`b dqa1` runs), so the IDE model works there -- the ES40 difference is the south
bridge.

Facts:
  - ES40 real HW south bridge = ALi M1543C, with its integrated M5229 IDE.
  - EmulatR currently models a Cypress 82C693 IDE STAND-IN on the ES40 (the probe
    string confirms it).
  - Authoritative HRM in hand: M1543C_Desktop_Southbridge.txt v1.10, Section 3.6
    "IDE Master Controller" (p.42) and especially Section 4.1.2 "IDE Master M5229
    Configuration Registers" (p.84): IDSEL = AD27 (default), the Class Code
    register, per-channel enable bits (tri-state the primary/secondary pins), and
    native-vs-compatibility interrupt routing.  These are the exact bits the
    pc264 SRM reads to decide a channel has drives to enumerate.
  - Reference implementation oracle: axpbox/src/AliM1543C_ide.cpp (~2300 lines).
  - Cypress stand-in datasheet also on hand (Processor Support/Cypress ISA
    Bridge cy82c693...txt) to see what we model today.

First checks tomorrow (before deciding chip-swap vs stand-in fix):
  1. Is setDiskMedia / setCdMedia actually called on the ES40 config path in
     systemLib/Machine.cpp (is the mapped dqa media reaching the IDE model)?
  2. Does the modeled IDE present the correct PCI Class Code (IDE 0x0101), the
     channel-enable bits, and a valid ATA IDENTIFY signature on the channel /
     PCI device (IDSEL) the pc264 SRM probes?
  3. Compare DS20 vs ES40 IDE PCI location + SRM enumeration path -- what does
     DS20 present that the ES40 stand-in does not?

Decision point: if the gap is enumeration wiring (media attach / probed channel /
class code / IDENTIFY), fix the stand-in -- no chip swap needed.  If we want the
ES40 south bridge faithful, model the M1543C/M5229 IDE from the datasheet + the
AXPBox reference (the same triad approach that made the DPR work go smoothly).

Standing rules apply: discuss-before-code, ASCII(128), hex radix, header+source
documentation, do-no-harm gate (DS10/DS20 byte-identical; ES40 to P00).
