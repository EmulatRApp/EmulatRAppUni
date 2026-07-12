<!--
EmulatR V4 -- Tsunami/Typhoon Tier-0 change proposal (FOR WEB REVIEW).
Discuss-before-code artifact: exact current state -> proposed edit, file:line,
with evidence, per the 20260711 re-audit ledger Tier 0.  No code changed yet.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
ADR-0001 header; ASCII(128); hex radix.  HRM = tsunami_typhoon_21272_hrm.txt Rev 4.0.
-->

# Tsunami/Typhoon Tier-0 Change Proposal -- for review (2026-07-11)

Scope: the cheap, HRM-authoritative, ZERO-boot-risk items from the re-audit ledger
Tier 0.  Each is verified against the LIVE code.  Two items the audit lumped into
Tier 0 are RECOMMENDED FOR RELOCATION (below) -- please confirm.

## PROPOSED NOW (safe, HRM-authoritative)

### T0-1  DELETE the dead "Arbiter Gatekeeper" decoder  [T-HY1]
FILE: chipsetLib/TsunamiPchip.h
CURRENT: an entire alternate decode path exists and is UNREFERENCED:
  - routeMmioRead (:277) / routeMmioWrite (:307)      -- 0 external callers
  - handleCsrRead (:1343) / handleCsrWrite (:1384)     -- 0 external callers, and
    use WRONG 8-byte register spacing (0x000/0x008/...) vs the live readCSR's 0x40
  - handleSparseMemWrite (:1445) / handleDenseWrite (:1454) -- DEREFERENCE
    m_pciMemory, which is declared nullptr (:1529) and NEVER assigned (0 external
    assigns).  A latent nullptr crash if ever wired.
EVIDENCE: grep across chipsetLib/systemLib/eBoxLib/pipelineLib/mmuLib -> all six
functions have 0 external references; m_pciMemory has 0 non-nullptr assignments.
EDIT: delete the "Bus Arbiter Gatekeeper Interface" block (:275-340) and the four
handler methods (:1343-1458), plus the now-orphaned member IPciMemoryHandler*
m_pciMemory (:1529) and its include/type if used nowhere else.
RISK: none (unreachable code).  Removes a latent crash + a wrong-spacing trap.

### T0-2  MTR 0x040 -- honor writes (HRM Type=RW)
FILE: chipsetLib/TsunamiCchip.h
CURRENT: writeCSR case Cchip::MTR (:1187-1189) logs "MTR(ignored)" and drops the
write; reset value m_mtr=0x000000EF00000000 (:419) is correct.  HRM Table 10-7
lists MTR Type=RW (the SRM programs SDRAM timing and reads it back).
EDIT: replace the no-op body with `m_mtr = value; return;` (store; no timing side
effect is modeled -- storage is the faithful minimum for a read-back-consistent
RW reg).
RISK: none -- the ES40 boot does not write MTR (latent faithfulness fix).

### T0-3  DELETE dead DREV reset helper + constants
FILE: chipsetLib/Tsunami21272_CsrSpec.h
CURRENT: resetDchipDrev() (:485-491) and Dchip::DREV_RESET_TSUNAMI=0x10 /
DREV_RESET_TYPHOON=0x20 (:433-434) are dead: resetDchipDrev has 0 call-sites, and
the constants feed ONLY resetDchipDrev.  The LIVE DREV reset is the byte-sliced
m_drev=0x0101010101010101 (TsunamiDchip.h:128), whose comment states the variant
0x10/0x20 was the WRONG encoding.
EDIT: delete resetDchipDrev() and both DREV_RESET_* constants.
RISK: none (0 live callers).

### T0-4  Comment corrections (fold in with T0-2)
FILE: chipsetLib/TsunamiCchip.h
CURRENT: header comments (:52-55) label CSC/MTR/MPD "RO"; the write-switch comment
(:1116) says "RO-from-software registers -- writes ignored per HRM (CSC, DIR)."
Per HRM Table 10-7, CSC/MTR/MPD are Type=RW (only specific bit-FIELDS are RO/from
pins).  DIR is genuinely RO.
EDIT: correct the CSC/MTR/MPD "RO" labels to "RW"; keep DIR RO.  Comment-only.

## RECOMMENDED FOR RELOCATION (please confirm before I treat as Tier 0)

### R-1  CSC + MPD "honor writes" -> move to Tier 1 (#30), NOT Tier 0
WHY: they are Type=RW at the register level but are NOT simple storage:
  - CSC has RO bit-fields (CPU-present, revision, P1P<14>) sourced from pins; a
    blanket RW-store would let a firmware write CLOBBER the CPU-present/rev bits it
    later reads back = a real regression.  The faithful fix is FIELD-AWARE (preserve
    RO bits, store only RW bits) -- which IS the Tier 1 "CSC faithful fields" work
    (#30 item 7).  Do them together.
  - MPD is the I2C SPD bit-bang interface (DR/CKR/DS/CKS bits), not a data register;
    a blanket RW-store breaks the bit-bang (input bits must reflect the I2C slave).
    The faithful fix is the I2C interface (Tier 1 MPD work).
So T0-2 covers MTR only (clean storage); CSC/MPD land with their Tier 1 semantics.

### R-2  Clock revert (2^28 -> per-model) -> move to #24 (timebase), NOT Tier 0
WHY: the current EMULATR_PROFILE_ALPHA_CLOCK_HZ = 2^28 (CsrSpec.h:562) is a
DELIBERATE 2026-06-02 EXPERIMENT that lowers the interval clock 1e9 -> 2^28 to make
firmware real-time delays ~4x FASTER (a boot-speed hack), NOT the faithful value.
The faithful per-model values are documented right above it: ES40 600 MHz -> bit 19
(~1144 Hz), ES45 1 GHz -> bit 20 (~953.7 Hz), both ~12% of HRM 1024 Hz nominal.
Reverting it (a) makes the boot ~4x SLOWER in real time -- risking the just-
stabilized boot-to-P00 and needing a MAXCYC re-check -- and (b) changes the reported
CPU speed, which is exactly task #24 (the "5 MHz" RPCC-vs-tick timebase).  It is a
TIMEBASE change, not a hygiene fix, so it belongs in #24 with a full DS10/DS20/ES40
boot re-verification -- not landed blind in Tier 0.

## Net Tier-0 (if R-1/R-2 confirmed): T0-1..T0-4 only
All four are zero-boot-risk (unreachable-code delete; a reg the boot never writes;
dead constants; comments).  Do-no-harm gate: rebuild + doctest suite green + DS10/
DS20/ES40 still reach P00 (should be byte-identical -- none touches a live path).

## UPDATE 2026-07-11 -- T0-2/3/4 LANDED; T0-1 EXTENT REFINED (needs re-confirm)

LANDED (clean, ASCII, 0 dangling refs, header-only -- do-no-harm gate pending with T0-1):
  - T0-2 MTR write-honor: TsunamiCchip.h case Cchip::MTR now `m_mtr = value;` + doc.
  - T0-3 dead DREV: removed DREV_RESET_TSUNAMI/TYPHOON + resetDchipDrev() (CsrSpec.h).
  - T0-4 comments: CSC/MTR/MPD "RO"->"RW" header labels WITH field notes (CSC:
    CPU-present/rev/P1P<14> RO-from-pins; MPD: I2C SPD, DR/CKR RO inputs); the
    write-side CSC comment now says "deferred-RW/T-SM3", not RO.

T0-1 REFINED (reading the code EXPANDED the dead set 6 -> 10 functions -- re-confirm before cut):
  The dead "Arbiter Gatekeeper" is a fully self-contained subgraph.  routeMmioWrite
  also calls handleConfigWrite + handleSparseIoWrite, and handleConfigRead is a total
  orphan -- none were in the original 6.  ALL provably dead (widened grep incl.
  generated/indirect/function-pointer: 0 external refs; routeMmio* have 0 callers at
  all; each handle*/offsetToBDF is reachable ONLY from routeMmio* or not at all).  The
  LIVE decode path (readCSR/writeCSR + readPciConfig0/writePciConfig0) is entirely
  separate and untouched.

  DELETE (TsunamiPchip.h):
    A. class IPciMemoryHandler (:166-172)          -- used only by m_pciMemory
    B. routeMmioRead (:277-305) + routeMmioWrite (:307-340)  -- the Gatekeeper block
       [PRESERVE setIoPortHandler at :342 -- it is LIVE]
    C. handleCsrRead/handleCsrWrite/handleSparseMemWrite/handleDenseWrite/
       handleConfigRead/handleConfigWrite/handleSparseIoWrite + offsetToBDF (:1340-1522)
    D. member IPciMemoryHandler* m_pciMemory (:1529)  [PRESERVE m_ioPortHandler at :1528]
  PRESERVE (live; the dead code READ these but does not own them):
    members m_wsba/m_wsm/m_tba/m_pctl/m_plat/m_perror/m_perrmask/m_pmonctl/m_pmoncnt/
    m_pciDevices/m_ioPortRegistry/m_ioPortHandler; methods setIoPortHandler, readCSR/
    writeCSR, readPciConfig0/writePciConfig0.
  Extra tells the block is dead scaffolding: handleSparseMemWrite/handleDenseWrite
  deref the always-null m_pciMemory (latent crash) AND reference the garbage token
  `fmt::detail::state::width` (compiles only because never instantiated on a live path).

## What is needed from the architect / Web
1. Approve T0-1..T0-4 as written (or adjust).
2. Confirm R-1 (CSC/MPD RW-honor -> Tier 1 field-aware) and R-2 (clock revert -> #24
   timebase).  Recommendation: confirm both -- they avoid a regression (CSC clobber)
   and a boot-timing risk (clock) respectively.
3. Nothing else needed -- the HRM + live code are in hand.
