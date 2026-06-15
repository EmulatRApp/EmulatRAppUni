# PCI Fabric -- Section-7 Proposal + PCICFG-TRACE Verdict (cross-read vs SRM source)
# EmulatR V4 -- 2026-06-09
# Author: Claude (Anthropic), with T. Peer (project architect)
# Status: DESIGN / PROPOSAL. Discuss before code. Builds on
#   PCI_Fabric_Strata_and_BuildOrder_20260609.md (strata + build order + seam constraints).
# Evidence: run_pcicfg_20260609.log (cold boot to >>>, EMULATR_PCI_CFG_TRACE, 496 lines)
#   cross-read against PalcodeBitsavers apisrm 5.8 source (REFERENCE_INDEX.md s3.2).

================================================================================
## 1. PCICFG-TRACE verdict (Q1/Q2/Q3) -- matches and collisions

WALK: SRM reads vendor (reg 0x00) at every device d00..d17; only d05 answers
(Cypress 0xC6931080). d05 is multi-function so SRM probes d05 f0..f7 (f1=IDE hit).

MATCHES (V4 correct):
 - Config decode/swizzle sound across d00..d17 + d05 f0..f7 (Q3 clean; raw= vs
   decoded d/f/reg consistent). No swizzle bug.
 - IDE (d05 f1) sizes + gets BARs assigned exactly per the standard algorithm ->
   dqa0 works because V4 does the real thing.
 - Manifest BDFs validated: slot5=Cypress(d05), slot6=SCSI(d06), slot7=option(d07)
   match where SRM looks.

Q1 -- BAR sizing/assignment (d05 f1 IDE, textbook):
 - Per BAR 0x10..0x24: W 0xFFFFFFFF -> R (mask) -> W 0x00000000 (the size probe).
   V4's IDE reads back 0xFFFFFFFF (NO size mask -- it does not implement sizing).
 - ROM BAR 0x30: W 0xFFFFFFFE -> R -> W 0.
 - Then ASSIGN: BAR0=0x1F0, BAR1=0x3F6 (legacy IDE ports -- SRM's hardcoded compat
   knowledge), BAR2..5 = 0x010018..0x010024 (allocated I/O from current_baddr),
   ROM=0x01000800; then CMD=0x07 (IO+MEM+BUSMASTER), interrupt line.
 - VERDICT: SRM SIZES then ASSIGNS -- NOT write-once-accept. Each BAR sized once +
   assigned once (NOT rebind-on-every-write). Step-3 scope = the MIDDLE PATH:
   return the size mask + accept the assigned base + rebind the range ONCE.
 - LINCHPIN: the IDE survived returning all-ones ONLY because SRM had a legacy
   fallback (wrote 0x1F0/0x3F6 regardless). A generic HBA has NO fallback -- SRM
   computes its base FROM the mask -- so a sizing-incapable HBA gets a GARBAGE base.

Q2 -- SCSI no-show: d06 f0 = 4 vendor reads, all-ones, then abandoned (no BAR
   sizing, no further probe). Unregistered = invisible. Step 4 MUST present an
   enumeration-valid config header at d06 before SRM will size/bind it.

Q3 -- decode/swizzle: clean (see MATCHES).

SURPRISE (blind platform pokes): d07 (132 R / 12 W) and d13 (131 R / 4 W) get
   heavy config traffic DESPITE all-ones vendor -- SRM inits them UNCONDITIONALLY
   from the PC264 platform code, not by enumeration. d07 reg 0x40 writes = the
   21143 CFDA (Configuration Driver Area) -> d07 = the DE500 tulip. d13 writes
   prog-if (reg 0x09=0xF0) + 0x43/0x50/0x53 then a full 0x00..0x7F read sweep ->
   a second on-board device; EXACT IDENTITY OPEN (pin from pc264_io.c / pc264.c).
   Both fall into MISS -> the 0xffff0000-class noise, now seen at the config layer.
   Non-fatal for boot (firmware tolerates the absent devices).

================================================================================
## 2. Source corrections (M5 -- vs our _PROVISIONAL manifest)

From PalcodeBitsavers apisrm/ref:
 - pci_vendor_ids.h: DEC_TULIP=0x00021011 (dev 0x0002), NCR_810=0x00011000 (dev
   0x0001), INTEL_SIO=0x04828086, DEC_BRIDGE=0x00011011 (21052/21152 PCI-PCI).
   Our manifest: de500_tulip dev 0x0019, symbios_scsi dev 0x000f -- DIFFER. The
   DE500-AA is a 21143 (0x0019); SRM's generic DEC_TULIP constant is the 21040
   (0x0002). Reconcile per the actual DS10 device + dc287_def.h.
 - pc264_io.c pci_irq_table (the authoritative PC264 slot map):
     PCI0 slot5 = Cypress PCI/ISA Bridge (no INTx)
     PCI0 slot6 = ADAPTEC SCSI  (irq 19,18)
     PCI0 slot7/8/9 = PCI option slots 0/1/2 ; slot10 = N/A
   => The on-board SCSI is ADAPTEC (aic78xx -- apisrm has adaptec_him_*/aic78xx_*),
      NOT Symbios/NCR. Our manifest's symbios_scsi (0x1000/0x000f) at slot6 is the
      WRONG CHIP. (NCR810/sym and QLogic ISP/KGPSA drivers also ship, but slot6
      on-board = Adaptec per the PC264 table.) The DE500 tulip is an option-slot
      card (d07), matching DS20E show config (DE500 at an option slot), not a
      fixed on-board at slot6.

================================================================================
## 3. STRATEGIC: what a "clean virtual HBA" can and cannot do (S-SCOPE refinement)

SRM enumeration is vendor-AGNOSTIC (read vendor/class, size BARs) -- so a clean
virtual HBA with a valid config header WILL enumerate and show in `show config`.
BUT SRM BOOTS/uses a controller via a VENDOR-SPECIFIC driver bound on vendor/device
ID (cam.h + scsi.c + per-HBA adaptec/aic78xx, ncr810/sym, isp1020, kgpsa). A
made-up clean HBA enumerates but has NO SRM driver -> no dk* devices behind it ->
cannot be booted from or list disks.

CONSEQUENCE: the 30-disk SCSI fabric goal (surface dk* disks, boot from them) is
NOT achievable with a clean virtual HBA -- it requires register-faithfulness to a
real adapter SRM has a driver for (on-board Adaptec aic78xx, or the QLogic
ISP/KZPBA option). That is the regime-3 register-faithful HBA we deferred. The
clean-HBA approach has a CEILING: "a PCI device that enumerates," not "usable SCSI
disks." This refines S-SCOPE -- do not assume the clean HBA yields bootable SCSI.

WHY THIS IS OK FOR NOW: the boot route is the IDE CD (dqa0) via dq_driver, which is
already satisfied (dqa0 enumerates + ATAPI works). SCSI disk usage is a SCALE goal,
and it lands in regime 3. So: clean HBA = enumeration milestone (show config lists
it); real SCSI disks = register-faithful Adaptec/QLogic, sequenced into regime 3
behind the submit seam. Decide explicitly before building Step 4 whether its target
is "enumerates in show config" (clean, cheap) or "bootable dk* disks" (Adaptec/ISP
register-faithful, large).

================================================================================
## 4. Proposal -- per build-order step (cite source; M5)

STEP 1 (Stratum-0 envelope + submit/completion seam): unchanged from the strata
spec. Value type (space,addr,size,data,isWrite); submit(command-block)->completion
inline today; bank C-VALUE/C-WORKQ/C-DELIVERY/C-LATENCY/C-SNAPSHOT. Lands first.

STEP 3 (Stratum-2 BAR sizing + range binding) -- sized by the trace:
 - Implement the MIDDLE PATH: on a BAR write of 0xFFFFFFFF return the precomputed
   size mask (synthesizePciConfig.barMask already computes it); on a real base
   write, store it and REBIND the range map (unregister old interval, register new);
   decode range owned by BAR-register state, not the attach call.
 - Authority: pci_size_config.c (start_baddr/current_baddr per bus/space allocator;
   PCI-PCI bridge config decppb/ibmppb). Transcribe its sizing loop when building.
 - Make the IDE's func-1 BARs answer the size probe too (currently all-ones); even
   though SRM legacy-falls-back, correctness + it's the same mechanism Step 4 needs.

STEP 4 (Stratum-3 SCSI HBA) -- two-tier per s3 above:
 - TIER A (enumeration-only, cheap, SRM-boot-faithful): register an enumeration-
   valid config header at d06 -- vendor/device/class (mass-storage/SCSI 0x0100),
   header type 0, BARs that SIZE correctly (return masks). Result: `show config`
   lists the controller. Built on the Step-1 submit seam, value-out, completion-
   status register, NO IRQ (polled). NO dk* disks yet.
 - TIER B (bootable dk* disks, regime-3, DEFERRED): register-faithful to a real
   adapter SRM drives. TARGET (decided 2026-06-09): prefer QLogic ISP1020 (the KZPBA
   option) -- clean mailbox/IOCB ring, tractable. AVOID the on-board Adaptec aic78xx
   for register emulation: SEQUENCER-based (downloaded sequencer firmware + SCB DMA;
   the SRM CHIM adaptec_him_hwm.c is >1 MB) -- the hardest target. Use the Adaptec
   IDENTITY for Tier-A enumeration fidelity, model the ISP for Tier-B bootable.
   Source: isp1020_def.h/_driver.c (preferred); aic78xx_def.h/adaptec_him_*.c
   (identity only); cam.h + scsi.c for the SRM CAM layer. NOT on the kernel-boot
   critical path (boot = dqa0/IDE).
 - DEPRECATED + DROPPED 2026-06-09: manifest symbios_scsi (wrong chip). Removed from
   ds10_platform.json + defaultDs10Manifest(). d06 = Adaptec on-board; add the
   Adaptec-identity entry as part of Step-4 Tier-A.
 - (target,lun) device map per controller (M3 physical axis); one map per HBA.

STEP 5 (Stratum-4 media backend): replace the m_hasMedia stub with image-file
 backing so READ(10)/CAPACITY/TOC return data. On the critical path for an actual
 boot (else 30 NOT-READY devices). Media path from manifest StorageTarget.media.

BLIND-POKE devices (d07 tulip, d13): give them PASSIVE config handlers (answer
 config reads, absorb writes) so the platform pokes stop falling into MISS. Add
 d07 (DE500, already slot7 in manifest -- but it is an OPTION card, reconcile) and
 d13 (identify first) to the manifest as passive/presence devices. LOW priority
 (non-fatal for boot); do after Tier-A enumeration.

================================================================================
## 5. 30-disk manifest schema (M4) + IDs

 - Each storage entry carries EXPLICIT (target,lun) for SCSI (the (channel,unit)
   landed 2026-06-09 is the IDE axis). validate() rejects: dup (target,lun) per
   controller, target==hba_id, out-of-range target/lun, missing media when non-empty.
 - Correct the PCI IDs from pci_vendor_ids.h before they back any decode; correct
   the slot-6 SCSI vendor to Adaptec (or whatever the DS10-specific dump confirms).

================================================================================
## 6. Open items (resolve in/with the implementation)

 - O1 Name d13 (the second blind-poke device) from pc264_io.c / pc264.c.
 - O2 Confirm DS10-specific on-board SCSI = Adaptec aic78xx (PC264 table says so);
      confirm DE500 tulip is option-slot vs on-board; get a real DS10 show config /
      lspci to pin vendor/device IDs (retire _PROVISIONAL).
 - O3 Architect decision: Step-4 target = Tier-A (enumerate only) now, Tier-B
      (bootable Adaptec/ISP) deferred to regime 3? (s3 ceiling.)
 - O4 Boot route stays dqa0 (IDE/ATAPI) for the kernel-boot milestone; SCSI is the
      scale goal.
