<!--
EmulatR V4 -- ES40 SRM SROM/TIG validation: gated-trace extract (2026-07-11).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
Trace: RelWithDebInfo/traces/20260711-170950_srm.trc (210 MB, 800k retires,
gated window cyc ~282.9M-283.7M over the "starting drivers" SROM/TIG band).
-->

# ES40 SRM SROM/TIG validation -- trace extract (task #20) (2026-07-11)

## What the console checks (machine-confirmed from the gated trace)

FLASH SROM signature/checksum -> "Flash SROM invalid":
  The console scans the TIG flash config block (flash window PA 0x800_000c0000 ..
  0x800_000df800, read via LDWU helper at guest 0x1b7d8c, ~81 word reads) and
  validates the result against 0xAA55 -- the classic ROM signature:
      0x1b7d8c LDWU  R0 = flash word   = 0xFFFF    (EmulatR flash is erased here)
      0x6b3fc  ZAPNOT R0 = 0x0000FFFF
      0x13c138 ZAPNOT R0 = 0x0000FFFF
      0x13c148 XOR   R0 = R0 ^ 0xAA55  = 0x55AA
      0x13c14c BNE   R0  -> taken -> INVALID path
  VALID requires the accumulated/last word to equal 0xAA55 (0xAA55 ^ 0xAA55 = 0 ->
  BEQ = valid).  EmulatR reads 0xFFFF (erased) -> 0x55AA != 0 -> "Flash SROM invalid".

TIG load -> "TIG load failure":
  Reads in the 0x801_xxxx CSR region (via the generic LDQ helper at guest 0x1b7dd4)
  return 0 (3x at ~cyc 283,236,206), and the TIG data path is a stub in EmulatR
  (TsunamiCchip.h TODO(unwired) x3, m_ttr=0).  Zero/absent TIG data -> load fails.
  (Exact TIG CSR + expected value: one more drill; secondary to the SROM signature.)

## Root cause (confirmed)

EmulatR seeds the 2 MB TIG flash from the compressed es40_v7_3.exe (the SRM console
image only).  A real ES40 2 MB flash ALSO carries the SROM config block (with the
0xAA55 signature/checksum), env blocks, DSRDB, and the ARC console -- none of which
the .exe provides.  So the console's flash-config scan reads erased 0xFF and the
0xAA55 check fails.  FlashRom.h:115 already anticipated this: "a faithful machine
snapshot must carry the 2 MB TIG flash".

## CORRECTION (2026-07-11, untruncated PA) -- it is a MAPPING mismatch, not empty flash

The console reads the flash config at pa = 0x800_000c0000 (va == pa, identity
superpage).  EmulatR's TIG flash window is kTigFlashBase = 0x801_00000000, size
0x8000000 -> [0x801_00000000, 0x801_08000000).  0x800_000c0000 is BELOW that base,
so isTigFlashAddr() is FALSE and the read falls through to the unmapped I/O default
= 0xFFFF -> the 0xAA55 check fails.  The seeded flash DOES have content (es40_diag_
flash.rom @0x3000 = 6b9b901b..., not 0xFF); EmulatR simply is not mapping flash at
the PA the ES40 console reads.  So supplying a golden flash image at 0x801_xxx does
NOT help -- the console reads 0x800_000c0000.

Why DS10/DS20 are clean: they read flash at 0x801_xxx (EmulatR's TIG window) and
succeed.  ES40 reads 0x800_000c0000 -- a DIFFERENT address.  On a real ES40 the
"Flash SROM" is almost certainly the fail-safe booter (pc264fsb.rom, 49912 bytes,
2007) behind the ALi M1543C south bridge (LPC/firmware-hub), mapped into hose-0 I/O
space at ~0x800_000c0000 -- NOT the Tsunami TIG flash.  EmulatR uses a Cypress
STAND-IN south bridge (manifest KNOWN DIVERGENCE) that does not map the FSB there.

REVISED root: the ES40 SRM's flash/SROM read targets a hose-0 / south-bridge
firmware-hub PA (0x800_000c0000) that EmulatR does not route to any flash content.
Next: determine what 0x800_000c0000 IS on a real ES40 (AXPBox models it -- Tim has
axpbox-1.1.2/ in-tree; or the Tsunami/ALi HRM), then map the FSB (pc264fsb.rom) or
the flash there.  This supersedes the "seed a golden flash image" plan below.

## RESOLVED interpretation (2026-07-11, AXPBox XREF) -- ISA option-ROM region

pa 0x800_000c0000 = bit-43 I/O window + ISA legacy address 0xC0000 -- the ISA
expansion/OPTION-ROM region.  AXPBox maps a ROM there: src/Cirrus.cpp:302
"add_legacy_mem(5, 0xc0000, rom_max)" (the Cirrus VGA option ROM).  0xAA55 is the
STANDARD PCI/ISA option-ROM header signature.  So the ES40 "Flash SROM" check is
scanning ISA 0xC0000..0xDF800 for a 0xAA55-signed option ROM; EmulatR maps nothing
in the ISA legacy ROM window -> 0xFFFF -> "Flash SROM invalid".

KEY NUANCE (decide before any code): EmulatR runs ES40 HEADLESS (serial console,
no VGA card).  A real headless ES40 with no graphics option ROM also has no 0xAA55
at 0xC0000 -- so "Flash SROM invalid" may be FAITHFUL/EXPECTED for a headless
config, not a bug.  Boot CONTINUES past it to P00 (benign note, not a halt).  So:
  - If we model no option ROM (headless): the error is honest; leave it, document
    as expected.  Most faithful to the emulated (VGA-less) machine.
  - If we want a fully-configured ES40 (VGA present): shadow a 0xAA55-signed option
    ROM (e.g. a Cirrus/VGA BIOS, or the pc264fsb.rom if that is what belongs there)
    into the ISA legacy window at 0x800_000c0000 -- an option-ROM-shadow feature in
    the south bridge (EmulatR's Cypress stand-in lacks it; AXPBox add_legacy_mem is
    the reference).  This is a real subsystem addition, cosmetic (boot already P00).

## TIG LOAD FAILURE -- ROOT-CAUSED (2026-07-11): missing DPR / RMC model (NOT headless-faithful)

The "*** Error - TIG load failure ***" (img "TIG load error" @guest 0x1a68c0) is a
SEPARATE root cause from the SROM option-ROM scan, now drilled:
  - The console reads a region at PA 0x801_1000_2xxx (TIGbus window, ABOVE EmulatR's
    flash sub-window 0x801_0000_0000..0801_0800_0000), via the generic LDQ helper
    @guest 0x1b7dd4; EVERY read returns 0 (cyc ~283.235M).
  - That PA is the DPR -- the Dual-Port RAM shared between the host CPU and the RMC
    (Remote Management Console) on the TIGbus.  AXPBox registers the DPR at exactly
    U64(0x0000080110000000) size 0x100000 (src/DPR.cpp:48; DPR.hpp: "dual-port RAM
    and management controller").  The image expects real config there -- string
    "DPR AAR0 Config" @guest 0x1a5808, plus "RMC" @0x1a1e78; AXPBox seeds the DPR
    with CPU speed + BCD time + config.
  - EmulatR models ONLY the flash sub-window of the TIGbus; nothing at 0x801_1000_xxxx.
    So DPR reads = 0 -> the console sees an all-zero DPR -> "TIG load failure".
CLASSIFICATION: a GENUINE unmodeled-subsystem gap (the RMC/DPR exists on every ES40,
headless or not -- a real machine would NOT print this), UNLIKE the SROM option-ROM
scan which is faithful-for-headless.  Cosmetic (boot reaches P00), but modeling the
DPR is real fidelity AND feeds multiple show config fields (DPR AAR0 Config, CPU
speed -> overlaps #24, time, RMC).  FIX (deferred, tasked): add a ~16 KB DPR device
at 0x801_1000_0000 and seed it AXPBox-style (CPU speed, BCD time, AAR config, RMC
status).  Reference: axpbox/src/DPR.cpp + DPR.hpp.

## DEFINITIVE (2026-07-11) -- SROM check: a VGA/graphics option-ROM scan; message is FAITHFUL headless behavior

Grounded by three checks:
  1. pc264fsb.rom / pc264nt.rom / pc264srm.rom all start with c3c3 5a5a 3c3c a5a5
     (DEC firmware header magic), NOT 0xAA55 -> they are NOT option ROMs and are
     not the thing being validated at 0xC0000.
  2. The SRM image carries VGA (6x) + graphics (9x) strings -- it scans the ISA
     option-ROM window (0xC0000) for a 0xAA55-signed graphics/expansion option ROM.
  3. AXPBox does NOT print this message because it MODELS a Cirrus VGA card
     (src/Cirrus.cpp: static u8 option_rom[65536] + CCirrus; add_legacy_mem @0xc0000).
     AXPBox = ES40 WITH graphics; EmulatR = HEADLESS ES40.

SILICON vs FIRMWARE (the architect's question): the check is FIRMWARE POLICY, not a
silicon behavior.  Hardware returns 0xFFFF (PCI master-abort / open-bus all-ones)
for the absent option ROM; the CPU takes no special action.  The SRM firmware reads
0xFFFF, finds no 0xAA55, prints "Flash SROM invalid", and CONTINUES to >>>.  There
is NO silicon fall-back-to-memory for this.  (Alpha's real fallback chain
SROM -> fail-safe booter (pc264fsb.rom) -> SRM console is a POWER-UP mechanism for a
CORRUPT MAIN CONSOLE image, detected by the serial SROM -- a different failure mode,
not a missing option ROM at runtime.)  So a real headless silicon ES40 (no graphics
adapter) behaves exactly as EmulatR does: notes the absence, boots to >>>.  The
message is FAITHFUL to the VGA-less machine we emulate.

DECISION: accept #20 as expected-for-headless (faithful; zero code).  Suppressing it
would mean modeling a graphics adapter (VGA/Cirrus option ROM) = a DIFFERENT, larger
machine config (AXPBox's), not a fidelity fix.  "TIG load failure" is a separate seam
(unwired TIG data path); revisit only if it gates something.

## Fix options (design decision -- discuss before code)

(A) FAITHFUL, data-driven: carry a real 2 MB ES40 flash image (SRM + SROM config +
    DSRDB + env + ARC) and seed FlashRom from it instead of the .exe.  Resolves
    Flash-SROM + TIG(if the image carries TIG/DSR data) + ARC string + likely the
    Bcache/cycle fields together.  Needs a genuine ES40 flash dump (not on hand).

(B) SYNTHESIZE the minimal SROM config block: place a valid config at the scanned
    flash offset (window 0xc0000-region) whose word/checksum resolves to 0xAA55 so
    the console's check passes.  Targeted; must match the console's exact scan +
    checksum algorithm (extract fully from the trace first, so we do not "mask" it
    the way the 2026-07-08 CSERVE-0x66 BCD-TOY did).

Recommendation: prefer (A) if a real ES40 flash image can be sourced (most faithful,
fixes the cluster); else (B) with the exact checksum extracted and documented.  TIG
load is a separate seam (unwired TIG data path) tracked under the same task.

## Provenance

Exact ord/pc/value rows in traces/20260711-170950_srm.trc.  TEMP one-shot arm in
PipelineDriver.h (kTraceArmCyc=282900000 / kTraceLen=800000); REMOVE after this
capture is acted on.
