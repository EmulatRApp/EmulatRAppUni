<!--
EmulatR V5 -- Session Journal JRN-VMB-018
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-018 -- %APB-F-NOIOVEC root-cause investigation (DS20 OpenVMS boot)

    Doc id   : JRN-VMB-018
    Date     : 2026-07-24
    Status   : DEEP REVERSE-ENGINEERING COMPLETE to the failing module; final
               confirmation run PENDING (window over 0x20095840).  Working
               hypothesis: APB's config-tree search cannot resolve the boot
               path 0.0.105.0 -> GCT/FRU content gap (console-side data), NOT
               a PCI or env-var failure.  NO code changed this session (all
               probes runtime env knobs).
    Relates  : JRN-VMB-017 P3 (frontier + runbook), 20260724 PCI audit
               addendum (PCI exonerated), GCT_FRU_Support_Spec_20260607,
               20260622_gct_fru_cyclic_link_diagnostic_briefing.
    Method   : Phase-0 probes-first (P3.4): live DIAG-PC windows + WREG value
               capture + CSERVE/callback ledger + static disasm of the APB
               image (extracted from vStorage/Alpha/dka0.vdisk, lbn 283169,
               1226 blocks) via the VA 0x10000000 == console-PA mapping from
               JRN-VMB-017.

--------------------------------------------------------------------------------
## 1. Findings chain (each verified live, in order)

 1. RULED OUT: PCI.  APB window has ZERO config cycles beyond 5 answered d07
    reads, zero unhandled Pchip accesses, one healthy IDE status read
    (0x1F7 -> 0x50).  See the 2026-07-24 PCI audit addendum.
 2. RULED OUT: missing boot_dev env var.  After `set bootdef_dev dqa0` the
    console `show` proves boot_dev = dqa0.0.0.105.0 BEFORE `b dqa0`; same
    NOIOVEC.  (Earlier suspicion of empty boot paths was pre-set state.)
 3. GETENV path DECODED and WORKING: APB callback DISPATCH router at VA
    0x101aac40 (CRB at HWRPB+0x7E0, [HWRPB+0xC0]); function table base
    0x101aacb8, bounds <=54; GETENV(34) handler 0x101ab430 walks the
    OS-mapped ENV_VAR_TABLE (24 x {desc,id} at 0x101ab130) LOCALLY (no
    CSERVE -- why the ledger never showed it) and returned severity 0,
    len 14 = the topology string.  Callback function code travels in R1
    (stub sets R1=orig R16; ledger histogram: GETC x40, PUTS x40 -- R25
    was a red herring).
 4. Boot-driver SELECTION works: index cell [0x2005ee58] = 1; driver
    table at 0x200712e0 (7 driver descriptors, generic service thunk
    0x2000e770, per-driver blocks 0x63820/0x63900/...); driver 1's
    identify method 0x2000e9d0 matched a 2-char type mnemonic and stored
    type + a -1 sentinel into the parsed descriptor (0x2006a430).
 5. THE FAILING CALL: shared service 0x2000def0 -> far module ENTRY
    0x20095840 (the driver-database / IOVEC-construction module).  Its
    return status, captured live by WREG at the 0x2000e844 zapnot:
    **R0 = 0x00158284** (facility 0x15, msg 1104, severity SEVERE).
    Sibling constant 0x0015828C exists (msg 1105).
 6. SEMANTICS of 0x158284 (static): module 0x95840 is RECURSIVE (calls
    itself at 0x200964fc) and at 0x20096514 treats 0x158284 as the
    "no match at this node -- continue searching" sentinel, comparing
    packed record name/path fields (ldq_u at +12/+15/+20/+23 of r3
    records).  NOIOVEC = the search exhausted with no match.  ~14 sites
    in the module return these constants (pool slots 0x65320/0x65598).

## 2. WORKING HYPOTHESIS (to confirm with the pending run)

APB resolves the boot topology (hose/bus/slot 0.0.105.0) against the
CONSOLE'S CONFIGURATION TREE -- the GCT/FRU structure the banner reports
("initializing GCT/FRU at 3ff32000").  The tree search finds no device
node matching the path -> 0x158284 exhaustion -> %APB-F-NOIOVEC.  If so,
the FIX is console-side GCT/FRU content (EmulatR's FRU/EEPROM/config
inputs to the guest's tree builder) -- NOT PCI, NOT env vars, NOT APB.
Note the known GCT issue history: 20260622_gct_fru_cyclic_link briefing.

## 3. NEXT STEPS (in order)

 1. CONFIRMATION RUN (pending; user rebooting):
      EMULATR_DIAG_WREG= EMULATR_DIAG_PCLO=0x20095840 \
      EMULATR_DIAG_PCHI=0x20099000 EMULATR_DIAG_CAP=300000 \
      tools/run_ds20_bplus.sh ; exit LFU (no u srm) ; b dqa0
    READ: the trace's memAddr column names the records the search walks.
    Records in/derived from the GCT region -> hypothesis CONFIRMED; the
    specific compare fields show WHAT a matching node must contain.
    (Beware stale env: the 12:15 run inherited the previous window --
    set all three DIAG vars explicitly in one command line.)
 2. If confirmed: dump/audit EmulatR's GCT as built (console `show fru`,
    the GCT builder inputs, 20260622 briefing) vs the node APB needs
    (bus0/slot5 IDE with the 105 path encoding).  Fix = supply the
    missing FRU/config data so the guest console builds the node; then
    re-run b dqa0.
 3. APB.EXE (user offering): decode EIHD/symbol vector to put real names
    on 0x20095840 / 0x2000def0 / 0x2000e770 and the 0x158284 condition;
    strengthens the record and any remaining hunt.
 4. On progression past NOIOVEC: expect driver init touching IDE (PIO --
    dq_driver.c is pure inportb/w, ZERO DMA: authority check done), then
    multi-block reads (#32), then SYSBOOT.  The Pchip DMA engine stays
    consumer-gated for the VMS runtime driver (see PCI audit addendum).
 5. Housekeeping owed (unchanged): CSERVE entry-ledger throttle;
    DS10/ES40 regression to >>>; memory.md update when this closes.

## 4. Key addresses (this investigation)

  APB image = boot media lbn 283169 x 1226 blocks; loaded VA 0x20000000.
  main flow proc 0x20003450; report block 0x20003a28; blbs test 0x20003a10
  service thunk 0x2000e700 (cell 0x2005ee58, table 0x200712e0)
  driver service 0x2000e770; shared method 0x2000def0
  driver1 identify 0x2000e9d0; parsed descriptor 0x2006a308 (+0x128 = -1)
  DISPATCH router 0x101aac40 (table 0x101aacb8, bounds 54)
  GETENV handler 0x101ab430; ENV_VAR_TABLE 0x101ab130 (24 entries)
  decision module 0x20095840 (recursion 0x200964fc; sentinel cmp 0x20096514)
  status constants 0x00158284 / 0x0015828C (pool 0x65320 / 0x65598)
  GCT/FRU (console banner) PA 0x3ff32000
