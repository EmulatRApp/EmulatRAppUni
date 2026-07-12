<!--
EmulatR V4 -- Tsunami/Typhoon (21272) RE-AUDIT: current-state punch list.
Reconciles the 2026-07-07 HRM faithfulness ledger against the LIVE code (the
2026-07-07 cites were partly stale -- much of the P1 reset/RO-RW cluster has
since landed).  Establishes what remains for "audit-safe complete" before any
Titan (21274 / ES45) work.  Verified against live chipsetLib code + HRM Rev 4.0.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
HRM cites: tsunami_typhoon_21272_hrm.txt (EC-RE2CA-TE Rev 4.0).
-->

# Tsunami/Typhoon (21272) Re-Audit -- Current-State Ledger (2026-07-11)

## 0. Headline

The P1 reset-value + RO/RW cluster from the 2026-07-07 audit is ESSENTIALLY
LANDED: MTR (reset 0xEF00000000, PHCW=14/PHCR=15), MPD (0x0F), TTR (0x7330),
TDR, PRBEN (reset 0), STR (0x2828...), DREV (0x0101..., RO byte-sliced) reset
values are all CORRECT now; DSC/DSC2 are RO; MISC CAS/W1C/W1S mostly wired.  So
the old ledger reads much worse than the live code is.  What actually blocks
"audit-safe complete" is a small hygiene/RW batch, a Cchip/Dchip semantics
batch, and the two large ES40 architectural items (real Pchip1 hose + DMA
engine), which are CONSUMER-BLOCKED and must not land blind.

Key HRM cross-checks: Table 10-7 -- CSC/MTR/MPD Type=RW; PRBEN Type=Special;
DSC2 RO; DREV RW.  CSC P1P<14> RO "Pchip1 present" (L17729/L18002); DSC P1P<6>
RO (L19245); PCTL PID<47:46>/RPP<45> RO from pins (L19601-19766).  DPR/RMC: NO
hits in the 21272 HRM -- board-level, not a chipset CSR.

## 1. Cchip

    CSC 0x000     OPEN    CPU-present fabricated into bits[3:0] (collides w/ BC);
                          P1P<14> never set; PBQMAX/IDDW/IDDR absent; whole reg RO
                          (HRM: bits>15 RW).  TsunamiCchip.h:399-405,1121.  M
    MTR 0x040     PARTIAL reset correct; WRITES IGNORED :1187 (HRM Type=RW).  S
    MPD 0x0C0     PARTIAL reset 0x0F correct; writes ignored :1184; no I2C SPD.  S/M
    AAR0-3        PARTIAL ASIZ (incl extended, m_extendedAsizDecode) faithful;
                          SA<8> unmodeled; ROWS/BNKS hardcoded 2/1 :1564.  M
    PRBEN 0x340   PARTIAL reset 0 correct; plain RW storage (HRM=Special:
                          read-to-CLEAR/write-to-SET, per-CPU).  :864,1054.  M
    IIC0-3        OPEN    CsrSpec names them Interval-Ignore-Count, but CODE still
                          models IPI storage (m_iic/sendIPI :944,1085); no ICNT
                          decrement, no OF<24>.  fireIntervalTimer TODO :722.  M
    MPR0-3        OPEN(lo)reads 0, writes ignored (WO SDRAM mode).  S
    TTR 0x580     CLOSED  reset 0x7330, RW.
    TDR 0x5C0     CLOSED  reset 0, RW.
    MISC 0x080    PARTIAL CAS/W1C/W1S/IPREQ->IPINTR/ABT->ABW wired; remaining:
                          ABW first-set LOCK not enforced; DEVSUP TODO :1508.  S/M
    Int-timer clk OPEN    EMULATR_PROFILE_ALPHA_CLOCK_HZ still the 2^28 experiment
                          (CsrSpec.h:554) -> ~4x fast ticks.  Revert (T-HY3).  S
    PWR/CMONCTL/WDR/MCTL OPEN(lo) no model; RAZ/storage.  S

## 2. Dchip (each = one uint64; TsunamiDchip.h:297)

    DSC 0x800     PARTIAL reset 0x01, RO (fixed); P1P<6> reads 0; not byte-sliced. M
    DSC2 0x8C0    CLOSED  reset 0, RO.
    STR 0x840     PARTIAL reset 0x2828... correct, RW; write stores raw (no per-byte
                          replicate); STR-write -> CSC<13:8> sync NOT wired.  M
    DREV 0x880    CLOSED  reset 0x0101..., RO, byte-sliced reads correct.  (Dead
                          DREV_RESET_*=0x10 constant in CsrSpec.h:433 -- comment sweep.)
    8-way slice   OPEN(lo)RO regs already read correct replicated resets; only the
                          STR WRITE path drifts from per-Dchip slice.  L

## 3. Pchip (one instance only; TsunamiChipset.h:850)

    WSBA/WSM/TBA  OPEN    raw storage, never consulted; WSBA3 SG<1> should RO=1.  L
    PCTL 0x300    OPEN    raw store; PID<47:46>/RPP<45> always 0 -> SRM sees no
                          remote Pchip; HOLE/MWIN/PTEVRFY inert.  M/L
    PERROR 0x3C0  PARTIAL W1C/W1S correct; NO error source ever sets a bit; no
                          freeze/lock/LOST/SYN/CMD/ADDR; no PERRMASK IRQ0.  M
    PERRMASK/TLBIV/TLBIA/PMONCTL/... OPEN(lo) storage/sinks.  S-M
    DMA engine    OPEN    ABSENT: no direct-map / SG PTE / SG TLB / monster window /
                          hole.  No DMA datapath through Pchip at all.  L
    PCI config    PARTIAL Type-0 BDF + miss=all-ones FAITHFUL; Type-1 absent; IDSEL
                          one-hot / dev>20 mask absent; BARs don't drive dense decode. M/L
    Pchip1 hose   OPEN    0x802/0x803 = all-ones mirror; ES40 populates BOTH hoses.
                          MUST land WITH the P1P/RPP/PID presence bits.  L
    Arbiter Gate  OPEN    routeMmio*/handleCsr* (TsunamiPchip.h:277-340,1343-1441):
    (dead code)           0 callers, WRONG 8-byte reg spacing (vs 0x40), deref
                          nullptr m_pciMemory (latent crash).  DELETE (T-HY1).  S

## 4. TIG / board-level

    TIG reg file  CLOSED  smir/halt/clr_irq4/CPU-START/arb faithful (prior audit).
    DPR/RMC       OPEN    dual-port RAM @0x801_1000_0000; demanded by "TIG load
                          failure"/show config; NOT a 21272 CSR (board-level).
                          Task #25.  M

## 5. Cross-cutting

    Topology SSOT OPEN    ChipsetTopology (T-TOPO) not built; all presence bits
                          (CSC P1P<14>, DSC P1P<6>, PCTL RPP/PID, BC population,
                          CPU-present mask) should derive from one latched struct.
                          Foundation for the dual-hose presence work.  M

## 6. AUDIT-SAFE COMPLETE requires (OPEN/PARTIAL only; value/effort order)

TIER 0 -- high value, S effort, HRM-authoritative (do first):
  1. DELETE dead Arbiter Gatekeeper decoder (latent nullptr deref + wrong spacing) -- T-HY1.
  2. Make MTR / MPD / CSC honor WRITES (HRM Type=RW; silently dropped today).
  3. Revert EMULATR_PROFILE_ALPHA_CLOCK_HZ 2^28 -> profile clock (~4x timer skew), under a boot-timing check -- T-HY3.
  4. Dead DREV_RESET_*=0x10/0x20 constant + MTR/MPD RO-vs-RW comment cleanup (fold into #2).

TIER 1 -- semantics, M effort, HRM-authoritative:
  5. PRBEN read-to-clear / write-to-set "Special" per-CPU (T-SM4).
  6. IIC = Interval-Ignore-Count (ICNT decrement + OF<24>); decouple from IPI m_iic misuse (T-SM5).
  7. CSC faithful fields (drop fabricated CPU-present<3:0>, add P1P<14>/PBQMAX/IDDW/IDDR, bits>15 RW) (T-SM3).
  8. STR-write -> CSC<13:8> sync + DSC P1P<6> + per-byte STR replicate (T-SM1/2).
  9. AAR SA<8> split-array + honor written ROWS/BNKS (T-SM7).
  10. MISC ABW first-set lock + DEVSUP one-poll suppression (T-SM8).
  Foundation: T-TOPO topology SSOT (feeds the presence bits in 7/8/11).

TIER 2 -- architectural / CONSUMER-BLOCKED, L effort (sequence WITH consumers; do NOT land blind):
  11. Real Pchip1 second hose + presence bits (CSC/DSC P1P, PCTL RPP/PID) -- land together; gated on T-TOPO.
  12. Pchip DMA translation engine (direct-map + SG PTE + SG TLB + monster window + hole) -- blocked until a DMA-issuing device exists.
  13. PERROR error capture (freeze/lock/LOST/SYN/CMD/ADDR + PERRMASK IRQ0 + b_error) -- blocked until error sources exist.
  14. Type-1 config + IDSEL one-hot + dev>20 mask + BAR-driven dense decode (PCI-enum workstream).
  15. Full Dchip 8-way byte-slice WRITE datapath (lowest payoff; RO reads already correct).

BOARD-LEVEL (not a 21272 CSR, but ES40 boot-relevant):
  16. DPR/RMC dual-port RAM @0x801_1000_0000 (task #25).

## 7. Judgement -- what "audit-safe complete" means here

Tiers 0 + 1 are HRM-authoritative and can land now register-by-register; completing
them brings the Cchip/Dchip CSR surface to full faithfulness.  Tier 2 (Pchip1 hose,
DMA engine, PERROR, Type-1) are genuinely CONSUMER-BLOCKED: implementing them without
their consumers (a DMA-issuing device, real error sources, a second-hose device
population) risks speculative/wrong models -- the exact "mask" failure mode we have
avoided all session.  "Audit-safe complete" for Tier 2 therefore means EITHER build
them together with their consumers, OR record them as consumer-gated-by-design with
the HRM contract captured, so the deferral is a documented decision, not a silent
gap.  This ledger IS that record.
