<!--
EmulatR V4 -- HWRPB Per-CPU SLOT Field Map (AARM ground truth) + Hwrpb.h reconciliation
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-29.
Source authority: AARM Section III "Common Architecture, Console Interface",
Table 26-4 "Per-CPU Slot Fields" (extract supplied by Tim 2026-06-29, from ASA HRM /
alpha_arch_ref.txt). Cross-checked against deviceLib/Hwrpb.h (EmulatR's encoded layout)
and the live DS20 SRM HWRPB dump (memory.md 2026-06-25 entry, region map sec.9).
ASCII(128) only.
-->

# HWRPB Per-CPU SLOT -- AARM Field Map + Hwrpb.h Reconciliation

**Date:** 2026-06-29
**Status:** SPEC GROUND TRUTH captured. One STRUCTURAL DIVERGENCE found in
`deviceLib/Hwrpb.h::PerCpuSlot` (offsets >= +592 and the slot size). Code fix is
specified below but NOT yet applied (gated on the apisrm `hwrpb_def` cross-check and
the two-HWRPB question, tasks 2 / 3).

---

## 0. TL;DR

- The AARM per-CPU slot is defined from **+0 to the Cycle Counter Frequency at +624**
  (a quadword, last byte at +631). Minimum slot span = **632 bytes**.
- The **live DS20 SRM** built its slots with stride **0x280 = 640** (632 padded up to a
  16-byte/octaword boundary). That is the size the OS actually inherits on this platform.
- `Hwrpb.h` matches the AARM **byte-for-byte from +0 to +591**, then **diverges**:
  - it models +592..+767 as an opaque `hwpcb_filler[176]` -- but the AARM has FIVE real,
    OS-read fields there (SW-compat, console data-log PA + length, cache descriptor,
    **cycle-counter frequency**);
  - it embeds a **DSRDB block at slot+0x300..0x400** -- but DSRDB is a SEPARATE top-level
    HWRPB section (pointed to by HWRPB header +312, found live at PA 0x2ac0), NOT part of
    the per-CPU slot;
  - its `static_assert(sizeof(PerCpuSlot) == 0x400)` (1024) is **oversized by 384 bytes**
    and disagrees with both the AARM (632) and the live SRM (640 = 0x280).

**This resolves task 2:** the live **0x280 is correct**; `Hwrpb.h`'s **0x400 is the bug.**

---

## 1. Full field map (slot-relative offsets)

Legend: OK = offset and meaning agree with AARM; ~ = offset agrees, semantic note;
X = divergence.

| Offset (dec / hex) | AARM Table 26-4 field | Hwrpb.h PerCpuSlot member | Status |
|---|---|---|---|
| +0   / 0x000 | HWPCB (128 B; contents per Table 27-10) | `hwpcb` (Hwpcb, 128 B) | OK |
| +128 / 0x080 | STATE FLAGS (Table 26-5 bits) | `state` (SlotState) | OK |
| +136 / 0x088 | PALCODE MEMORY SPACE LENGTH | `pal_mem_len` | OK |
| +144 / 0x090 | PALCODE SCRATCH SPACE LENGTH | `pal_scr_len` | OK |
| +152 / 0x098 | PA OF PALCODE MEMORY SPACE | `pal_mem_pa` | OK |
| +160 / 0x0A0 | PA OF PALCODE SCRATCH SPACE | `pal_scr_pa` | OK |
| +168 / 0x0A8 | PALCODE REVISION (qword, bit-packed) | `pal_rev` u32 + `pal_var` u32 | ~ (1) |
| +176 / 0x0B0 | PROCESSOR TYPE (63:32 minor, 31:0 major) | `cpu_type` | OK |
| +184 / 0x0B8 | PROCESSOR VARIATION (PE / IEEE-FP / VAX-FP) | `cpu_var` | OK (2) |
| +192 / 0x0C0 | PROCESSOR REVISION (4 ASCII) | `cpu_rev` | OK |
| +200 / 0x0C8 | PROCESSOR SERIAL NUMBER (octaword, 16 B) | `serial_no[2]` | OK |
| +216 / 0x0D8 | PA OF LOGOUT AREA | `logout_mem_pa` | OK |
| +224 / 0x0E0 | LOGOUT AREA LENGTH | `logout_length` | OK |
| +232 / 0x0E8 | HALT PCBB | `halt_pcbb` | OK |
| +240 / 0x0F0 | HALT PC | `halt_pc` | OK |
| +248 / 0x0F8 | HALT PS | `halt_ps` | OK |
| +256 / 0x100 | HALT ARGUMENT LIST (R25) | `halt_arglist` | OK |
| +264 / 0x108 | HALT RETURN ADDRESS (R26) | `halt_ret_addr` | OK |
| +272 / 0x110 | HALT PROCEDURE VALUE (R27) | `halt_proc_value` | OK |
| +280 / 0x118 | REASON FOR HALT | `halt_code` | OK (3) |
| +288 / 0x120 | RESERVED FOR SOFTWARE | `reserved_sw` | OK |
| +296 / 0x128 | RXTX BUFFER AREA (168 B; interproc console) | `icba[21]` (168 B) | ~ (4) |
| +464 / 0x1D0 | PALCODE AVAILABLE (16 qword, 128 B) | `palcode_revs[16]` (128 B) | ~ (5) |
| **+592 / 0x250** | **PROCESSOR SOFTWARE COMPATIBILITY FIELD** | `hwpcb_filler[176]` (start) | **X** |
| **+600 / 0x258** | **Console Data Log Physical Address** | (inside filler) | **X** |
| **+608 / 0x260** | **Console Data Log Length** | (inside filler) | **X** |
| **+616 / 0x268** | **Cache descriptor** (assoc / block / size) | (inside filler) | **X** |
| **+624 / 0x270** | **Cycle Counter Frequency** (SCC/PCC per sec) | (inside filler) | **X** |
| +632 / 0x278 | (end of AARM-defined fields; pad to slot size) | -- | -- |
| **slot size** | **0x280 (640)** live SRM = 632 padded to octaword | `static_assert == 0x400` | **X** |
| +768 / 0x300 .. 0x400 | NOT in the slot (DSRDB is its own section, HWRPB+312, live PA 0x2ac0) | `dsrdb_smm` ... `dsrdb_name[7]` | **X** |

### Notes
1. **PALCODE REVISION** is one quadword in the AARM: bits 63:48 = max procs that can
   share this PALcode image, 47:32 = compatibility revision, 31:24 SBZ, 23:16 variation,
   15:8 major, 7:0 minor. `Hwrpb.h` splits the low 32 bits into `pal_rev`/`pal_var` and
   drops the high 32 (max-procs + compat). Offset is right; widen to a packed qword when
   the builder is reworked.
2. **PROCESSOR VARIATION +184** carries the SMP-relevant **PE (Primary Eligible, bit 2)**,
   IEEE-FP (bit 1), VAX-FP (bit 0). PE is exactly the bit the secondary-bring-up work
   (SMP Phase 5) must honor -- the slot says whether a CPU may become primary.
3. **REASON FOR HALT** codes (AARM): 0 boot/start/powerfail, 1 operator crash, 2 kstack
   not valid, 3 invalid SCBB, 4 invalid PTBR, 5 CALL_PAL HALT in kernel, 6 double-error
   abort, 7 machine-check in PALcode. NOTE: EmulatR's `HaltCode` enum (Hwrpb.h lines
   105-115) uses a DIFFERENT numbering (its 5=ScbbNotValid vs AARM 3; its 2=OperatorHalt
   vs AARM 2=kstack). Reconcile the enum against this table when the halt path is touched.
4. The +296 region is named **RXTX BUFFER AREA** (interprocessor console comm, AARM
   Section 26.4); EmulatR calls it `icba`. Same offset/size (168 B) -- rename for clarity.
5. **PALCODE AVAILABLE +464**: per AARM the FIRST quadword (SLOT[464]) is the overall
   **firmware/console revision** field (format keyed by HWRPB[16]: if rev > 6, bits 63:48
   max procs sharing console, 47:32 console build seq, 23:16 variant, 15:8 major, 7:0
   minor). Subsequent quadwords are indexed by PALcode variant. EmulatR's comment marks
   [0] "unused" and [1]/[2] = VMS/OSF -- the [0]/[464] slot is the console rev, not unused.

---

## 2. The structural divergence, stated precisely

EmulatR's `PerCpuSlot` is **correct for the first 592 bytes** and **wrong after**:

- **Bytes 592..631** should be SW-compat (+592), console data-log PA (+600) and length
  (+608), cache descriptor (+616), and **cycle-counter frequency (+624)**. EmulatR buries
  all five inside `hwpcb_filler[176]`. The cycle-counter-frequency field is the one with
  teeth: the OS reads SLOT+624 and only falls back to HWRPB[112] if it is zero -- a slot
  that puts garbage there (or omits it) can hand the OS a wrong PCC rate.
- **The slot size** is 0x280 (640), not 0x400 (1024). The live SRM proves it; the AARM
  field span confirms it.
- **The DSRDB does not live in the slot.** It is a top-level HWRPB section reached via the
  header's dsrdb_offset (+312), present live at PA 0x2ac0. EmulatR embedding a DSRDB at
  slot+0x300 is a category error that also inflates the slot to 0x400.

### Why this is latent today (and when it bites)
The HWRPB the OS will consume is the **SRM-built one at PA 0x2000** (slots = 0x280).
EmulatR's `HwrpbBuilder` (which emits this wrong 0x400 slot) writes a *separate* HWRPB at
PA 0 that may not be the one consumed -- that is the open **two-HWRPB question (task 3)**.
So the bug is dormant **until** either (a) EmulatR's builder becomes the live HWRPB, or
(b) we start validating the SRM's region with `sizeof(PerCpuSlot)` as the stride -- at
which point slot 1 would be read 0x180 bytes too high and every field past +592 would be
misaligned. **Fix the struct before either happens.**

---

## 3. Specified fix for Hwrpb.h (NOT yet applied)

1. Replace `hwpcb_filler[176]` (slot+0x250) with the real tail:
   - `uint64_t sw_compat;        // 0x250 (+592) processor software-compatibility`
   - `uint64_t console_log_pa;   // 0x258 (+600) console data-log physical address`
   - `uint64_t console_log_len;  // 0x260 (+608) console data-log length`
   - `uint64_t cache_descriptor; // 0x268 (+616) assoc/block/size descriptor`
   - `uint64_t cycle_count_freq; // 0x270 (+624) per-CPU SCC/PCC frequency`
2. Remove the embedded `dsrdb_*` members; promote DSRDB to its own struct (a top-level
   HWRPB section, sized from apisrm `apu_hwrpb_def.h` / the live 0x2ac0 dump).
3. Change the slot size: `static_assert(sizeof(PerCpuSlot) == 0x280, ...)` and pad
   explicitly from +632 (0x278) to +640 (0x280) with a named `uint64_t slot_pad;` (or a
   1-qword reserved field), documenting the octaword round-up.
4. Cross-check every offset above against apisrm `apu_hwrpb_def.h` SLOT$ definitions
   before committing (the SDL is the byte-authoritative Digital source; the AARM is the
   architectural one -- they should agree; if not, the live SRM dump breaks the tie).
5. Re-mint any golden HWRPB snapshot used by the boot-time validator (task 5) after the
   struct changes.

---

## 4. What this unblocks

- **Task 2 (slot-size reconcile): RESOLVED in analysis** -- 0x280 correct, 0x400 wrong;
  the fix is section 3 above (code change still pending the apisrm cross-check).
- **Task 1 (field-by-field map): the per-CPU slot half is now done** -- this table is the
  spec column; MEMDSC / CTB / CRB / DSRDB tables come next from the same AARM chapter.
- **Task 5 (validator):** the validator's per-slot checks key off this table -- especially
  PE bit (+184.2), PRESENT/AVAILABLE/BIP state bits (+128), and cycle-counter freq (+624).
- **SMP Phase 5:** PE (+184 bit 2) and the STATE-FLAGS present/available/BIP bits (+128)
  are the secondary-eligibility and start-handshake fields the bring-up will read/write.
