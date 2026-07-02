# EmulatR — Project Instructions

This file is auto-loaded for any Claude session whose working folder
is `D:\EmulatR\`. It complements (does not replace) the global
CLAUDE.md and exists so that project-specific context — paths,
versions, reference docs — is shared by every session for the life of
the project.

## Project layout

| Version | Path | Access | Notes |
|---------|------|--------|-------|
| **V4 (current)** | `D:\EmulatR\EmulatRAppUniV4` | read/write | Active development target |
| V0 (sources)  | `D:\EmulatR\EmulatRAppUniV3` | read-only  | Untouched sources merged into V1 |
| V1            | `D:\EmulatR\EmulatRAppUni`   | read-only  | |
| V2 (POC)      | `D:\EmulatR\EmulatrPOC`      | read-only  | |
| Reference     | `D:\EmulatR\Processor Support` | read-only | Alpha CPU/chipset/PALcode/SRM docs and sources |

When the user says "the project" without qualification, assume V4
(`EmulatRAppUniV4`).

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

- Treat anything under V0/V1/V2 and `Processor Support` as read-only —
  do not edit, even to "fix" formatting.
- Prefer surgical `Edit` over rewriting whole files in V4.
- For any non-trivial change in V4, summarize the intended diff before
  applying it.

## Deferred / planned work

Project-level notes about work intentionally postponed until a
prerequisite is met. Future sessions should consult these before
starting net-new architectural work in the same area.

- **Snapshots (save/restore machine state)** — deferred until SRM
  Console reaches the `>>>` prompt. Rationale: snapshots are an
  optimization investment that pays back per restore, and they're only
  valuable once we know what end state to capture. Design notes and
  implementation plan are in
  `EmulatRAppUniV4\Emulatr\journals\Snapshots_Design_Notes.md`.
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
  `EmulatRAppUniV4\Emulatr\tools\host_decompressor\` (see its README). It
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
  for an on-board device V4 does NOT enumerate, gets all-ones, masks it to
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

---

## CANONICAL LOCATION / SINGLE SOURCE OF TRUTH (2026-06-24)

This `CLAUDE.md` and `memory.md` now live in the **git repo root**
(`EmulatRAppUniV4/Emulatr/`) and are version-controlled so every machine (PC, Mac)
shares them via `git pull`. The old copies at `D:\EmulatR\` (outside git) are
SUPERSEDED -- ignore/delete them. The absolute `D:\...` paths in the table above are
the PC environment; on the Mac the repo is at a different path -- interpret the
conventions relatively.

`memory.md` is the live, append-only context log -- READ IT FIRST each session.

## ACTIVE WORK (as of 2026-06-24)

Building a **spec-validated map of the HWRPB region** (the firmware->OS hand-off
contract: HWRPB / per-CPU slots / CTB / CRB / MEMDSC / DSRDB / GCT-FRU @ 0x3ff32000).
The DS20 "AlphaPC 264DP" mis-badge is the first detected divergence in that contract,
not a cosmetic item. Full methodology, runtime instrument designs, and the step-by-step
resume plan are in:
  `journals/HWRPB_Region_Fidelity_and_Resume_20260624.md`
Supporting design doc (platform identity 3-channel model + P0-P6):
  `journals/Platform_Interface_Contract_and_Latch_Plan_20260624.md`

## SANDBOX CAVEAT

The Cowork Linux sandbox sees this repo over a FUSE mount that returns TRUNCATED reads
and cannot unlink files. Run ALL git operations and file-integrity checks on the NATIVE
OS (Windows/Mac), never from the sandbox. Sandbox-side "modified"/truncated views of
files (e.g. the platform manifests) are phantom until confirmed with native `git diff`.
