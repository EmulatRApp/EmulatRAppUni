# Session Handoff — DS10/DS20 boot to LFU, ES40=Typhoon, ACV=MEMDSC gate (2026-07-06 PM)

Git-tracked handoff (Mac-side auto-memory does NOT follow to the PC — read this first on the PC).
Branch `es40/reserved-ipr-0x2d`, pushed through `493e05b`.

## Commits this session (all pushed)
- `fa95357` fix(ev6): 0x2d rollback as labeled path-selector scaffold + three-bug decomposition
- `b677e2b` feat(diag): retire-time PC-window trace + register last-writer (`EMULATR_DIAG_PCLO/PCHI`, `DIAG_WREG/WMIN`)
- `70ccd4b` fix(chipset): **TIGbus window reads 0, not all-ones** — unblocks DS10/DS20 boot
- `19e26f2` chore(pal): `EMULATR_2D_NOOP` faithful-path toggle
- `493e05b` feat(platform): ES40 IIC manifest = authoritative PC264/Clipper node set

## Platform status
- **DS10 + DS20:** boot through SRM init to the console/LFU (`UPD>`) stage on both 0x2d paths. `UPD>` is the EXPECTED transient pre-`>>>` prompt (architect); literal `>>>` awaits DEFERRED LFU `.sys`-loading (ds10_disk1/ds10srm.sys = SRM console; disk2/3 = PCI option ROMs). Faithfully unblocked; regression-clean.
- **ES40:** runtime frontier = the `0x1b7dd4` `kFaultAcv` loop.

## Key facts established / corrected
1. **0x2d is a PATH-SELECTOR, not a bug; its fault delivery is FAITHFUL** (vector 0x8400, clean save, no shadow artifact). Rolled back to faulting (labeled scaffold). Journal: `20260706_0x2d_path_selector_and_three_bug_decomposition.md`.
2. **DS10 `0x13d38` root = V4 mis-routing the TIGbus window** (`0x801_0000_0000-0x801_3FFF_FFFF`) as PCI sparse memory → all-ones → `BLBS bit0` poll of `0x801_2800_0000` stuck. Fix (70ccd4b): TIG-window unmodeled reads return **0**. Applies to all Tsunami-family incl. ES40.
3. **ES40 = TYPHOON** (21272 high-bandwidth dual-Dchip VARIANT, NOT Tsunami, NOT a separate 21274 part). Authoritative: `chipsetLib/TsunamiVariant.h` (`variantFromModel`: DS10/DS20->Tsunami, ES40->Typhoon, ES45/DS15/DS25->Titan/21274). The manifest + `TsunamiChipset.h:72` "Tsunami 21272" comments are the STALE side of the known 3-way identity split.
4. **Chipset differentiation seam = `PlatformCapabilities`/`PlatCap`** (systemLib/PlatformCapabilities.h) — bitwise caps latched at Machine ctor; gate via `caps.has(PlatCap::ChipsetTyphoon|ChipsetTsunami|ChipsetTitan|DualPchip|SbAli)`. Four axes: CPU(universal)/model/chipset/southbridge (`journals/20260705_platform_axis_classification.md`). This is the seam for comprehensive Tsunami-complete + Typhoon.
5. **ES40 `0x1b7dd4` ACV = CHIPSET-axis Typhoon memory-region-layout / MEMDSC / AAR-ASIZ gate** (NOT translator/superpage/CPU — those framings WITHDRAWN). Garbage R16=`0xFFFFFFFF7F827F5F` from a LEGITIMATE `SUBQ`(R02=~1GB `0x3fc12000`, R00=~3.2GB scan-end of the fill loop @`0x5afac`). ALU/CSERVE/translator exonerated. SRM init runs superpage/1-1 (no PTE walk), so the seg1 garbage VA correctly ACVs — the ACV is a SYMPTOM; root is upstream. Full trace: `journals/20260702_es40_acv_garbage_origin_traced.md`. This single gate blocks ALL ES40 downstream.
6. **ES40 IIC staged & correct.** `es40_v7_3_platform.json` = authoritative PC264/Clipper set (from apisrm `srmconsole/IIC_DRIVER.C #if PC264` + KCRCM, sourced from the consumer `galaxy_pc264.c build_power_hw` which reads ONLY `iic_system0@0x70`->fail_register + `iic_system1@0x72`->func_register). ES40 = `CLASS_CLIPPER` (var=5), NOT DS20/DS20E — do NOT add OCP LEDs (0x40/0x42/0x4E) or `iic_cont@0xB6` to force a SYSVAR badge (SYSVAR>>10 gate is binary: member 1=264DP skips build_power_hw, !=1 runs it; not an OCP selector). Reachability fix = a `kIicBaseByModel[ES40]` row (TsunamiChipset.h:703), base PROVEN-VIA-POKE (first IIC write -> `UNHANDLED OUTER WRITE` at the real base, as located DS20). BUT the poke is DOWNSTREAM of the ACV — can't fire until the ACV clears.

## THE NEXT STEP (the single gate for everything ES40)
Finish the ACV root: capture **R09** (fill-loop count `while R01<R09` @`0x5afac`) → the Typhoon memory-size/MEMDSC/ASIZ field that overshoots the scan to ~3.2GB (leads: count-driven overshoot vs region-base mismatch, MEMDSC @ PA 0x2840). Tool: extend the DIAG facility (`EMULATR_DIAG_WREG=9` or add R09 to the DIAG-PC line) — the ONLY wrinkle is CYCLE TIMING: the fill->SUBQ->ACV chain now lands ~1.3B cycles (shifted later by spin-skip/TIG), so window with cap ~0x50000000 + `EMULATR_SPINSKIP=1`, not 302M. Value-gate (`EMULATR_VALUE_GATE=0x3fc12000`) did NOT fire this session (floor too high / dump routes to .trc — needs `EMULATR_TRACE_WINDOW=1` + no/low floor). Fixing the region layout unblocks build_power_hw -> IIC base poke -> FRU -> `>>>`; it's the same problem as the comprehensive-Typhoon work (all PlatCap-gated, behind this one gate).

## Run recipe (Mac; adapt paths on PC)
`cd out/build/<build> ; EMULATR_2D_NOOP=1 EMULATR_SPINSKIP=1 EMULATR_NO_PUTTY=1 ./Emulatr --firmware firmware/es40_v7_3.exe --mem 4294967296 --no-autoload --max-cycles 0x50000000`
Console: TCP 10023 (headless probe via python socket; `>>>`/`UPD>` on the port). Stop: `touch EMULATR_STOP`.
