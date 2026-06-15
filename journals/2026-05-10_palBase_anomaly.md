# 2026-05-10 — palBase Anomaly Diagnosis

**Status:** Root-caused. One-line fix identified; secondary issue surfaced.

**Trace under analysis:** `D:\EmulatR\traces\20260509-205540_srm.trc` —
178,096,516 cycle retire-compact log from the overnight run that
halted on `kFaultUnimplemented` at PC `0x12f88`.

---

## TL;DR

V4's `execHwMtpr` leaf reads the **wrong operand**. PALcode encodes the
source GPR of `hw_mtpr` in **Rb** (bits 20:16); V4 reads **Ra** (bits
25:21), which is hard-wired to **R31** by the assembler macro. Every
HW_MTPR therefore writes **0** to its target IPR instead of the intended
value. Confirmed against canonical Digital PALcode source. V1 has the
same bug (inherited).

The morning's two `DEBUG: HW_MTPR HW_PAL_BASE` lines are not anomalies —
they are the bug firing visibly because we instrumented PAL_BASE
specifically. Every other HW_MTPR in the trace (hundreds, possibly
thousands of them) is silently writing 0 to its target IPR too.

The fault at `0x12f88` is a **second, independent** issue:
`hw_mtpr Rx, scbd=0x5F` references an IPR not in V4's `HW_IPR` enum.
scbd `0x5F` corresponds to **PT31** (32nd PAL temp) — present on
EV5/21164, but EV6/21264 only defines PT0..PT23 (scbd 0x40..0x57).
The SRM .exe being loaded contains legacy EV5-era IPR access.

---

## Evidence

### 1. The PALcode assembler convention

From `Processor Support\Palcode\palcode\apisrm\apisrm\ref\ev6_huf_decom.m64`
lines 146-156:

```
.macro hw_mfpr gpr, ipr_num
    reg_length = %LENGTH(gpr) - 1
    exp = <^x1f@16> ! <%EXTRACT(1,reg_length,gpr)@21> ! <ipr_num>
    respal19 exp
.endm

.macro hw_mtpr gpr, ipr_num
    reg_length = %LENGTH(gpr) - 1
    exp = <^x1f@21> ! <%EXTRACT(1,reg_length,gpr)@16> ! <ipr_num>
    respal1d exp
.endm
```

| Field           | HW_MFPR              | HW_MTPR              |
| --------------- | -------------------- | -------------------- |
| bits 25:21 (Ra) | **gpr (destination)**| `^x1f` = R31 (unused)|
| bits 20:16 (Rb) | `^x1f` = R31 (unused)| **gpr (source)**     |
| bits 15:0       | ipr_num              | ipr_num              |
| opcode          | 0x19 (respal19)      | 0x1D (respal1d)      |

So `hw_mtpr R30, HW_PAL_BASE` emits **Ra=R31, Rb=R30, scbd=0x10**.
The source register is in Rb.

### 2. V4's leaf reads the wrong field

`palBoxLib/grains/PalEntries.cpp::execHwMtpr` (line 316):

```cpp
if (coreLib::isPalTemp(sel)) {
    c.cpu->palTemp[coreLib::palTempIndex(sel)] = c.opA;   // <-- BUG
    return r;
}
// ... and similar for the switch arms ...
```

`c.opA` is populated when the grain row sets `S_ReadsRa`. The TSV row for
HW_MTPR in `GrainMasterV4.tsv` line 457:

```
HW_MTPR  0x1D  0x0  HwMtpr  Hw  PalBox  S_HwFormat|S_Privileged|S_IprWrite|S_ReadsRa|S_ReadsInt  write internal processor register
```

It reads Ra. Ra is R31 per the macro. R31 is hard-wired zero.
**Every HW_MTPR writes zero.**

`ExecCtx.h` line 41-43 confirms the wiring:

```cpp
//     S_ReadsRa  -> opA = readInt/readFp((encoded >> 21) & 0x1F)
//     S_ReadsRb  -> opB = readInt/readFp((encoded >> 16) & 0x1F)
```

V1 has the identical bug at `palLib_EV6/Pal_Service.h:3950`:

```cpp
const uint64_t value = readIntReg(slot, slot.di.ra);
```

### 3. The trace confirms

Cycle 4194406, PC `0x6005e0`, encoded `0x77fe1010`:

```
0x77fe1010 = 0111 0111 1111 1110 0001 0000 0001 0000
              opc=1D  Ra=R31  Rb=R30  scbd=0x10  func=0x10
```

Trace line 4194413 (post-commit state): `R30 = 0x0000000000600000`.

`hw_mtpr R30, HW_PAL_BASE` was meant to **set PAL_BASE := 0x600000** —
a no-op confirmation of the existing palBase the firmware loader had
already installed. With the bug it instead **wrote R31 = 0**, zeroing
palBase.

The DEBUG print in the leaf captured this exactly:

```
DEBUG: HW_MTPR HW_PAL_BASE pc=0x6005e0
  old=0x600000 -> new=0x0   cycle=4194406
  pre: R30=0x600000
  encoded=0x77fe1010  Ra=R31  Rb=R30  scbd=0x10  func=0x10
```

The print labels it correctly ("Rb=R30") — the bug is that the leaf
ignores Rb and uses Ra's read into `c.opA`.

---

## Phase map of the 178M cycle run

Working from a handful of strategic offsets in the trace:

| Cycle range | PC region | palMode | Phase |
| ----------- | --------- | ------- | ----- |
| 0 — 4,194,398 | `0x900xxx` → `0x6005c0` | 1 | SRM stub → PALcode bootstrap entry (JSR from 0x90041c → 0x6005c0) |
| 4,194,399 — 4,194,406 | `0x600600..0x6005e0` | 1 | **palBase corruption (cyc 4194406)** |
| 4,194,407 — ~5,000,000 | `0x6006xx..0x600920` | 1 | More PAL bootstrap; eventual HW_REI to kernel |
| ~5M — ~178M | `0x600920..0x600948`, `0x6009xx`, `0x6011xx`, `0x6020xx` | **0** | SRM kernel-mode work — decompression, string handling, console output. Long byte-copy loops (~11 cycles/byte). String "ailure, " observed in R16 at cyc 100M — diagnostic message rendering. |
| 178,096,444 | `0x6006fc` | 1 | HW_REI re-enters PAL mode (R26 = `0x600701`, bit 0 = palmode bit) |
| 178,096,445 — 178,096,493 | `0x600700..0x600798` | 1 | PAL subroutine (BSR to 0x6007d8, HW_REI return at 0x600808) |
| 178,096,494 | `0x60079c` | 1 | **HW_REI dispatches to PC `0x8000`** (R00 = `0x8001` set up the preceding BIS) |
| 178,096,495 — 178,096,496 | `0x008000..0x008004` | 1 | Tiny trampoline: BIS, BR → `0x012f40` |
| 178,096,497 — 178,096,515 | `0x012f40..0x012f88` | 1 | Kernel/OS PALcode installation. HW_MTPRs setting up its own IPR state. **Fault at `0x12f88` on `hw_mtpr Rx, scbd=0x5F`.** |

Two important observations:

1. **The path into `0x012xxx` is not palBase-relative.** HW_REI at
   `0x60079c` jumped to `0x8000` via either EXC_ADDR or R00 (the BIS at
   `0x600798` set R00 = `0x8001`, and `0x8001` matches the
   "PC=`0x8000`, palMode=1" semantics of HW_REI). The BR at `0x008004`
   to `0x012f40` is PC-relative. **We would have reached `0x012f88`
   regardless of palBase corruption.**

2. **The HW_MTPR at `0x012f50`** (cyc 178,096,501, encoded `0x77e11010`,
   Rb=R1=0x8000) is the kernel attempting **"set PAL_BASE := 0x8000"**
   — i.e. installing its own PALcode region. With the bug it wrote 0
   instead. With the fix, palBase would correctly become 0x8000.

---

## Auxiliary findings

### Both HW_MTPR HW_PAL_BASE events in the trace

The leaf's DEBUG print fires on every HW_PAL_BASE write. Boot output
captured exactly two:

| Cycle       | PC      | Encoded    | Ra/Rb       | R-Rb value | Result |
| ----------- | ------- | ---------- | ----------- | ---------- | ------ |
| 4,194,406   | 0x6005e0| 0x77fe1010 | R31 / R30   | 0x600000   | palBase = 0 (bug) |
| 178,096,501 | 0x012f50| 0x77e11010 | R31 / R1    | 0x8000     | palBase = 0 (bug) |

Both events have Ra=R31, both have a non-zero Rb. The bug silently
clobbers both writes.

### Mode-transition mechanics

The trace makes clear that V4's HW_REI dispatches to a PC encoded with
the palmode bit in bit[0] of the source. Three observed cases:

- cyc 178,096,444 PC=`0x6006fc` HW_REI, R26 = `0x600701` → next PC `0x600700`, palMode = 1
- cyc 178,096,489 PC=`0x600808` HW_REI, R6  = `0x60078d` → next PC `0x60078c`, palMode = 1
- cyc 178,096,494 PC=`0x60079c` HW_REI, R0  = `0x8001`   → next PC `0x008000`, palMode = 1

Worth verifying: the HW_MTPRs to HW_EXC_ADDR earlier in each sequence
are also affected by the same Ra/Rb bug. Yet the HW_REI dispatches
landed at non-zero targets. Possible explanations:
- V4's HW_REI uses a different IPR source (R26 / R0 directly?)
- The HW_MTPRs to EXC_ADDR happen to use an encoding where the bug is
  invisible (e.g., Ra and Rb both reference the same register)
- Some other path writes EXC_ADDR

This thread deserves a follow-up after the fix lands; for now it just
shows the bug's blast radius is larger than the two PAL_BASE writes.

### scbd 0x5F — secondary fault

`hw_mtpr Rx, 0x5f10` at the fault PC. scbd `0x5F` is not in `HW_IPR.h`.
Candidate identities:

- **PT31 (32nd PAL temp).** EV5/21164 has 32 PAL temps (PT0..PT31, scbd
  0x40..0x5F). EV6/21264 reduced this to 24 (PT0..PT23, scbd 0x40..0x57).
  The SRM .exe carries EV5-vintage code that addresses PT31. On real
  EV6 this would fault too — *unless* the loaded SRM is itself an EV5
  binary or has a custom PT31 handler.
- A vendor-internal CBox / scratch IPR that we just haven't enumerated.

The surrounding instructions in the probe (HW_M_CTL=0x28, HW_IER_CM=0x0B,
HW_SIRR=0x0C, HW_CC_CTL=0xC1) are unambiguously EV6 IPRs, so this is
EV6 code that happens to touch one EV5-only PAL_TEMP.

Recommended action: extend `HW_IPR.h` and `CpuState::palTemp` to 32
entries, treat scbd 0x40..0x5F uniformly as PT0..PT31. Cheap, harmless
to EV6 PALcode that only addresses PT0..PT23, and lets EV5-era code
execute.

---

## Proposed fix

Two coordinated edits:

### `grainFactoryLib/GrainMasterV4.tsv` line 457

```diff
- HW_MTPR  0x1D  0x0  HwMtpr  Hw  PalBox  S_HwFormat|S_Privileged|S_IprWrite|S_ReadsRa|S_ReadsInt  write internal processor register
+ HW_MTPR  0x1D  0x0  HwMtpr  Hw  PalBox  S_HwFormat|S_Privileged|S_IprWrite|S_ReadsRb|S_ReadsInt  write internal processor register
```

### `palBoxLib/grains/PalEntries.cpp::execHwMtpr`

Replace every read of `c.opA` with `c.opB` inside the `execHwMtpr` body.
The PAL_TEMP fast-path at line 316 and each switch arm that writes
through `c.opA` (HW_EXC_ADDR, HW_PAL_BASE, HW_I_CTL, HW_M_CTL, etc.) all
need the same swap.

Also update the encoding comment at lines 95-100 to correctly describe
the convention:

```diff
- //   bits[25:21]  Ra (target on MFPR, source on MTPR)
- //   bits[20:16]  Rb (unused for IPR access)
+ //   bits[25:21]  Ra (target on MFPR; R31/unused on MTPR per macro convention)
+ //   bits[20:16]  Rb (R31/unused on MFPR; source on MTPR per macro convention)
```

Regenerate generated dispatch tables (`genGrains.py` run) so the
S_ReadsRb flag flows through.

---

## Validation plan

1. **Unit test.** Add a doctest case in `tests/palBoxLib/test_palentries.cpp`:
   build a grain for `hw_mtpr R30, HW_PAL_BASE` with `encoded=0x77fe1010`,
   pre-set R30=0xCAFE, invoke leaf, assert `cpu.palBase == 0xCAFE`. The
   existing test machinery via `makeHwGrain` should handle the encoding.

2. **HW_MFPR symmetry check.** Pull HW_MFPR's encoding from the same
   macro file and write a round-trip test: write a known value via
   HW_MTPR, read back via HW_MFPR, assert equality. Catches future
   regressions.

3. **Re-run the SRM boot.** Expect palBase to remain at 0x600000 through
   the first phase, then transition to 0x8000 at cyc ~178M when the OS
   PALcode takes over. Should advance past `0x12f88` — at minimum
   getting further before the next unmapped IPR or instruction halts us.

4. **Predicted next fault.** Even with the bug fixed, scbd=0x5F at
   `0x12f88` is still unmapped. Either (a) extend the PAL_TEMP range to
   32 simultaneously, or (b) let it fault again and identify the next
   unhandled IPR. Recommend (a) — it's a 2-line change and unblocks an
   entire class of EV5-vintage PALcode sequences.

---

## Implementation order

1. Fix HW_MTPR (TSV + leaf + comment). One commit.
2. Add unit test for HW_MTPR + HW_MFPR roundtrip. Second commit.
3. Extend PAL_TEMP range to 32 entries (HW_IPR.h + CpuState.h
   `palTemp[24]` → `palTemp[32]`). Third commit.
4. Re-run boot. Observe whether SRM reaches `>>>` or surfaces the
   next gap. Capture trace; iterate.

Estimated work: ~30 min of edits + regen + build, then however long
the next boot takes to either succeed or trip a new fault.

---

## What this re-prioritizes

- **Snapshots (deferred):** still deferred until `>>>`. This finding
  doesn't change that.
- **MMIO instrumentation (Task #36):** lower priority now. The
  chipset routing is wired but not exercised heavily during this run;
  with the fix landed, real MMIO traffic will start once the OS
  PALcode initializes Tsunami CSRs.
- **EV5 profile (deferred):** the PT31 finding hints that an EV5
  profile would need to provision palTemp[32] from day one. Note for
  future Ev5EntryVectors.h sibling.
