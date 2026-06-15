# HMDocs - Authoring Conventions & Coverage Tracker

Working copy of the EmulatR Help & Manual project (clean rebuild).
Project file: `claudeRV4.hmxp`. Tim owns the TOC/structure (built in H&M);
Claude fills topic content.

## Authoring conventions (from Example-styles_ and Tim's direction)
- Styleclasses: body = `Normal`; headings = `Heading1` / `Heading2` / `Heading3`;
  inline code & console tokens = `<text styleclass="Code Example">...</text>`.
- Cross-links (See Also): `<para styleclass="Normal"><text styleclass="SeeAlso">See Also.</text></para>`
  followed by one `<para styleclass="Normal"><link displaytype="text" defaultstyle="true"
  type="topiclink" href="<filename-no-ext>">Display Text</link></para>` per link.
- ASCII-only in content (no smart quotes / em-dashes; use `--`). Escape `<` `>` `&` `"`.
- Clean page `<title>` + matching `Heading1` (drop the build-sheet "- " / "(NEW)" / "(moved)" text).
- `<keywords>`: index EVERY noun on the page -- chip names, identifiers, file names,
  product names, key terms -- one `<keyword>` each, alphabetical.
- No visible "Moniker:" line in the body.

## Structure decisions (resolved)
- Nav captions: CLEANED by Claude in the TOC (75 captions stripped of "- " /
  "(NEW)" / "(moved...)" / trailing "/"; real parens like "(CD001)" preserved).
- Combined pages: keep closely-related sets combined; SPLIT the SRM commands
  (show config/device/memory, boot, set) into individual pages when authoring 4.3.

## Coverage (new clean structure)
Status: [x] done  [~] in progress  [ ] empty skeleton

Section 1 - Introduction  -- COMPLETE
- [x] 1. Introduction (landing, See Also -> children)
- [x] What is EmulatR
- [x] Supported Systems
- [x] Current Platform Support
- [x] Feature Matrix
- [x] Current Development Status

Section 2 - Installation  -- COMPLETE
- [x] 2. Installation (landing)
- [x] Required Tools & Frameworks
- [x] Building from Source
- [x] Firmware Requirements
- [x] First Startup

Section 3 - Quick Start  -- COMPLETE
- [x] 3. Quick Start (landing)
- [x] Launching EmulatR
- [x] Connecting to the Console
- [x] Loading Firmware
- [x] Reaching the SRM Prompt
- [x] Clean Shutdown

Section 5 - Architecture & Internals (partial)
- [x] Overview  (REWRITTEN from verified V4 sources via an Explore pass -- the
      Claude-web draft was substantially V1/inaccurate)
- [x] Memory [x] Addressing [x] Firmware Decode & Decompression [x] Execution Flight Plan
==> SECTION 5 (Architecture & Internals) COMPLETE. All from verified V4 sources
    (GuestMemory/AAR/LockMonitor; Ev6Translator/SPAM/TlbEpoch; DEC inflate +
    host_decompressor oracle; the 5-stage cold-boot flight plan).

V4 CORRECTIONS applied to Overview (authoritative, from the code):
- GuestMemory is the sole memory interface; SafeMemory REMOVED. Sparse 64 KiB
  paging via VirtualAlloc (Windows) / mmap (Linux/ARM). MMIO + faults handled by
  the TsunamiChipset arbiter, not by memory.
- TLB = SPAM (Set-Partitioned Associative Memory) via SPAMShardManager itbMgr/dtbMgr
  in CpuState (16x8 = 128 ea) + Ev6Translator + epoch invalidation. NO Ev6SiliconTLB.
- NO FaultDispatcher: faults are faultCode on BoxResult, delivered at WB via
  Ev6EntryVectors (0x100-0x780).
- Pipeline = PipelineDriver, six stages IF/DE/GR/EX/MEM/WB (not IF/ID/IS/EX/MEM/WB).
- Southbridge = Cypress CY82C693 (Cypress_CY82C693ISABridge), NOT AliM1543C.
- NO "five-stratum PCI" model; standard bus/slot/function.
- Authoritative sources = Alpha ARM + DEC SRM/PALcode sources; AXPBox is a
  NON-authoritative cross-check only.

GOALS/OBJECTIVES: the Overview carries the 5 primary goals + fidelity standard.
If a "Goals & Objectives" topic is added under Section 1, lift that material there.

Section 4 - Operating EmulatR (in progress)
- [x] 4. Operating EmulatR (landing)
- 4.1 Configuring & Running -- COMPLETE: [x] landing [x] Configuring [x] Executing
      [x] Executing Two Instances [x] Example - Performance
- 4.2 Console Connectivity -- COMPLETE: [x] landing [x] Console Support
      [x] TCP Console Overview [x] Terminal Clients (combined)
- 4.3 SRM Console -- COMPLETE (commands SPLIT): [x] landing [x] SRM Overview
      [x] Common Commands [x] SRM Environment Variables
      [x] show config [x] show device [x] show memory [x] boot [x] set
      TOC rewired (combined node -> 5 nodes, ids 470000000000001-005); combined
      page file deleted; Common Commands See Also re-pointed. VERIFY ON H&M RELOAD.
- 4.4 Storage -- COMPLETE: [x] landing [x] Create Guest Disk Storage
      [x] Virtual Disk Images [x] Virtual CDROM [x] IDE Devices [x] ATAPI Devices
      [x] ISO-9660 PFD [x] SCSI Devices [x] Storage Interconnect and Fabric
      [x] SDI/DSSI/ST506 (combined)
- 4.5 Snapshots -- COMPLETE: [x] landing [x] Snapshot Operations (combined)
- 4.6 LFU -- COMPLETE: [x] landing [x] LFU Commands (combined)
- 4.7 Guest Boot -- COMPLETE: [x] Guest Boot page
- 4.8 Logging & Diagnostics -- COMPLETE: [x] landing [x] Diagnostics & Debugging (NEW)
      [x] SRM Performance Logging
==> SECTION 4 (Operating EmulatR) COMPLETE (all 8 subsections).

Section 7 (Troubleshooting) -- COMPLETE: [x] landing [x] EmulatR will not start
  [x] Firmware Hangs / Slow Boot / Missing Devices (combined) [x] Console Connection
  Issues [x] Halt Button is IN, BOOT NOT POSSIBLE [x] Example (Console) execution
  [x] evaluate Address.

Section 6 (Reference) -- COMPLETE: [x] landing [x] Environment Variables (from
  ENV_VARS.md: gotchas + category tables + compile-time flags + CLI mirrors)
  [x] Glossary (~28 terms) [x] Dependencies [x] Attributions & Credits.

## DISCREPANCIES to reconcile (newer 2026-06-13 boot log vs earlier pages)
A fresh PuTTY capture shows a newer build; pages written from the older transcript
now disagree:
- MEMORY: log = 1024 MB; pages say 64 MB -> Supported Systems, show memory,
  Reaching the SRM Prompt, Example - Performance.
- CPU CLOCK: log = DS10 268 MHz; pages say 266 MHz -> Supported Systems,
  show config, Example - Performance.
- CD DEVICE: log = dqa1.1.0.105.0 (IDE primary SLAVE) is the CD, dqa0 = ATA disk
  (matches Guest Boot). Pages calling dqa0 the CDROM are stale -> show device,
  ATAPI Devices, Virtual CDROM, Example - Performance.
- GCT/FRU addr: log 3ff32000 vs pages 3f32000.
ACTION: confirm current defaults, then sweep these pages.

Remaining: rest of Section 4, rest of Section 5, Sections 6-8.
