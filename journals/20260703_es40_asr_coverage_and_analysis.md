<!--
EmulatR V4 -- ES40 Authoritative Reference (ASR) Coverage and Analysis
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-07-03
Purpose: assess the authoritative reference sources available for the ES40
platform, analyze in depth the layers that are covered, and enumerate the
gaps precisely so the missing references can be located. Analysis/design
artifact, NOT generated code. Cowork is the source of truth for live file
state; this brief reasons from the reference documents only.
ASCII(128) only.
-->

# ES40 Authoritative Reference (ASR) Coverage and Analysis

## 0. Scope and an access finding to record first

The intent was for this analysis to draw on the full PALcode / processor-
support documentation hive. That hive is NOT reachable from the web
session that produced this brief. The path used on the working tree
(processor-support directory) is not mounted here. What is in hand is the
six reference documents carried in the project (Section 1). This matters
because it bounds what can be analyzed authoritatively now versus what is
blocked pending references you would need to locate (Section 5).

Every coverage verdict below is grounded by keyword census across the six
documents, not asserted from memory.

## 1. ASR in hand

    Document                                    Bytes     Authoritative for
    alpha_arch_ref.txt (AARM)                   2,227,715 Common Arch, OpenVMS/OSF
                                                          PAL, Console Interface
                                                          Architecture (HWRPB,
                                                          MEMDSC, CTB, CRB, CONFIG).
    tsunami_typhoon_21272_hrm.txt               905,337   ES40 chipset: Cchip,
                                                          Dchip, Pchip, memory,
                                                          IIC/TIG, interrupts.
    Alpha_21264-EV67 HRM                        1,339,033 EV6 core: IPRs, PAL entry
                                                          vectors, MMU, interrupts.
    EV6_Specification_Rev_2_0_199604.txt        527,099   EV6 pre-release detail,
                                                          PAL coding restrictions.
    palcode_dsgn_gde.txt                        132,949   CALL_PAL environment,
                                                          PAL dispatch mechanics.
    Titan_Chipset (EK-ES450, 21274)             820,135   ES45 (Titan). Family
                                                          reference; NOT ES40.

## 2. ES40 stack decomposition and coverage verdict

The path from power-up to the SRM prompt on ES40 decomposes into five
layers. Verdicts: HAVE (authoritative doc in hand), SPEC (architecture
spec in hand but platform-specific SOURCE absent), GAP (no authoritative
reference in hand).

    Layer                              Verdict   Primary reference in hand
    L1 EV6 core (reset/IPR/PAL/intr)   HAVE      EV6 HRM, EV6 Spec, PAL guide
    L2 Tsunami 21272 chipset           HAVE      21272 HRM
    L3 Console / HWRPB / PAL arch      SPEC      AARM (spec); source absent
    L4 South bridge + SuperIO + IIC    GAP       none for ALi M1543C
    L5 Boot storage (on-board SCSI)    GAP       none for Symbios/QLogic

Keyword census (hits, core docs): ALi M1543C / vendor 0x10b9 = 0 in every
core doc. Cypress CY82C693 (the coded stand-in) = 0. Symbios/QLogic SCSI
= 0 in core docs. PCF8584 = 0. SuperIO FDC37C669 / 82077 = incidental only.
By contrast Pchip = 814 (21272 HRM), IIC/TIG = 133, DRIR/DIM interrupt = 62,
console CTB/CRB/CONFIG = 267 (AARM), CALL_PAL/CSERVE = 251 (AARM) + 56 (PAL
guide), EV6 PAL-entry/IPR = 195 (EV6 HRM). The split is clean: L1/L2 are
richly covered; L4/L5 are absent; L3 is spec-rich but source-poor.

The important structural point: the boot is currently blocked at L2
(the 4GB AAR memory-sizing defect), which is fully analyzable from the ASR
in hand. The NEXT wall is L4 (south-bridge init, ALi vs Cypress stand-in),
which is NOT analyzable from the ASR in hand.

## 3. In-depth analysis -- covered layers

### 3.1 L1: EV6 / 21264 core (HAVE)

Authoritative and complete for everything ES40 init exercises at the CPU
level:

- Reset / retire-mapper init sequence. EV6 HRM Appendix D and EV6 Spec
  Appendix 2 (Restriction 1) specify the first-80-instruction not-done
  sweep and the map-before-use rule. This governs how V4's reset stream
  must behave; already reflected in the working notes.
- PALcode entry-vector table. palBase + fixed offsets (RESET, IACCVIO,
  INTERRUPT at +0x100, ITB/DTB miss, OPCDEC at +0x400, etc.), plus the
  CALL_PAL dispatch curves (privileged palBase+0x2000+64*func;
  unprivileged palBase+0x1000+64*(func-0x80)). Fully specified. The
  OPCDEC-handler and INTERRUPT-vector findings in the 2026-05-19 notes
  were validated against this table.
- IPR set for HW_MFPR / HW_MTPR. Complete in the EV6 HRM. This is the
  reference the _PROVISIONAL-until-HRM-verified rule protects.
- Interrupt arbitration (IPL, ASTRR/ASTEN, hardware interrupt lines).
  Complete. The chipset side of delivery is L2 (Section 3.2).

Verdict: no ES40 gap at L1. The core is the best-covered layer.

### 3.2 L2: Tsunami 21272 chipset (HAVE)

The 21272 HRM is authoritative for the entire ES40 chipset and is where
the live blockers live. The ES40-relevant register/behavior set, tied to
current work:

- Memory sizing (AAR0-3, MTR). The active defect. AAR field layout and
  ASIZ encoding (Table 10-14) are quoted in the AAR action plan; Tsunami
  tops at 1GB/array, so 4GB ES40 is four 1GB arrays. Fully analyzable now;
  no reference gap.
- CSC platform strap. CSC (Section 10.2.2.1) bits <7:0> are read-only and
  initialized from Cchip pins at power-up; bits <13:8> track Dchip STR.
  Those low strap bits are the platform-identity input the DS20 badge work
  seeds (CSC<7:0> TIGbus strap). The HRM gives the field map; what value
  ES40 straps is a platform fact (Section 4.3), but the mechanism is fully
  documented here.
- Interrupt latch vs deliver. DRIR / DIMn / DIRn and the drir & dim ->
  b_irq eval, plus MISC<ITINTR>/<IPINTR> and the interval timer, are all in
  the HRM (Sections 10.2.x). This is the reference for the latch-then-
  divert interrupt path already sketched in CchipPhaseA notes.
- IIC / TIGbus / MPD. The Cchip TIGbus and the MPD (memory presence
  detect) serial path are documented (Sections 9.10, 10.2.2.4). This is
  the chipset-level mechanism behind device-presence probing; note the
  DS20 0x9e discriminator itself is a firmware/source fact (L3/platform),
  but the bus mechanism it rides on is here.
- Pchip / dual-hose. The 21272 supports one or two Pchips; ES40 populates
  both (dual-hose). The HRM documents the shared CAPbus/PADbus, the FPQ/
  TPQM/TPQP queues shared across two Pchips, and the DMA window CSRs
  (WSBAn/WSMn/TBAn, PCTL, TLBIV/TLBIA). V4 currently models Pchip1 as a
  coarse all-ones mirror; the HRM is sufficient to replace that with a real
  second-hose model when needed.

Verdict: no ES40 gap at L2. Everything the chipset does during init is
specified. The AAR defect and the badge/CSC work are both fully sourced.

### 3.3 L3: Console / HWRPB / PAL architecture (SPEC; source absent)

The AARM is authoritative for the architecture; the ES40-specific PAL and
console SOURCE is not in hand.

- HWRPB and MEMDSC. Fully specified (AARM Chapters 26-27); the MEMDSC
  cluster format and validation checksum are quoted in prior work. No gap
  at the spec level.
- CTB / CRB / console callbacks, CONFIG block, DSRDB. Specified in the
  AARM console chapter (267 census hits). Sufficient to reason about the
  console data structures ES40 builds.
- CALL_PAL / CSERVE dispatch mechanism. Specified (AARM + PAL guide). BUT
  the OpenVMS-personality CSERVE codes the ES40 PAL actually implements
  (0x44 MTPR_EXC_ADDR, 0x45 JUMP_TO_ARC, 0x46 IIC_WRITE, 0x65
  MP_WORK_REQUEST, 0x66) are defined by the platform PAL SOURCE
  (ev6_vms_pc264_pal.mar or an ES40 variant), not by the generic spec. The
  mechanism is covered; the exact per-code behavior is a SOURCE fact
  (Section 4.3).

Verdict: architecture COMPLETE; platform PAL/console source ABSENT. Any
analysis that depends on exact ES40 PAL behavior (e.g. the get_sysvar
platform-detect path, the CSERVE semantics) is blocked on source, exactly
as the DS20 0x9e discriminator was resolved only by reading pc264.c /
iic_driver.c on your tree.

## 4. Gaps -- what is missing and what it blocks

### 4.1 L4 south bridge: ALi M1543C (real) and CY82C693 (stand-in) -- GAP

The real ES40 south bridge is the ALi M1543C (vendor 0x10b9). The ES40
manifest models the Cypress CY82C693 as a stand-in and flags that the ES40
SRM probes the ALi at its own BDF/config and may diverge during south-
bridge init. Neither the ALi datasheet nor a Cypress CY82C693 datasheet is
in hand. This is the FIRST wall past the L2 memory-sizing fix, and it is
not analyzable from the ASR in hand.

Blocks: south-bridge init fidelity, ISA/PIC/UART/IDE/SuperIO behind the
bridge, and any decision about whether to model the ALi natively or extend
the Cypress stand-in.

### 4.2 L5 boot storage: on-board SCSI (Symbios / QLogic) -- GAP

ES40 boots on-board Symbios/QLogic SCSI; the manifest carries a DS10-style
IDE/ATAPI stand-in. No Symbios 53C8xx or QLogic ISP datasheet, and no SRM
SCSI driver source, in hand. Downstream of reaching the prompt, so not on
the console-init critical path, but required for an actual boot.

### 4.3 L3 platform PAL / console SOURCE (apisrm/ref, ES40 variant) -- GAP

The generic architecture is covered; the platform-specific source is not.
Needed to resolve: the exact CSERVE code semantics for the ES40 PAL
personality; the ES40 platform-detect / get_sysvar path (the ES40 analogue
of the DS20 0x9e finding); the CSC strap value ES40 asserts; and any ES40-
specific console init step. This is the same class of source that resolved
DS20, and it is absent here.

### 4.4 ES40 board BDF topology -- GAP (trace-substitutable)

The manifest's PCI slot/BDF assignments are _PROVISIONAL (DS10-derived).
The 21272 HRM covers Pchip config mechanics but not the ES40 board-level
device map. Resolvable either from an ES40 service/technical manual or from
an ES40 PCICFG trace.

### 4.5 Peripheral datasheets -- GAP (relevance to confirm)

PCF8584 (I2C), FDC37C669 / SMC SuperIO, 82077AA FDC are referenced as
project resources but absent here, and some may be DS10-path-only rather
than ES40 (ES40 IIC rides the Cchip TIGbus/MPD per L2; the PCF8584 may not
be on the ES40 path at all). Confirm ES40 relevance before sourcing.

## 5. Prioritized sourcing list (boot-critical-path order)

To locate/upload, in the order they gate the ES40 path to the prompt:

1. ALi M1543C datasheet (PCI-ISA bridge / IDE / SuperIO integration).
   Highest priority: the next wall after the AAR fix. If native ALi
   modeling is deferred in favor of extending the Cypress stand-in, then a
   Cypress CY82C693 datasheet is the substitute need.
2. ES40 platform PAL/console source (apisrm/ref: the ES40 platform C file
   analogous to pc264.c, plus the ES40 PAL .mar). Resolves get_sysvar/
   platform-detect, CSERVE semantics, and the CSC strap value.
3. ES40 board BDF map OR an ES40 PCICFG trace. Resolves the _PROVISIONAL
   topology in the manifest.
4. On-board SCSI datasheet + SRM SCSI driver source (Symbios/QLogic).
   Needed for actual boot, not for reaching the prompt.
5. PCF8584 / SuperIO / FDC datasheets -- only after confirming they are on
   the ES40 path rather than DS10-only.

## 6. What can proceed now vs. what is blocked

Proceed now (fully sourced from the ASR in hand):
- The L2 4GB AAR memory-sizing fix (AAR action plan).
- The CSC strap mechanism for platform identity (mechanism is L2; the ES40
  strap VALUE is item 2 above).
- Any L1 core-behavior question and any HWRPB/MEMDSC (L3 spec) question.

Blocked pending references (Section 5):
- South-bridge init fidelity (item 1).
- ES40 platform-detect / CSERVE-code behavior / strap value (item 2).
- Exact board topology (item 3).
- Boot-device bring-up (item 4).

Recommendation: treat the AAR fix as the current front (no references
missing), and in parallel locate items 1 and 2, since they jointly gate
everything between the memory-sizing fix and the ES40 prompt. Items 3-5 can
follow.
