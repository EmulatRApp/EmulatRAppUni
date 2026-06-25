# HWRPB Region Fidelity & Mac Resume Contract

**Date:** 2026-06-24 (authored on the PC/Windows environment)
**Audience:** the next session, resuming from the **Mac (Intel)** environment via `git pull`.
**Status:** active. This is the authoritative resume document; `memory.md` (now in the
repo root) carries the condensed version. Read both before resuming.

---

## 0. TL;DR

The DS20 SRM badges **"AlphaPC 264DP"** instead of **"AlphaServer DS20"**. We proved the
IIC/OCP/FRU device layer is correct on the wire, so the badge is a **firmware-internal
SysType decision** we localized but could not cheaply extract statically. The decision is
**reframed**: the HWRPB and everything it anchors is the **firmware->OS hand-off contract**
— the critical section for booting an OS — and the badge is the *first detected divergence*
in it. The next work is a **spec-validated, uncompromisingly precise map of the HWRPB
region**, built with two small runtime instruments, validated against the AARM + apisrm
ground truth, and locked in with a boot-time validator. Steps are in section 8.

---

## 1. Repo state at handoff (verify NATIVELY on the Mac)

- **Single source of truth moved into git.** `memory.md` and the project `CLAUDE.md` were
  rehomed from `D:\EmulatR\` (outside git) into the **repo root** (`…/EmulatRAppUniV4/Emulatr`).
  Delete or ignore the old `D:\EmulatR\` copies. On the Mac the repo lives at a different
  path; the conventions in `CLAUDE.md` apply, the absolute Windows paths are illustrative.
- **Committed earlier in the day** (verify with `git log`): the P1/P2 latch + P3 +
  ds10 label + run_fw.sh + vsenv.sh + the interface-contract journal were committed and
  pushed; that is the commit the Mac should already have.
- **Likely still UNCOMMITTED** at handoff (commit deliberately, file-by-file): the
  `EMULATR_SYSVAR_WATCH` store-watch in `pipelineLib/MemDrainer.h`, the rehomed
  `memory.md`/`CLAUDE.md`, `tools/ghidra_scripts/DumpSysvarFns.java`, and this journal.
- **DO NOT** `git add -A`. The working tree is full of build artifacts (`*.dir/`,
  `*_autogen/`, `Debug/`, `*.img`, `ghidra/`, deployed `firmware/`) and a stray
  `dumSysVarFns2.java`. Stage only intended sources/docs.

### CRITICAL: the Cowork sandbox mount is unreliable — do git natively
The Cowork Linux sandbox sees the Windows repo over a FUSE mount that **returns truncated
reads and cannot unlink files**. This session it made `.gitignore` and
`ds20`/`ds25_v7_3_platform.json` look truncated/"modified" in the *sandbox's* git view
(identical blob hash on two distinct files = phantom; the OCP entries are intact), and an
in-sandbox `git stash` corrupted the index once. **Run all git operations and integrity
checks on the native OS (Mac).** Before committing a manifest, open it natively and confirm
it is complete (closes with `] }` and still contains its devices). Treat any sandbox-side
"modification" of `ds20`/`ds25` as phantom until proven on native git.

---

## 2. What is PROVEN about the banner (do not re-litigate)

From `EMULATR_IIC_TRACE=1 ./run_fw.sh ds20 cold`:
- OCP at **IIC 0x40 and 0x42 ACK** the `iic_init` probe; **0x4E NAKs** (correctly — 0x4E
  present would force the DS20E branch).
- The firmware then runs **`sable_ocp_init`** (hundreds of 0x40/0x42 writes = LCD
  bit-bang of "Console Started") => **`fopen("iic_ocp0")` SUCCEEDS**.
- FRU at **0xA2 reads DEC/DS20**, **0xA4 reads EV6** — `synthesizeFruImage` is correct.

**Conclusion: the IIC/OCP/FRU layer is correct; the badge is NOT an IIC-bus emulation gap.**
`platform latched: … ocp40=Y ocp42=Y usedDefault=0` confirms model/manifest agree and the
manifest loaded. The badge is decided elsewhere, in firmware.

## 3. Where the badge IS decided (firmware), and why static RE stalled

- Banner table at firmware VA **0x153cd8**, stride 0x2c, indexed by SysType member:
  member 1 = `"AlphaPC 264DP %3d MHz"` (the DEFAULT), member 6 = `"AlphaServer DS20 %3d
  MHz"`, member 8 = DS20E. Decision strings: **0x19ad90** "Defaulting system type to
  AlphaPC 264DP", **0x19adc0** "Error determining system type, SYSVAR = %x".
- `get_sysvar`/`build_dsrdb` compute the member and read SYSVAR from the HWRPB. On the
  apisrm reference this keys off `fopen("iic_ocp0")` — but our OCP opens, so V7.3-2 either
  runs `get_sysvar` before `iic_init` (latching the default), or reads SYSVAR from a
  source we model wrong. **That is exactly the HWRPB-region question.**
- Three extraction routes EXHAUSTED: (1) SYSVAR store-watch x3 never fired (SYSVAR is not
  a simple low-value store at a guessed value); (2) Ghidra string/table XREF found ZERO
  code references (the decision code is un-analyzed, reached by GP-relative/computed
  addressing); (3) `DumpSysvarFns.java` returned only data->string refs. Pure static RE
  is real manual work and not the right next move.

---

## 4. THE REFRAME — HWRPB region = OS hand-off contract (the actual work)

The HWRPB and its chained structures — **per-CPU slots, CTB (console terminal block),
CRB (console routine block), MEMDSC (memory descriptor), DSRDB (dynamic system
recognition data block), and the GCT/FRU tree at 0x3ff32000** — are what the OS reads at
hand-off. A casual scan-and-patch here buys a cosmetic banner and plants a silent landmine
for OS boot. So we build a **validated, spec-true map of the region**, with the 264DP badge
as one line item in a correctness ledger.

### Methodology (the bar we hold)
1. **Anchor on ground truth first.** Derive the canonical HWRPB/CTB/CRB/MEMDSC/DSRDB
   layout (offset, size, semantics, legal values) from the **Alpha AARM, Console Interface
   chapter** and **apisrm `hwrpb_def`** (both in `D:\EmulatR\Processor Support`; use
   `REFERENCE_INDEX.md`). Verify `deviceLib/Hwrpb.h` (already has offsets + `static_assert`)
   field-by-field against it. Nothing gets interpreted except against this reference.
2. **Locate precisely, then dump the WHOLE region** (not a SYSVAR spot-read). Validate the
   HWRPB by its **self-referential header** (first quadword == its own PA; SYSTYPE/SYSVAR at
   +0x80/+0x88). Dump every pointer it chains to.
3. **Validate field-by-field against spec.** Each field: offset/size/value-or-range and
   whether it matches what the OS requires. SYSVAR=member 1 becomes one documented entry.
4. **Resolve the TWO-HWRPB question authoritatively.** EmulatR's `HwrpbBuilder`
   (`deviceLib/HwrpbBuilder.cpp::populateHwrpb`, spec from `FirmwareDeviceManager.h::buildHwrpb`)
   writes an HWRPB with **hardcoded `system_type=DEC_TSUNAMI`, `system_variation=0`, at PA 0**,
   AND the SRM builds its own. **Establish (do not assume) which one the OS actually consumes
   at boot**, and make EmulatR's builder spec-correct where it is the live one. An OS booting
   against the wrong/stale HWRPB is the silent failure we are guarding against.
5. **Make it a regression.** Capture a validated golden HWRPB and add a **boot-time validator**
   (extending the P1/P2 latch philosophy): header self-pointer, SYSTYPE/SYSVAR sane, MEMDSC
   consistent with configured RAM, CTB/CRB present and well-formed. Keeps the region correct
   as adjacent code changes on the road to OS boot.

---

## 5. Authoritative spec sources
- **Alpha AARM** — "Console Interface" (HWRPB / CTB / CRB / per-CPU slot / MEMDSC layout).
  Find via `Processor Support/REFERENCE_INDEX.md`.
- **apisrm `hwrpb_def`** (the firmware's own struct definitions) under the apisrm source tree.
- `deviceLib/Hwrpb.h` — EmulatR's encoded offsets + `static_assert`s (cross-check vs above).
- Confirmed runtime anchors: GCT/FRU tree at **0x3ff32000**; banner table at firmware VA
  **0x153cd8**; SYSVAR/SYSTYPE conventionally at HWRPB **+0x88 / +0x80**.

## 6. Open correctness question to settle early
**Which HWRPB does the OS consume — EmulatR's pre-built one (PA 0, hardcoded) or the
SRM-built one?** Trace `buildHwrpb()` call sites and whether the SRM relocates/rebuilds.
This determines where SYSVAR really lives and what the badge fix actually is.

---

## 7. Instrument designs (build these — env/sentinel-gated, zero-cost when off)

### 7a. `EMULATR_HWRPB_SCAN` — guest-memory locator
- **Trigger:** a sentinel file (mirror `EMULATR_STOP`, polled in `Machine::run` near
  `Machine.cpp:1058`). At `>>>` type `set sys_serial_num ZZHWRPBPROBE01`, then
  `touch EMULATR_HWRPB_SCAN`.
- **Action:** scan guest physical RAM (via `m_chipset.guestMemory()`) for the ASCII pattern
  in env `EMULATR_SCAN_PATTERN` (default the serial above). Log **every** hit PA plus a
  256-byte hexdump around it. Additionally flag any hit whose region looks like an HWRPB
  header (self-pointer: qword at candidate base == candidate base).
- **Disambiguation:** the serial lands in several places (NVRAM/flash image, FRU EEPROM,
  GCT/FRU @ 0x3ff32000, possibly HWRPB). Pick the HWRPB by the self-pointer header. NOTE:
  `sys_serial_num` may NOT propagate into the HWRPB — if not, you'll get FRU/GCT; then chase
  the HWRPB pointer from the GCT root, or scan directly for the self-referential header.

### 7b. `EMULATR_PA_WATCH=<hex_pa>` — exact-PA store/load watch
- Once 7a yields the **exact SYSVAR PA**, add a watch keyed on that PA in the store path
  (`pipelineLib/MemDrainer.h`, beside the existing `EMULATR_SYSVAR_WATCH` block) — and ideally
  a load-watch on the read path. On the NEXT boot it WILL fire on `get_sysvar`'s write/read
  and hand us the **PC**. Take that PC straight to Ghidra (`G` -> Go To) to finally read the
  decision function. This is the move that breaks the static-RE deadlock (the blind value
  watch failed only because we were guessing the value; with the precise PA it can't miss).

---

## 8. MAC RESUME PLAN (step by step, tomorrow)

1. **Pull + verify natively.** `git pull`; `git log --oneline -10` to confirm the latch
   commit is present; `git status` and review any pending changes on NATIVE git (ignore
   sandbox-side phantom manifest mods).
2. **Toolchain.** On the Mac, ensure `cmake`, `ninja`, and Qt 6.10.2 are on PATH
   (Homebrew: `brew install cmake ninja qt`); `tools/vsenv.sh` is a no-op there. Configure
   with the Ninja preset (or the Qt Creator kit): `cmake --preset Qt-Release` (set `QTDIR`
   to the mac Qt prefix) then `cmake --build out/build/release -j`. Run the doctests
   (`Emulatr_tests`); expect all green.
3. **Spec map.** Pull the canonical HWRPB/CTB/CRB/MEMDSC/DSRDB layout from AARM + apisrm
   `hwrpb_def`; cross-check `deviceLib/Hwrpb.h`. Start the fidelity doc's field table.
4. **Build 7a** (`EMULATR_HWRPB_SCAN`). Boot DS20 to `>>>`, `set sys_serial_num ZZHWRPBPROBE01`,
   `touch EMULATR_HWRPB_SCAN`. Identify the HWRPB by the self-pointer header; record its PA.
5. **Read SYSVAR** at HWRPB+0x88; confirm it decodes to member 1. Dump the whole region +
   chained structures; validate each field against the spec table.
6. **Build 7b** (`EMULATR_PA_WATCH=<SYSVAR_PA>`). Reboot DS20; capture the **PC** that
   writes/reads SYSVAR.
7. **Ghidra.** Go to that PC, read `get_sysvar`/`build_dsrdb`; document why member 1 is
   chosen (ordering vs `iic_init`? a hardware/HWRPB field we model wrong?). THEN decide the
   correct fix (host-side HwrpbBuilder, IIC-init ordering, or a modeled register).
8. **Resolve the two-HWRPB question** (section 6) and reconcile EmulatR's builder.
9. **Lock it in.** Write the boot-time HWRPB validator; capture the golden region; finalize
   the fidelity journal. Commit + push so the PC side stays in sync.

---

## 9. Levers / anchors quick reference
- `./run_fw.sh <ds10|ds20|ds25|es40|es45> [cold] [extra args]` — launcher (model is
  ini-driven; console on TCP 10023; PuTTY auto-launch unless `EMULATR_NO_PUTTY`).
- `EMULATR_FLASH_ROM=ds20_flash.rom` + `rm -f ds20_flash.rom` — factory-fresh NVRAM
  (default flash backing is `ds10_flash.rom` for ALL models — a known wrong-platform trap,
  also the source of the startup `memtest: No such command` stale nvram script).
- `EMULATR_IIC_TRACE=1` — IIC bus transactions. `EMULATR_SYSVAR_WATCH=1` — SYSVAR/banner
  store-watch (note: did NOT fire; SYSVAR isn't a simple store — use 7a/7b instead).
- `touch out/build/release/EMULATR_STOP` — clean stop + flush flash.
- Canary line to grep each boot: `platform latched: …`.
- Addresses: dsrdb table **0x153cd8**; decision strings **0x19ad90 / 0x19adc0**;
  GCT/FRU **0x3ff32000**; SYSVAR/SYSTYPE conventionally HWRPB **+0x88 / +0x80**;
  interactive-wait spin **0x1ad770** (LFU prompt, not a hang).

## 10. Cautions
- Sandbox mount is read-truncating + can't unlink — **git/integrity natively only** (sec 1).
- `ds10_v7_3_platform.json` is a clone of ds20; its **device tree** still needs a genuine
  DS10 review (only the `platform` label was corrected).
- Do not commit build artifacts; stage intended sources/docs explicitly.
