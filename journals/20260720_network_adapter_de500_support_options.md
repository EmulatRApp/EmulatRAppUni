<!--
EmulatR V4/V5 -- Journal: Network Adapter (Ethernet) Support Options
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic, Cowork).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Per docs/notes/ADR-0001-source-file-headers.md (Markdown header as HTML comment).
ASCII(128) only.  Hex radix.
-->

# Network Adapter (Ethernet) Support Options -- DE500 first, family roadmap

    Doc id      : NET-ADAPTER-001
    Status      : BRIEFING / FOR CONSIDERATION.  Scopes the Alpha PCI Ethernet
                  family and recommends the concrete EmulatR target (the DE500-BA
                  / DECchip 21143 "Tulip") and an implementation shape (chip core
                  + thin board/EEPROM wrapper).  Non-blocking for the SRM `>>>`
                  path; closes the on-board-NIC enumeration gap.
    Date        : 2026-07-20
    Model       : claude-opus-4-8 (Cowork).  Drafted from the project architect's
                  survey + EmulatR-specific grounding.
    Relates to  : CLAUDE.md "Deferred / planned work -> PCI device enumeration +
                  on-board device models" (the DS10 on-board DEC 21143 / DE500
                  tulip candidate, and the TsunamiPchip UNHANDLED OUTER WRITE at
                  PA 0x800_FFFF_0000).  SRM conformance findings D3/D5 (real DS10
                  enumerates ewa/ewb DE500-BA; EmulatR enumerates none).
                  journals/20260612_dq_ew_driver_requirements_review.md,
                  networking_note_patch_20260628_npcap_policy.md.
    Encoding    : ASCII-128.  Hex radix.

---

## 1. Why this matters for EmulatR

Two open items converge on the on-board Ethernet:

  - **PCI-enumeration gap (CLAUDE.md deferred).** During `from_init` the SRM reads
    a PCI BAR for an on-board device V5 does NOT enumerate, gets all-ones, masks
    it to base 0xFFFF0000, and pokes that device's index/data register pair into
    the void (PA 0x800_FFFF_0000) -> TsunamiPchip UNHANDLED OUTER WRITE.  STRONG
    CANDIDATE is the on-board DE500 "tulip" Ethernet: apisrm/ref/dc287_def.h is
    full of 0xFFFF0000 CSR references, and the observed byte-toggle values match
    the CSR9 SROM bit-bang the firmware uses to read the station MAC.  Non-fatal
    today (the firmware tolerates the missing NIC), but a real NIC model silences
    it and unblocks network boot / device probing.

  - **SRM conformance delta (D3/D5).** On real DS10 hardware `init` enumerates:
        bus 0, slot 9  -- ewa -- DE500-BA Network Controller
        bus 0, slot 11 -- ewb -- DE500-BA Network Controller
    EmulatR enumerates neither.  Modeling the DE500 is the single change that
    turns D3 (missing PCI devices) and D5 (fibre/NIC absence) green for the
    Ethernet rows and lets `wwidmgr`/`show dev` line up with silicon.

So the NIC is not just a feature -- it is the concrete device behind an existing
"UNHANDLED WRITE" wall and a measured conformance gap.

## 2. The Alpha PCI Ethernet family

| Adapter | Chipset | Speed | Typical systems | SRM device |
|---|---|---|---|---|
| DE435 | DEC 21040 | 10 Mbps | early Alpha PCI (AlphaStation 200/400) | EWA0 |
| DE450 | DEC 21040/21041 | 10 Mbps | AlphaServer 1000, 1200 | EWA0 |
| **DE500** | **DEC 21140A (-AA) / 21143 (-BA)** | **10/100 Mbps** | **DS10/DS20, ES40, XP1000** | **EWA0** |
| DE600 | Intel 82558 | 10/100 Mbps | later EV6/EV67 | EIA0/EIAx |
| DEGPA | Intel 82543 (Gigabit) | 10/100/1000 | GS80/160/320 | EGA0 |
| DEGXA | Intel Pro/1000 | Gigabit | later AlphaServers | EGA0 |

SRM/OpenVMS name families by controller lineage:
  - **EWA** = DE4xx / DE5xx (DEC "Tulip" 2104x / 2114x family)
  - **EIA** = DE600 (Intel 8255x)
  - **EGA** = Gigabit (Intel 82543 / Pro-1000)

## 3. Per-adapter notes (condensed)

  - **DE435 (21040)** -- earliest PCI Tulip, 10Base-T only, very simple register
    set, no Fast Ethernet.  Only interesting for AlphaStation 200/400-era targets.
  - **DE450** -- "improved DE435": 10 Mbps, better media detect, AUI/BNC/RJ45,
    same early-Tulip architecture; driver-compatible with the DE435.
  - **DE500** -- THE standard EV5/EV6 card.  Bus-mastering DMA, descriptor rings,
    MII, full duplex, auto-negotiation, widely supported by SRM, native OpenVMS
    and Linux/BSD drivers, excellent documentation.  Shipped on essentially every
    DS10/DS20/ES40/XP1000.
  - **DE600 (Intel 82558)** -- architectural break from Tulip: better throughput,
    lower CPU, interrupt moderation, more efficient DMA.  A completely different
    device to model.
  - **DEGPA (82543)** / **DEGXA (Pro-1000)** -- Gigabit, GS-series and later
    AlphaServers, EGA devices.  Substantially more complex than Tulip; different
    programming model again.

## 4. What an ES40 / DS10 actually expects

For an AlphaServer ES40 (and DS10/DS20):
  1. DE500  -- most common integrated/installed adapter (primary target)
  2. DE600  -- later systems (secondary)
  3. DEGPA  -- optional Gigabit upgrade (rare on these boxes)

The DE500 is what SRM expects and what the boot firmware probes.  Everything else
is an add-in the firmware discovers but does not require.

## 5. REFINEMENT -- model the 21143 (DE500-BA), not the 21140A

The survey lists the DE500 as "DECchip 21140A."  That is the DE500-**AA**.  The
part actually on DS10/ES40 on-board (and what SRM prints) is the DE500-**BA**,
which is the DECchip **21143** ("Tulip III"):

  - Real DS10 `init` says "DE500-**BA** Network Controller" (Sec 1), i.e. the -BA
    board, i.e. the **21143** -- not the 21140A.
  - The 21143 is the variant with the integrated MII + SIA and, crucially, the
    **CSR9 (ROM/MII) serial bit-bang** the SRM uses to clock the station MAC out
    of the 93C46 SROM.  That bit-bang is exactly the byte-toggle pattern CLAUDE.md
    saw hitting PA 0x800_FFFF_0000.  Modeling the 21140A would NOT reproduce the
    -BA identity or the CSR9 MAC read faithfully.
  - **In-tree confirmation (authoritative).** `apisrm/ref/ew_driver.c` -- the SRM
    "port driver for the TULIP board" -- carries explicit DE500-**AA**, DE500-**BA**,
    and DE500-**FA** code paths and a note "Add monet support for the **21143** chip
    - different [from] the de500 series devices."  So the -BA IS the 21143, per
    Digital's own driver, and the per-board media/auto-neg differences live there.
  - The secondary oracle agrees: AXPBox's reference NIC is `src/DEC21143.cpp`
    (21143), not a 21140A -- corroborating the 21143 as the right core.

Recommendation: **core = DECchip 21143**, presented as DE500-BA.  The 21140A/-AA
becomes a trivial feature-subset/EEPROM variant later if ever needed.  (The two
are largely register-compatible Tulips; the -BA MII/CSR9 path is the delta that
matters for faithfulness.)

## 6. Implementation shape (chip core + thin board wrapper)

Mirror how Digital built the hardware:

  - **Model the 21143 as the core PCI device** -- PCI config space, bus-mastering
    DMA, the TX/RX descriptor rings, the CSR0..CSR15 register block, the interrupt
    model (CSR5/CSR7), the MII management interface, and the 93C46 SROM/EEPROM
    serial protocol (CSR9).  The authoritative register map is `apisrm/ref/
    dc287_def.h` ("DC287 'TULIP' Chip"): OCSR0 Bus Mode (0x00), OCSR1 TX Poll
    (0x08), OCSR2 RX Poll (0x10), OCSR3 Rx-Ring base (0x18), OCSR4 Tx-Ring base
    (0x20), OCSR5 Status (0x28), OCSR6 Serial Command (0x30), OCSR7 Interrupt Mask
    (0x38), OCSR8 Missed-frame counter (0x40), OCSR9 Address-and-Mode-Diagnostic /
    SROM serial (0x48) ... through CSR15.  The 93C46 layout the CSR9 bit-bang
    clocks out is `apisrm/ref/srom_def.h`.
  - **Treat the DE500 board as a thin wrapper** around that chip: it supplies the
    SROM contents (station MAC, media table, connector map), the PCI subsystem
    vendor/device IDs, and the media configuration.  Then DE500-AA / -BA / -XA and
    relatives are just different EEPROM images over the same 21143 core -- no
    separate device models.
  - This keeps the hard, reusable work (DMA, rings, interrupts, MII, SROM) in one
    place and makes board variants data, not code.  A future DE435/DE450 is a
    feature-subset of the same Tulip lineage; DE600 and the Gigabit parts are
    genuinely separate controllers and out of scope for the first pass.

## 7. EmulatR integration specifics

  - **Platform manifest.** Add the NIC as a PCI device entry in the ES40 / DS10
    `<model>_v7_3_platform.json` (class/vendor/device/subsystem + bus/slot), so
    PlatformConfig enumerates it at the slots SRM expects (DS10: slots 9 and 11
    for ewa/ewb).  This is what removes the all-ones BAR read and the
    TsunamiPchip UNHANDLED OUTER WRITE.
  - **SROM MAC.** Seed a station MAC into the 93C46 image so the CSR9 bit-bang
    returns a real address instead of the current void poke.  A one-shot
    STORE-WATCH on PA 0x800_FFFF_00xx (per CLAUDE.md) pins the exact storing PC to
    validate the CSR9 sequence against the model.
  - **Host backend.** Datapath (actual packet TX/RX) is governed by
    networking_note_patch_20260628_npcap_policy.md.  A STUB that (a) answers PCI
    config with a valid BAR, (b) absorbs CSR writes, and (c) presents a MAC via
    SROM is enough to satisfy enumeration + silence the UNHANDLED write WITHOUT a
    live host datapath; the full npcap-backed datapath is a later increment.
  - **Driver target.** OpenVMS `EW` driver + SRM `ewa`/`ewb`.  See
    journals/20260612_dq_ew_driver_requirements_review.md for the EW/DQ driver
    requirement notes already gathered.

## 8. Sequencing / priority

  - LOWER priority than the path to `>>>` and the current ES40 boot-transfer work
    -- the missing NIC does NOT block boot (firmware tolerates it).
  - HIGH value once picked up: it is the concrete device behind the standing
    UNHANDLED-WRITE wall AND it flips the Ethernet rows of the SRM conformance
    register (D3/D5) to match silicon.
  - Suggested first increment: **enumeration + SROM-MAC stub only** (Sec 7 host
    backend option), no datapath.  That is the minimum that silences the void
    poke and satisfies `show dev` / `init` enumeration; measure it against the
    real-DS10 golden with the SRM conformance kit.

## 9. References

### 9.1 In-tree PRIMARY references (PalCode / apisrm) -- cite these first

All under `Processor Support/Palcode/palcode/apisrm/apisrm/ref/`.  These are
Digital's own SRM sources and register headers -- the authoritative HRM-level
reference for the model; corroborate against them before any secondary source.

  - **`ew_driver.c`** -- SRM "port driver for the TULIP board."  DE500-AA / -BA /
    -FA code paths + 21143 ("monet") support.  Primary reference for CSR usage,
    the media/connector table, auto-negotiation, and the -BA vs -AA sequences.
    (Digital 1993, 1996.)
  - **`dc287_def.h`** -- "DC287 'TULIP' Chip header definitions."  The CSR
    PCI-offset register map (OCSR0..OCSR9+ -> CSR0..CSR15), incl. OCSR9
    Address-and-Mode-Diagnostic (the SROM serial port).  (Digital 1993, D.W. Neale.)
  - **`srom_def.h`** -- 93C46 SROM layout: station MAC + media/connector table
    that CSR9 clocks out.  (Digital 1990.)
  - **`lanrom_def.h`** -- LAN option/expansion ROM definitions.
  - **`f21140_edit.c`** -- 21140 SROM/media edit utility (DE500-AA / 21140A ref).
  - **`dc287_def.h` 0xFFFF0000 CSR references** -- the same CSR block CLAUDE.md
    saw the firmware poke at PA 0x800_FFFF_0000 when the NIC is un-enumerated;
    the authoritative cross-check for Sec 1 / Sec 7.
  - DE600 family (EIA, out of first-pass scope): `ei_driver.c`, `i82558.h`,
    `i82558_pb_def.h` (Intel 82558) -- a separate controller, cited for completeness.

  (When adding these to `Processor Support/REFERENCE_INDEX.md`, group them under a
  "Networking / LAN" heading so future sessions find them via the index-first rule.)

### 9.2 Secondary / external

  - Secondary oracle: AXPBox `src/DEC21143.cpp` (21143 reference model) -- use to
    corroborate CSR semantics, never as the primary authority over EmulatR.
  - Alpha LAN device-name families and upgrade notes: HPE Alpha community thread
    (EWA/EIA/EGA).
  - Linux `de4x5` driver doc (DE4xx/DE5xx Tulip register behavior), chiark mirror.
  - Stromasys CHARON-AXP users guide (DE500/DE600 modeling notes), manualzz.
  - Internal: CLAUDE.md deferred "PCI device enumeration + on-board device
    models"; SRM conformance kit register D3/D5;
    journals/20260612_dq_ew_driver_requirements_review.md;
    networking_note_patch_20260628_npcap_policy.md.

## 10. Standing rules

  ASCII-128; hex; surgical Edit; platform-device changes land in the manifest
  JSON + deviceLib, not hardcoded; discuss before code (P-0).  EmulatR is the
  PRIMARY oracle; AXPBox DEC21143.cpp is corroborative only.
