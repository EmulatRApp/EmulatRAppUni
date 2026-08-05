# EmulatR — Project Instructions

This file is auto-loaded for any Claude session whose working folder
is `D:\EmulatR\`. It complements (does not replace) the global
CLAUDE.md and exists so that project-specific context — paths,
versions, reference docs — is shared by every session for the life of
the project.

## Project layout

| Version | Path | Access | Notes |
|---------|------|--------|-------|
| **V5 (current)** | `D:\EmulatR\EmulatRAppUniV5` | read/write | Active development target |
| **V4 **       | `D:\EmulatR\EmulatRAppUniV4` | read-only | Frozen development target |
| V0 (sources)  | `D:\EmulatR\EmulatRAppUniV3` | read-only  | Untouched sources merged into V1 |
| V1            | `D:\EmulatR\EmulatRAppUni`   | read-only  | |
| V2 (POC)      | `D:\EmulatR\EmulatrPOC`      | read-only  | |
| Reference     | `D:\EmulatR\Processor Support` | read-only | Alpha CPU/chipset/PALcode/SRM docs and sources |

When the user says "the project" without qualification, assume V5
(`EmulatRAppUniV5`).

**Canonical folder: `D:\EmulatR` (PC).** Do NOT work against `D:\EmulatR (1)`
or any other " (N)" copy -- that is a stray duplicate, not the git-tracked tree.
If a Cowork session is connected to such a copy, re-point it at `D:\EmulatR`
before making changes.

## Reference documentation — read the index, not every PDF

`D:\EmulatR\Processor Support\` contains a large library of Alpha CPU
manuals, chipset HRMs, PALcode/SRM/firmware guides, and the MILO /
apisrm / diags / fwtools source trees. **Do not Glob the whole tree
on every question.** Instead:

1. Open `D:\EmulatR\Processor Support\REFERENCE_INDEX.md` first — it
   lists every document with a one-line description, organized by
   topic (architecture, per-CPU HRMs, chipsets, PALcode/SRM, source
   trees) and ends with a "what should I read for X?" lookup table.
2. From the index, open only the specific PDF / source file relevant
   to the question.
3. For 21164 / EV5 questions, the hand-extracted IPR table at
   `Processor Support\EV5_IPR_REFERENCE.md` and the plain-text
   `Processor Support\21164ds.txt` are usually faster than the PDFs.
4. If the user adds a new reference doc, append it to the relevant
   section of `REFERENCE_INDEX.md` so future sessions can find it.

The `emulatr-reference` skill at `D:\EmulatR\skills\emulatr-reference\`
encodes the same workflow. It travels with the workspace; whether or
not Cowork registers it as a first-class Skill, the project CLAUDE.md
you are reading right now already encodes the "read the index first"
behavior, so future sessions inherit it.

## House conventions

- Treat anything under V0/V1/V2, **V4 (FROZEN)**, and `Processor Support` as
  read-only — do not edit, even to "fix" formatting. All work lands in V5.
- Prefer surgical `Edit` over rewriting whole files.
- For any non-trivial change, summarize the intended diff (file paths + line
  numbers + edit shape) before applying it; wait for approval.
- **EmulatR is the PRIMARY Oracle.** AXPBox, SimH, and other emulators are
  SECONDARY/supportive only — corroborate, or use when EmulatR is not yet
  authoritative — never the primary authority over EmulatR. Any decision that
  would treat a non-EmulatR emulator as ground truth is DISCUSS-FIRST.

### Build & run conventions

- Format all command-line examples as executable **bash** (not
  PowerShell/cmd), so they paste-and-run in Git Bash (Windows) or a
  Mac/Linux shell alike.
- Root every build/run at the per-config tree
  `<project>/out/build/<config>`, where `<config>` is one of
  `release`, `relwithdebinfo`, or `debug`. `cd` into that root before
  launching so relative paths (`firmware/…`, log outputs) resolve, and
  the launch path is `out/build/<config>/Emulatr[.exe]` on both hosts.
  `tools/build_emulatr.sh <config>` builds and populates that tree
  (on Windows it keeps the in-source VS build and mirrors the artifact
  there; on Mac/Linux it builds out-of-source directly into it).
- `cmake` is not on PATH in a bare Git Bash shell — it arrives with the
  VS environment. Build via `tools/build_emulatr.sh`, which sources
  `tools/vsenv.sh` (vcvars) first; don't call `cmake` directly there.
- Use `relwithdebinfo` (or `debug`) for any diagnostic capture: the
  `EMULATR_DIAG_*` retire-time facility is compiled OUT of `release`.
- After any pull that touches runtime code, rebuild, then confirm the
  facility you need is actually in the binary before a long run, e.g.
  `grep -a -c 'EMULATR_DIAG_WREG' out/build/<config>/Emulatr.exe` (> 0).

## Deferred / planned work

Project-level notes about work intentionally postponed until a
prerequisite is met. Future sessions should consult these before
starting net-new architectural work in the same area.

- **Snapshots (save/restore machine state)** — DONE. SRM reaches `>>>`
  on DS10/DS20/ES40; Level 1 snapshot landed and the entry snapshot
  (EMULATR_FAST_DECOMPRESS=snapshot -> `firmware/<stem>.snap`, renamed
  from `.axpsnap` in V5) is built. Design notes:
  `journals/Snapshots_Design_Notes.md`.
- **EV5 (21164) emulator profile** — eventual; would need a parallel
  `coreLib/Ev5EntryVectors.h` mirroring the EV6 one. EV5 vector layout
  is documented in `Processor Support\Palcode\palcode\milo-sources-2.0.35-0.2\milo-2.0.35-0.2\palcode\lx164\dc21164.h`
  lines 793-806; differences are noted in `REFERENCE_INDEX.md`.
- **`S_PalLinux` codegen extension** — `genGrains.py` currently
  iterates only Tru64 and VMS personalities; the `S_PalLinux` flag
  exists in the enum and on TSV rows but is not yet emitted into a
  `lookupPalLinux()` dispatch table. Mechanical ~10-line extension to
  the codegen's personality iteration loop.
- **Host-native decompression as an alternate boot path** -- the guest
  self-decompresses on the emulated CPU every cold boot (~4M cycles, the
  0x60111c spin). We now have a native, source-built oracle of the exact
  DEC decompressor (Mark Adler inflate c10p1 + DEC wrapper) in
  `tools\host_decompressor\` (see its README). It
  produces a byte-identical image to EmulatR's CPU, so today it is a
  trusted reference / regression guard and a clean `decompressed.rom`
  generator (strictly better than AXPBox, which runs the guest
  decompressor on its own CPU). OPTIONAL future integration as a runtime
  alternate path via `--decompress=inline|host|cache` (default `inline`,
  the faithful path). RECOMMENDED design is the INTERCEPT-HYBRID, not a
  full bypass: let the guest `ev6_huf_decom.m64` startup run so its CPU/PAL
  side-effects happen for real (PAL_BASE, I_CTL SDE bits, ITB/DTB + icache
  flush, shadow regs, save/restore of SROM params R16-R21), but detect
  entry to the `decompress()` C function, host-fill the output memory, set
  R0 = decompressed base, and advance PC to the call's return site -- we
  skip only the ~4M-cycle inner inflate and inherit every side-effect. A
  full bypass risks a subtle missing-side-effect divergence that would be
  painful to chase because the image bytes look perfect. NOTE: this
  overlaps the snapshot work for warm-boot perf; its distinctive value is
  the trusted oracle + cold-boot/determinism case. SEQUENCE AFTER the SRM
  reaches `>>>` -- it does not advance the current runtime blocker (the R2
  clock-interrupt return), which is downstream of decompression.
- **PCI device enumeration + on-board device models** -- after the SCBB
  fix + FETCH-FIXUP removal (2026-05-31), the SRM cold boot runs clean to
  360M+ cyc and, during `from_init`, emits repeated `TsunamiPchip:
  UNHANDLED OUTER WRITE offset=0x0000ffff0001` (index/data byte pairs,
  values 0x80/0xc0/0x5b/0x15/0xa3...). ROOT: the firmware reads a PCI BAR
  for an on-board device V5 does NOT enumerate, gets all-ones, masks it to
  base 0xFFFF0000, and pokes that device's index/data register pair into
  the void (PA 0x800_FFFF_0000), which falls through TsunamiPchip::write
  to UNHANDLED. STRONG CANDIDATE = the DS10 on-board DEC 21143 / DE500
  "tulip" Ethernet (`apisrm/ref/dc287_def.h` is full of 0xFFFF0000 CSR
  refs; the byte-toggle values match the CSR9 SROM bit-bang that reads the
  MAC). NON-FATAL today (firmware tolerates the missing NIC; boot
  continues). FIX when ready: implement a real PCI bus walk so on-board
  devices (Ethernet, SCSI, etc.) are discovered and get sane BARs, instead
  of the firmware computing a garbage all-ones base. A stub that answers
  the NIC's config with a valid BAR + absorbs CSR writes would silence it
  short-term; a full tulip model is the larger lift. To pin the exact
  device, a one-shot STORE-WATCH on PA 0x800_FFFF_00xx gives the storing
  PC. LOWER PRIORITY than the path to `>>>` (this does not block boot).
- **`Ev6Translator` harvest (reference -> V5)** -- an emailed reference
  translator (`journals/ref_ev6Translation_struct_20260702.h`) supplies pieces
  the in-tree `mmuLib/Ev6Translator.h` lacks: a 3-level HW page-table walk, DTB/
  ITB PTE register-format converters (for `HW_MTPR/MFPR ITB_PTE/DTB_PTE`),
  alignment-before-translation fault ordering, and VA-form-aware (43/48-bit)
  segment decode. HARVEST-ONLY, NOT drop-in: foreign deps (Ev6SPAMShardManager,
  HWPCB, PendingEvent, QMutex) + its own defects (kseg SPE still 48-bit-hardcoded;
  walk omits the mode KRE/URE check). Full plan + do/don't list:
  `journals/20260702_ev6translator_harvest_task.md`. SEQUENCE AFTER the ES40 R16
  backtrace -- the ACV loop root is a garbage pointer, not the translator, though
  the alignment-order + VA-form items may correct the fault classification.
- **File-naming convention audit (project-wide, run-time artifacts)** -- the
  V5 naming convention is stem-keyed and version-free, but coverage is uneven
  and LOGGING is an outright gap.
  THE CONVENTION (the written target the audit conforms to): the STEM is the
  `--firmware` basename (e.g. `ds20_v7_3`), falling back to the ini model when
  no firmware is given; where both are present the FIRMWARE STEM WINS and
  derives the platform.  File NAMES carry no tree-version identifier (V5
  onward: `Emulatr.ini` not `EmulatrV4.ini`; `ds20_platform.json` not
  `ds20_v4_platform.json`) -- the version lives in the tree/branch and in file
  HEADERS.  A vendor firmware revision inside a stem (`ds20_v7_3.exe`) is NOT
  a tree version and is exempt.
  AUDIT SCOPE:
  (a) CONFORMING -- verify only, do not change: `<stem>.exe`, `<stem>.rom`,
      `<stem>_platform.json`, `<stem>.snap`.  Document the named exceptions
      `ds10_diag_flash.rom` / `ds20_diag_flash.rom`.
  (b) UNVERIFIED: TOY/CMOS persistence -- target is `<stem>_toy.bin`; the
      naming as actually landed in SPEC-TOY-001 (HEAD de6a13d) is unchecked.
  (c) THE GAP -- LOGGING.  RULE DECIDED 2026-07-31 (architect), LANDING
      OWED.  Run-dir log naming is ad hoc today:
      `console_capture_20260729.log`, `emulatr_c1_gate.log`,
      `run_ds20_showdev_20260725_004804.log`,
      `putty_console_p10023_20260725004805.log`, `gateB_ds20_sirr_r2.log`
      -- script-chosen names, date-stamped variants, and port-keyed PuTTY
      captures, NONE stem-keyed.  THE RULE: PREPEND the stem to the existing
      run-artifact form, giving `<stem>_<purpose>_<YYYYMMDD>_<HHMMSS>.<ext>`
      -- e.g. `ds20_v7_3_showdev_20260731_200412.log`.  The PLACEMENT half of
      the existing rule is UNCHANGED (run/console logs to the run dir's
      `./logs`, execution/retire/CPU traces to `./traces`); only the NAME
      gains the stem prefix.  Rationale: parallel multi-platform / per-port
      instances become separable and greppable, and that parallel workflow is
      now the intended one.  IN SCOPE TOO: the spdlog application log and the
      stderr diagnostic stream -- where each lands, and whether either is
      stem-keyed at all.  LANDING IS TWO EDITS, NOT ONE: (i) the emitting
      scripts and code, and (ii) the `emulatr-log-trace-output` session skill,
      whose text still mandates the bare `purpose_YYYYMMDD_HHMMSS.ext` form.
      Leaving the skill stale puts TWO conventions in force -- the exact
      two-owners-for-one-fact pattern these SSOT rules exist to prevent.
  (d) ALSO SWEEP for stem consistency: `snapshots/` contents, `traces/`
      outputs, and the HWRPB-scan sentinel file.
  SEQUENCE STRICTLY AFTER the SIRR commit and Gate C -- this is cross-cutting
  cleanup and MUST NOT ride the interrupt-path commit.  (Captured 2026-07-31
  from the Gate-B triage session.)

---

## CANONICAL LOCATION / SINGLE SOURCE OF TRUTH (2026-06-24)

This `CLAUDE.md` and `memory.md` now live in the **git repo root**
(`EmulatRAppUniV5/`) and are version-controlled so every machine (PC, Mac)
shares them via `git pull`. The old copies at `D:\EmulatR\` (outside git) are
SUPERSEDED -- ignore/delete them. The absolute `D:\...` paths in the table above are
the PC environment; on the Mac the repo is at a different path -- interpret the
conventions relatively.

`memory.md` is the live, append-only context log -- READ IT FIRST each session.

## ACTIVE WORK (as of 2026-07-15)

**V5 Translation Buffer (TB) fork.** SRM `>>>` is reached on DS10/DS20/ES40,
which closed the V4 objective and gated the TB work. V5 = V4 (frozen Oracle) + a
decode-amortizing TB tier, POC-first. Authoritative plan (lever hierarchy
snapshot/warp/TB; three-routes/two-passes; register-state + cycle-cost
invariants; Route-2-residue-empty; split-key dispatch; shared invalidation
substrate; the ES40 silicon LFU spin as the first WARP-recognition target):
  `journals/20260715_v5_tb_implementation_brief.md`
Read `memory.md` first each session (durable EV6/PAL/chipset substrate + ruled-
out lists), then the brief.

## SANDBOX CAVEAT

The Cowork Linux sandbox sees this repo over a FUSE mount that returns TRUNCATED reads
and cannot unlink files. Run ALL git operations and file-integrity checks on the NATIVE
OS (Windows/Mac), never from the sandbox. Sandbox-side "modified"/truncated views of
files (e.g. the platform manifests) are phantom until confirmed with native `git diff`.
