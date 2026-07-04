<!--
EmulatR V4 -- Platform Identity SSOT: ini (launcher) -> platform.json (hardware)
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-07-03
Purpose: capture the architect's vision + the audited current state for
resolving the ini/platform.json split, so the platform.json becomes the
single source of truth (SSOT) for controller + device inventory while the
ini stays the thin launcher/model-selector.  Design/decision artifact, NOT
generated code.  Discuss-before-code.  ASCII(128) only.
-->

# Platform Identity SSOT -- ini (launcher) -> platform.json (hardware inventory)

## 1. The vision (architect, 2026-07-03)

    EmulatR starts
      -> looks for EmulatrV4.ini
      -> establishes the MODEL reference (e.g. ES40)  [+ runtime knobs]
      -> model looks up <model> platform.json
      -> platform.json is the SSOT for CONTROLLER + DEVICE inventory
         (chipset variant, southbridge, hose count, IIC, PCI, storage)

Division of responsibility:
- EmulatrV4.ini  = the LAUNCHER.  Thin.  Owns: which machine (model), how
  much RAM (memorySize), and pure run-time knobs (trace, logging, console,
  snapshot, storage dir).  It does NOT enumerate hardware.
- <model> platform.json = the HARDWARE SSOT.  Owns everything that "latches
  to EmulatR": the chipset (21272 variant), the southbridge type, the hose/
  Pchip count, the memory ceiling/tiling profile, and the enumerated device
  inventory (IIC FRUs, PCI functions, storage).  The .json is retained (not
  merged into the ini) precisely because hosting/loading an enumerated
  device list is what it is good at.

The model string is the KEY that binds the two: ini names the model, the
model selects exactly one platform.json, and that json answers "what am I
made of."

## 2. Current state (audited 2026-07-03) -- why this is needed

The tree today does NOT implement the vision; identity is a loosely-coupled
THREE-way split with a live incoherence:

- Chipset variant  <- ini [System] model  (variantFromModel(); Machine.cpp
  :353, resolved at member-init).  NOT from the json.
- Firmware ROM     <- ini [ROM] firmwareImage  (a SEPARATE explicit field,
  not derived from model).
- Manifest/devices <- the FIRMWARE STEM  (Machine.cpp:463, fw.stem() +
  "_platform.json"), NOT the model.
- Coherence        <- WARN ONLY (Machine.cpp:516: manifest.platform !=
  iniModel logs a warning, no hard-stop).

Consequences found:
- LIVE MISMATCH in the committed EmulatrV4.ini: model=ES40 but
  firmwareImage=ds20_v7_3.exe.  A bare `./Emulatr` yields ES40 chipset +
  DS20 ROM + DS20 device manifest -- three-way incoherent, warn-only.
  Masked in practice only because run_es40_srm_trace_full.sh passes
  --firmware es40_v7_3.exe.
- The ini comment claims "model is the master switch: it selects
  <lower(model)>_platform.json and the default firmware" -- ASPIRATIONAL;
  the code derives the manifest from the firmware stem (versioned) and
  takes the firmware from an independent ini field.
- ORDERING GAP: m_chipset is a member-initializer (Machine.cpp:353) built
  from the ini model BEFORE PlatformConfig::load runs (Machine.cpp:470).
  The json physically cannot drive the chipset today -- it is downstream of
  chipset construction and feeds only devices (m_chipset.iic()
  .configureDevices, :493).
- Manifests carry devices but NOT controller keys: ds10/ds20/es40
  platform.json top-level = {manifest_version, platform, comment,
  iic_devices, pci_devices}.  No chipset/variant/memMax/hoses/cscStrap.
- Residual model/variant coupling in code (to be deleted by the SSOT):
  TsunamiChipset.cpp:42 m_model = (variant==Typhoon ? "ES45" : "ES40")
  [stale "Typhoon==ES45" error; m_model is identity-bearing -- drives
  isAliPlatform() + IIC base]; :51 banner "Typhoon 21274" [wrong: 21272].

## 3. The two design options

Option A -- MERGE ini + json into one file.
  Rejected by the vision.  The json's strength is hosting an enumerated
  device list; folding it into the ini bloats the launcher and loses the
  per-model hardware file.  Also breaks the "one thin launcher, many model
  manifests" shape.

Option B -- json as HARDWARE SSOT, ini as thin launcher.  [CHOSEN]
  ini owns model + size + runtime knobs; json owns controllers + devices.
  model is the binding key.  This is the vision in Section 1.

## 4. Target design (Option B)

### 4.1 platform.json schema additions (controllers)
Add controller keys alongside the existing device arrays:

    "chipset":     "typhoon",          // 21272 variant selector (binding)
    "southbridge": "ali_m1543",        // device-type selection (real ES40)
    "hoses":       2,                   // Pchip/hose count
    "firmware":    "es40_v7_3.exe"      // the ROM this platform boots

Capability CONSTANTS stay COMPILED (kTsunamiInfo/kTyphoonInfo/kTitanInfo:
array sizes, memMax, DREV, CREV, ASIZ ceiling).  The json declares WHICH
chipset (selection = config); the compiled table declares WHAT it can do
(hardware fact).  Rationale: a hardware fact must not be a user-editable
JSON number (no "Tsunami with 8GB arrays" footgun).  Split along the
config-vs-hardware-fact line; no duplication.

### 4.2 Consumption / construction order (the real refactor)
1. Load ini -> model + runtime knobs.
2. model -> select platform.json (deterministic; find-or-fail if absent).
3. Load platform.json FIRST (before any controller construction).
4. One factory reads the json: chipset (-> compiled capability lookup) +
   southbridge + hoses + device inventory -> constructs the controller set.
5. json.firmware selects the ROM (unless CLI --firmware overrides, still
   subject to the coherence check).

This inverts today's order: m_chipset must stop being an ini-model member-
initializer.  Make it lazily constructed (std::optional / unique_ptr in the
ctor body) after the json loads, OR load the json in main.cpp and pass the
resolved platform into the Machine constructor.

### 4.3 Hard-stops (charter: no silent degradation; supersede warn-only)
- model has no platform.json           -> FAIL (claimed model, no manifest).
- json.chipset unknown                 -> FAIL.
- memorySize > compiled cap(chipset)   -> FAIL (name model+cap+requested).
- memorySize not tileable to legal ASIZ arrays under the chipset -> FAIL
  (do not round; e.g. 6GB on Typhoon floors to 4GB today -- reject instead).
- firmware personality != model/json   -> FAIL or loud, not the current warn.

## 5. Immediate, low-risk items (can land before the refactor)
- Fix the committed EmulatrV4.ini mismatch: firmwareImage ES40 path (or
  document why the default is DS20).  Removes the live three-way incoherence.
- Fix TsunamiChipset.cpp:42 (variant->model mismodel) and :51 ("21274" ->
  "21272").  Correctness cleanup; independent of the refactor; my ES40->
  Typhoon flip makes :42 semantically wrong if that ctor is ever used.

## 6. Sequencing
- The Section 4 refactor (json SSOT + ordering inversion) is a real change.
  SEQUENCE AFTER the Windows PC confirms the ES40 ROM actually decodes the
  Typhoon 8GB ASIZ / DREV 0x20 (the open empirical question).  No point
  building the data-driven gate for a capacity not yet proven viable.
- Section 5 items are cheap correctness and may land now.
- Ties in: the ES40 Typhoon variant work (experiment/es40-typhoon-32gb,
  commit decd3cb) is the capability half already in place; this journal is
  the binding/consumption half.  The DS20 264DP badge defect shares the
  same seam (CSC strap belongs in the json controller keys).

## 7. Open confirms
- C1 firmware selection: should model auto-select <model>_v7_3.exe (vision),
  or does json.firmware own it, with ini [ROM] as override only?
- C2 manifest naming: firmware-stem-versioned (es40_v7_3_platform.json,
  today) vs model-keyed (es40_platform.json, the ini comment's claim).
  Pick one; the resolver's lookup depends on it.
- C3 who owns cpuCount / memorySize ceiling: ini authors the request; json/
  compiled table authors the max.  Confirm the request-vs-max split.
