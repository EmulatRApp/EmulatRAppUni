<!--
EmulatR V4 -- memory.md SURGICAL PATCH BRIEF (ES40 platform discovery map)
Date: 2026-06-28.  Source: claude.ai web.  Apply against the live memory.md.
PATCH BRIEF, not a file. Cowork: insert at the TOP entry position
(most-recent-at-top). Prefer surgical Edit. ASCII(128) only.
-->

# memory.md patch brief -- ES40 platform discovery map (2026-06-28)

One edit: insert the entry below at the top entry position (under the file header /
above the newest existing entry).

================================================================================
INSERT new entry at top entry position
================================================================================

## 2026-06-28 -- ES40 mapped (Tsunami sibling); badge = ONE family-wide mechanism

**Headline: mapped the ES40/ES45 class for SRM boot. ES40 = Tsunami/21272 sibling of
DS10/DS20 -> INCREMENTAL identity discovery on the console you already boot, NOT a new
bring-up. ES45/DS25 = Titan/21274 = a separate larger track (new chipset model),
DEFER. The banner/badge is ONE shared family-wide mechanism: fix DS20 -> DS10/ES40
inherit. NO CODE LANDED (mapping day).** Full map + task list:
`journals/20260628_es40_platform_discovery_map.md`.

- **THE SPLIT (sequencing spine).** Tsunami/21272: DS10(1), DS20(2), ES40(4 CPU,
  600 MHz) -- SAME chipset, already booting. Titan/21274: ES45(1 GHz), DS25 --
  DIFFERENT chipset (project Titan doc = ES45 service manual, 21274 Pchip/Cchip/
  Dchip); needs a 21274 model standing up first. ES40 NEXT; ES45/DS25 LATER; they are
  different KINDS of work, not different sizes. ES40 already a known profile in-tree
  (CchipPhaseA 600 MHz; AXPBox PAL_BASE 0x600000 copied for ES40).
- **ES40 = identity discovery, not bring-up.** Shares SRM console body, decompressor,
  HWRPB hand-off, IIC/OCP/FRU architecture, EV6/EV67 PAL. Same instrument loop as DS20
  (IIC_TRACE, PA_WATCH, HWRPB_SCAN, static disasm).
- **BADGE = ONE SHARED MECHANISM (key framing).** get_sysvar/build_dsrdb reads SYSVAR
  -> SysType member (table VA 0x153cd8: 264DP=1, DS20=6, DS20E=8) -> falls back to
  member 1 (264DP) when it cannot ID the platform. Root-cause hypothesis: Cchip
  CSC<7:0> TIGbus strap low byte not seeded with the platform's strap value. FIX BUILT
  ONCE on DS20 (the strap-seeding construction path + verified decode + the existing
  instrumentation); DS10/ES40 INHERIT the mechanism and each only need their correct
  strap CONSTANT. Per-platform member numbers (DS10=?, ES40/Clipper=?) are DISCOVERY
  items (apisrm SysType table + Tsunami HRM strap encoding); feeding them is the same
  path -- no new badge engineering for ES40. Acceptance family-wide: DS10/DS20/ES40
  each badge correctly via one seeded-strap -> member -> banner path.
- **THE GATE (apisrm VERIFY).** One Tsunami/pc264 console self-IDing by SYSVAR, or
  per-platform binaries? pc264.c (cited already: start_secondary @ pc264.c:333) is the
  in-play Tsunami platform module = DS10/DS20/ES40 lineage. If its sysvar->member logic
  carries Clipper/ES40, ES40 is the SAME binary + right strap + device tree (no
  separate image). Else source/build an ES40 image. Resolve READ-ONLY first.
- **ES40 IS THE 4-CPU SMP TARGET.** It exercises N>2 (LL/SC interlock, per-CPU HWRPB
  PCS slots, IPI fan-out) -- a reason to do ES40 WITH the SMP harness work
  (journals/20260628_smp_harness_inventory_and_completion_plan.md), not strictly after.
- **DELTAS vs DS20 (enumerable):** SYSVAR member (badge), CPU count 4, device tree
  (FRU reports ES40, OCP/LCD set) -> new es40_platform.json, profile clock 600 MHz,
  PCI topology (same 21272 Pchip). Load-base thread (0x600000 vs 0x20000000) rides the
  DS20 decompressor track, NOT ES40 identity -- out of scope here.
- **TASK 1 (housekeeping):** remove the early-project es40_rom_toolkit.zip from the
  primary dev tree -- NON-AUTHORITATIVE sketch, DISREGARDED, omitted from the map by
  design. NATIVE Windows delete, NOT via the Cowork FUSE mount (unlink/truncate
  hazard). May exist only as a claude.ai upload -> then no action.
- **Tomorrow:** code + doctest plans -- platform-identity test (strap -> ES40 member ->
  "AlphaServer ES40", sharing the DS20 badge-fix path), manifest/device-tree test,
  4-CPU LL/SC contention test on its real target.
