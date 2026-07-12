<!--
EmulatR V4 -- T-TOPO (ChipsetTopology SSOT) design shape (FOR WEB REVIEW).
Tier-1 foundation: one latched struct from which every Tsunami/Typhoon presence/
population bit derives, so CSC/DSC/PCTL stop fabricating them independently.
Discuss-before-code artifact -- no code changed.  ASCII(128); hex radix.
HRM = tsunami_typhoon_21272_hrm.txt Rev 4.0.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
-->

# T-TOPO -- ChipsetTopology SSOT: design shape (2026-07-11)

## 1. Why

Presence/population bits are fabricated independently today (e.g. TsunamiCchip.h:
399-405 puts a CPU-present bitmask into CSC<3:0> AND ORs BC into the same bits =
collision).  T-TOPO is ONE latched struct of topology FACTS + HRM-correct
accessors; CSC/DSC/PCTL then DERIVE their bits from it instead of each inventing
them.  T-TOPO does NOT change any register's on-wire encoding -- that is the
consumer work (CSC = #30) which verifies against firmware.  T-TOPO only supplies
the facts and the field math.

## 2. HRM bit-field map (the derivation targets)

CSC 0x000 (Cchip):
    BC      <1:0>  RO   base configuration (from variant/pins)
    C0CFP   <2>    RO   CPU0 clock-forward-preset  (CPU0 present)
    C1CFP   <3>    RO   CPU1 clock-forward-preset  (CPU1 present)
    P1P     <14>   RO   Pchip 1 present
    (DWTP<17:16>, IDDW<13:12>, AW<8>, SFD<6> ... are TIMING/config, NOT topology --
     out of T-TOPO scope; leave as-is / their own tasks.)
DSC 0x800 (Dchip, byte-sliced x8, mirrors CSC's CPM-driven bits):
    BC      <1:0>  RO   base configuration
    C0CFP   <2>    RO   CPU0 present
    C1CFP   <3>    RO   CPU1 present
    C2CFP   <4>    RO   CPU2 present
    C3CFP   <5>    RO   CPU3 present
    P1P     <6>    RO   Pchip 1 present
    (powers up to CPM<6:0> from the Cchip -> DSC is the FULL 4-CPU CxCFP view; CSC
     only carries CPU0/1.  Read as a quadword with the byte repeated x8.)
PCTL 0x300 (per Pchip; PID pins):
    PID     <47:46> RO  Pchip ID (0 for Pchip0, 1 for Pchip1)
    RPP     <45>    RO  Remote Pchip present (the OTHER hose populated)

## 3. Topology FACTS (the SSOT inputs)

    struct ChipsetTopology {
        ChipsetVariant variant;        // Tsunami | Typhoon  (have it: m_variant)
        uint8_t        cpuPresentMask; // bit n = CPU slot n populated (1..4)  (derive from cpuCount today)
        bool           pchip1Present;  // hose 1 populated  (NEW input -- see 6)
        uint8_t        bcConfig;       // BC<1:0> base config (from variant/board)  (NEW)
        // (Bcache size/present -- future, feeds show-config Bcache; not a CSC bit)
    };

Latched ONCE at chipset construction/reset from the manifest-derived inputs; never
mutated by CSR writes (these are RO-from-pins facts).

## 4. Accessors (the SSOT API -- the ONLY place field math lives)

    cscTopoBits()  -> uint64  : BC<1:0> | (C0CFP<2> if cpu0) | (C1CFP<3> if cpu1) | (P1P<14> if pchip1)
    dscTopoBits()  -> uint8   : BC<1:0> | CxCFP<5:2> for each present CPU | (P1P<6> if pchip1)
                                (the byte the Dchip replicates x8)
    pctlPid(hose)  -> uint64  : hose==0 ? 0 : 1   (into PID<47:46>)
    pctlRpp(hose)  -> bool    : the OTHER hose present (Pchip0.RPP = pchip1Present;
                                Pchip1.RPP = true (Pchip0 always present))
    cpuPresent(n)  -> bool ; cpuCount() -> int   (convenience)

## 5. Consumers (derive, do not fabricate)

  - TsunamiCchip CSC reset: `m_csc = (timing/config fields) | cscTopoBits();`
    (replaces the :399-405 CPU-loop + BC collision -- but see 7: the ENCODING
     change is the #30 CSC task, gated on firmware verification.)
  - TsunamiDchip DSC reset: `m_dsc = replicate8(dscTopoBits());`  (+ DSC P1P<6>)
  - TsunamiPchip PCTL reset (per hose): PID<47:46> = pctlPid(hose);
    RPP<45> = pctlRpp(hose).  (Unblocks "SRM can't see Pchip1" once #27 lands.)

## 6. NEW input plumbing required

pchip1Present and bcConfig are NOT chipset inputs today (only cpuCount + variant).
They are per-MODEL facts:
    ES40  : dual-hose  -> pchip1Present = true   (manifest already notes it)
    DS10/DS20 : single hose -> pchip1Present = false
Plumb them from the platform manifest (<model>_platform.json) via PlatformConfig
into the chipset ctor (a small ctor-arg / topology-struct addition).  This is part
of T-TOPO.  _PROVISIONAL any board value until manifest-confirmed.

## 7. SCOPE BOUNDARY + the one risk to flag

T-TOPO delivers the FACTS + HRM-correct accessors ONLY.  It is behavior-neutral by
itself IF the consumers keep their current on-wire values until each is converted.
The CSC RE-ENCODING (dropping the CPU-present-in-<3:0> mask for the HRM CxCFP/BC
layout) is the #30 CSC task and carries a REAL RISK: the current <3:0> CPU-mask may
be what the SRM firmware actually reads (reverse-engineered), even though it does
not match the HRM field names.  So #30 must VERIFY against a boot trace -- does the
SRM read CSC<3:0>, and does it interpret it as a CPU mask or as CxCFP/BC? -- BEFORE
changing the encoding.  Do NOT let T-TOPO silently flip the CSC bits; land the SSOT
first (accessors return the SAME bits the code emits today, refactored to one
place), then #30 changes the encoding with firmware evidence.  This keeps T-TOPO a
zero-behavior-change refactor -- verifiable byte-identical, like Tier 0.

## 8. Proposed landing order (each verifiable)

  T-TOPO.1  Add ChipsetTopology struct + accessors; latch from existing inputs
            (variant, cpuCount).  Accessors return TODAY's emitted bits (behavior-
            neutral).  Refactor CSC/DSC/PCTL reset to CALL the accessors.  Gate:
            byte-identical boot (DS10/DS20/ES40 to their current endpoints).
  T-TOPO.2  Plumb pchip1Present + bcConfig from the manifest into the struct.
            Gate: still byte-identical (the values match today's implicit ones
            until a consumer uses them).
  THEN #30 (CSC) consumes it and changes the CSC encoding WITH firmware verification;
       #27 (Pchip1) consumes pctlPid/pctlRpp when the real second hose lands.

## 9. What is needed from Web / architect

  1. Approve the ChipsetTopology struct shape (section 3) + accessor API (section 4).
  2. Confirm the SCOPE BOUNDARY (section 7): T-TOPO = behavior-neutral SSOT refactor
     returning today's bits; the CSC encoding change stays in #30 with a firmware
     trace.  (Recommendation: confirm -- it keeps the foundation a byte-identical
     refactor and defers the one risky encoding decision to where it gets evidence.)
  3. Confirm the input-plumbing approach (section 6): pchip1Present/bcConfig from
     the platform manifest via PlatformConfig.
