# TASK: Harvest the reference `Ev6Translator` into V4 (eventual integration)

**Date filed:** 2026-07-02
**Status:** DEFERRED / harvest-only. Not drop-in for V4. Sequence AFTER the ES40 R16
backtrace (the ACV root is a garbage pointer, not the translator).
**Reference artifact:** `journals/ref_ev6Translation_struct_20260702.h` (emailed
2026-07-02; 1784 lines; AI-generated reference struct, not compiled against V4).

---

## Why (context)

Investigating the ES40 `kFaultAcv` loop (`LDQ R0,0(R16)` @ pc `0x1b7dd4`,
VA=R16=`0xFFFFFFFF7F827F5F`, Kernel mode) surfaced gaps in the in-tree
`mmuLib/Ev6Translator.h`. The emailed reference translator is **complementary**:
it supplies pieces V4's translator lacks. It is a **harvest source**, NOT a
replacement — different type/dependency surface and its own bugs.

## HARVEST (pull these into `mmuLib/Ev6Translator.h`, adapted to V4)

1. **3-level hardware page-table walk** — `walkPageTable_EV6` (ref lines 253-373):
   8 KB pages, L1/L2/L3 indices, valid-bit + `read64` per level, BusError on mem
   fail. V4's translator has **no HW walk** (it relies on guest PAL TLB refill).
   Adapt: use V4 `GuestMemory::read8`, V4 `AlphaPte`, V4 fault codes.
2. **DTB/ITB PTE register-format converters** — `fromDtbPteRegister`/
   `toDtbPteRegister` + ITB variants (ref 429-552). Needed for `HW_MTPR/MFPR
   ITB_PTE/DTB_PTE` (register bit layout differs from the architectural memory
   PTE). Bit positions are documented in the ref (DTB: URE[12]/SRE[11]/ERE[10]/
   KRE[9], UWE[8]/SWE[7]/EWE[6]/KWE[5], FOW[4]/FOR[3]; ITB shifts +1, read-only).
   VERIFY against V4 `pteLib/AlphaPTE_Core.h` bit names before adopting.
3. **Alignment-before-translation fault ordering** — `translateVA_WithAlignment`
   (ref 759-815) checks `(va & (size-1))` FIRST → raises `Unaligned` before any
   protection check. Directly relevant to the ACV: a 7-mod-8 `LDQ` should trap
   `Unaligned`/`DtbMiss`, not loop on ACV. Reconcile V4's classification order.
4. **VA-form-aware segment decode** — `extractSegment` (ref 1555) uses
   `va_ctl & 0x2` (VA_48) → segShift 46 (48-bit) vs 41 (43-bit). Fixes the
   43-vs-48-bit gap the ACV analysis flagged in V4's `tryKsegTranslate`.

## DO NOT COPY WHOLESALE — the ref's own defects

- `tryKsegTranslate` (ref 1607) still **hardcodes 48-bit SPE field positions**
  (`va>>46/>>41/>>30`) regardless of VA form — internally inconsistent with its
  own VA-form-aware `extractSegment`. If harvesting the kseg path, make the SPE
  decode VA-form-conditional too.
- `walkPageTable_EV6` omits the **mode-based KRE/URE permission check** (only
  FOW/FOR/FOE); its `WalkStatus::ACV` enum path is unused. Permission is left to
  a post-walk `canRead(mode)`. Wire the mode check into the walk or keep V4's
  `applyTlbHit` permission gate.
- Dependency surface is NOT V4: `Ev6SPAMShardManager`, `HWPCB`, `CPUStateView`,
  `PendingEvent`/`makeDTBMissEvent`, `QMutex`, `raiseTranslationFault_inl`.
  Treat as pseudocode to port, not code to compile.

## Segment map (useful reference, ref lines 1529-1543)

```
VA[seg]==00 seg0  (page-mapped, user+kernel)
VA[seg]==01 INVALID (ACV)
VA[seg]==10 kseg  (direct phys map, KERNEL ONLY)
VA[seg]==11 seg1  (page-mapped, KERNEL ONLY)
```
The ES40 fault VA `0xFFFFFFFF7F827F5F` has `VA[47:46]=0b11` = seg1 (page-mapped,
kernel-only) in 48-bit form → it goes through the walk; the ACV is a
PTE-permission / walk outcome, not a kseg gate. (Still downstream of the bad R16.)

## Sequencing

1. FIRST: finish the ES40 R16 data-flow backtrace (`tools/trace_reg_backtrace.py`)
   — find where `0xFFFFFFFF7F827F5F` is computed. That is the ACV root.
2. THEN: harvest items 3 (alignment order) + 4 (VA-form segment) — cheapest, and
   may correct the fault classification that turns the misaligned LDQ into a loop.
3. LATER: harvest items 1 (HW walk) + 2 (PTE reg converters) as V4 grows a real
   page-walk / IPR-driven TLB fill path.
