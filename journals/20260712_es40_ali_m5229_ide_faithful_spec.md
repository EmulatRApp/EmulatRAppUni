<!--
EmulatR V4 -- ES40 ALi M5229 IDE: faithful config-space implementation SPEC.
Discuss-before-code artifact -- NO code changed.  ASCII(128); hex radix.
HRM: ALi M1543C Desktop Southbridge Preliminary Datasheet v1.10, Sec 4.1.2
"IDE Master M5229 Configuration Registers" (IDSEL = AD27).
Oracle: axpbox/src/AliM1543C_ide.cpp (AliM1543C_ide_cfg_data/_cfg_mask).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
-->

# ES40 ALi M5229 IDE -- faithful config-space implementation (SPEC, 2026-07-12)

## 0. Status

Discuss-before-code. This is the design; no source edited. On approval, the
edits in section 5 land as surgical Edits, then the section 7 tests run.

## 1. What today's runs established (facts, not plan)

- DS20 (tools/run_ds20_showdev.sh) enumerates BOTH IDE units:
  `dqa0.0.0.105.0 EMULATR VIRTUAL DISK` + `dqa1.1.0.105.0 EMULATR VIRTUAL CDROM`.
  The CY82C693 ATA taskfile engine is sound; it is NOT the problem.
- ES40 (tools/run_es40_showdev.sh) DOES enumerate the disk once media is
  attached: `show config` shows `5/1 Cypress 82C693 IDE  dqa0.0.0.105.0`.
  The original "ES40 lists only dva0" symptom was the empty-media manifest,
  not a south-bridge defect.
- The defect that remains is a MIS-CATEGORIZATION. func1 (the IDE at PCI
  00:05.1) presents the Cypress identity -- IDE-TRACE `reg=0x00 val=0xC6931080`
  (vendor 0x1080, device 0xC693), set in Cy82C693Ide.h:288. The ES40 console
  name-lookup is ID-based for the IDE, so it prints "Cypress 82C693 IDE".
- func0 (the ISA bridge at 00:05.0) is ALREADY the ALi part: `show config`
  shows `5/0 Acer Labs M1543C`. wireDevices() (TsunamiChipset.h:639) gates the
  bridge by isAliPlatform() -> AliM1543C (0x10B9/0x1533). Only the IDE function
  was left as the Cypress stand-in.
- The console programs the BARs to the LEGACY addresses (IDE-TRACE
  `reg=0x10 val=0x000001F0`, `reg=0x14 val=0x000003F6`) and enumerates over the
  fixed 0x1F0/0x170 command blocks. i.e. the ES40 SRM drives the IDE in
  COMPATIBLE mode, matching the fixed-port registration at
  TsunamiChipset.h:673-676.

## 2. Root cause, precisely

On ALi platforms wireDevices() gates the BRIDGE (func0) to ALi but registers
the SAME Cy82C693Ide (Cypress identity) at func1 UNCONDITIONALLY
(TsunamiChipset.h:672). So ES40 shows an Acer bridge hosting a Cypress IDE.
The fix is to give func1 the faithful ALi M5229 PCI personality on ALi
platforms, while leaving DS10/DS20 on the Cypress identity (do-no-harm).

## 3. Authoritative M5229 config map (datasheet Sec 4.1.2, confirmed by axpbox)

Datasheet default values (line refs are M1543C_Desktop_Southbridge.txt):

    Off   Reg   Attr        Default      Notes
    00-01 VID   R           0x10B9       vendor (Acer Labs)
    02-03 DID   R           0x5229       device (M5229 IDE)
    04-05 COM   R/W         0x0000       command (SRM sets IO/BM enables)
    06-07 STS   R/W-clear   0x0280       status (DEVSEL medium)
    08    RID   R           0xC1         revision
    09    PIF   R/W         0xFA         prog-IF  (see 3.1)
    0A    SCC   R/W         0x01         subclass = IDE
    0B    BCC   R/W         0x01         base class = mass storage
    0C    resv  R           0x00
    0D    LT    R/W         0x00         latency timer
    0E    HT    R           0x00         header type 0 (single-function; the
                                          multifunction bit lives on func0)
    0F    resv  R           0x00
    10-13 BAI   R/W         0x000001F1   primary cmd block  (0x1F0 | IO)
    14-17 BAII  R/W         0x000003F5   primary control    (0x3F4 | IO)
    18-1B BAIII R/W         0x00000171   secondary cmd block(0x170 | IO)
    1C-1F BAIV  R/W         0x00000375   secondary control  (0x374 | IO)
    20-23 BAV   R/W         0x0000F001   bus-master IDE base (0xF000 | IO)
    2C-2D SVID  R/W-lock    0x0000
    2E-2F SDID  R/W-lock    0x0000
    30-3B resv  R           0x00
    3C    IL    R/W         0x00         interrupt line
    3D    IP    R           0x01         interrupt pin (INTA)
    3E    MG    R           0x02         min_gnt
    3F    ML    R           0x04         max_lat

axpbox cross-check (AliM1543C_ide_cfg_data): 0x00=0x522910B9, 0x04=0x02800000,
0x08=0x0101FAC1, 0x10=0x000001F1, 0x14=0x000003F5, 0x18=0x00000171,
0x1C=0x00000375, 0x20=0x0000F001, 0x3C=0x040201FF (IL/IP/MG/ML). Identical to
the datasheet. Extra M5229 timing regs axpbox seeds: 0x48=0x4A000000 (UDMA
test), 0x54=0x44445555 (UDMA setting + FIFO threshold), 0x78=0x00000021 (IDE
clock). These have no timing effect in a best-effort-deterministic model; they
are store-through with the seeded defaults so SRM read-back is faithful.

### 3.1 prog-IF 0xFA reconciles "compatible mode is default"

Datasheet line 7495: "Compatible mode is the default mode; native PCI mode
will only be enabled [explicitly]." prog-IF 0xFA = 1111_1010b: bit0 (primary
native) = 0 and bit2 (secondary native) = 0 -> BOTH channels COMPATIBLE by
default; bit1/bit3 (native-capable) = 1; bit7 (bus-master) = 1. So 0xFA is the
reset value AND it means compatible-mode operation -> fixed ports 0x1F0/0x170,
exactly what the ES40 trace showed and what our fixed-port handler answers.
CONSEQUENCE: adopting the M5229 identity does NOT move the console off the
working compatible-mode path. This is the key low-risk finding.

## 4. Design decision

Two shapes; recommending A.

(A) PERSONALITY INJECTION into the existing IDE device  [RECOMMENDED]
    Cy82C693Ide already owns the func1 config space + the proven ATA/ATAPI
    taskfile engine (dqa disk + CD). ATA taskfile behavior is chip-independent;
    only the PCI config-space identity/mask differs between Cypress and M5229.
    Add a small identity selector; initConfig() fills the config array from the
    selected map; a per-identity writable-mask replaces the hardcoded RO check.
    wireDevices() selects M5229 on isAliPlatform() before registration.
    + Reuses the engine DS20 already proved.
    + Surgical (V4 rule: prefer Edit over rewrite).
    + Do-no-harm trivially: default stays Cypress.
    - The class name Cy82C693Ide now carries two personalities (documented).

(B) NEW AliM5229Ide class sharing the ATA engine
    Extract the taskfile engine to a shared base and add a dedicated M5229
    config device, registered at func1 on ALi platforms.
    + Cleanest identity separation; mirrors AliM1543C.h precedent.
    - Large refactor of a working ~640-line device (regression risk), for a
      difference that is purely config-space bytes.

Recommendation: (A). It delivers a FAITHFUL M5229 PCI presentation (identity,
class, prog-IF, native BAR defaults, interrupt pin, timing-reg defaults, and
datasheet R/W masks) with minimal blast radius, and keeps the one proven ATA
engine. If you want the stronger structural separation, we do (B) instead.

## 5. Seam-by-seam edit shape (for approval)

FILE 1: deviceLib/Tsunami/Cy82C693Ide.h
  a. Add enum + member (near m_cfg, ~line 283):
       enum class IdeIdentity : uint8_t { CypressCy82C693, AliM5229 };
       IdeIdentity m_identity = IdeIdentity::CypressCy82C693;   // default = DS10/DS20
       std::array<uint8_t,256> m_cfgWMask{};                    // per-identity RW mask
  b. Add setter (public, ~line 108, before wiring):
       void setIdentity(IdeIdentity id) noexcept { m_identity = id; initConfig(); }
  c. Rework initConfig() (285-296): switch on m_identity; fill m_cfg + m_cfgWMask
     from the selected map. Cypress branch = today's exact bytes (unchanged
     values) so DS10/DS20 stay byte-identical. AliM5229 branch = the section 3
     map + the axpbox timing seeds (0x48/0x54/0x78).
  d. Replace the hardcoded RO test in pciConfigWrite (235-237) with a
     mask-driven write: `m_cfg[off] = (m_cfg[off] & ~mask) | (val & mask);`
     using m_cfgWMask[off]. BAR alignment masks from datasheet/axpbox cfg_mask
     (0x10:0xFFFFFFF8, 0x14:0xFFFFFFFC, 0x18:0xFFFFFFF8, 0x1C:0xFFFFFFFC,
     0x20:0xFFFFFFF0). Status 0x06-07 write-1-clear per axpbox mask 0x00000105.
  e. Header block + inline change comments per ADR-0001 (FILE/FUNCTION/CHANGE).

FILE 2: chipsetLib/TsunamiChipset.h
  a. In wireDevices(), immediately before line 672
     (`m_pchip.registerPciDevice(0, 5, 1, &m_ide);`), add:
       if (isAliPlatform(m_model))
           m_ide.setIdentity(Cy82C693Ide::IdeIdentity::AliM5229);  // ES40/ES45 = M5229
  b. Header block + inline comment.

No change to the ATA engine, port registrations, media attach, or DS-class
paths. No new file (option A). Include-guard / ASCII / hex-radix rules honored.

## 6. Do-no-harm

DS10/DS20 (isAliPlatform == false) never call setIdentity(); m_identity stays
CypressCy82C693; initConfig() Cypress branch reproduces today's exact bytes.
=> DS10/DS20 config space byte-identical. ES40/ES45 (isAliPlatform == true) get
the M5229 personality. Gate is the same isAliPlatform() the bridge already uses
(TsunamiChipset.h:631), so bridge and IDE stay consistent by construction.

## 7. Test / verification plan

1. Build clean (MSVC/Qt), no warnings.
2. DS20 regression: tools/run_ds20_showdev.sh -> P00, `show dev` still lists
   BOTH dqa0 + dqa1; IDE-TRACE `reg=0x00` still 0xC6931080 (Cypress unchanged).
3. ES40 faithful: tools/run_es40_showdev.sh -> P00.
   - IDE-TRACE `reg=0x00` now 0x522910B9 (ALi), `reg=0x08` now ...FA (prog-IF).
   - `show config` 5/1 no longer says "Cypress 82C693 IDE" (expect an ALi/M5229
     name, consistent with the 5/0 Acer bridge).
   - dqa0 STILL enumerates (compatible-mode path preserved, section 3.1).
   - Capture whether dqa1 (CD) now appears (see open question d).
4. Diff the ES40 IDE-TRACE cfg block before/after to confirm the console takes
   the same enumeration path (no unexpected native-mode BAR reprogramming).

## 8. Open questions for Tim

a. Shape: option A (personality injection, recommended) or B (separate
   AliM5229Ide class)?
b. Depth of the M5229 timing registers (0x40-0x7B UDMA/FIFO/clock): seed the
   axpbox-known defaults + store-through only (recommended, no timing model),
   or model more?
c. Class-code register R/W: datasheet marks CC (09-0Bh) R/W (lets SRM flip
   prog-IF native/compat). Honor R/W (faithful) or pin RO for safety? If R/W
   and the SRM ever flips to native, our fixed-port model would need native BAR
   addressing -- not observed in the ES40 trace, but a latent risk.
d. Scope of the dqa1-on-ES40 gap: DS20 shows the no-disc CD; ES40 `show dev`
   did not. Fold into this task or track separately?

## 9. Decision log (2026-07-12)

- SHAPE = Option B (Tim): dedicated AliM5229Ide class, separation of
  responsibility gated by executing model. NOT personality injection.
- Binding constraint: faithful-implementation-no-shortcuts. Every M5229 value
  traces to datasheet Sec 4.1.2 (confirmed by axpbox). Any value not in the
  datasheet is marked _PROVISIONAL and confirmed before it drives dispatch.
- Q2/Q3/Q4: pending Tim ("other answers to follow").

### 9.1 New sub-decision B forces: how to share the ATA taskfile engine

The ATA/ATAPI taskfile behavior (Channel[2], disk[2]+CD, IDENTIFY, PIO stream,
ATAPI CDB) is chip-INDEPENDENT and is already proven on DS20. Option B must NOT
duplicate it (duplication => divergence risk + two engines to keep faithful).
Two ways to separate PCI identity from the shared engine:

(B-comp) COMPOSITION  [RECOMMENDED]
    New AtaTaskfileEngine (the current Cy82C693Ide I/O-port body, lifted
    verbatim: IIoPortHandler + channels + disks + ATA/ATAPI). Two thin PCI
    controllers each OWN one engine and forward IIoPortHandler to it, while
    implementing IPciDeviceHandler with their own identity:
      class Cy82C693Ide : IPciDeviceHandler { AtaTaskfileEngine eng; Cypress cfg }
      class AliM5229Ide  : IPciDeviceHandler { AtaTaskfileEngine eng; M5229   cfg }
    wireDevices() registers the model-selected controller at (0,5,1) and routes
    the 0x1F0/0x170/0x3F6/0x376 port ranges to its engine.
    + Cleanest "separation of responsibility" (engine = ATA, controller = PCI
      personality); matches Tim's framing.
    + DS20 path runs the SAME engine code (behavior-preserving; regression gate
      = DS20 still enumerates both units, IDE-TRACE reg0x00 = 0xC6931080).
    - Lifts the engine body out of Cy82C693Ide (behavior-preserving move).

(B-inherit) INHERITANCE
    Base TsunamiIdeController = engine; Cy82C693Ide / AliM5229Ide derive and
    override only config-space. Similar result, tighter base-class coupling.

Recommend B-comp. Do-no-harm gate unchanged: DS10/DS20 wire Cy82C693Ide (byte-
identical Cypress identity); ES40/ES45 (isAliPlatform) wire AliM5229Ide.

### 9.2 Faithfulness note on the M5229 timing registers (informs Q2)

0x40-0x7B (UDMA timing / FIFO threshold / IDE clock) have documented RESET
values (datasheet) and R/W attributes but their EFFECT is cycle-accurate DMA
timing, which EmulatR's best-effort-deterministic model does not consume. Under
faithful-implementation-no-shortcuts this is a legitimate INERT-TRUTHFUL
deferral IF: reset values = datasheet, R/W mask = datasheet attributes, and the
deferred timing EFFECT is recorded with the datasheet reference (TODO tag). It
is NOT storage-masking because no consumer reads a timing side-effect. That is
the proposed Q2 answer; confirm.

## 10. Edit map -- B-comp shape (SUPERSEDES section 5; decision 2026-07-12)

Decision: Option B, COMPOSITION (B-comp). A dedicated AliM5229Ide, gated by the
executing model, sharing the ATA engine by composition -- chosen for the clean
interface-registration seam (one controller object per identity registered at
00:05.1). Section 5 above (option A, personality injection) is retained only as
history; THIS is the authoritative edit map.

### 10.0 Open question RESOLVED: PciConfigSpace composes out too

Yes -- factor a small PciConfigSpace value-type ALONGSIDE the engine. The
config-space MECHANICS (256-byte store; width-correct read across offsets;
masked write; status write-1-clear; BAR alignment masks) are identical PCI
semantics independent of device identity. Each controller supplies only DATA:
its reset-value byte map + its R/W mask, from the datasheet. Result = clean
three-part separation:
  AtaTaskfileEngine  (ATA/ATAPI behavior)  +
  PciConfigSpace     (PCI config mechanics) +
  <controller>       (identity data + the glue binding engine ports and config
                      to the PCI function).
Bonus: PciConfigSpace is directly unit-testable (RO fields reject writes; BAR
alignment; status W1C). SCOPE GUARD: introduce PciConfigSpace for the two IDE
controllers ONLY; do NOT refactor Cy82C693IsaBridge / AliPciFunctionStub /
AliM1543C now (they hand-roll config space; unifying them is a later, separate
pass -- do-no-harm).

### 10.1 Files

FILE 1 (NEW) deviceLib/Tsunami/AtaTaskfileEngine.h
  Lift the current Cy82C693Ide I/O-port body VERBATIM (behavior-preserving
  move): IIoPortHandler over 0x1F0/0x170/0x3F6/0x376; Channel[2]; Disk[2] + CD;
  loadSignature; cmdRead/cmdWrite; PIO stream; ATAPI CDB path; attachMedia /
  attachDevice; status/error/selectedUnit accessors. NO behavior change -- this
  is the DS20 regression anchor.

FILE 2 (NEW) deviceLib/Tsunami/PciConfigSpace.h
  The reusable mechanics from 10.0. Constructed/initialized from {reset byte
  map, per-byte RW mask}. read(reg,width) / write(reg,val,width). BAR alignment
  + status-W1C handled here from the mask, not per-controller.

FILE 3 (NEW) deviceLib/Tsunami/AliM5229Ide.h
  IPciDeviceHandler + IIoPortHandler. Members: AtaTaskfileEngine m_eng;
  PciConfigSpace m_cfg. initConfig -> the section-3 M5229 map (VID 0x10B9, DID
  0x5229, RID 0xC1, class 0x0101, prog-IF 0xFA, BAR defaults 1F1/3F5/171/375/
  F001, IP 0x01) + the datasheet R/W mask. pciConfigRead/Write delegate to
  m_cfg; the four I/O-port ranges forward to m_eng; attachMedia/attachDevice
  forward to m_eng. ADR-0001 header; include guard DEVICELIB_TSUNAMI_ALIM5229IDE_H.

FILE 4 (REFACTOR) deviceLib/Tsunami/Cy82C693Ide.h
  Becomes thin, SAME public API (tests depend on it): composes AtaTaskfileEngine
  m_eng + PciConfigSpace m_cfg. The Cypress map + mask MUST reproduce today's
  exact reset bytes AND today's write behavior (RO 0x00-03 / 0x08-0B / 0x0E; all
  else raw-writable incl. unmasked BARs) so DS10/DS20 stay byte-identical. The
  engine body relocates to FILE 1.

FILE 5 (WIRING) chipsetLib/TsunamiChipset.h
  a. Add member AliM5229Ide m_aliIde (beside Cy82C693Ide m_ide).
  b. Active-IDE seam: raw ptr AtaTaskfileEngine* m_activeIde (or accessor
     activeIde()). setDiskMedia/setCdMedia (TsunamiChipset.h:125-129) route to
     m_activeIde instead of m_ide directly.
  c. wireDevices() (~672): if isAliPlatform(m_model) -> registerPciDevice(0,5,1,
     &m_aliIde) + route the four port ranges to m_aliIde; m_activeIde =
     &m_aliIde.engine(); attachDevice(0,1,&m_cdrom) on the active engine. Else
     the Cypress path exactly as today; m_activeIde = &m_ide.engine().
  d. ADR-0001 header + inline comments.

### 10.2 Do-no-harm + tests (delta from section 7)

  - DS10/DS20 wire Cy82C693Ide (unchanged reset bytes + relocated-but-unchanged
    engine). Regression gate unchanged: DS20 enumerates both units; IDE-TRACE
    reg0x00 = 0xC6931080.
  - Existing tests stay green UNCHANGED (public APIs preserved):
    tests/deviceLib/test_cy82c693ide.cpp, tests/chipsetLib/test_ide_wiring.cpp.
  - ADD (CHECK-only per V4 doctest rule): a PciConfigSpace unit test (RO reject /
    BAR alignment / status W1C) and an AliM5229Ide identity test (reg0x00 =
    0x522910B9; reg0x08 prog-IF byte = 0xFA; BAR reset defaults).

### 10.3 Still-open (gate the M5229 mask + scope; from section 8)

  - Q3 class-code R/W: the M5229 R/W mask's bytes 0x09-0x0B are the decision
    point (datasheet marks CC R/W; safety argues RO). Pending.
  - Q4 dqa1-CD-on-ES40 scope: fold in or separate. Pending.

## 11. Phased plan -- lever convergence FIRST (architecturally-honest order)

Decision (Tim, 2026-07-12): establish the model-bifurcation lever BEFORE landing
AliM5229Ide, so the controller hangs off a single honest selector. Order:
PHASE 1 = converge the south-bridge lever; PHASE 2 = the B-comp M5229 controller
(section 10), gated on the converged lever + Q3/Q4.

### 11.0 Live-tree reality (verified 2026-07-12) -- two surprises

1. TWO INCONSISTENT LEVERS that actively disagree:
   - Chipset: `TsunamiChipset::isAliPlatform(model)` (TsunamiChipset.h:631) =
     string match {ES40,ES45,DS25} -> wires the ALi bridge. Model-string based.
   - Caps: `PlatCap::SbAli` (PlatformCapabilities.h:80) is derived from the
     MANIFEST ISA-bridge modelName containing "ali" (derive() :132-137).
   BUT every manifest -- es40/es45/ds25 included -- declares `cypress_isa` /
   `cypress_ide`. So SbAli is NEVER set on any platform today; the chipset shows
   "Acer Labs M1543C" only because the string-lever wins. The manifest lies.

2. ORDERING: wireDevices() runs INSIDE the chipset ctor (TsunamiChipset.h:106),
   BEFORE Machine derives m_caps (Machine.cpp:550). The chipset receives only the
   model string + cpuCount + memSize (Machine.cpp:353) -- NOT the manifest or
   caps. So wireDevices() cannot read SbAli today; the lever must be PASSED IN.

### 11.1 Phase 1 -- converge the lever (behavior-preserving for the bridge)

Goal: ONE manifest-sourced south-bridge selector drives BOTH the bridge and (in
Phase 2) the IDE; retire isAliPlatform. Net bridge wiring is UNCHANGED
(ES40/ES45/DS25 -> ALi; DS10/DS20 -> Cypress), but now sourced honestly.

FILE 1 (NEW rule, shared) systemLib/PlatformCapabilities.h
  Factor the manifest -> south-bridge rule into one free function, e.g.
    enum class SouthBridge : uint8_t { Cypress, AliM1543C };   // (chipsetLib-owned; see note)
    SouthBridge southBridgeFromManifest(DeviceManifest const&) noexcept;
  derive() calls it to set SbAli/SbCypress (single source for the rule).
  LAYERING NOTE: put the SouthBridge enum in chipsetLib (near TsunamiVariant) so
  the chipset does not depend up into systemLib; Machine maps manifest ->
  SouthBridge and passes it down.

FILE 2 (MANIFESTS -- faithfulness fix) es40/es45/ds25 _v7_3_platform.json
  Declare the REAL ALi south bridge so the honest lever resolves ALi:
    func0: name/model "ali_isa" (or "ali_m1543c"), class 0x060100 (unchanged).
    func1: name/model "ali_ide" (or "ali_m5229"), class 0x010100 (unchanged).
  DS10/DS20 stay cypress_isa/cypress_ide. This is the SSOT correction that makes
  the manifest match the silicon (real ES40/ES45/DS25 = ALi M1543C).
  (There are Debug/Release/RelWithDebInfo copies of each manifest; the run-dir
  copy is authoritative per run -- fix the source + let the build copy, and
  verify the RelWithDebInfo copy for the test runs.)

FILE 3 (CTOR) chipsetLib/TsunamiChipset.h
  Add a SouthBridge param to BOTH ctors, DEFAULT SouthBridge::Cypress (so the
  variant ctor used by doctest stays Cypress = byte-identical). Store m_southBridge.
  Replace isAliPlatform(m_model) at wireDevices() :639 with
  `if (m_southBridge == SouthBridge::AliM1543C)`. DELETE isAliPlatform.

FILE 4 (WIRING) systemLib/Machine.cpp
  Compute SouthBridge from the manifest (southBridgeFromManifest) and pass it to
  the TsunamiChipset ctor (:353). Requires the manifest be available at chipset-
  construction time -- it is (mr.manifest); confirm member-init order (m_settings/
  manifest before m_chipset) or hoist the computation to a helper evaluated in the
  init list.

### 11.2 Phase 2 -- AliM5229Ide on the converged lever

Exactly section 10 (B-comp: AtaTaskfileEngine + PciConfigSpace + controllers),
except the wireDevices() gate is now `m_southBridge == AliM1543C` (the same lever
Phase 1 established for the bridge) -- so bridge + IDE are selected by ONE honest
switch. Gated additionally on Q3 (class-code mask) + Q4 (dqa1 scope), still open.

### 11.3 Do-no-harm + tests

- Bridge wiring result UNCHANGED after Phase 1 (ES40/ES45/DS25 -> ALi via the
  corrected manifests; DS10/DS20 -> Cypress). Gate: DS20 P00 + both dqa units;
  ES40 still shows "Acer Labs M1543C" at 5/0. doctest: variant ctor defaults
  SouthBridge::Cypress -> unchanged.
- Scales to Titan: DS15/DS25/ES45 (Titan chipset) that use the ALi south bridge
  set SouthBridge::AliM1543C from their manifest -> reuse AliM5229Ide with ZERO
  new bifurcation code. The south-bridge axis is orthogonal to the chipset axis.
