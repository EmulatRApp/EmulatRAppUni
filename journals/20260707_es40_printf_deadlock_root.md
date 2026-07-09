<!--
Title:  ES40 boot deadlock root -- SRM FAO/printf console library livelock,
        reconciled with the authoritative build_power_hw / IIC call stack.
Date:   2026-07-07
Author: Timothy Peer (architect) / Claude (static disassembly + log analysis, Cowork)
Status: FINDINGS. Supersedes the "COM2 combott_txready() spin" framing in
        20260706_es40_com2_txready_spin_root.md (Root A UART and Root B console-base
        were both wrong levers). Reconciles the dynamic locus (SRM print library)
        with the authoritative SRM call stack (gct_init$pc264_hw -> build_power_hw
        -> IIC 0x70/0x72). Feeds: 20260707_es40_interface_coverage_audit.md.
Method: ASCII(128) only. Static disassembly of the byte-faithful decompressed image
        (tools/host_decompressor/out/es40_decompressed.bin, VA = file_off + 0x8000),
        plus Tim's es40_longrun.log (6.27e9-cycle run).
-->

# ES40 Boot Deadlock Root (2026-07-07)

## 0. One-paragraph summary

The ES40 does not reach `P00>>>`. It enters an unbounded / effectively-infinite
state inside the SRM console FAO/printf output library, reached during the
firmware-side HWRPB config-tree build. This is a TRUE deadlock, not "slow":
Tim's es40_longrun.log ran 6,274,964,986 cycles (876 s wall) and ended in the
same code region as the earlier 0x50000000 (1.34e9) run. The dynamic locus (the
print library) sits DOWNSTREAM of the authoritative controlling routine
`gct_init$pc264_hw` -> `build_power_hw` (galaxy_pc264.c), which reads the IIC
system status registers at nodes 0x70/0x72 -- and the ES40 IIC controller is
UNMAPPED in EmulatR V4. The print activity is the visible symptom; the audit
(section 6) is required to prove the exact coupling and to close interface
coverage generally.

## 1. Empirical result -- deadlock, not slow (question closed)

From es40_longrun.log terminal block:

    PROFILE: retires=4294967218 cycles=6274964986 wall=876.19s
    PC       = 0x204b20  palMode=false  halted=false  cycles=6274964986
    lastFault= 7 (kFaultAcv)  excAddr=0x1b7d34
    Stop reason: MaxCyclesExceeded at PC=0x1b7d34

6.27e9 cycles is ~14x the earlier 1.34e9 run and lands in the same locus. That
retires the 2026-07-06 open question "deadlock vs merely slow": DEADLOCK.

The `lastFault=7 (kFaultAcv) excAddr=0x1b7d34` is a STALE record (as flagged
2026-07-06). 0x1b7d34 is `RET (R26)` following the CALL_PAL thunk table at
0x1b7d28..0x1b7d48; it is not the active fault. The terminal ACTIVITY is the
print library, not an ACV.

## 2. The terminal locus is the SRM FAO/printf console library (disassembly-proven)

Tooling note: no Alpha cross-binutils is available in the Cowork sandbox, so a
compact Alpha disassembler was written in Python and VERIFIED against the known
anchor: file bytes at VA 0x1b7d80 decode to `LDBU R0,0(R16) ; MB ; RET (R26)`,
byte-identical to the 2026-07-06 journal's documented encoding. Base = 0x8000.

Routines identified (VA = decompressed file_off + 0x8000):

- 0x628b8  putc / buffered fputc. Args R16 = FILE-like control block, R17 = char.
           Buffers into R16->buf[R16->idx] (idx at +48, 256-byte cap tested at
           0x628dc `LDA R0,-256(R18)`, buffer bytes at +52), then emits via the
           device-write proc R16->[28]: `LDQ R26,8(R27) ; JSR (R26)` at
           0x62928/0x62934. Increments a chars-written count at +36. Straight
           line (NOT itself a loop).
- 0x204b20 compiler unsigned divide/modulo helper: power-of-two test
           (`AND R17,R17-1`), UMULH reciprocal-multiply magic division. This is
           number-to-digits conversion. The cycle cap fell here (R17=0x10 divisor).
- 0x62e48  `LDQ R0,80(R27) ; ... ; JSR (R0)` -- bound-procedure emit primitive.
           An xref sweep of the whole image finds 400+ BSR sites into 0x62e48:
           this is the ubiquitous SRM FAO/printf output core.
- 0x63738  a COUNT-BOUNDED print loop: loads a count from *(PV-392)->[16], then
           iterates that many times calling 0x62e48, decrementing to zero
           (0x63790 `BNE R0,0x63770`). Bounded by the count field.

The DATA being formatted is the SRM symbol/descriptor table at 0x173xxx. The
format strings there are decisive and identify config-tree / device-enumeration
display output:

    "bus %d, slot %d"      ", function %d"      " -- %s%s -- %s\n"
    "%-24s   %-8s"         "%08X/%08X"          "%08X"
    "0123456789"           "running"  "ready"  "null"  "std"

## 3. Two corroborating facts from the run

1. ZERO SRM console text egressed COM1 across the entire 876 s run (the only
   banner in the log is EmulatR's own). The formatted bytes never reach the wire.
2. A persistent but HANDLED `kFaultDtbMissDouble` storm in PAL: FaultEventLog
   shows repeated pc=0x8321 / 0x8591, op=0x1b (HW_LD), encoded=0x6c845000
   (a VPTE-form PAL load, disp=0, quadword), palMode=1, from ~cyc 1.238e9 on.
   Boot survives it (it reaches the print library at 6.27e9), so it is not fatal,
   but the config-walk is dereferencing through an unhealthy translation regime.

## 4. Reconciliation with the authoritative call stack (Tim, prior audit)

Authoritative SRM spine (ES40 = CLIPPER, pc264, SYSVAR variation 5):

    powerup()                       kernel.c:1959
      build_hwrpb(hwrpb)            hwrpb.c:333
        build_config(hwrpb,off)     hwrpb.c:487
          gct_init$pc264_hw()       galaxy_pc264.c:158
            if ((SYSVAR[0]>>10)!=1) galaxy_pc264.c:213   var=5 -> TRUE -> runs
              build_power_hw(root)  galaxy_pc264.c:1520   <-- controlling blocker
                fopen("iic_system0")/fread -> iic_driver.c:239  IIC node 0x70
                fopen("iic_system1")/fread -> iic_driver.c:240  IIC node 0x72
                if (!status) return(status)  galaxy_pc264.c:1582
            build_fru_root(root)    galaxy_pc264.c:1657
              build_smb_fru / PWR0-2 / FAN FRU -> build_fru.c   IIC EEPROMs

EmulatR V4 leaves the ES40 IIC controller UNMAPPED (confirmed live in
es40_longrun.log: "TsunamiChipset: no proven IIC base for model 'ES40' -- IIC
left UNMAPPED"). `kIicBaseByModel` (chipsetLib/TsunamiChipset.h) has DS10 /
DS20 / DS20E rows but no ES40 row.

Coupling -- two candidate mechanisms, to be decided by the section 5 probe:

  (M1) build_power_hw's fopen/fread on iic_system0/1 cannot resolve (no
       controller), the routine and/or its callers emit progress/error text
       through the FAO library in a retry or per-entry loop, and never
       terminate. The print library is then the SYMPTOM; the ROOT is the
       missing IIC controller (interface gap) or the DDB/node resolution.
  (M2) A config-tree enumeration count that V4 mis-populates (all-ones from an
       unmodeled device read, or a wrong node/bus count) drives the
       count-bounded print loop (0x63738-class) for billions of iterations.

Both are interface-coverage failures. Neither supports the retired COM2 UART
framing.

## 5. The decisive probe (I prep, Tim runs -- Windows binary)

Capture the FAO format-string pointer (R16) and caller (R26) at the print core
entry 0x62e48. A repeated format string names the runaway and what it prints.

    cd out/build/relwithdebinfo
    EMULATR_2D_NOOP=1 EMULATR_SPINSKIP=1 EMULATR_NO_PUTTY=1 \
    EMULATR_DIAG_PCLO=0x62e48 EMULATR_DIAG_PCHI=0x62e4c \
    EMULATR_DIAG_WREG=16 EMULATR_DIAG_WMIN=26 EMULATR_DIAG_CAP=2000 \
    ./Emulatr.exe --firmware firmware/es40_v7_3.exe --mem 4294967296 \
      --no-autoload --max-cycles 0x50000000 > es40_printf_xref.log 2>&1

Interpretation:
- repeated "bus %d, slot %d" / device line  -> M2 (enumeration count), fix the
  count / the unmodeled-device read (same class as the TIGbus "return 0, not
  all-ones" DS10 fix).
- repeated error/status string             -> M1 (IIC fopen/fread failure loop),
  fix = map the ES40 IIC controller (kIicBaseByModel ES40 row) + node resolution.
(Confirm the EMULATR_DIAG_* env names against pipelineLib/PipelineDriver.h; the
names here are taken from 20260706_es40_com2_txready_spin_root.md.)

## 6. Why this forces the interface audit

We cannot currently assert full ES40 interface coverage. The dynamic locus and
the authoritative call stack meet at the IIC gap, but the config-tree walk also
touches PCI enumeration, the CRB/console hand-off, RTC, FRU EEPROMs, and the
Cchip/Dchip/Pchip CSR surface -- any of which, if stubbed to a value the
firmware rejects, produces the same "never reaches >>>" signature. The
authoritative remedy is a single reconciled ledger: every interface the ES40
SRM REQUIRES (apisrm source, file:line) vs what EmulatR V4 DELIVERS (V4 source,
file:line, fidelity). That ledger is:
  journals/20260707_es40_interface_coverage_audit.md  (in progress)

## 7. Artifacts / tooling

- Alpha disassembler: /tmp/adis.py (Cowork sandbox), anchor-verified at 0x1b7d80.
- Static substrate: tools/host_decompressor/out/es40_decompressed.bin
  (4,155,392 bytes; VA = file_off + 0x8000).
- Run log analyzed: es40_longrun.log (uploaded 2026-07-07).
- Format-string table region: VA 0x173xxx (SRM symbol/descriptor + FAO strings).

## 8. Supersessions

- SUPERSEDES 20260706_es40_com2_txready_spin_root.md: the terminal state is NOT
  a COM2 MSR/LSR poll; combott_txready returns ready for MSR=0x30/0x00 alike.
  The 0x1b7xxx PCs are leaf I/O helpers; the controlling logic is the FAO
  library driven by the config-tree build, upstream-gated by build_power_hw/IIC.
- CONFIRMS the 2026-07-05 es40_next_frontier build_config_tree/build_power_hw
  framing as the authoritative spine.

## 16. RANGE-WATCH RESULT + web-analysis reconciliation (2026-07-07, end of session)

Ran EMULATR_GMEM_WATCH_LO=0x1038000 EMULATR_GMEM_WATCH_HI=0x1039000 (new range watch,
GuestMemory.cpp) over the full ES40 boot.  RESULT: ZERO stores anywhere in the SCB page.
The vector table is never written at that PA -- rules out offset-drop-to-page-base and any
in-page install.  Branch: Q2a (install step never runs -- LEAD) vs Q2b-far (store mistranslates
to a far page).  Cannot fully split without catching the store (need CLK_ISR, or value-key on
the known base 0x1038000).

WEB-ANALYSIS RECONCILIATION (20260707_es40_interrupt_return_null_dispatch_analysis.md):
- Its section-5 "concrete defect already visible" (execHwMtpr raw-assigns i_ctl/m_ctl, never
  derives i_spe/m_spe, "lines 471-472") is a STALE-SNAPSHOT artifact.  LIVE tree already derives
  both: PalEntries.cpp 1711-1712 (i_spe=I_CTL<5:3>) / 1729-1730 (m_spe=M_CTL<3:1>).  Fix B2 =
  no-op; do NOT land.
- Its Fix B1 (mask VA<45:44> in SPE[2]) is also already correct: tryKsegTranslate arm is
  pa_out = va & 0x00000FFFFFFFE000 (Ev6Translator.h:152), which clears <47:44>.  B1 = no-op.
- Both pre-staged Q2b sub-fixes target already-correct code.  The SPE/kseg translator is
  faithful (corroborated: DS10/DS20 reach P00>>> through it).

REAL latent finding (NOT this halt's cause -- page-base watch is empty, so not exercised here):
tryKsegTranslate returns a PAGE-ALIGNED PA (all three SPE masks clear VA<12:0>), while the TLB
path applyTlbHit (Ev6Translator.h:234-235) composes pfn | (va & offsetMask).  Kseg drops the
page offset, TLB keeps it -- an asymmetry.  If any kseg access with a nonzero offset is ever
exercised it lands at the page base -- a genuine bug.  Filed to investigate-then-fix; verify it
is exercised before touching (do-no-harm: DS10/DS20 boot today).

TOOLS ADDED THIS SESSION (all diagnostic-gated): MTPR_IPL enum 0x0E->0x0F (HW_IPR.h, doc/
forward-compat); 3 opcode refs in REFERENCE_INDEX.md; EMULATR_IRQDIAG (CMake option);
EMULATR_DIAG_CYCLO/CYCHI + excAddr column (PipelineDriver.h); DecListingSink LOOKBACK_DUMP
10->60; EMULATR_GMEM_WATCH_LO/_HI range watch (GuestMemory.cpp).  Next-session plan:
20260708_es40_tomorrow_scb_install_plan.md.

## 9. RESULT 2026-07-07 -- IIC row broke the livelock; new frontier = clean HALT

es40_iic_32g.log (32 GB, ES40 kIicBaseByModel row 0xFFF80000 active):
- IIC controller now MAPPED: "TsunamiPchip: registered PCI mem 0xFFF80000-0xFFF80001";
  the "no proven IIC base for model ES40" warning is GONE.
- The printf / err_printf LIVELOCK IS BROKEN. Prior runs ran forever (MaxCyclesExceeded in
  the FAO storm); this run reaches a CLEAN HALT at cyc 1,273,790,765 (~1.27B, wall 209 s):
  "Stop reason: HaltedClean, lastFault=13 (kFaultHalt), PC=0, halted=true".
- NEW FRONTIER = a firmware HALT reached via the console/device bound-method DISPATCH chain
  at ~0xa87xx (disasm: repeated LDL Robj,0(R5) / LDL R27,off(Robj) / LDQ R26,8(R27) /
  JSR (R26) / BEQ R0 -- vtable/proc-descriptor calls that RETURN and progress, NOT the
  storm). R26=0xa87a8 is the RA into the dispatch; the HALT executed inside a callee.
  Register context at halt: R03=R17=R19=0x66 (102) -- a recurring value (cf the earlier
  "CSERVE 0x66" note); likely a halt-reason / undefined-service code.
- Residual (unchanged, handled): the PAL DtbMissDouble storm (0x8321/0x8591) + the 0x5afac
  fill-loop unaligned scan to ~1 GB (va 0x3fc12xxx). Not the blocker.
- OPERATIONAL: the halt auto-snapshotted 34.3 GB (~2 min). For diagnosis, run at 4 GB.
- NEXT: re-run with EMULATR_CONSOLE_MIRROR=1 (+ 4 GB) to capture what the SRM PRINTS right
  before the halt -- the console text (banner fragment or panic message) names the cause.
  The halt site is a RAM proc-descriptor (vtable), NOT statically resolvable, so the console
  output is the decisive signal.

## 10. ROOT CAUSE 2026-07-07 -- unimplemented CSERVE func 0x66 = get_time

es40_iic_mirror.log (4 GB, EMULATR_CONSOLE_MIRROR=1): the SRM console boots CLEAN on COM2
through idle PCB / semaphores / heap / driver structs / idle PID / file system / hardware
init / timer / "Memory size 4096 MB" / "testing memory" -- then "SYSFAULT CPU0 - pc =
001b783c" with a full crash dump (garbage R16=R3=0xFFFFFFFF7F82893F, frame PC=0x1b7dd4,
caller 0x5b058). Huge progress from the old infinite storm.

MECHANISM (disasm-proven):
- 0x1b7dd4 = read leaf: LDQ R0,0(R16); MB; RET -- faults on garbage R16.
- 0x8c2d0 (a common primitive, 17 call sites incl the 0x5afxx/0x5b0xx memory scan):
    R2 = input (0x8c2f0); R16 = 102/0x66 (0x8c2f8); CALL CSERVE via 0x1b78f8
    (= CALL_PAL 0x09 = CSERVE); R0 = R2 - R0 (0x8c308).  return = input - CSERVE(0x66).
- CSERVE function 0x66 = **get_time** (architect, authoritative): the kernel/console
  request for the current system time/date. 0x8c2d0 is thus a TIMING primitive
  (input - now). V4 execCserve (PalEntries.cpp) does NOT implement 0x66, so R0 comes back
  stale/garbage -> input - garbage = 0xFFFFFFFF7F82893F -> memory test dereferences it ->
  SYSFAULT.

FIX (discuss-before-code): implement execCserve case 0x66 = get_time, returning the current
time in the SRM-expected register/format. DETERMINISM: source it from the FAITHFUL
deterministic RTC already in tree (deviceLib/Tsunami/ToyRtc.h -- MC146818, cycle-derived
deterministic epoch), NOT host wall-clock, so cold boot stays byte-reproducible. TO PIN
before coding: (a) the exact return format/register get_time uses (VMS 64-bit time / packed
date-time / RTC fields), (b) whether R17/R18 carry a buffer pointer or it returns in R0.
Source: ev6_vms_pc264_pal.mar cserve get_time + the SRM caller expectation.

This is the LAST identified blocker before the memory test completes; implementing get_time
should carry ES40 past "testing memory" toward the console prompt.

IMPLEMENTED 2026-07-07 (static-helper approach, architect-approved):
- deviceLib/Tsunami/ToyRtc.h: factored the calendar math out of materializeClock into a
  static `calendarFromCycles(cycles, cyclesPerSecond)` (ONE source of truth for the RTC
  port path and get_time), added `kDefaultCyclesPerSecond`, and a public static
  `timestampMMDDhhmm(cycles, cps=default)` that packs BCD/24h MMDDhhmm.  Deterministic;
  sourced from the same cycle-derived epoch as the RTC (no host wall-clock).
- palBoxLib/grains/PalEntries.cpp: `execCserve` case 0x66 -> R0 =
  ToyRtc::timestampMMDDhhmm(c.cpu->cycleCount) (via #include deviceLib/Tsunami/ToyRtc.h;
  deviceLib and palBoxLib are same-layer, direct include OK per architect).
- tests/deviceLib/test_toyrtc.cpp: doctest -- helper byte-matches a 24h/BCD RTC read from
  the same cycles, and a concrete date (0x02101345 = 2026-02-10 13:45).
CONFIRMED 2026-07-07 (es40_gettime.log, 4 GB, console mirror):
- ToyRtc get_time doctest PASSES (1/1).
- The memory-test SYSFAULT is GONE.  The SRM console now advances well past it:
  starting console -> idle PCB -> semaphores -> heap -> driver structs -> idle PID ->
  file system -> "initializing hardware" -> "initializing timer data structures" ->
  "lowering IPL" -- then a CLEAN HALT (Stop reason: HaltedClean, PC=0, R26=0x5dec4,
  R21=0x2f8/COM2).  Several init phases deeper than the old SYSFAULT; "lowering IPL" is
  near the end of SRM init before the console command loop.
- NEW FRONTIER: the clean halt right after "lowering IPL" (caller R26=0x5dec4).  The PAL
  DtbMissDouble storm (0x8321/0x8591) persists but is handled (boot proceeds through it).
- FRONTIER TRACED (2026-07-07): the platform lever is NOT it -- EMULATR_PLATFORM=isp and
  =silicon halt IDENTICALLY (register-identical, ~cyc 1.2395B).  "lowering IPL" = CALL_PAL
  0x0F = MTPR_IPL (VMS PAL, distinct from OSF SWPIPL).  V4 dispatches it CORRECTLY:
  computeCallPalEntry (Ev6EntryVectors.h, 0x2000+F*0x40) -> 0xA3C0, which holds the real
  MTPR_IPL handler (not empty).  So NOT a missing CALL_PAL.  The handler -> 0xec80 writes
  the hardware IPL IPR (HW_MTPR idx 0xa10) and tests a "pending at new IPL" flag at
  R21+0x11a0; it is SET, so BR 0x12668 -> raise to IPL 29, set up the interrupt, BR 0xd8d0
  (the SCB interrupt dispatcher).  So lowering IPL UNMASKS a pending interrupt (clock /
  interval-timer, matching the old "R2 clock-interrupt return" blocker) and the HALT is in
  delivering it.  The vectored handler PC comes from the SCB in RAM -> not statically
  resolvable; a runtime PC-capture on the 0xd8d0 dispatch is the decisive next step.
  FLAG: coreLib/HW_IPR.h defines MFPR_IPL=0x000E AND MTPR_IPL=0x000E (same value) -- confirm
  that is the intended shared IPR index and not a copy-paste (MTPR_IPL would be 0x000F as a
  CALL_PAL code, but as a HW_MxPR IPR SELECTOR read/write can share an index).
  [RESOLVED: architect confirms 0x000E for both is correct -- shared IPR selector, read vs
  write by opcode (HW_MFPR 0x19 / HW_MTPR 0x1D).  Not the bug.]

## 12. ROOT PINPOINTED 2026-07-07 -- interval-timer interrupt never DELIVERED (IER gate)

es40_clockint_diag.log (DIAG PC-window 0xd800-0x13000) shows the SRM IDLE-LOOPS at 0xec80,
NOT halting in delivery:
    0xec80 HW_MTPR idx=0xa10,R31 (pal=1)   ; lower IPL / write IPL-IER
    0xec90 HW_LD R7,[R21+0x11a0] (PA 0x71a0); soft pending-interrupt flag
    0xec94 BEQ R7,0xecb4                    ; R7==0 EVERY iteration -> taken
    0xecb4 HW_REI (pal=0)                   ; return to non-PAL, nothing delivered
Repeats ~every 100-500 cyc for ~680K cyc, then halts (timer calibration/watchdog timeout).
The soft flag at 0x71a0 stays 0 -> the clock interrupt handler NEVER runs.

ROOT: V4 latches the interval timer (MISC<ITINTR> + per-CPU b_irq<2>) but never DELIVERS it.
Machine::canAcceptInterrupt (systemLib/Machine.cpp:720) gates delivery on: (1) PAL relocated
+ palBase!=0 (met); (2) NOT inPalMode (met in the 0xecb4 HW_REI non-PAL windows); (3)
HW_IER<EIEN2> = bit 35 (interval-timer enable) SET.  cpu.ier resets to 0 and is only set by a
HW_MTPR HW_IER.  The ES40 VMS SRM enables/sets IPL+IER through the MTPR_IPL path (HW_MTPR
idx=0xa10 at 0xec80), and V4 is NOT translating that write into cpu.ier<EIEN2> -> the gate
stays shut forever -> no clock delivery -> idle-loop -> halt.

FIX AREA (architect design -- core interrupt delivery): (a) V4's HW_MTPR handling of the IPR
index the VMS PAL uses to set IPL/IER (idx 0xa10 = iprSelector bits[15:8]=0x0a) must update
cpu.ier (or the arbitration must read the real IER the PAL programs); and/or (b) the deferred
"Phase D architectural IPL compare" in canAcceptInterrupt (recognize the SRM lowered IPL below
22 so the latched clock IRQ delivers).  CONFIRM FIRST: trace cpu.ier writes +
canAcceptInterrupt(22) results during the idle loop to see whether IER<EIEN2> is ever set and
which gate condition is false.  This is the "R2 clock-interrupt return" blocker and likely the
LAST major gate before P00>>>.

## 13. PROBE RESULT 2026-07-07 -- clock delivery PROVEN; boot advanced to "lowering IPL"

EMULATR_IRQDIAG run (es40_irq_spinskip.log, SPINSKIP on, corrected VS build).  2929 probe
lines.  Findings:

CLOCK DELIVERY WORKS.  When the SRM finally writes HW_IER with ei2 (bit 35) set, V4 delivers:
    IRQDIAG-IER  pc=0xec81 opB=0x7effffe000 ier=0x7effffe000 ei2=1 cyc=1239510358
    IRQDIAG-DELIVER cyc=1239510365 savedPc=0x1b7d34
So canAcceptInterrupt(22), the execHwMtpr HW_IER/HW_IER_CM seam (selector 0x010A/0x010B ->
cpu.ier via ierCmIerPortion), and the interval-timer -> irq_h<2> -> ei2/bit35 mapping are ALL
faithful.  NO fix needed in the interrupt path, and specifically NOT in mtpr_ipl -- the PAL
MTPR_IPL handler writes HW_IER itself and V4 honors it.

Gate breakdown confirms ei2 was the sole blocker before that write: post-relocation FIRE lines
read [reloc=1 palBase=0x600000 inPal=0 ei2=0] -- three of four conditions green, only ei2 low.

BOOT DEPTH.  Console now reaches (CON COM2): "initializing hardware" -> "initializing timer
data structures" -> "lowering IPL".  This is the deepest ES40 boot yet and BLOWS PAST the old
0x60222c decompressor panic (cyc 12.5M).  The get_time (CSERVE 0x66) fix + the (already
correct) clock path carried it to cyc ~1.24B.

NEW FRONTIER -- post-tick clean HALT.  At 0xec80 the SRM idle-loops "lowering IPL", writing
HW_IER=0x2060000000 (ei2 MASKED: bit37 EI[4] + bits29,30 PC) ~1970 times over ~855K cyc; the
interval timer stays latched-but-masked.  It then drops to IPL 0 (0x7effffe000), takes ONE
clock tick (the DELIVER above), the tick handler runs (writes HW_IER=0x62e0000000 at pc=0x11b61),
and ~215 cyc later the machine executes a deliberate CALL_PAL HALT:
    PC=0x0 palMode=false halted=true lastFault=13 (kFaultHalt)  Stop reason: HaltedClean  cyc=1239510580
Open question: does the tick handler hit an error/assert (H1), or did the SRM expect periodic
ticks DURING the masked idle wait and give up (H2)?  NEXT: trace the ~215-cyc window
[1239510365 .. 1239510580] from the clock ISR (palBase+0x680=0x600680) through pc=0x11b61 to the
HALT to see the path.  Separate blocker from sec.12; the interrupt-delivery question is CLOSED.

## 14. ROOT PINPOINTED 2026-07-07 -- HALT = HW_REI to PC 0 via a ZERO HW_EXC_ADDR

DecListingSink 60-deep lookback (LOOKBACK_DUMP restored 10->60) + Ghidra cross-read nailed it.
The clean HALT is NOT a decision -- it is a register-form HW_REI returning to PC 0:
    0x8585  HW_MFPR R23, HW_EXC_ADDR (sel 0x06/0x0106)  -> R23 = 0   (should be interrupted PC)
    0x8589  BIS     R23, R31, R06                        -> R06 = 0
    0xd5f1  HW_REI  (enc 0x7bf7a000, REGISTER form, Rb=R23) -> PC = R23 = 0
    0x0     zero-word decodes as HALT (kFaultHalt, HaltedClean)
Ghidra: 0xd5f1 is the console PAL's register-form return ("BR r3"); a failed return-consistency
check routes to a deliberate halt stub at 0xb475.  Real PAL substitutes 0xb475; V4 lands at 0x0
because the register is literally 0 -- SAME event: the console PAL detects bad return state.

ROOT: HW_MFPR HW_EXC_ADDR returns 0 when the console PAL (base 0x8000) re-reads it in the
interrupt-return path.  excAddr WAS correct at entry: the clock divert set it
(systemLib/Machine.cpp:202  cpu.excAddr = savedPc = 0x1b7d34) and the handler saved 0x1b7d34 to
its impure slot 0x150(R21) at 0xda85.  So excAddr is ZEROED between interrupt entry and the
re-read (~188 cyc), a span that sits BEFORE the 60-deep lookback -- culprit instruction not yet
visible.  Delivery is faithful; the bug is EXC_ADDR preservation across the console PAL
interrupt-return.  NOT mtpr_ipl, NOT the IER path.

TOOL: added EMULATR_DIAG_CYCLO/CYCHI cycle-window gate + excAddr on every DIAG-PC line
(pipelineLib/PipelineDriver.h).  NEXT CAPTURE: wide PC window + CYCLO=1239510360 CYCHI=1239510581
prints excAddr per instruction across the whole divert->halt span -> names the excAddr-zeroing
instruction.

## 15. TRUE ROOT 2026-07-07 -- console-PAL interrupt return through a NULL dispatch pointer

The excAddr=0 was a SYMPTOM, not the cause.  Full DIAG-PC capture (wide PC + CYCLO window,
excAddr column) shows excAddr held 0x1b7d34 correctly until:
    cyc 1239510550  0xda94  7be2a000  HW_REI (REGISTER form, Rb=R02)  -> PC = R02 = 0
    cyc 1239510551  0x0     00000000  fault=6 (I-fetch miss at PC 0)  excAddr still 0x1b7d34
    cyc 1239510552  0x8580  ...        fault handler sets excAddr = faulting PC = 0
So the trap delivery zeroed excAddr BECAUSE a HW_REI already jumped to PC 0.  R02 traces to:
    0xda6c  HW_LD R02, 0(R04)  memAddr=0x1038600   -> R02 = [0x1038600] = 0
    0xda88  BIC   R02,#3,R02                        -> R02 = 0
where R04 = [R21+0x170]=0x1038000 (a table/dispatch base ptr) + [R21+0x158]=0x600 (a vector
offset).  So the console PAL interrupt-return does a DOUBLY-INDIRECT load of the resume/dispatch
PC from [0x1038000 + 0x600] and gets 0 -> HW_REI to 0 -> I-fetch fault at 0 -> the console PAL's
return-consistency check (Ghidra: deliberate halt stub 0xb475) fails -> HALT (V4 lands at 0x0
because the register is literally 0).

MEMORY EVIDENCE (EMULATR_GMEM_WATCH): NEITHER 0x1038600 NOR the base 0x1038000 is EVER stored to
in the whole boot -> the dispatch table at 0x1038000 is zero-filled / never built (or built at a
different PA than the console PAL's physical HW_LD reads -> a VA/PA or SCBB mismatch).  0x600 is
the SCB hardware-interrupt vector region offset (SCB HW ints 0x600-0x6F0), so this looks like the
interrupt being forwarded to an OS/console SCB vector at +0x600 that V4 never populated.

CONFIRMED FAITHFUL (do not touch): clock delivery, canAcceptInterrupt(22), the HW_IER seam, the
divert cause (stageInterruptDivert isum = EI[2] = 1<<35 = IRQ_CLK, Machine.cpp), and MTPR_IPL.
The console dispatched to sys__int_clk correctly and counted the tick; only the RETURN dispatch
pointer is null.

OPEN QUESTION (needs Ghidra + es40.dec register values -- web-chat analysis half of the hybrid
workflow): (1) what is R21 (last writer before 0xda50) and what structure does [R21+0x170]=
0x1038000 point at -- an SCB, a PCB, or a per-CPU dispatch table? (2) which init/registration
step should have written [0x1038000+0x600], and did V4 skip/diverge on it, or is it a physical
HW_LD reading a different PA than a virtual store landed on?  NOTE the console PAL routine at
runtime 0xda50 = image offset 0x5a50 (palBase 0x8000); sys__int_post (ev6_osf_pc264_pal.mar:1927)
is a DIFFERENT, p_temp-based routine -- 0xda50 is the R21/cns-based path.  0xcafebeef seen at
[R21+0xf0] is the forced ISP-model sentinel (0xBFFC), not poison -- flag but likely incidental.
- REGRESSION (separate, from Batch 1): the DREV byte-slice change (0x0101010101010101)
  broke test_ticket01_5_variant_binding + test_ticket01_dispatch, which asserted the OLD
  unfaithful DREV=0x10/0x20 variant-marker.  Reconcile: variant is faithfully carried by
  Cchip MISC<REV> (1/8), not DREV -- retarget those tests, or revert DREV.  (2 suite fails.)
