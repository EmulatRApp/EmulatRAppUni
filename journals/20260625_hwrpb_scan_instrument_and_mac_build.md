# HWRPB-Scan Instrument + macOS Build Hardening

**Date:** 2026-06-25 (Mac / Intel environment, resuming the 2026-06-24 HWRPB plan)
**Status:** instrument BUILT + suite green (472/472); **LIVE DUMP CAPTURED -- HWRPB
located at PA 0x2000, badge pinned to SYSVAR=0x405 @ PA 0x2058 (see §7).**
**Predecessor:** `journals/HWRPB_Region_Fidelity_and_Resume_20260624.md` (the methodology
+ resume plan this session executes steps 2-4 of). Read that first.

---

## 0. TL;DR

Resumed on the Mac. Got the macOS build working end-to-end (added a Release option;
fixed `run_fw.sh` so it actually runs on macOS), then **built the `EMULATR_HWRPB_SCAN`
instrument** (journal §7a) into `systemLib/Machine.{h,cpp}`. It locates the HWRPB base in
guest RAM two independent ways and dumps it. Suite is 472/472 green. Also caught a
**decimal/hex error in the resume journal**: SYSTYPE/SYSVAR are at HWRPB **+80/+88 decimal
= 0x50/0x58**, NOT the journal's "+0x80/+0x88". Everything below is UNCOMMITTED.

---

## 1. macOS build state (verified working)

- **Binary builds + runs.** `scripts/build_mac.sh` (from commit 64b8451) works: Qt 6.10.2
  via aqtinstall at `~/Qt/6.10.2/macos`, spdlog+fmt via brew, Xcode CLT. Output is a bare
  `Emulatr` (Mach-O x86_64), NOT `Emulatr.exe`.
- **Added a Release option** (was RelWithDebInfo-only): `./scripts/build_mac.sh [Release|
  RelWithDebInfo|Debug]`, default RelWithDebInfo (back-compat). Release -> `out/build/mac-release`,
  RelWithDebInfo -> `out/build/mac-debug` (legacy name kept). NOTE: RelWithDebInfo already
  defines `-DNDEBUG` (asserts off) so Release mainly buys -O3 over -O2 -- a modest speedup,
  not transformative (the emulator is an interpreter; reaching `>>>` is CPU-bound regardless).
- **`tools/run_fw.sh` made macOS-portable** (it is the CMake-deployed launcher; the root
  `run_fw.sh` and `tests/run_fw.sh` are stale copies -- CMakeLists.txt:830 copies
  `tools/run_fw.sh`). Three additive fixes:
  1. **Binary name**: pick `Emulatr.exe` if present else `Emulatr` (was hardcoded `.exe`,
     FATAL'd on Mac).
  2. **`sed -i` -> portable**: GNU `sed -i "expr"` is broken on BSD/macOS sed (treats the
     expr as the `-i` backup suffix) AND `\s` is GNU-only. Replaced with a temp-file +
     POSIX `[[:space:]]` class. THIS WAS A REAL BUG: it silently no-op'd the `model =` line,
     so `./run_fw.sh ds20` left the ini at its prior model.
  3. Console echo + a `binary :` line note for macOS (`nc localhost 10023`, not PuTTY).

### Boot gotcha hit + resolved this session
A DS20 boot "hung"/ran 32 min spinning. Root cause: the ini had **`model = ES40`** while the
run used **DS20 firmware** -- DS20 firmware on an ES40 chipset (ALi south bridge vs Cypress)
spins. It happened because the broken `sed -i` never set `model = DS20`. After the sed fix,
`./run_fw.sh ds20 cold` correctly flips the ini to DS20 (banner echoes `model : DS20`) and
the boot looked healthy. LESSON: always confirm `model` in the live ini matches the firmware;
the `run_fw.sh` banner now shows it.

### PuTTY on macOS (answered)
EmulatR does NOT auto-launch PuTTY by default (`autoLaunchPutty = false`). Even enabled it is
Windows-pinned: `puttyPath = putty.exe` and `launchPutty()` hardcodes a `d:/emulatr/traces/...`
`-sessionlog` arg (`SRMConsoleDevice.cpp:656`). brew PuTTY on Mac needs XQuartz. RECOMMEND
`nc localhost 10023` instead. (Deferred: an `#ifdef __APPLE__` branch in `launchPutty()`.)

---

## 2. THE INSTRUMENT -- `EMULATR_HWRPB_SCAN` (built this session)

**Files:** `systemLib/Machine.h` (member `m_hwrpbScanSentinel` + private method decl),
`systemLib/Machine.cpp` (`scanGuestForHwrpb()` + sentinel resolve in `run()` + poll in
`systemTick()`).

**Seam correction vs the resume journal:** the journal said trigger "near Machine.cpp:1058".
That line is STALE (pre the P2-T2 split). The real stop-sentinel poll is in
`Machine::systemTick()` (the `kStopPollMask = 0xFFFFF` block); the HWRPB-scan poll sits right
beside it, same ~1M-tick cadence. Resolution of the sentinel path is in `run()` setup beside
the `m_stopSentinel` block.

**Behavior** (env-gated; zero cost when off = one getenv + one exists() per ~1M ticks):
- Sentinel: `$EMULATR_HWRPB_SCAN_FILE` if set, else `EMULATR_HWRPB_SCAN` in cwd. Pre-cleared
  in `run()`; consumed (removed) after firing. Non-fatal -- run continues after the scan.
- **Scan A (pattern):** `EMULATR_SCAN_PATTERN` (default `HEAX1PEER`). Walks the sparse
  allocated pages (`GuestMemory::forEachPage`, 64 KB pages, in-page search -- cross-page
  straddle accepted-missing since the structs are aligned). For each hit PA `H`: dumps a
  `[H-64, H+192)` hex+ASCII window, then tests candidate base `B = H - 64` (the spec-pinned
  `system_serial_number` offset) via the HWRPB self-pointer (`read8(B)==B`) + identifier
  (`read8(B+8)==0x0000004250525748` = "HWRPB\0\0\0" LE). Confirmed hit -> decode + dump
  header.
- **Scan B (signature, serial-independent):** qword-aligned walk (step 8) reading the two
  header qwords straight from the page buffer (host x86_64 == guest Alpha, both LE); matches
  the self-pointer + "HWRPB" id directly. Finds the HWRPB even if `sys_serial_num` never
  propagates into it, AND reports EVERY match -> this resolves the **two-HWRPB question**
  (EmulatR's hardcoded PA-0 builder vs the SRM-built one) in one shot.
- A confirmed HWRPB report decodes rev(+16), size(+24), SYSTYPE(+80), SYSVAR(+88), and the
  banner member `(SYSVAR>>10)&0x3F`, then hexdumps `[base, base+0x140)` (full 320-byte
  header + a little).

---

## 3. SPEC GROUND TRUTH (anchored to deviceLib/Hwrpb.h static_asserts)

`HwrpbHeader` field offsets (all `static_assert`-pinned, so trustworthy):
- `+0  hwrpb_pa`            -- self-pointer (qword == own PA). PRIMARY validator.
- `+8  identifier`         -- "HWRPB\0\0\0" = `0x0000004250525748`. SECOND validator.
- `+16 revision`, `+24 hwrpb_size`, `+32 primary_cpu_id`, `+40 page_size`
- `+64 system_serial_number[2]` -- 16-byte ASCII (10 chars + pad). **Where the marker lands.**
- `+80 system_type`  (SYSTYPE)  = **0x50**
- `+88 system_variation` (SYSVAR) = **0x58** ; banner member id = `(SYSVAR>>10)&0x3F`
- `+160 cpu_slot_offset`, `+184 ctb_offset`, `+192 crb_offset`, `+200 mddt_offset`,
  `+216 fru_offset`, `+312 dsrdb_offset` -- the chained structures to map next.

### *** JOURNAL CORRECTION (important for the NEXT step) ***
The 2026-06-24 resume journal (§5, §9) says "SYSVAR/SYSTYPE conventionally at HWRPB
**+0x80 / +0x88**". That is a decimal/hex slip. The static_asserts pin SYSTYPE/SYSVAR at
DECIMAL 80/88 = **0x50 / 0x58**. (`0x88` = decimal 136 = `tbb_offset`.) The `EMULATR_PA_WATCH`
must target `base + 0x58` for SYSVAR -- watching 0x80/0x88 would miss. Fix the journal note
when that step starts.

---

## 4. HOW TO RUN (next live session)

1. `cd out/build/mac-release && ./run_fw.sh ds20 cold` (confirm banner `model : DS20`;
   startup logs `HWRPB-scan sentinel = <abs path>`).
2. Attach console: `nc localhost 10023`.
3. At `>>>`:  `set sys_serial_num HEAX1PEER`
4. Another terminal, in the run dir: `touch EMULATR_HWRPB_SCAN`
5. Dump lands on stderr -> `fw_ds20_<ts>.out`, bracketed `==== EMULATR_HWRPB_SCAN begin..end ====`.

---

## 7. LIVE DUMP RESULTS (2026-06-25, DS20 cold boot, at p00>>> )

Trigger: `set sys_serial_num HEAX1PEER` at `>>>`, then `touch EMULATR_HWRPB_SCAN`.
Scan A: 9 pattern hits. Scan B: **exactly 1 HWRPB**, at **PA 0x2000** (same as Scan A's
confirmed hit). Decoded `HwrpbHeader` @ 0x2000 (all fields spec-conformant):

| Off  | Field            | Value                 | Note |
|------|------------------|-----------------------|------|
| +0x00| self-pointer     | 0x2000                | validates (==base) |
| +0x08| identifier       | "HWRPB"               | validates |
| +0x10| revision         | 14                    | |
| +0x18| hwrpb_size       | 0xb80 (2944 B)        | |
| +0x20| primary_cpu_id   | 0                     | |
| +0x28| page_size        | 0x2000 (8 KB)         | Alpha |
| +0x30| pa_size_bits     | 0x2c (44)             | |
| +0x38| max_valid_asn    | 0xff (255)            | |
| +0x40| serial           | "HEAX1PEER"           | our marker propagated INTO the HWRPB |
| +0x50| **SYSTYPE**      | **0x22 = 34 = DEC_TSUNAMI** | CORRECT; shared DS10/DS20/ES40 |
| +0x58| **SYSVAR**       | **0x405** -> member **1** = "AlphaPC 264DP" | **THE BADGE** |
| +0x60| system_revision  | 0                     | |
| +0x68| intrclock_freq   | 0x400000              | 1024 Hz x 4096 |
| +0x70| cycle_count_freq | 0x05f5e100            | 100 MHz |
| +0x78| vptb_va          | 0x200000000           | |

member = (SYSVAR >> 10) & 0x3F = (0x405 >> 10) & 0x3F = 1. DS20 would be member 6
(=> SYSVAR variation field <15:10> = 6 => SYSVAR 0x1805).

### Three conclusions that REFINE the plan
1. **Two-HWRPB question RESOLVED.** Exactly ONE live HWRPB, at PA 0x2000, and it carries the
   serial we just set => it is the **SRM-built** one. EmulatR's `HwrpbBuilder`
   (FirmwareDeviceManager.h:175 "fixed at PA 0", hardcoded DEC_TSUNAMI/var 0) is NOT what the
   firmware/OS uses -- there is no validated HWRPB at PA 0 in the scan. **=> Patching
   EmulatR's HwrpbBuilder would NOT change the badge.** The decision is firmware-internal.
2. **SYSTYPE is correct; the badge is PURELY SYSVAR.** 0x22/DEC_TSUNAMI is family-wide
   (DS10/DS20/ES40), so it cannot distinguish DS20 from 264DP. The discriminator is SYSVAR
   0x405 at **PA 0x2058**, variation field <15:10> = 1.
3. **Disambiguation works.** The other 8 serial hits validated as NON-HWRPB: console
   command-echo/history buffers (~0x2f2d3, 0x2fca0, 0x2fce0, 0x30da0, 0x30e20, 0x3babb,
   0x3ff4a473), the `sys_serial_num` env-var value store (0x30e20, preceded by the var name),
   and a heap copy at 0x3ff4bf58 (0xefefefef guard bytes). All correctly rejected by the
   self-pointer + id test.

CAVEAT to verify: assume PA 0x2000 is STABLE across deterministic cold boots (low-memory,
firmware-allocated). Confirm on the next cold boot before trusting a hardcoded watch PA; if
it moves, re-run the scan to re-locate, or have PA_WATCH self-locate via the header first.

---

## 8. NEXT STEPS (now concrete)

1. **DONE: get the dump.** HWRPB @ PA 0x2000; SYSVAR=0x405=member 1 confirmed as the 264DP
   badge; two-HWRPB question resolved (one, SRM-built, at 0x2000).
2. **Build `EMULATR_PA_WATCH=0x2058`** (store-watch on the SYSVAR field) in
   `pipelineLib/MemDrainer.h` beside the existing `EMULATR_SYSVAR_WATCH`/GCT blocks. ARM IT
   BEFORE a COLD boot (the SYSVAR write happens during cold init, long before `>>>`), capture
   the writing PC(s). NOTE: parameterize the PA via env (not just 0x2058) so we can re-point
   if the HWRPB base moves; ideally self-locate the HWRPB header first, then watch base+0x58.
3. **Ghidra** the captured PC -> read `get_sysvar`/`build_dsrdb`; document WHY member 1 is
   chosen (ordering vs `iic_init`? a HWRPB/HW field we model wrong?). THEN pick the correct
   fix (firmware-state we feed, IIC-init ordering, or a modeled register) -- NOT a blind
   SYSVAR patch.
4. **Map the chained region** (CTB/CRB/MEMDSC/DSRDB/FRU via the +160/+184/+192/+200/+216/+312
   offsets now readable from the 0x2000 header) field-by-field vs spec. TOP-LEVEL DIRECTORY
   DONE -- see §9. Still TODO: dump + validate each sub-structure's INTERNAL fields.
5. **Lock it in**: boot-time HWRPB validator + golden capture (extends the P1/P2 latch).

---

## 9. HWRPB REGION MAP (top-level directory, decoded from the live 0x2000 header)

All section pointers below are read directly from the captured header bytes (base = PA
0x2000; every `*_offset` field is RELATIVE to base, confirmed by `fru_offset` landing on the
known GCT/FRU anchor). Header field offsets per `deviceLib/Hwrpb.h::HwrpbHeader`. Absolute
PA = 0x2000 + offset.

| Section            | Header field (+dec) | Raw value   | Absolute PA  | Count x Size      |
|--------------------|---------------------|-------------|--------------|-------------------|
| HWRPB header       | (base)              | --          | 0x2000       | size 0xb80        |
| TB hint block (TBB)| tbb_offset +136     | 0x140       | 0x2140       | --                |
| Per-CPU SLOT array | cpu_slot_offset +160| 0x180       | 0x2180       | count 2 x **0x280** |
|  - slot 0          |                     |             | 0x2180       |                   |
|  - slot 1          |                     |             | 0x2400       |                   |
| CTB (console term) | ctb_offset +184     | 0x680       | 0x2680       | count 1 x 0x160   |
| CRB (console rtn)  | crb_offset +192     | 0x7e0       | 0x27e0       | --                |
| MEMDSC / MDDT      | mddt_offset +200    | 0x840       | 0x2840       | --                |
| DSRDB              | dsrdb_offset +312   | 0xac0       | 0x2ac0       | --                |
| CDB (config data)  | cdb_offset +208     | 0x36880     | 0x38880      | external          |
| FRU / GCT tree     | fru_offset +216     | 0x3ff30000  | **0x3ff32000** | (== journal anchor) |

Scalar header fields (also from the dump): rev 14, size 0xb80, primary_cpu_id 0, page_size
0x2000 (8 KB), pa_size 0x2c (44), max_valid_asn 0xff, serial "HEAX1PEER", SYSTYPE 0x22
(DEC_TSUNAMI), SYSVAR 0x405 (member 1 = 264DP), intrclock_freq 0x400000 (1024 Hz x 4096),
cycle_count_freq 0x05f5e100 (100 MHz), vptb_va 0x200000000, checksum 0x45455076_2eb37485,
reserved_hardware 0x3ff32000 (also points at the GCT/FRU base -- note, verify use).

### Layout observations
- The inline sections pack CONTIGUOUSLY inside the HWRPB's own 0xb80 allocation
  (0x2000..0x2b80): header->TBB->slots->CTB->CRB->MDDT->DSRDB. slot1(0x2400)+0x280 = 0x2680 =
  CTB; CTB(0x2680)+0x160 = 0x27e0 = CRB; etc. Coherent and self-consistent.
- CDB (0x38880) and FRU/GCT (0x3ff32000) are EXTERNAL to the HWRPB block.

### *** FIDELITY DIVERGENCE to verify (ledger item) ***
The live SRM HWRPB uses a **per-CPU SLOT size of 0x280** (640 B). EmulatR's `Hwrpb.h`
`PerCpuSlot` is `static_assert(sizeof==0x400)` (AARM-canonical, with an in-slot DSRDB sub-
block at slot+0x300). The live layout instead uses 0x280 slots AND a SEPARATE top-level
DSRDB (dsrdb_offset=0xac0). Reconcile against apisrm `hwrpb_def` -- either the SRM packs a
smaller slot than AARM-canonical, or our `PerCpuSlot` model is larger than this firmware
emits. This is exactly the kind of contract divergence the region-mapping is meant to catch
(cf. the 264DP badge). Does NOT block boot; flag for the field-by-field validation pass.

### Caveats / still-TODO
- This is the TOP-LEVEL DIRECTORY only (section base PAs). The INTERNAL fields of each
  sub-structure (CTB cons_type/RX-TX, CRB callback PVs, MDDT memory clusters vs configured
  RAM, DSRDB name/LURT, FRU/GCT contents) are NOT yet dumped/validated -- next pass should
  dump each PA region and check field-by-field vs spec.
- To dump arbitrary regions, extend the probe with an `EMULATR_DUMP_PA=<pa>[:<len>]` (reuse
  the same hexdump helper) or re-run the scan after depositing markers. (Discuss.)
- Cross-check all offsets vs apisrm `hwrpb_def` when on a host that has Processor Support
  (likely PC-only; not confirmed present on the Mac).

---

## 10. PERSISTENCE MACHINERY + GRACEFUL-EXIT FLUSH (2026-06-25)

### What `update srm` / `set` actually persist (grounded in `chipsetLib/FlashRom.cpp`)
Three distinct layers -- conflating them muddied the HWRPB question:
1. **Flash ROM** = emulated AMD command-FSM flash (mirrors AXPBox `CFlash`): guest writes
   via unlock->erase->program at magic addrs 0x5555/0x2AAA. BOTH `update srm` AND `set <env>`
   reach flash through this FSM. Backing file `ds20_v7_3.rom`, restored at boot, persisted
   ONLY on clean exit via `~Machine::forceFlush()`. Instrument: `EMULATR_FLASH_TRACE` logs
   every AMD-FSM write.
2. **NVRAM env vars** live INSIDE that flash region (our `HEAX1PEER` was at flash off 0x5f815).
3. **HWRPB** = RAM only, built fresh each boot in DRAM at 0x2000; NEVER persisted.

PROVEN by dumpbin (python scan of both firmware files): `ds20_v7_3.exe` has ZERO "HWRPB"/
serial bytes (it's the COMPRESSED self-decompressing image -- the identifier + decision code
are inside the compressed payload). `ds20_v7_3.rom` has ZERO "HWRPB" and ONE serial hit
(0x5f815 = the NVRAM env). => The HWRPB is NOT ROM-resident; LFU/`update srm` writes the
FIRMWARE to flash, NOT the HWRPB. The HWRPB (and SYSVAR) are built later, at `from_init`, by
the running SRM -- a DRAM event (EMULATR_PA_WATCH path), NOT a flash event (FLASH_TRACE path).

CONSEQUENCE for the `.rom`-as-firmware idea: `main.cpp` routes firmware by EXTENSION -- `.exe`
-> SRM decompressing loader; `.rom` -> `loadDecompressedRom` (expects a PRE-decompressed
image). `ds20_v7_3.rom` is the flash backing (compressed SRM + env), so `--firmware
ds20_v7_3.rom` MIS-ROUTES. Dropped that approach.

### Graceful-exit flush (LANDED + verified)
PROBLEM: `~Machine::forceFlush()` only runs on a clean `run()` return (the EMULATR_STOP
sentinel). Ctrl-C/SIGTERM killed the process first -> destructor skipped -> `update srm`/`set`
LOST. FIX (additive, no env gate, portable Windows+macOS via `std::signal`):
- `systemLib/Machine.{h,cpp}`: `std::atomic<bool> m_stopRequested` + `requestStop()`
  (async-signal-safe: atomic store ONLY) + `stopRequested()`. `systemTick()` polls it EVERY
  tick (atomic load ~free) beside the EMULATR_STOP sentinel -> clean `return false`.
- `main.cpp`: file-scope `std::atomic<Machine*> g_machineForSignal`; `extern "C"`
  `emulatrSignalHandler` sets requestStop() then resets to SIG_DFL (2nd signal = force-quit);
  installed for SIGINT+SIGTERM right after `mach` is constructed. ALSO wrapped `mach.run()`
  in try/catch so an exception can't bypass the destructor flush (mach is a main() local;
  catch+fall-through guarantees ~Machine runs).
- DESIGN NOTE: the handler does NOT create the sentinel FILE (file I/O is not
  async-signal-safe); it sets the atomic -- the async-signal-safe equivalent of the same
  lever. Suite 472/472 green. VERIFIED end-to-end: boot, `kill -INT` -> log
  `Machine::run: stop requested (signal) -- clean exit at cycle 109119114`, process exits
  via the destructor (flush) path.
- BONUS: this also makes headless background runs flushable (was dying on
  MaxCyclesExceeded/kill without a flush).

---

## 11. STATIC ANALYSIS OF THE DECOMPRESSED FIRMWARE (option C, 2026-06-25)

GOAL: read the badge decision (`get_sysvar`/`build_dsrdb`) WITHOUT Ghidra, since the
`.exe` is the COMPRESSED self-decompressing image (no plaintext code/strings).

### Decompressed image built + verified (the durable deliverable)
- `tools/host_decompressor` (oracle.c + inflate.c) builds clean with clang on the Mac
  (`cc -O2 -o oracle src/oracle.c src/inflate.c`). Run:
  `./oracle ../../firmware/ds20_v7_3.exe out/decompressed_ds20_v7_3.bin`.
- Output: WimC@0x2400, compressedSize=0x1f1097, **target=0x8000**, size 0x2f9a00 (3,119,616).
  Signature check PASSED (hw_ret R2 x3 / R6 x0) => byte-faithful.
- **ADDRESS MAP: runtime VA = file_offset + 0x8000** (decompress target). So a runtime PC X
  disassembles at image file offset X-0x8000. This is the KEY enabler: any PC the PA-watch
  later yields is instantly readable in this image.
- The `.bin` is a GENERATED artifact (regenerate via the oracle); NOT committed.

### Banner table FULLY decoded (@ 0x153cd8, stride 0x2c) -- confirms journal anchor
`table[member]`, base 0x153cac (member 0 = invalid, strptr 0x00f00000). Each 0x2c entry =
11 longwords: [0]=string ptr, [1]=code1, [2]=code2(0x19|0x4b), [3..8]=0xffffffff, [9]=[10]=0x41a.
- member 1 -> "AlphaPC 264DP %3d MHz" (0x19a6c8), code1=0x72e code2=0x19
- member 2 -> "AlphaServer DS20 %3d MHz" (0x19a6e0), code1=0x780
- member 3 -> "AlphaServer DS20 %3d MHz" (0x19a700), code1=0x730
- member 4..8 -> "COMPAQ AlphaServer DS20E" (0x1a90c8)
SYSVAR 0x405 -> (>>10)&0x3F = member 1 -> 264DP. Self-consistent with the live HWRPB.

### Decision strings + iic_ocp0 located (empirically, == journal)
"Defaulting system type to AlphaPC 264DP" @ **0x19ad90**; "Error determining system type,
SYSVAR = %x" @ **0x19adc0**; `iic_ocp0` @ 0x17a3c0 / 0x1a0218. NO debug symbols
(`get_sysvar`/`build_dsrdb` names absent).

### STRONG INFERENCE
The existence of the distinct message "Defaulting system type to AlphaPC 264DP" + our result
(SYSVAR=member 1=264DP) => `get_sysvar` is hitting its **DEFAULT path**: it cannot positively
identify the DS20 and falls back to 264DP. "Error determining system type, SYSVAR = %x" is the
error-path sibling (and would print the computed SYSVAR).

### Static code-location HIT THE DOCUMENTED GP-RELATIVE WALL (route exhausted)
Four independent methods all returned ZERO get_sysvar candidates: (1) absolute LDAH/LDA
construction of the targets; (2) gp-relative pairs 0x30 apart (Defaulting<->Error gap);
(3) triple-constraint (Defaulting+Error+table, consistent gp); (4) consensus-gp voting across
all known data targets incl. iic_ocp0. Confirmed: the string/table addresses are COMPUTED
gp-relative (NOT stored as literals -- 0x19ad90 appears NOWHERE as a 4/8-byte value), so static
XREF can't pin the decision instruction without the runtime gp or a runtime PC. This is exactly
the wall the 2026-06-24 journal recorded as EXHAUSTED -- do not relitigate it.

### NEXT (cheap + decisive)
1. **Console capture**: `plink -raw -P 10023 localhost | tee console.log`; look early in boot
   for "Defaulting system type to AlphaPC 264DP" or "Error determining system type, SYSVAR =
   0x..". The firmware tells us the path (and, for Error, the SYSVAR value) -- near-definitive,
   no disassembly.
2. **Runtime PC**: capture get_sysvar's write/IIC-probe PC via PA-watch, then disassemble this
   image at PC-0x8000. The image (built above) is the substrate that makes that trivial.

---

## 6. UNCOMMITTED at handoff (stage file-by-file, NATIVE git only)

- `systemLib/Machine.h`, `systemLib/Machine.cpp` -- the instrument.
- `tools/run_fw.sh` -- binary-name + BSD-sed portability.
- `scripts/build_mac.sh` -- Release/RelWithDebInfo/Debug option.
- `journals/20260625_hwrpb_scan_instrument_and_mac_build.md` (this file), `memory.md` entry.
- DO NOT `git add -A` (build artifacts: `out/`, `*.img`, `*flash.rom`, `ghidra/`). A stray
  literal-Windows-path file `D:\EmulatR\EmulatRAppUniV4\rpcc_probe.txt` appeared in the run
  dir from an `rpcc_probe` hardcoded path -- harmless, ignore/delete, do not stage.
- The instrument is a TEMP probe (like `EMULATR_SYSVAR_WATCH`): REMOVE once the region map
  is locked.
