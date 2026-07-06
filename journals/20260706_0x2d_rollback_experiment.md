<!--
Title:  0x2d no-op rollback -- empirical isolation on DS10 (+ DS20 confirm)
Date:   2026-07-06
Author: Timothy Peer (architect) / Claude (analysis + experiment)
Status: DECIDED -- roll back the 0x2d no-op; restore kFaultUnimplemented.
Supersedes the "tolerate 0x2d write" fix (commit eb74087) and section 12.4
of journals/ACV_Superpage_Enable_Probe_20260705.md.
-->

# 0x2d Rollback -- Empirical Isolation (2026-07-06)

> **REFRAMED later the same day (read first).** This journal's "ES40's SAME fault
> MIS-DELIVERS → 0x1b7dd4" framing is WITHDRAWN. Direct capture proved V4's 0x2d
> fault delivery is faithful (correct vector 0x8400, clean saved excAddr=0x13f45, no
> shadow-bank artifact — sde1=1/willSwap=0 on all platforms) and the OPCDEC handler
> returns cleanly. 0x2d is a **path-selector**, not a delivery bug: faulting vs
> no-op'ing it routes each platform to a *different, independent* downstream defect
> (DS10 device-model poll, ES40 virtual-MMIO DtbMiss, DS20 benign 300M delay). The
> rollback below is therefore a **kept-and-labeled scaffold**, not a fix. Full record:
> `journals/20260706_0x2d_path_selector_and_three_bug_decomposition.md`.

## Decision

**Roll back the unassigned-IPR-index `0x2d` no-op (commit `eb74087`,
`HW_RESERVED_2D`) and restore `kFaultUnimplemented` on both `HW_MFPR` and
`HW_MTPR` to index `0x2d`.** The no-op was the bisected DS10/DS20 regressor and
its justification was uncited and unsupported by the HRM. Direct experiment on
this branch confirms all three premises of the handoff's reversal.

## The three premises, each verified (not inferred)

### 1. HRM ruling on HW_MTPR to an unassigned IPR index

The 21264/EV67 HRM (Processor Support/"Alpha 21264-EV67 ... HRM.txt",
`21264-Tuft_univerisity_hrm.txt`):

- IPR index table **ends at `C_SHFT = 0x2c`** (HRM ~line 9046). `0x2d`/`0x2e`
  are **UNASSIGNED** indices.
- `HW_MFPR`/`HW_MTPR` fields (Table 6-6, HRM ~12180): the index is bits `[15:8]`
  (INDEX) with `[7:0]` = SCBD_MASK. **There is no "function field"** in the
  HW_MxPR format.
- **OPCDEC** is defined as "Illegal opcode or **function field**" and for
  executing PAL-reserved instructions **outside PALmode** (HRM ~11965, ~12587).
  Neither applies to a valid `HW_MTPR` (opcode `0x1D`, in PALmode) with an
  unassigned INDEX.
- The HRM's "writes ignored" language applies to **reserved BIT-FIELDS within a
  register**, NOT to an unassigned IPR **index**.

**Conclusion:** the HRM is **SILENT** on the behavior of HW_MxPR to an
unassigned index. It neither documents a no-op (eb74087's uncited claim) nor an
explicit fault. The behavior is implementation-specific. So the tie-breaker must
be the guest PAL's observed dependency, below.

### 2. DS10/DS20 actually execute a `0x2d` write

Instrumented the `HW_MTPR HW_RESERVED_2D` path (temporary `PROBE2D` stderr line).
DS10 fires it **exactly once**:

    PROBE2D: HW_MTPR idx=0x2d pc=0x0000000000013155 encoded=0x77e72d40 opB=0x0 cyc=186786248

Same encoded instruction word (`0x77e72d40`, HW_MTPR R31 -> index 0x2d, write 0)
that ES40 issues. So the no-op-vs-fault distinction is live on DS10, not just ES40.

### 3. Faulting the write un-regresses DS10 (and DS20)

Same binary, `--no-autoload`, 1 GiB, model from ini:

| 0x2d handling | DS10 outcome |
|---------------|--------------|
| **no-op** (branch `eb74087`) | froze in a DtbMiss loop, **stuck at PC=0x13d38** (PAL) from ~380M cyc to the 402M cap; never left. `lastFault=kFaultDtbMiss`, excAddr=0x600920. |
| **fault** (`kFaultUnimplemented`, restored) | 0x2d fault at ~187M -> PAL handler -> **advanced** through 0x61xxx/0x62xxx into the **0x1c6xxx console region** (by the `>>>` gate BPs at 0x1c6d4c/0x1c6788), cycling varied PCs, no hang, to the 805M cap. Matches the good-baseline "boots past poll -> 0x61xxx". |

The fault path reproduces real DS10 hardware (which boots); the no-op does not.
Since the HRM is silent, the guest PAL's demonstrated reliance on the fault is
the authoritative signal: **fault is the faithful behavior.**

## Why the same write helps ES40 as a no-op but breaks DS10

This is the key asymmetry and it names the *real* ES40 bug. All three platforms
issue the identical `0x2d` write. On DS10/DS20 the fault is delivered cleanly:
the PAL unimplemented/OPCDEC handler runs and returns, boot continues. On ES40
the **same fault mis-delivers** and cascades into the NULL-base `DtbMissDouble`
"0x1b7dd4 ACV loop". eb74087 masked ES40's delivery bug by removing the fault
entirely -- which happened to route ES40 around its own bug while removing the
fault DS10/DS20 depend on.

**Therefore:** the ES40 `0x1b7dd4` ACV is a **fault-DELIVERY** defect (wrong
vector / bad saved state, e.g. R23=0x13f45), NOT a "tolerate the write" problem.
The correct ES40 fix delivers the `0x2d` fault the way DS10's PAL successfully
consumes it. This is the next ES40 task, sequenced UPSTREAM of the IIC /
`build_power_hw` blocker.

## Final code state (this branch, working tree)

- `coreLib/HW_IPR.h`: `HW_RESERVED_2D = 0x012D` retained as a *documented
  unassigned index*; comment corrected (V4 faults it; the no-op claim was uncited).
- `palBoxLib/grains/PalEntries.cpp`:
  - `execHwMfpr` 0x2d: `value=0` -> `r.faultCode = kFaultUnimplemented; return r;`
  - `execHwMtpr` 0x2d: no-op `break` -> `r.faultCode = kFaultUnimplemented; return r;`
- Temporary `PROBE2D` instrumentation removed.

## Consequence / sequencing

Rolling back restores the ES40 `0x1b7dd4` ACV loop (expected). ES40 is now
re-framed as a fault-delivery fix, upstream of IIC. DS10/DS20 return to booting
to `>>>`.
