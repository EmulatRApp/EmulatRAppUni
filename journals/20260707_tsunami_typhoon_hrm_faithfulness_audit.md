<!--
Title:  Tsunami/Typhoon (21272) chipset HRM faithfulness audit -- Cchip, Dchip,
        Pchip, TIG, address decode. Register-by-register vs the 21272 HRM.
Date:   2026-07-07
Author: Timothy Peer (architect) / Claude (audit synthesis, Cowork; two parallel
        source sweeps reconciled against tsunami_typhoon_21272_hrm.txt Rev 4.0).
Status: LEDGER + PRIORITIZED TASK LIST. Scope = Tsunami/Typhoon 21272 only (Titan
        21274 is a separate path). Builds on ASA-EMULATR-PRD-004-REV-A (Cchip
        offsets/DIR-combinational/MISC-CAS/IPI-via-MISC validated there; not
        re-litigated). Charter: code faithful FIRST; comment cleanup is a planned
        later sweep (fold comment corrections into a register's edit when efficient).
Method: ASCII(128) only. HRM cites by section/table; V4 cites by file:line.
-->

# Tsunami/Typhoon (21272) HRM Faithfulness Audit (2026-07-07)

## 0. Verdict

FAITHFUL today: Cchip MISC (IPREQ/IPINTR/ITINTR/NXM/NXS/CPUID/REV/ABT/ABW via CAS),
DIM0-3, DIR0-3 (combinational DRIR&DIM), DRIR, AAR ASIZ encoding (incl. Typhoon
0x8/0x9/0xA), TDR; the full MMIO base-address map (HRM Table 10-1) + routeMmioOffset;
PCI/config master-abort = all-ones; the entire TIG-bus register file (correct 0, not
all-ones, unmodeled-read default). These are DONE -- do not disturb.

NOT faithful (tasked below): a cluster of wrong RESET values (MTR, MPD, TTR, PRBEN,
STR, DREV), RO/RW inversions (DSC/DSC2 writable; CSC/MTR/MPD over-restricted), the
Dchip 8-way byte-slice data path (DSC/STR/DREV layout + P1P), PRBEN special semantics,
IIC interval-ignore-count, MPD I2C SPD interface, and -- the biggest ES40 items -- the
Pchip1 second hose (currently an all-ones mirror, not a Pchip) and the entire Pchip DMA
translation engine (direct-map + scatter-gather TLB + monster window) + PERROR capture.

## 1. Cchip register ledger (HRM 10.2.2 / Ch.6)

- CSC 0x000 (10.2.2.1): PARTIAL/WRONG. Upper fields seeded right, but CPU-present is
  fabricated into bits[3:0] (collides with BC/CxCFP), P1P<14> not set (SRM can't see
  Pchip1), PBQMAX/IDDW/IDDR missing, whole reg RO though HRM bits>15 are RW.
  V4 TsunamiCchip.h:354-360,773-775,1074-1076.
- MTR 0x040 (10.2.2.2; rev-detect 6.5): STUB/WRONG. reset 0; HRM PHCW<39:36>=14
  (Rev C), PHCR<35:32>=15 -> firmware Cchip-rev detect reads 0. Writes ignored (HRM RW).
  V4 :374,777-780,1140-1142.
- MISC 0x080 (10.2.2.3): FAITHFUL. Minor: direct-ABW first-set lock not enforced;
  DEVSUP suppression unwired (TODO :1461-1463).
- MPD 0x0C0 (10.2.2.4): STUB/WRONG. reset 0xFF (HRM 0x0F); no I2C SPD bit-bang.
  V4 :375,782-785,1137-1139.
- AAR0-3 0x100-1C0 (10.2.2.5): PARTIAL. ASIZ (incl Typhoon) faithful; SA<8>/TSA<9>
  split-array bits unmodeled; ROWS/BNKS hardcoded. V4 :1493-1545,1085-1109.
- DIM0-3 / DIR0-3 / DRIR: FAITHFUL. (Dead comment lists DIM2/3 at 0x500/540; live code
  uses 0x600/640 correctly.)
- PRBEN 0x340 (10.2.2.9): WRONG. Modeled as plain RW storage; HRM = per-CPU probe
  enable, read-to-CLEAR (return UNPRED), write-to-SET (ignore data); reset should be 0
  (V4 = 0xFFFF...F). V4 :388,817-820,1007-1010.
- IIC0-3 0x380/3C0/700/740 (10.2.2.10): STUB. Interval-ignore-count: no ICNT decrement,
  no OF<24>, no ITINTR suppression; OF writable (HRM RO). Mis-named "IPI" in V4.
  V4 :383,450-454,897-916,1038-1053; fireIntervalTimer :666-691.
- MPR0-3 0x400-4C0 (10.2.2.12): STUB. WO SDRAM mode-set; writes dropped. Low impact.
- TTR 0x580 (10.2.2.14): PARTIAL. RW ok; reset 0 (HRM 0x7330). V4 :372,922-924.
- TDR 0x5C0 (10.2.2.15): FAITHFUL (reset 0, RW).
- Interval timer (6.3.2): PARTIAL. Functional; IIC ignore-count gating absent. FLAG:
  EMULATR_PROFILE_ALPHA_CLOCK_HZ overridden to 2^28 (CsrSpec.h:554-562) => ~4x fast.
- ABSENT (HRM Table 10-8, no V4 model): PWR 0x780 (10.2.2.16), CMONCTLA/B +
  CMONCNT01/23 0xC00-CC0 (10.2.3, Typhoon), WDR (10.2.2.11), MCTL/0x500 (MBZ, benign).

## 2. Dchip register ledger (HRM 10.2.4 / Ch.7)

Cross-cutting WRONG: 8-way BYTE-SLICE absent. HRM DSC/STR/DREV/DSC2 are byte-sliced
across up to 8 Dchips and read as a quadword with the value replicated per byte
(Tables 10-31/33/34 footnotes). V4 stores each as a single uint64 (TsunamiDchip.h:291-294).

- DSC 0x800 (10.2.4.1): WRONG. HRM RO (from CPM pins), byte-sliced; V4 writable, reset
  0x01 mislabeled, P1P<6> reads 0 (SRM sees no Pchip1). V4 :118,167-170,227-230.
- DSC2 0x8C0 (10.2.4.2): WRONG (RO violated; V4 writable). reset 0 ok. V4 :120,235-237.
- STR 0x840 (10.2.4.3): PARTIAL/WRONG. reset 0 (HRM per-byte 0x28 = IDDW=2,IDDR=4);
  not byte-sliced; STR-write -> CSC<13:8> sync not wired. V4 :119,171-173,231-233.
- DREV 0x880 (10.2.4.4): WRONG. HRM RO byte-sliced 4-bit-per-Dchip rev (init 1 in each
  REVn low nibble -> 0x0101010101010101); V4 = 0x10/0x20 (variant) placed in a RAZ
  nibble, REV0<3:0>=0. V4 :123-124,175-177; CsrSpec.h:433-434.
- Data-slice datapath (Ch.7): ABSENT.

## 3. Pchip + routing ledger (HRM Ch.8 / 10.2.5 / 10.1)

- WSBA0-3 / WSM0-3 / TBA0-3 (10.2.5.1-.3): STUB. Raw storage; never consulted (no DMA
  path). WSBA3 SG<1> should read RO=1. V4 TsunamiPchip.h:784-819,927-974,1534-1543.
- PCTL 0x300 (10.2.5.4): PARTIAL/STUB. Raw store; PID<47:46>/RPP<45> (dual-hose
  presence) always 0 -> SRM sees no remote Pchip; HOLE/MWIN/PTEVRFY/ARBENA/etc inert.
  V4 :825-827,980-983.
- PERROR 0x3C0 (10.2.5.6): PARTIAL. W1C mechanics correct, but NO error source ever
  sets a bit; no freeze/lock/LOST/SYN/CMD/ADDR capture; no Cchip b_error. Reads 0.
  V4 :836-841,995-1006.
- PERRMASK/PERRSET/TLBIV/TLBIA/PMONCTL/PMONCNT/SPRST/PLAT: STUB (storage/sinks).
- DMA translation engine (10.1.4.1-.4): ABSENT. No direct-map, no scatter-gather PTE
  fetch, no SG TLB (168x4-QW), no monster window, no window-hole. There is NO DMA
  datapath through the Pchip at all (only CPU-initiated bus ops). V4 :70-73.
- PCI config: PARTIAL. Type-0 BDF decode real + miss=all-ones (FAITHFUL); Type-1
  (bus!=0) ABSENT; IDSEL one-hot (Table 10-3)/dev>20 mask absent; BARs do NOT drive
  dense-memory decode (only hand-registered m_pciMemRegistry). V4 :1161-1189,592-611.
- Dense PCI mem: PARTIAL (fixed claimants else all-ones). Sparse mem: repurposed to
  return 0 for the TIGbus window (documented deviation). Sparse-I/O byte/len decode:
  FAITHFUL-ish for legacy ISA; HAE extension ignored.
- Pchip1 (second hose): ABSENT/WRONG. 0x802/0x803 space returns all-ones on read, drops
  writes (TsunamiChipset.h:219-220,250-251); only ONE TsunamiPchip instance (:850). ES40
  populates BOTH hoses -> unfaithful for ES40.
- Monster window / peer-to-peer: ABSENT.
- INTx->DRIR routing (:421-434): plausible board convention, functional, unverified.
- DEAD CODE trap: unused "Arbiter Gatekeeper" decoder (routeMmioRead/handleCsrRead,
  :277-340,1343-1458) uses WRONG 8-byte reg spacing + derefs nullptr m_pciMemory; delete.

## 4. TIG ledger (TsunamiTig.h) -- FAITHFUL

smir(+0x40)=0, per-CPU halt (+0x3C0/+0x5C0), clr_irq4(+0x440), CPU-START latch
(+0xA00..), arb_ctrl, arb/PLD rev=0 RO, unmodeled-read default 0. All faithful to the
SRM/PALcode behavior (TIG is board-level, not an HRM CSR table). Only caveat: rev=0
placeholder (harmless); sibling clr_* not decoded (benign, add on demand).

## 5. Prioritized task list (the missing components)

P0 -- dual-hose / ES40 boot-relevant
  T-DH1  Pchip1 real second hose: instantiate a second Pchip (or per-instance state)
         and route 0x802/0x803 (PciMem/CSR/IACK/IO/Cfg) to it. (TsunamiChipset.h:219-220,
         250-251,850; HRM 10.1.2.1/T10-1.)
  T-DH2  PCTL PID<47:46>/RPP<45> + CSC P1P<14> + DSC P1P<6> from platform config so
         SRM detects the remote Pchip/hose. (HRM 10.2.5.4/10.2.2.1/10.2.4.1.)

P1 -- reset/RO fidelity (mechanical, HRM-unambiguous; BATCH 1 candidate)
  T-RV1  MTR reset PHCW=14/PHCR=15 (0x0000000EF0000000) [+ RW]. (10.2.2.2; 6.5.)
  T-RV2  MPD reset 0x0F. (10.2.2.4.)
  T-RV3  TTR reset 0x7330. (10.2.2.14.)
  T-RV4  PRBEN reset 0. (10.2.2.9.)
  T-RV5  STR reset 0x28. (10.2.4.3.)
  T-RV6  DREV reset byte-sliced rev-1 (0x0101010101010101). (10.2.4.4.)
  T-RO1  DSC / DSC2 make RO (ignore writes). (10.2.4.1/.2.)

P2 -- semantics + mechanisms
  T-SM1  Dchip 8-way byte-slice datapath (DSC/STR/DREV replicate-per-byte). (Ch.7.)
  T-SM2  STR-write -> Cchip CSC<13:8> sync. (10.2.4.3.)
  T-SM3  CSC faithful: drop fabricated CPU-present; model P1P/BC/CxCFP/SED RO low bytes;
         PBQMAX/IDDW/IDDR; bits>15 RW. (10.2.2.1.)
  T-SM4  PRBEN read-to-clear / write-to-set per-CPU. (10.2.2.9.)
  T-SM5  IIC interval-ignore-count + OF<24>; rename off "IPI". (10.2.2.10; 6.3.2.)
  T-SM6  MPD I2C SPD bit-bang interface + synthetic SPD. (10.2.2.4.)
  T-SM7  AAR SA<8>/TSA<9> split-array + honor ROWS/BNKS. (10.2.2.5.)
  T-SM8  MISC direct-ABW first-set lock + DEVSUP suppression. (10.2.2.3.)

P3 -- PCI fabric (needed when real PCI DMA/enumeration is exercised; overlaps the
      deferred PCI-enumeration workstream in CLAUDE.md)
  T-PC1  Pchip DMA translation: direct-map + scatter-gather PTE fetch + SG TLB +
         TLBIV/TLBIA + monster window + window-hole. (10.1.4.)
  T-PC2  PERROR error capture (freeze/lock/LOST/SYN/CMD/ADDR + PERRMASK IRQ0 + b_error).
         (10.2.5.6.)
  T-PC3  Type-1 config + IDSEL one-hot (T10-3) + dev>20 mask. (10.1.3.3.)
  T-PC4  BAR-driven dense-memory decode (replace hand-registered registry). (10.1.2.1.)
  T-PC5  HAE sparse extension; peer-to-peer; PCTL feature-bit enforcement. (10.1.3/Ch.8.)

P4 -- hygiene / completeness
  T-HY1  Delete dead "Arbiter Gatekeeper" decoder (wrong spacing + nullptr deref).
  T-HY2  Add RAZ/storage models for PWR, CMONCTLA/B+CNT, WDR. (10.2.2.16/10.2.3/.11.)
  T-HY3  Revert EMULATR_PROFILE_ALPHA_CLOCK_HZ 2^28 experiment to real profile clock.
  T-HY4  MPR WO write acceptance (needs SDRAM model). (10.2.2.12.)

## 6. Regression discipline (mandatory)

Every one of these touches SHARED Tsunami code (DS10/DS20/ES40 all construct
TsunamiChipset). No batch is complete until DS10 AND DS20 boot-to-console AND ES40
advance are re-verified on Windows (Claude cannot run the binary). Reset-value changes
(P1) are lowest risk but still change what firmware reads at reset -- regress-test each
batch. The comment-cleanup sweep runs AFTER the code is faithful (architect charter).

## 7. Changes APPLIED 2026-07-07 (this session)

- Batch 1 reset values (TsunamiCchip.h / TsunamiDchip.h): MTR 0x000000EF00000000
  (PHCW=14 Rev C / PHCR=15), MPD 0x0F, TTR 0x7330, PRBEN 0, STR 0x2828282828282828,
  DREV 0x0101010101010101. Comments folded to cite HRM sections.
- Dchip DSC + DSC2 -> read-only (writes logged + discarded, matching DREV); STR stays RW.
- Comment corrections (interim; full sweep deferred per charter): TsunamiChipset.h memory
  taxonomy block (ES40=Typhoon 32GB / DS10-DS20=Tsunami 4GB / ES45=Titan 32GB, HRM
  Table 10-15); TsunamiCchip.h MPD TODO parenthetical corrected after the reset change.
- ALL of the above touch SHARED Tsunami code; DS10 + DS20 boot-to-console + ES40 advance
  MUST be re-verified on Windows (Claude cannot build/run the MSVC/Qt binary).

## 8. Remaining items -- status and why staged (NOT landed blind)

GROUP A -- bounded, land in reviewed batches WITH a DS10/DS20/ES40 regression each
(header-local, but behavior changes on shared code; Claude cannot regression-test):
- MISC ABW direct first-set lock + DEVSUP suppression (T-SM8).
- PRBEN per-CPU read-to-clear / write-to-set (T-SM4).
- IIC interval-ignore-count + OF<24> (T-SM5).
- MPD I2C SPD bit-bang (DS/CKS out, DR/CKR in) + synthetic SPD EEPROM (T-SM6).
- AAR SA<8>/TSA<9> split-array + honor written ROWS/BNKS (T-SM7).
- CSC faithful fields: drop the fabricated CPU-present-in-bits[3:0]; model P1P<14>/BC/
  CxCFP/SED (RO low bytes) + PBQMAX/IDDW/IDDR; bits>15 RW; STR-write -> CSC<13:8> sync
  (T-SM2/T-SM3).
- Dchip 8-way byte-slice write replication for DSC/STR/DREV (T-SM1).

GROUP B -- architectural AND consumer-blocked; faithful design captured here, deliberately
not implemented blind:
- Pchip1 real second hose (T-DH1) + presence bits (T-DH2). NOTE: setting P1P/RPP/PID
  WITHOUT a real hose would advertise "hose 1 present" while 0x802/0x803 still return
  all-ones -> potentially WORSE than today. The hose datapath and the presence bits must
  land TOGETHER. Necessity for reaching >>> is unproven (the hose-1 walk is not yet
  reached), so sequence with the device-enumeration work / when the boot demonstrably
  needs it.
- Pchip DMA translation engine (T-PC1): direct-map (WSBAn ENA/SG, WSMn mask, TBAn base;
  HRM Table 10-5), scatter-gather PTE fetch (Fig 10-7/8) + 168x4-QW SG TLB (Fig 8-2) +
  TLBIV/TLBIA, monster window (10.1.4.4), window-hole (PCTL HOLE). BLOCKED: V4 has NO
  DMA-issuing consumer today (only CPU-initiated bus ops). A translation engine with no
  caller is untestable dead code -- it belongs with the first DMA-capable device model
  (NIC/SCSI).
- PERROR error capture (T-PC2): W1C mechanics already correct; needs the error SOURCES
  (master-abort/target-abort/parity/SGE) to exist first -> wire alongside the PCI fabric.
- Type-1 config + IDSEL one-hot (T10-3) + BAR-driven dense-mem decode (T-PC3/T-PC4):
  part of the deferred PCI-enumeration workstream (CLAUDE.md).

GROUP C -- hygiene, safe but staged for care:
- Delete the dead "Arbiter Gatekeeper" decoder (~230 lines; confirmed unreferenced;
  wrong 8-byte reg spacing + nullptr m_pciMemory deref) (T-HY1).
- RAZ/storage models for PWR / CMONCTLA-B / CMONCNT01-23 / WDR (T-HY2).
- EMULATR_PROFILE_ALPHA_CLOCK_HZ 2^28 experiment -> profile clock (T-HY3): GLOBAL timing
  change, model-dependent value (ES40 600 MHz vs ES45 1 GHz); revert under a build +
  boot-timing check, NOT blind.

## 9. New TODO items surfaced 2026-07-07 (add to the ledger)
- b_irq<0/1> edge-assert delivery hook (Cchip :846/1015): the poll model (pendingIrq0/1)
  is functionally correct today, so LOW priority.
- Pchip :1270 registered no-op ISA handler (folds into T-HY1).
- Stale header TODO(unwired) entries to reconcile in the comment sweep -- e.g. the
  ABT/ABW auto-promote entry (Cchip :130) is actually WIRED at :1320-1344.

## 10. Charter note
Per architect direction: faithful CODE first, comment cleanup as a dedicated later sweep
(remove existing comments + insert correct comments once the code is faithful). Fold a
register's comment fix into the same edit that lands its wiring when efficient; remove the
register's TODO(unwired) in that same edit (project TODO discipline).

## 11. Gating architecture -- DECIDED 2026-07-07 (architect)

Decision: Tier-2 topology SOURCE = **Manifest SSOT (declared)**; chip-object construction
INPUT = **a single latched ChipsetTopology struct**.

Model (from journals/20260705_platform_axis_classification.md):
- Tier 0 -- Universal 21272 truths (reset values, RO/RW, W1C, byte-slice, PRBEN/IIC/MPD
  mechanisms): NO gate. Identical Tsunami and Typhoon; shared by every DS and ES box.
- Tier 1 -- Chipset-variant (Tsunami vs Typhoon): gate on m_variant via the
  ChipsetVariantInfo data table (extended ASIZ, MISC REV 1/8, Typhoon-only DIM2/3-DIR2/3-
  IIC2/3-CMON, 4-CPU field widths). Add missing per-variant scalars to that table as DATA.
- Tier 2 -- Board/topology population (populated CPU/hose/Dchip counts, Pchip1-present):
  NOT expressible from variant alone. DECIDED source = the <model>_platform.json manifest.

Implementation shape (foundation task T-TOPO, precedes the Tier-2 chipset fixes):
1. Extend the manifest schema (PlatformConfig.h / <model>_platform.json) with declared
   topology: num_cpus, num_hoses (populated), num_dchips (Pchip1-present derivable from
   num_hoses>1). Parse into DeviceManifest.
2. Machine derives + latches ONE `ChipsetTopology` value (variant + cpuCount + hoseCount +
   dchipCount + flags) at construction, BEFORE any guest instruction retires.
3. Pass that single struct to the Cchip/Dchip/Pchip constructors (replacing the ad-hoc
   variant/cpuCount/memSize args over time). Register read/write handlers stay SINGLE-PATH
   and data-driven; every per-model reset value + presence bit (CSC/DSC P1P, PCTL RPP/PID,
   CPU-present mask, DREV/DSC BC population) is computed from the latched struct.
4. Reconciliation: this supersedes the ini [System] cpuCount as the topology SSOT (part of
   the ini->json migration); until migrated, PlatformConfig may seed num_cpus from the ini
   when absent in json, so nothing regresses.
5. Southbridge axis stays on PlatCap (SbCypress/SbAli) -- e.g. the SuperIO gate T-SIO2. The
   topology struct is the CHIPSET/board axis; the capability set is the southbridge/device
   axis. Keep them distinct.

Consequence for sequencing: T-TOPO lands first (foundation, inert until consumed), then the
Tier-2 chipset fixes (dual-hose presence bits + real Pchip1) consume it. Tier-0/Tier-1 fixes
do not depend on it and can proceed in parallel batches with regression gates.
