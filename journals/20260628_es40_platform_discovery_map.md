<!--
EmulatR V4 -- ES40 Platform Discovery Map + Family-Wide Badge Framing
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-28.  Source: claude.ai web (analysis/design only); Cowork executes
against the live tree at D:\EmulatR\EmulatRAppUniV4\Emulatr.
Purpose: map the ES40/ES45 platform class for SRM firmware boot. Establishes the
Tsunami-vs-Titan sequencing spine; frames ES40 as platform-identity discovery on
the already-booting Tsunami console (NOT a new bring-up); enumerates the ES40-vs-DS20
delta checklist; frames the banner/badge as ONE shared family-wide mechanism (fix
DS20 -> DS10/ES40 inherit). No code landed this session (architecture mapping day).
ASCII(128) only.
-->

# ES40 Platform Discovery Map + Family-Wide Badge Framing

**Date:** 2026-06-28
**Status:** ARCHITECTURE MAP. No code landed this session. Today = map; tomorrow =
code + doctest plans.
**Predecessors / context:**
- DS20 badge campaign + HWRPB region work (memory.md 2026-06-24/25/28 entries;
  `journals/HWRPB_Region_Fidelity_and_Resume_20260624.md`,
  `journals/20260628_hwrpb_handoff_gates_plan.md`)
- `journals/20260628_smp_harness_inventory_and_completion_plan.md` (the 4-CPU target)
- `Tsunami_HRM_vs_AXPBox_Profile.md`, `20260519_decompressor_pal_overlap_findings.md`,
  `CchipPhaseA_Design_Notes.md` (ES40 profile + load-base facts already in tree)

NOTE: an early-project `es40_rom_toolkit.zip` sketch was reviewed and DISREGARDED as
non-authoritative. Nothing in it informs this map. Task 1 below removes it.

---

## 0. TL;DR

"ES40/45" is TWO classes, split by chipset, and the split sets the sequencing:

- **Tsunami / 21272 family -- DS10, DS20, ES40.** Same chipset you already boot.
  ES40 is INCREMENTAL platform-identity discovery, not a firmware bring-up.
- **Titan / 21274 family -- ES45, DS25.** A DIFFERENT chipset (the project's Titan
  doc = the ES45 service manual; 21274 Pchip/Cchip/Dchip). A separate, larger track
  that needs a 21274 chipset model standing up first. DEFER until after ES40.

ES40 next; ES45/DS25 later -- and they are different KINDS of work, not different
sizes of the same work.

The banner/badge is ONE shared family-wide mechanism. Fixing DS20's badge fixes the
mechanism for DS10 and ES40 too -- each platform then needs only its correct strap
VALUE through the same path. Adding ES40 does not add badge engineering effort.

---

## 1. The split that orders everything: Tsunami vs Titan

| Platform | Codename (orientation) | Chipset | CPUs | Profile clk | Status in EmulatR |
|----------|------------------------|---------|------|-------------|-------------------|
| DS10     | Webbrick               | Tsunami 21272 | 1 | -- | boots; badge wrong |
| DS20     | Goldrush               | Tsunami 21272 | 2 | -- | boots; badge wrong (active campaign) |
| ES40     | Clipper                | Tsunami 21272 | 4 | 600 MHz | NOT YET; this map |
| ES45     | --                     | Titan 21274   | 4 | 1 GHz | needs 21274 model |
| DS25     | --                     | Titan 21274   | 2 | -- | needs 21274 model |

Grounding (project tree): Titan doc is "AlphaServer ES45", registers are the 21274
(e.g. 21274 Pchip error regs) -- a Tsunami-shaped but distinct part. `CchipPhaseA_
Design_Notes.md` carries `ES40 = 600 MHz` / `ES45 = 1 GHz` as interval-timer
constants. `20260519_decompressor_pal_overlap_findings.md`: AXPBox `PAL_BASE =
0x600000` is the ES40 value EmulatR copied. So ES40 is already a known profile in the
tree; the chipset under it is the one you boot today.

Codenames are orientation only; no load-bearing fact depends on them.

---

## 2. ES40 is a platform-identity problem, not a firmware bring-up

Because ES40 shares the 21272, it shares everything downstream: SRM console body,
decompressor, HWRPB hand-off, IIC/OCP/FRU device architecture, EV6/EV67 PALcode
personality. ES40 is the SAME console you are already debugging for DS20, IDENTIFIED
differently. The discovery is the same instrument-driven loop you already ran for
DS20 (`EMULATR_IIC_TRACE`, `EMULATR_PA_WATCH`, `EMULATR_HWRPB_SCAN`, static disasm of
the decompressed image), applied to the ES40 identity + topology deltas.

---

## 3. The badge: ONE shared family-wide mechanism (the key framing)

The banner defect is a SINGLE mechanism shared across the entire Tsunami/pc264 family
(DS10, DS20, ES40), NOT a per-platform bug:

- firmware `get_sysvar`/`build_dsrdb` reads SYSVAR, decodes a SysType member from the
  table at firmware VA `0x153cd8` (known members: 264DP=1, DS20=6, DS20E=8), and
  FALLS BACK to member 1 (264DP) when it cannot identify the platform.
- Working root-cause hypothesis (carried from the DS20 campaign): the Cchip CSC<7:0>
  TIGbus strap low byte is not seeded with the platform's correct strap value, so the
  DSR/SMM lookup feeding HWRPB[80]/[88] + the DSRDB falls to the 264DP default. A
  Cchip construction-init issue, not a firmware-side decode bug.

CONSEQUENCE (Tim's framing, on the record): the FIX is built ONCE, on DS20.

- SHARED, do-once (the engineering): the CSC<7:0> strap-seeding construction path +
  the verified `get_sysvar` decode + the instrumentation (Ghidra string xref on
  "AlphaPC 264DP", PC-watch the fallback assignment site, store-watch HWRPB[80]/[88],
  trace the Cchip CSC read).
- PER-PLATFORM, cheap (just a value): the strap CONSTANT that yields each platform's
  SysType member. DS20 = member 6 (known). DS10 = member ? and ES40 (Clipper) =
  member ? are DISCOVERY items (source: apisrm SysType table + Tsunami HRM strap
  encoding). Feeding them is the same path -- no new effort.

ACCEPTANCE (family-wide, one path): DS10 badges "AlphaServer DS10", DS20 badges
"AlphaServer DS20", ES40 badges "AlphaServer ES40", all via one seeded-strap ->
SysType-member -> banner path. "100 MHz" RPCC mis-calibration (same symptom family)
tracked alongside, not separately.

So: **fix DS20's badge first; DS10 and ES40 inherit the mechanism.** ES40 adds a
strap value, not a campaign.

---

## 4. ES40-vs-DS20 delta checklist (the enumerable discovery surface)

1. **Platform identity / SYSVAR member** -- the badge (Section 3). Same mechanism;
   ES40 resolves to the Clipper member. HIGHEST value (also still open for DS20).
2. **CPU count = 4** -- ES40 is the natural 4-CPU validation target for the SMP
   harness (`journals/20260628_smp_harness_inventory_and_completion_plan.md`). It is
   the platform that actually exercises N>2: LL/SC interlock, per-CPU HWRPB PCS slots,
   IPI fan-out. Reason to do ES40 WITH the SMP work, not strictly after it.
3. **Device tree (IIC/OCP/FRU)** -- ES40-specific contents (FRU reports ES40; OCP/LCD
   device set differs). Same IIC bus model proven for DS20; new `es40_platform.json`
   manifest analogous to `ds20_v7_3_platform.json`.
4. **Profile clock = 600 MHz** -- already a constant in `CchipPhaseA_Design_Notes.md`;
   confirm the interval-timer `roundLog2Nearest` target for ES40.
5. **PCI / IO topology** -- ES40's larger PCI fan-out; SAME 21272 Pchip already
   modeled. Enumerate slots/bridges vs DS20.
6. **Firmware image question** -- see Section 5 (the gate).

---

## 5. The gate: one Tsunami console self-IDing, or a separate ES40 image? (apisrm VERIFY)

Everything above assumes you can feed the firmware an ES40 identity. The first-order
question -- to resolve READ-ONLY against your apisrm/srmconsole tree:

- `pc264.c` is the Tsunami platform module already in play (you cite `start_secondary
  @ pc264.c:333` in the SMP work). "pc264" = AlphaPC-264 / DP264 family = the
  DS10/DS20/ES40 lineage. IF pc264.c is the common platform module and its
  sysvar->member logic includes Clipper/ES40, THEN ES40 is the SAME console binary,
  self-selected by SYSVAR -- no separate image needed; you need the right strap +
  device tree. IF apisrm instead emits per-platform console binaries, you must source
  the ES40 image (HP/DEC ES40 firmware update kit, or build from apisrm).

Check in apisrm/srmconsole (mapping, read-only):
- platform build dir layout: one Tsunami/pc264 console, or per-platform binaries?
- `pc264.c` + companions: sysvar/systype derivation; the SysType member table source
  (what compiles to `0x153cd8`); where Clipper/ES40 appears.
- the FRU/OCP platform module: what ES40 reports over IIC.
- the MP-boot / CPU-topology path for the 4-CPU case.

This one fact collapses most of the ES40 uncertainty: feed-straps vs source-an-image.

---

## 6. Load-base investigation (grounded; toolkit absent)

Open thread, decided ENTIRELY by firmware evidence, not by any sketch: the live tree
runs at `PAL_BASE = 0x600000` (AXPBox-copied ES40 value). The DS20 decompressor
self-check compares against link bases `0`, `0x20000000`, `0x20000240`; the DS20
`0x60222c` halt was diagnosed as a load-base mismatch (image at `0x600000` is a base
the firmware does not recognize). If `0x20000000` is the base the firmware wants, it
is because the firmware's OWN self-check names it -- this rides on the DS20 SrmLoader
/ decompressor-link-base track, not on this map. Named here as a connected
investigation; NOT a fix to apply, and explicitly out of scope for the ES40 identity
work (Section 9).

---

## 7. TASK LIST (this is the "next step" list)

Task 1 is housekeeping; 2-5 are the read-only ES40 mapping that sets up tomorrow's
code/doctest plans. Nothing here writes emulator code -- that is tomorrow, gated on
sign-off per discuss-before-code.

- [ ] **Task 1 -- REMOVE the early-project toolkit from the primary dev system.**
      Delete `es40_rom_toolkit.zip` (and any extracted copy) from the primary
      Windows dev tree. NON-AUTHORITATIVE early sketch; no content retained; absent
      from this map by design. Do the removal NATIVELY on Windows -- NOT via the
      Cowork FUSE mount (the known unlink/truncate hazard). If it is not present on
      the primary tree (it may exist only as a claude.ai upload), no action.
- [ ] **Task 2 -- [READ-ONLY apisrm] Resolve the Section-5 gate.** One Tsunami/pc264
      console self-selecting by SYSVAR, or per-platform binaries? Does the SysType
      table carry a Clipper/ES40 member? Report findings.
- [ ] **Task 3 -- [READ-ONLY] Enumerate the ES40 SysType member + strap value.** From
      apisrm SysType table source + Tsunami HRM strap encoding: the member number for
      ES40 (and DS10, while there) and the CSC<7:0> strap that yields it. Feeds the
      family-wide badge fix (Section 3).
- [ ] **Task 4 -- [READ-ONLY] Draft the ES40 delta sheet** from Section 4: device
      tree (FRU/OCP) contents, PCI topology vs DS20, profile-clock confirm. Output =
      the spec for an `es40_platform.json` manifest (do NOT write the manifest yet).
- [ ] **Task 5 -- [DESIGN] Scope tomorrow's doctest spine** from the deltas: a
      platform-identity test (SYSVAR strap -> ES40 member -> "AlphaServer ES40"
      banner, sharing the DS20 badge-fix path), a manifest/device-tree test, and the
      4-CPU LL/SC contention test from the harness plan finally on its real target.

---

## 8. VERIFY / CONFIRM ledger

- **GATE (Section 5):** apisrm one-console-vs-per-platform; Clipper/ES40 in the
  SysType table. Resolve before any ES40 image/manifest decision.
- **CONFIRM:** ES40 (and DS10) SysType member numbers + their CSC<7:0> strap values.
- **VERIFY:** ES40 profile-clock interval-timer target (`roundLog2Nearest`) vs the
  `CchipPhaseA_Design_Notes.md` 600 MHz constant.
- **VERIFY:** ES40 PCI topology (slots/bridges) on the existing 21272 Pchip.

---

## 9. Out of scope (do NOT chase here)

- **ES45 / DS25 (Titan / 21274).** Needs a 21274 chipset model standing up before
  any firmware discussion is meaningful. The project's Titan service manual is the
  substrate. A peer of the Tsunami chipset bring-up, NOT of ES40 identity discovery.
  Separate track, after ES40.
- **The DS20 boot-halt / load-base track** (`0x600000` vs `0x20000000`, the
  `0x60222c` panic). Connected (Section 6) but its own SrmLoader/decompressor track;
  not part of ES40 identity mapping.
- **PALmode PC<0> fidelity, HWRPB two-gate hand-off, LL/SC seam edits** -- their own
  journals; referenced where they intersect, not re-opened here.

---

## 10. CONFIRMED vs DISTILLED (provenance)

- CONFIRMED (project tree): Titan = 21274 / ES45 service manual; ES40 = 600 MHz,
  ES45 = 1 GHz; AXPBox `PAL_BASE = 0x600000` copied for ES40; DS20 decompressor link
  bases `0`/`0x20000000`/`0x20000240`; SysType members 264DP=1/DS20=6/DS20E=8 at VA
  `0x153cd8`; `pc264.c` as the in-play Tsunami platform module.
- DISTILLED / design judgment: the Tsunami-vs-Titan sequencing spine; ES40 = identity
  discovery not bring-up; the badge-as-one-shared-mechanism framing (Tim's, affirmed);
  the delta checklist; the apisrm gate framing. These follow FROM the confirmed facts;
  confirm in review, not transcribed.
- DISREGARDED: `es40_rom_toolkit.zip` (early-project sketch; non-authoritative; Task 1
  removes it). Its load-base number coincided with real decompressor evidence but
  contributed no authority -- the evidence stands on its own.
