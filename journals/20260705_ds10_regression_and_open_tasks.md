# DS10 -- Boot-to-`>>>` Confirmed, Regression Bisected, and Open Tasks (2026-07-05)

## 1. Milestone

**DS10 boots to the SRM `>>>` prompt** -- confirmed live over the on-chip UART/COM1
console (PuTTY on TCP :10023), model=DS10, firmware `ds10_v7_3.exe`, 1 GiB. DS10 is
the reference Tsunami platform and the boot-to-`>>>` baseline for regression testing.

## 2. Regression: DS10 was broken by two ES40-era CPU-global fixes

DS10 boots to `>>>` on the **pre-ES40-updates** build but **hangs on the current
branch** (`es40/reserved-ipr-0x2d`). Git-bisected on the Mac (Release + probes,
`out/build/mac-diag`), model=DS10, `--mem 0x40000000`, `--max-cycles 0x12000000`:

| Commit | DS10 result |
|--------|-------------|
| `01c78c5` (pre-updates baseline) | boots past the poll to console `0x61xxx` |
| `5c306d6` (VA_FORM + all Typhoon/chipset changes) | STILL passes -> VA_FORM + chipset INNOCENT |
| `b6e4c67` (unalign flip) | halts DS10 at ~192M in the PAL trap/DtbMiss handler |
| current branch (unalign reverted) | STILL hangs at `0x13d34` -> a SECOND regressor |

Two independent regressors, each validated only on ES40, neither re-tested on DS10:

### 2a. unalign flip -- `b6e4c67` (`unalignTrapEnabled = true`)
Made every unaligned data access trap to the guest PAL UNALIGN handler. This
re-exposed the latent `0xdb64` PAL UNALIGN handler bug (R20 zeroed before STQ to
top-of-PA) that the flip *assumed* the VA_FORM three-form fix had resolved -- it had
not, for DS10's access pattern. DS10 grinds/halts at ~192M (PAL `0x8680` region).
**Reverted to `false` on the branch** (uncommitted, documented in `coreLib/CpuState.h`
~line 619). Faithful trapping is the correct EV6 behavior; it must wait on FIXING the
handler, not just enabling the trap.

### 2b. `0x2d` tolerate -- `eb74087`
The ES40 ACV fix (tolerate `HW_MTPR` to unassigned IPR index `0x2d`) changes DS10's
SRM path so the `0x13d34` **device-ready poll never clears**:

```
0x13d1c  HW_ST R31,[R16+0x1440]   ; KICK device @ 0x801_3000_0000 (TIG)
0x13d34  HW_LD R20,[R12+0x1000]   ; POLL device @ 0x801_2800_1000
0x13d38  BLBS  R20 -> 0x13d34     ; UNBOUNDED wait for bit0 to CLEAR (no timeout)
```

The poll is unbounded (no counter/timeout in the enclosing block) -- so on real DS10
the device *always* clears bit0, i.e. it is mandatory+functional. At `5c306d6` the
poll clears and DS10 boots; with the `0x2d` no-op it never clears. Spin-skip correctly
REFUSES this loop (live CSR read in body).

### 2c. KEY -- `0x2d` is platform-divergent, NOT CPU-universal
`0x2d` **helps ES40 (clears the ACV) but breaks DS10**. This falsifies the
`20260705_platform_axis_classification.md` conclusion that `0x2d` is a CPU-axis truth
that "stays global." The axis reasoning was right in form; "correct on every platform"
was an unverified assumption. `0x2d` is the **first real consumer for the capability
primitive** (`systemLib/PlatformCapabilities.h`): it must be `platHas(...)`-scoped, not
applied globally. The boot-to-`>>>` evidence gate simply was never run on DS10.

## 3. What this validated

- The refuse-by-default spin-skip predicate: a live-CSR-read body is exactly the
  "value changes between iterations" clause -> it declined the poll and surfaced it as
  a real frontier instead of warping past it (3rd time it paid off).
- The "classify before scoping / verify per-platform" discipline -- the hard way.
- The reference-platform test that catches this class: **run DS10 to `>>>` over PuTTY
  BEFORE widening any CPU-global fix.**

## 4. OPEN TASKS

### Blocking DS10 boot-to->>> on the current branch (do first)
1. **Fix the `0xdb64` PAL UNALIGN handler** (R20 zeroed before STQ to top-of-PA), then
   re-enable faithful `unalignTrapEnabled = true`. Verify DS10 AND ES40 both reach
   `>>>`. Until then keep the `false` revert (uncommitted on branch).
2. **Scope `0x2d` by capability** -- gate the `HW_RESERVED_2D` no-op behind
   `platHas(...)` (the primitive's first live call site). First determine WHY DS10
   diverges: does DS10 even write `0x2d`? What did the pre-fix `kFaultUnimplemented`
   OPCDEC path do that made the poll device clear? The gate axis is likely NOT model
   but a capability (which platforms depend on the OPCDEC-side-effect vs tolerate).
3. **Re-run the DS10 + ES40 boot-to->>> gate** (spin-skip on for speed) after 1+2.

### Branch hygiene (before any merge to main)
4. The branch `es40/reserved-ipr-0x2d` (commits: `eb74087` 0x2d, `f7e2f4d` spin-skip,
   `a5bd43a` capability) **must not merge as-is** -- `0x2d` global regresses DS10.
   Land the capability-scope (task 2) first.
5. **Commit the DecListingSink diagnostic infra** (value-gate cycle-floor + PC-gate) as
   its own reusable commit -- currently uncommitted.
6. **Strip the ES40 probe scaffolding** (`Ev6Translator.h` Hook A/C under
   EMULATR_BRINGUP_PROBES) once the frontier work is settled.
7. Decide disposition of the uncommitted `CpuState.h` unalign revert (task 1 gates it).

### ES40 frontier (parallel, from the same session)
8. With spin-skip, ES40 advances to the native console region `0x62xxx`; give the
   **bounded 63x DtbMissDouble burst** at PAL `0x8321`/`0x8591` (self-resolves) one
   characterizing glance -- legit TB-fill sweep vs VPTB=0 mechanism resurfacing.
9. Identify the **`0xefefefef` memory-dump** diagnostic that fires ~2M lines when ES40
   reaches the console-region frontier (separate from spin-skip; not yet chased).
10. Confirm ES40 reaches `>>>` (only reached `0x62xxx`/console code so far).

### Tooling / follow-on
11. **Spin-skip v2**: nested-inner-countdown skip behind an explicit synchronous-CSR
    clause (the `0x151d0`-in-a-side-effectful-body case). v1 correctly refuses it.
12. Add DS10 (and ES40) boot-to-`>>>` as an explicit regression gate before widening
    any CPU-global fix -- the missing step that let 2a/2b land.

### Pre-existing deferred (CLAUDE.md, unchanged)
13. PCI device enumeration + on-board device models.
14. `Ev6Translator` reference harvest (3-level HW walk + fault ordering).
15. Snapshots (after `>>>`), host-native decompression alternate path, `S_PalLinux`
    codegen, ini->platform.json SSOT refactor.

## 5. Reference

- Regression bisect + poll analysis: this journal.
- `0x2d` root + oracle byte-compare: `journals/ACV_Superpage_Enable_Probe_20260705.md`
  sections 11-12.
- Axis model + capability primitive: `journals/20260705_platform_axis_classification.md`.
- Memory: [[ds10-regression-from-es40-updates]], [[es40-acv-reserved-ipr-0x2d]],
  [[platform-axis-and-capability-gating]], [[spin-skip-optimizer]].
