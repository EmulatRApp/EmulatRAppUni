# Checkpoints — 2026-06-16

## 22:40 — SRM reached `>>>`; snapshot restore proven; interrupt-controller chipset WIP uncommitted

- **Working on:** Post-milestone follow-through now that the SRM console boots
  to the `>>>` prompt. Active coding is on the Tsunami interrupt path / PCI
  groundwork — the working tree has uncommitted edits to `chipsetLib/`
  (`Pic8259Pair.h`, `TsunamiCchip.h`, `TsunamiDchip.h`, `TsunamiPchip.h`,
  `TsunamiChipset.cpp/.h`), `deviceLib/Tsunami/Uart16550.h`,
  `pipelineLib/{PipelineDriver.h,MemDrainer.h}`, `palBoxLib/grains/PalEntries.cpp`,
  `systemLib/Machine.cpp`, `main.cpp`, and `CMakeLists.txt` (all touched
  Jun 16 ~00:15–00:25). None committed yet — last commit is `85216b8`
  (docs/branding cleanup); this is the at-risk WIP a crash would lose.
- **Done since last checkpoint (15 Jun was doc/license only):**
  - **MILESTONE — SRM console reaches `>>>`.** Full cold boot observed:
    64 MB memory, `probing hose 0, PCI`, PCI-to-ISA bridge bus 1, `bus 0 slot 5
    dqa Cypress 82C693 IDE`, GCT/FRU init, LFU `update srm` → 7.3-1 PASSED,
    then `>>>` (SRM V7.3-2, OpenVMS PAL V1.98-83 / Tru64 PAL V1.92-73,
    EV6 pass 1 266 MHz). `show config` renders Cchip/Dchip/Pchip 0/TIG Rev
    7.31/Arbiter cleanly; `dqa0` = EMULATR VIRTUAL CDROM enumerates; ISA block
    (MOUSE 60/12, KBD 60/1, COM1 3f8/4, COM2 2f8/3, LPT1 3bc/7, FLOPPY 3f0/6/2)
    all correct — no regression from the SuperIO (#22) work.
  - **MILESTONE — snapshot restore-to-`>>>` works end to end** ("worked as
    predicted"). An instant SRM prompt is now on tap; the ~90-min cold boot is a
    non-issue for daily work and #21 (the boot warp) drops blocker→nice-to-have.
    Snapshot dir holds the golden near-`>>>` images — do NOT clear it.
  - SCSI/NIC sourcing completed and de-risked: `pka0` = QLogic ISP1020/1040
    modelled as a **mailbox stub** (MBOX0–7 0x70–0x7e, `ISP_CFG0` 0x04 rev
    0x0003=1040; echo reg-test, ack fw load, return checksum-OK, scan = no
    targets — never execute the RISC blob `ql1040_fw.h`). `qlogicisp.c` is the
    authoritative register/mailbox proxy; `qla1280.c` NVRAM layout is optional
    cross-ref. NIC first (#23): `ewa0` = DECchip 21143 "tulip"
    (`drivers/net/tulip/` + apisrm `ew` driver).
- **Open / next:**
  - Commit the uncommitted `chipsetLib/`+pipeline+PAL WIP before further change
    (snapshot it / `git add` — it is the main crash-loss exposure right now).
  - Model `ewa0` (21143 tulip) first, then `pka0` (ISP1040 mailbox stub). The
    one missing source is the **apisrm `isp`/`ew` probe order** (#24 grep):
    which mailbox subset, in what order, the SRM console walks before declaring
    the adapter present — confirm by trace.
  - Resolve NVRAM persistence (#6/#11) — see watch-out.
- **Watch-outs:**
  - **NVRAM settings revert across boot.** After reaching `>>>`,
    `memory_test` read back `full` and `full_powerup_diags` `ON` despite being
    set `none`/`off` earlier; `fpd ON` re-triggers the full diag pass
    (`Testing the System` / `Testing the Disks` / benign `exer: No such
    command`). Either a hard-kill skipped `~Machine::forceFlush`, or
    "Flash ROM writes are disabled" is literal (real persistence bug, ties #11).
    Always end via the sentinel (`touch EMULATR_STOP` → `clean exit at cycle …`),
    never `taskkill`, so the flush runs.
  - **Licensing contradiction still unresolved (carried from 15 Jun):** source
    headers + License page say *eNVy Systems Non-Commercial v1.1*, but
    `Emulatr/LICENSE.md` says *GPL v3.0 + commercial*. Needs the user to declare
    which is authoritative before publishing; License page not yet wired into TOC.
  - **Git push to `EmulatRApp/EmulatR` still blocked** on auth (PAT / SSH;
    `EmulatRApp` is an org, push as a member with Write) — migration pending.

## 23:07 — HP firmware v7.3 corpus decompressed via host oracle; chipset/SMP usefulness mapped

- **Working on:** Analyzing a set of HP/DEC Alpha SRM firmware images and
  classifying them by CPU core vs. system chipset for current and future
  EmulatR use (SMP, Tsunami/Typhoon/Titan). No code changes — analysis +
  deliverables only.
- **Done since last checkpoint:**
  - Decompressed **7 firmware images** using the project's native
    `host_decompressor` oracle (Mark Adler inflate + DEC wrapper), NOT the
    emulated CPU — results trusted. All returned `inflate ret 0` and passed
    the `hw_ret(R2)×3` regression signature; `es45_v7_3` output came back
    **byte-identical** to the cached known-good image.
  - Identified two "found" files: **`palcode-ds10.rom`** is misnamed — it is
    the **complete DS10 SRM console firmware** (SRM + PALcode + AlphaBIOS +
    LFU, 1.9 MB decompressed), self-IDs as DS10/DS10L/XP900/PC264 (21264/
    Tsunami — V4's exact target). **`pc264srm.sys`** is the **PC264 / AlphaPC
    264DP (DS20-family) SRM console** (EV6 reference-board lineage).
  - Saved deliverables to **`D:\EmulatR\Analysis\firmware_v7_3_profile\`**:
    report, JSON summary, and the 7 decompressed `.bin` images.
  - Produced a **CPU-vs-chipset partition** of the corpus (grounded in
    `REFERENCE_INDEX.md`, `chipsetLib/`, `memory.md`): CPU side is a near
    non-issue (EV6/EV67/EV68 share the ISA; only CIX CTPOP/CTLZ/CTTZ delta,
    reported via AMASK/IMPLVER — EV6 core already runs all 7 streams).
    Chipset is the binding constraint. **Tsunami/Typhoon (already modelled):**
    palcode-ds10 (DS10, 1 CPU), pc264srm + ds20 (2-way), es40 (Typhoon, 4-way).
    **Titan (no model, no HRM in `Processor Support`):** ds15 (1), ds25 (2),
    es45 (4) — future-port material.
- **Open / next:**
  - Offered to (a) fold the chipset/SMP breakdown into the profile report and
    (b) add **ds20/es40/pc264 to the `host_decompressor` batch list** so they
    regenerate alongside the es45 regression. Awaiting user go-ahead.
  - SMP escalation ladder on the current chipset: DS10 (UP, done to `>>>`) →
    DS20/pc264 (2-way HWRPB/IPI bring-up) → ES40 (4-way, exercises Typhoon-only
    PWR/CMONCT/per-CPU DIM CSRs no boot test currently hits).
- **Watch-outs:**
  - **SMP secondary still stalls** — `ipcr` is documented storage-only (no IPI),
    so a second CPU can't come up; IPIs ride the Tsunami Cchip (MISC
    IPREQ/IPINTR, DIMn/DRIR mask, TIG halt 0x3C0/0x5C0). This is the gap to
    close for 2-way bring-up.
  - **No Titan chipset model and no Titan HRM on hand** — ds15/ds25/es45 are not
    bootable until a Titan model exists; cheapest spec is diffing each Titan
    image against its Tsunami sibling (es45↔es40, ds25↔ds20) to isolate
    chipset-specific setup.
  - Cross-model rule: 21272 register architecture transfers across DS10/DS20/
    ES40, but per-machine slot/BDF/IRQ topology does NOT — don't use ds20/es40
    images for DS10 PCI slot numbering (relevant to the open PCI-enum blocker).

## 01:07 — Six platform manifests written + CMake wired; ES40 halt-button test ready to run

- **Working on:** Turning the firmware-corpus analysis into runnable EmulatR
  platform configs. Authored per-model platform manifests and wired firmware +
  manifest deployment into the build so an ES40 (Typhoon, 4-way) boot test can
  run. Files written/edited this session (in V4 build tree); not yet committed —
  crash-loss exposure.
- **Done since last checkpoint:**
  - **Wrote 6 platform manifests**, all validated as strict JSON (bash
    `json.tool` pass). **Runnable Tsunami/Typhoon:** `es40_platform.json` +
    `.win` and `ds20_platform.json` + `.win`. **Staged/blocked-on-Titan:**
    `ds25_platform.json` and `es45_platform.json` (clearly marked not-yet-
    bootable). Each carries an honest divergence note rather than faking exact
    topology.
  - **CMake updated** (`CMakeLists.txt`): added firmware images
    `es40cv_v7_3.exe`, `ds20_v7_3.exe`, `ds25_v7_3.exe`, `es45_v7_3.exe` to the
    firmware-copy list; added a Windows-only POST_BUILD block deploying
    `es40_platform.win` / `ds20_platform.win` next to the exe (matches runtime
    `<lower(model)>_platform.win` lookup).
- **Open / next:**
  - **Run the ES40 halt-button test:** set `config/EmulatrV4.ini`
    `[System] model=ES40, cpuCount=1` and
    `[ROM] firmwareImage=firmware/es40cv_v7_3.exe`, build (POST_BUILD deploys
    firmware+manifest), run, watch COM1 banner and the `HALTPROBE`/smir trace.
    Model switch auto-picks `es40_platform.win`.
  - Index the LFU option-card firmware library (KZPSA/KZPBA SCSI, KGPSA/`fca_*`
    FC, DEFPA FDDI, KZPDC RAID, DE-series NICs from `<platform>_diskN` dirs) into
    `REFERENCE_INDEX.md` — directly useful for the deferred PCI-enumeration work
    (these `.sys` are the actual option-ROM contents of the devices the SRM walks).
- **Watch-outs:**
  - **ES40 south bridge is the ALi M1543C, not the Cypress CY82C693.** Cypress
    used as a documented stand-in so the console path works; south-bridge init is
    the most likely spot for an early divergence *before* the halt message. DS20
    (PC264) is the cleaner second test — it genuinely uses the modelled Cypress.
  - **Naming traps:** ES40 firmware is **`es40cv_v7_3.exe`** (the "cv" variant),
    not `es40_v7_3.exe` — wired as cv everywhere. And **`ds10srm.sys` ≠
    `palcode-ds10.rom`**: same 767,488-byte size but different SHA — two distinct
    DS10 SRM builds (.sys = 2006, .rom = 2005), not the same file.
  - DS25/ES45 manifests are staged only — not bootable until a Titan chipset
    model exists.

## 03:07 — Chipset reference topics being rewritten as clean Help&Manual XML

- **Working on:** Converting the chipset/architecture reference material (carried
  from the firmware-corpus analysis) into proper Help&Manual (H&M) documentation
  topics under `HMDocs/Topics/`. No emulator code changes — docs deliverable only.
- **Done since last checkpoint (01:07):**
  - Surveyed the three H&M projects; located the one holding architecture/chipset
    topics. Found the seven topic files already exist but in poor shape: **ALi**
    (~8.8K) and **Titan** (~13.8K) were raw-markdown dumps shoved into `Normal`
    paragraphs; the rest were stubs.
  - Reverse-engineered the project's H&M XML schema from a well-formatted topic:
    heading styleclasses, inline + block `Code Example`, `NotesBox`, tables, and
    `SeeAlso` / `topiclink` cross-topic links. Set up a TaskCreate to track the
    seven-topic rewrite.
  - **Rewrote four topics as clean, cross-linked H&M:** SouthBridge, Cypress,
    CY82C693 (part detail), and Tsunami/Typhoon.
- **Open / next:**
  - Rewriting the two remaining raw-markdown topics — **Titan** (in progress) then
    **ALi** — to match the same H&M schema, then cross-link all seven.
  - After the rewrite, verify topics render/validate and wire any new ones into the
    project TOC.
- **Watch-outs:**
  - Topic file paths contain a literal `&` (Help&Manual) which broke an unquoted
    bash command earlier — always quote these paths.
  - This is uncommitted doc WIP in the H&M project tree; the four rewritten topics
    plus the in-flight Titan edit are the crash-loss exposure for this session.
  - Substantive emulator blockers from earlier entries (uncommitted `chipsetLib/`
    WIP, NVRAM-revert, SMP secondary stall, PCI enumeration) are untouched here.

## 05:06 — Session close: 7 H&M topics done; Titan/ALi chipset work staged for commit (blocked by index.lock)

- **Working on:** Wrapping the session. All seven chipset/architecture H&M
  topics finished and cross-linked; user signed off ("Tomorrow or Thursday we
  will begin testing ES40") and asked for a git commit + Memory.md update.
- **Done since last checkpoint (03:07):**
  - **Completed the H&M topic set (7/7)** in `H&M/HMDocs/Topics/` with proper
    styleclasses (Heading1/2, tables, bulleted lists, NotesBox, inline
    `Code Example`, `See Also`): Chipset (hub), South Bridge, Cypress, CY82C693,
    Tsunami/Typhoon (21272), DECchip 21274 (Titan), ALi M1543C. The earlier
    raw-markdown ALi/Titan dumps were replaced with formatted content; all
    cross-linked Chipset → Tsunami/Titan → South Bridge → Cypress/ALi.
  - **Memory.md updated** (closing 2026-06-17 entry) via file tools — succeeded.
  - Verified git is reading current (non-stale) content before staging — Titan
    enum + ALi wiring both present in git's diff, so no truncated-file risk.
- **Open / next:**
  - **Git commit is BLOCKED** — a 0-byte `EmulatRAppUniV4\Emulatr\.git\index.lock`
    can't be removed from the sandbox ("Operation not permitted"); a Windows-side
    git client is holding the index. User must clear the lock (close the app or
    delete `index.lock`) then commit. Staged set: `chipsetLib/AliM1543C.h`,
    `Titan21274_CsrSpec.h`, `TitanChipset.h`, `TitanPchip.h`, `TsunamiVariant.h`,
    `TsunamiChipset.h`, `systemLib/Machine.cpp`, `CMakeLists.txt`,
    `tests/chipsetLib/test_ticket01_5_variant_binding.cpp`, the es40/ds20/ds25/es45
    platform manifests, and journals `20260616_titan_21274_interface.md` +
    `20260617_es40_ali_m1543c_interface.md`. Commit msg drafted in transcript.
  - **Next test session (Thu): ES40 boot.** Titan scaffold reuses Tsunami
    Cchip/Dchip/TIG with a new dual G/A-port Pchip+AGP; TsunamiVariant now maps
    DS15/DS25/ES45→Titan (was mislabeled Typhoon=21274). Build DS10 first to
    confirm no regression, then watch the `HALTPROBE`/smir trace.
- **Watch-outs:**
  - Titan/ALi code verified standalone via `g++` only — **not yet built in
    MSVC/Qt**; build DS10 before trusting the ES40 path.
  - **Repo scope:** git root is `EmulatRAppUniV4\Emulatr\`, so Memory.md, the H&M
    topics, REFERENCE_INDEX.md, and the firmware-profile analysis live OUTSIDE
    this repo — on disk but not version-controlled by the pending commit.
  - ES40 uses the **ALi M1543C** south bridge (Cypress is the documented
    stand-in) — most likely early-divergence spot; DS20 (genuine Cypress) is the
    cleaner second test.
  - Mount lag persisted all session — files were validated via the Windows-side
    Read tool, not bash byte counts.
