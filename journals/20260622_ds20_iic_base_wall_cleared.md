# Journal -- 2026-06-22 -- DS20 post-banner wall CLEARED (PCF8584 IIC base)

Project:           EmulatR -- Alpha AXP / EV6 (21264) Emulator (V4)
Architect:         Timothy Peer.  AI collaboration: Claude (Cowork) + claude.ai web.
Scope:             D:\EmulatR\EmulatRAppUniV4\Emulatr (V4 active tree)
ASCII(128) only.

================================================================================
HEADLINE
================================================================================
The DS20 cold-boot "wall" -- a tight spin in the routine at native 0x1ad6xx
(loop RET at 0x1adb60) that had blocked every post-banner boot -- is GONE.
Root cause was a single wrong constant: the PCF8584 IIC controller stub was
registered at the DS10 platform base 0xFFFF0000, but the DS20 v7.3 firmware
maps its IIC into the "low BIOS region of ROM space" at 0xFFF80000.  DS20 IIC
writes missed the stub, fell through to UNHANDLED OUTER WRITE, the polled
iic_init handshake never completed, and the console spun forever.

Fixing the base to be per-model let the DS20 boot sail through PCI enumeration,
the PCI-to-ISA bridge, on-board IDE (Cypress 82C693) enumeration, ISA-table
reinit, and into GCT/FRU construction -- all previously-unreachable ground.
The dva0 option-firmware probe that used to be a ~20-minute polled stall is now
near-instant.  Run stopped only on the --max-cycles cap (halted=false), not a
wall.  >>> not yet reached; budget-limited, not blocked.

================================================================================
HOW WE GOT HERE (the path, including two wrong turns honestly recorded)
================================================================================
1. Symptom presented as a "tight loop on 0x1adb60" + DIVERT-REI MISMATCH spam
   on R4/R5/R20/R21/R23 every ~262K cyc (interval-timer period).

2. FALSE LEAD #1 (Claude/Cowork): read the DIVERT-REI mismatch as a PAL
   shadow-register (I_CTL[SDE]) swap-parity bug on the clock-interrupt path.
   Built a gated swap-ledger probe (PalEntries.cpp sdeLog + Machine.cpp arm +
   tools/diag_sde_swap_trace.sh).  Plausible but it was the SPOTLIGHT, not the
   cause: the interval timer kept interrupting the SAME stuck loop, so the
   checker kept firing on it.  claude.ai web correctly pushed back (briefing
   uploads/20260622_shadow_bank_swap_case_count_briefing.md): the program bank
   recovers next tick (was=0), so it is a checker bank-alignment artifact, and
   the real blocker is an unmodeled device poll.  LESSON: a periodic-interrupt
   diagnostic firing on a stuck loop describes the interrupt, not the stall.

3. FALSE LEAD #2 (Claude/Cowork): on seeing UNHANDLED OUTER WRITE at PCI-mem
   0xFFF80000, matched it to dc287_def.h DC287_ICSR0 0xFFF80000 and called it
   the DEC 21143/DE500 "tulip" Ethernet.  WRONG: that 0xFFF80000 is a CSR0
   RESET VALUE, not an address (coincidence).  Tim supplied the 21143 HRMs;
   the access geometry settled it AGAINST the tulip -- tulip CSRs are 32-bit at
   8-byte stride (CSR9 SROM bit-bang at +0x48), but the log shows byte writes
   to adjacent +0/+1.  LESSON: confirm device identity by ACCESS PATTERN, not
   by grepping for a value that happens to match an address.

4. RESOLUTION: the byte index/data pair at +0 (S0/data) / +1 (S1/control-
   status) is exactly the PCF8584 I2C controller.  EmulatR already had a
   COMPLETE model (deviceLib/Tsunami/IicPcf8584.h: S0/S1, PIN/LRB/BB per the
   NXP datasheet Tim provided, full START/STOP/ACK machine, empty-bus NAK,
   manifest-driven devices).  The ONLY defect was the registration base.
   pc264_init.c:43 ("Map I2C controller into low BIOS region of ROM space")
   confirms the IIC is a FIXED platform mapping, per-model, NOT a PCI BAR --
   so a per-model base is architecturally faithful, not a workaround.

5. PROVENANCE: added an env-gated store-watch (EMULATR_IIC_WATCH in
   MemDrainer.h).  It pinned every DS20 IIC write to the generic writeb
   primitive STB r17,0(r16) at PC 0x1ade60, target PA 0x800.FFF8.0000(+0) /
   .0001(+1), stable across all writes.  Value sequence (0x80->S1 init,
   0xc5/0xc0->S1 = ESO|STA|ACK START, data->S0) is textbook PCF8584.

================================================================================
THE FIX (landed, validated)
================================================================================
FILE: chipsetLib/TsunamiChipset.h (ctor, ~636)

Replaced the single hardcoded `registerPciMemRange(0xFFFF0000, 0xFFFF0002,
&m_iic)` with a per-model table + FAIL-LOUD find-or-fail (architect decision:
no silent default -- an unmatched model must announce, not be laundered into
DS10's base):

    static constexpr struct { char const* model; uint64_t base; }
        kIicBaseByModel[] = {
            { "DS10",  0xFFFF0000ULL },   // proven: iic_write_csr      [2026-06-03]
            { "DS20",  0xFFF80000ULL },   // proven: writeb@0x1ade60     [2026-06-22]
            { "DS20E", 0xFFF80000ULL },   // shares DS20 chassis/IIC map (defensive)
        };
    uint64_t const* iicBase = nullptr;
    for (auto const& e : kIicBaseByModel)
        if (m_model == e.model) { iicBase = &e.base; break; }
    if (iicBase) registerPciMemRange(*iicBase, *iicBase + 2, &m_iic);
    else { log "no proven IIC base for <model>; IIC unmapped";
           if (iicBaseRequired(model)/*DS10/DS20/DS20E*/) std::abort(); }

Design notes:
- DS10 is safe because it is an explicit ROW, not a fallback (comment says so).
- iicBaseRequired is INTENTIONALLY narrower than variantFromModel's recognized
  set: ES40/ES45/DS25 are accepted configs whose IIC base is not yet proven, so
  they skip+log (first poke -> UNHANDLED surfaces the real base, the same signal
  that found DS20) rather than hard-stopping at launch (which would brick those
  configs over an unproven device).  Widening the lambda forces proof-before-boot
  if ever desired -- one line.
- CONFIRMS closed against the tree: registerPciMemRange is half-open [start,end)
  (so +2 claims exactly S0,S1); arg is the WINDOW-RELATIVE offset (PA - kBasePA),
  not full system PA; claimant loop runs BEFORE the UNHANDLED fallthrough.

================================================================================
VALIDATION (DS20 cold boot, --max-cycles 0x10000000)
================================================================================
- "TsunamiPchip: registered PCI mem 0xFFF80000-0xFFF80001"  (DS20 matched row;
  closes the m_model=="DS20" [CONFIRM] empirically)
- UNHANDLED OUTER writes: 0  (the storm is gone)
- Console reached (app_output_20260622163459.log):
    banner -> "probing hose 1/0, PCI" -> "probing PCI-to-ISA bridge, bus 1"
    -> "ERROR: ISA table corrupt! Initializing table to defaults" (self-healing)
    -> "bus 0, slot 5 -- dqa -- Cypress 82C693 IDE" (our IDE wiring enumerating)
    -> "*** system serial number not set" -> "initializing GCT/FRU at 3ff32000"
- Stop reason: MaxCyclesExceeded at PC=0xa96e4, fault=5 (kFaultDtbMiss),
  halted=false, cycles=0x10000000.  Soft budget stop, NOT a wall.

================================================================================
NEXT (all downstream, previously unreachable)
================================================================================
- Bigger --max-cycles (0x80000000+) to chase >>>.
- GCT/FRU: watch whether construction completes or hits the known cyclic-link
  set_hang (project_gct_cyclic_link_set_hang, originally seen on DS10).  Running
  the DS10 profile next serves as both the regression check (0xFFFF0000 row
  intact) AND a read on whether that stall is corrected by current fixes.
- SRM ENV/EEPROM Fault-on-Write frontier (SCB vector 0xB0) is the next known
  frontier after console idle.
- CLEANUP (gated, no rush): remove EMULATR_IIC_WATCH (MemDrainer.h) and the
  shadow swap-ledger probe (PalEntries.cpp sdeLog + Machine.cpp arm) -- both are
  REMOVE-WITH markers; flip DecListingSink.h:263 m_emitEnabled back to false.

================================================================================
CREDIT / METHOD NOTE
================================================================================
claude.ai web's adversarial pushback (it is the device, not the shadow swap)
redirected the investigation correctly; Cowork's two false leads were caught by
(a) that pushback and (b) confirming against the tree/datasheet/access-pattern
rather than asserting.  The hybrid workflow -- web for open-ended analysis,
Cowork for tree-grounded edits -- worked exactly as intended here.
