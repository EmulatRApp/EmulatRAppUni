<!--
EmulatR -- Development Trajectory & Recommendations (session synthesis).
Placed in V5 as the forward charter.  Captures the 2026-07 session: the
resolved ES40-boot roots, the "faithful Oracle" thesis, the freeze gate, the
phased plan (finish/freeze Oracle -> golden-trace -> OS bring-up ->
optimization), and the forward open-items ledger.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience -- the load-bearing principle throughout.
Version note: V1..V5 are INTERNAL development-state placeholders; the PUBLIC
version advances by decimal increment (e.g. v1.4.0 -> v1.4.1 per state).
-->

# EmulatR -- Trajectory & Recommendations (V5 charter, 2026-07-11)

## 0. Where the project stands

Lineage: V1 (6-stage faithful design) -> V2 (POC) -> V3 (merge of V1 + POC) ->
V4 (booting).  V4 now boots the ES40 to the SRM prompt `P00>>>`, executing
faithfully, consistently, and DETERMINISTICALLY across boots.  DS10/DS20 reach
`>>>`/`P00>>>` as well.  V4 is to be frozen as the reference; V5 is the active
forward tree.

Console evidence (ES40, this session): "Memory size 4096 MB" -> memtest ->
four 1024 MB arrays at 0x0/0x40000000/0x80000000/0xc0000000, 4-way interleave,
GCT/FRU at 0x3fc30000, "AlphaServer ES40 Console V7.3-2", `P00>>>`.

## 1. The Oracle thesis (the strategic frame for V4->V5)

V4's characterization: a CONSTRUCTION-BUILD OF A FAITHFUL ORACLE to SRM `>>>`.

The Oracle's value is FIDELITY, not speed.  It is useful only if it is RIGHT.
AXPBox is used as an oracle but takes shortcuts (NOPs the memory test, reads
size from a CSR to skip probing, etc.) -- it is not a faithful execution of the
platform.  EmulatR's differentiator is faithful execution: the moment it takes
those shortcuts it stops being an oracle and becomes another approximation.

Consequence for sequencing: an oracle's authority comes from being FROZEN and
VALIDATED.  Only a frozen Oracle can adjudicate downstream changes (an
optimization, a storage datapath) by divergence-or-not against a known-good
reference.  Therefore the Oracle must be completed and frozen BEFORE either the
execution-model change or the OS-boot campaign -- not braided into finishing it.

## 2. Resolved this session (the record)

Root causes CONFIRMED (machine-code / arithmetic dispositive, not inference):

- AAR ASIZ decode-width mismatch.  pc264 SRM decodes 3-bit ASIZ ((AAR>>12)&7);
  EmulatR wrote the extended 4-bit code (AAR0=0x9009, ASIZ=0x9=4GB), which
  pc264 read as 0x1 = 16 MB -> firmware mis-sized memory -> the whole downstream
  cascade.  Fixed by the tiling function (below).
- CSERVE 0x66 = get PAL base, 2 MiB-aligned: R0 = cpu.palBase >> 21 << 21.
  NOT get_time (that label, inherited from earlier journals, was wrong).
  Disassembly proved get_time(0x8C2D0) = `arg - cserve(0x66)`; the no-op left a
  stale R0 -> wild VA -> ACV.  Returning palBase-aligned UNIFIES three symptoms:
  the memtest ACV, the 2026-07-08 SCB-base regression, and (to confirm) the SCB
  null-dispatch at ~0x1038600 (the BCD-TOY value 0x01010000+0x28000=0x1038000
  matches the recorded wrong-SCB base).

Design decisions established:

- Tiling function: tile(memSize, bankCount=4, asizWidth, baseWidth) -> 4 equal
  power-of-2 banks | NotRepresentable.  Supported totals restricted to powers of
  two (POLICY, not spec: HRM forbids non-power-of-2 ARRAYS, not a 12 GiB total).
  pc264 (3-bit) tops out at 4x1 GB = 4 GiB; 8/16/32 GiB are a faithful, LOUD
  NotRepresentable.  4-bit-decoding firmware reaches 4x8 GB = 32 GiB.
- isExtendedAar is a FIRMWARE property (the loaded console's ASIZ decode width),
  NOT a platform property.  Derive it from firmware identity; gate the extended
  path on it; do not delete it (future 4-bit configs need it).
- ECC is SILICON (Tsunami/Typhoon memory data path), performed on every access;
  firmware only configures / initializes (the fill loop) / stresses / reads
  status.  ECC was diagnostic context that ruled out a wrong hypothesis; it is
  NOT part of any fix and EmulatR need not model it.

Retired leads (the arc, so no session re-opens them): VPTB propagation ->
(-1)<<42/<<32 shift-slip -> OR-merge base|PA -> missing-0x3ff kseg base ->
CSERVE-incidental -> memory-size math -> AAR-ASIZ (real) -> CSERVE 0x66=palBase
(real).  Lesson: MACHINE CODE was dispositive where value-only traces were
ambiguous; several pivots came from reading values without the mechanism.

Re-audit (Tsunami/Typhoon 21272) opened and Tier 0 landed:
- T0-1 delete the dead "Arbiter Gatekeeper" decoder (a closed 10-function
  subgraph + IPciMemoryHandler + null m_pciMemory; latent nullptr + a garbage
  token proving it never ran); live decode path untouched.
- T0-2 MTR honor writes (storage; Type=RW).  T0-3 delete dead DREV constants.
  T0-4 comment corrections (RO->RW with field notes).
- Relocated: CSC/MPD write-honor -> Tier 1 (field-aware: CSC has RO-from-pins
  fields; MPD is the I2C SPD bit-bang) so a blanket store cannot clobber.
  Clock 2^28 experiment -> #24 (timebase), not a hygiene revert.

## 3. The freeze gate: "Oracle-complete" criteria

V4 is declared Oracle-complete (and frozen) when ALL hold:

  a. DS10, DS20, ES40 (and ES45 when its chipset lands) reach a CLEAN `>>>` --
     no residual console errors that mask missing fidelity.
  b. The Tsunami/Typhoon re-audit is closed to "audit-safe complete": Tier 0 +
     Tier 1 landed; Tier 2 either implemented-with-consumer or recorded as
     consumer-gated-by-design with the HRM contract captured (not a silent gap).
  c. The three ES40 console errors are resolved: DPR/TIGbus config store (clears
     "Flash SROM invalid" + "TIG load failure" + feeds show config), and the
     southbridge IDENTITY (ES40 = ALi M1543C, not the Cypress 82C693 currently
     reported).
  d. Storage/IO is brought up ONLY to console fidelity: device PRESENCE and
     PROBE (show device / show config), NOT OS-driven datapaths.
  e. Boot is deterministic, and GOLDEN TRACES are captured for each platform as
     the Oracle's validation artifact (see 5).

Explicitly OUT of the freeze gate (deferred to Phase B): OS-driven SCSI/Fibre
datapaths, DMA engines driven by OS drivers, network, actual `boot dqa0` into an
OS.  Those are exercised by an operating system, not by reaching the prompt.

## 4. The question answered: attach storage/network and boot an OS?

YES -- as a deliberately-declared NEXT phase, AFTER the freeze, not now.

Why yes: an OS boot is the ultimate fidelity test and the ONLY way to complete
the platform's fidelity story.  The SRM exercises a narrow slice; an OS drives
the Tier-2 consumer-blocked surfaces (real HBA datapaths, Pchip DMA, second
hose, interrupt-under-load, the southbridge as a driver sees it).  An OS is the
CONSUMER those deferred items were waiting for.

Why after the freeze: an OS boot is a test, and a test is meaningful only
against a FIXED reference.  Attaching HBAs/DMA while the audit is still landing
and the console still errors reproduces the multi-variable trap that cost weeks
on the memtest ACV -- a hang could be the new datapath, an incomplete register,
or interrupt delivery, all moving at once.  Freeze first; then every OS-boot
failure is a NEW divergence traceable against a known-clean substrate.

Ordering WITHIN the OS phase (it is three things, not one):
- Storage first, minimally: the ATAPI CD path only (dqa0, PACKET ->
  READ(10)/READ CD from the ISO) to load the bootstrap, then the install-target
  disk.  This is the critical path.
- Fibre HBA is NOT on the boot path -- defer until an installed OS wants it.
- Network is NOT needed to boot -- defer to OS-runtime; add where it is a
  bounded addition, not a speculative one.
So the minimal boot campaign = ATAPI CD datapath + the DMA/interrupt/Pchip
plumbing it rides on.  Nothing else.

## 5. Development trajectory (phased)

PHASE A -- Finish & Freeze the Oracle (current work).
  A1. Tsunami/Typhoon re-audit Tier 1: T-TOPO topology SSOT first (the latched
      source for all presence bits), then CSC field-aware RW, then STR->CSC
      sync / DSC P1P<6> / AAR SA<8> / MISC / IIC(=Interval-Ignore-Count) / PRBEN.
  A2. DPR/TIGbus dual-port RAM (@0x801_1000_0000, task #25) + config/FRU layout
      the SRM expects -> clears the SROM/TIG console errors and feeds show config.
      Model faithfully to the SRM's expected image; do not back-fill bytes to
      silence the check.
  A3. Southbridge identity: ES40 -> ALi M1543C (resolve ALi-vs-Cypress).
  A4. Storage/IO to console fidelity: dqa0 presence + probe (not datapath).
  A5. FREEZE V4 at clean `>>>`.  Capture GOLDEN DETERMINISTIC TRACES for all
      platforms -- the Oracle's deliverable and the diff baseline for everything
      downstream.  (We already do this ad hoc; formalize it.)

PHASE B -- Use the Oracle: OS bring-up (new, explicitly-scoped phase).
  B1. ATAPI CD boot path end-to-end (dqa0 PACKET -> SCSI READ from ISO) + the
      Pchip DMA + interrupt delivery it requires (the Tier-2 consumers).
  B2. `boot dqa0` into OpenVMS / Tru64; debug hangs by GOLDEN-TRACE DIFF against
      the Oracle, not by stubbing-until-it-boots.
  B3. Install-target disk (ATA-HD).  THEN Fibre HBA, THEN network -- each as its
      own ticket when an installed OS demands it.

PHASE C -- Optimize (Oracle-driven, measured, not a bet).
  C1. PROFILE FIRST: measure the fraction of cycles in the interpreter hot loop
      vs. PALcode/TB-miss/MMIO/device.  This single measurement decides the
      achievable tier and collapses the estimate spread.
  C2. Land the CACHING work (VA-translation cache, PA-keyed decoded-instruction
      cache, collapsed non-fault EX/MEM/WB): ~5-20x, tens-to-low-hundreds of MHz,
      NOT a rewrite, and prerequisite infrastructure a JIT needs anyway.
  C3. THEN decide the execution-model change (JIT or other) as a measured call
      driven by the Oracle + profile -- may or may not be a JIT; may or may not
      be faithful to the current engineering design.

## 6. Recommendations (crisp)

1. FREEZE before optimize AND before OS-boot.  The Oracle must be a fixed,
   validated reference before it can adjudicate anything.
2. GOLDEN-TRACE capture is the Oracle's key deliverable -- the connective tissue
   between "build the Oracle" and "use the Oracle."
3. STORAGE split: console-fidelity now (presence/probe); OS datapath in Phase B
   (ATAPI CD first); Fibre + network deferred to when an installed OS wants them.
4. PERFORMANCE: profile -> caching -> measured JIT decision.  A JIT is a
   PORTABLE win (both shippable x86_64 targets, Windows + Mac Intel) and is the
   DOMINANT term; host ISA is minor (~1.3-2x).  Rough projection from ~9 MHz:
   caching ~5-20x; a mature block JIT another ~5-15x (hundreds of MHz to low GHz,
   potentially at/above the real 21264's 500-1250 MHz) -- BUT realized speedup
   depends on the compute-vs-trap profile, and a JIT is in TENSION with fidelity
   (it must reproduce every side effect/fault/ordering; bugs bury in generated
   host code).  Keep the interpreter as the correctness oracle even under a JIT.
5. HARDWARE: do NOT buy Apple Silicon on an "ISA similar to Alpha" premise --
   that premise is false (M-series is ARM/AArch64; Alpha is its own dead ISA;
   no reusable overlap).  Apple Silicon helps single-thread interpreters on RAW
   speed, but only via a NATIVE arm64 build (an x86_64 build runs under Rosetta 2
   and forfeits the benefit).  A native arm64 EmulatR is a worthwhile future
   target (and a 3rd packaging target, macos-arm64) -- a porting project that
   would JUSTIFY the hardware, not the other way around.  Do the free caching
   multiplier on existing hardware first.
6. PACKAGING (done this session): tools/build_kit.py -- multi-target
   (windows-x86_64 zip, macos-x86_64 tar.gz), one manifest, FIRMWARE
   REFERENCED-NOT-BUNDLED (README matrix links the ZX/HP archive; HP/HPE
   copyright), forbidden-dev-path guardrail (fails loudly, text + UTF-16LE
   binary), exec-bit preserved inside the .app bundle, SHA-256 manifest,
   --stage-only for Setup Factory.  README must carry the macOS Gatekeeper
   (sign/notarize or xattr quarantine removal) and Rosetta-2 (Intel-on-Apple-
   Silicon) notes.
7. CONTROLLER-SURFACE COMPLETENESS: the same audit-then-faithful-gap-coverage
   pass applies to EVERY controller EmulatR presents -- 16550 UART, the
   southbridge functions, the Super-I/O boundary, the Pchip -- each against its
   own authoritative spec.  The T/T re-audit is the template.
8. DISCIPLINE CARRIES FORWARD, and matters MORE in Phase B: golden-trace-and-
   diff, consumer-gated ("do not implement blind"), and faithful-over-expedient.
   The temptation to stub-until-it-boots (the AXPBox path) is strongest exactly
   when an OS hangs and you cannot see why; the Oracle is what lets you resist it.

## 7. Forward open-items ledger

ES40 console (Phase A):
  - DPR/TIGbus config store @0x801_1000_0000 (task #25) -- SROM/TIG errors + show config.
  - Southbridge identity ALi M1543C vs Cypress 82C693.
  - system serial number not set (cosmetic; set sys_serial_num).

Tsunami/Typhoon re-audit (Phase A):
  - Tier 1: T-TOPO SSOT -> CSC field-aware -> STR/DSC/AAR/MISC/IIC/PRBEN.
  - Tier 2 (consumer-gated): real Pchip1 second hose + presence bits; Pchip DMA
    translation engine; PERROR error capture; Type-1 config + IDSEL + BAR decode;
    full Dchip 8-way byte-slice WRITE path.  Classify each as inert-truthful vs
    storage-masking; correct the storage-masking ones or build with the consumer.

Devices / OS (Phase B):
  - IDE/ATAPI full-spec sufficiency -- UNVERIFIED until a real boot dqa0 drives
    the PACKET/READ path; controller identity (ALi vs Cypress) must be right first.
  - ATA-HD install target; then Fibre HBA; then network -- each its own ticket.

Chipset generations:
  - ES45 / 32 GiB requires the TITAN (21274) chipset MODEL + ES45 firmware
    TOGETHER -- not a firmware swap onto the T/T model (Titan has different
    Cchip/Dchip/Pchip semantics and wider ASIZ/base decode).  Deferred until the
    T/T surface is audit-safe complete.  Titan HRM (Titan_Chipset EK-ES450-SV) on
    hand.

Timebase:
  - #24: clock 2^28 experiment -> per-model faithful values (ES40 ~1144 Hz, ES45
    ~953.7 Hz) reconciled against HRM 1024 Hz nominal and RPCC.  Full DS10/DS20/
    ES40 boot re-verify (changes boot real-time + reported CPU speed).

Loose thread:
  - ChatGPT's cited-HRM perspective on IPR/CSR 0x1010 was never reconciled
    against our HRM copy.  Low-stakes now that the blocker it commented on is
    resolved; reconcile when convenient (verify the citation vs our HRM; confirm
    IPR-space vs chipset-CSR-space).

## 8. Version semantics (for the record)

V1..V5 are INTERNAL development-state placeholders, not public releases.  V0-V3
are read-only locked references; V4 freezes at Oracle-complete; V5 is the active
forward tree carrying this trajectory.  The PUBLIC version advances by decimal
increment per development state (e.g. v1.4.0 today -> v1.4.1 at the next state).
The first public set: DS10/DS20 + ES40/ES45 as a single x86_64 binary, faithful
to the SRM `>>>`.
