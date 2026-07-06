<!--
ADR-0001
Title:   ES40 SRM ACV-loop root isolation -- Dstream superpage enable-state probe
Status:  PROPOSED  (probe-first; any fix is gated on readout + architect sign-off)
Date:    2026-07-05
Author:  Timothy Peer (architect) / Claude (analysis)
Owner:   Cowork (implementation)

Context:
  ES40 SRM console bring-up. The single remaining gate to P00>>> is the ACV
  loop at guest PC 0x1b7dd4. This ADR defines the diagnostic that SELECTS the
  fix class -- it does not pre-commit a fix. SEQUENCE (per CLAUDE.md): this is
  the ES40 R16-backtrace gate; the Ev6Translator harvest (3-level HW walk +
  fault-ordering) and PCI enumeration / on-board-device models remain DEFERRED
  behind it and are OUT OF SCOPE for this ticket.

Authoritative sources (mounted under Processor Support/):
  ev6_osf_pc264_pal.mar : 3961-3972 (console clears VPTB, sets VA_CTL.VA_48),
                          3994-3996 (P_MISC<63> PHYS = "indicate 1-1 mapping")
  ev6_osf_pal.mar       : 93-94   ("Set both spe modes so we can access i/o
                                    and have normal unix superpage mode"),
                          2644-2649 (DTBM_SINGLE software 1-to-1 escape is
                                    NOP'd out in the srm_console build),
                          2615+    (DTBM_SINGLE handler body)
  EV6_Specification_Rev_2.0_199604.txt : 7533-7547 (SPE VA->PA decode)
  ev6_defs.mar          : 808-810  (I_CTL.SPE = bits[5:3]),
                          2108-2110 (M_CTL.SPE = bits[3:1]),
                          287       (VA_CTL.VA_48 = bit 1)

V4 code under test:
  mmuLib/Ev6Translator.h    : tryKsegTranslate 132-181; translateData 258-323;
                              applyTlbHit 210-235
  mmuLib/TranslationResult.h
-->

# ES40 SRM ACV-Loop Root Isolation -- Dstream Superpage Enable-State Probe

## 1. Objective

Determine, with one static check plus one instrumented boot pass, WHY the
console's 1-1 superpage data access lands in the DTB-miss path (and therefore
in the ACV loop at 0x1b7dd4) instead of being satisfied by `tryKsegTranslate`.
Then land the single fix that the readout selects. Do not land a fix before the
readout selects a branch (see section 7).

## 2. Confirmed mechanism (do not re-litigate)

The ACV loop is fully explained by this chain, and every step is already
observed or verified:

1. The console runs in a 1-1 physical / superpage regime. On console entry the
   guest PAL clears VPTB and sets `VA_CTL.VA_48`, then sets `P_MISC<63>` (PHYS,
   "indicate 1-1 mapping"). VPTB=0 is INTENTIONAL, not a collapse.
   [ev6_osf_pc264_pal.mar 3961-3972, 3994-3996]

2. The srm_console PAL build has NO software 1-to-1 fallback in the DTB-miss
   handler. In `DTBM_SINGLE` the `blt p_misc, trap__d1to1` escape is gated
   `.if eq srm_console` and is NOP'd out in the srm build. The console therefore
   depends ENTIRELY on hardware superpage (SPE) translation.
   [ev6_osf_pal.mar 2644-2649; reset sets both SPE modes at 93-94]

3. In V4, a console superpage access reaches `translateData`, is not
   S_PhysAddr (normal LDx/STx, so no upstream bypass), passes the canonical
   check, and calls `tryKsegTranslate(va, cpu.mode, cpu.m_spe, ...)`. That call
   returns `NotKseg`, so control falls to `cpu.dtbMgr.lookup` -> miss ->
   `DtbMiss`. [Ev6Translator.h 303-322]

4. The MEM drainer maps `DtbMiss` -> `kFaultDtbMiss`; the guest PAL DTB-miss
   vector walks the VPTE with VPTB=0, reads a firmware CODE word (0x403bfc13),
   and installs it into V4's DTB via HW_MTPR DTB_TAG0/PTE0 (wired in C2b).

5. The access retries. This time `cpu.dtbMgr.lookup` HITS the garbage entry;
   `applyTlbHit` runs the permission check; 0x403bfc13 has bit0=1 (looks valid)
   but KRE=0, so `!pte.canRead(mode)` -> `AccessViolation`. [Ev6Translator.h 220]

6. ACV delivered; the console retries the same PC; loop at 0x1b7dd4.

The ONLY unknown is step 3: WHY does `tryKsegTranslate` return `NotKseg` for a
VA that should be a superpage hit? That is what this probe answers.

## 3. Leading hypothesis (`_PROVISIONAL` -- do not code against it yet)

`tryKsegTranslate` (Ev6Translator.h 132-181) is present and its VA patterns
match the CPU spec (SPE[2]: VA<47:46>==0b10; SPE[1]: VA<47:41>==0x7E;
SPE[0]: VA<47:30>==0x3FFFE). The decode is not the suspect. The suspects are the
INPUTS to it:

  H1 `_PROVISIONAL`: `cpu.m_spe` is 0 at the faulting access -- the guest's
     HW_MTPR to M_CTL (SPE enable) is not being reflected into `cpu.m_spe`
     (never wired, or the write path does not update it), so every branch of
     `tryKsegTranslate` is skipped and every access falls to DtbMiss.

  H2 `_PROVISIONAL`: `cpu.m_spe` is populated but SHIFTED WRONG. M_CTL.SPE is
     bits[3:1] and I_CTL.SPE is bits[5:3] (different shifts). If the extraction
     uses one shift for both, or masks the raw field without shifting, the bit
     that `tryKsegTranslate` tests (spe & 0x4 for SPE[2], etc.) is the wrong bit.

  H3: `cpu.mode` is not Kernel at the access. `tryKsegTranslate` returns
     AccessViolation for a kseg/superpage-shaped VA in non-kernel mode. If the
     console's CM=kernel (P_MISC, set at ev6_osf_pc264_pal.mar 3989) is not
     reflected into `cpu.mode`, a superpage VA yields ACV directly -- a DIFFERENT
     ACV path than section 2 (no DtbMiss first). The probe distinguishes them.

  H4: the console's 1-1 VA is genuinely not superpage-shaped (matches none of
     the three SPE patterns). Least likely given the PAL, but must be ruled out,
     not assumed away. If true, escalate to architect -- it implies a
     regime/VA-form question, not a translator fix.

## 4. Phase 0 -- static wiring check (do this FIRST; it may resolve H1/H2 with no run)

4.1  Locate where `cpu.m_spe` and `cpu.i_spe` are assigned. Expected: the
     HW_MTPR handler leaf for M_CTL and I_CTL.
       grep -rn "m_spe" coreLib/ palBoxLib/ mmuLib/
       grep -rn "i_spe" coreLib/ palBoxLib/ mmuLib/
       grep -rniE "M_CTL|I_CTL" palBoxLib/grains/   (find the mtpr leaves)

4.2  Confirm the extraction shift against ev6_defs.mar:
       cpu.m_spe MUST equal  (m_ctl >> 1) & 0x7    (M_CTL.SPE = bits[3:1])
       cpu.i_spe MUST equal  (i_ctl >> 3) & 0x7    (I_CTL.SPE = bits[5:3])
     Record the actual expressions found. A shared shift, an unshifted mask, or
     a missing assignment is a confirmed defect (H2 or H1) -- report it and STOP
     for architect sign-off before editing (section 7).

4.3  Confirm there is a code path in which the guest actually WRITES M_CTL/I_CTL
     SPE during ES40 boot. If no HW_MTPR M_CTL with SPE bits ever executes, H1
     is a guest-path problem (the SPE-enable instruction is not running in V4),
     not a wiring problem -- capture the last M_CTL write seen (value + guest PC)
     via the Phase 1 probe.

If Phase 0 conclusively identifies H1 or H2, you may skip Phase 1 and go
straight to section 7 with the finding. If Phase 0 is inconclusive, run Phase 1.

## 5. Phase 1 -- dynamic regime-capture probe

Add a compile-gated probe behind `EMULATR_ACV_PROBE`. It captures the full
translation regime at the two moments that matter, filtered so the log is not
flooded. Remove the probe after readout (section 7).

### 5.1 Hook A -- in `translateData`, immediately before `return DtbMiss;` (Ev6Translator.h ~322)

Fire when the access is a kernel-stream Dstream miss OR the VA is superpage-
shaped, so H3/H4 are not filtered out. Log, as one ASCII row:

  cyc, guestPC (cpu.pcAddr()), va (raw, 64-bit hex),
  cpu.mode, cpu.va_ctl, isCanonicalVA(va, cpu.va_ctl),
  cpu.m_spe, cpu.i_spe,
  m_ctl_raw, i_ctl_raw           (if reachable from CpuState; else mark n/a),
  spe_shape = which SPE window va falls in, computed independently of cpu.m_spe:
      SPE2 if (va>>46)&0x3 == 0x2
      SPE1 if (va>>41)&0x7F == 0x7E
      SPE0 if (va>>30)&0x3FFFF == 0x3FFFE
      none otherwise,
  kseg_result = the TranslationResult that tryKsegTranslate(va, cpu.mode,
      cpu.m_spe, tmp) returned on this access (Success/NotKseg/AccessViolation).

### 5.2 Hook B -- in `applyTlbHit`, at the DataRead `AccessViolation` return (Ev6Translator.h ~220)

Fire on the retry ACV. Log:

  cyc, guestPC, va, pte.raw (expect the 0x403bfc13-class code word),
  pte.pfn(), pte.canRead(mode), cpu.mode.

This confirms the retry is hitting the garbage code-word PTE and ties the ACV to
the DtbMiss captured by Hook A at the same VA.

### 5.3 Run

One cold-boot 4 GB pass (four 1 GB Tsunami AAR arrays) to the ACV. Capture the
first ~20 Hook-A rows and the first ~20 Hook-B rows. The faulting VA from the
0x1b7dd4 loop should appear in both.

## 6. Readout -> fix branch (architect selects; do not auto-apply)

Read the row for the faulting VA:

  * `spe_shape != none` AND `cpu.m_spe` lacks the matching bit  -> H1.
    Fix: wire the M_CTL SPE write into `cpu.m_spe` as (m_ctl>>1)&0x7 (and the
    I_CTL write into `cpu.i_spe` as (i_ctl>>3)&0x7). Confirm the guest actually
    issues the M_CTL SPE-enable before the first console superpage access.

  * `spe_shape != none` AND `cpu.m_spe` has bits but in the wrong position  -> H2.
    Fix: correct the extraction shift in the M_CTL/I_CTL mtpr leaf; the two use
    DIFFERENT shifts (M_CTL bits[3:1], I_CTL bits[5:3]).

  * `kseg_result == AccessViolation` at Hook A (no DtbMiss first) AND
    `cpu.mode != Kernel`  -> H3.
    Fix: propagate P_MISC CM=kernel (console entry) into `cpu.mode`. This is a
    mode-tracking fix, not a translator fix.

  * `spe_shape == none` (VA matches no SPE window)  -> H4.
    STOP. Report the raw VA to the architect. This is a regime / VA-form
    question (why is the console's 1-1 VA not superpage-shaped?), not a
    translator edit. Cross-read against ev6_osf_pc264_pal.mar console-entry.

  * `isCanonicalVA == false`  -> the VA is non-canonical; the surfaced fault
    would be NonCanonical, not ACV. Inconsistent with the observed ACV -- report,
    do not edit.

## 7. Constraints and sign-off (house rules)

- ASCII-only in all artifacts and log rows.
- The probe is compile-gated (`EMULATR_ACV_PROBE`) and REMOVED after readout;
  it must not remain in the default build.
- No page-walker, TLB-capacity, or eviction work in this ticket -- that is the
  deferred Ev6Translator harvest and is OUT OF SCOPE. (Eviction was Code's
  candidate 3; it is not reachable here because the console superpage path
  should never consume a DTB entry in the first place.)
- Do NOT land any Phase-2 fix until the readout selects a branch AND the
  architect signs off. Report the Phase 0 finding and the Phase 1 rows first.
- Mark any value not confirmed against a primary source `_PROVISIONAL`.

## 8. Acceptance criteria (after the selected fix)

1. For the faulting VA, `tryKsegTranslate` returns Success with the expected PA
   (PA<43:13> = VA<43:13> for an SPE[2] hit), and `translateData` returns
   Success without entering the DTB-miss path.
2. The 0x1b7dd4 ACV loop clears; record the NEW frontier PC (a new blocker on
   the way to P00>>> is expected and is progress, not a regression).
3. No regression on the already-passing DS10 V7.3-2 path to >>>.
4. The `EMULATR_ACV_PROBE` instrumentation is removed from the default build.

## 9. Findings 2026-07-05 (Phase 0 static + Phase 1 M_CTL capture)

Refines section 3.  Provisional block above preserved as-authored; this section
records what the probes settled.

### 9.1 Phase 0 -- write-handler wiring is CORRECT (H5 raised and killed)

A new hypothesis H5 was raised: V4's M_CTL/I_CTL write handler masks/drops the
SPE field on store (a read-modify-write or write-mask excluding bits[3:1] would
land a correct 0x28 source as 0x20, matching the observation).  KILLED by
inspection of the handler:

    // PalEntries.cpp HW_M_CTL leaf
    c.cpu->m_ctl = c.opB;                              // full store, NO mask
    c.cpu->m_spe = static_cast<uint8_t>((c.opB>>1)&0x7u);
    // PalEntries.cpp HW_I_CTL leaf
    c.cpu->i_ctl = c.opB;                              // full store, NO mask
    c.cpu->i_spe = static_cast<uint8_t>((c.opB>>3)&0x7u);

The store is the entire source operand -- no mask, no RMW.  Shifts match
ev6_defs.mar (M_CTL.SPE=bits[3:1] shift 1; I_CTL.SPE=bits[5:3] shift 3).  So the
STORED value equals the Rb source operand, and `m_spe` is a faithful projection
of it.  **H5 dead; H2 (wrong-shift projection) also dead** -- `m_spe=0` is the
correct projection of the observed `M_CTL=0x20`, which genuinely has SPE=0.  The
SPE bit is lost UPSTREAM of the store, in the PAL SPE-computation as V4 runs it.

### 9.2 Phase 1 -- the single M_CTL write (Hook M)

    ACVPROBE MCTL-WRITE cyc=248653107 pc=0x0000000000013edd val=0x20 m_spe=0

M_CTL written EXACTLY ONCE across the whole boot, and LATE (cyc 248M, during
console execution) -- not during early reset-PAL init.  Decode of 0x20:
SPEC_ST_CONS = bits[5:4] = 0b10 = 2 (the documented default,
`M_CTL__SPEC_ST_CONS__V=2`), SPE = bits[3:1] = 0.  Well-formed, not garbage.

Expected value from the reset PAL for a VA_48 console = **0x28**
(ev6_osf_pal.mar:974-992: `lda p4,2` base SPE<1>, `sll p4,r3,p4` promotes to
SPE<2> when VA_48, `bis` with the `cmoveq` SPEC_ST_CONS default 0x20, `hw_mtpr`
M_CTL); 0x24 for the non-VA_48 case.  Delta observed-vs-expected = exactly
**bit 3, the SPE<2> bit**, while SPEC_ST_CONS (0x20) came through intact.

### 9.3 Narrowed to H1a vs H1b (both under section-2 mechanism)

- **H1a**: pc=0x13edd is a bare/unrelated SMC-config write; the VA_48-derived
  SPE-init block never runs in V4 (consistent with "M_CTL written exactly once,
  and late" -- the early reset-PAL SPE-init M_CTL write is ABSENT).
- **H1b**: pc=0x13edd IS the SPE-init block but V4 mis-executes the
  VA_48->SPE<2> shift chain (prime suspects: `sll p4,r3,p4` shift-count operand;
  the `hw_mfpr I_CTL` VA_48 read landing r3 large so the shift flushes p4 to 0).

## 10. Findings 2026-07-05 (PC-gate ring + Hook A ACV-window) -- SPE HYPOTHESIS REFRAMED

A second capture (PC-gate on 0x13ee1 = one instr past the mtpr; Hook A floored to
the 248M window) resolves BOTH H1a/H1b AND the parent framing.  Net: **the ACV is
NOT a superpage-enable problem.  H1a and H1b are both moot.**

### 10.1 Disassembler caveat (record once)

V4's HW_MTPR listing renders the source as `R31` for every mtpr, but EXECUTION
uses the correct GPR.  Proof: `0x13ec5 HW_MTPR R31,I_CTL` -> Hook logged
I_CTL=0x340387, which is R01 (loaded 0x13ebd `LDA R01,0x387(R01)`, R01=0x340000).
So the listing's Ra field is cosmetic-wrong; trust the Hook values, not the `R31`.

### 10.2 0x13edd is NOT the SPE-init (H1a/H1b dissolved)

Ring at the M_CTL write:

    0x13ed1  LDA R01,0x0004      R01 = 0x4
    0x13ed5  LDA R03,0x0020      R03 = 0x20
    0x13ed9  HW_MTPR (R01),IPR(0x5f10)   <- R01=0x4 goes to IPR 0x5f10, NOT M_CTL
    0x13edd  HW_MTPR (R03),M_CTL=0x20    <- R03=0x20; SPE=0 is CORRECT for this write

The `0x4` (an SPE field value) and the `0x20` (SPEC_ST_CONS) go to TWO DIFFERENT
IPRs.  M_CTL legitimately gets 0x20 (SPE off) here -- this is a routine Mbox
config, not the SPE-enable.  So debating whether V4 mis-shifts a VA_48->SPE<2>
chain (H1b) or never reaches it (H1a) is beside the point: this block was never
the SPE-enable.

### 10.3 The faulting VAs are NOT superpage-shaped -> SPE is not the lever (H4, but bigger)

Hook A over the 248M ACV window (mode=0=Kernel confirmed, VA_types.h:72):

    cyc 248146402  va=0x00000000008b0000  shape=none  m_spe=0  canon=1
    cyc 248360047  va=0x00000000003f8000  shape=none  (memory-sizing sweep,
    cyc 248450181  va=0x00000000003fa000  shape=none   +0x2000/step)
    cyc 248540315  va=0x00000000003fc000  shape=none
    cyc 248630449  va=0x00000000003fe000  shape=none
    cyc 248655187  va=0xffffffffffffffc0  shape=none  canon=1   (= -0x40)
    cyc 248655196  va=0x00000001fffffff8  shape=none  -> FAULT[1] kFaultDtbMissDouble
                                                         pc=0x8321 op=0x1b (PAL HW_LD)
    cyc 248655687  va=0x0000000000000090  shape=none  va_ctl=2  (= +0x90)
    cyc 248655781  va=0x00000000001b8b20  shape=none  va_ctl=2

NONE match any SPE window (S2/S1/S0 all 0).  Turning SPE on would not translate a
single one of these -- they are ordinary low/wild kernel VAs, not kseg superpage
VAs.  The fault is **kFaultDtbMissDouble** (kernel VA misses DTB -> PAL DTB-miss
handler fetches the PTE at a VPTB-derived address 0x1fffffff8 -> that fetch ALSO
misses -> double), NOT a superpage AccessViolation.

### 10.4 Reframed root (aligns with the pre-existing garbage-pointer finding)

The wild VAs `0xffffffffffffffc0` (= base 0 - 0x40) and `0x90` (= base 0 + 0x90)
are classic NULL/garbage-base accesses -- consistent with CLAUDE.md's
"ES40 ACV loop root is a garbage pointer, not the translator" and the
es40-4gb-sizing memory note "kFaultAcv fork is operand/loop, not sizing; only open
datum = runtime R00/R02 @ 0x5afac."  The translator is behaving correctly: these
VAs genuinely cannot translate (no page table, VPTB=0, not superpage-shaped).  The
defect is UPSTREAM -- a base register holding 0/garbage when the access is issued.

DECISIVE NEXT PROBE (proposed, not yet run): capture the ISSUING instruction for
the first wild VA (e.g. 0xffffffffffffffc0) -- its PC, base register index, and
the base value + where that register was last written.  That base register's
garbage source is the real ACV root.  Superpage instrumentation (Hooks A/B, the
M_CTL/I_CTL probes) can be retired; the SPE ADR framing (sections 3-8) is
superseded by this section for the ES40 case.

Decider IN FLIGHT: a value-gate ring dump (gate=0x20, cycle-floor 248653050,
EMULATR_TRACE_WINDOW so the 16-deep lookback ring is maintained at NOTRACE speed)
dumps the SPE-computation instruction history + live register values at the
producing retire, and the `palMode` flag at pc=0x13edd tells whether that PC is
even PAL.  Result to be appended.

## 11. ROOT CAUSE CONFIRMED 2026-07-05 -- missing Tbox serial-line IPR SL_XMIT (0x2d)

The Hook C capture (VA-gate on 0xffffffffffffffc0) + fault-log correlation
resolves the whole cascade to a single V4 gap.  The NULL base of section 10 is a
DOWNSTREAM symptom, not the root.

### 11.1 The fault chain (every link verified)

    cyc 248653133  pc=0x13f45  encoded=0x77e72d40  op=0x1d (HW_MTPR)
        scbd = (encoded>>8)&0xFF = 0x2d  ->  IPR index 0x2d
        V4 HW_IPR enum stops at HW_C_SHFT=0x2c; no case for 0x2d
        -> dispatch default -> kFaultUnimplemented  (FAULT[0], the FIRST fault
           of the entire boot -- 248M cyc clean before it)
    -> PAL unimplemented/OPCDEC fault handler runs (Hook C GPR dump at the
       downstream access shows R23=0x13f45 = the saved faulting PC being processed)
    cyc 248655187  pc=0xd94c   load, base = 0, disp = -0x40  ->  va=0xffffffffffffffc0
        not superpage-shaped, VPTB=0, no page table -> DtbMiss
    -> PAL DTB-miss handler PTE-fetch at pc=0x8321/0x8591 (op=0x1b HW_LD)
       va=0x1fffffff8 -> DtbMiss again -> kFaultDtbMissDouble, 62x, looping
       (this IS the "0x1b7dd4 ACV loop")

### 11.2 What IPR 0x2d is

EV6 Rev 2.0 spec (EV6_Specification_Rev_2.0_199604.txt) IPR-index table, after the
Mbox/Cbox block:
    M_CTL 0x28 / DC_CTL 0x29 / DC_STAT 0x2a / DATA(C_DATA) 0x2b / SHIFT_CONTROL(C_SHFT) 0x2c
    Tbox IPRs:  SL_XMIT (w)   <- 0x2d
                SL_RCV  (r)   <- 0x2e
SL_XMIT / SL_RCV are the on-chip serial-line transmit/receive registers.  The SRM
writes SL_XMIT here as part of normal early init (Mbox config at 0x13edd, then
serial-line init at 0x13f45).  This is a normal-path write; V4's missing Tbox
serial IPR is the sole true domino.  (The disasm's "HW_MTPR R31" Ra field is the
cosmetic bug of section 10.1; the fault classification does not depend on the
source value, only on the unimplemented IPR index.)

### 11.3 Fix

Add SL_XMIT (0x2d) and SL_RCV (0x2e) to coreLib/HW_IPR.h and the HW_MTPR/MFPR
dispatch in PalEntries.cpp.  Minimal unblock: silent-accept the SL_XMIT write
(no-op, as V4 already stubs HW_CC_CTL) and return a benign ready/zero for SL_RCV
reads.  Optional follow-up: wire them to the console serial device if the SRM
drives console I/O through the on-chip serial line at any later point.

ACCEPTANCE: the kFaultUnimplemented at 0x13f45 and the entire DtbMissDouble storm
vanish; boot advances past cyc 248.65M to a NEW frontier (expected, = progress).
The SPE machinery (sections 3-10) and its probes are confirmed innocent for this
loop and can be retired once the new frontier is characterized.

## 12. CORRECTION 2026-07-05 (SUPERSEDES 11) -- 0x2d is NOT SL_XMIT

Section 11 is left intact above as the record of a misread; this section corrects
it against the authoritative 21264 HRM (Processor Support/21264-Tuft_univerisity_hrm.txt).

### 12.1 What the HRM actually says

- SL_XMIT = I_CTL[13] (WO, "drives a value on SromClk_H"); SL_RCV = I_CTL[14] (RO).
  HRM lines 10039-10040.  They are BIT-FIELDS of I_CTL (IPR index 0x11), NOT
  standalone IPR indices.
- The IPR index table ENDS at C_SHFT = 0010 1100 = 0x2c (HRM line 9046), then
  goes to sec 5.1 Ebox IPRs.  There is NO 0x2d / 0x2e IPR index -- both are
  UNASSIGNED.
- HRM sec 11.2.2: after the SROM load, the three serial pins (SromClk_H /
  SromData_H / SromOE_L) become a SOFTWARE UART -- a diagnostic terminal driven
  by bit-banging I_CTL[SL_XMIT] (clock/out) and sampling I_CTL[SL_RCV] (in).

### 12.2 What Section 11 got wrong, and why

Section 11 read the EV6 Rev 2.0 spec's register-continuation rows ("SL_XMIT (w) /
SL_RCV (r)", index column BLANK) as index-table rows and inferred 0x2d/0x2e by
position, then wrote "root cause confirmed" without a byte-compare.  Both the
index inference and the "confirmed" claim were wrong.  The identification
0x2d = SL_XMIT is WITHDRAWN.

### 12.3 What 0x13f45 actually is

Ring capture at the write (PC-gate 0x13f49, EMULATR_BRINGUP_PROBES): 0x13f45 is one
HW_MTPR R31 (write 0) in a COHERENT register-init/clear sweep spanning ~0x13f0d..
0x13f45 -- a series of HW_MTPR R31 to control-register indices with the 21264's
required pipeline-spacing NOPs.  V4 is fetching sensible instructions (NOT garbage,
so no fetch-level divergence is evident).  scbd 0x2d is an UNASSIGNED index the
sweep writes 0 to; real 21264 tolerates a write to an unassigned index (no fault),
V4 wrongly raised kFaultUnimplemented.

### 12.4 Fix status -- reclassified

- KEEP (empirical): silencing the kFaultUnimplemented at 0x13f45 cleared the whole
  DtbMissDouble cascade and advanced the boot ~16M cyc.  Verified.
- CORRECT (semantics): the committed change added FICTITIOUS registers
  HW_SL_XMIT=0x2d / HW_SL_RCV=0x2e.  Those are not registers.  To be replaced with
  honest handling: a write to an unassigned IPR index is a no-op on real HW, so the
  0x2d write should be tolerated as such -- NOT modeled as SL_XMIT.
- REAL WORK (fidelity): the actual SL_XMIT/SL_RCV are I_CTL[13]/[14] and drive the
  on-chip serial line / software-UART diagnostic terminal.  Modeling that is the
  fidelity task the architect selected ("implement the interface correctly").

### 12.5 New frontier + open questions

- 0x14880 spin (PAL mode, ra=0x14304, crawling, no faults): provisionally the
  PALcode software-UART loop sampling I_CTL[SL_RCV] (bit 14), which V4 never
  drives -> samples 0 forever.  Under confirmation via ring capture.
- Open: (a) is the 0x13f0d init sweep the NORMAL path or an error/reset path V4
  entered after an earlier unmodeled serial interaction? (coherent fetch does not
  rule out a wrong branch); (b) full byte-compare of 0x13f40-0x13f48 vs the
  authoritative decompressed image; (c) the on-chip serial-line model itself.
