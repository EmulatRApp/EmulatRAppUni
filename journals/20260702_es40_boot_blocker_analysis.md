# ES40 Boot-Blocker Analysis — It Is Not the IIC Base

**Date:** 2026-07-02
**Model:** ES40 (Tsunami 21272), firmware `firmware/es40_v7_3.exe` (SRM V7.3)
**Status:** empirical findings from traced cold-boot runs. Supersedes the working
assumption that ES40 stalls at the IIC controller base. Three candidate blockers
identified and ranked; the real current end-state is an **unimplemented CSERVE
function (0x66)**, not the IIC.

---

## UPDATE (2026-07-02, later) — root cause is an EmulatR SIGSEGV, not a missing CSERVE

Follow-up debugging (lldb + the authoritative `$cserve_def` from
`ev6_pc264_pal_defs.mar`) supersedes the "implement CSERVE 0x66" action below:

- **`CSERVE func=0x66` (102) is UNDEFINED in the spec.** The pc264 `$cserve_def`
  stops at `MP_WORK_REQUEST = 101 (0x65)`; `CSERVE$START` is 66 *decimal* = `0x42`,
  a different code. A full apisrm sweep finds **no** `0x66` CSERVE. The real PAL
  would no-op an undefined CSERVE too — so EmulatR's no-op is **faithful, not the
  bug**. Do NOT implement a `0x66` service. (credit: PC-side agent)
- **The actual failure is an EmulatR host-side crash.** Running past the ~282M
  point under lldb: the process takes **`SIGSEGV` (exit 139)** at
  cyc 282,057,652 in **`memoryLib::GuestMemory::write8` (`GuestMemory.cpp:319-320`)**
  — `std::memcpy(&page[pa & kPageMask], &value, 8)` with a **bad page base
  pointer** (fault address 64 KB-aligned, offset 0; address varies run-to-run =
  wild host pointer, not a within-page overrun). It is a **guest store** during
  the CSERVE-0x66 memory-operation phase (R17=`0xC0000000` region base,
  R18≈`0x7FFFA`≈512K count, R2=`0xffff4000`).
- **So `0x66` is a symptom/marker, not the blocker.** The blocker is the
  `write8` crash. The `CSERVE 0x66` storm is just the last thing logged before the
  faulting store.

**Suspected mechanism / fix:** `write8` computes
`pidx = static_cast<uint32_t>(pa >> kPageShift)` (`GuestMemory.cpp:240`; same
truncation on the read paths, lines 199/223) and relies solely on
`ensurePage(pidx)`'s `pidx >= m_pageCount` check. There is **no explicit
`pa >= m_size` upper-bound guard**, and the `uint32_t` truncation can alias a PA
outside backed RAM. **Recommended fix:** add an explicit bound at the top of the
byte accessors — `if (pa >= m_size) return MemStatus::OutOfRange;` (or route
above-RAM PAs to the MMIO path) — and, to capture the exact offending PA in one
line, log `pa` when `pa >= m_size`. This is a host-robustness bug worth fixing
regardless of ES40: **a guest store to an out-of-range PA must not segfault the
emulator.**

**Revised next action (replaces #1 below):** fix `GuestMemory::write8`
out-of-range handling; re-run ES40; capture the offending PA to learn what the
firmware's CSERVE-0x66 memory op is actually addressing; only then revisit the
IIC/device phase.

---

## TL;DR

1. **The IIC base is a red herring for the current block.** The
   `TsunamiChipset: no proven IIC base for model 'ES40' -- IIC left UNMAPPED`
   line is printed at **chipset construction, before the CPU runs**. In a
   512M-cycle cold boot the ES40 firmware **never reaches IIC/PCI device code** —
   no `UNHANDLED OUTER`, no PCI-config, no IIC traffic is emitted.
2. **ES40 boots much further than expected.** It clears decompression, PAL
   relocation (→ `0x8000`), and runs deep into the native SRM console
   (`0x1bxxxx`) — comparable to DS20's progress.
3. **The real end-state at ~282M cycles is a storm of `CSERVE func=0x66`
   (102) calls** from PC `0x1b78f8`, which EmulatR treats as "reserved / no-op."
   The run **terminates abnormally at cyc 282,057,652** with no clean stop
   epilogue. This — not the IIC — is the current ES40 blocker.
4. The **early `0x600920` poll loop is a transient**, not a hang: it is present
   ~5–8M cycles and the PC advances past it.

---

## Original premise vs. what the trace showed

**Premise going in:** "ES40 is bound in a tight loop and unable to pass the IIC
base." This came from the visible startup warning about the IIC being unmapped.

**What the runs actually show:** the IIC warning is emitted once, at construction
time, and is irrelevant to the runtime spin. The firmware progresses through
several boot phases and ends up in an unimplemented-CSERVE storm ~280M cycles
later, in a completely different code region (`0x1b78f8`, native SRM), having
never touched the IIC decode window.

Two intermediate assumptions were also **wrong and corrected by evidence**:
- "Add `kIicBaseByModel{ES40, 0xFFF80000}`" — wrong mechanism *and* wrong layer
  (see §5). ES40's I2C is the ALi M1543C SMBus, not a fixed-base PCF8584.
- "ES40 is wedged at `0x600920`" — that's an early transient; PC advances past it.

---

## Methodology

- **Builds** (Ninja, Qt 6.10.2, clang x86_64):
  - `out/build/mac-release` — Release, `EMULATR_BRINGUP_PROBES=OFF` (clean binary).
  - `out/build/mac-diag` — Release, **`EMULATR_BRINGUP_PROBES=ON`** (adds the
    `HW_REI`/`PCSAMPLE`/`CSERVE`/`UNHANDLED OUTER` narration used below). Kept
    separate so the clean release binary is untouched.
- **Model selection:** ES40 is INI-driven (`[System] model = ES40`); there is no
  `--model` flag. Each run set the model in `config/EmulatrV4.ini` and restored
  it afterward.
- **Runs:** cold boot (`--no-autoload`), `--mem 4 GiB`, varying `--max-cycles`.
  - 100M cyc (release): ended at PC `0x60141c`, `kFaultDtbMiss`.
  - 8M cyc (diag): ended at PC `0x601210`; `HW_REI` log captured (below).
  - 512M cyc (diag, `--max-cycles 0x20000000`): reached the CSERVE 0x66 storm at
    ~282M and terminated abnormally.
- **Instruments used:** `EMULATR_PCI_CFG_TRACE`, `EMULATR_IIC_TRACE`,
  `EMULATR_IIC_CTRL_TRACE`, `EMULATR_TIG_TRACE` (all silent — device code not
  reached); the compile-gated `HW_REI`/`CSERVE` probes did the real work.
  - Note: `EMULATR_IIC_WATCH` is **hardcoded** to the DS20 range
    `0x800.FFF8.0000–1` (`pipelineLib/MemDrainer.h:1024`) and cannot observe ES40.
  - The `UNHANDLED OUTER READ/WRITE` forensic is compile-gated by
    `EMULATR_BRINGUP_PROBES` (`chipsetLib/TsunamiPchip.h:716`), **OFF** in normal
    release builds — hence the diag build.

---

## Boot trajectory (phase by phase)

| Phase | Cycles | PC / evidence | Notes |
|-------|--------|---------------|-------|
| SROM / decompression | 0 – ~4.19M | `PCSAMPLE pc=0x900000 → 0x9003f0` | self-decompress on emulated CPU |
| PAL relocation to `0x600000` | ~4.19M | `Step D PAL relocation TRIGGER pa=0x6005c0` | expected |
| Early SRM poll loop | ~5M – 8M+ | `HW_REI` from `0x600351` → `0x600920` (also `0x6009a4`), steady **~90,134-cyc** period | **transient**, not a hang — timer-cadence poll/wait; PC advances past it |
| Native SRM console | 8M – ~280M | PC in `0x1bxxxx`, PAL relocated to `0x8000`, `palMode=0` | substantial progress; memory scan (UNALIGN-FIXUP at pc `0x5afac`, va `0x3fc12xxx` +8/step) |
| **CSERVE 0x66 storm** | ~279.9M – 282.06M | `CSERVE func=102 (0x66)` from PC `0x1b78f8`, ~1300+ calls | **current blocker**; EmulatR no-ops it; run ends abnormally at cyc 282,057,652 |

---

## The three candidate blockers, ranked

### 1. (PRIMARY) Unimplemented `CSERVE func=0x66` — the current end-state
- ES40 SRM calls **CSERVE function `0x66` (102)** repeatedly from PC `0x1b78f8`
  (grainPc `0x1b78f8`, palBase `0x8000`, native mode), starting ~cyc 279.9M and
  bursting through 282.06M. Also a handful of `0x65` (MP_WORK_REQUEST, correctly
  no-op'd).
- **EmulatR does not implement `0x66`.** `palBoxLib/grains/PalEntries.cpp:401`
  names only `0x65 = MP_WORK_REQUEST`; everything else (incl. `0x66`) returns
  `"(reserved / no-op)"` and defaults to a no-op.
- **Call context (register args) points to a memory-region service:**
  `R16=0x66` (func), `R17=0xC0000000`/`0x40000000` (region base), `R18=0x7fffa`
  (~512K count), `R4=0x3fffffff`/`0xffffffff` (masks). This is consistent with a
  Bcache/memory scrub, page-zero, or ECC/scan service the ES40 SRM expects the
  console/PAL to perform. No-op'ing it likely leaves memory (or a result the SRM
  reads back) in a state that derails the firmware.
- **The 512M run terminated abnormally at ~282M** (0 `PROFILE`/`Stop reason`
  lines in 13,707 lines of output; highest `cyc=282057652`) — i.e. the process
  did not run out its cycle budget cleanly; it died during the 0x66 storm.
- **ACTION:** identify `CSERVE 0x66` in the ES40/EV6 `$cserve_def`
  (apisrm `pal_def.sdl` / `ev6_pc264_pal_defs`) and implement it (or a faithful
  effecting stub) in `PalEntries.cpp`. This is the highest-value ES40 fix.

### 2. (LATENT) IIC base unmapped for ES40
- Real gap, but **downstream** of blocker #1 and not hit in 512M cycles.
- `chipsetLib/TsunamiChipset.h:690` `kIicBaseByModel[]` has proven rows only for
  DS10 (`0xFFFF0000`), DS20/DS20E (`0xFFF80000`); ES40/ES45/DS25 are deliberately
  left UNMAPPED rather than guessed. The manifest *does* stage ES40 IIC devices
  (`iic_acks=[0x70 0x72 0xA2 0xA4 0xC0]`), so the intent exists; the controller
  decode does not.
- See §5 for why the fix is **not** a one-line `0xFFF80000` row.

### 3. (NOT A BLOCKER) Early `0x600920` poll loop
- Present ~5–8M cycles; steady ~90,134-cycle `HW_REI` cadence into `0x600920`.
  Looks like an interrupt-serviced calibration/wait. The PC **advances past it**
  (by 100M it's at `0x60141c`; by 280M it's in `0x1bxxxx`). Document only so a
  future reader doesn't re-chase it.

---

## §5. IIC addressing on ES40 — authoritative source reconciliation

Two SRM source trees under `Processor Support/` give *different* answers because
they are different firmware generations:

- **`apisrm/ref/pc264_io.c`** (older, "CLIPPER"/PC264 family): memory-mapped
  PCF8584 at fixed base **`0xFFF80000`** — `iic_write_csr` does
  `outmemb(0, 0xfff80000 | addr, data)`; S0 at +0, S1 at +1. `galaxy_def_configs.h`
  groups `#if (CLIPPER || PC264)` and `CLIPPER` = "Compaq AlphaServer ES40".
- **`srmconsole/5.8/PC264/…`** (newer, per-platform): ES40 firmware platform is
  **SHARK** (`PLATFORM.MAR: SHARK=1, M1543C=1`). `API_IIC.C` routes I2C by bus:
  SHARK → **`M1543_BUS`** (ALi M1543C SMBus) or **`CCHIP_BUS`** (Cchip GPIO);
  only SWORDFISH uses a direct `PCF8584_BUS`. The M1543C SMBus base is
  **programmable**, read at runtime from the M7101 PMU PCI config register
  `SBASMB` (offset `0x14`, `& 0xFFFFFFFC`) — not a fixed address.

**Which does `es40_v7_3.exe` use?** V7.3 is late → the SHARK/M1543C path is far
more likely. **Corroborated by EmulatR's own code:** the ES40 I2C path is stubbed,
not built —
- Cchip `MPD` (`0xC0`, "SPD/I2C") is a **static read-only stub**, SPD pins fixed
  at `0xFF` (`TsunamiCchip.h:99`);
- the ALi M1543C **SMBus is not modeled** — `AliM1543C.h` is a "bring-up
  scaffold: identity + enumeration + config store-through," and the M7101 PMU
  (dev `0x7101`, owner of `SBASMB`/SMBus) is a **config-identity stub only**.

**Conclusion for the IIC row:** do **not** add `{ES40, 0xFFF80000}` — that maps a
fixed PCF8584 the SHARK firmware won't poke. When ES40 eventually reaches I2C, the
correct work is to model the **M1543C SMBus controller** at the I/O base the
firmware programs into M7101 `0x14` (and/or the Cchip GPIO/`MPD` bit-bang path).
The empirical confirmation (which mechanism/base `es40_v7_3.exe` actually uses)
requires reaching that code — which is **blocked by #1**. `ES45`/`DS25` are
Typhoon/Titan and must be checked separately.

---

## Is the Tsunami/Typhoon chipset "implemented for ES40"?

**Scaffolded, not proven.** The variant mapping (`ES40 → Tsunami 21272`,
`TsunamiVariant.h:152`), Cchip, Pchip, the ALi M1543C south bridge, the PCF8584
model, and the ES40 platform manifest all exist. But ES40 **boot** is far less
exercised than DS20's: it wedges on an unimplemented CSERVE service (#1) before
device bring-up, and the ES40-specific I2C path (M1543C SMBus / Cchip GPIO) and
several M1543C field effects are explicit TODO stubs. So the answer to "is it
implemented" is: **the chipset skeleton is there; the ES40 boot path is not yet
proven and has at least one hard gap (CSERVE 0x66) upstream of the device work.**

---

## Recommended next steps (in priority order)

1. **Decode & implement `CSERVE 0x66`.** Find its definition in the EV6/ES40
   `$cserve_def` (apisrm `pal_def.sdl` / `ev6_pc264_pal_defs`); disassemble the
   ES40 image around caller PC `0x1b78f8` to confirm the contract (args in
   R16–R19, expected R0). Implement in `palBoxLib/grains/PalEntries.cpp`. This is
   what currently ends the boot.
2. **Re-run and re-baseline.** With 0x66 handled, cold-boot ES40 again and find
   the next stop; the IIC/PCI device phase should finally be reached.
3. **Then** address the ES40 I2C path per §5 (M1543C SMBus model), *not* a fixed
   PCF8584 base row.
4. Housekeeping: `EMULATR_IIC_WATCH` is DS20-hardcoded (`MemDrainer.h:1024`) —
   generalize it (or add an ES40 range) before relying on it for ES40.

---

## Appendix — reproduction

```bash
# Diagnostic build with bring-up probes (keeps mac-release clean):
cmake -S . -B out/build/mac-diag -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DEMULATR_BRINGUP_PROBES=ON -DCMAKE_PREFIX_PATH=/Users/timpeer/Qt/6.10.2/macos
cmake --build out/build/mac-diag

# ES40 cold boot (set model=ES40 in config/EmulatrV4.ini first; restore after):
cd out/build/mac-diag
./Emulatr --firmware firmware/es40_v7_3.exe --mem 4294967296 --no-autoload \
          --max-cycles 0x20000000 > es40_long.out 2>&1

# Key greps:
grep -E "CSERVE|Stop reason|PROFILE" es40_long.out
grep -oE "func=[0-9]+ \(0x[0-9a-f]+\)" es40_long.out | sort | uniq -c
```

### Evidence anchors
- `palBoxLib/grains/PalEntries.cpp:401` — CSERVE dispatch; `0x66` → reserved/no-op.
- `chipsetLib/TsunamiChipset.h:690` — `kIicBaseByModel` (DS10/DS20 only).
- `chipsetLib/TsunamiCchip.h:99` — MPD SPD/I2C static `0xFF` stub.
- `chipsetLib/AliM1543C.h` — M1543C/M7101 config-only scaffold (no SMBus).
- `pipelineLib/MemDrainer.h:1024` — `EMULATR_IIC_WATCH` DS20-hardcoded range.
- `chipsetLib/TsunamiPchip.h:716` — `UNHANDLED OUTER` forensic (probe-gated).
- Authoritative I2C: `Processor Support/.../apisrm/ref/pc264_io.c` (CLIPPER,
  `0xFFF80000`) vs `.../srmconsole/5.8/PC264/SRC/{API_IIC,M1543C_IIC}.C`,
  `.../CFG/SHARK/PLATFORM.MAR` (SHARK/M1543C SMBus, programmable via M7101 `0x14`).
```
