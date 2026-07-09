# ES40 boot blocker = SRM COM2 `combott_txready()` spin — NOT the memory-layout ACV (2026-07-06)

**Status:** Root of the ES40 runtime frontier re-identified. The long-standing
"`0x1b7dd4` `kFaultAcv` garbage-pointer / Typhoon-memory-layout" framing is
**stale and wrong**. The machine's terminal state is an **infinite poll of COM2's
Modem/Line Status registers** driven by the SRM console driver
(`apisrm/ref/combo_driver.c :: combott_txready()`, `GALAXY && CLIPPER` path).
Root A (the UART `readMSR` fabrication) was tested and is **NOT the lever** —
deeper tracing shows the `0x1b7xxx` PCs are LEAF I/O helpers, and the real loop
is a **console-output / callback routine at `0x62xxx`** (caller `R26=0x629f0`).
Working hypothesis is now the SRM console-**callback** path (`call_backs.c` /
CRB), not the UART status bits. See "SESSION 2 CONTINUATION" below. WIP —
resuming next session.

This supersedes the ACV framings in:
- `journals/20260702_es40_acv_garbage_origin_traced.md`
- `journals/20260703_es40_4gb_aar_memory_sizing_action_plan.md`
- the "ES40 `0x1b7dd4` ACV" line in `journals/20260706_pm_session_handoff.md`

---

## TL;DR

The ES40 does not hang on a bad RAM pointer. It hangs **spinning in the SRM
console output path**, reading COM2's `MSR (0x2FE)` and `LSR (0x2FD)` forever and
never seeing the bit it waits for. This is the same *class* of bug as the DS10
TIGbus `BLBS` poll (fixed 2026-06 by "return 0, not all-ones"): V4 answers an
unmodeled / unpopulated interface with a value the firmware can never satisfy.

The original poster's very first instinct — "*it seems this is likely due to the
undefined interfaces that it requires*" — was correct. The intervening
memory-layout and kseg-translator theories were both chasing a **stale**
`kFaultAcv` record.

---

## How we got here (correction chain)

| Belief | Source | Verdict |
|--------|--------|---------|
| Garbage-pointer `LDQ` ACV @ `0x1b7dd4`, `R16=0xFFFF…7F82` | 20260702 journal | **Stale.** Fault site moved; value differs. |
| Typhoon 4 GB memory-region / MEMDSC overshoot, "capture R9" | PM handoff | **Wrong lever.** Fill-loop bound is **R18**, not R9. |
| kseg / `Ev6Translator` VA-form mis-decode | this session (Hyp. A) | **Ruled out.** Terminal fault is an I/O poll, not a translation ACV. |
| **COM2 MSR/LSR poll never exits (SRM `combott_txready`)** | this session | **Confirmed** against `apisrm` source. |

---

## Evidence — the retire-time DIAG capture

Facility: `pipelineLib/PipelineDriver.h` retire hook — `EMULATR_DIAG_PCLO/PCHI`
(PC-window instruction trace) + `EMULATR_DIAG_WREG/WMIN` (register last-writer).
Compiled into `relwithdebinfo` (stripped from `release`). Run rooted at
`out/build/relwithdebinfo` per the new CLAUDE.md "Build & run conventions".

Run recipe:
```bash
EMULATR_2D_NOOP=1 EMULATR_SPINSKIP=1 EMULATR_NO_PUTTY=1 \
EMULATR_DIAG_PCLO=<lo> EMULATR_DIAG_PCHI=<hi> EMULATR_DIAG_WREG=<n> ... \
./Emulatr.exe --firmware firmware/es40_v7_3.exe --mem 4294967296 \
  --no-autoload --max-cycles 0x50000000 > <log> 2>&1
```

### 1. The memory-fill loop @ `0x5afac` — real, but NOT the blocker
```
0x5afac  STQ R16, 0(R0)     ; store fill value R16
0x5afb4  LDA R0, 8(R0)      ; R0 += 8   (marching pointer)
0x5afb8  BNE R18, 0x5afa8   ; loop while R18 != 0   ← counter is R18
```
- Fills from `~0x3fc12000` (≈1 GB) upward, **unaligned** (`va` ≡ 7 mod 8), so every
  store takes an `UNALIGN-FIXUP` — the dominant reason wall-cycles ≫ retires.
- **Completes** (terminal `R18 = 0`). The handoff's "R9 bound" was a dead end; the
  `R9` writes it flagged come from an unrelated routine at `0x5250c`.
- This loop is upstream activity, not the terminal hang.

### 2. The terminal loop @ `0x1b7d80` — COM2 status poll
```
0x1b7d80  enc=0x28100000  =  LDBU R0, 0(R16)     (opcode 0x0A = byte load)
          memAddr alternates  0x801fc0002fe  <->  0x801fc0002fd   (fault=0, never advances)
```
- `0x801.FC00.0000` = the **IODense** window (it is literally `R20` in the register
  dump). Offsets `0x2FE`/`0x2FD` = ISA I/O ports **COM2 MSR** and **COM2 LSR**
  (MILO `uart.h:80  #define com2Msr 0x2fe`).
- `memAddr` never marches past those two bytes — it is a **poll**, not a walk.
- The reads are byte-width (`LDBU`), matching an 8-bit UART status register.

### 3. The `kFaultAcv` was a red herring
Terminal dump: `Stop: MaxCyclesExceeded at PC=0x1b7d34, lastFault=7 (kFaultAcv)`,
`R16=0x80000d0000000000`, `R20=0x00000801fc000000`. The `lastFault=7` is a
**stale** record from an earlier fault; the terminal *activity* is `fault=0`
COM2 status reads. Do not chase `0x1b7d34` as an ACV site.

---

## Source mapping — `apisrm/ref/combo_driver.c :: combott_txready()`

The guest binary is DEC SRM (`es40_v7_3`). The `0x1b7d80` loop maps to the SRM
console COM driver, built for `GALAXY && CLIPPER` (**ES40 = Clipper**). The routine
reads MSR then LSR — exactly the observed `0x2FE` then `0x2FD` pattern — and is
reached from the console-output path `console_ttpb->txready()` (`kernel.c:1947`).

```c
int combott_txready (int port) {
    cp = com_devtab + port;
    c = combo_inportb (cp->port + MSR);            // <-- reads 0x2FE (MSR)

#if GALAXY && CLIPPER
    if ((get_console_base_pa() == 0) &&
        (port == 1) /* COM2 */ &&
        (cpu_mask != all_cpu_mask)) {
        ... (reads IER 0x2F9 + LSR 0x2FD) ...
        return (0);                                // <-- "don't service" — never tx-ready
    }
#endif
    if (!cbip && cp->flow & 2) {                   // hardware flow control
        if ((c & MSR$M_DSR) && !(c & MSR$M_CTS))   // DSR set AND CTS clear
            return (0);
    }
    if (combo_inportb (cp->port + LSR) & LSR$M_THRE) // <-- reads 0x2FD (LSR)
        return (1);
    else { ...; return (0); }
}
```

The console-output caller loops until `txready()` is true. If `txready()` can
never return non-zero, the console print spins — which is precisely the terminal
state.

---

## V4's contribution — `Uart16550::readMSR()` fabricates a populated port

`deviceLib/Tsunami/Uart16550.h`:
```c
uint8_t readMSR() const noexcept {
    uint8_t msr = 0x30;                         // CTS + DSR asserted, UNCONDITIONALLY
    if (m_backend && m_backend->isConnected())
        msr |= 0x80;                            // DCD only if a backend is connected
    return msr;
}
```
`readLSR()` returns `THRE|TEMT` with `DR=0` whenever there is no RX data. **COM2
(`0x2F8`) has no backend** in V4 (the console is COM1 `0x3F8`), so for COM2 these
are constant: `MSR = 0x30`, `LSR = 0x60`, forever.

Real, unpopulated COM2 hardware reads `MSR = 0x00` (no modem lines). V4's `0x30`
makes an **empty port look half-alive** — CTS+DSR asserted but nothing behind it.
Note V4's own inconsistency: `DCD` is gated on `isConnected()`, `CTS+DSR` are not.
The MILO device-detect idiom (`uart.c: if (inb(com2Lsr)==0) break;` and
`uart_getchar`'s `while(!(LSR&1));`) shows firmware keys "no device" off reading
`0`/quiescent status — which V4 never presents.

---

## Two candidate roots (one capture away from resolved)

Both are consistent with a loop that reads MSR+LSR forever:

**A — UART model.** V4's fabricated `MSR=0x30` (CTS+DSR on an empty port)
mis-feeds the flow-control test `if (DSR && !CTS) return 0` and/or the device
detection. Fix ≈ 2 lines: gate `CTS+DSR` on `isConnected()` in `readMSR()` so an
unconnected COM2 reads `0x00`, and let the port present the "absent device"
signature the firmware checks. Local to `Uart16550.h`.

**B — Console-base / HWRPB.** If `get_console_base_pa() == 0`, the `GALAXY &&
CLIPPER` COM2 gate makes `combott_txready` return `0` **unconditionally**,
regardless of UART state, and the console-output caller spins. Fix is on the
HWRPB / console-base-PA setup path — directly adjacent to the active
`journals/HWRPB_Region_Fidelity_and_Resume_20260624.md` work, NOT the UART.

### Distinguishing test
Widen the PC window over the whole routine and watch which I/O ports are read:
```bash
EMULATR_2D_NOOP=1 EMULATR_SPINSKIP=1 EMULATR_NO_PUTTY=1 \
EMULATR_DIAG_PCLO=0x1b7d00 EMULATR_DIAG_PCHI=0x1b7dc0 EMULATR_DIAG_CAP=400 \
./Emulatr.exe --firmware firmware/es40_v7_3.exe --mem 4294967296 \
  --no-autoload --max-cycles 0x50000000 > es40_com2_txready_diag.log 2>&1
```
- Loop reads **IODense `0x801fc0002f9` (IER)** each pass ⇒ it is inside the Galaxy
  `get_console_base_pa()==0` block ⇒ **Root B** (console-base/HWRPB).
- Loop reads only `0x2fe` (MSR) + `0x2fd` (LSR) and returns on `LSR.THRE` ⇒ it is
  the flow-control / detect path ⇒ **Root A** (UART model).

---

## RESOLVED (2026-07-06, same day): Root A confirmed, fix staged

The widened `0x1b7d00–0x1b7dc0` capture (`es40_com2_txready_diag.log`) settles it:

- **No IER (`0x801fc0002f9`) read anywhere** in the loop ⇒ the Galaxy
  `get_console_base_pa()` gate is NOT taken ⇒ **Root B ruled out**.
- The looping body is the SRM byte-I/O helper `combo_inportb`:
  ```
  0x1b7d80  LDBU R0,0(R16)   ; read MSR (0x2fe) / LSR (0x2fd)
  0x1b7d84  MB               ; barrier after the MMIO read
  0x1b7d88  JMP (R26)        ; return to caller
  ```
  called repeatedly from the COM2 status path. The periodic `0x1b7d30/0x1b7d34`
  (pal=1, JMP R26) pair is a recurring interrupt/clock callback, not the blocker.

**Root cause — `Uart16550` fabricates modem lines on an UNWIRED port.** COM2 is
constructed `m_com2{ nullptr, 0x2F8, "COM2" }` and never receives `setBackend()`
(only COM1 does, `Machine.cpp:417`), so its `m_backend == nullptr` — yet
`readMSR()` returned `0x30` (CTS+DSR) unconditionally. The SRM reads CTS+DSR
asserted, concludes COM2 is populated, and then waits on carrier/line status an
empty port can never deliver.

**Staged fix (experiment).** `deviceLib/Tsunami/Uart16550.h :: readMSR()` now
returns `0x00` when `m_backend == nullptr` (unwired port). Wired ports (COM1) are
unchanged: `0x30`, `|0x80` (DCD) when a client is attached. Rebuild
`relwithdebinfo`, re-run the boot, and check whether the frontier advances past
`0x1b7d80`:
- Advances ⇒ Root A + fix confirmed; capture the new frontier.
- Still spins ⇒ widen the window to the `combott_txready` caller PC (the `R26`
  return site) and read the exact bit/line it tests; the LSR "no device"
  presentation (`readLSR()`) may also need gating.

---

## SESSION 2 CONTINUATION (2026-07-06, late) — deeper than the UART

The Root-A MSR fix was built (after `touch deviceLib/Tsunami/Uart16550.h` to force
the recompile — the mount's mtime is skewed, so an untouched header no-ops the
build) and re-run. **Frontier unchanged** — still `Stop at PC=0x1b7d34`,
byte-identical registers. That's the *expected* negative: per `combott_txready`
source, the `if ((c & DSR) && !(c & CTS))` check yields the same result for
`MSR=0x30` and `MSR=0x00`, so the MSR value cannot gate this loop. The
`readMSR()` edit is still correct (an unwired port shouldn't assert modem lines)
and is left staged, but it is **not** the blocker's lever.

### The `0x1b7xxx` region is a bank of LEAF I/O helpers, not the loop

Widening the PC window revealed each `0x1b7xxx` "loop" is a tiny helper of the
form `load/store ; MB ; JMP (R26)`:

| Helper PC | op | reads/writes | note |
|-----------|-----|--------------|------|
| `0x1b7d80` | `LDBU R0,0(R16)` | IODense `0x2fe/0x2fd` (COM2 MSR/LSR), `0x70/0x71` (RTC) | `combo_inportb` byte read |
| `0x1b7dd4` | `LDQ R0,0(R16)` | TIGbus `0x801.100A.4000` marching **+0x40** | bulk TIG/flash read (progressing, bounded) |
| `0x1b75a0` | `STQ R31,0(R9)` | RAM `0x2083d0`+ marching +8 | memzero (progressing, bounded) |
| `0x1b7e20` | read | IODense `0x70` (RTC) | another leaf read |

These are leaf functions; each capture window that included them filled `CAP`
with helper traffic before reaching the decision code. The `memzero` and
TIG/flash loops **march** (progress) — they are not the deadlock.

### The real loop is the CALLER at `0x62xxx` (R26 = 0x629f0)

The terminal dump's `R26 = 0x629f0` pointed at the caller. Windowing
`0x62800–0x62c00` captured it: a substantial routine spanning
`0x628b8–0x62958` + `0x629c8–0x62a00` that:
- builds a buffer on the **stack** (`0x208b50–0x208c0d`) — includes `STB` byte
  stores (`0x628f4 -> 0x208c0d`), i.e. string/byte formatting;
- reads a **source region `0x173xxx`** (`0x173478`, `0x173198`, `0x173458`);
- maintains a **counter at stack `0x208bfc`** — load / increment / store every
  pass (`0x62938 LDL`, `0x6293c +1`, `0x62940 STL`);
- calls the `0x1b7xxx` leaf I/O helpers.

Shape = a **console output / formatting (`puts`-like) routine**, almost
certainly SRM **console-callback** code (`apisrm/ref/call_backs.c` /
`callbacks_alpha.mar`, the CRB routines). Only ~15 iterations appeared in an
800-line window, so it is a large, slow outer loop, not a 3-instruction spin.

### Working hypothesis (revised)

The blocker is the **console-callback path looping because the console
environment / CRB it expects is not what V4 presents** — i.e. a HWRPB/CRB
fidelity gap, NOT a UART status bit. This aligns with the observation that the
callback routines were never actually implemented/populated for any platform,
and with the active `HWRPB_Region_Fidelity_and_Resume_20260624.md` work.

Open question not yet resolved: **deadlock vs. merely slow.** The `0x208bfc`
counter increments each pass — need to learn whether it is bounded (routine
eventually returns; boot is just grinding through slow TIG reads + unaligned
fixups) or unbounded (true deadlock waiting on a callback/handshake).

### RESUME NEXT SESSION — concrete steps

1. **Identify `0x628b8` in source.** Match the "format into stack buffer + emit
   via I/O helpers + counter" routine to `apisrm/ref/call_backs.c` /
   `callbacks_alpha.mar`. Naming it tells us exactly what console state / CRB it
   expects. (PC-tracing has hit diminishing returns — do this from source.)
2. **Bound-check the `0x208bfc` counter** (memory watch on PA `0x208bfc`, or a
   value-gate) to settle deadlock vs slow.
3. **Inspect the CRB / console-callback setup** V4 builds in the HWRPB against
   what the routine dereferences (the `0x173xxx` source region is a candidate —
   identify what lives there).
4. Keep the `readMSR()` edit (correct, harmless) OR revert if preferred:
   `git checkout deviceLib/Tsunami/Uart16550.h`.
5. Build discipline: `touch` the changed header before `./tools/build_emulatr.sh
   relwithdebinfo`, and confirm the `built:` timestamp is fresh (mount mtime is
   unreliable; verify natively).

### Artifacts (this session)
- Logs in `out/build/relwithdebinfo/`: `es40_r09_diag.log`,
  `es40_acv_r16_diag.log`, `es40_com2_txready_diag.log`,
  `es40_com2_caller_diag.log` (memzero `0x1b75a0`),
  `es40_com2_caller2_diag.log` (TIG/flash `0x1b7dd4`),
  `es40_com2_caller3_diag.log` (the `0x62xxx` caller), `es40_msrfix_boot.log`.
- Staged edit: `deviceLib/Tsunami/Uart16550.h :: readMSR()`.
- Build-conventions edit landed: `CLAUDE.md` + `tools/build_emulatr.sh`
  (out/build/<config> rooting, deps-only mirror).

---

## Aside — `i2c.tar` is NOT part of this blocker

`Processor Support/.../i2c/i2c/i2c.tar` (also mirrored under `PalcodeBitsavers/`)
unpacks to a standalone Linux/Alpha **hardware-monitor + I²C** package:
`hwmon`, `lm75.c` (temp sensor), `adm9240.c`, `i2c-algo-pcf.c` (the PCF8584
bit-bang algorithm), and `hwmon-*.conf` for up2000/nautilus/shark/swordfish. It is
NOT in the `apisrm` console tree (correctly noted by the OP). It is a good
reference for the **later** IIC/FRU/environmental work — Gap 1 (`build_power_hw`,
PCF8584 base, FRU/temp EEPROMs) — but irrelevant to the COM2 spin. Filed for the
device-enumeration phase.

---

## Next steps

1. Run the distinguishing capture above; classify Root A vs Root B.
2. **If A:** patch `Uart16550::readMSR()` (and confirm `readLSR()` "no device"
   presentation) so an unconnected COM is quiescent; rebuild; re-run to a new
   frontier. Show diff before applying (V4 house convention).
3. **If B:** trace `get_console_base_pa()` / console-base-PA setup in the HWRPB
   hand-off; align with `HWRPB_Region_Fidelity_and_Resume_20260624.md`.
4. Either way, this is the single gate to the ES40 console prompt; the
   `build_power_hw` IIC work (Gap 1) is still downstream of it.

---

## Reference index (exact sites)

- V4 terminal loop: guest `PC 0x1b7d80`, `LDBU`, IODense `0x801.FC00.02FE/02FD`.
- V4 fill loop: guest `PC 0x5afac`, `STQ R16,0(R0)` / `BNE R18`.
- V4 UART model: `deviceLib/Tsunami/Uart16550.h :: readMSR() / readLSR()`.
- V4 IODense base: `R20 = 0x00000801fc000000` (register dump).
- SRM driver: `apisrm/ref/combo_driver.c :: combott_txready()` (line 348),
  Galaxy COM2 gate ~line 367, flow-control ~line 405, caller `kernel.c:1947`.
- Port defs: MILO `uart.h:80  com2Msr=0x2fe`; apisrm `combo_def.sdl` (LSR=5, MSR=6).
- MILO idiom: `milo-.../uart.c :: uart_getchar / uart_init_line`.
- DIAG facility: `pipelineLib/PipelineDriver.h` (`EMULATR_DIAG_PCLO/PCHI/WREG/WMIN`).
- Logs: `out/build/relwithdebinfo/es40_r09_diag.log`, `es40_acv_r16_diag.log`.
