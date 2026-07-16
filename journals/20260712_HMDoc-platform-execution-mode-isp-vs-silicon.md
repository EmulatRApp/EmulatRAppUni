<!--
HMDoc-Platform Execution Mode: ISP (Engineering-Virtual) vs Silicon (REAL_HW).
EmulatR V4/V5 -- how the firmware's own platform() gate (PA 0xBFFC == 0xCAFEBEEF)
selects the ISP simulator path vs the real-hardware path, why EmulatR defaults to
ISP, and what that means for device discovery (IDE/SCSI/etc.).
Destined for the Help & Manual docset (HMDoc-*).  ASCII(128) only; hex radix.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
-->

# HMDoc-Platform Execution Mode: ISP (Engineering-Virtual) vs Silicon (REAL_HW)

## 1. Why this matters (the significance)

EmulatR presents the Alpha console firmware with a single, firmware-visible flag
that tells it whether it is running on **DEC's pre-silicon ISP software
simulator** or on **real hardware**.  That one flag changes the firmware's entire
initialization strategy.  By default EmulatR answers "ISP simulator," and in that
mode the SRM console **deliberately skips the real-hardware probe, timing, and
device-driver-attach steps** that V4 does not yet model -- which is exactly why
the console reaches `P00>>>` cleanly, and equally why platforms like the ES40 do
**not** enumerate their IDE (`dqa`/`dqb`) drives in `show dev`.

This is not a device bug and not a chipset bug.  It is a **whole-machine
execution-mode choice**.  Any device or behavior the firmware gates behind
`platform() == REAL_HW` is invisible in ISP mode, no matter how faithfully the
device itself is modeled.  Understanding this lever is a prerequisite for reading
any "device does not appear / is not discovered" symptom on an EmulatR platform.

## 2. The firmware mechanism (single source of truth: platform())

The console firmware carries its own detector.  apisrm `pc264.c` line 173:

    int platform( ) {
        int platform;
        /* ISP flag at location 0xBFFC.  In your ISP startup command
         * procedure, deposit 0xBFFC with CAFEBEEF. */
        if ( (*(int *)0xBFFC == 0xCAFEBEEF) )
            platform = ISP_MODEL;
        else
            platform = REAL_HW;
        return platform;
    }

- `ISP_MODEL` and `REAL_HW` are defined in `pc264_io.h` (`enum {ISP_MODEL = 1,
  REAL_HW}`) and `emulate.h` (`#define ISP_MODEL 1`).
- ISP == DEC's Instruction-Set-Processor / engineering *virtual* model (the
  pre-silicon software simulator DEC used to bring up the SRM before chips
  existed).  REAL_HW == actual silicon.
- The detector is a single 32-bit read of physical address **0x0000_BFFC**.  On
  the ISP simulator, DEC's startup deposits the sentinel **0xCAFEBEEF** there; on
  real hardware that location holds something else, so `platform()` returns
  REAL_HW.

## 3. How EmulatR selects the mode (the lever)

EmulatR does NOT deposit anything.  It presents the sentinel as a **read
intercept** so the value survives the SRM self-decompressor (which overwrites
image offset 0x3FFC == PA 0xBFFC, so a one-time store would not last).  See
`pipelineLib/MemDrainer.h` lines 458-486:

    // env -> 0xBFFC answer -> firmware platform().
    // EMULATR_PLATFORM: unset or "isp" => ISP (default; reaches >>>);
    //                   "silicon" => REAL_HW (no intercept).
    static bool const s_isp = <EMULATR_PLATFORM != "silicon">;
    if (s_isp && pa == 0x000000000000BFFCull)
        br.data = 0xCAFEBEEFull;           // platform() => ISP_MODEL

Environment knob:

    EMULATR_PLATFORM = (unset) | isp   -> ISP_MODEL  (DEFAULT)
    EMULATR_PLATFORM = silicon         -> REAL_HW

There is intentionally ONE flag; the firmware's own `platform()` is the single
source of truth.  (`BadgeMhzGauge.h` and others note the ISP default explicitly.)

## 4. What ISP mode skips (the consequence)

In ISP mode the firmware short-circuits every step that would touch real hardware
it assumes the simulator does not model.  The `platform() == ISP_MODEL` guard
appears throughout the driver set, e.g.:

    dq_driver.c:435       IDE  -- "IDE controller is not emulated", return failure
    dac960_driver.c:406   Mylex RAID
    esc_nvram_driver.c    ESC NVRAM (several sites)
    ew_driver.c:5436      Ethernet
    n810_driver.c:563     n810
    kernel.c:1852         kernel init branch

For the IDE specifically, `dq_initialize()` returns `msg_failure` BEFORE calling
`dq_init_port -> dq_reset -> dq_init_ide -> dq_identify`.  So in ISP mode the SRM
**never issues IDENTIFY over the taskfile** -- which is precisely the observed
signature on the ES40: the console lists the controller and channel names in
`show config` (that comes from the PCI topology walk), but `show dev` shows no
`dqa`, and an IDE I/O trace shows zero `0x1F0/0x170` taskfile accesses.

## 5. The IDE path in silicon mode (what SHOULD happen)

In REAL_HW mode `dq_init_ide()` (dq_driver.c:676) runs and chooses the taskfile
base from the controller's programming interface:

    progif = incfgb(pb, 0x09);
    // primary channel:
    if (progif & 0x01)  csr = incfgl(pb,0x10) & ~3;  // NATIVE: BAR0
    else                csr = 0x1F0;                  // COMPAT: fixed port
    // + a Cypress-only special case:
    if (id == 0xC6931080 && class == 0x0101) { csr = 0x1F0; program BARs; }

Consequences for EmulatR's controllers:

- **DS10/DS20 (Cypress CY82C693, id 0xC6931080)** hit the special case that
  forces `csr = 0x1F0` -- this is *why they enumerate IDE*.
- **ES40/ES45/DS25 (ALi M5229, id 0x522910B9)** do NOT match the special case, so
  they use the prog-IF path.  EmulatR's `AliM5229Ide` presents prog-IF **0x00**
  (compatibility mode), so `dq_init_ide` selects the fixed `csr = 0x1F0` -- which
  the shared `AtaTaskfileEngine` already answers.  No native/BAR/DMA path is
  required for console enumeration.

So the ALi IDE config (identity + compat prog-IF) is already correct for
discovery; it simply never runs while EmulatR is in ISP mode.

NOTE (open): DS10/DS20 currently enumerate IDE under EmulatR's ISP default, yet
the `dq_initialize` ISP gate is upstream of the Cypress special case.  Either the
shipped ds20 (pc264) console's IDE path lacks that gate, or its device-dispatch
differs from the reference source.  To be confirmed against the ds20 binary.

## 6. The tension (why EmulatR defaults to ISP)

ISP mode is a deliberate, honest simplification: it lets the console reach the
operator prompt on a machine model that does not yet implement the real-hardware
timing loops, register handshakes, and probe sequences the silicon path drives.
The MemDrainer note is explicit that the REAL_HW path is "currently unmodeled ->
stalls."  So today:

    ISP mode     -> reaches P00>>>, but NO real device discovery (IDE/SCSI/etc.)
    Silicon mode -> real device discovery, but stalls on unmodeled real-HW
                    timing/looping counters (needs warp, and/or modeling)

This maps directly onto the V5 trajectory.  Phase A (the faithful Oracle to the
SRM prompt) is served by ISP mode with device PRESENCE/PROBE via the topology
walk.  Actual device DATAPATH discovery -- and eventual OS boot -- requires the
silicon path, which is a Phase-B/optimization concern: model or warp the
real-hardware timing so REAL_HW mode runs to completion.

## 7. How to run each mode

    # ISP (default): reaches P00>>>, IDE not discovered
    ./tools/run_es40_showdev.sh

    # Silicon + warp experiment: real discovery; bypass the looping counters
    PLATFORM=silicon WARP=1 ./tools/run_es40_showdev.sh
      # -> EMULATR_PLATFORM=silicon (drop the 0xBFFC intercept -> REAL_HW)
      # -> WARP=1 sets EMULATR_IDLEWARP (skip idle/tick delay loops)

The run log's build stamp records `platform=` and `warp=` so every run is
self-identifying.  No rebuild is needed to switch modes: the lever is a runtime
read-intercept keyed off the environment.

## 8. Open questions / next steps

1. Does `PLATFORM=silicon WARP=1` reach the ES40 IDE probe, or stall earlier on
   another unmodeled real-HW step?  The console tail + trace locate the stall.
2. Enumerate every `platform() == ISP_MODEL` site the ES40 SRM hits on the silicon
   path; each is an unmodeled-hardware step to model or warp past.
3. Confirm the DS20 (pc264) IDE-under-ISP behavior vs the reference `dq_driver.c`
   ISP gate (Section 5 NOTE).
4. Decide the long-term policy: is silicon mode the Phase-B execution target for
   OS boot, with ISP retained as the frozen-Oracle-to-prompt mode?

## 9. Reference map

    Firmware:  pc264.c:173 platform(); pc264_io.h:48 / emulate.h:33 ISP_MODEL;
               dq_driver.c:435 (ISP gate) / :676 dq_init_ide (prog-IF + Cypress
               special case); dac960/esc_nvram/ew/n810/kernel ISP_MODEL sites.
    EmulatR:   pipelineLib/MemDrainer.h:458-486 (0xBFFC read-intercept +
               EMULATR_PLATFORM knob); deviceLib/BadgeMhzGauge.h (ISP-default note);
               deviceLib/Tsunami/AliM5229Ide.h (ALi identity, compat prog-IF);
               deviceLib/Tsunami/AtaTaskfileEngine.h (fixed-port taskfile).
    Runner:    tools/run_es40_showdev.sh (PLATFORM / WARP knobs, stamped).
