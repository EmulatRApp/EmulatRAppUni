# HWRPB Region — Two-Gate Hand-off Plan (consuming/preparing for OS boot)

**Date:** 2026-06-28 (Mac / Intel)
**Status:** PLAN. No code landed this session — this captures the agreed next-steps
sequence so the next live session executes it directly.
**Predecessors (read first):**
- `journals/HWRPB_Region_Fidelity_and_Resume_20260624.md` (methodology + §8 resume plan)
- `journals/20260625_hwrpb_scan_instrument_and_mac_build.md` (instrument BUILT; live dump;
  region top-level map in its §9)

---

## 0. TL;DR

The HWRPB region is the firmware→OS hand-off contract. We have located it (PA 0x2000,
single SRM-built copy) and mapped its **top-level directory**. The next progression toward
**consuming/preparing the region for OS boot** is to recognize the bifurcation is
**TEMPORAL** and build the missing half:

- **Gate A — `P00>>>` (HAVE IT):** `EMULATR_HWRPB_SCAN`, the *console-idle* snapshot.
- **Gate B — `boot` hand-off (NEXT):** the region as the OS bootstrap actually inherits it.

The OS never consumes the `>>>` image; it consumes the HWRPB **as it stands at `boot`**,
after the SRM finalizes the chained structures and sets per-CPU bootstrap state. Plan:
deepen Gate A to field-by-field (MEMDSC first) → stand up Gate B → diff A↔B (that delta IS
"preparing for OS boot") → lock in a boot-time validator.

---

## 1. Where the gates are NOW

- **Gate A built:** `EMULATR_HWRPB_SCAN` in `systemLib/Machine.cpp` (`scanGuestForHwrpb()`,
  poll in `systemTick()` at the `kStopPollMask` block; sentinel resolved in `run()`).
  Trigger at `>>>`: `set sys_serial_num <marker>` → `touch EMULATR_HWRPB_SCAN`. Locates the
  HWRPB two ways (pattern + self-pointer/"HWRPB" signature) and hexdumps the header.
- **`EMULATR_PA_WATCH=<hex|dec>` built:** address-keyed store-watch in `pipelineLib/MemDrainer.h`
  (~line 843), prints PC/PA/VA/size/value/palMode/RA on any store overlapping the watched
  quadword. Currently aimed at SYSVAR (0x2058) for the badge; **reusable as the Gate-B trigger.**
- **Region top-level directory mapped** (live 0x2000 header — see 0625 journal §9):
  header → TBB(0x2140) → 2×CPU slot(0x2180,0x2400; stride **0x280**) → CTB(0x2680) →
  CRB(0x27e0) → MDDT(0x2840) → DSRDB(0x2ac0) → CDB(0x38880) → FRU/GCT(0x3ff32000).
- **Known divergences already on the ledger:** (a) SYSVAR=0x405=member 1 → "AlphaPC 264DP"
  badge; (b) per-CPU slot stride **0x280** live vs spec **0x400** (`Hwrpb.h` PerCpuSlot).

---

## 2. Step 1 — DEEPEN Gate A: field-by-field, MEMDSC FIRST  *(immediate next action)*

Directory ≠ validation. The two divergences above prove field-level fidelity is NOT
guaranteed, so we cannot honestly "prepare for OS boot" until each consumed structure is
checked vs spec.

**Add `EMULATR_DUMP_PA=<pa>[:<len>]` (comma/space-separated list) to `scanGuestForHwrpb()`** —
the `hexdump` lambda already exists in that function; just parse the env and call it per
region. Default len 256.

**Priority target = MDDT/MEMDSC @ PA 0x2840** — the single most boot-critical structure: the
OS reads it to learn physical memory geometry; wrong clusters = OS can't map memory = silent
boot failure. Decode field-by-field, don't just hex. Spec (`deviceLib/Hwrpb.h`):

`MemoryDescriptor`:
- +0  `checksum` (sum of all clusters)
- +8  `reserved`
- +16 `cluster_count`
- +24 `cluster[3]`, each `MemoryCluster` = 56 B (7 qwords):
  - +0 `start_pfn`, +8 `pfn_count`, +16 `test_count`, +24 `bitmap_va`,
    +32 `bitmap_pa`, +40 `bitmap_checksum`, +48 `usage`

Alpha page size = 8 KB (0x2000). **Validation: Σ(pfn_count) × 0x2000 must equal configured
RAM (4 GiB).** Add a dedicated MEMDSC decoder that prints each cluster + the byte total and
flags any mismatch vs configured mem. (Get configured size from the chipset/GuestMemory —
confirm the API; that lookup was the next thing to check when this session paused.)

Then walk the rest with the same dump: **per-CPU slot 0** (resolve 0x280 vs 0x400 against
apisrm `hwrpb_def` — the OS indexes the slot array by this stride, so a wrong stride is a
landmine), **CTB/CRB** (console-callback PVs the OS invokes for early I/O), **DSRDB**.

---

## 3. Step 2 — STAND UP Gate B: the `boot` hand-off capture

Reuse the `EMULATR_PA_WATCH` machinery, but point it at the **per-CPU slot bootstrap-in-
progress (BIP) / state field** (PerCpuSlot `state` @ slot+0x080 per `Hwrpb.h`). When the SRM
sets BIP and transfers control to the secondary bootstrap, fire the **same region dump** used
by Gate A. That dump is the "consuming" snapshot — the contract state the OS inherits.
Alternative/confirming trigger: detect PC leaving firmware image space (runtime VA ≥ 0x8000,
i.e. file_off + 0x8000 map) into the loaded bootstrap. Prefer the BIP store-watch (reuses
existing seam, deterministic).

---

## 4. Step 3 — BIFURCATE: diff Gate A vs Gate B

Whatever the SRM mutates between `>>>` and `boot` — per-CPU PCBB/HWPCB, restart block,
BIP/console-control flags, any MEMDSC finalization — **is** "preparing the region for OS
boot." Capture that delta; it's the real hand-off contract and becomes the **golden image**.

---

## 5. Step 4 — LOCK IT IN: boot-time HWRPB validator

Extends the P1/P2 latch philosophy. Assert at Gate B: header self-pointer + identifier;
MEMDSC clusters sum to configured RAM; CTB/CRB well-formed; per-CPU slot stride/flags sane;
SYSVAR/SYSTYPE in range. Regression guard that keeps the region correct as we push toward an
actual OS boot.

---

## 6. The badge, in its place

The 264DP/SYSVAR badge (PA_WATCH→Ghidra at image off PC−0x8000, or the cheap console-capture
for "Defaulting system type to AlphaPC 264DP") is **ONE ledger item, not the OS-boot critical
path.** Run it in parallel if convenient; it does NOT block the hand-off work. The reframe
(region-as-contract over banner-patch) stands.

---

## 7. Concrete immediate action (when code resumes)

1. Confirm the configured-RAM accessor on `Machine`/`GuestMemory` (size/capacity API).
2. Add `EMULATR_DUMP_PA=<pa>[:<len>]` parse + per-region `hexdump` to `scanGuestForHwrpb()`.
3. Add the MEMDSC decoder; dump **MDDT @ 0x2840**, validate Σpfn×8K == 4 GiB.
4. Rebuild (`./scripts/build_mac.sh Release`), boot DS20 cold, at `>>>`
   `set sys_serial_num <marker>`; `EMULATR_DUMP_PA=0x2840:0x100 touch EMULATR_HWRPB_SCAN`
   (or set the env before launch). Read the cluster decode.

All git/integrity NATIVE only (sandbox FUSE truncates). This journal + memory updated
this session; instrument code is NOT yet written.
