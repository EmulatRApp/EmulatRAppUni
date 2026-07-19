# Session Journal -- VMB halt at 0x20000000: the ITB-miss frontier

    Doc id      : JRN-VMB-001
    Status      : ACTIVE -- work order for Cowork. Steps 0/1/2 below.
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

## 7. Step 0 findings -- TO BE FILLED BY COWORK

    toFaultCode(ItbMiss)  -> ...
    IBox consumer of :556 -> ...
    PalBox ITB_MISS entry -> ...
    Verdict               -> H1 CONFIRMED / H1 RULED OUT

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
  - **F-3 comment cleanup.** The five walker-promising sites. Should land as its
    own commit with P-1 written into the header as a stated prohibition, not
    merely as deleted TODOs. Gated on sign-off; do not fold it into the Step 1
    probe commit.
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
