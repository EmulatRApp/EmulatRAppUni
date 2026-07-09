<!--
Title:  ES40 (pc264 / CLIPPER) SRM interface coverage audit -- authoritative
        REQUIREMENTS (apisrm firmware source) vs DELIVERY (EmulatR V4 source).
Date:   2026-07-07
Author: Timothy Peer (architect) / Claude (audit synthesis, Cowork; two parallel
        source sweeps of apisrm and EmulatRAppUniV4).
Status: LEDGER. Companion to 20260707_es40_printf_deadlock_root.md. Answers the
        standing question "are we certain we have full ES40 interface coverage?"
        -- ANSWER: NO. This is the reconciled gap ledger. Boot-critical gaps are
        flagged [BC]; diagnostic/optional gaps [DIAG].
Method: ASCII(128) only. apisrm cites from
        Processor Support/Palcode/palcode/apisrm/apisrm/ref (the pc264/Clipper ref
        tree; note it targets the Cypress CY82C693 super-I/O, not the ALi M1543C).
        V4 cites from EmulatRAppUniV4/Emulatr. All file:line verified this session.
-->

# ES40 SRM Interface Coverage Audit (2026-07-07)

## 0. Verdict

We do NOT have full ES40 interface coverage. The boot to `P00>>>` has ONE proven
single-point-of-stall (the IIC/SMB controller, section 3) and several latent gaps
(PCI enumeration, HWRPB/CRB hand-off, SCSI boot media) that would surface after it.
Three independent lines of evidence converge on the IIC gate:
  - authoritative call stack: build_power_hw -> fread iic_system0/1 -> !status abort
    (galaxy_pc264.c:1520/1574-1583);
  - apisrm mechanism: an unresponsive PCF8584 drives iic_rw_common into repeated
    2-second master timeouts, each calling err_printf through the FAO print library
    (iic_driver.c:587-616, 1592) -- which is exactly the observed dynamic locus;
  - V4 delivery: the ES40 IIC controller base is ABSENT from kIicBaseByModel
    (chipsetLib/TsunamiChipset.h:702-707), so the controller is left UNMAPPED and
    nodes 0x70/0x72 are unreachable even though they are declared and loaded.

## 1. Boot-path spine (apisrm, verified ordering)

    powerup()                         kernel.c:1959
      memconfig / mem_size            memconfig_pc264.c:421-424, powerup.c:289/320
      build_hwrpb(HWRPB=0x2000)       hwrpb.c:333/348
        build_ctb                     hwrpb.c:467-470
        build_memdsc (if mem_size)    hwrpb.c:475-476
        build_crb                     hwrpb.c:481-482
        build_config                  hwrpb.c:487 -> galaxy_config_tree.c:312
          gct_init$pc264_hw()         galaxy_pc264.c:158
            build_smb_hw              galaxy_pc264.c:181
            build_cpu_hw (loop)       galaxy_pc264.c:186-192
            build_iop_hw  (CSC>>14&3) galaxy_pc264.c:196
            build_memory_sub_hw       galaxy_pc264.c:203
            if((SYSVAR[0]>>10)!=1)    galaxy_pc264.c:213   var=5 -> TRUE
              build_power_hw(root)    galaxy_pc264.c:1520  [BC] the gate
            build_fru_root            galaxy_pc264.c:1657  (skipped if power_hw !SUCCESS)
      console tt bind / idle loop     combo_driver.c:643-676, kernel.c:1946-1951
      prints "P00>>>"

## 2. Interface matrix (REQUIREMENT vs DELIVERY)

Legend: [BC]=boot-critical, [DIAG]=diagnostic/optional. Status: OK / GAP / PARTIAL.

### 2.1 CPU / PAL (universal)               STATUS: PARTIAL [BC]
- REQ: EV6 PAL, OpenVMS personality, TB fill for firmware VAs.
- V4:  faithful CPU; but a HANDLED kFaultDtbMissDouble storm (PAL VPTE HW_LD
       pc=0x8321/0x8591, enc=0x6c845000) runs from ~cyc 1.24e9 (see deadlock_root
       journal sec 3). Not fatal, but the config walk translates through an
       unhealthy regime. Watch when the IIC gate clears.

### 2.2 Memory sizing / MEMDSC              STATUS: OK-ish [BC]
- REQ: mem_size set before build_hwrpb (powerup.c:289/320); build_memdsc gated on it
       (hwrpb.c:475). rtc_read(0x22) vs 0x35 chooses high-heap layout
       (memconfig_pc264.c:391/399). build_memory_sub_hw (galaxy_pc264.c:203) reads the
       AAR-tiled configured memory into the config tree.
- MEMORY CEILING (HRM-verified, tsunami_typhoon_21272_hrm.txt Table 10-15 AAR ASIZ
       <15:12>; bit <15> "Typhoon only"): base Tsunami ASIZ max 0x7 = 1GB/array x 4 =
       4GB (DS10/DS20/DS20E). Typhoon extends ASIZ: 0x8=2GB, 0x9=4GB, 0xA=8GB per array
       -> ES40 (Typhoon) = 32GB max. Titan (21274) ES45 = 32GB (ES45 EK-ES450-SV). So
       ES40 is a 32GB machine, NOT 4GB. (21272=Tsunami; Typhoon = 21272 high-bw variant,
       NOT 21274; 21274=Titan -- REFERENCE_INDEX sec 3.1.)
- V4:  memory sized from --mem; isExtendedAar gated on isTyphoon()||isTitan()
       (TsunamiVariant.h). RTC faithful (2.5). Prior "Typhoon memory-region / ASIZ
       overshoot" ACV framing is WITHDRAWN (deadlock_root sec 8).
- CORRECTION 2026-07-07: earlier runs/recipes used --mem 4294967296 (4GB) for ES40 --
       WRONG for a Typhoon box. Faithful ES40 = 32GB (--mem 34359738368 / 0x800000000).
       This matters: the deadlock is inside the config-tree build (build_memory_sub_hw/
       MEMDSC/AAR), the path memory size exercises; re-run the IIC-mapped boot at 32GB.

### 2.3 Tsunami/Typhoon Cchip CSR           STATUS: OK [BC]
- REQ: ReadTsunamiCSR(CSR_CSC); IOP count ((CSC>>14)&3)+1 (galaxy_pc264.c:196/245).
- V4:  chipsetLib/TsunamiCchip.h spec-driven MISC/DRIR/NXM/interval-timer; CSC
       decodes valid. FAITHFUL. ES40 classified Typhoon (TsunamiVariant.h:155-157).

### 2.4 Dchip                               STATUS: OK [DIAG]
- REQ: DREV etc.
- V4:  chipsetLib/TsunamiDchip.h variant-driven. FAITHFUL enough.

### 2.5 Pchip / PCI config + enumeration    STATUS: GAP [BC-latent]
- REQ: PCI config addr (bus<<16)|(slot<<11)|(func<<8) at PCI0_CONFIG_BASE+hose*0x200
       (galaxy_pc264.c:120-127); build_fru walks slots (skips ISA/SCSI slot5/6,
       251-255). Dual-hose on ES40.
- V4:  NO real bus walk; only bridge (0,5,0) + IDE (0,5,1) hardcoded
       (TsunamiChipset.h:622-676). Manifest PCI devices (e.g. de500_tulip) NOT
       enumerated into config space (Machine.cpp:560 attaches storage only).
       Unmodeled config read -> 0xFFFFFFFF (TsunamiPchip.h:1168-1170); Pchip1 is a
       coarse 0-mirror (TsunamiPchip.h:529-532). Latent: surfaces after the IIC gate,
       when build_fru_root enumerates. Candidate for a runaway-count print loop.

### 2.6 TIG bus window                      STATUS: OK [BC] (fixed 2026-06)
- REQ: TIG CSR reads (smir, halt, flash window).
- V4:  chipsetLib/TsunamiTig.h; unmodeled TIG-window reads return 0 not all-ones
       (TsunamiTig.h:185-197) -- the DS10/halt-refusal fix. FAITHFUL.

### 2.7 IIC/SMB PCF8584 controller + BASE   STATUS: GAP [BC] <== THE GATE
- REQ: MMIO PCF8584 at phys 0xFFF80000 (DATA) / 0xFFF80001 (STATUS);
       iic_write_csr=outmemb(0,0xFFF80000|addr), iic_read_csr=inmemb (pc264_io.c:
       1229/1246), each followed by an inportb(0,0x80) chip-select toggle
       (pc264_io.c:1235/1252). Reset writes PIN/S0P/own-addr/S2+CLOCK(0x14)/INIT(0xC0)
       (iic_driver.c:1160-1206). STATUS bit0 bb=1 (not busy), bit7 pin=0 (serviceable)
       (iic_def.h:159-169). iic_busb spins <=100000 for bb=1 (iic_driver.c:1581-1592);
       iic_rw_common START=0xC5, 2s timer, waits IIC_SR_DONE, STOP=0xC3 + err_printf on
       timeout (587-616). Polled driver for PC264 (POLLED, 138).
- V4:  IicPcf8584 model register-faithful (deviceLib/Tsunami/IicPcf8584.h:65-318),
       BUT kIicBaseByModel has NO ES40 row (TsunamiChipset.h:702-707): only DS10
       0xFFFF0000, DS20/DS20E 0xFFF80000. ES40 deliberately excluded from
       iicBaseRequired -> controller left UNMAPPED (TsunamiChipset.h:726-734, and the
       live log line). Binary confirms 0xFFF80000 is a real ES40 IIC-base constant
       (per-model base table at image VA 0x8d10/0x8d20/0x8d28 = FFFF0000/FFFC0000/
       FFF80000; plus 6 inline LDAH ...,0xfff8 sites). => reads fall through to Pchip
       all-ones (0xFF): STATUS pin bit7 stuck 1 -> transfers never complete -> 2s
       timeouts -> err_printf storm.

### 2.8 IIC device nodes (sniff/create)     STATUS: PARTIAL [BC]
- REQ: iic_node_list PC264 (iic_driver.c:189-273): iic_cont MSTR@0xB6, smb0@0xA2,
       cpu0@0xA4, cpu1@0xAC, iic_system0 LED@0x70, iic_system1 LED@0x72, KCRCM
       rcm_temp@0x9E, rcm_nvram@0xC0-0xCE. iic_init sniff: 1-byte READ; if(status!=1)
       continue -> no inode (iic_driver.c:1071-1076); allocinode at 1098. So 0x70/0x72
       must ACK + return 1 byte or the files do not exist.
- V4:  nodes declared es40_v7_3_platform.json:5-26 and loaded into m_iic via
       synthesizeFruImage + configureDevices (Machine.cpp:472-493). manifest iic_acks
       already lists 0x70 0x72 0xA2 0xA4 ... (live log). Correct data, but unreachable
       until 2.7 maps the controller. Once mapped, configured nodes ACK.

### 2.9 build_power_hw / build_fru consumer STATUS: blocked-by-2.7 [BC]
- REQ: build_power_hw fopen("iic_system0"/"iic_system1")+fread 1 byte each; if(!status)
       return (galaxy_pc264.c:1574-1583). build_fru_root + build_smb_fru read PWR/FAN
       FRU EEPROMs (galaxy_pc264.c:1657, build_fru.c) at 0xA2/0xA4.
- V4:  n/a until 2.7. Data bytes only affect which sensor subpackets build; the go/no-go
       is solely status==1.

### 2.10 COM1 16550 UART @0x3F8 (console)   STATUS: OK [BC]
- REQ: COM1_BASE 0x3F8/IRQ4 (smcc669_def.h:76); combott_init builds tta0/tta1, wires
       rx/tx, GALAXY&&CLIPPER perm_poll=1 (combo_driver.c:532/607); console_ttpb=COM1,
       inode "tt", tt_inited=1 (643-676). Needs LSR THRE for txready.
- V4:  Uart16550.h faithful; COM1 backed at 0x3F8 (TsunamiChipset.h:634); readLSR
       THRE|TEMT always set (Uart16550.h:863-872). FAITHFUL.

### 2.11 COM2 16550 UART @0x2F8            STATUS: OK [DIAG]
- REQ: COM2 0x2F8/IRQ3; Galaxy COM2 gate in combott_txready (get_console_base_pa()==0
       path) returns 0 (combo_driver.c GALAXY&&CLIPPER).
- V4:  COM2 unbacked (nullptr); readMSR now returns 0x00 for unwired port
       (Uart16550.h:910-930, the 2026-07-06 edit). Correct. (The terminal frames show
       COM2 0x2F8 in the leaf I/O because the console driver services both tta0/tta1;
       not the blocker -- see deadlock_root.)

### 2.12 RTC / TOY @0x70/0x71               STATUS: OK [BC-minor]
- REQ: rtc_read(0x22) vs 0x35 (heap layout); rtc_read(0x11)==69 gates saved baud
       (pc264_io.c:1413); year at shutdown.
- V4:  deviceLib/Tsunami/ToyRtc.h full MC146818, deterministic epoch, registered
       0x70-0x72 (TsunamiChipset.h:645). FAITHFUL. (ALi 0x72/0x73 high-bank not modeled.)

### 2.13 Flash / NVRAM env                  STATUS: OK [BC-supporting]
- REQ: ev_read lp_notree/os_type (galaxy_config_tree.c:224/239), arc_enable
       (kernel.c:1985), sys_serial_num on some families.
- V4:  chipsetLib/FlashRom.h Am29F016 on TIG window, autoselect/erase/program,
       persisted; holds NVRAM env. FAITHFUL for config/NVRAM (not firmware code).

### 2.14 HWRPB / CTB / CRB / config-tree     STATUS: GAP [BC-latent]
- REQ: build_hwrpb fills IDENT/REV/SYSTYPE/SYSVAR/NPROC + THB/SLOT/CTB/CRB/MEM offsets,
       twos-checksum (hwrpb.c:410-503); CTB VT loads CSR/SCB/baud (860-899); CRB
       console callbacks expected by the dispatch loop.
- V4:  the live HWRPB/CTB/CRB is built by the GUEST firmware (correct). V4's own
       HwrpbBuilder (deviceLib/HwrpbBuilder.h, FirmwareDeviceManager.h:519-599) is
       UNUSED on the boot path (zero callers; only SRMConsole SHOW DEVICE), and if used
       hardcodes DEC_TSUNAMI/variation 0 and zeroes CRB callbacks
       (FirmwareDeviceManager.h:553-575). So no V4-built CRB the console expects --
       fine while firmware self-builds, but a latent gap if we ever inject one.

### 2.15 ALi M1543C south bridge            STATUS: PARTIAL [BC-latent]
- REQ (hw fact): ES40 south bridge is ALi M1543C. BUT the apisrm ref tree pokes the
       CY82C693 (pc264_io.c) -- a real firmware-source vs hardware discrepancy; the
       es40_v7_3 binary is authoritative for which it actually programs (TO VERIFY).
- V4:  chipsetLib/AliM1543C.h identity/config store-through faithful; IRQ-steering
       PIRQ store-through-only (TODO, AliM1543C.h:140-145). Model-gated
       isAliPlatform(ES40/ES45/DS25) -> ALi (TsunamiChipset.h:618-632). BUT the ES40
       manifest still declares cypress_isa (es40_v7_3_platform.json:28) -> enumerated
       bridge (ALi) disagrees with manifest (Cypress). Reconcile after the IIC gate.

### 2.16 PlatformCapabilities classification STATUS: PARTIAL [infra]
- V4:  variantFromModel(ES40)->Typhoon (TsunamiVariant.h:155-157) correct. PlatCap
       bits derived (Machine.cpp:549-550) but INERT (zero call sites,
       PlatformCapabilities.h:42-46); SbAli cap currently mis-derives to SbCypress from
       the stand-in manifest string. The IIC base row (2.7) is its natural first consumer.

### 2.17 SCSI boot media                    STATUS: GAP [BC-latent, post->>>]
- REQ (hw fact): ES40 boots from on-board SCSI (Symbios/QLogic).
- V4:  ships only a DS10-style Cypress IDE/ATAPI stand-in
       (es40_v7_3_platform.json:32-43); no SCSI HBA. Not on the path to >>>, but blocks
       actual OS boot afterward.

### 2.18 Print / FAO console library         STATUS: OK (it is the symptom) [BC]
- REQ: err_printf/FAO in the IIC timeout/retry paths (iic_driver.c:1592/616/621);
       PowerUpProgress POST codes (kernel.c:1128).
- V4:  FAO core FUN_00062e48 (487 xrefs) + emit FUN_001b7390 + formatter FUN_000632d8;
       putc 0x628b8; divide 0x204b20. The library works; it is looping because 2.7
       feeds it endless IIC-timeout messages.

## 3. Prioritized gap register

[BC, PRIMARY]  2.7 IIC/SMB controller base ABSENT for ES40  -> the gate. Fix first.
[BC, next]     2.5 PCI enumeration (no bus walk; all-ones)  -> likely next frontier
               once IIC clears (build_fru_root slot walk). Watch for a runaway count.
[BC-latent]    2.14 HWRPB/CRB builder unused/Tsunami-defaulted (only matters if injected)
[BC-latent]    2.15 ALi vs Cypress manifest/enumeration disagreement
[post->>>]     2.17 SCSI boot media absent
[watch]        2.1 PAL DtbMissDouble storm; 2.16 PlatCap inert/mis-derived

## 4. Recommended fix sequence (discuss-before-code)

1. [PRIMARY] Add the ES40 row to kIicBaseByModel (TsunamiChipset.h:702-707) at base
   0xFFF80000 (authoritative: pc264_io.c:1229 + the binary's per-model base table) and
   register the PCF8584 controller MMIO for ES40 (mirror the DS20 path;
   iicBaseRequired). This is the fix the 2026-07-02 journal reverted prematurely when
   the blocker was mis-attributed to CSERVE 0x66 -- re-justified now by the call stack +
   the err_printf storm.
2. [VERIFY handshake] Confirm IicPcf8584 STATUS after START returns pin=0 (serviceable)
   /bb per the polled contract so iic_rw_common completes instead of hitting the 2s
   timeout (iic_def.h:159-169; iic_driver.c:587-616). Configured nodes 0x70/0x72 must
   ACK + return one byte (they are loaded, section 2.8).
3. [OBSERVE] Re-run ES40; expect build_power_hw's two freads to return 1, gct_init to
   proceed past galaxy_pc264.c:215 into build_fru_root, and the err_printf storm to stop.
   Next frontier likely 2.5 (PCI enumeration).
4. [TO VERIFY, parallel] Disassemble the binary's iic_read_csr/iic_write_csr to confirm
   0xFFF80000 (vs an ALi SBASMB base) is what es40_v7_3 actually programs -- resolves the
   contested CY82C693-vs-ALi question authoritatively before committing the base.

## 4b. CHANGE APPLIED 2026-07-07 -- ES40 IIC row (step 1), with isolation proof

Applied to chipsetLib/TsunamiChipset.h (discuss-before-code approved):
  1. kIicBaseByModel[] += { "ES40", 0xFFF80000ULL } (append-only, after DS20E).
  2. iicBaseRequired lambda += "ES40" (hard-stop hygiene if the row is ever removed).
  3. Refreshed the 690-699 comment block: ES40 now TRIAL-mapped; the "blocked by
     CSERVE 0x66" note marked SUPERSEDED; contested ALi-SMBus alternative retained as
     the fallback hypothesis.

Isolation proof (answers "does this harm DS10/DS20?" -- NO):
  - kIicBaseByModel is model-string-keyed, find-or-fail, break-on-first-match
    (TsunamiChipset.h:712-714). DS10/DS20/DS20E match their own explicit rows and never
    evaluate the ES40 row. Append-only => zero blast radius.
  - registerPciMemRange(base, base+2, &m_iic) fires ONLY when m_model=="ES40"
    (TsunamiChipset.h:715-716). DS10/DS20 register their own bases; no overlap.
  - m_iic (IicPcf8584) is model-AGNOSTIC (comment L683); its bus content is per-model
    from the manifest. ES40 nodes load from es40_v7_3_platform.json; DS10/DS20 nodes
    are independent. No shared mutable state is altered for the Cypress boxes.
  - This is the SOUTHBRIDGE/DEVICE axis (keyed by model / <model>_platform.json) per
    journals/20260705_platform_axis_classification.md -- the correct scope, NOT a
    CPU-axis global change. Platform-isolation interface: systemLib/PlatformConfig.h +
    PlatformCapabilities.h; contract journal Platform_Interface_Contract_and_Latch_
    Plan_20260624.md.

Handshake (step 2) -- NO CHANGE NEEDED: IicPcf8584::startTransaction (deviceLib/Tsunami/
IicPcf8584.h:314-325) already returns m_status=0x00 (PIN=0 serviceable, ACK) for a
configured node and kSt_LRB (NAK) for an absent one; STOP/idle => kSt_PIN|kSt_BB. So the
loaded 0x70/0x72 Status nodes ACK and iic_rw_common's completion wait is satisfied
(no 2s timeout). Same path validated by the DS20 badge work.

Self-falsifying property: if es40_v7_3 actually programs the ALi SBASMB base (not
0xFFF80000), the new mapping is simply never touched -> ES40 shows no change -> clean
revert; DS10/DS20 unaffected either way.

VERIFY (Tim, Windows): rebuild relwithdebinfo (touch TsunamiChipset.h first -- mount
mtime skew), re-run the ES40 boot AT 32GB (--mem 34359738368, the faithful Typhoon
size; see 2.2). Expect: build_power_hw's two freads return 1;
gct_init proceeds past galaxy_pc264.c:215 into build_fru_root; the err_printf storm
stops; next frontier likely 2.5 (PCI enumeration). If still stuck, the deadlock_root
FAO probe distinguishes the next gate.

## 5. Open questions

- Does es40_v7_3 program the PCF8584 @0xFFF80000 (Clipper/pc264) or the ALi M1543C
  SMBus (SBASMB)? Static disasm of the CSR helper decides it (section 4.4). Strong
  indication so far: 0xFFF80000 present as a base constant + inline LDAH.
- Once IIC clears, is the next stall the PCI slot walk (2.5) with an all-ones-driven
  count? The deadlock_root probe (gated FAO capture) is ready to answer.
- The apisrm-ref-vs-ALi south-bridge discrepancy (2.15): which super-I/O does the
  binary decode for RTC/COM/IIC chip-selects?
