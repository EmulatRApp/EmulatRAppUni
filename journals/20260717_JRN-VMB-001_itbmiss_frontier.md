# Session Journal -- VMB halt at 0x20000000: the ITB-miss frontier

    Doc id      : JRN-VMB-001
    Status      : STEP 1 RUN -- premise no longer reproduces (see Section 7.5).
                  0x20000000 halt is GONE; halt moved to PC 0xa508 (DtbMiss),
                  cyc 2.43B. Direction pending web. Steps 0/1 done; Step 2 N/A.
    Date        : 2026-07-17
    Platform    : ES40 _PROVISIONAL (inferred from the ACVPROBE task context in
                  Ev6Translator.h:325-331; CONFIRM before running)
    Subject     : CDROM-from-IDE boot works end to end; VMB halts at the first
                  fetch. Translator path proven correct. The walker that the
                  header's TODOs promise must never be built.
    Artifacts   : mmuLib/Ev6Translator.h (565 lines, read this session)
    Encoding    : ASCII-128. Hex radix.

---

## 1. Milestone -- CDROM-from-IDE boot works end to end

Recorded as done. The chain, all observed:

  - SRM read 627,712 real bytes off the CD and loaded them. Console reports
    `base = 5bc000`.
  - SRM built a page table at `0x3ff04000`.
  - SRM mapped VA `0x20000000` -> the loaded image.
  - SRM jumped to the bootstrap.

The IDE/ATAPI path is no longer the frontier. This closes the CD-boot thread and
opens a new one.

## 2. The halt

VMB halted at exactly `PC = 0x20000000` -- offset 0, the *first* fetch.
`0x00000000` decodes to `CALL_PAL HALT`. Therefore the fetch at VA `0x20000000`
returned zeros.

The image bytes demonstrably exist in memory (we watched them come off the CD).
So VA `0x20000000` is not resolving to the physical page holding the bootstrap.

## 3. Findings

**F-1. The translator followed the correct path. Both VA=PA bypasses are ruled
out.**

  - `EMULATR_BOOTSTRAP_ITB_BYPASS` (Ev6Translator.h:525-534) is **not in the
    build**. The macro is defined nowhere -- not in CMakeLists, not in the cache,
    not as a `#define`. Further: `kBootstrapVaLo` / `kBootstrapVaHi` are
    referenced only inside those `#ifdef` blocks and are **never defined
    anywhere**, so if the macro were on, the file would not compile. It compiles.
    The block is dead. Ruled out.
  - PAL-mode bypass (:511). `inPalMode()` is `(pc & 1)`. The halt PC is
    `0x20000000`, bit 0 clear, and the dump reads `palMode = false`. Does not
    fire.

  So the fetch went: alignment -> not PAL -> canonical (passes) -> kseg (no
  match; `0x20000000` fits no SPE window) -> **ITB lookup -> miss ->
  `TranslationResult::ItbMiss`** (:556).

  This reframes the bug. It is not path selection. It is translation content, or
  what happens to the miss.

**F-2. ARCHITECTURAL -- the EV6 has no hardware page-table walker. There is no
walker to build.**

  TB misses on Alpha trap to PALcode, which performs the walk in software. Primary
  source: 21264/EV67 HRM section 6.9, "Translation Buffer (TB) Fill Flows,"
  states it shows the expected **PALcode flows** for DTB miss and ITB miss. The
  `DTBM_DOUBLE_3` entry point is described as a Dstream TB miss on virtual page
  table entry fetch, with the instruction to **use the three-level flow**.

  "Three-level flow" names a **PALcode routine**, not a hardware unit. EmulatR
  already supports three-level translation. It shipped inside the firmware.

  Corroboration from our own tree: `VA_FORM` / `IVA_FORM` exist for exactly one
  purpose -- to hand PALcode the *virtual address of the PTE* so its software walk
  is a single load instead of arithmetic. A CPU with a hardware walker has no
  reason to expose that register. Same for `HW_MTPR ITB_TAG` / `ITB_PTE`: those
  are how PALcode installs what it found. Both are hardware assists **for** a
  software walk. Their existence is the proof.

**F-3. The header's comments promise a walker that must never arrive.**

  The implemented behavior at :547-556 is correct and states the real contract:
  *"On miss, ItbMiss -> kFaultItbMiss -> PALcode ITB-miss vector refills (HW_MTPR
  ITB_TAG/PTE) and retries the fetch."* That IS the Alpha contract.

  But five sites promise otherwise, and they are an open invitation to "finish"
  the translator:

  | Line   | Text                                                              |
  |--------|-------------------------------------------------------------------|
  | :33    | "the parts of EV6 translation that do NOT require a TLB or a page-table walker" |
  | :43-45 | "When the page walker and TLB land they slot in between the kseg detector and the miss return" |
  | :56-57 | "When the page walker arrives it will need physical reads to fetch PTEs" |
  | :258   | "5.TODO (page walk -- not yet implemented) -> DtbMiss"             |
  | :494   | "6. TODO (ITB walk -- not yet implemented) -> ItbMiss"             |

  The TLB half of :43-45 landed at C3. The walker half is a category error.

**F-4. `translateInstruction` has ZERO instrumentation. The I-side is blind.**

  All three ACVPROBE hooks live in `translateData` (:261-435):

  | Hook | Line | Path                                    |
  |------|------|-----------------------------------------|
  | B    | :325 | DTB hit, permission-denied              |
  | A    | :369 | DTB miss (before `return DtbMiss` :435) |
  | C    | :402 | DTB, exact-VA pointer-root capture      |

  `translateInstruction` (:496-557) has none. The `return
  TranslationResult::ItbMiss` at :556 is bare. **Even with
  `EMULATR_BRINGUP_PROBES` on, the fetch at `0x20000000` emits nothing.**

**F-5. The fetch at `0x20000000` is plausibly the FIRST ITB miss in the entire
boot.**

  Ev6Translator.h:41-45 says it outright: the kseg/PAL paths are *"enough to run
  console / kernel-kseg test cases end-to-end through the pipeline **without page
  tables existing**."*

  The SRM console has never needed the ITB. VMB is the first code to run at a
  translated VA. So the ITB-miss -> PALcode path has never carried load, and this
  is the exact moment it first would.

**F-6. Comment asymmetry between the D-side and the I-side.**

  - D-side (:315-318) names a concrete mechanism and cites the ticket that built
    it: *"the MEM drainer maps it to kFaultDtbMiss ... (HW_MTPR DTB_TAG0/PTE0,
    **wired in C2b**)"*.
  - I-side (:547-549) names no mechanism and cites no ticket.

  May be nothing. May be the bug. Step 0 settles it.

## 4. Hypotheses

Both produce zeros at exactly offset 0. Both fit every observation. They are
distinguishable.

  **H1 -- `ItbMiss` is returned but never vectored.** The IBox fetch path swallows
  the non-Success return, the fetch yields `0x00000000`, that decodes to
  `CALL_PAL HALT`, and we halt at precisely the first fetch. Consistent with F-5
  (path never exercised) and F-6 (no named mechanism on the I-side).

  **H2 -- PALcode ran, and `VA_FORM` handed it a stranded base.** The defect our
  own Hook B comment already describes (:340-346): `vptb != 0` while
  `va_ctl<63:43> == 0`. If VA_FORM's base is stranded at zero, PALcode's ITB_MISS
  handler computes the VPTE address near zero, loads a zero PTE, installs PFN 0,
  and the retried fetch reads PA `0x0` -- zeros. A stranded base breaks *every*
  ITB fill, which is exactly consistent with "the first translated fetch in the
  boot dies."

  **H3 (residual) -- the handoff left `PC<0>` set and the console masked bit 0 when
  printing `PC = 20000000`.** This is the only surviving route to the physical
  bypass at :511. Killed or confirmed by capturing the RAW pc at the miss (Step
  1). Low prior -- the dump says `palMode = false` -- but it is free to test and it
  is the user's original hypothesis, so test it rather than argue it.

## 5. Prohibitions -- READ BEFORE EDITING

  **P-1. Do NOT add a page-table walker to `Ev6Translator.h`, or anywhere in
  C++.** See F-2. A C++ walker would be a second, competing translation path that
  races real PALcode, produces right answers for wrong reasons, and shadows PAL
  bugs -- the identical failure mode to `EMULATR_BOOTSTRAP_ITB_BYPASS`. It would
  also execute in zero guest cycles and silently forfeit the cycle-accuracy
  litmus, because the walk's cycles come from retiring real PAL instructions.
  This translator never reads a PTE from memory. **The miss returns ARE the
  interface to PALcode; they are not stubs.**

  **P-2. Do NOT re-enable or resurrect `EMULATR_BOOTSTRAP_ITB_BYPASS`.** It is
  dead (F-1) and it must stay dead. It would mask exactly the bug we are hunting.

  **P-3. Do NOT inherit Hook A's cycle floor.** `EMULATR_HOOKA_CYC_FLOOR` defaults
  to `248000000`, tuned to the ES40 memtest ACV window. The VMB jump is nowhere
  near it. A probe inheriting that floor emits nothing and reads as a null
  result.

  **P-4. Do NOT name the new probe ACVPROBE.** That family is scoped to the
  memtest ACV task. This is a different frontier; the name will mislead a reader
  in three months. Use `ITBPROBE`.

  **P-5. No source edits beyond the Step 1 probe land without sign-off.** Step 0
  is read-only. Step 2 is gated on Steps 0 and 1.

## 6. Work order

### 6.1 Step 0 -- STATIC. No build, no run, no edits. Do this first.

Cheapest rung and it may end the investigation outright. If `ItbMiss` never
vectors, PAL never runs, `IVA_FORM` is never read, and any trace comes back empty
-- a null result indistinguishable from a stranded base. **Do not skip to Step 1
or 2 before answering these three.**

  1. Does `mmuLib::toFaultCode` map `TranslationResult::ItbMiss` ->
     `kFaultItbMiss`? (`mmuLib/TranslationResult.h`)
  2. Does the fetch / IBox stage **consume** `translateInstruction`'s non-Success
     return and raise it? Compare side by side against how the MEM drainer
     consumes `DtbMiss` -- the D-side has a named drainer wired in C2b, the I-side
     names nothing (F-6). Trace the return value from :556 to its consumer. If it
     has no consumer, that is H1, confirmed, and the investigation is over.
  3. Is ITB_MISS's PAL entry offset wired in PalBox? Is it dispatched?

**Deliverable:** three answers, quoted with file:line evidence. Written back into
this journal under Section 7. No opinions -- what the code does.

**Exit:** any gap -> H1 CONFIRMED. Stop. Report. The fix is the dispatch, not a
walker (P-1).

### 6.2 Step 1 -- probe the ItbMiss return. Only if Step 0 is clean.

**The key property: H2 is testable AT the miss, before PALcode executes a single
instruction.** If `vptb != 0 && (va_ctl & 0xFFFFF80000000000) == 0` at the moment
of the ITB miss, the stranded base is confirmed -- the walk PAL is about to
perform is *guaranteed* to read the wrong VPTE address. We do not need to watch
it fail. This is one instruction earlier and vastly cheaper than tracing the
walk, and it is the exact predicate the Hook B comment already spelled out.

Add one probe immediately before `return TranslationResult::ItbMiss;` at :556.
Model its shape on Hook C (:402-420, exact-VA gate), **not** Hook A (P-3).

```cpp
#if defined(EMULATR_BRINGUP_PROBES)
        // ITBPROBE (2026-07-17, JRN-VMB-001): the I-side is otherwise blind --
        // Hooks A/B/C all live in translateData.  Per F-5 this is very likely
        // the FIRST ITB miss the boot has ever taken, so capture the full miss
        // state.  Deliberately NOT named ACVPROBE (P-4): different task.
        // Gate is an exact VA match (Hook C pattern), NOT Hook A's cycle floor.
        {
            static uint64_t s_itbProbeVa = ~0ULL;
            static bool     s_itbProbeInit = false;
            static unsigned s_itbProbeHits = 0;
            if (!s_itbProbeInit) {
                s_itbProbeInit = true;
                char const* e = std::getenv("EMULATR_ITBPROBE_VA");
                if (e != nullptr) {
                    s_itbProbeVa = std::strtoull(e, nullptr, 0);
                }
            }
            if (s_itbProbeVa != ~0ULL && va == s_itbProbeVa
                && s_itbProbeHits < 16u) {
                ++s_itbProbeHits;
                uint64_t const vaFormBase = cpu.va_ctl & 0xFFFFF80000000000ULL;
                std::printf(
                    "ITBPROBE MISS n=%u cyc=%llu pal=%d va=%016llx mode=%d "
                    "asn=%u ispe=%llx vptb=%016llx vactl=%016llx "
                    "vaform_base=%016llx STRANDED=%d\n",
                    s_itbProbeHits,
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<int>(cpu.inPalMode()),
                    static_cast<unsigned long long>(va),
                    static_cast<int>(cpu.mode),
                    static_cast<unsigned>(cpu.asn),
                    static_cast<unsigned long long>(cpu.i_spe),
                    static_cast<unsigned long long>(cpu.vptb),
                    static_cast<unsigned long long>(cpu.va_ctl),
                    static_cast<unsigned long long>(vaFormBase),
                    static_cast<int>(cpu.vptb != 0ULL && vaFormBase == 0ULL));
                std::fflush(stdout);
            }
        }
#endif
        return TranslationResult::ItbMiss;
```

**CAUTION -- verify before trusting the `pal=` column.** The whole point of that
field is H3: capture whether `PC<0>` is set at the *fetch*. Hook C uses
`cpu.pcAddr()`, and the name suggests it may already mask bit 0. **Check what
`pcAddr()` returns.** If it masks, `pal=` must come from the raw PC field or from
`inPalMode()` directly -- do not print a masked value and conclude "not PAL," that
would fabricate the answer to the exact question being asked. The sketch above
uses `inPalMode()` for this reason; confirm that accessor reads the unmasked bit.

Notes on the sketch:

  - The `s_itbProbeHits < 16u` cap is deliberate: if PAL live-locks retrying the
    same miss, the repeat count IS the diagnostic. 16 shows the loop without
    flooding.
  - `STRANDED=1` is the H2 verdict, computed inline so it is greppable.
  - Exact-VA gate keeps this out of every other miss once the ITB starts carrying
    real load.
  - Field names are guesses against `CpuState` (`cpu.vptb`, `cpu.asn`,
    `cpu.i_spe`, `cpu.cycleCount`). Verify each against the struct; the
    field-access pattern per :52 is `cpu.va_ctl`, `cpu.mode`, `cpu.m_spe` /
    `cpu.i_spe`. Fix, do not invent.

**Driver script.** Write to `D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\probe_vmb_itbmiss.sh`.
Read an existing sibling in `tools/` first and match its conventions
(self-locating `SCRIPT_DIR`, project discovery, restore-on-exit for anything that
mutates run state, ASCII-only). Shape:

```bash
#!/usr/bin/env bash
# probe_vmb_itbmiss.sh -- JRN-VMB-001 Step 1.  Capture the ITB miss at the VMB
# entry VA.  Requires a build with EMULATR_BRINGUP_PROBES.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="${EMULATR_RUN_DIR:-$SCRIPT_DIR/../out/build/cli}"   # _PROVISIONAL: confirm
cd "$RUN_DIR"

mkdir -p logs traces
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="logs/probe_vmb_itbmiss_${STAMP}.log"

export EMULATR_ITBPROBE_VA=0x20000000

# ... exec Emulatr.exe with the ES40 CD-boot invocation ... 2>&1 | tee "$LOG"

echo "ITBPROBE lines:"
grep -c 'ITBPROBE' "$LOG" || true
grep 'STRANDED=1' "$LOG" && echo "  -> H2 CONFIRMED: VA_FORM base stranded"
```

**Verdict table:**

| Observation                          | Conclusion                                |
|--------------------------------------|-------------------------------------------|
| No `ITBPROBE` line at all            | The miss never happened -> re-examine F-1; or the probe/gate is wrong. Do not conclude anything about PAL |
| `ITBPROBE ... pal=1`                 | **H3.** Handoff left PC<0> set; :511 bypassed to physical. Console masked the bit |
| `ITBPROBE ... STRANDED=1`            | **H2 CONFIRMED** before PAL runs. Fix is VPTB propagation / VA_FORM base, not the translator |
| `ITBPROBE ... STRANDED=0`, n=1       | Miss is clean and PAL should service it -> proceed to Step 2 |
| `ITBPROBE ... n` climbing to 16      | PAL is live-locking on the refill -> Step 2, and the loop itself is the lead |

### 6.3 Step 2 -- bounded retire trace. Only if Steps 0 AND 1 are both clean.

Most expensive rung. Do not start here.

Window: `jumping to bootstrap` -> first fetch at `0x20000000`. Feasible now that
`EMULATR_TRACE_HOOKS` is on and IRQDIAG is off.

Capture the chain:

  1. Dispatch to `palBase + ITB_MISS` offset -- did it happen at all?
  2. `MFPR IVA_FORM` -- what value did PAL read?
  3. The `LDQ` of the PTE -- from what address, and what came back?
  4. `MTPR ITB_PTE` -- what PFN was installed?

Plus two static dumps to bound the answer:

  - Guest PA `0x5bc000` -- is the image actually there?
  - The page-table entry at `0x3ff04000` for VA `0x20000000` -- does it point at
    `0x5bc000`?

Those two split the residual into "image stored to the wrong PA" vs "PTE content
wrong" vs "PTE right but the fill installed something else."

**Output:** traces to `{run-dir}/traces/`, per convention below. This window is
narrow; a per-run subdirectory is unnecessary unless it emits multiple files.

## 7. Step 0 findings -- FILLED BY COWORK 2026-07-17

Read-only static analysis. Live source staged from D:\EmulatR\emulatrappuniv5.
Line numbers are current-tree (post the F-3 comment cleanup, Section 9); the
ItbMiss return web cited as ":556" now sits lower after the added prohibition
block, so anchors below are by code, not the pre-cleanup line number.

    Q1 toFaultCode(ItbMiss) -> kFaultItbMiss
       CONFIRMED.  mmuLib/TranslationResult.h:81
         case TranslationResult::ItbMiss: return coreLib::kFaultItbMiss;
       (enum ItbMiss = 4 at :56).

    Q2 IBox consumer of the non-Success return
       CONFIRMED consumed, NOT swallowed.  pipelineLib/PipelineDriver.h:
         :147   translateInstruction(cpu, cpu.pc, pa) -> itr
         :167   if (itr != Success) {
         :172-176   cpu.va = cpu.pc; cpu.mm_stat = cpu.pc;
                    slot.result.faultCode = toFaultCode(itr);   // = kFaultItbMiss
         :177   retire(slot, cpu);
       The I-side has NO separate drainer (this settles F-6): it converges on
       the SAME unified retire() the D-side uses.  retire():
         :1238  if (r.faultCode != kNoFault)
         :1288  entryOffset = ev6::entryForFault(r.faultCode)
         :1291  if (palBase == 0 || entryOffset == kEntry_None) -> clean halt
         :1412  palModeEnter(cpu)
         :1413  cpu.pc = computeHwExceptionEntry(palBase, entryOffset) | 1
       Residual: the :1291 guard halts if palBase == 0.  For ItbMiss the offset
       is 0x580 (!= kEntry_None), so palBase == 0 is the ONLY non-vector route.
       Step 1 prints palBase to close it.

    Q3 ITB_MISS PAL entry offset wired + dispatched
       CONFIRMED.  coreLib/Ev6EntryVectors.h:
         :84    kEntry_ITB_MISS = 0x580
         :219   case 6: return kEntry_ITB_MISS;   // kFaultItbMiss
       Dispatched at PipelineDriver.h:1413-1416 as
         (palBase & ~0x7FFF) | 0x580 , PC<0> = 1 (enter PAL).
       Note: delivery is by the pipeline retire() hardware-exception path, not
       the CALL_PAL functional dispatch in PalEntries.cpp (that path is for
       CALL_PAL instructions, a different mechanism).

    Verdict -> H1 RULED OUT.
       The ItbMiss return is consumed and vectored to palBase|0x580; it is not
       swallowed.  Corroboration for H2: the halt is an EXECUTED CALL_PAL HALT
       (code 0), which means a value (0x00000000) was successfully fetched at
       0x20000000 -- only possible if the miss WAS serviced and the retry read
       a physical page of zeros (PFN 0).  That is the H2 stranded-base
       signature.  Proceed to Step 1 (probe the miss for STRANDED and palBase,
       and print raw va + pal to test H3).

    Incidental finding (parked, not edited -- see Section 9):
       coreLib/Ev6EntryVectors.h:68-73 and :180-185 still say hardware-trap
       delivery is "NOT yet implemented ... terminates the run rather than
       diverting" and describe entryForFault as feeding "the future
       trap-delivery path."  That is false as of C4 -- the delivery traced in
       Q2 is live.  Same stale-comment class as F-3, different file.

## 7.5 Step 1 run result -- 2026-07-18.  THE PREMISE NO LONGER REPRODUCES.

Run: out/build/relwithdebinfo/logs/probe_vmb_itbmiss_20260717_154056.log
(boot: b dqa1, CD).  Companion aborted-early run: ..._152759.log.

Probe machinery confirmed LIVE, so the zero result is a TRUE NEGATIVE:
  - "ITBPROBE MISS" format string IS compiled into Emulatr.exe (verified in
    the binary), and EMULATR_BRINGUP_PROBES is on (ACVPROBE/PCSAMPLE output
    present in the same log).
  - ITBPROBE fired 0 times.  Reason: there was NO ITB miss at 0x20000000.
    VMB fetched 0x20000000 successfully and kept running.

The halt MOVED.  It is no longer at the first fetch:
  - Boot ran to cyc 2,429,550,593 (~200x further than the old 0x20000000
    halt; well under the 22e9 max, so a genuine CALL_PAL HALT, not a cutout).
  - Final state: PC=0xa508  palMode=true  lastFault=5 (kFaultDtbMiss)
    excAddr=0x1adab0  palBase=0x8000  halt code=0.
  - New frontier is a DATA-side TB miss deep in bootstrap, not the I-side
    ItbMiss this journal targeted.  H1/H2/H3 for 0x20000000 are MOOT here.

Two differences from the earlier 0x20000000-halt run (candidates for WHY):
  1. LFU firmware update.  This run did "UPD> u srm -> Updating to 7.3-1 ...
     PASSED" BEFORE booting; the earlier halt run booted straight from b.
     A different SRM build can build the 0x20000000 page table differently,
     plausibly ending the PFN-0 fill.
  2. Platform mismatch (logged ERROR): ini [System] model='ES40' vs manifest
     platform='DS20' (firmware ds20_v7_3.exe); plus south-bridge DRIFT
     (model ES40 -> ALi M1543C but manifest -> Cypress CY82C693, SbAli=0).
     Console still badges DS20 and boots; incoherence unresolved.

Also noted, non-fatal: a b_irq<1> divert storm (~30 rapid diverts to target
0x8680 = palBase+INTERRUPT, same savedPc=0x1ade64) around cyc 277M; the run
recovered and continued.  Parked as a possible secondary interrupt-redelivery
issue.

pre-run (from ..._152759.log): palBase=0x900000 targetPalBase=0x600000
palMode=1 srmValid=1.  palBase is never 0 -> the retire() :1291 palBase==0
halt residual (Step 0 Q2) is CLOSED regardless.

Direction: PENDING (2026-07-18, "waiting on web").  Open options recorded for
the web variant to adjudicate:
  (A) pivot to the new 0xa508 / kFaultDtbMiss frontier (new journal, Step-0
      static + bounded trace);
  (B) re-run WITHOUT the LFU update to confirm 0x20000000 is genuinely fixed
      vs masked by SRM 7.3-1 (ITBPROBE still armed to catch it if it recurs);
  (C) fix the ES40/DS20 platform mismatch first.
No source edits pending on this; the ITBPROBE + Step 0 findings stay in tree.

## 7.6 Step 1b -- 2026-07-18.  RE-REPRODUCED.  Probe blind: HIT path, not miss.

The 0x20000000 halt reproduces RELIABLY on BOTH sources (b dqa0 AND b dqa1),
identical console: base=5bc000, image_bytes=627712, page table at 0x3ff04000,
"jumping to bootstrap code" -> halted CPU 0, halt code=0, PC=20000000, back to
P00>>>.  (The one CD run that reached cyc 2.43B, Section 7.5, had an LFU
"u srm -> 7.3-1" update in front of it; a plain boot halts at 0x20000000.)

Run probe_vmb_itbmiss_20260717_170551.log: my script (EMULATR_ITBPROBE_VA set),
exe verified to contain the "ITBPROBE MISS" string.  b dqa0 halted at
0x20000000 (line 4887).  ITBPROBE fired 0 times.

DEDUCTION (airtight, by elimination):
  - Halt PC = 0x20000000, bit 0 CLEAR.  translateInstruction's inPalMode() is
    (pc & 1) == 0 for this fetch -> the PAL-mode bypass (:511) CANNOT fire.
    H3 dead by construction (not just by the earlier palMode=false dump).
  - 0x20000000 is canonical and matches no SPE window -> not kseg.
  - That leaves ITB miss (-> probe fires) or ITB hit (-> Success).  Probe
    armed + in binary + 0 fires -> NOT a miss.
  => The fetch is an ITB HIT whose PTE composes PA 0 (PFN 0): reads
     0x00000000 = CALL_PAL HALT.  H2 confirmed in FORM, but it surfaces on
     the HIT path, and there was NO fetch-miss at all -- so the PFN-0 ITB
     entry was installed WITHOUT a miss (SRM/PAL direct HW_MTPR ITB_PTE
     during page-table/HWRPB setup, or a GH superpage from a nearby-VA miss).

CONSEQUENCE FOR THE PROBE: web's Step 1 probe sits at the ItbMiss return and
is structurally blind to a hit-path failure.  The instrument must move/extend
to the r.isHit() return in translateInstruction (and applyTlbHit's compose),
gated on (va & ~1) == target, printing pte.raw / pfn / valid / foe / composed
pa.  This is the PTE-on-the-hit-path signal.  RECOMMENDED probe (Step 1b),
additive, same EMULATR_BRINGUP_PROBES gate, keep the miss probe too (its 0
count is itself the proof there is no miss):

    if (r.isHit()) {
        TranslationResult const hit = applyTlbHit(r.pte, va,
            coreLib::AccessKind::Execute, cpu.mode, pa_out);
    #if defined(EMULATR_BRINGUP_PROBES)
        // gate (va&~1)==EMULATR_ITBPROBE_VA, cap 16, stderr:
        //   ITBPROBE HIT n= cyc= pal= va= mode= pte= valid= pfn= foe=
        //     res= pa= ZEROPFN=
    #endif
        return hit;
    }

Next after Step 1b confirms PFN 0 at the hit: trace WHERE the ITB entry for
0x20000000 was installed -- the HW_MTPR ITB_TAG/ITB_PTE site (palBoxLib /
PalEntries.cpp) with tag matching 0x20000000, and what PFN the guest handler
computed.  That is the Step 2 rung, and it is now a KNOWN-target trace (the
ITB install for one specific VA), not an open-ended retire window.

Direction still PENDING web.  Step 1b probe is staged, NOT yet applied (no
source edit beyond the Section 7 Step 1 probe has landed).

## 8. Output conventions

Binding on every artifact this work order produces:

  - Script body -> `D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\` . Never the repo
    root, never a build dir.
  - Run / console logs -> `{run-dir}/logs/`
  - Retire / CPU traces -> `{run-dir}/traces/` (plural)
  - Naming: `purpose_YYYYMMDD_HHMMSS.ext`, e.g.
    `probe_vmb_itbmiss_20260717_143022.log`
  - `mkdir -p logs traces` from the run dir before writing.
  - Nothing lands loose in the run-dir root.
  - ASCII-128 in every artifact, source and script alike.

## 9. Parked threads

  - **ALi M5229 vs Cypress identity note.** Raised for capture alongside this
    milestone; content not reconstructed here. Tim to supply, or Cowork to pull
    from the ES40 IDE session record. `_PROVISIONAL`.
  - **F-3 comment cleanup. DONE 2026-07-17.** Landed as its own edit to
    mmuLib/Ev6Translator.h, separate from the Step 1 probe (per this note).
    All five walker-promising sites removed (plus a sixth stray "page walk
    path" line in tryKsegTranslate); the two stale path-lists corrected to the
    code as it runs; the I-side ITB comment brought up to D-side specificity;
    and P-1 written into the header AND a dedicated PROHIBITION block ahead of
    the struct (rationale: races PALcode / shadows PAL bugs / forfeits cycle
    accuracy), with a CHANGE 2026-07-17 record. Comment-only, ASCII-128.
    NEW parked item: the same stale-comment class exists in
    coreLib/Ev6EntryVectors.h:68-73 / :180-185 (see Section 7 incidental
    finding); should get the same treatment in its own commit, on sign-off.
  - **Platform confirmation.** This journal assumes ES40 (`_PROVISIONAL`, see
    header). If the CD-boot run is DS20, the ACVPROBE cycle-floor reasoning in
    P-3 still holds but the referenced task context does not.

## 10. Sign-off gates

  | Gate | Condition                                                        |
  |------|------------------------------------------------------------------|
  | G-a  | Step 0 is read-only. No edits. Report before touching source      |
  | G-b  | Step 1 probe is the ONLY source edit authorized by this journal    |
  | G-c  | Step 2 requires Steps 0 and 1 both clean, and explicit sign-off    |
  | G-d  | P-1 is absolute. If any step seems to call for a walker, stop and report instead |
