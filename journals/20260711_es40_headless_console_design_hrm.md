<!--
EmulatR V4 -- HEADLESS CONSOLE DESIGN HRM.
Analysis + implementation reference for EmulatR's headless (serial-console,
VGA-less) platform posture, and why the ES40 SRM "Flash SROM invalid" /
"TIG load failure" diagnostics are FAITHFUL behavior rather than defects.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
Status: DESIGN REFERENCE (rooted 2026-07-11).  Supersedes ad-hoc treatment of
the ES40 SROM/TIG boot diagnostics.  Evidence: trace 20260711-170950_srm.trc,
AXPBox 1.1.2 source XREF, pc264 firmware headers.
-->

# EmulatR Headless Console -- Design HRM (2026-07-11)

## 1. Purpose and scope

This document roots a standing DESIGN DECISION: EmulatR emulates a HEADLESS
(serial-console, no graphics adapter) Alpha system by default.  It is written as
an analysis/implementation reference (HRM-style) so that future work does not
re-litigate the ES40 SRM boot diagnostics "*** Error - Flash SROM invalid ***"
and "*** Error - TIG load failure ***" as if they were defects.  They are the
correct, expected behavior of the machine EmulatR emulates.

Scope: the option-ROM / graphics-firmware presence check performed by the pc264
SRM console during "starting drivers", the hardware PA map it touches, the
silicon-vs-firmware layering that governs the outcome, the design posture that
makes the diagnostic faithful, and the implementation invariants that follow.

## 2. Design posture: headless by default

EmulatR models an ES40 (and the DS10/DS20/ES45 family) with:
  - a SERIAL console (16550 UART -> host TCP / PuTTY), and
  - NO graphics adapter (no VGA/Cirrus card, hence no VGA option ROM).

This is a legitimate, common real-world configuration: AlphaServer ES40 systems
were routinely deployed as headless servers driven entirely from the serial
console.  A headless machine has nothing in the ISA/PCI option-ROM region, and
its firmware is written to tolerate that.

Consequence: any firmware check that scans for a graphics/expansion option ROM
will find none and will say so.  That is not a bug; it is the machine reporting
its own (VGA-less) configuration.

## 3. Hardware reference -- the option-ROM window

3.1 PA map (Tsunami 21272 / EV6, hose 0)

    Region                         Physical address
    -----------------------------  --------------------------------------------
    System RAM (kseg/direct)       0x0000_0000_0000 .. (memSize)
    I/O window (chip bit<43>=1)    0x0800_0000_0000 .. 0x0FFF_FFFF_FFFF
      Pchip0 PCI/ISA dense memory  0x0800_0000_0000 + PCI address
        ISA legacy option-ROM area 0x0800_0000_0000 + 0x000C_0000  (= 0x800_000C_0000)
      TIG-bus flash window         0x0801_0000_0000 .. 0x0801_0800_0000  (EmulatR)
        (FlashRom 2 MB, byte at pa = base + (flash_off << 6); kTigFlashSize
         = FlashRom::kSize(0x200000) << 6 = 0x8000000)

Two DISTINCT ROM concepts share the word "flash" and must not be conflated:
  (a) TIG-bus flash (0x801_0000_0000): the 2 MB system flash holding the SRM /
      ARC consoles + env.  DS10/DS20 read it here; EmulatR maps it here.
  (b) ISA option-ROM window (0x800_000C_0000 == ISA memory 0xC0000): where PCI/
      ISA expansion cards (e.g. a VGA/Cirrus adapter) shadow their option ROM.

3.2 The ISA option-ROM signature

ISA/PCI expansion ROMs begin with the 16-bit signature 0xAA55 (little-endian
0x55 0xAA) at offset 0.  Absence of a card (or of its ROM) leaves the window
unmapped; a PCI read of unmapped space returns a master-abort / open-bus value
of all-ones (0xFFFF for a word).

## 4. The SRM firmware option-ROM scan (machine-confirmed)

During "starting drivers" (cyc ~283.0M-283.5M in trace 20260711-170950_srm.trc)
the pc264 SRM scans the ISA option-ROM window for a valid 0xAA55-signed ROM:

    guest 0x6b368  BIS   R16 = 0x800_000c0000   ; scan pointer := option-ROM base
    guest 0x1b7d8c LDWU  R0  = [option-ROM]      ; read a 16-bit word (helper)
                    ... scans window 0x800_000c0000 .. 0x800_000df800 (~81 words)
    guest 0x13c148 XOR   R0  = R0 ^ 0xAA55       ; compare against the ROM signature
    guest 0x13c14c BNE   R0  -> "invalid" path   ; nonzero -> no valid option ROM

VALID requires the read word to equal 0xAA55 (0xAA55 ^ 0xAA55 = 0 -> BEQ).  On a
headless machine the window is unmapped, the read returns 0xFFFF, and
0xFFFF ^ 0xAA55 = 0x55AA != 0 -> the SRM prints "*** Error - Flash SROM invalid
***" and continues.  A companion check yields "*** Error - TIG load failure ***".
The SRM image itself carries VGA (x6) and graphics (x9) strings, confirming the
scan is graphics/expansion-ROM discovery, not system-flash validation.

The pc264 system-flash components are NOT the object of this check:
  pc264fsb.rom / pc264nt.rom / pc264srm.rom all begin c3c3 5a5a 3c3c a5a5 (the
  DEC firmware header magic), NOT 0xAA55.  They are consoles, not option ROMs.

## 5. Silicon vs firmware -- the governing principle

This is the crux the design must record.  The outcome is decided by FIRMWARE
POLICY, not by silicon behavior:

  - HARDWARE (silicon): a read of the absent/unmapped option-ROM window returns
    0xFFFF (PCI master-abort / open-bus all-ones).  The CPU raises no fault and
    takes NO autonomous action.  Silicon does not "fall back to memory" or
    anywhere else on a benign I/O read; it simply returns the bus value.
  - FIRMWARE (SRM software running on that silicon): reads 0xFFFF, finds no
    0xAA55, LOGS the diagnostic, and CONTINUES to the ">>>" prompt using the
    serial console.  The decision to note-and-continue is software policy.

There IS a genuine firmware fallback chain on Alpha, but it is a DIFFERENT
mechanism and does not apply here:
    serial SROM (EEPROM) -> fail-safe booter (pc264fsb.rom) -> SRM console.
It runs at POWER-UP and is triggered by a CORRUPT MAIN CONSOLE image detected by
the serial SROM.  It is not triggered by a missing graphics option ROM at
console runtime.  Conflating the two ("silicon falls back to the FSB when the
option ROM is absent") is incorrect.

Therefore: a real headless silicon ES40 (no graphics adapter) behaves EXACTLY as
EmulatR does -- reads open-bus 0xFFFF, prints the diagnostic, boots to ">>>".

## 6. Design decision and rationale

DECISION: EmulatR presents the option-ROM window as unmapped (open-bus 0xFFFF)
on the headless platform, and the SRM's "Flash SROM invalid" / "TIG load
failure" diagnostics are ACCEPTED AS FAITHFUL.  No code change; no fabricated
signature.

Rationale:
  - Faithfulness: it reproduces the exact behavior of the VGA-less machine being
    emulated.  The diagnostic is the machine truthfully reporting its config.
  - Anti-mask discipline: writing a synthetic 0xAA55 into the window to silence
    the message would be a MASK -- the same class of error as the 2026-07-08
    CSERVE-0x66 BCD-TOY (a value tuned to make a check pass, not the real
    contract).  It would also imply a graphics adapter that does not exist,
    corrupting any later graphics/console-selection logic.
  - Determinism: open-bus 0xFFFF is a fixed, deterministic result; no timing or
    host state enters.

AXPBox contrast (why it does not show the message): AXPBox 1.1.2 MODELS a Cirrus
VGA adapter (src/Cirrus.cpp: static u8 option_rom[65536]; CCirrus;
add_legacy_mem(5, 0xc0000, rom_max)).  Its option ROM IS present, so the scan
passes.  AXPBox emulates a graphics-equipped ES40; EmulatR emulates a headless
ES40.  Both are faithful to their respective configurations -- this is a
CONFIGURATION difference, not a fidelity gap.

## 7. Implementation invariants (for future work)

I1. The ISA option-ROM window (0x800_000C_0000, == ISA 0xC0000) MUST read as
    open-bus 0xFFFF while no graphics/expansion device is modeled.  Do NOT route
    it to the TIG flash (0x801_0000_0000) or seed it with a synthetic 0xAA55.
I2. "Flash SROM invalid" and "TIG load failure" during "starting drivers" are
    EXPECTED on the headless platform.  Do not treat their appearance as a
    regression; treat their DISAPPEARANCE (without a modeled graphics adapter) as
    the regression (it would mean the window is being mis-mapped or masked).
I3. The TIG-bus flash (0x801_0000_0000) and the ISA option-ROM window
    (0x800_000C_0000) are separate; keep their mappings independent.
I4. Firmware-policy diagnostics (note-and-continue) are distinct from hardware
    faults (kFault*).  This scan is the former: it must not raise a fault or
    halt; boot proceeds to ">>>".

## 8. Extension path -- modeling a graphics-equipped ES40 (optional, deferred)

If a graphics configuration is ever desired (to match AXPBox, or to exercise the
VGA/graphics console path), the FAITHFUL implementation is to add a graphics
adapter as a real device, not to fake the signature:
  - Add a VGA/Cirrus PCI device on hose 0 with a proper option ROM whose first
    word is 0xAA55 and whose body is a real (or faithfully stubbed) VGA BIOS.
  - Shadow that option ROM into the ISA legacy window at 0x800_000C_0000 during
    PCI enumeration (AXPBox add_legacy_mem is the reference mechanism).
  - The SRM scan then finds the signature and the graphics console becomes
    available; the diagnostic disappears BECAUSE the modeled machine now has the
    hardware.
This is a real subsystem (device + option ROM + shadow), tracked separately; it
is NOT part of the headless posture and must not be half-implemented as a bare
signature.

## 9. TIG load failure (companion condition)

"TIG load failure" is the same class of firmware-policy diagnostic.  EmulatR's
TIG-bus data path is a stub (TsunamiCchip.h TODO(unwired) x3, m_ttr = 0), so the
TIG-load read returns 0/absent and the SRM notes the failure and continues.  It
does not gate the boot (P00 is reached).  Wire the TIG data path only if a future
feature depends on it; until then it is a benign, documented stub.

## 10. References

  - Trace: RelWithDebInfo/traces/20260711-170950_srm.trc (gated window over the
    "starting drivers" SROM/TIG band).  Detail: journals/
    20260711_es40_srom_tig_validation_trace.md.
  - EmulatR: chipsetLib/TsunamiChipset.h (kTigFlashBase 0x80100000000,
    kTigFlashSize 0x8000000, tigFlashOffset, isTigFlashAddr); chipsetLib/
    FlashRom.{h,cpp}; chipsetLib/TsunamiCchip.h (TIG TODO(unwired)).
  - AXPBox 1.1.2: src/Cirrus.cpp (option_rom[65536], add_legacy_mem @0xc0000);
    the graphics-equipped reference config.
  - Firmware: firmware/pc264fsb.rom (fail-safe booter, 49912 B), pc264nt.rom
    (ARC), pc264srm.rom (SRM) -- all c3c3-5a5a headers, NOT option ROMs.
  - Task #20 (resolved-as-designed).  Related posture note: the es40 manifest
    KNOWN DIVERGENCE (ALi M1543C vs the Cypress stand-in south bridge).
