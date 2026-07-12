<!--
EmulatR V4 -- MEMORY-ARRAY DECODE-WIDTH SEAM: DESIGN HRM.
Analysis + implementation reference for the memory-size decode seam that
separates the model tiers: Tsunami/es40_v7_3 (3-bit ASIZ, 4 GB) vs
Typhoon/Titan/es45 (4-bit ASIZ, up to 32 GB).  Roots WHY EmulatR faithfully
caps ES40 at 4 GB and how the >4 GB ES45 tier is reached.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
Status: DESIGN REFERENCE (rooted 2026-07-11).  Anchors: AXPBox 1.1.2
src/System.cpp; apisrm memconfig_pc264.c + tsunami.h; the #6 AAR fix.
-->

# Memory-Array Decode-Width Seam -- Design HRM (2026-07-11)

## 1. Purpose and scope

This roots a standing DESIGN DECISION and the reasoning behind it: the maximum
system memory a modeled Alpha reports is set by TWO independent limits --
the CHIPSET (Cchip) physical-address width, and the LOADED FIRMWARE's memory-
array-size (ASIZ) decode width.  The effective ceiling is the MINIMUM of the two.
This is why EmulatR faithfully caps ES40 (loading es40_v7_3) at 4 GB, and why the
>4 GB path belongs to the ES45/Titan tier.  It is written as an analysis/
implementation reference so future memory work does not mistake the 4 GB ES40
ceiling for an EmulatR defect, and starts the ES45 tier from the right model.

## 2. The two limits and the ceiling rule

    effective_max_memory = min( Cchip_PA_limit , firmware_ASIZ_decode_limit )

  (a) Cchip physical-address limit (hardware).  Per AXPBox 1.1.2 src/System.cpp
      (verbatim): "the Version 1 Cchip only supports 4GB of system memory (32
      bits total) ... The Typhoon Cchip supports 32GB of system memory (35 bits
      total)."
        Tsunami 21272 Version-1 Cchip : 4 GB  (32-bit system-memory window)
        Typhoon / Titan Cchip         : 32 GB (35-bit system-memory window)
  (b) Firmware ASIZ decode width (software).  The SRM's get_array_size reads the
      array-size field from the Cchip AAR/MMR and masks it to N bits:
        3-bit ASIZ (mask & 7)  -> max code 7 -> 1 GB/array
        4-bit ASIZ (mask & 0xF) -> codes 8/9/A/.../C -> 2/4/8/.../32 GB/array

The array-size formula (both firmwares) is:
        array_size_bytes = 2 ^ ( ASIZ + 23 )   ==  (1 << (ASIZ+3)) * 1 MB
    ASIZ  size            ASIZ  size (extended, 4-bit)
    ----  ----            ----  ------------------------
    0x1   16 MB           0x8   2 GB
    0x2   32 MB           0x9   4 GB
    0x3   64 MB           0xA   8 GB
    0x4   128 MB          0xB   16 GB
    0x5   256 MB          0xC   32 GB
    0x6   512 MB
    0x7   1 GB  (3-bit max)

## 3. es40_v7_3 is a 3-bit console -- PROVEN

Source (apisrm/ref, the pc264/Tsunami family):
  tsunami.h:        aar_m_asiz = 0x00007000       (ASIZ = bits <14:12>, 3 bits)
                    CSR_AAR0..CSR_AAR3 (spacing 0x40) -> exactly 4 arrays
  memconfig_pc264.c get_array_size:
        size = ( ReadTsunamiCSR( CSR_AAR0 + array*0x40 ) >> 12 ) & 7;   // 3-bit
        return size ? (1 << (size+3)) * 1MB : 0;                        // max 1 GB
        total memory = sum over MAX_MEMORY_ARRAY arrays

Empirical confirmation (the #6 AAR fix): EmulatR first encoded 4 GB as ONE array
with the extended 4-bit ASIZ 0x9 (AAR0 = 0x9009).  es40_v7_3 read it as
(0x9 & 7) = 1 -> 16 MB (mis-sized), which broke the memtest.  The fix re-encoded
4 GB as 4 x 1 GB arrays (ASIZ 0x7 each) -- the ONLY way a 3-bit decoder reaches
4 GB -- and the SRM then sized 4096 MB correctly.  So es40_v7_3 is definitively
3-bit; its ceiling is 4 arrays x 1 GB = 4 GB, which also equals the Tsunami V1
Cchip's 4 GB PA limit.

## 4. AXPBox cross-reference -- the 4-bit reference encoding

AXPBox 1.1.2 (a mature ES40 emulator) does NOT cap at 4 GB; it targets the
Typhoon Cchip's 32 GB.  Its encoding (src/System.cpp:1346-1347) is the OPPOSITE
of EmulatR's ES40 tiling -- a SINGLE array with a 4-bit ASIZ nibble:
        // WE PUT ALL OUR MEMORY IN A SINGLE ARRAY FOR NOW...
        return ((u64)(iNumMemoryBits - 23) << 12);   // ASIZ = memoryBits - 23
    4 GB (bits=32) -> ASIZ 0x9 ; 8 GB -> 0xA ; 32 GB (bits=35) -> 0xC.

This is EXACTLY the single-array 4-bit encoding that broke on es40_v7_3.  It works
in AXPBox because AXPBox runs a 4-BIT-DECODING console (its cl67 SRM), not the
3-bit es40_v7_3.  So AXPBox and EmulatR do not disagree about the hardware -- they
are paired with different firmware:
        AXPBox   : Typhoon Cchip + 4-bit console (cl67) -> up to 32 GB, 1x4-bit array
        EmulatR  : es40_v7_3 3-bit console           -> 4 GB, 4 x 1 GB arrays
The decode width is FIRMWARE-BORNE.  Two faithful emulators, two firmwares, two
encodings, same silicon.

## 5. Model tiers (the design)

    Model  Chipset            Cchip PA   Firmware      ASIZ   Max mem   Encoding
    -----  -----------------  ---------  ------------  -----  --------  -----------------
    ES40   Tsunami 21272      4 GB (V1)  es40_v7_3     3-bit  4 GB      4 x 1 GB (ASIZ 7)
    ES45   Titan 21274        32 GB      es45_v7_3     4-bit  <= 32 GB  1..4 arrays, ASIZ 8..C
    (DS10/DS20 track ES40: Tsunami + 3-bit pc264 console -> 4 GB.)

EmulatR already models decode-width as a per-firmware property:
chipsetLib/TsunamiCchip.h m_extendedAsizDecode (the 4-bit ASIZ<15> path, codes
0x8/0x9/0xA = 2/4/8 GB), DEFAULT false -> 3-bit -> correct for es40_v7_3.  So the
32 GB fail-fast and the 4 x 1 GB tiling on ES40 are FAITHFUL, not limitations.

## 6. Implementation invariants

M1. The reported memory ceiling is min(Cchip PA limit, firmware ASIZ decode).  Do
    not raise one without the other; a 4-bit encoding under a 3-bit console loses
    memory (the SRM masks the high ASIZ bit -> mis-size), which is the #6 bug.
M2. es40_v7_3 (and the DS10/DS20 pc264 consoles) are 3-bit: encode <=4 GB as
    <=4 arrays of <=1 GB (ASIZ 1..7).  Never emit a 4-bit ASIZ (>=0x8) to a 3-bit
    console.  m_extendedAsizDecode MUST stay false for these firmwares.
M3. Fail-fast (refuse, do not silently truncate) when requested memory exceeds the
    (Cchip, firmware) representable maximum -- as EmulatR does today for ES40 >4 GB.
M4. The >4 GB path requires BOTH a Typhoon/Titan Cchip (32 GB PA) AND a 4-bit
    console; set m_extendedAsizDecode=true ONLY when a 4-bit-decoding firmware is
    loaded.  Determining a firmware's decode width is a per-image fact (verify
    against the IMAGE / a boot, not a source snapshot -- see the cserve-0x66 and
    this seam's lessons).

## 7. ES45/Titan enablement path (deferred; the definitive 4-bit proof)

Because Titan (21274) uses a DIFFERENT memory-config register file than the
Tsunami AAR, es45_v7_3 has no Tsunami-style get_array_size (>>12 & mask on
CSR_AAR0) to disassemble; its decode lives in Titan memory CSRs (MMRs).  The
definitive proof that es45_v7_3 is 4-bit is therefore the ENABLEMENT ITSELF:
  1. Model the Titan memory-config CSRs (array/base/size) for the ES45 tier.
  2. Set m_extendedAsizDecode=true for the Titan tier (4-bit firmware).
  3. Encode >4 GB faithfully (single 4-bit array or multi-array per Titan).
  4. Boot es45_v7_3 at 8 GB; the P00 "Memory Testing and Configuration Status"
     reporting 8192 MB IS the proof of the 4-bit seam.
Overlaps task #5 (es45/ds25 platform badges) and the Titan chipset (experimental).

## 8. References

  - AXPBox 1.1.2 src/System.cpp: chipset PA limits (V1 4 GB / Typhoon 32 GB, the
    "System memory / Pchip0 / TIGbus" PA map), and the 4-bit ASIZ encoding
    (memoryBits-23)<<12 (single-array).  Runs the cl67 4-bit console.
  - apisrm/apisrm/ref: tsunami.h (aar_m_asiz=0x7000, CSR_AAR0..3),
    memconfig_pc264.c get_array_size (>>12 & 7).
  - EmulatR: chipsetLib/TsunamiCchip.h (m_extendedAsizDecode); the #6 AAR-ASIZ
    fix journal 20260710_es40_memtest_acv_RESOLVED_aar_asiz_and_tiling.md.
  - Tasks: #6 (AAR fix, closed), #21 (large-mem test, closed), #23 (ES45/Titan
    >4 GB path), #5 (es45/ds25 badges).
  - Sibling design HRM: 20260711_es40_headless_console_design_hrm.md.
