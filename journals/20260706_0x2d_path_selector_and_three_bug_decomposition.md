<!--
Title:  0x2d as path-selector; three-bug decomposition (DS10 / DS20 / ES40)
Date:   2026-07-06
Author: Timothy Peer (architect) / Claude (analysis + experiments)
Status: FINDINGS LOCKED. Supersedes the "ES40 mis-delivers the 0x2d fault" framing
        in 20260706_0x2d_rollback_experiment.md, and the "NULL-base load at 0xd94c"
        narrative in ACV_Superpage_Enable_Probe_20260705.md Section 11.
Depends on: 20260706_0x2d_rollback_experiment.md (the rollback + its DS10/DS20 boot proof)
-->

# 0x2d as Path-Selector; Three-Bug Decomposition (2026-07-06)

## 0. One-paragraph summary

The 0x2d reserved-IPR write is **not** the ES40 boot blocker and its fault delivery
is **not** defective. V4's OPCDEC delivery for the 0x2d write is byte-for-byte
faithful (correct vector, clean saved state, no shadow-bank artifact) and the guest
OPCDEC handler runs and returns cleanly. The 0x2d *handling* (fault vs no-op) merely
**selects which downstream code path** each platform takes, and each path ends at a
**different, independent** V4 defect. There is **no shared root**. Faulting 0x2d
(the current rollback) is therefore a **path-selecting scaffold**, not a fix; it
happens to route DS10/DS20 to `>>>` while routing ES40 into a fault-path artifact.

## 1. The three-bug decomposition (centerpiece — proven)

Each platform, at its respective blocker, bottoms out on a *different mechanism* in
a *different subsystem*. All instruction decodes below are tool-verified from the
raw bits (format-aware: branch = opcode/Ra/disp only, no phantom Rb).

| platform | 0x2d path | blocker PC | instruction | access | outcome | subsystem | real bug? |
|----------|-----------|-----------|-------------|--------|---------|-----------|-----------|
| **DS10** | no-op (faithful) | `0x13d38` | `HW_LD R20,0x1000(R12)` then `BLBS R20,-2` | **physical** (opcode 0x1B, bypasses DTB) | load **succeeds**, spins on bit0 that never clears | **PCI/device model** | **YES** |
| **DS20** | no-op (faithful) | `0x13ec0` | `SUBQ R12,#1` / `BEQ R12` / `BR` | **none** | finite countdown, R12=`0x11E1A300`=300,000,000 | firmware settling delay | **NO — non-event** |
| **ES40** | fault (scaffold) | `0x1b7dd4` | `LDQ R0,0x0(R16)` / `STQ R17,0x0(R16)` | **virtual** (opcode 0x29/0x2D, uses DTB) | R16 holds valid MMIO/config VAs, **DtbMiss** storm | **MMU translation** | **YES** |

Two genuine bugs (DS10 device model, ES40 translation) in two subsystems, plus a
DS20 delay that is merely slow. The mechanisms do not share a precondition:

- **DS10** accesses MMIO **physically** (`HW_LD`) and it *works* — the defect is the
  device never signals ready (bit0 stuck). The address `0x801_2800_0000`(+`0x1000`
  disp) is **Pchip0 PCI memory space** for a device V4 does not model — **not a
  chipset CSR**. Provisional device id pending; the diagnosis (unmodeled Pchip0
  device, read returns a value whose bit0 never clears) is firm.
- **ES40** accesses MMIO/config **virtually** (`LDQ`/`STQ`) and never gets that far —
  the defect is no DTB/superpage mapping for those VAs, so it DtbMisses. R16 holds
  *valid* addresses (`0x801_0000_0000`, `0x801_03f8_0000`, low `0x2xxxx`), **not a
  NULL base** (see Section 4 supersession).
- **DS20** touches no memory in its loop — a hardcoded 300M-count immediate
  (`LDAH R12,0x11e2(R31)` + `LDA R12,-0x5d00(R12)` at `0x13ea0/0x13ea4`) — so it is
  provably a finite settling delay, spin-skippable, not a hang. No register-left-
  wrong failure mode exists (the count is a compile-time literal).

## 2. Delivery is faithful — proven three independent ways

The ES40 `0x1b7dd4` "ACV loop" was long framed as a fault-*delivery* defect. It is
not. At the 0x2d fault (cyc 248653133, pc=0x13f45, enc=0x77e72d40, the SAME word all
platforms issue):

1. **Vector correct.** `palBase=0x8000`, `entry=0x400`, `absVec=0x8400` (OPCDEC).
2. **Saved state clean.** `FDLV-POST: excAddr=0x13f45, newPc=0x8401` — the faulting
   PC is saved correctly; no corruption of excAddr/cause.
3. **No shadow-bank artifact.** `sde1=1` on ALL of ES40/DS20/DS10, but `willSwap=0`
   on all — the 0x2d fault is a *nested* PAL-mode fault (`pal=1` already), so
   `palModeEnter` performs no shadow swap. The live `intReg[]` IS what the handler
   sees; the shadow bank is the non-live copy. FORK-B (unpopulated-bank) is ruled out.

And the handler itself runs cleanly: the ES40 OPCDEC handler's first ~200
instructions are a normal context-save to the `0x60xx` PAL impure area, then it
**returns and continues** into real init (`0x15xxx`, Tsunami CSR writes). It does
**not** immediately cascade. The `0x1b7dd4` DtbMiss storm occurs ~37M cycles later
(cyc ~285M), downstream and independent of the delivery.

DS20 is a valid delivery control (fires the identical fault at pc=0x13655, PALmode,
sde1=1) through the shared handler prefix `0x8400..0x8551`, then the two PAL builds
diverge in *code content* (different firmware) at `0x8555`+ — so DS20 is a valid
*delivery* oracle but NOT an instruction-level oracle for ES40's handler past the
shared prefix.

## 3. 0x2d is a path-selector; the rollback is a labeled scaffold

- Faulting 0x2d: DS10/DS20 → `>>>` (verified boot, rollback journal); ES40 → the
  fault-path artifact `0x1b7dd4`.
- No-op'ing 0x2d: DS10 → its device-model bug `0x13d38`; DS20 → its 300M delay
  (then unknown); ES40 → its faithful-path frontier `0x629f0` (per session-1;
  re-confirm on the no-op path).

Since silicon almost certainly *ignores* a write to an unassigned IPR index, the
**no-op is the more likely faithful behavior**, and the fault is a scaffold that
masks DS10's device bug while manufacturing ES40's `0x1b7dd4`. The rollback stays in
the tree (it makes DS10/DS20 boot, verifiably) but is **kept-and-labeled as a
scaffold**, not asserted as correct. Final disposition is an architect decision and
is coupled: going no-op REQUIRES fixing DS10's device bit first, or DS10 regresses.

**Consequence for next work:** the real bugs are **DS10's unmodeled Pchip0 device**
and **ES40's virtual-MMIO DtbMiss** — both on/near the faithful path. ES40's
`0x1b7dd4` is fault-path archaeology (do not chase it as if it were the faithful
frontier; the faithful ES40 frontier is `0x629f0`).

## 4. Supersessions (so the record cannot re-assert disproven claims)

- **WITHDRAWN — "0x2d = SL_XMIT" (ACV_Superpage_Enable_Probe_20260705.md §11).**
  Already corrected in §12 of that journal; restated here: SL_XMIT/SL_RCV are
  I_CTL[13]/[14], not IPR indices. 0x2d is an unassigned index.
- **SUPERSEDED — "OPCDEC handler does a NULL-base load at 0xd94c (base=0, disp=-0x40
  → 0xffffffffffffffc0)" (same journal, §11.1).** Direct capture this session shows
  the ES40 handler returns cleanly and the actual `0x1b7dd4` DtbMiss uses **R16 =
  `0x801_0000_0000` (a valid Tsunami/Typhoon MMIO address), not zero**. There is no
  NULL base; it is an unmapped-virtual-MMIO translation miss. The `0xd94c`/NULL
  narrative is withdrawn.
- **REFRAMED — "ES40 mis-delivers the 0x2d fault → 0x1b7dd4"
  (20260706_0x2d_rollback_experiment.md).** Delivery is faithful (Section 2 here).
  `0x1b7dd4` is a downstream, fault-path-only translation bug, not a delivery defect.

## 5. Reproduction (scaffolding removed from the tree; described here to re-create)

- **0x2d path toggle:** the throwaway `EMULATR_2D_NOOP=1` env-gate in
  `execHwMtpr` HW_RESERVED_2D made 0x2d a no-op (else fault) so one build reproduced
  both paths. Re-add as a 3-line env check if needed.
- **Diagnostic technique (kept as reusable infra — see the diagnostics facility):**
  PC-window instruction trace (`EMULATR_PCLO/PCHI`), fault-VA + base-register
  miss-watch (`EMULATR_MISSLO/MISSHI`), and last-writer-provenance capture
  (regWriteIdx/regWriteValue gated by value) — this trio read 0x2d, the DS10 poll,
  the ES40 DtbMiss base, and the DS20 R12 constant. Retained as committed infra;
  the FDLV/HDLR one-shot gates were throwaway and removed.

## 6. Next phase (from the consolidated baseline, in priority order)

1. **DS10 device model** — model the unmodeled Pchip0 device at PCI mem
   `0x2800_0000` so its ready bit (bit0) clears; unblocks DS10 on the faithful path.
   Likely unblocks DS20 too if DS20's post-delay frontier is the same poll.
2. **ES40 translation** — the virtual `LDQ`/`STQ` of MMIO/config VAs
   (`0x801_xxxx`, low `0x2xxxx`) that DtbMiss; needs the DTB/superpage mapping for
   that regime. (But confirm on the FAITHFUL path first — `0x629f0`, not `0x1b7dd4`.)
3. **DS20** — one spin-skip run past the 300M delay to reveal its real no-op
   frontier (may be the same device poll as DS10 → the fix unblocks two platforms).
