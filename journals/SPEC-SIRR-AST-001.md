<!--
EmulatR V5 -- Design Spec SPEC-SIRR-AST-001
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# SPEC-SIRR-AST-001 -- SOFTWARE INTERRUPTS + AST DELIVERY (audit PE-4/PE-5)

    Doc id   : SPEC-SIRR-AST-001
    Date     : 2026-07-28
    Status   : FOR ARCHITECT REVIEW -- GATE-1 (design approval) NOT CLEARED.
               NO CODE WRITTEN.  Written while the post-audit DS20 rerun
               was in flight; the rerun's verdict decides priority, not
               content.
    Relates  : JRN-AUD-001 (audit; PE-4/PE-5 are its top open blockers),
               JRN-SCSI-033 (SWPCTX landing), PE-2 PCTX model (landed
               46ce011 -- this spec DEPENDS on it: ASTER/ASTRR now have
               a truthful home for the first time).
    Authority: EV6 HRM 5.2.9 (IER_CM), 5.2.10 (SIRR), 5.2.11 (ISUM),
               5.2.12 (HW_INT_CLR); AARM Part II-A (AST architecture);
               apisrm ev6_vms_callpal.mar (MTPR_SIRR/MFPR_SISR handlers),
               ev6_defs.mar (EV6__ISUM__* field definitions).

--------------------------------------------------------------------------------
## 1. WHAT IS MISSING TODAY

  HW_MTPR SIRR is a silent no-op (PalEntries.cpp) and HW_MFPR SIRR
  reads 0.  Nothing composes ISUM's software or AST fields: Machine.cpp
  stages `cpu.isum` ONLY from chipset EI causes (the interval timer).
  So:
    - Every guest MTPR_SIRR request evaporates.  The VMS handler RMWs
      the SIRR IPR (callpal.mar:1109-1116); MFPR_SISR reads it back
      (:1139-1142); the MP path parks a CPU number in it (:7539-7543).
      All three are writing to and reading from nothing.
    - The entire VMS software-interrupt tier is dead: IPL 3 RESCHED
      (scheduling), IPL 4 IOPOST (I/O post-processing), fork dispatch.
    - ASTs cannot be delivered at all (PE-5): ISUM's ASTK/ASTE/ASTS/
      ASTU are never computed, so $DCLAST, I/O completion ASTs, and
      process deletion have no path.

  This is the leading suspect for the OS-era INVEXCEPTN wall
  ("Exception while above ASTDEL" = IPL too high for normal dispatch).

--------------------------------------------------------------------------------
## 2. THE HAZARD THAT SHAPES THIS SPEC (read before implementing)

  It is TEMPTING to land this in two halves -- first make SIRR a real
  backing store so MFPR_SISR stops lying, then wire delivery later.

  DO NOT.  A truthful SIRR without delivery is WORSE THAN THE LIE, and
  this is not a stylistic objection -- it is a hang:

    Today   MFPR_SISR always returns 0.  Guest code that requests a
            software interrupt and then polls SISR for the ISR to clear
            it sees 0 immediately and proceeds (wrongly, but it moves).
    Half    SIRR stores the request; MFPR_SISR returns it set; no
            delivery ever occurs, so nothing ever clears it.  The same
            poll now spins forever.

  The failure would present as a NEW hang at a LATER point than the
  current wall, and would read as a regression caused by the fix --
  costing a debug cycle to re-derive.  STATE AND DELIVERY LAND
  TOGETHER, in one commit, or not at all.

  (Generalized rule proposed for the ledger: when a register is
  currently a silent zero, making it truthful is only safe if every
  consumer of the truthful value also exists.  A half-wired IPR is a
  liveness change, not just a fidelity change.)

--------------------------------------------------------------------------------
## 3. ARCHITECTURAL REFERENCE (transcribed, field-exact)

  IER_CM (HRM 5.2.9, Table 5-5) -- selected when IPR index<7:2> == 0b000010;
  index<1:0> select which fields a HW_MTPR writes (bit1 = IER, bit0 = CM):
      EIEN[5:0]   [38:33]  external interrupt enables
      SLEN        [32]     serial line
      CREN        [31]     corrected read error
      PCEN[1:0]   [30:29]  performance counters
      SIEN[15:1]  [28:14]  SOFTWARE interrupt enables
      ASTEN       [13]     AST interrupt enable (master gate)
      CM[1:0]     [4:3]    current mode (00 K, 01 E, 10 S, 11 U)

  SIRR (HRM 5.2.10, Table 5-6) -- read-write:
      SIR[15:1]   [28:14]  software interrupt requests
      all other bits reserved (MBZ)

  ISUM (HRM 5.2.11, Table 5-7) -- READ-ONLY, derived:
      EI[5:0]     [38:33]
      SL          [32]
      CR          [31]
      PC[1:0]     [30:29]
      SI[15:1]    [28:14]
      ASTU        [10]
      ASTS        [9]
      ASTE        [4]
      ASTK        [3]

  PCTX (HRM 5.2.21 -- landed 46ce011, this spec's prerequisite):
      ASTRR[3:0]  [12:9]   request, bit order U=12 S=11 E=10 K=9
      ASTER[3:0]  [8:5]    enable,  bit order U=8  S=7  E=6  K=5

  DERIVATION RULES:
      SI[n]  = SIRR[n] AND IER.SIEN[n]                       (HRM 5.2.10)
      AST[m] = PCTX.ASTRR[m] AND PCTX.ASTER[m]
               AND IER.ASTEN
               AND (CM >= m)                                 (HRM Table 5-7)
        where m is the mode value 0=K 1=E 2=S 3=U.  "CM >= m" is the
        LESS-PRIVILEGED-OR-EQUAL test: a kernel AST (m=0) is deliverable
        from any mode; a user AST (m=3) only while already in user mode.
        AARM Part II-A states the same rule from the PS side ("the
        current mode ... must be equal to or higher than the value of
        the mode associated with the AST request").

  ISUM READ SIDE EFFECT (HRM 5.2.11): if a new hardware/SL/CRD/PC
  interrupt arrives simultaneously with an ISUM read, the read returns
  ZEROS (passive release); the interrupt re-signals on return to native
  mode.  PALcode is advised to read ISUM twice and OR.  We do NOT model
  the zero-return race (we are not superscalar); RECORD AS DEVIATION
  D1 -- our ISUM read is always coherent, which is a strict superset of
  legal behavior and cannot starve the handler.

--------------------------------------------------------------------------------
## 4. PROPOSED IMPLEMENTATION

  S1. CpuState: add `uint64_t sirr = 0;` next to `ier` / `isum`.
      Snapshot: CpuState is raw-POD serialized (Snapshot.cpp) -- adding
      a field CHANGES THE IMAGE LAYOUT.  Check the snapshot version
      constant and bump it, or place the field so existing images stay
      readable.  [OPEN Q1 -- see Sec 6.]

  S2. coreLib/IprFields.h: add `composeIsum(cpu)` next to the existing
      ierCmCompose, implementing Sec 3's derivation exactly, OR'd with
      the hardware cause bits Machine.cpp already stages in cpu.isum.
      Pure function of state; unit-testable without a machine.

  S3. PalEntries HW_MTPR SIRR: store `c.opB & 0x1FFFC000` (SIR[15:1]
      only; reserved bits dropped, not stored).  HW_MFPR SIRR: return
      cpu.sirr.  HW_MFPR ISUM: return composeIsum(cpu) instead of the
      raw staged field.

  S4. DELIVERY (the half that must not be omitted).  Machine.cpp owns
      interrupt arbitration.  Today canAcceptInterrupt() gates on
      PAL-relocation + !inPalMode + the EI IER bit, and returns TRUE
      unconditionally for non-EI levels (its own comment says software
      interrupts and ASTs "fall through to accepted for now ... their
      own per-source gates will land alongside SIRR / ASTRR wiring in a
      follow-up phase" -- that follow-up is THIS spec).
      Add: after every retire (same site as the interval-timer check),
      if composeIsum(cpu) has any SI or AST bit set AND the CPU can
      accept at that source's IPL, stage the INTERRUPT divert exactly
      as stageInterruptDivert does for EI -- excAddr save, PC ->
      palBase + kEntry_INTERRUPT (0x680), palMode set, cpu.isum SET to
      the composed value so the guest handler's ISUM read decodes the
      right cause.
      The guest PAL then services and clears SIRR via its own MTPR --
      which now actually works, closing the loop that Sec 2 warns about.

  S5. IPL mapping for the accept gate.  Software interrupt SIR[n] is
      IPL n (VMS uses 1..15; IPL 3 RESCHED, IPL 4 IOPOST are the hot
      pair).  AST is IPL 2.  The existing canAcceptInterrupt takes an
      irqLevel and maps 19..30 onto EI bits; extend it to gate SI/AST
      on the composed ISUM rather than returning true.  [OPEN Q2.]

  S6. Tests (doctest, CHECK only), all pure-state where possible:
      T1  composeIsum: SI[n] set only when SIRR[n] AND SIEN[n].
      T2  composeIsum: AST[m] requires ASTRR AND ASTER AND ASTEN AND
          CM >= m -- table-drive all 4 modes x 4 request modes (16
          cases), pinning the less-privileged-or-equal rule in both
          directions.
      T3  composeIsum ORs the hardware EI bits Machine staged.
      T4  MTPR SIRR drops reserved bits; MFPR SIRR round-trips SIR[15:1].
      T5  MFPR ISUM is derived, and is NOT writable (MTPR ISUM stays a
          permissive no-op -- architecturally read-only).
      T6  DELIVERY: a staged SI at an acceptable IPL diverts to
          palBase+0x680 with excAddr saved and palMode set; and does
          NOT divert while inPalMode (the existing PALmode gate must
          still hold -- #70 / the in-PALmode injection halt).
      T7  LIVENESS REGRESSION PIN (the Sec 2 hazard, stated as a test):
          request -> deliver -> guest clears SIRR -> ISUM returns to
          zero.  This is the loop whose absence would hang a poller.

--------------------------------------------------------------------------------
## 5. SEQUENCING / RISK

  This changes WHEN THE MACHINE TAKES INTERRUPTS -- the highest-blast-
  radius class of change in the emulator.  Every prior interrupt change
  in this campaign (the PALmode gate, the IER gate, the relocation gate)
  was landed against a boot that was otherwise stable, and each caught
  a regression the unit tests could not.

  Therefore: land S1-S3 + S4 in ONE commit with T1-T7 green, then run
  the FULL tri-platform gate (DS10 + DS20 + ES40 to P00>>>) BEFORE the
  OS boot, because a spurious software interrupt during console
  execution would present as a console-era regression, not an OS-era
  one.  The tri-platform gate is already owed (JRN-SCSI-033 5b); this
  is the change that makes it non-optional.

--------------------------------------------------------------------------------
## 6. OPEN QUESTIONS FOR GATE-1

  Q1. Snapshot compatibility: CpuState is raw-POD serialized.  Add
      `sirr` and bump the snapshot version, or reuse reserved space to
      keep existing .snap images loadable?  Existing images are a
      convenience, not an obligation -- but the answer should be
      explicit, not incidental.
  Q2. IPL model: EmulatR has no first-class IPL register -- IER bits
      and PS/CM stand in for it.  Should the SI accept gate compare
      against a derived IPL (from IER SIEN + the PAL's own IPL model),
      or is "SIEN[n] set" sufficient as the enable, letting the guest
      PAL's own IPL discipline do the rest?  The HRM says the hardware
      gate IS the enable bit; VMS layers IPL on top by writing IER.
      RECOMMENDATION: follow the hardware -- enable bit only.  This
      also keeps the gate honest instead of inventing an IPL model.
  Q3. Should AST delivery be gated additionally on !inPalMode?  The
      generic PALmode gate already covers it; naming it here so the
      answer is recorded rather than inherited.

--------------------------------------------------------------------------------
## 7. WHAT THIS SPEC DOES NOT COVER

  - Arithmetic-trap delivery (audit PE-8 / FV-11 / WB-6) -- a separate
    seam, though it shares the "exception has no delivery path" shape.
  - HW_INT_CLR W1C semantics for SL/CR/PC (HRM 5.2.12) -- currently a
    no-op; unrelated to SI/AST but in the same register neighbourhood.
  - Interprocessor interrupts (IPIR); SMP is out of scope here.
