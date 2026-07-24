<!--
EmulatR V5 -- AXPbox <-> EmulatR Interface Gap Analysis (three-way)
Project: EmulatR (Alpha 21264 / EV6 emulator), emulatrappuniv5 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Purpose: reconcile three authorities -- the HRM/datasheet spec hive
(Processor Support), the AXPbox-1.1.2 reference implementation, and the
emulatrappuniv5 live tree -- across four interface axes: device coverage,
system/CPU/device software seams, chipset register surface, and
config/platform description. ASCII(128) only.
Date: 2026-07-22.
-->

# AXPbox <-> EmulatR Interface Gap Analysis (three-way reconciliation)

## 0. What this document answers

The question on the table: "have we ascertained the gap between AXPbox and
EmulatR in terms of interfaces?" The answer is now yes, across the four axes
requested (device/peripheral coverage, system/CPU/device software seams,
chipset register interface, config/platform interface), and framed as the
three-way reconciliation the architect specified:

  Point 1 -- HRM  : the authoritative spec hive under D:\EmulatR\Processor
                    Support (datasheets + DEC PALcode + reference drivers).
  Point 2 -- AXP  : the AXPbox-1.1.2 reference implementation hive.
  Point 3 -- V5   : the emulatrappuniv5 live tree.

The interesting gap is rarely a simple two-way AXP-vs-V5 diff. It is the
three-way delta: where AXP and V5 disagree, HRM (and the VMS PALcode that
actually drives the boot) is the tie-breaker. This document uses the same
confidence marks as Tsunami_HRM_vs_AXPBox_Profile.md: [HRM] from spec,
[AXP] observed in AXPbox source, [V5] observed in the live tree, [?] needs
confirmation.

## 1. Provenance and snapshot caveats

- Interface surface for AXP was read from the D:\EmulatR\axpbox working copy.
  Its class/method surface is identical to axpbox-1.1.2 (headers differ only
  by a few hundred bytes of the architect's annotations; only System.cpp is
  materially newer in the working copy). For an interface gap analysis the
  two AXPbox hives are interchangeable; where a CSR body is quoted it is from
  the working-copy System.cpp.
- Not every V5 header was staged. TsunamiCchip.h, TsunamiPchip.h, and
  TsunamiDchip.h (the CSR switch bodies) were read only through the
  TsunamiChipset.h aggregator plus the profile doc. Every claim that depends
  on an unstaged body is marked [?] and collected in Section 8.
- HRM facts for the chipset axis are folded from the already-verified
  Tsunami_HRM_vs_AXPBox_Profile.md (cited [PROFILE]) rather than re-derived
  from the 880 KB HRM text. Device-datasheet facts are named against the
  Processor Support hive files (ALi M1543C datasheet, 53C895 manual, DEC
  21143 Tulip HRM, PC16550D, PCF8584) but were not re-read line by line for
  this pass; they are the confirmation source for the [?] device items.

## 2. Executive summary -- the gap in one paragraph

AXPbox and EmulatR are not two implementations of the same interface; they
are two different interface philosophies pointed at overlapping silicon.
AXPbox is a single-target (ES40 / Tsunami / ALi M1543C south bridge / SCSI
disk), inheritance-based, constructive machine: one CSystemComponent base
class, one flat MMIO range table, one interrupt() call, and a config file
that literally builds the device object graph. EmulatR V5 is a
multi-target (Tsunami + Typhoon + Titan; ALi and Cypress south bridges;
IDE/ATA disk), composition-based, declarative machine: several narrow role
interfaces, a region-switch MMIO router, a Cchip DRIR model with a real 8259
in front of it, and a JSON presence-manifest that enumerates what firmware
should discover. The consequence is a clean split of strengths: AXPbox is
ahead on breadth and depth of working devices (real SCSI, full graphics VGA,
keyboard/mouse, floppy, ISA DMA, bus-master DMA translation, PCI-master
scatter-gather), while EmulatR is ahead on structural fidelity to the actual
DEC boot path (per-array AAR memory sizing, NXM-through-Cchip error path,
faithful TIG halt-probe gate, RMC DPR, deterministic single-threaded
ordering, and a data-driven PCI/IIC identity surface). The reconciliation
work is: (a) close V5's device holes where the OS (not just the polling SRM
console) will exercise them -- SCSI or IDE-DMA+IRQ, keyboard, DMA; (b) close
V5's chipset holes where the HRM demands them -- per-CPU DIM masking and the
Pchip DMA-window CSRs; and (c) keep V5's structural wins rather than
regressing to AXPbox expedients (the 0xfe arbiter rev, the stored smir, the
single-array AAR).

### The sharpest gaps, ranked

1. [V5 hole, high] No SCSI anywhere. AXPbox's primary OpenVMS disk path is
   the Sym53C810/895 HBA + CSCSIBus + SCSI target. V5 has no HBA, no SCSI
   bus, and only a read-only ATAPI CD target (VirtualIsoDevice). V5 routes
   disk through IDE/ATA instead. See 3.1, 7 row A.
2. [V5 hole, high] V5 IDE is polled-PIO only -- no bus-master DMA, no IRQ
   (TODO(ali-ide-dma-irq)). Fine for the polling SRM console; a loaded OS
   driver will stall. AXPbox does full PRD bus-master DMA + IRQ. See 3.1.
3. [V5 hole, high] No chipset-owned bus-master DMA translation. AXPbox has
   CSystem::PCI_Phys / _direct_mapped / _scatter_gather (4 windows, SG PTE
   walk) on the PCI base class; V5's only DMA path is a per-device
   untranslated 32-bit-word closure (Dec21143Tulip::setDmaAccess), used by
   exactly one device. See 4.4-A.
4. [V5 hole, high, HRM-demanded] Per-CPU DIM masking (Cchip 0x200/240/600/
   640) and masked DIR (0x280..) not confirmed present. AXPbox computes
   drir & dim[i] per CPU. V5 asserts DRIR bits directly. [?] pending Cchip
   header. See 5.1, 8.
5. [V5 hole, med, HRM-demanded] Pchip DMA-window CSR file (WSBA/WSM/TBA/
   PCTL/PERROR) not confirmed as stored registers; V5 Pchip is an I/O
   router with a translateDmaToPa hook. AXPbox implements all with HRM RMW
   masks. See 5.2.
6. [V5 stub, med] Keyboard/8042 is a bare 0xAA->0x55 self-test handshake
   with no input backend and a permanently-deasserted IRQ1. No ISA 8237 DMA
   and no ISA 8254 PIT. AXPbox has full 8042+mouse, 8237, 8254. See 3.1.
7. [V5 stub, med] VGA is a text-console absorber (VgaTextConsole), not a
   graphics model. AXPbox ships full Cirrus and S3 Trio64. See 3.1.
8. [divergent silicon, track] For the same slots V5 sometimes models a
   different chip than AXPbox (Cypress CY82C693 south bridge / IDE for the
   DS10/DS20/PC264 class; generic text VGA). This is intentional multi-target
   scope, not a defect, but it means "the ES40 interface" is only partly
   shared. See 3.2.
9. [AXP hole, low] EmulatR-only devices with no AXPbox analog: PCF8584 IIC/
   SMBus (manifest-driven FRU/NVRAM), SMC FDC37C669 SuperIO, RMC DPR, Titan
   21274 chipset. These serve platforms AXPbox never targeted. See 3.1, 5.3.

## 3. Axis 1 -- Device / peripheral coverage

### 3.1 Coverage matrix

Fidelity key: FULL = functional model; PARTIAL = subset; STUB = presence /
handshake only; none = absent.

| Function | HRM part(s) | AXP class / fidelity | V5 class / fidelity | Verdict |
|---|---|---|---|---|
| IDE / ATA | ALi M1543C IDE; Cypress CY82C693 IDE | CAliM1543C_ide -- FULL (bus-master DMA + IRQ + ATAPI, threaded) | ITsunamiIde -> Cy82C693Ide + AliM5229Ide over shared AtaTaskfileEngine + PciConfigSpace -- PARTIAL (polled PIO + ATAPI FSM; no DMA, no IRQ; BARs RO-zero; TODO(ali-ide-dma-irq)) | PARTIAL; V5 adds Cypress variant AXP lacks |
| SCSI HBA | 53C895 Data Manual | CSym53C810, CSym53C895 -- FULL SCRIPTS engine, threaded | none | MISSING in V5 |
| SCSI target / bus | 53C895; SCSI-2 | CDisk/CDiskDevice + CSCSIDevice + CSCSIBus (phase machine) -- FULL | scsi::VirtualScsiDevice (abstract) + VirtualIsoDevice (RO ATAPI CD) over IBlockMedia -- PARTIAL (CD only, no bus, no fixed-disk target) | PARTIAL |
| NIC | DEC 21143 Tulip HRM | CDEC21143 -- FULL (TX+RX, MII, SROM, libpcap L2), threaded | Dec21143Tulip -- PARTIAL (enum + SROM/MAC + TX setup-frame + INTA; no RX, no host L2) | MATCHED chip / PARTIAL function |
| VGA / framebuffer | Cirrus / S3 Trio64 datasheets | CVGA + CCirrus + CS3Trio64 -- FULL graphics | VgaTextConsole -- STUB (text framebuffer 0xB8000 + benign register I/O; TODO(vga-graphics-tier)) | DIVERGENT-CHIP (generic text vs specific graphics) |
| Serial UART | PC16550D | CSerial -- FULL (telnet/socket) | Uart16550 -- FULL (register-level, deterministic RX inject, level IRQ) | MATCHED |
| Keyboard / mouse / 8042 | ALi M1543C KBC | CKeyboard -- FULL (8042 + kbd + mouse, scancode sets, IRQ1/IRQ12) | Kbd8042Stub -- STUB (0xAA->0x55 only, IRQ1 always false, no backend; task #45) | PARTIAL (bare stub) |
| Floppy FDC | 82077 / ALi | CFloppyController + CDMA + CDiskController -- FULL-ish | Floppy82077 -- STUB (deliberate fast-fail no-media probe satisfier) | PARTIAL |
| ISA DMA (8237) | ALi M1543C | inside CDMA -- FULL (8 ch, page regs) | none (ISADIAG flags page regs unhandled) | MISSING in V5 |
| PIC (8259 pair) | ALi M1543C | inside CAliM1543C -- FULL | Pic8259Pair -- FULL (ICW1-4/OCW, IRR/ISR/IMR, ELCR, INTA, cascade) | MATCHED (V5 standalone arguably more faithful) |
| PIT (8254) | ALi M1543C | inside CAliM1543C -- FULL-ish | none classic; Cchip interval timer serves timer duty | DIVERGENT (Cchip timer vs ISA 8254) |
| RTC / TOY | MC146818 / DS1287 | inside CAliM1543C -- FULL (regs A-D, 0x70-0x73) | ToyRtc -- FULL (MC146818, deterministic time, CMOS NVRAM; no ALi 0x72/0x73 high bank) | MATCHED |
| I2C / SMBus | PCF8584 datasheet; i2c-cchip.c | none | IicPcf8584 -- FULL (START/ADDR/RW FSM, manifest device table) | MISSING in AXP (V5-only) |
| USB | ALi M1543C USB | CAliM1543C_usb -- STUB (BAR reg file, "not functional") | AliPciFunctionStub(0x5237) -- STUB (config identity only) | MATCHED (both stubs) |
| Flash ROM | Am29F016 | CFlash -- FULL-ish (storage + minimal mode) | FlashRom -- FULL (Am29F016 command FSM, sector erase, persistence) | MATCHED (V5 more detailed) |
| Port80 / POST | ISA | CPort80 -- STUB | none | MISSING in V5 (trivial) |
| SuperIO | SMC FDC37C669 | none (ES40 integrates in M1543C) | Smc37c669SuperIo -- FULL (config-level, wraps Floppy82077) | MISSING in AXP (V5 DS10/PC264 part) |
| ISA / south-bridge func0 | ALi M1543C; Cypress CY82C693 | CAliM1543C -- FULL umbrella (ISA+DMA+PIC+PIT+TOY+LPT+NMI) | IsaBridge (ALi M1533/M1543C) + AliM1543C(chipsetLib) + Cy82C693IsaBridge -- config identity + I/O route; sub-functions are separate objects | MATCHED chip (ALi) + DIVERGENT (Cypress V5-only) |
| PCI config space | PCI 2.x | CPCIDevice base (config_data[8][64] + mask, add_function, register_bar) -- inheritance | PciConfigSpace (256B + writable mask) + IPciDeviceHandler -- composition | MATCHED (different architecture) |

### 3.2 Where the two model DIFFERENT silicon for the same slot

This is the crux of "in terms of interfaces": for several slots the ES40
interface AXPbox implements is not the interface V5 implements, because V5
also targets the DS10/DS20/PC264 (Cypress) and ES45 (Titan) boards.

- South bridge: AXP = ALi M1543C only. V5 = ALi M1543C/M1533 AND Cypress
  CY82C693 (0x1080/0xC693). Reconcile per-target, not globally.
- IDE: AXP = ALi M1543C integrated IDE. V5 = ALi M5229 (0x10B9/0x5229) AND
  Cypress CY82C693 IDE, both on the shared AtaTaskfileEngine.
- Primary disk transport: AXP = SCSI (Sym53C8xx). V5 = IDE/ATA. This is the
  single largest interface divergence and it is architectural, not a stub.
- VGA: AXP = specific Cirrus / S3 Trio64 graphics chips. V5 = chip-agnostic
  text absorber.
- Timer: AXP = ISA 8254 inside the south bridge. V5 = Tsunami Cchip interval
  timer (the HRM-correct primary tick for this platform; the 8254 is legacy).

## 4. Axis 2 -- System / CPU / device software seams

### 4.1 AXPbox seam (reference)

One hub, one contract. CSystem (System.hpp) holds fixed arrays
(MAX_COMPONENTS 100). Every device derives from CSystemComponent
(SystemComponent.hpp) and auto-registers in its base ctor
(system->RegisterComponent(this), SystemComponent.cpp:36). The device
contract is a single polymorphic interface:

  virtual u64  ReadMem(int index, u64 address, int dsize);   // dsize in BITS
  virtual void WriteMem(int index, u64 address, int dsize, u64 data);
  virtual int  SaveState(FILE*); virtual int RestoreState(FILE*);
  virtual void check_state();  // polled each Run() iteration -- the "tick"
  virtual void ResetPCI(); virtual void init();
  virtual void start_threads(); virtual void stop_threads();

Memory ranges are registered explicitly and idempotently
(CSystem::RegisterMemory(component, index, base, length), System.cpp:177;
re-registering an index rebases in place -- this is how live BAR
reprogramming works). MMIO/PIO dispatch is one flat linear scan of
asMemories[] in CSystem::ReadMem/WriteMem (System.cpp:708/449). IO ports and
MMIO are the same 64-bit address space at this layer; IO-ness is a per-BAR
attribute inside CPCIDevice. Interrupts: CSystem::interrupt(number, assert)
(System.cpp:1760) sets a DRIR bit then drives each CPU pin via
CAlphaCPU::irq_h(irq, assert, delay). PCI is CPCIDevice : CSystemComponent
(PCIDevice.hpp) with a reusable config-space engine (config_data[8][64] +
config_mask, add_function, register_bar) and, crucially, a chipset-owned
bus-master DMA path: do_pci_read/do_pci_write -> CSystem::PCI_Phys /
PCI_Phys_direct_mapped / PCI_Phys_scatter_gather (System.cpp:1887-2060) ->
PtrToMem. No per-device clock registration; devices are polled via
check_state().

### 4.2 EmulatR seam (live tree)

Several narrow seams, no single device base class. The CPU-facing bus is
ISystemBus on the chipset (TsunamiChipset.h:146): read/write/fetch(pa,
width) with width in BYTES (opposite of AXP's bit dsize). MMIO dispatch is a
region switch, not a scan: routeMmioOffset(off) -> RegionId enum ->
m_cchip / m_dchip / m_pchip realm objects. The device-facing contracts are
split by access class (IDeviceHandlers.h):

  struct IPciDeviceHandler { pciConfigRead(reg,width); pciConfigWrite(reg,value,width); };
  struct IIoPortHandler    { ioRead(port,width);       ioWrite(port,value,width); };
  struct IoPortRange { startPort; endPort; IIoPortHandler*; };
  struct PciMemRange { start; end; IIoPortHandler*; };  // NOTE: reuses IIoPortHandler

Registration is eager and hardcoded in the TsunamiChipset wiring
(m_pchip.registerIoPortRange / registerPciMemRange / registerPciDevice),
with an explicit "no removal API yet" (TsunamiChipset.h:730). Interrupts go
through a real 8259: evalDeviceIrqs() polls each device's pending flag,
drives Pic8259Pair, and mirrors PIC output onto Cchip DRIR<55> on level
change; PCI INTx uses raisePciInterrupt(pchip,line) ->
m_cchip.assertInterrupt(32+pchip*4+line); CPU delivery is gated by
Machine::canAcceptInterrupt (IPL/PALmode). PCI config space is per-device
(each device owns its decode via IPciDeviceHandler; PciConfigSpace is a
reusable 256-byte store + writable mask). DMA is a per-device injected pair
of closures (Dec21143Tulip::setDmaAccess(MemReadFn, MemWriteFn)), untranslated,
used by one device. IoSeam.h defines a future async submit/completion
transport (IIoTarget::submit, IWorkQueue) that is declared-only and NOT wired
into mmioRead yet. FirmwareDeviceManager is SRM device-tree metadata (HWRPB
builder, SHOW DEVICE), not an MMIO seam.

### 4.3 Concept map

| Concept | AXP | V5 | Note |
|---|---|---|---|
| Device base class | CSystemComponent (one iface) | none; IPciDeviceHandler + IIoPortHandler split | AXP one contract; V5 role-narrow ifaces; V5 has no SaveState virtual on device ifaces |
| Range registration | RegisterMemory(comp,index,base,len), idempotent rebase | registerIoPortRange / registerPciMemRange / registerPciDevice, no removal | V5 dense-mem claim capped to 16-bit rebased offset (PciMemRange reuses IIoPortHandler); no live rebase |
| MMIO dispatch | O(n) scan of asMemories[]; dsize bits | O(1) region switch -> realm objects; width bytes | V5 threads cpuId; AXP threads source component |
| PIO / IO-port | same table as MMIO | dedicated IIoPortHandler + separate sparse-IO route | AXP unifies IO/MMIO; V5 bifurcates |
| Interrupt assert | interrupt(number,assert) -> irq_h(delay) | raisePciInterrupt / assertInterrupt; 8259 + DRIR<55> mirror; Machine gate | AXP fixed 100-clock delay; V5 explicit 8259 + IPL arbitration |
| PCI config space | base-class engine + auto register_bar | per-device decode; PciConfigSpace store + mask | AXP reusable engine; V5 per-device |
| Bus-master DMA | do_pci_read/write + PCI_Phys SG (4 windows) | per-device setDmaAccess closures, untranslated | Major gap (4.4-A) |
| Clock / tick | check_state() polled per Run() | chipset.step(cycles) + evalDeviceIrqs polls fixed device set | neither has per-device registered periodic callback |

### 4.4 Sharpest seam gaps

A. Bus-master DMA translation. AXP: first-class chipset-owned SG target
   (PCI_Phys_scatter_gather, 4 windows, PTE walk) on the PCI base class,
   available to every PCI device. V5: one device, one untranslated 32-bit
   closure. Any second DMA-capable device re-implements it; a guest that
   programs a Pchip DMA window expecting translation is not served. This is
   the same hole seen from the seam side that Section 5.2 sees from the CSR
   side. [HRM] the 21272 defines WSBA/WSM/TBA windows and SG-TLB; both are
   required for faithful PCI DMA.
B. PCI INTx routing. AXP reads the config-space interrupt-line byte and
   asserts exactly that (firmware-programmed). V5 hardwires
   pciIntxToDrirBit = 32 + pchip*4 + (intx&3) and ignores the guest-written
   line register. [?] check against board wiring; HRM leaves board INTx
   swizzle unspecified, so this is a convention choice, but ignoring the
   guest routing byte can misroute a driver that reprograms it.
C. IO/MMIO unification. AXP one table, full 64-bit ranges. V5 forces dense
   PCI-mem claims back through IIoPortHandler with a 16-bit rebased-offset
   cap (asserted at registration) -- a structural limit AXP does not have.
D. Per-device lifecycle. AXP mandates SaveState/RestoreState/check_state/
   ResetPCI/init/threads on every device. V5 device ifaces carry only access
   methods; reset/snapshot are per-device ad hoc. Conversely V5 has typed
   BusResult/BusStatus::BusError + reportNxm NXM machinery and CPU-side IPL
   arbitration that AXP lacks (AXP just uses a fixed delay).
E. Registration model. AXP dynamic (base-ctor auto-register, Unregister,
   idempotent rebase -> live BAR reprogram works). V5 eager/hardcoded, "no
   removal API yet" -> dynamic remap / hot teardown unsupported at the seam.
F. Async offload seam. V5 has IoSeam.h scaffolding (submit/completion) not
   wired into mmioRead; AXP has none (synchronous only). Planned-but-absent
   on the V5 side.

## 5. Axis 3 -- Chipset register interface (Tsunami/Typhoon 21272, +Titan 21274)

Block bases agree with the HRM PA map (Cchip 0x801_A000_0000, Pchip0
0x801_8000_0000, Pchip1 0x803_8000_0000, Dchip 0x801_B000_0000, TIG
0x801_0000_0000). AXP CSR bodies are literal from System.cpp
(cchip_csr_read/write @1331/1377, pchip @1227/1274, dchip @1456/1472, tig
@1520/1542, interrupt @1760). V5 Cchip/Pchip/Dchip switch bodies were not
staged; V5 columns below are from the aggregator + [PROFILE] and any
register-level V5 claim is [?].

### 5.1 Cchip CSR

| Reg | Off | HRM purpose | AXP | V5 | Verdict |
|---|---|---|---|---|---|
| CSC | 0x000 | clock ratios, CPU/array/Pchip presence | store, RMW masks | derived from variant/cpuCount; Titan CSC<14> pchip1 setter TODO | both store; V5 derives |
| MISC | 0x080 | rev, CPUID, arb REQ/GRANT, ITINTR<7:4>, IPINTR<11:8>, IPREQ<15:12>, NXM, NXS | W1S 0x00000f0000f00000, W1C 0x0000000010000ff0; full 4-CPU arbitration; CPUID OR'd live | ITINTR set by fireIntervalTimer, W1C via miscWriteW1C; NXM/NXS via latchNxm(); arbitration fields [?] | both model ITINTR/IPINTR/NXM/NXS; V5 arb unconfirmed; V5 bit layout partly AXP-derived, _PROVISIONAL |
| AAR0 | 0x100 | array 0 base ADDR<34:24> + size ASIZ<15:12> | returns (iNumMemoryBits-23)<<12 = size only; single array | real per-array base+ASIZ (3-bit Tsunami / 4-bit Typhoon-Titan), hard-stop on over-mem | V5 more faithful [PROFILE 2.1] |
| AAR1-3 | 0x140/180/1c0 | arrays 1-3 | return 0 (absent) | per-array compute | V5 more faithful |
| DIM0-3 | 0x200/240/600/640 | device interrupt mask per CPU | full dim[] array; store, defer re-eval | [?] not confirmed present | GAP: AXP ahead; confirm V5 |
| DIR0-3 | 0x280/2c0/680/6c0 | masked request (RO) = DRIR & DIMn | drir & dim[idx], RO CSR | internal pendingIrqN queries; guest-readable CSR [?] | AXP exposes as CSR; V5 as method |
| DRIR | 0x300 | raw device request (RO) | state.cchip.drir, RO | assertInterrupt/deassertInterrupt set/clear; ISA->55, PCI INTx->32+4*pchip+line, NXM->63 | both latch 64-bit DRIR; V5 guest-readable 0x300 [?] |
| IIC0-1 | Cchip IIC | interval-timer suppress counters | not modeled | not modeled (PCF8584 is a separate device) | neither; [?] open HRM item |
| TTR/TDR | TIG timing | TIGbus timing regs | in tig_read/write, not cchip | storage-only in Cchip; real TIG in TsunamiTig | equivalent |
| PWR | config | power mgmt | not modeled | not modeled | neither |

### 5.2 Pchip CSR

AXP implements the full DMA-window file with HRM RMW masks: WSBA0-2
0x000/040/080, WSBA3 0x0c0 (SG/DAC special, enable forced), WSM0-3
0x100..1c0, TBA0-3 0x200..2c0, PCTL 0x300, PLAT 0x340, PERROR 0x3c0,
PERRMASK 0x400, TLBIV 0x480 (stub), TLBIA 0x4c0 (stub), PCI reset 0x800 ->
ResetPCI() fan-out. V5's Pchip is functionally an I/O/MMIO router (sparse
mem/IO windows, port registries, IACK intercept at chipset offset
0x1_F800_0000, translateDmaToPa hook) rather than a WSBA/WSM/TBA/PCTL
register file; the register-level storage is [?] (Pchip header not staged),
and [PROFILE 3] recorded "V4 models essentially none of it" at the 2026-05
snapshot. Gap: for faithful PCI-master DMA translation V5 needs the WSBA/WSM/
TBA/PCTL registers, not just the translate hook. Neither side models PMONCTL.

### 5.3 Dchip + DPR + TIGbus

- Dchip: AXP has DSC 0x800 / STR 0x840 / DREV 0x880 / DSC2 0x8c0 (RO; writes
  unsupported). V5 carries DREV per variant (Tsunami 0x10, Typhoon 0x20,
  Titan 0x22 in TsunamiVariant.h); DSC/STR/DSC2 modeling [?].
- DPR (RMC dual-port RAM): V5-only faithful model (TsunamiDpr, 16 KB, ES40/
  Typhoon) from Compaq EK-ES240-SV.A01 Table C-1, including 0xda=0xaa
  "TIG load done" (the byte pc264 SRM checks) and 0xd9=0xba I2C-done
  (corrected vs AXPbox's misplacement). AXP references a CDPR as oracle only.
- TIGbus: V5 models it as a real register file (TsunamiTig) clean-roomed from
  DEC apisrm (TSUNAMI_IO.C), with a faithful smir halt-probe gate (status-
  only 0, no stored W1C re-assertion) and arbiter/PLD rev = 0 placeholder. AXP
  uses tig_read/write byte switches with a hardcoded arbiter rev 0xfe and a
  stored smir. V5 is the more faithful of the two here.

### 5.4 IRQ line model

EV6 line map (both): EI[0]->IPL30 (error/NXM), EI[1]->IPL23 (device), EI[2]->
IPL22 (interval timer), EI[3]->IPL21 (IPI), EI[4]/EI[5] CPU-internal perf.
AXP: state.eir latched by irq_h; delivery (eien & eir) && !(pc&1); interrupt()
sets DRIR and drives per-CPU drir & dim[i] -> irq_h(1/0, delay 100) for
device, irq_h(2, delay 0) for timer, irq_h(3) for IPI from MISC IPREQ. V5:
interval timer via chipset.step -> fireIntervalTimer (b_irq<2> level latch,
W1C on MISC<ITINTR>); device path via evalDeviceIrqs -> Pic8259Pair -> DRIR<55>
on level change; PCI INTx -> assertInterrupt(32+4*pchip+line); NXM ->
latchNxm -> DRIR<63>; delivery gated by Machine::canAcceptInterrupt.

Known V4 bugs [PROFILE] and V5 status:
- Timer latch inside canAcceptInterrupt gate [PROFILE 1.2, task #70]: ticks
  dropped while masked -> firmware polling MISC<ITINTR> hangs. V5 step() now
  latches from the cycle accumulator outside the accept gate -- consistent
  with the fix having landed. AXP never had this bug.
- Missing DRIR/DIM device path [PROFILE 1.3]: V4 modeled none; V5 has added
  the DRIR device path (assert/deassert, evalDeviceIrqs, PCI INTx + ISA
  DRIR<55> conventions). The per-CPU DIM masking gate (AXP drir & dim[i])
  remains [?] -- the sharpest open chipset question.

### 5.5 Where each diverges from HRM

- AXP from HRM: DIM stored without immediate re-eval (ordering shortcut);
  AAR single-array collapse; arbiter rev 0xfe and stored smir are non-HRM
  expedients; no IICn suppress counters; no PMONCTL; the 100-clock device
  irq delay is an AXP realism invention, not an HRM constant.
- V5 from HRM: interval-timer rate is a power-of-two approximation
  (~953.7 Hz at 1 GHz vs HRM 1024 Hz nominal -- named trade-off, firmware is
  rate-agnostic); DRIR-bit-to-device wiring is a documented V5 convention
  (docs/hrm_deviations.md); MISC bit layout partly AXP-derived and
  _PROVISIONAL pending HRM 10.2.2.3; TIG rev and Titan DREV/CREV are
  placeholders pending the 21274 Eng Spec.

## 6. Axis 4 -- Config / platform interface

AXPbox config is CONSTRUCTIVE: es40.cfg is a recursive brace block language
parsed by CConfigurator that literally builds the device object graph. The
instance name carries topology (pci<bus>.<dev>, disk<bus>.<dev>, cpuN,
serialN); the class word (tsunami, ev68cb, ali, ali_ide, dec21143,
sym53c810/895, file/device/ramdisk, cirrus/s3) selects a C++ constructor and
structural flags (HAS_PCI, HAS_DISK, IS_NIC, ON_GUI...). Keys are rich and
per-block: rom.srm/rom.flash/rom.dpr, memory.bits (power-of-two exponent),
cpu speed/icache, disk file/serial_number/model_number/rev_number/
autocreate_size/cdrom, NIC adapter/mac, serial port/action, vga rom,
timezone, gui backend.

EmulatR config is DECLARATIVE: *_platform.json is a strict JSON presence
manifest (systemLib::DeviceManifest) with exactly two inventories,
iic_devices[] and pci_devices[], plus a platform badge string. It carries
NO cpu / memory / chipset / firmware keys -- those live in the ini
([System] model/cpuCount/memorySize) and CLI (AppOptions: --firmware,
--mem, --pal-mode, snapshot/trace/log flags). PCI entries are far richer in
identity than AXP: full BDF hose.bus.slot.func (function IS represented,
unlike AXP), vendor/device/class_code/bars[]/interrupt_pin/option_rom,
synthesized into a real 256-byte type-0 header with a BAR size-probe
handshake (ManifestPciDevice). storage[] is IDE-only (channel/unit/lun,
ata_disk/atapi_cdrom, media/media_kind). PlatformCapabilities::derive()
computes feature bits (SbAli/SbCypress, ChipsetTsunami/Typhoon/Titan,
DualPchip, ConsoleUartCom2) from the manifest rather than hard-coding model
checks.

Concept map highlights (full table available on request): machine identity
AXP one class word vs V5 badge + variant + derived caps across three
sources; CPU count AXP per-CPU nodes vs V5 ini/CLI scalar; memory AXP
memory.bits exponent-in-config vs V5 --mem raw-bytes-CLI; PCI AXP bus.device
vs V5 hose.bus.slot.func; disk AXP file/device/ramdisk + serial/rev + SCSI-id
vs V5 IDE channel/unit only; NIC AXP adapter+mac vs V5 presence-only; serial
AXP port+action vs V5 none (derived UART bit); firmware AXP rom.srm-in-config
vs V5 --firmware-CLI.

Sharpest config gaps:
- AXP has that V5 lacks: arbitrary device tree (any CPU/PCI count wired
  anywhere); CPU speed/icache; per-disk serial/rev + host raw-device
  passthrough + ramdisk; SCSI-id addressing; live NIC binding (adapter+mac);
  serial/console routing (telnet port + auto-launch); real option-ROM image
  loading; GUI backend; timezone; firmware path inside the portable config.
- V5 has that AXP lacks: full PCI BDF incl hose + function and dual-Pchip;
  data-driven PCI identity (vendor/device/class_code/bars/interrupt_pin re-
  pointable from config, not baked into C++); IIC/SMBus node inventory
  driving SRM sysvar/badge/power; capability abstraction (SbAli scoping
  across ES40/ES45); _PROVISIONAL fidelity discipline; graceful-degradation
  loader (bad manifest -> compiled-in default DS10, still ok).
- Structural bottom line: AXP's config expressiveness ceiling is "any device
  the emulator has a class for, wired anywhere"; V5's is "enumerate what
  firmware should discover, with far richer PCI/IIC identity, but CPUs/memory/
  console/SCSI/media-host-binding live outside the file."

## 7. Three-way reconciliation table (the crux)

For each interface where AXP and V5 disagree, the HRM/PALcode tie-breaker and
the recommended direction. Direction = which authority V5 should converge to.

| Interface | AXP | V5 | HRM / PALcode says | Recommended direction |
|---|---|---|---|---|
| A. Primary disk transport | SCSI (Sym53C8xx) | IDE/ATA | ES40 ships BOTH on-board SCSI and IDE; DS10/DS20 are IDE-primary. VMS PALcode is transport-agnostic (boots whatever HWRPB advertises) | Keep IDE as V5 primary for DS10/DS20; add IDE DMA+IRQ (row B) before OS load. SCSI is a later ES40-fidelity track, not a boot blocker |
| B. IDE DMA + IRQ | full PRD bus-master + IRQ | polled PIO, no IRQ | ALi M5229 / CY82C693 datasheets define bus-master IDE + INTA; an OS ATA driver requires it | Close TODO(ali-ide-dma-irq): add bus-master DMA + interrupt. High priority for OS (not console) boot |
| C. Bus-master DMA translation | PCI_Phys SG (WSBA/WSM/TBA) | per-device untranslated closure | HRM 21272 Ch.3: WSBA/WSM/TBA windows + SG-TLB are the defined DMA path | Add Pchip WSBA/WSM/TBA CSRs (row F) and a shared chipset DMA-translate used by all PCI masters |
| D. Per-CPU DIM masking | drir & dim[i] per CPU | direct DRIR assert [?] | HRM 6.x + [PROFILE 1.3]: DIRn = DRIR & DIMn is the defined per-CPU effective request | Confirm/add DIM masking + guest-readable DIM/DIR/DRIR CSRs. Needed for correct multi-line + eventual SMP |
| E. Interval timer source | ISA 8254 + Cchip | Cchip interval timer | HRM 6.3.2: Cchip b_irq<2> interval timer is the platform tick; 8254 is legacy ISA | Keep V5 Cchip timer (HRM-correct); 8254 only if a guest legacy driver probes it |
| F. AAR memory sizing | single-array size read | per-array base+ASIZ | HRM 10.x: AAR encodes per-array base + ASIZ | Keep V5 (more faithful); do NOT regress to AXP single-array |
| G. NXM error path | none explicit | Cchip MISC<NXM>+DRIR<63> -> b_irq<0> | HRM 6/10: NXM raises an error interrupt via Cchip, not a raw CPU MCHK | Keep V5 (HRM-correct) |
| H. TIG smir / arbiter rev | stored smir, rev 0xfe | status-only smir, rev 0 | apisrm TSUNAMI_IO.C: smir is a halt-probe status gate; rev is board-specific | Keep V5 clean-room (do NOT copy AXP's 0xfe / stored smir) |
| I. PCI INTx routing | config interrupt-line byte | hardwired 32+4*pchip+line | HRM leaves board swizzle unspecified; config line byte is guest-programmable | Honor the config-space interrupt-line byte where a driver reprograms it; keep the DRIR map as default |
| J. Keyboard/8042 | full 8042+mouse | 0xAA->0x55 stub | ALi M1543C KBC datasheet | Build the 8042 command FSM (task #45) when interactive console/OS input is needed |
| K. VGA | Cirrus/S3 graphics | text absorber | board VGA option cards | Text console is enough for SRM; graphics is a far-future tier |

## 8. Open items requiring unstaged headers or HRM confirmation

To close the [?] cells, read these V5 headers (not staged this pass):
- TsunamiCchip.h -- confirm (1) per-CPU DIM masking gate between DRIR and
  effective request; (2) guest-readable DIM/DIR/DRIR CSRs at 0x200/0x280/
  0x300; (3) MISC W1S/W1C mask bit layout vs AXP 0x00000f0000f00000 /
  0x0000000010000ff0; (4) MISC arbitration REQ/GRANT field modeling.
- TsunamiPchip.h -- confirm whether WSBA/WSM/TBA/PCTL/PERROR are stored
  registers or the Pchip is purely an I/O router with a translate hook.
- TsunamiDchip.h -- confirm DSC/STR/DSC2 beyond DREV.
HRM confirmations still open (from [PROFILE 7.9], unchanged): INTERRUPT PAL
entry offset; CSR 0x100 memory-size encoding; IICn semantics + any device-IRQ
delivery delay; exact MISC bit layout vs HRM 10.2.2.3.

## 9. Recommended reconciliation sequence

Ordered by boot-path impact, each independently buildable and cl67/firmware-
validatable, honoring the V5 discuss-before-code + _PROVISIONAL discipline:

1. Confirm the [?] Cchip/Pchip items (Section 8) -- read the two headers;
   this is analysis, not code, and unblocks D and C accurately.
2. Per-CPU DIM masking + guest-readable DIM/DIR/DRIR CSRs (row D) if 8 finds
   them absent. HRM-demanded; low blast radius (Cchip-local).
3. IDE bus-master DMA + IRQ (row B) -- the highest-value device gap for OS
   (not console) boot. Close TODO(ali-ide-dma-irq).
4. Pchip WSBA/WSM/TBA/PCTL CSRs + a shared chipset DMA-translate (rows C, F)
   -- makes B's DMA and any PCI-master DMA HRM-faithful.
5. Keyboard 8042 command FSM (row J) -- when interactive input is needed.
6. SCSI HBA + bus + fixed-disk target (row A) -- the largest device track;
   ES40-fidelity, deferrable behind IDE-DMA for DS10/DS20 boot.
7. VGA graphics tier (row K) -- far future.

Throughout: keep the V5 structural wins (AAR per-array, NXM-through-Cchip,
TIG clean-room, deterministic single-threaded ordering, data-driven PCI/IIC
identity). The reconciliation is additive to V5's fidelity, not a regression
toward AXPbox expedients.
