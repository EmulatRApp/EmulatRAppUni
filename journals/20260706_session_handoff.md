# Session Handoff — ES40 / DS10 / DS20 Boot, Regression, and the 0x2d Faithfulness Reversal (2026-07-06)

Comprehensive handoff for a fresh code session. Read this first, then the detailed
journals it cross-references. Authoritative sources (21264 HRM, Alpha Architecture
Handbook, apisrm SRM sources) are needed for the top task and will be re-provided.

---

## 0. Platform scorecard

| Platform | Result | Build |
|----------|--------|-------|
| **DS10** | boots to `>>>` (PuTTY-confirmed, live UART) | pre-regression `5c306d6` |
| **DS20** | boots to `>>>` (PuTTY-confirmed) | pre-regression `5c306d6` |
| **ES40** | clears ACV + delays, then **stalls in `build_config_tree`/`build_power_hw`** (IIC unmapped) — no `>>>` | branch `es40/reserved-ipr-0x2d` |

Console ports (base 10023 + model offset; `EMULATR_CONSOLE_PORT` overrides):
DS10=10023, DS20=10024, DS25=10025, **ES40=10026**, ES45=10027.

Stop cleanly with `touch EMULATR_STOP` in the run dir (out/build/mac-diag), wait for
exit, then `rm` the sentinel. Environment currently clean (no instances running).

---

## 1. THE TOP DECISION — roll back the 0x2d no-op; it is likely UNFAITHFUL (do this first)

This is the most important and most recent finding, and it reverses a prior "fix".

**What was done (branch, commit `eb74087`):** `HW_MTPR` to the unassigned EV6 IPR
index `0x2d` (guest pc=0x13f44, part of a register-init sweep) was changed from
`kFaultUnimplemented` to a silent **no-op** (`HW_RESERVED_2D`). Rationale in the
commit/journal: "real 21264 ignores writes to unassigned indices." This cleared the
long-standing ES40 `0x1b7dd4` ACV loop and let ES40 advance.

**Why it is now suspect — two independent reasons:**
1. **Unsourced.** The "real HW ignores unassigned-index writes" claim was NEVER
   cited. The 21264 HRM (Tufts) checked this session does NOT support it: its
   "writes ignored" rule is for **reserved bit-fields within a register** (HRM
   ~10349/10358), NOT an unassigned IPR **index**; and **OPCDEC** faults on
   "illegal opcode or **function field**" (HRM 12258) — which an unassigned index
   plausibly is. So the HRM is at best ambiguous and leans toward **fault**. This is
   exactly the "evolving cruft / uncited comment" failure mode flagged this session.
2. **Empirical regression.** DS10 and DS20 **boot to `>>>` when `0x2d` faults**
   (pre-regression build) and **hang when it is a no-op** (branch). Real DS10/DS20
   hardware boots, so the faithful behavior is the one that boots them — the FAULT.
   The no-op is therefore very likely unfaithful and is what regressed DS10/DS20.

**Faithful re-plan (recommended, not yet executed):**
- **Roll back `HW_RESERVED_2D`** — restore the fault on the `0x2d` write. Un-regresses
  DS10/DS20 by construction.
- **Re-frame the ES40 `0x1b7dd4` ACV as a fault-DELIVERY bug**, not a "tolerate the
  write" problem. On real HW the write faults → the SRM OPCDEC handler runs →
  continues (DS10/DS20 prove this). ES40 cascaded to the NULL-base `DtbMissDouble`
  because V4 mis-delivers that fault (wrong vector / bad state R23=0x13f45), not
  because the fault is wrong. Fixing V4's fault delivery should boot all three.

**Verify before/while rolling back (do NOT assert):**
- The exact **21264 HRM / Alpha Architecture Handbook** ruling on `HW_MTPR` to an
  unassigned IPR index (fault/OPCDEC vs ignore). Cite it.
- That DS10/DS20 actually **execute** a `0x2d` write (confirm the regression
  mechanism instead of inferring it — a one-line probe answers it).

**Consequence for sequencing:** rolling back the no-op puts ES40 back at the ACV,
UPSTREAM of the IIC/`build_power_hw` blocker in section 3. The IIC work is real but
sequenced BEHIND the faithful ACV fix.

---

## 2. The DS10/DS20 regression (bisected, confirmed)

DS10/DS20 boot to `>>>` pre-ES40-updates and hang on the branch. Git-bisected
(model=DS10, ds10_v7_3.exe, 1 GiB, `--max-cycles 0x12000000`):

| Commit | DS10 |
|--------|------|
| `01c78c5` baseline | boots past poll → `0x61xxx` |
| `5c306d6` (VA_FORM + all Typhoon/chipset) | STILL passes → VA_FORM + chipset INNOCENT |
| `b6e4c67` (unalign flip) | halts ~192M (PAL trap/DtbMiss) |
| branch (unalign reverted) | STILL hangs at `0x13d34` → 2nd regressor = `0x2d` |

**Two CPU-global fixes, validated only on ES40, each regressed DS10/DS20:**
- **unalign flip `b6e4c67`** (`unalignTrapEnabled=true`) — re-exposed the latent
  `0xdb64` PAL UNALIGN handler bug (R20 zeroed before STQ). **Already reverted** to
  `false` on the branch (commit `2459fce`, documented). Faithful trapping must wait
  on fixing the `0xdb64` handler, then re-enable + re-verify DS10/DS20/ES40.
- **`0x2d` no-op `eb74087`** — see section 1.

Lesson made concrete: **run DS10/DS20 boot-to-`>>>` before widening any CPU-global
fix.** Neither of these was re-tested on the Tsunami family when it landed. Detail:
`journals/20260705_ds10_regression_and_open_tasks.md`.

---

## 3. ES40 next frontier — `build_config_tree` / `build_power_hw` hangs on the unmapped IIC (contingent on §1)

With the (suspect) `0x2d` no-op, ES40 clears the ACV, spin-skips the ~990M-cycle
delays, and reaches SRM console code — then stalls in a non-terminating dispatch loop
orbiting `0x62xxx/0x6bxxx/0x8cxxx/0xa8xxx/0xae2xx`, no COM1 init, no `>>>`.

**Root (source-confirmed):** the loop is the SRM's `build_config_tree`
(`galaxy_pc264.c`). ES40 is **SYSVAR variation 5 (shark.c / Clipper)**
(`plat_sys_variation = 5<<10`), so `gct_init$pc264_hw`'s `(SYSVAR[0]>>10)!=1` test is
TRUE → it runs **`build_power_hw`** (which variation-1/264DP SKIPS). `build_power_hw`
reads the `iic_system0`/`iic_system1` status registers at IIC **0x70/0x72** (one byte
each; bit-tests ps_present/ps_ok/fan_ok/temp to decide sensor subpackets) over the
SMB = IIC bus. **The ES40 IIC controller is UNMAPPED in V4** — `kIicBaseByModel`
(`chipsetLib/TsunamiChipset.h`) has rows for `DS10=0xFFFF0000`, `DS20=0xFFF80000`,
`DS20E`, but **no ES40 row** → the reads never complete → hang. On `fread` failure
`build_config_tree` returns `!SUCCESS` and unwinds (galaxy_pc264.c ~1582), so IIC is
**required** on the ES40 path.

**Corrections to earlier hypotheses (do not repeat):**
- NOT the ADM9240. `build_power_hw` reads `0x70/0x72` status regs, not the sensor.
  The ADM9240 is the real LM87-class monitor (diags/i2c/adm9240.c, shark.c) but is
  faithful-diags, off the boot-critical path — defer it.
- `shark.c`'s I2C table is the WRONG controller family for the base (PCF8574 GPIO
  bit-bang / `SHARK_I2C_REG0`), NOT the PCF8584 V4 models. Do not source the
  controller base from shark.c. It IS useful for ADM9240 semantics later.
- The `0x70/0x72` node addresses ARE already in `es40_platform.json`. So the unblock
  is CODE-only (the `kIicBaseByModel` row), not a manifest device add.

**Fix plan (once §1 lets ES40 reach here faithfully):**
1. Add the ES40 `kIicBaseByModel` row — a chipset/CSR-layout capability (code today,
   MANIFEST-ideally per the SSOT vision; flag that in the commit so it doesn't become
   the next scattered constant).
2. FRU EEPROMs (`0xA2/0xA4`, already in manifest) become readable on the same bus.
3. ADM9240 afterward (faithful diags).

**How to find the ES40 IIC base — PROVE it, don't assume (method caution):**
Use the DS20-locating technique (first PCF8584 poke → `UNHANDLED OUTER WRITE` surfaces
the base), **form-checked** against the DS10/DS20 `0xFFFx_xxxx` Tsunami-I/O pattern.
BUT first resolve a three-way fork by observation, because the earlier run showed NO
`UNHANDLED OUTER WRITE` and "throttled log" is the unfalsifiable-comfortable
explanation to reach for LAST (the SL_XMIT / "delays are serial" pattern):
- **never-entered** — the `>>10 != 1` guard was false (var != 5 on that run);
- **fopen-null** — `fopen("iic_system0")`/DDB resolution failed BEFORE any CSR poke
  (→ fix node resolution, not the base row);
- **CSR-poke-unhandled** — the poke happened; catch it + read the address (→ add the
  row). Also confirm WHICH sink an unmapped Tsunami CSR/IO access routes to (Pchip
  outer-write vs CSR-decode) before trusting any grep's silence.
Recommended instrument: an unconditional (non-throttled) watch on any bus access into
the high Pchip-window offsets (`0xFF00_0000-0xFFFF_FFFF`), logging PC+addr+R/W — makes
the event prove itself and yields the base + the poke PC (confirming it's inside
`build_power_hw`). Also check for an ES40 symbol map to confirm the `0x629f0` loop
symbolizes to `build_power_hw`/`gct_init` (that mapping is currently INFERRED).

**Keep separate (do not merge into the base fix):** the `0x9e` `console_id` node —
DS20's manifest has it, ES40's does not. That's a manifest/device-inventory question,
orthogonal to the controller base (chipset axis). Let the boot show whether ES40's
config path actually consults `0x9e` before adding it (evidence-before-scope).

Detail: `journals/20260705_es40_next_frontier.md`.

**MEMO: test ES40 at 32 GB** (Typhoon, ASIZ 0xA) on the **Windows PC** (132 GB host).
The 4 GB run found this frontier; 32 GB exercises extended-AAR tiling and may change
the config-tree path.

---

## 4. Tools + infrastructure built this session (committed, keep)

- **Spin-skip optimizer** (`systemLib/SpinSkip.h`, commit `f7e2f4d`, env
  `EMULATR_SPINSKIP`, default OFF): fast-forwards PROVEN side-effect-free register
  countdown loops (closed-form trip count, refuse-by-default, interrupt-aware). ~5-18x
  faster ES40 boot; identical fault signature. Loud refusals = free map of the boot's
  side-effectful loops. See [[spin-skip-optimizer]].
- **Capability primitive** (`systemLib/PlatformCapabilities.h`, commit `a5bd43a`,
  INERT/zero call sites): model=key, manifest caps=values, gate tests the capability
  (`platHas(PlatCap::...)`). The `kIicBaseByModel` row (§3) and the `0x2d` scope (§1)
  are its natural first consumers. See [[platform-axis-and-capability-gating]].
- **DecListingSink value-gate cycle-floor + PC-gate** (commit `d6439ed`): armed
  lookback-ring dumps (`EMULATR_VALUE_GATE_FLOOR`, `EMULATR_PC_GATE`). Reusable.
- **Ev6Translator Hook A/C probes** (commit `dc8f437`, `EMULATR_BRINGUP_PROBES`):
  ES40-ACV diagnostic scaffolding — STRIP once the ES40 path is settled.
- **host_decompressor oracle** (`tools/host_decompressor/oracle`): authoritative
  decompressed image; file offset = guest PC − 0x8000. Used for byte-compares. See
  [[host-decompressor-oracle]].

---

## 5. Git state

Branch **`es40/reserved-ipr-0x2d`**, pushed to `origin`
(github.com/EmulatRApp/EmulatRAppUni). 7 commits:
`eb74087` 0x2d no-op · `f7e2f4d` spin-skip · `a5bd43a` capability+axis ·
`2459fce` unalign revert · `d6439ed` trace value-gate/pc-gate · `dc8f437` ES40 probes ·
`a3e84e4` DS10/ES40 journals.

**DO NOT MERGE to main** until §1 is resolved: `eb74087` (0x2d no-op) regresses
DS10/DS20 and is likely unfaithful. The likely outcome is a *revert* of `eb74087`, not
a merge. On resume, verify the tracked `config/EmulatrV4.ini` `model=` value (run-dir
copies were edited across platform tests; run-dir ini is gitignored).

---

## 6. Open tasks (prioritized)

1. **[faithfulness, top]** Verify HRM/AAH ruling on `HW_MTPR` to an unassigned IPR
   index; confirm DS10/DS20 execute a `0x2d` write; then **roll back `HW_RESERVED_2D`**
   and re-frame the ES40 ACV as a fault-DELIVERY fix (§1).
2. **[DS10/DS20]** Fix the `0xdb64` PAL UNALIGN handler; re-enable faithful unalign
   trapping; re-verify DS10/DS20/ES40 boot-to-`>>>`.
3. **[ES40, contingent on 1]** Add the ES40 `kIicBaseByModel` row — resolve the
   never-entered/fopen-null/CSR-poke fork by observation; form-check the base;
   confirm the `0x629f0` loop is `build_power_hw` via symbol map.
4. **[ES40]** Then FRU EEPROM readability; ADM9240 later (faithful diags).
5. **[ES40]** Test at 32 GB on the Windows PC.
6. **[hygiene]** Strip the Ev6Translator ES40 probes; audit all session comments for
   authoritative citations (no evolving cruft); resolve merge-gating on the branch.
7. **[axis]** The `0x9e` console_id node — evidence-before-scope.
8. **[deferred]** PCI enumeration + on-board device models; Ev6Translator reference
   harvest; snapshots after `>>>`; ini→platform.json SSOT (the IIC base is a textbook
   candidate to migrate into the manifest).

---

## 7. Working discipline reaffirmed this session (carry forward)

- **Faithful emulation.** Model real hardware behavior; the empirical boot on real
  DS10/DS20/ES40 is ground truth.
- **Comments must cite authoritative sources.** An uncited "real HW does X" is
  evolving cruft — check or request the HRM/AAH/SRM source before asserting. The
  `0x2d` no-op is the cautionary example.
- **Prove events; don't infer from absence.** "Throttled log" / "it happened and got
  dropped" is the unfalsifiable-comfortable assumption that bit SL_XMIT and
  "delays are serial." Make the event prove itself.
- **Distinguish V4 build-guard labels from firmware requirements** (the
  "ES40 isn't IIC-required" misread).
- **Reference-platform gate:** boot DS10/DS20 (and ES40) to `>>>` before widening any
  CPU-global fix.

## Cross-references
- `journals/20260705_ds10_regression_and_open_tasks.md`
- `journals/20260705_es40_next_frontier.md`
- `journals/20260705_platform_axis_classification.md`
- `journals/ACV_Superpage_Enable_Probe_20260705.md` (sec 11-12; note its §11-12
  "0x2d = reserved, no-op faithful" conclusion is SUPERSEDED by §1 above)
- Memory: [[ds10-regression-from-es40-updates]], [[es40-acv-reserved-ipr-0x2d]],
  [[platform-axis-and-capability-gating]], [[spin-skip-optimizer]],
  [[verify-hw-ids-against-hrm]], [[host-decompressor-oracle]]
