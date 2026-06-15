# AXPBox-vs-V4 Topology Audit

Companion to `EV6_audit_2.0.md`.  Where the EV6 audit measures V4
against the HRM, this document measures V4 against **AXPBox** -- a
second emulator that successfully boots SRM Console to the `>>>`
prompt.  The premise: AXPBox solves the same "what's necessary to
boot real SRM" problem we are solving.  Comparing the two surfaces
behaviors that the HRM leaves underspecified but that are
empirically required for boot.

## Why this audit exists

The HRM tells us what's spec-correct.  AXPBox tells us what's
empirically required.  These aren't always the same:

-   The HRM Section 5.2.14 says CALL_PAL_R23 selects R23 vs R27
    as the linkage register.  It does NOT say "you'll wedge boot
    if you skip writing the linkage register entirely."  AXPBox
    writes R23 unconditionally; V4 (until 2026-05-19) wrote
    neither.  The HRM alone wouldn't have caught the gap.
-   The HRM Section 5.4 describes Cbox CSRs.  It doesn't tell you
    which ones SRM Console reads first, in what order, and what
    response causes the firmware to proceed vs spin.  AXPBox's
    CCHIP handling has the answer.

Reading AXPBox alongside PALcode source closes the loop:

```
PALcode source  ----expects----> behavior X
                                 |
AXPBox          ----provides---> behavior X (boots clean)
                                 |
V4              ----must match--> behavior X (or document why not)
```

## How to use

Rows are organised by **topic** (functional area), not by HRM section.
Each row records:

-   AXPBox file:line where the behavior is implemented
-   What AXPBox does (one-sentence summary)
-   V4 status (see legend below)
-   Cross-check column: HRM section + PALcode source confirmation
-   Notes: justification for divergence, or commit reference

### Status legend

```
ALIGNED       -- V4 and AXPBox do the same thing; PALcode source
                 confirms it; HRM allows it.  Best state.
V4-AHEAD      -- V4 implements something AXPBox doesn't, OR V4 is
                 more HRM-correct than AXPBox.  Document why we
                 went further (usually because we caught a future
                 OS-boot need or a spec divergence in AXPBox).
V4-BEHIND     -- AXPBox has a behavior V4 needs to add.  Open TODO.
DIVERGENT     -- V4 and AXPBox handle differently; PALcode source
                 hasn't been consulted yet.  Needs ground-truth
                 resolution.
EVIDENCE-ONLY -- AXPBox does it because it works empirically; HRM
                 and PALcode source either don't cover it or are
                 ambiguous.  Document the empirical anchor.
```

### Authoritative sources for resolution

When V4 and AXPBox disagree, resolve in this priority:

1.  Alpha 21264/EV6 HRM
2.  Alpha Architecture Reference Manual
3.  EV6 PALcode source (`D:\EmulatR\Processor Support\Palcode\palcode\palcode\src\`)
4.  Tsunami/Typhoon HRM
5.  AXPBox (as the second working implementation)

If all five agree, V4 should match.  If AXPBox diverges from the
spec / PALcode but still works, document it as "AXPBox-bug-but-
benign" and let V4 follow the spec.

---

# Coverage summary

| Topic area | Status | Notes |
| ---------- | ------ | ----- |
| CALL_PAL dispatch + linkage register   | V4-AHEAD       | V4 honors I_CTL[CALL_PAL_R23]; AXPBox writes R23 unconditionally. |
| HW_MFPR / HW_MTPR IPR coverage         | TODO sweep     | AXPBox has rich macro-based dispatch in cpu_pal.hpp. |
| PAL shadow register swap               | TODO sweep     | AXPBox uses r[32+N] indexing; V4 has intShadow[8]. |
| MMU translation shortcuts              | DIVERGENT      | V4 has VA_CTL[VA_48]=0 hack; AXPBox approach unknown. |
| Interrupt arbitration (canAcceptInterrupt) | ALIGNED    | Both gate on per-source enable mask. |
| Cchip MISC<ITINTR> handling             | ALIGNED       | Both assert MISC bits 4:7 on timer fire. |
| Tsunami device-interrupt routing       | TODO sweep     | AXPBox's CSystem::interrupt is comprehensive. |
| SRM firmware load and Step D relocation| ALIGNED (mostly) | V4 SrmLoader rewrite 2026-05-19 adopted AXPBox's "no PA-0 mirror" model. |
| FP load/store (LDF/LDG/LDS/LDT/STF/STG/STS/STT) | ALIGNED | V4 ported from V1 with HRM cross-check; matches AXPBox. |
| CPU clock / cycle counter              | DIVERGENT      | AXPBox: cc_per_instruction=70 cycles; V4: 1 cycle/retire. |
| FPCR / IEEE FP exception delivery      | V4-BEHIND      | AXPBox has full FPCR machinery; V4 has stub. |

Total topic areas: 11 (initial scaffold).  More as we sweep.

---

# Topic 1: CALL_PAL dispatch + linkage register

| Aspect | AXPBox | V4 | Cross-check | Status |
| ------ | ------ | -- | ----------- | ------ |
| Entry PC formula                     | cpu_misc.hpp:240-247: `pal_base \| (1<<13) \| ((function & 0x80) << 5) \| ((function & 0x3f) << 6) \| 1` | coreLib/Ev6EntryVectors.h::computeCallPalEntry | HRM 5.2.14 + ev6_osf_pal.mar | ALIGNED -- same formula, different bit-construction style. |
| Privileged-mode check (function<0x40 in user mode) | cpu_misc.hpp:32-34: raises UNKNOWN2 if `function<0x40 && cm!=0` | NOT IMPLEMENTED in execCallPalDispatch | HRM 6.7 + Alpha ARM | V4-BEHIND -- V4 does not check the mode/func constraint; firmware tolerates because SRM runs kernel-only. |
| Disallowed func ranges (0x40..0x7F, > 0xBF) | cpu_misc.hpp:32-34: UNKNOWN2 -> machine check | NOT IMPLEMENTED | Alpha ARM | V4-BEHIND -- V4 dispatches them anyway; firmware tolerates. |
| Linkage register write                | cpu_misc.hpp:245: `state.r[32+23] = state.pc` (PAL-side R23, unconditional) | palBoxLib::execCallPalDispatch writes R23 or R27 per `iCtlCallPalLinkageReg(cpu.i_ctl)` (2026-05-19) | HRM 5.2.14 + ev6_osf_pal.mar:803 ("p23 = r23 call_pal linkage register") | V4-AHEAD -- V4 honors I_CTL[CALL_PAL_R23]; AXPBox writes R23 unconditionally.  Both work for current firmware (sets CALL_PAL_R23=1), but V4 is more HRM-correct for firmware that clears the bit. |
| Linkage value content                 | cpu_misc.hpp:245: `state.pc` (PC of the CALL_PAL instruction itself) | `g.pc + 4` ORed with palMode bit | HRM unclear; ev6_osf_pal.mar reads it as a return PC | DIVERGENT -- AXPBox uses CURRENT PC, V4 uses NEXT PC.  Resolution: PALcode source's `hw_stq/p p23, CNS__P23(p4)` saves whatever is in p23; doesn't constrain content.  V4's convention (return PC) matches CALL_PAL's semantic intent better. |
| Bit-0 PAL marker on destination PC    | cpu_misc.hpp:241,247: `| 1` ORs bit 0 into PC | V4 tracks palMode on CpuState separately, divertTarget bit 0 cleared | HRM 5.2.14 + 4.1.3 | DIVERGENT but functionally equivalent -- AXPBox uses Alpha's "PC bit 0 = PAL mode" convention literally; V4 tracks the bit on cpu.palMode and clears bit 0 from divertTarget.  Both correctly transition to PAL mode. |
| Personality dispatch (VMS vs OSF/Tru64) | cpu_misc.hpp:36 branches on `state.pal_vms`; calls vmspal_* intrinsics | V4 has minimal personality awareness (execCallPalDispatch is generic; firmware PAL bytes provide personality) | OSF/VMS PAL sources | DIVERGENT -- AXPBox intercepts ~40 VMS CALL_PAL funcs as intrinsics for performance; V4 lets firmware PAL bytes handle them. |
| PAL shadow swap on entry              | cpu_misc.hpp uses `r[32+N]` indexing throughout (R0-R31 vs shadow R0-R31) | coreLib::palModeEnter() exchanges intReg[4..7] + intReg[20..23] with intShadow[0..7] when iCtlSdeHigh() | HRM 6.6 + 5.2.14 | DIVERGENT shape, ALIGNED intent -- AXPBox stores both contexts simultaneously in r[0..63]; V4 swaps in/out of intReg[]. |

---

# Topic 2: HW_MFPR / HW_MTPR IPR macros

| Aspect | AXPBox | V4 | Cross-check | Status |
| ------ | ------ | -- | ----------- | ------ |
| Dispatch structure                    | cpu_pal.hpp DO_HW_MFPR / DO_HW_MTPR macros: switch on function (8-bit INDEX) | palBoxLib::iprSelector + execHwMfpr/Mtpr switch | HRM 4.1.4 | ALIGNED -- both use bits[15:8] as the INDEX. |
| PCTX special case (function & 0xC0 == 0x40) | cpu_pal.hpp:30 -- composite ASN/ASTRR/ASTER/FPEN/PPCEN read | V4 stubs HW_PCTX (silent 0) | HRM 5.2.20 | V4-BEHIND -- AXPBox synthesizes PCTX from component fields; V4 silent-0. |
| IER_CM read composition (CM at bits [4:3]) | cpu_pal.hpp:56: `(((u64)state.cm) << 3)` | coreLib::ierCmCompose() | HRM 5.2.8 | ALIGNED -- both confirm CM at bits [4:3]. |
| ISUM composition (eir & eien, slr & slen, etc.) | cpu_pal.hpp:65-75: per-source AND of request and enable | cpu.isum: set by trap delivery; not synthesized on read | HRM 5.2.10 | V4-BEHIND -- AXPBox composes ISUM dynamically; V4 returns stored isum which may not reflect current source state. |
| EXC_SUM (FP exception flags)          | cpu_pal.hpp:80: returns state.exc_sum (stored) | V4 silent-0 | HRM 5.2.12 | V4-BEHIND -- V4 has no FPCR / exception flag tracking yet. |
| PAL_BASE write masking                | cpu_pal.hpp:201: `set_PAL_BASE(state.r[REG_2] & 0x00000fffffff8000)` -- mask bits [47:15] | coreLib::palBaseSanitize: mask bits [43:15] per HRM 5.2.13 | HRM 5.2.13 | DIVERGENT -- AXPBox allows up to bit 47 (48-bit PA range); HRM 5.2.13 says PA is 44-bit so bits [43:15] only.  V4 follows HRM.  AXPBox may be permissive for future 48-bit-VA extensions. |
| CC (cycle counter) write              | cpu_pal.hpp:~300: writes state.cc + state.cc_ena | palBoxLib HW_CC: writes cpu.ccOffset = opB - cpu.cycleCount | HRM 5.1.1 | DIVERGENT mechanism, ALIGNED architectural semantics -- both make HW_MFPR CC read back the value written. |
| VA_CTL bit decomposition              | cpu_pal.hpp:305-307: `va_ctl_vptb = sext...; va_ctl_va_mode = ...` (parsed into fields) | cpu.va_ctl stored raw + coreLib/VA_types.h::vaCtl* accessors | HRM 5.1.5 | ALIGNED -- both decompose; AXPBox at write time, V4 at access time. |

---

# Topic 3: MMU translation shortcuts

| Aspect | AXPBox | V4 | Cross-check | Status |
| ------ | ------ | -- | ----------- | ------ |
| PAL-mode physical bypass              | Translation skipped when in PAL mode (state.pc & 1) | mmuLib/Ev6Translator.h:231: `if (cpu.palMode) { pa = va; return Success; }` | HRM 4.1 | ALIGNED -- both treat PAL mode as physical addressing. |
| Native-mode "physical-when-VA_48-clear" hack | NOT PRESENT (AXPBox uses proper TLB walks) | Ev6Translator.h:238: `if ((va_ctl & 0x2) == 0) { pa = va; return Success; }` | HRM 5.1.5 says VA_48 controls FORMAT, not phys-vs-virt | V4-BEHIND -- AXPBox does it correctly via TLB; V4 has a hack that works for boot but breaks when firmware enables 48-bit VA mode. |
| Superpage (kseg) detection            | AXPBox honors SPE bits in I_CTL / M_CTL for kseg mapping | Ev6Translator.h::tryKsegTranslate reads cpu.i_spe / cpu.m_spe (duplicate state) | HRM 5.2.14 | DIVERGENT -- V4 reads from cpu.i_spe / cpu.m_spe (which aren't synced with I_CTL/M_CTL writes!); AXPBox reads I_CTL/M_CTL bits directly. |
| TLB walking on miss                   | Full DTB/ITB walk via PALcode hand-off (DTBM/ITBM trap) | NOT IMPLEMENTED -- translator returns DtbMiss; no walker | HRM 4.6 | V4-BEHIND -- V4 has no walker; relies on the VA_48 hack to keep firmware in physical mode. |

---

# Topic 4: Interrupt arbitration

| Aspect | AXPBox | V4 | Cross-check | Status |
| ------ | ------ | -- | ----------- | ------ |
| External interrupt gating              | AlphaCPU.cpp:469: `(state.eien & state.eir) || ...` -- per-source enable AND request | systemLib/Machine.cpp::canAcceptInterrupt: gates on `cpu.ier & (1 << (57 - irqLevel))` | HRM 5.2.8 + 5.4 | ALIGNED -- both require per-source enable bit to be set. |
| NOT-in-PAL-mode check                  | AlphaCPU.cpp:428: `if (state.check_int && !(state.pc & 1))` | V4 does not check palMode in canAcceptInterrupt | HRM 4.5.2 | V4-BEHIND -- AXPBox refuses to deliver while in PAL; V4 might deliver an interrupt while palMode is true, which is architecturally invalid. |
| IPL check                              | Per-personality (vmspal_ent_ext_int does it for VMS) | V4 has cpu.ipl field but does not gate canAcceptInterrupt on it yet | HRM 5.2.8 + Alpha ARM Chapter 6 | V4-BEHIND -- IPL machinery present but not consulted. |
| Interval timer assert                  | System.cpp:1762: `state.cchip.misc |= 0xf0; acCPUs[i]->irq_h(2, true, 0)` | chipsetLib/TsunamiCchip.h fireIntervalTimer asserts MISC<ITINTR> and pendingIrq2 | HRM 5.4 + Tsunami HRM | ALIGNED. |

---

# Topic 5: Cchip MISC + Tsunami CSR handling

(Brief; expand in follow-up sweep.)

| Aspect | AXPBox | V4 | Cross-check | Status |
| ------ | ------ | -- | ----------- | ------ |
| MISC W1C semantics                     | Per-bit W1C on ITINTR/IPINTR/NXM | chipsetLib/TsunamiCchip.h miscWriteW1C with CAS loop and explicit W1C/W1S/WO/RO masks | Tsunami HRM Section 10 | ALIGNED -- V4's Phase B 2026-05-14 work landed the same semantics. |
| MISC ITINTR deassert                   | Implicit on W1C (bit cleared, b_irq<2> deasserts when no CPU has bit set) | TsunamiCchip.h::miscWriteW1C deasserts pendingIrq2 when ITINTR clears | Tsunami HRM | ALIGNED. |
| ITINTR set on timer fire               | System.cpp:1762: `state.cchip.misc |= 0xf0` (sets bits 4..7 for all 4 CPUs) | TsunamiCchip::fireIntervalTimer sets MISC<ITINTR> = 0xF + asserts pendingIrq2 | Tsunami HRM | ALIGNED. |

---

# Topic 6: SRM firmware load + Step D relocation

| Aspect | AXPBox | V4 | Cross-check | Status |
| ------ | ------ | -- | ----------- | ------ |
| Firmware load PA                       | System.cpp::LoadROM lines 1569-1668: load at PA 0x900000 only | systemLib/SrmLoader.cpp: load at 0x900000 only (post-2026-05-19 rewrite) | -- | ALIGNED -- V4 adopted AXPBox model. |
| Initial PAL_BASE                       | Set from firmware header field (0x900000) | descriptor.initialPalBase = loadPa = 0x900000 | -- | ALIGNED. |
| HW_MTPR HW_PAL_BASE relocation         | Tracked via set_PAL_BASE path | machine.cpp::onBeforeFetch detects PC at descriptor.entryPa, sets palImageRelocated, target palBase = 0x600000 | -- | DIVERGENT but functionally equivalent. |

---

# Topic 7: FP load/store

(Already comprehensively addressed in EV6_audit_2.0.md rows for opcodes 0x20-0x27.  ALIGNED across all 8 leaves as of 2026-05-19.)

---

# Pending topic areas (TODO scaffold expansion)

-   Cbox CSR shift register (C_DATA / C_SHFT / chain logic)
-   SROM / boot-time firmware decompression
-   Console device modeling (UART, NVRAM, serial line)
-   PCI device enumeration and BAR setup
-   AXPBox's `state.check_int` vs V4's per-step canAcceptInterrupt poll
-   Snapshot save/load format (different file format, different topology)
-   Per-CPU state isolation (AXPBox has multi-CPU support; V4 is single-CPU)

---

# Change history

-   2026-05-19  Initial scaffold + topics 1-7 first-pass.  CALL_PAL
                linkage register V4-AHEAD finding ratified.
