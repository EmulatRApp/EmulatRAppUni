b a0<!--
EmulatR V5 -- Tsunami/Typhoon (21272) PCI-interface audit ADDENDUM (2026-07-24)
Delta over: journals/20260711_tsunami_typhoon_reaudit_current_state_ledger.md
(which remains the authoritative punch list) + PCI_Fabric_* design docs.
Question answered: "Is the PCI interface complete?"  HRM: EC-RE2CA-TE Rev 4.0.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
ASCII(128); hex radix.
-->

# Tsunami PCI Interface -- Audit Addendum (2026-07-24)

## 0. Verdict

NO -- the PCI interface is NOT complete against the 21272 HRM, but it is
complete ENOUGH for everything the boot path has actually demanded through
APB today, and the remaining gaps are precisely inventoried and (still)
consumer-blocked.  The 2026-07-11 re-audit ledger Sec 3 remains accurate
line-for-line in the live V5 tree (re-verified today):

  HAVE (verified LIVE this session):
   - S1 decode/swizzle: config cycles decode correctly (PCICFG-TRACE clean;
     2026-06-09 verdict re-confirmed on DS20 2026-07-24).
   - Type-0 config walk: d05 f0/f1 (Cypress bridge+IDE) and d07 (21143 ewa)
     enumerate; empty slots float all-ones; console `show config` correct;
     both hoses probe without incident (hose 1 = all-ones mirror, tolerated).
   - Device config spaces: V5 PciConfigSpace (256-byte backed, per-byte
     write mask) RETAINS BAR writes -- better than the June audit state.
   - Sparse/dense IO+mem windows; legacy IDE ports; polled IDE works
     (console read 1226 blocks; APB reads IDE status 0x1F7 fine).

  MISSING (unchanged from the 07-11 ledger, re-verified in code today):
   - DMA datapath: WSBA/WSM/TBA are raw storage, never consulted; no
     direct-map, no SG PTE/TLB, no monster window, no hole.  (Ledger #12)
   - BAR -> range rebind: decode ranges fixed at attach; BAR writes stored
     but do not re-point routing.  (Ledger #14 / Fabric R1 middle path)
   - BAR sizing masks: devices read back stored value, not the size mask
     (SRM legacy-fallback hides this for IDE only).
   - Type-1 config, IDSEL one-hot, dev>20 master-abort shaping.  (#14)
   - Real Pchip1 second hose + presence bits (CSC P1P / DSC P1P / PCTL
     RPP/PID).  (#11)
   - PERROR error capture / PERRMASK IRQ.  (#13)

## 1. New live evidence (2026-07-24 runs)

 - The %APB-F-NOIOVEC wall is NOT a PCI failure: in the APB window there are
   ZERO config cycles beyond five d07 reads (all answered "hit"), zero
   unhandled Pchip accesses, one healthy IDE status read.  The failure is in
   APB's environment/IOVEC setup (see the JRN-VMB-017 P3 follow-on work).
   Completing the PCI fabric would NOT have moved this error.
 - The 32 UNHANDLED OUTER WRITEs (offset 0x1001048, vals 0x2801/0x2803) are
   CONSOLE-era (log lines 13xxx, powerup), consistent with the known ewa/
   option-device pokes into unclaimed IO space -- non-fatal, unchanged.

## 2. What the boot path will demand NEXT (consumers arriving)

 1. APB bootdriver device init (post-NOIOVEC): PIO IDE via legacy ports --
    ALREADY SATISFIED.  Multi-block reads = ticket #32 (device-side, not
    Pchip).
 2. VMS SYSBOOT/exec DQDRIVER: BUSMASTER IDE DMA -> the Pchip DMA window
    engine (WSBA/WSM/TBA consulted, direct-map + SG) becomes REQUIRED.
    This is the first real consumer for ledger #12 -- when it arrives,
    build the DMA engine against ITS traffic (trace-first), not blind.
 3. OS PCI re-enumeration: BAR sizing masks + write-acceptance + one-shot
    range rebind (Fabric R1 "middle path") -- consumer = the OS bus walk.
 4. ewa/DE500: config presence landed (d07 answers); CSR/SROM model remains
    the known non-fatal gap.

## 3. Standing recommendation (unchanged in kind, updated in order)

 - Tier-0/1 CSR faithfulness batch (ledger Sec 6) remains landable now.
 - Tier-2 items stay consumer-gated; the FIRST consumer to arrive will be
   IDE busmaster DMA (VMS driver), so the DMA window engine should be
   designed against a captured DQDRIVER trace when the boot gets there.
 - The current blocker (APB IOVEC) is on the ENV/callback path, not PCI;
   do not divert PCI effort to it.
