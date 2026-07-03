<!--
EmulatR V4 -- ES40 Significant Address Regions: trace-log checkpoint reference
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree.
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-07-02.  ASCII(128) only.
Purpose: a single lookup of every significant PA / VA / milestone PC gathered from
the DS10 + DS20 execution runs, for cross-referencing the ES40 (CLIPPER) trace logs.
-->

# ES40 Trace Checkpoint Reference -- Significant Address Regions

**Scope + caveat.** Every anchor below is CONFIRMED against DS10 / DS20 runs (live
traces, the HWRPB scan dump, or read-only apisrm source). ES40 shares the Tsunami
21272 chipset and the entire downstream firmware path (SRM console body,
decompressor, HWRPB hand-off, IIC/OCP/FRU architecture, EV6/EV67 PALcode), so the
DS20 map is the working template for ES40. Per Tim's framing this similarity is a
**non-authoritative guess**: expect ES40 to diverge at (a) the SysType/badge member
value, (b) the south-bridge (real ES40 = ALi M1543C; manifest ships a Cypress
STAND-IN), (c) dual-Pchip / larger PCI fan-out, (d) 4-CPU topology. Confidence tags:
[LIVE] measured on hardware/trace; [SRC] from apisrm/HRM source; [ES40?] expected but
unverified for ES40.

Runtime PC note: the decompressed firmware image maps **runtime VA = file_offset +
0x8000**, so any runtime PC disassembles in the decompressed image at PC - 0x8000.
Firmware function PCs below are DS20 V7.3-2 runtime PCs; ES40 (V7.3, same console
family) should be near-identical but CONFIRM against the ES40 image if a PC misses.

---

## 1. HWRPB region (DRAM) -- the firmware->OS hand-off contract  [LIVE, DS20]

SRM builds ONE HWRPB at **PA 0x2000** (self-describing; validate by self-pointer +
"HWRPB" id, not by size). All section pointers are RELATIVE to base 0x2000. The whole
block is 0x2000..0x2b80 (size 0xb80); GCT/FRU is out-of-line at 0x3ff32000.

| Field / section | PA (abs) | Off from base | Notes |
|-----------------|----------|---------------|-------|
| HWRPB base / self-ptr | 0x2000 | +0 | read8(base)==base validates it; store here = build_hwrpb entry |
| id "HWRPB" | 0x2008 | +8 | 0x0000004250525748 |
| revision | -- | +0x28 | = 14 |
| size | -- | -- | = 0xb80 |
| serial number | 0x2040 | +0x40 (64) | sys_serial_num echo; used by HWRPB-scan pattern match |
| **SYSTYPE** | **0x2050** | **+0x50 (80)** | = 0x22 = DEC_TSUNAMI (family-wide DS10/DS20/ES40) |
| **SYSVAR** | **0x2058** | **+0x58 (88)** | **THE BADGE WORD.** member = (SYSVAR>>10)&0x3F |
| TBB (translation buffer) | 0x2140 | +0x140 | |
| per-CPU slot 0 | 0x2180 | +0x180 | count 2, **stride 0x280** (NOT 0x400) |
| per-CPU slot 1 | 0x2400 | +0x400 | slot0 + 0x280 |
| CTB (console terminal block) | 0x2680 | +0x680 | 1 x 0x160 |
| CRB (console routine block) | 0x27e0 | +0x7e0 | |
| MEMDSC / MDDT | 0x2840 | +0x840 | memory descriptor |
| DSRDB | 0x2ac0 | +0xac0 | dynamic system recognition; badge string indexes here |
| CDB | 0x38880 | -- | |
| FRU / GCT tree | 0x3ff32000 | -- | fru_offset 0x3ff30000 rel -> abs 0x3ff32000 |

Per-CPU slot internals (AARM Table 26-4, slot size 0x280): cycle-counter frequency at
**slot+0x270** (e.g. 0x2180+0x270). SYSVAR members map (SYSTYPE 0x22): see Section 8.

**Watch in ES40 logs:** `GMEM-WATCH(0x2058)` store value = the ES40 badge member (see
Section 8); confirm HWRPB still lands at 0x2000 and SYSTYPE=0x22 (should, family-wide).

---

## 2. SYSVAR / badge decision -- function PCs + string/table anchors  [LIVE + SRC, DS20]

| Anchor | Address | Kind | Notes |
|--------|---------|------|-------|
| `get_sysvar` entry | pc 0x7f5c0 | runtime PC | seeds SYSVAR=0x5, fopens the discriminator IIC device |
| get_sysvar BSR site | pc 0x5c3e4 | runtime PC | caller |
| fopen-result branch | `BEQ pc=0x7f608` | runtime PC | R16==0 (fopen failed) -> member-1 fallback |
| member-1 literal load | `LDA 0x405 @ pc 0x7f660` | runtime PC | the 264DP fallback (member 1) |
| SYSVAR store site | store to 0x2058 @ pc 0x5c3f8 | runtime PC | writes the final badge word |
| build_hwrpb self-ptr store | hwrpb.c:371 | src | `hwrpb->BASE = virt_to_phys(hwrpb)` -> store 0x2000 to PA 0x2000 |
| build_dsrdb | pc264.c:566 | src | switch(SYSVAR>>10): 1->264DP, 6->DS20, 8->DS20E, default->error print |
| SysType/banner TABLE | VA **0x153cd8** | rodata | stride 0x2c, base 0x153cac (member 0 = invalid) |
| member 1 string | VA 0x19a6c8 | rodata | "AlphaPC 264DP %3d MHz" |
| member 2/3 string | VA 0x19a6e0 / 0x19a700 | rodata | "AlphaServer DS20 %3d MHz" |
| "Defaulting system type to AlphaPC 264DP" | VA 0x19ad90 | rodata | printed only on the default arm |
| "Error determining system type, SYSVAR = %x" | VA 0x19adc0 | rodata | printed only on the default arm |
| iic_ocp0 name string | VA 0x17a3c0 / 0x1a0218 | rodata | the OCP-LED node name (0x40) |

**ES40 timing gotcha [LIVE, DS20]:** get_sysvar / build_hwrpb run ~cyc 222M and are
DOWNSTREAM of a pre-banner console wait ("ISA table corrupt! / Initializing table to
defaults"). An UNATTENDED cold boot idles before this and NEVER computes the badge
(a 250M-cycle cap stopped at PC=0x8321 with the SYSVAR markers still empty). To reach
the badge/banner you MUST drive the LFU / console at PuTTY. Expect the same for ES40.

---

## 3. Boot / decompression milestone PCs  [LIVE + SRC, DS20]

| Milestone | Address / PC | Notes |
|-----------|--------------|-------|
| decompressor inner spin | pc ~0x60111c | ~4M-cycle self-inflate (early cold-boot cost) |
| decompressor link-base self-check | bases 0, 0x20000000, 0x20000240 | DS20; compares load base |
| load-base halt (DS20) | pc 0x60222c | diagnosed as load-base mismatch (image @0x600000) |
| PAL_BASE | 0x600000 | AXPBox ES40 value EmulatR copied (family) |
| krn$_idle loop | pc 0x7bad0 | IDLEWARP hooks here; prompt-wait MAY route through it |
| MaxCycles stop (DS20 unattended) | PC=0x8321 | where the 250M cap landed pre-badge |
| IIC first node-0x40 START (iic_init) | ~cyc 185M (ord 184.64M) | device-probe/registration window |
| build_hwrpb / get_sysvar / SYSVAR store | ~cyc 222M | badge decision (attended boot) |
| banner streams (MHz_eff sampled) | ~cyc 200M | approximate; near-raw throughput point |

Cycle numbers are DS20, approximate, and attended-boot-dependent -- use as relative
landmarks for ES40, not absolutes.

---

## 4. Chipset MMIO -- Tsunami / Typhoon 21272  [LIVE + SRC]

Shared top-level PA map (family-wide; Titan 21274 shares it too). hose = h << 33.

| Block | Base PA | Key offsets |
|-------|---------|-------------|
| Cchip | **0x801_A000_0000** | MISC +0x80 (= 0x801A0000080); CSC strap low byte CSC<7:0>; DIR0-3 +0x280/+0x2C0/+0x680/+0x6C0 |
| Dchip | (Cchip block) | -- |
| Pchip0 | **0x801_8000_0000** | CSR window 0x80180000000-0x80190000000 |
| Pchip1 | **0x803_8000_0000** | ES40 dual-Pchip (hose 1 populated); EmulatR models it as coarse all-ones mirror [ES40?] |
| TIG bus | **0x80130000000** | see Section 5 |

**Badge root-cause hypothesis [SRC, carried from DS20 campaign]:** the Cchip CSC<7:0>
TIGbus strap low byte is not seeded with the platform's strap value, so the DSR/SMM
lookup feeding HWRPB[+0x50]/[+0x58] falls to the 264DP default. If real, this is a
Cchip construction-init issue -- watch Cchip MISC / CSC reads (0x801A0000080 area) in
the ES40 trace. NOTE: superseded for DS20 by the manifest-discriminator fix, but the
CSC-strap path remains the family-wide theory for how the strap value should arrive.

---

## 5. TIG bus + halt gate  [LIVE, DS20/DS10]

TIG base 0x80130000000; register = base + (offset << 6)... actual probed addresses:

| Register | PA | Behavior |
|----------|-----|----------|
| **smir (halt gate)** | **0x80130000040** (TIG+0x40) | status-only; MUST read 0. All-ones default = firmware sees "Halt IN" -> "BOOT NOT POSSIBLE" |
| halt (per-CPU) | 0x3C0 / 0x5C0 (in TIG window) | R/W |
| ipcr | (TIG window) | R/W, storage-only (no IPI -> SMP secondary stall) |
| arb_ctrl | (TIG window) | R/W |
| trace-arm reg (console) | `e pmem:80130000FF8` | arms the retire window from >>> |

**ES40 [ES40?]:** ES40 has an ALi south bridge; its halt source MAY read the same TIG
smir (existing model works) or an ALi-specific reg (a divergence -- itself a useful
finding). Watch `HALTPROBE: TIG read pa=0x80130000040` in the ES40 log. If it stalls
at "Halt Button is IN, BOOT NOT POSSIBLE", smir is the gate.

---

## 6. IIC bus nodes  [LIVE + manifest]

| Node | Name | Present on | Role |
|------|------|-----------|------|
| 0x40 | iic_ocp0 | DP264 + DS20 + ES40 | OCP LED (IIC_LED_TYPE) -- NOT a discriminator |
| 0x42 | iic_ocp1 | -- | OCP LED |
| 0x4E | iic_8574_ocp | DS20E/Goldrack | member-8 discriminator |
| 0x70 / 0x72 | iic_system0/1 | family | system status (fans/temp/PSU); ES40 manifest has these |
| 0x9e | iic_rcm_temp | **DS20 only** | KCRCM temp sensor = the DS20 member-6 discriminator (the fix) |
| 0xA2 | iic_smb0 | family | FRU EEPROM (ES40 manifest: model "ES40") |
| 0xA4 | iic_cpu0 | family | CPU FRU EEPROM (EV6) |
| 0xC0 | iic_rcm_nvram0 | family | RCM NVRAM (256 B) |

ES40 manifest (`es40_v7_3_platform.json`) currently registers: 0x70, 0x72, 0xA2, 0xA4,
0xC0. **[ES40? OPEN]** ES40's badge discriminator node is UNKNOWN -- if ES40's
get_sysvar probes an ES40-specific node not in the manifest, it will fall back to
264DP just like DS20 did. Watch `IIC-TXN addr=0x..` NAKs around get_sysvar. IIC SCB
vectors (interrupt path, unused on pc264 -- driver is POLLED): 0xa9 / 0xaa.

---

## 7. DRAM working locations + ISP flag  [LIVE, DS20]

| Location | PA | Meaning |
|----------|-----|---------|
| ISP-model detect flag | **0xBFFC** (offset 0x3FFC) | read for 0xCAFEBEEF; EMULATR_PLATFORM lever (unset=ISP intercept; =silicon disables) |
| guest tick counter | 0x3c970 | RSCCWARP rewrites this out-of-band (quarantined) |
| IIC poll queue head | 0x3c4f0 - 0x3c500 | self-referential/empty queue the polled driver spins on |
| env-var store | 0x30e20 | NVRAM env shadow in DRAM |
| heap serial copy | 0x3ff4bf58 | one of the non-HWRPB serial hits (correctly rejected by scan) |
| clock-interrupt PAL vectors | 0xa3c1, 0xa4c1, 0xec91 | fire ~20k times under the IIC spin window |

---

## 8. SYSVAR member map (SYSTYPE 0x22 = DEC_TSUNAMI)  [LIVE + SRC]

| member | (SYSVAR>>10) | SYSVAR word | Badge | Discriminator |
|--------|--------------|-------------|-------|---------------|
| 1 | 1 | **0x405** | AlphaPC 264DP (FALLBACK) | none (both fopens NULL) |
| 6 | 6 | **0x1805** | AlphaServer DS20 | iic_rcm_temp @ 0x9e |
| 8 | 8 | **0x2005** | DS20E / Goldrack | iic_8574_ocp @ 0x4E |
| **? [ES40?]** | ? | ? | **Compaq AlphaServer ES40** | **UNKNOWN -- discovery item** |

**Highest-value ES40 open question:** the ES40 (CLIPPER) member number + its
discriminator (IIC node or CSC strap). Sources to resolve: apisrm SysType table +
Tsunami HRM strap encoding; and directly, this trace's `GMEM-WATCH(0x2058)` value.
If ES40 badges "AlphaPC 264DP" (0x405) it fell back -- same class of miss as DS20,
fixable by the same discriminator-device manifest pattern once its node is known.

---

## 9. Flash / NVRAM persistence  [LIVE, DS20]

| Item | Location | Notes |
|------|----------|-------|
| flash backing (AMD-FSM) | ds20_v7_3.rom / es40 diag flash | write-unlock 0x5555 / 0x2AAA; persisted on clean exit only |
| NVRAM serial | flash offset 0x5f815 | inside the flash image |
| oem_string | flash NVRAM | non-empty MASKS the banner platform name -> fresh flash clears it |
| HWRPB | RAM only | NEVER persisted; rebuilt each boot (a DRAM event at from_init) |

---

## 10. Interrupt routing (21272)  [SRC + LIVE]

| Item | Value | Notes |
|------|-------|-------|
| device-class IRQ | b_irq<1> | DRIR & DIMn -> DIRn; any DIRn<55:0> asserts b_irq<1> (level, low) |
| error IRQ | b_irq<0> | DRIR <62:58>; <63> = internal Cchip error (NXM) |
| IPI | b_irq<3> | IPREQ->IPINTR; IER bit 36 (interproc EIEN) -> EI[3] |
| DIM0 unmasked bits | 48, 55, 61-63 | 55 = PCI0 INT (Cypress SIO 8259) = kIsaBridgeDrirBit; 48 = DS20 Cchip IIC TIG (unused in generic table); 61-63 = errors |
| PCI missing-NIC poke | PA 0x800_FFFF_0000 | firmware reads absent DE500 BAR (all-ones), pokes index/data into the void (non-fatal) |

---

## 11. ES40-specific expectations + deltas (the discovery surface)  [ES40?]

1. **SYSVAR member (badge)** -- Section 8; the top open item. Expect the SAME mechanism
   as DS20; ES40 resolves to the Clipper member (value unknown). Watch 0x2058.
2. **South bridge** -- real ES40 = ALi M1543C (vendor 0x10b9, dev 0x1533); the manifest
   ships a Cypress CY82C693 STAND-IN (vendor 0x1080, dev 0xc693) so the ISA/UART console
   works. SRM may probe the ALi at its own BDF/config and diverge during south-bridge
   init. Watch for ALi/M1543/Cypress lines + config-space probes.
3. **Dual-Pchip** -- ES40 populates hose 1 (Pchip1 @ 0x803_8000_0000); EmulatR mirrors
   it as coarse all-ones. Watch Pchip1 CSR accesses.
4. **4-CPU topology** -- ini set cpuCount=1 to avoid GCT spinning on absent secondaries;
   per-CPU HWRPB slots at 0x2180 stride 0x280 (slot1 0x2400) still apply.
5. **Profile clock 600 MHz** -- ES40 interval-timer constant (vs DS20). Cosmetic to the
   MHz_eff badge (which reports host throughput, not this).
6. **Manifest load** -- if the log shows "built-in default DS10 bus", es40_v7_3_platform
   .json did NOT load and the device set is wrong (wrong IIC/PCI tree -> bad discovery).

---

## 12. Quick grep menu for the ES40 console/machine logs

```
# manifest actually loaded (want ES40, NOT the DS10 fallback)
grep -iE "manifest|platform|built-in default|DS10 bus|ES40|Cypress|ALi|M1543"
# badge decision
grep -iE "GMEM-WATCH.0x2058|SYSVAR|SYSTYPE|member|AlphaServer|AlphaPC|Compaq"
# halt gate / stall
grep -iE "HALTPROBE|0x80130000040|smir|Halt Button|BOOT NOT POSSIBLE"
# chipset init / strap
grep -iE "Cchip|0x801A000|CSC|Pchip|0x8038000|DRIR|MCHK|machine check"
# boot progress / console wait
grep -iE "Console V|ISA table corrupt|Initializing table|P00>>>|UPD>|LFU|decompress"
# device probes
grep -iE "IIC-TXN|IIC-RD|IIC-CTRL|addr=0x9e|addr=0x40|addr=0x70|FRU"
# stop / profile
grep -iE "StopReason|MaxCycles|PROFILE|WARP-ACCOUNTING|fault|PANIC"
```
