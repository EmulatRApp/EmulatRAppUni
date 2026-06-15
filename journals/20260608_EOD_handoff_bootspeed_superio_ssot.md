# EOD Handoff — 2026-06-08 — Boot speed, dqa0 confirmed, SuperIO diagnosed, SSOT wired

Big session. This is the durable record; the Cowork task list (#1–#22) is
session-scoped, so the ledger at the bottom is the source of truth.

---

## 1. Headline wins (DONE, validated live)

- **dqa0 enumerates end-to-end (#1–#3).** `show device` shows
  `dqa0.0.0.105.0  DQA0  EMULATR VIRTUAL CDROM`; full ATAPI handshake
  (reset → 0x14/0xEB sig → IDENTIFY PACKET → TEST UNIT READY → 02/3A/00
  no-media). The S4 IDE wiring was already correct; it just needed a clean
  full cold boot to exercise it. No code fix was needed (#2 moot).
- **`memory_test=none` → 39-min memory test GONE (3.8 s).** Set at `>>>`,
  persists via clean exit. Confirmed live: `64 Meg` at +3866 ms vs +2,339,886 ms.
- **Flash writes work despite the banner (#11 DONE).** `update srm` PASSED;
  `full_powerup_diags=OFF` survived a cold boot. "Flash ROM writes are
  disabled" is firmware-IMAGE protection, benign for config NVRAM.
- **Fast-forward warps exist and work** (`EMULATR_TICKWARP=1`): RSCCWARP at
  `0x7c304` collapsed a 260M-cycle wait coherently (checksum OK).

---

## 2. Code changes (state matters — most are COMPILE-PENDING unless noted)

A clean relwithdebinfo + release build was done late session, so the items
below should be IN those binaries. Verify with the env vars / behaviors noted.

### Built clean (in the last rebuild)
- **SSOT Slice A (#13):** `main.cpp` loads `EmulatorSettings` once via
  `IniLoader::loadDefault()`, threads `const EmulatorSettings&` into the
  `Machine` ctor (default arg, keeps tests building). `Machine::makeCom1Cfg`
  reads `srmConsole.port` from settings. Files: `main.cpp`, `systemLib/Machine.h`,
  `systemLib/Machine.cpp`.
- **SSOT Slice B (#14):** firmware path precedence CLI > ini; `AppOptions`
  `--firmware` no longer hard-required (`systemLib/AppOptions.cpp`); `main.cpp`
  falls back to `[ROM] firmwareImage`, errors only if both empty. Defaults
  corrected: `model ES45→DS10`, `cpuCount 4→1`, `firmwareImage es45→ds10_v7_3.exe`
  in `config/EmulatorSettings.h` + `config/EmulatrV4.ini`. SHA-256 enforcement
  SKIPPED (no crypto in tree; field parsed-but-unenforced).
- **CMake:** `config/EmulatorSettings.cpp` + `config/IniLoader.cpp` added to
  `EMULATR_SOURCES` — they were ORPHAN TUs (never compiled), which is why the
  ini was dead config. (`CMakeLists.txt`)
- **`EMULATR_CONSOLE_PORT` env override (#12):** per-launch console TCP port,
  default 10023, in `makeCom1Cfg`. Lets two instances coexist.
- **Timestamped console mirror (#7):** `Uart16550::writeTHR` now calls
  `consoleMirror()` — gated by `EMULATR_CONSOLE_MIRROR` (silent by default,
  retires the old ungated per-byte mirror), line-buffered, each line prefixed
  with wall-clock Δ since the previous line → the boot-phase profiler.
  (`deviceLib/Tsunami/Uart16550.h`)
- **FDC width-trace:** `Floppy82077.h` `fdcTrace` now logs `w=<width>` +
  full untruncated `val` (claude-web step 2).
- **SuperIO (dormant, #22):** `deviceLib/Tsunami/Smc37c669SuperIo.h` +
  `deviceLib/Tsunami/Floppy82077.h`, wired in `chipsetLib/TsunamiChipset.h`
  (`m_superio` over the 0x3F0 window, FDC delegated). Does NOT work yet — see §4.

### New files this session
- `journals/Config_SSOT_Integration_Scaffold_20260608.md` (SSOT design/scaffold)
- `deviceLib/Tsunami/Floppy82077.h`, `deviceLib/Tsunami/Smc37c669SuperIo.h`
- Edited `journals/Path_To_Prompt.md` §3 (removed "DO NOT build the FDC37C669").

---

## 3. Operational env vars (the knobs that now exist)

| Env | Effect |
|-----|--------|
| `EMULATR_CONSOLE_PORT=N` | console TCP port (default 10023); run instances side by side |
| `EMULATR_CONSOLE_MIRROR=1` | timestamped `[CON COM1 +dt ms] <line>` boot-phase profile to stderr |
| `EMULATR_FDC_TRACE=1` | FDC/SuperIO port trace (now with `w=`) |
| `EMULATR_TICKWARP=1` | fast-forward the RSCC/tick delay loops (0x7c304/0x7c314) |
| `EMULATR_FLASH_TRACE`, `EMULATR_IIC_TRACE`, `EMULATR_IDE_TRACE` | device traces |
| `EMULATR_NO_AUTOLOAD=1` | full cold boot (no snapshot autoload) |
| `EMULATR_STOP` sentinel (touch the file) | clean exit + `~Machine` flash flush |

Standard profiling run (timestamped, non-truncating log):
```
EMULATR_NO_AUTOLOAD=1 EMULATR_CONSOLE_PORT=10026 EMULATR_CONSOLE_MIRROR=1 EMULATR_TICKWARP=1 \
  ./out/build/relwithdebinfo/Emulatr.exe --firmware firmware/ds10_v7_3.exe --autosnapshot off \
  2> "run_$(date +%Y%m%d_%H%M%S).log"
```

---

## 4. Boot-phase profile (measured via the [CON] mirror)

Cold boot stalls, ~96 min total before the fixes:

| Phase | Cost | Cause | Status |
|-------|------|-------|--------|
| memory test | 39 min | full POST | FIXED: `memory_test=none` |
| post-GCT/FRU (pre-dqa0) | 6.9 min | `0x7bef0` software-tick loop | OPEN (#21) |
| pre-dva0 | 8.2 min | same loop | OPEN (#21) |
| dva0 check | 36 min | same loop | OPEN (#21) |

**The remaining ~50 min is ONE loop:** `pc=0x7bef0` stores `0x3c970 = counter+1`
once per ~262,147 cyc (2^18) — a calibrated software-tick busy-wait. The
`EMULATR_TICKWARP` gates (`0x7c304` RSCC, `0x7c314` tick) do NOT cover it.

**Next step for #21:** add a warp for the `0x7bef0` loop. Need its disassembly
first — existing warps hook the COMPARE PC (jump counter, let the loop exit);
`0x7bef0` is the STORE, so we need (a) the store source register and (b) the
target-compare PC/reg, else the store clobbers the jump. Plan: one-shot full
register dump at `0x7bef0` (like the TICKWARP gate announce), OR
`tools/alpha_disasm.py`. Tick interval here is 2^18 (not 2^20).

---

## 5. SuperIO (#22) — fully diagnosed, fix specced, blocked on a build quirk

- The firmware DOES configure the FDC37C669 SuperIO. The `0x3F0` write stream
  matches FDC37C935 HRM Table 61 one-for-one (`0x07` LDN, `0x60/0x61` base
  default `0x03F0`, `0x70` IRQ6, `0x74` DMA2, `0xF0/0xF1/0xF4` FDD, `0x30`
  Activate; globals `0x22/0x23/0x2D/0x2E`). Those bytes are the register INDICES.
- **Data + the `0x3F1` data-port writes are MISSING because the firmware writes
  index+data as 16-bit WORDS to `0x3F0`** (index low, data high). EmulatR folds
  that into one `0x3F0` access and the byte-only handler drops the high byte.
  Confirmed EmulatR has NO odd-port routing drop (registry routes `0x3F0`/`0x3F1`
  identically; `0x3F0` fires). The width-trace (`w=`) was added to confirm.
- **Fix (specced, not written):** byte-split config-port word accesses in
  `Smc37c669SuperIo` — low→`0x3F0` (index), high→`0x3F1` (data). LOCK byte order
  against the `0x60/0x03` pair; the `0x55,0x55` key is special (both bytes to
  `0x3F0`/CONFIG PORT, NOT split); scope ONLY `0x3F0` (leave FDC functional
  `0x3F2-0x3F5`, esp FIFO, byte-handled). Device-ID: 669 = index `0x0D` value
  `0x03` (smcc669 source); 935 = index `0x20` value `0x02`.
- **BLOCKER:** the `w=` trace never landed — incremental builds won't recompile
  `TsunamiChipset.cpp` on the 3-level header change (`Floppy82077.h` →
  `Smc37c669SuperIo.h` → `TsunamiChipset.h`). Use `cmake --build ... --clean-first`.
- Authority: `Processor Support/.../smcc669_driver.c`, `smcc669_def.h`,
  `pc264_init.c` (SMC_init), `pc264_io.c:528` (call site); FDC37C935 HRM (uploaded).
- Benign for the prompt; NOT a boot-speed factor (the 36-min dva0 stall is the
  `0x7bef0` sleep, not the floppy). dva0 box checked.

---

## 6. Other open items

- **#6 persistence wrinkle:** `full_powerup_diags=OFF` persisted across a cold
  boot but `memory_test` reverted to `full` once (possibly `update srm` reset it).
  Re-confirm: set `memory_test none` → `touch EMULATR_STOP` → reboot → `show`.
- **#10 halt:** `>>>halt` prints "CPU 0 is halted" and RETURNS to `>>>` — it does
  NOT exit/flush. `CALL_PAL HALT` exits cleanly through `~Machine::forceFlush`
  (code-verified), but the console `halt` command doesn't emit it. Clean shutdown
  = `EMULATR_STOP` sentinel. To make `>>>` exit, wire a console command to the
  stop path (#17 covers this).
- **#17 power_off / #18 echo-off:** specced (snoop interception; echo-off via
  console settings), not written. Double-echo at `>>>` is `echoEnabled=true` +
  guest echo; fix = default local echo off.
- **#4 IIC:** provisionally benign (boot reaches `>>>`); IIC status model is
  contract-correct (P1/P3/P5). Only 21 IIC-TXN total in a full boot — NOT the
  6-min stall.
- **SSOT C/D (#15/#16):** model/cpuCount plumb + retire `OPA_Console_Config`
  QSettings reader. `model` is the master switch → `<model>_platform.json` +
  default firmware. Precedence LOCKED: `struct defaults < ini < CLI`; env = debug
  toggles only.

---

## 7. Recommended next-session order

1. **Commit this clean build + run the test suite** (green checkpoint).
2. **#21 `0x7bef0` warp** — register-dump diagnostic → write the warp → the
   remaining ~50 min collapses → fully fast cold boot.
3. **#22 SuperIO** — `--clean-first` build to get `w=`, then the byte-split.
4. **#17/#18 console polish**, then **SSOT C/D**.

For day-to-day iteration: snapshots autoload to `>>>` in seconds — only use
`EMULATR_NO_AUTOLOAD` cold boots for profiling.

---

## 8. Task ledger (Cowork #1–#22, end of session)

- #1 dqa0 trace+classify — DONE
- #2 fix dqa0 + absent-slave — DONE (no fix needed; C1 contract handles it)
- #3 verify dqa0 in show device — DONE
- #4 IIC (01) classify — provisionally benign; in progress
- #5 boot-phase loops — superseded by #19/#21 (profiled)
- #6 SRM persistence — flash writes work; memory_test revert wrinkle open
- #7 gate console output — DONE (timestamped mirror, compile-pending→built)
- #8 profile cold-boot — DONE (the [CON] mirror is the profiler)
- #9 10x IPS — open (depends on #21 + JIT scope)
- #10 >>> halt clean shutdown — console halt does NOT exit; CALL_PAL HALT does
- #11 flash-disabled banner — DONE (benign)
- #12 console port — DONE (EMULATR_CONSOLE_PORT)
- #13 SSOT Slice A — DONE (built)
- #14 SSOT Slice B — DONE (built; SHA skipped)
- #15 SSOT Slice C (model/cpuCount) — pending
- #16 SSOT Slice D (retire OPA QSettings) — pending
- #17 console power_off — specced (snoop), pending
- #18 console echo-off — specced, pending
- #19 early-boot profile — DONE (memory test = 39min, fixed)
- #20 floppy fast-fail — DONE/subsumed into #22 (FDC = SuperIO LDN)
- #21 option-firmware scan sleeps — root-caused to 0x7bef0; warp is next
- #22 FDC37C669 SuperIO — DONE. Word-fold confirmed (16-bit words to 0x3F0,
  low=index high=data); byte-split implemented (config-port 0x3F0 only, FDC
  range unsplit) + start-in-config-mode + device-ID seed. Validated live:
  SIO-TRACE 0->93 all [cfg], correctly paired (0x60/0x03, 0xF061 base 0x03F0,
  0x70/0x06 IRQ6, COM1/COM2/LPT configured); 407 suite green; dqa0/dva0 intact.
