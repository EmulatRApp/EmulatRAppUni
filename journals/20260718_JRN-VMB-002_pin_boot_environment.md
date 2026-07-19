# Session Journal -- Pin the boot environment before the 0xa508 frontier

    Doc id      : JRN-VMB-002
    Status      : ACTIVE -- work order for Cowork. E-1..E-5.
    Date        : 2026-07-17
    Supersedes  : JRN-VMB-001 Steps 1-2 ORDERING only. Prohibitions P-1..P-5 of
                  JRN-VMB-001 REMAIN IN FORCE, P-1 especially (no page-table
                  walker; see JRN-VMB-001 F-2).
    Subject     : The 0x20000000 halt stopped reproducing without any fix being
                  applied. Four boot-environment variables are unpinned
                  simultaneously. Pin them, then run a 2-cell firmware matrix.
    Encoding    : ASCII-128. Hex radix.

    NOTE TO COWORK: you are ON the machine. Every question below is answerable
    from the filesystem and the source tree. Do NOT wait for values to be
    relayed through chat -- read them, and write the answers into Section 7.

---

## 1. State change since JRN-VMB-001

  - CDROM-from-IDE boot: still good.
  - JRN-VMB-001 Step 0 (static: toFaultCode / IBox consumer / PalBox ITB_MISS
    dispatch): **never reported.** Section 7 of that journal is still empty. It
    remains outstanding and is folded in below as E-0.
  - JRN-VMB-001 Step 1: probe compiled into `Emulatr.exe` (`ITBPROBE MISS`
    string present), fired **zero times**.
  - The `0x20000000` halt did **not** reproduce. Boot ran to cyc 2,429,550,593
    and halted at `PC = 0xa508`, `palMode = true`, `lastFault = 5
    (kFaultDtbMiss)`, `palBase = 0x8000`, halt code 0.
  - `palBase = 0x900000` pre-run; image header `+0x10 = 0x00600000` matches the
    logged `targetPalBase = 0x600000`.
  - Platform mismatch logged as ERROR: `ini [System] model='ES40'` vs manifest
    `platform='DS20'`, plus south-bridge drift (ES40 -> ALi M1543C, manifest ->
    Cypress).

## 2. The confound -- READ THIS BEFORE INTERPRETING ANY RUN

**No fix was applied.** The only source change authorized by JRN-VMB-001 was a
`printf`. No translator edit, no MMU edit, no dispatch wiring. A bug that stops
reproducing without a fix has not been fixed -- it has been hidden by an
environmental change, and it will return.

At least two things moved between the halting run and the clean run, and both
are serious:

  **C-1. `UPD> u srm` mutated persistent state.** LFU wrote to the flash `.rom`.
  That is not a per-run setting; it persists across resets. The console banner
  read **7.3-2** before the update; LFU reported writing **7.3-1**. That is a
  DOWNGRADE, and it means the firmware under test changed mid-session.

  **C-2. The platform is incoherent** (`model='ES40'` vs `platform='DS20'`), so
  the south bridge identity is contested. `kFaultDtbMiss` deep in OS bootstrap
  on a machine of contested chipset identity is exactly what a guest probing a
  register the wrong south bridge does not decode would produce. `0xa508` may be
  an artifact of the mismatch. Diagnosing it first means diagnosing a machine
  that does not exist.

## 3. What is unpinned

  | Id  | Variable                | State                                       |
  |-----|-------------------------|---------------------------------------------|
  | U-1 | Firmware provenance     | **CLOSED (E-1).** Both images byte-identical, `653a1d95...`, authoritative. Only one image exists; it was never the variable |
  | U-2 | Flash `.rom` state      | **THE variable (D-4).** Persisted; written by LFU; overrides the image (D-1). Pre-LFU content of UNKNOWN provenance and possibly unrecoverable (4.4) |
  | U-3 | Platform identity       | `ini='ES40'` vs `manifest='DS20'`; south bridge ALi M1543C vs Cypress |
  | U-4 | Probe arming            | `ITBPROBE` silence is untested -- the grep proved the STRING is linked, not that the gate ever armed |

Four unpinned variables in the boot environment at once. No run is interpretable
until these are down to zero.

## 4. RESOLVED by E-1 -- the image was never the variable. Flash is.

**E-1 result (2026-07-17): `ds20_v7_3.exe` (authoritative V7.3 CD ISO) and
`ds20_v7_3_save.exe` (the initially-tested image) are BYTE-IDENTICAL, sha256
`653a1d95...565f`, 2045952 bytes.**

There is only ONE firmware image and it is authoritative. U-1 is closed. The
prior two-image version table is **DEAD** -- there was never a 7.3-2 image and a
7.3-1 image. Do not reason from it.

### 4.1 What E-1 forces, by deduction, without a single further run

    Run 1  : banner 7.3-2.  Image = 653a1d95.
    Run 1  : u srm -> flash written, LFU reported 7.3-1.
    Run 2  : banner 7.3-1.  Image = 653a1d95, byte-identical to Run 1.

The image was CONSTANT across both runs. The banner CHANGED. The only thing that
changed was flash content.

  **D-1. The banner is sourced from FLASH, not from the image. Flash overrides
  the image.** This is now established from observation, not hypothesis.

  **D-2. E-2.1 is effectively answered: the seeding policy is seed-if-absent, or
  equivalent.** Confirm it from the source anyway (E-2.1 stands) -- but the
  behavior is already demonstrated.

  **D-3. The "run against the authoritative ISO version" run was a NULL
  EXPERIMENT.** A file was replaced with a byte-identical file. It tested
  nothing. "Unchanged 7.3-1" is exactly what a no-op predicts, and the run
  carries no information about provenance.

  **D-4. The `0x20000000` halt ran on flash-7.3-2. The clean 2.43B run ran on
  flash-7.3-1. The variable was ALWAYS flash content.** Never the image.

### 4.2 The new unknown, and it is bigger than the old one

**Where did flash's 7.3-2 come from?** The one image we have is 7.3-1 (per LFU,
which read it and said so). So flash was NOT seeded from this image -- or was
seeded and then modified by something unrecorded. Candidates, in rough order:

  - An earlier preseed step. Note `probe_es40_preseed` appears in the project's
    own output-naming conventions, so a preseed mechanism exists and has been
    used.
  - An earlier session that loaded a different firmware image no longer present.
  - Hand-built or synthesized flash content from the Flash/NVRAM bring-up work.

Until this is answered, **flash content is an artifact of unknown provenance that
has been the actual firmware under test for every run in this journal.**

### 4.3 Inverted reading -- take this seriously, it reverses C-1

`u srm` read the **authoritative CD** and wrote authentic 7.3-1 into flash. If
flash previously held bogus preseeded content badged 7.3-2, then the LFU update
was **an accidental REPAIR, not a contamination.** Under that reading:

  - The `0x20000000` halt was an artifact of bad flash content, not an EmulatR
    defect.
  - The current state (flash = authoritative 7.3-1) is MORE correct than the
    state that produced the halt.
  - `0xa508` is the real frontier and the ITB thread retires.

This directly contradicts C-1's framing of the LFU as a confound. Both readings
are live. Section 5's redesigned matrix separates them.

### 4.4 TIME-CRITICAL

**Does a pre-LFU backup of the `.rom` exist?** If not, flash-7.3-2 is GONE and
the `0x20000000` halt may be permanently unreproducible -- the only known
configuration that exhibits the bug would be unrecoverable. This is now the most
urgent question in the journal. Check before anything else, and if a backup
exists, copy it somewhere safe immediately.

## 5. Work order

### E-0. Outstanding from JRN-VMB-001 Step 0 -- static, free, unreported

Still owed, still read-only, still may end the ITB thread outright:

  1. Does `mmuLib::toFaultCode` map `TranslationResult::ItbMiss` ->
     `kFaultItbMiss`?
  2. Does the fetch / IBox stage **consume** `translateInstruction`'s non-Success
     return (Ev6Translator.h:556) and raise it? Trace the return value to its
     consumer. Compare against the D-side MEM drainer (wired in C2b). If :556 has
     no consumer, that is H1 confirmed.
  3. Is ITB_MISS's PAL entry offset wired in PalBox, and dispatched?

Answer with file:line evidence. No opinions.

### E-1. Firmware provenance -- one command, do it first

```bash
sha256sum ds20_v7_3*.exe
ls -l    ds20_v7_3*.exe
```

Reference for the image already fingerprinted:

    sha256 653a1d95f6a5b28a084981bdb626bd48efec47929fbd892dd8f5a3df3b01565f
    size   2045952 (0x1f3800), payload ends 0x1f34aa, 854 B zero pad
    header +0x10 = 0x00600000 (load target)

Read the size delta alongside the hash -- it discriminates the cause:

  - Same size, different bytes -> genuinely different build, or in-place
    modification.
  - Different size -> different build, or extraction damage.

Both are non-authoritative, but they fail differently and only one is
recoverable by re-extracting from the ISO.

**Why this is first:** it determines whether every result to date -- the
`0x20000000` halt and the `0xa508` halt alike -- was produced against a known
artifact. If the images differ, prior runs are of unknown provenance and that
outranks every other question here.

### E-2. Flash policy and state -- READ THE SOURCE, do not experiment

**This is the highest-yield item in the journal and it costs a grep.**

  1. **Seeding policy.** Read the flash/NVRAM init path. Does EmulatR seed the
     `.rom` from the `.exe` only when the `.rom` is ABSENT, or on every run?
     - If seed-if-absent: **swapping the `.exe` is a NO-OP while a `.rom`
       exists.** Every run since `u srm` has booted 7.3-1 from flash regardless
       of which image was configured. This single fact would explain the
       "unchanged 7.3-1" banner completely, and would mean the authoritative-image
       run never happened.
  2. **Locate the `.rom`.** Path, size, mtime. Does its mtime coincide with the
     `u srm` run?
  3. **Version in flash.** `strings` the `.rom` and grep for `7\.3`. If EmulatR
     stores it decompressed, this reads the banner directly out of flash and
     settles U-2 without a run.
  4. **Was the `.rom` moved aside before the most recent run?** If not, that run
     did not test the authoritative image.

**Do not delete the `.rom`.** It is the only surviving evidence of what the prior
runs actually executed. Move it aside with a timestamped name.

### E-3. Platform coherence -- free, and a live candidate cause of 0xa508

Resolve `ini [System] model` vs manifest `platform`. They must agree. The south
bridge follows from the choice (ES40 -> ALi M1543C; DS20 -> Cypress CY82C693 --
genuine for the PC264 lineage per the manifest comment).

Note the interaction with JRN-VMB-001 P-3: the ACVPROBE cycle-floor reasoning
assumed ES40. If the machine is actually DS20, that task context does not apply.

### E-4. Probe arm-line -- one printf

`ITBPROBE` silence is currently uninterpretable. The grep proved the string
literal is linked; it did not prove `EMULATR_ITBPROBE_VA` was read, that the gate
matched, or that `s_itbProbeVa` is anything but `~0ULL`.

Emit at gate init, unconditionally when the env var parses:

    ITBPROBE ARMED va=%016llx

If that line is absent from a log, nothing else in that log means what it appears
to mean. This permanently converts an untested null into evidence.

### E-5. The matrix -- REDESIGNED after E-1. The axis is FLASH, not the image.

The original two-cell matrix is **VOID**: both cells specified the same image,
because both images are the same file (E-1). Image is a constant and cannot be an
experimental axis. Flash content is the variable (D-4), so flash is the axis.

Image is `653a1d95` (the only one) in every cell. Platform coherent (E-3), probe
armed (E-4).

  | Cell | Flash state                        | Purpose / prediction                |
  |------|------------------------------------|-------------------------------------|
  | A    | ABSENT -- deleted/moved aside      | Flash is seeded from the image, so the banner reveals **the image's true version**. Free, and it settles the question no static analysis could |
  | B    | pre-LFU backup restored (7.3-2)    | Expect halt at `0x20000000`. **Requires the backup to exist (4.4)** |
  | C    | current post-LFU (7.3-1)           | Already observed: runs to 2.43B, halts `0xa508` |

**Run cell A first.** It is free, it needs nothing that might not exist, and it
answers the original question directly:

  > "What version is `ds20_v7_3.exe`?" -- unanswerable by static inspection (the
  > payload is compressed under DEC's proprietary scheme; no plaintext banner, no
  > standard codec). But with flash absent, the image seeds flash, and the boot
  > banner IS the image's version. **Clear flash, boot, read the banner.**

Cell A predictions and what each means:

  | Cell A banner | Meaning                                                     |
  |---------------|-------------------------------------------------------------|
  | `7.3-1`       | Image is 7.3-1, consistent with LFU. Confirms flash-7.3-2 came from somewhere else entirely (4.2). The preseed/earlier-session hunt is on |
  | `7.3-2`       | Image is 7.3-2 -- and LFU's "7.3-1" came from the mounted CD, a DIFFERENT source than the boot image. Two firmware sources in play; both must be pinned |
  | neither       | Flash was not seeded from the image, or seeding is not seed-if-absent. D-1/D-2 are wrong. Stop and re-derive |

**Verify the banner on every cell before reading its result.** A cell whose
banner does not match its intended firmware is void.

Cell B is the one that matters for the ITB frontier, and it may already be
unreachable. See 4.4 -- check for the backup NOW.

### E-6. Snapshot-based state capture -- likely obsoletes JRN-VMB-001 Step 2

Step 2's questions were always memory-state questions, not control-flow ones. A
snapshot answers them without a retire trace, a probe, or a 547KB log.

#### E-6.1 Trigger design -- the gate question

**DO NOT gate on a command typed at the guest `P00>>>` prompt.** Three reasons:

  1. **Determinism.** The trigger would fire at whatever cycle the console parser
     reached, which depends on when a human typed. Two runs would snapshot at
     different cycles, and the A-vs-A calibration (E-6.4) becomes untestable --
     the diff would have no noise floor.
  2. **Boundary.** EmulatR sniffing guest console traffic for a magic string is a
     host-side side effect driven by guest I/O, with no hardware analog. That is
     the same class of object as `EMULATR_BOOTSTRAP_ITB_BYPASS` (JRN-VMB-001
     P-2): a debug hack wired into the execution path. It would need a
     prohibition of its own within a month.
  3. It cannot run unattended or scripted.

**DO gate on, in order of preference:**

  - **Halt.** Already an event; needs no new machinery. Sufficient for all three
    reads below -- a halt does not disturb guest memory, and the IPRs are CPU
    state the snapshot already owns.
  - **PC watchpoint.** Deterministic, replayable, no guest coupling. Use for a
    pre-jump capture if the halt is found to perturb state (E-6.5).
  - **Host-side SnapshotCli.** Already built. Correct for opportunistic dumps
    where determinism is irrelevant (E-6.3 S-A). The human drives the HOST, not
    the guest console, so the boundary holds.

#### E-6.2 The three reads -- READ, do not diff

The signal is not "what changed." It is "what is at these three addresses."

  | Id  | Location                            | Question                        | Splits                              |
  |-----|-------------------------------------|---------------------------------|-------------------------------------|
  | R-1 | guest PA `0x5bc000`                 | Is the image actually there?    | "stored to wrong PA" vs not         |
  | R-2 | page table `0x3ff04000`, entry for VA `0x20000000` | Does the PTE point at `0x5bc000`? | PTE content wrong vs right |
  | R-3 | IPRs `vptb`, `va_ctl`               | `vptb != 0 && (va_ctl & 0xFFFFF80000000000) == 0` ? | **H2 stranded base** |

**R-3 is the H2 predicate answered from captured state** rather than inferred
from a `printf`. If R-3 is true, H2 is confirmed and the E-4 / Step 1 probe is
redundant.

#### E-6.3 Placement -- pre-boot and post-boot, refined

The proposal was: snapshot at `P00>>>` before boot, and after the boot attempt.
The critical capture is **neither of those two, exactly**:

  - **At `P00>>>`:** the image is not loaded and the page table is not built, so
    R-1/R-2/R-3 are all unanswerable here. But it is valuable for a different
    reason: the decompressed SRM console is resident in RAM. `strings` it, find
    the banner, and you have **the image's true version** -- the thing static
    analysis could not reach through DEC's compression. This retires E-5 cell A
    without a dedicated run.
  - **At the halt:** image loaded, page table built, IPRs set. Answers
    R-1/R-2/R-3. **This is the money shot.**

  | Id  | Trigger                          | Purpose                                  |
  |-----|----------------------------------|------------------------------------------|
  | S-A | `P00>>>`, host-side SnapshotCli  | `strings` -> image version. Retires E-5 cell A |
  | S-B | halt                             | R-1 / R-2 / R-3                          |

#### E-6.4 A-vs-A calibration -- MANDATORY before trusting any diff

Run the same config twice, snapshot both, diff. If A-vs-A is not clean, the diff
tool is worthless before you start -- learned for one run instead of by misreading
a real result.

**Caveat that follows from E-6.1(1):** if console input is human-typed, the two
runs are not cycle-reproducible, so anything time- or cycle-derived (RSCC,
timers, counters) will differ. **That delta set IS the noise floor** --
characterize it and subtract it, or script the console input to eliminate it at
the source.

V4's determinism-first sequencing makes a clean result likely, which is exactly
why it is worth confirming: a clean A-vs-A turns the diff from suggestive into
evidence, and any drift found is itself a determinism bug worth having.

#### E-6.5 Non-perturbation check

Capture-side `flush()` drains the pipeline. Run with and without the trigger
armed and confirm the halt PC is unchanged. A diagnostic that changes the outcome
is diagnosing itself.

#### E-6.6 What a snapshot CANNOT see

  - **H1 is invisible to it.** "Does `ItbMiss` at `:556` reach the PAL vector?" is
    control flow. Memory state is identical whether the fault dispatched or got
    swallowed. **E-0's grep remains the only answer to H1, and the cheapest item
    on the board.**
  - It does not rescue 4.4. No pre-LFU `.rom` -> no 7.3-2 boot -> nothing to
    snapshot for cell B.

#### E-6.7 Status of JRN-VMB-001 Step 2

**LIKELY OBSOLETE.** If E-6.2's three reads land, the bounded retire trace never
happens. Do not run Step 2 until E-6 is exhausted.

## 6. Verdict table

Keyed to the redesigned (flash-axis) matrix in E-5.

  | Outcome                                   | Conclusion                       |
  |-------------------------------------------|----------------------------------|
  | B halts at 0x20000000, C does not         | **Flash-content-specific defect CONFIRMED on a pinned baseline.** JRN-VMB-001 Steps 0-2 resume against cell B. The ITB frontier is real |
  | B does not halt                           | The halt was an artifact of transient/dirty flash state, not reproducible even from the same `.rom`. Retire the ITB frontier. `0xa508` becomes the frontier |
  | No pre-LFU backup exists (4.4)            | Cell B unreachable. The `0x20000000` halt is unreproducible. Record it as OBSERVED-NOT-ROOT-CAUSED in the known-gaps tracker and move to `0xa508`. Do NOT quietly drop it |
  | Cell A banner != cell C banner            | Confirms flash overrides the image (D-1) and identifies the image's true version. Expected |
  | Cell A banner == neither 7.3-1 nor 7.3-2  | D-1/D-2 are wrong; seeding is not what we think. Stop and re-derive from E-2.1 |
  | Any cell's banner != its intended firmware | Cell void. Return to E-2.1 |
  | E-6.2 R-3 STRANDED = true                 | **H2 CONFIRMED from captured state.** Fix is VPTB propagation / VA_FORM base, NOT the translator (P-1). Step 1 probe and Step 2 trace both redundant |
  | E-6.2 R-1 false (no image at 0x5bc000)    | "Stored to wrong PA." Not an MMU bug at all -- the load path is the frontier |
  | E-6.2 R-1 true, R-2 false                 | PTE content wrong. Page-table BUILD is the frontier, still PALcode/SRM territory |
  | E-6.2 R-1 true, R-2 true, R-3 false       | Image present, PTE correct, base not stranded -> the fill or the dispatch lost it. Back to E-0/H1 |
  | E-6.4 A-vs-A not clean                    | Diff is unusable. Characterize the delta set as noise floor, or script console input. Also: any drift here is itself a determinism bug worth having |

## 7. Findings

    E-1   sha256 ds20_v7_3.exe      -> 653a1d95f6a5b28a084981bdb626bd48
                                       efec47929fbd892dd8f5a3df3b01565f
          sha256 ds20_v7_3_save.exe -> IDENTICAL
          size                      -> 2045952 (0x1f3800), both
          VERDICT                   -> CLOSED. One image. Authoritative.
                                       Image is NOT the variable (D-4).

FILLED BY COWORK 2026-07-18:

    4.4   pre-LFU .rom backup?      -> NONE existed.  BUT a critical correction:
          the CURRENT flash STILL reproduces the halt.  Tim's plain boots (no
          LFU) on the live .rom halt at 0x20000000 on BOTH dqa0 and dqa1
          (JRN-VMB-001 Sec 7.6).  So the reproducer was NOT lost -- it was the
          live flash all along.  Preserved it NOW before moving aside:
            flash_backup_20260718_004421.rom  sha256 11a2c656d7e0...c88894
            (+ ds20_v7_3.rom.preseed_20260718_004421.bak, same hash)
          This CONTRADICTS Section 4/D-4's "post-LFU flash 7.3-1 = clean 2.43B"
          model: the current (post-everything) flash HALTS.  So either a later
          run re-mutated flash, or the flash-version->behavior map is wrong.
          Flag for web.  The one 2.43B run had "u srm" IN FRONT of it and its
          banner still read 7.3-2 even after the "u srm -> 7.3-1" -- i.e. the
          LFU may not have changed the running image at all.

    E-0  STEP 0 (H1) -- RULED OUT (file:line):
    E-0.1 toFaultCode(ItbMiss)      -> kFaultItbMiss.  mmuLib/TranslationResult.h:81
    E-0.2 consumer of the return    -> CONSUMED, not swallowed.  IF stage
          pipelineLib/PipelineDriver.h:167-179 sets faultCode=toFaultCode(itr)
          (:176) + retire() (:177); retire() :1288 entryForFault -> :1413
          cpu.pc = computeHwExceptionEntry(palBase,0x580)|1.  Same unified
          retire() as the D-side (settles JRN-001 F-6).
    E-0.3 PalBox ITB_MISS dispatch  -> WIRED.  Ev6EntryVectors.h:84
          kEntry_ITB_MISS=0x580; :219 entryForFault(6)->ITB_MISS; dispatched
          PipelineDriver.h:1413-1416.  H1 RULED OUT.

    E-2.1 flash seeding policy      -> SEED-IF-ABSENT (confirms D-2).
          chipsetLib/FlashRom.cpp: loadRaw sets m_backingLoaded=true when the
          .rom exists (:216); seedFrom (copy from .exe) runs ONLY when loadRaw
          found no backing image (:224-226 "a persisted flash ... always
          wins").  While a .rom exists the .exe is a NO-OP.
    E-2.2 .rom path/size/mtime      -> out/build/relwithdebinfo/firmware/
          ds20_v7_3.rom, 2097152 (0x200000), mtime 2026-07-18 00:22:46 (was
          NEWEST artifact).  NOW moved aside (see 4.4); flash is ABSENT.
    E-2.3 version string in .rom    -> NONE (strings finds no 7.3 banner; flash
          stores the SRM image non-plaintext).  Note the live .rom hash
          (11a2c656...) differs from the .exe (653a1d95...) and is 2MB vs the
          .exe's 2045952 -- flash was seeded then MUTATED, or seeded from a
          different source.
    E-2.4 where did flash's content come from? -> UNRESOLVED.  Candidates per
          Sec 4.2 (preseed / earlier session / bring-up).  The live .rom is
          NOT a byte copy of the .exe (E-2.3), so it was mutated after seed
          (LFU program writes, SRM env writes) or seeded from elsewhere.

    E-3   platform resolved to      -> DS20.  APPLIED: set model=DS20 in the
          run-dir + RelWithDebInfo config/EmulatrV4.ini (committed).  KEY
          GOTCHA: IniLoader reads EmulatrV4.ini ONLY (IniLoader.cpp:223-232);
          the earlier DS20 "fix" went to the renamed config/Emulatr.ini, which
          the loader NEVER opens -- so every prior run silently loaded ES40.
          The EmulatrV4->EmulatR rename is INCOMPLETE (content moved, loader
          search-name did not).  Completing it in IniLoader.cpp is a separate
          sign-off item.

    E-4   ARMED line emitted?       -> IMPLEMENTED + committed.  mmuLib/
          Ev6Translator.h top of translateInstruction emits once at first
          I-fetch: "ITBPROBE ARMED va=..." or "ITBPROBE NOT-ARMED ...".  Gated
          EMULATR_BRINGUP_PROBES (default OFF) -> needs -DEMULATR_BRINGUP_
          PROBES=ON.  Not yet observed in a run (build+run pending).

    E-6  CAPTURE WORKFLOW -- BUILT (web's deterministic way, E-6.1):
          tools/snap_vmb_capture.sh.  Uses --snapshot-on-pc 0x20000000,0x20000001
          --snapshot-name-tag vmb_entry_<src> --autosnapshot on, probe armed.
          NOT the guest oem_string console trigger (E-6.1 rejects it;
          Tim's set-oem_string-preboot/postboot idea was put to Tim who chose
          web's --snapshot-on-pc, so NO console-snapshot source edit was made).
          Produces per boot: predig_vmb_entry_<src>_cyc<cyc>.axpsnap (the
          0x20000000-state money shot for R-1/R-2/R-3), auto_halt_*.axpsnap,
          and the console banner (= image TRUE version, E-5.A / S-A).
          Run twice: SRC=dqa0 then SRC=dqa1 (E-6.4 A-vs-A).

    E-5.A flash absent: banner      -> PENDING run (env now pinned for cell A).
    E-5.B flash = pre-LFU backup    -> N/A pre-LFU; cell B == the preserved live
          .rom (flash_backup_20260718_004421.rom), which DOES halt.  Restore it
          to reproduce the halt after cell A.
    E-6.4 A-vs-A diff clean?        -> PENDING runs.
    E-6.5 halt PC unchanged w/ trigger armed? -> PENDING (run with/without
          --snapshot-on-pc, compare halt PC).
    E-6.2 R-1 image at PA 0x5bc000? -> PENDING (read from the captured snapshot).
          R-2 PTE @0x3ff04000 -> 0x5bc000? -> PENDING.
          R-3 STRANDED (vptb!=0 && (va_ctl & 0xFFFFF80000000000)==0)? -> PENDING.
    S-A   image version             -> read the console banner in the cell-A log.

    PINNING ACTIONS DONE (2026-07-18):
      1. flash .rom moved aside (backup + .preseed.bak, both 11a2c656...),
         flash now ABSENT -> next boot = cell A (fresh authoritative .exe seed).
      2. platform EmulatrV4.ini model ES40 -> DS20 (run-dir + RelWithDebInfo).
      3. E-4 ITBPROBE ARMED arm-line committed.
      4. tools/snap_vmb_capture.sh committed (E-6 capture).
    NEXT (Tim): rebuild -DEMULATR_BRINGUP_PROBES=ON, then
      SRC=dqa0 bash tools/snap_vmb_capture.sh ; SRC=dqa1 bash tools/snap_vmb_capture.sh
    Reading R-1/R-2/R-3 out of the .axpsnap needs a small snapshot inspector
    (not built yet) or a load+dump -- flagged as the next Cowork step.

    VERDICT -> environment PINNED (image constant, platform DS20, flash cell A,
      probe armed).  The image-version matrix is dead (E-1); the live axis is
      flash content (D-4) with the 4.4 contradiction to resolve.  Run cell A +
      capture; R-1/R-2/R-3 from the snapshot then answer the ITB question per
      Section 6, without a retire trace (JRN-001 Step 2 stays obsolete).

## 8. Prohibitions still in force

From JRN-VMB-001, unchanged:

  - **P-1. No page-table walker.** The EV6 has none; TB misses trap to PALcode
    (HRM 6.9). If any step here seems to call for one, STOP and report.
  - **P-2.** Do not resurrect `EMULATR_BOOTSTRAP_ITB_BYPASS`.
  - **P-3.** Do not inherit Hook A's `CYC_FLOOR=248000000`.
  - **P-4.** The I-side probe is `ITBPROBE`, not `ACVPROBE`.
  - **P-5.** No source edits beyond the E-4 arm-line without sign-off.

New:

  - **P-6. Do not run LFU (`u srm` / `UPD>`) during a diagnostic run.** It mutates
    flash and destroys the baseline. If a run needs a specific firmware, seed it
    deliberately and record what was seeded. NOTE per 4.3: the LFU that triggered
    this journal may have been an accidental REPAIR rather than a contamination.
    P-6 stands regardless -- the objection is to unrecorded mutation of the
    baseline, not to the direction of the change.
  - **P-7. Do not chase `0xa508` until E-1..E-3 are closed.** It is being chased
    on a machine whose firmware, flash contents, and chipset identity are all
    unfixed.

## 9. Output conventions

  - Script body -> `D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\`
  - Run / console logs -> `{run-dir}/logs/`
  - Retire / CPU traces -> `{run-dir}/traces/` (plural)
  - Naming: `purpose_YYYYMMDD_HHMMSS.ext`
  - `mkdir -p logs traces` from the run dir first. Nothing loose in the run-dir
    root.
  - Moved-aside flash: `{run-dir}/flash_backup_YYYYMMDD_HHMMSS.rom`. Never
    deleted.
  - **Snapshots -> `{run-dir}/snapshots/` `_PROVISIONAL`.** The established
    convention names `logs/` and `traces/` only; a snapshot is neither -- it is
    captured state, not a stream. Proposed:
    `snapshots/snap_<trigger>_YYYYMMDD_HHMMSS.snap`, e.g.
    `snap_p00prompt_20260717_154056.snap`, `snap_halt_20260717_154212.snap`.
    Flag for a convention amendment rather than dumping them in `traces/`.
  - ASCII-128 in every artifact.
