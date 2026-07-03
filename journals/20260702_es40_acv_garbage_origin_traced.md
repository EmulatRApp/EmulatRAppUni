# ES40 ACV — Garbage-R16 Origin Traced to a Memory-Fill Overshoot (ALU + CSERVE 0x66 exonerated)

**Date:** 2026-07-02
**Model:** ES40 (Tsunami 21272), `firmware/es40_v7_3.exe` (SRM V7.3), `--mem 4 GiB`
**Status:** root chain of the `kFaultAcv` loop resolved to a **semantic / memory-layout**
issue. The ALU and CSERVE 0x66 are proven NOT the cause. Live lead: the configured
memory size / HWRPB memory descriptor (region layout).
**Predecessors:** `20260702_es40_boot_blocker_analysis.md` (SIGSEGV fix, MMU reframe,
PTBR/PT2 audit, DTB_PTE PFN fix).

---

## TL;DR

The ES40 ACV loop dereferences a garbage VA `R16 = 0xFFFFFFFF7F827F5F`. Tracing it:

1. `R16` is a **copy of `R00`**, and `R00` is written by **`SUBQ R02, R00, R00` @ pc `0x8c308`**.
2. `SUBQ` is **correct**: inputs `R02 = 0x3fc12000` (~1 GB) and `R00(before) = 0xC03EA0A1`
   (~3.2 GB) are **both clean** (no `0xFFFFFFFF` half); `0x3fc12000 − 0xC03EA0A1 =
   −0x807D80A1 = 0xFFFFFFFF7F827F5F` — a **legitimate negative**. **The ALU is not the bug.**
3. `R00 = 0xC03EA0A1` is **not** stale and **not** set by CSERVE — it is the **end pointer
   of a count-based memory-fill loop** (`@0x5afac`), which marched (by 8, unaligned) up to
   ~3.2 GB. **CSERVE 0x66 is not the cause either** (it is called nearby but does not set R00).
4. So the garbage is a **two-region delta**: a ~1 GB value minus a ~3.2 GB scan-end pointer,
   legitimately negative, then **mis-used as an address** → ACV. The ~3 GB scan region exists
   **because memory is 4 GB**. Root is **upstream memory-size / region layout** (HWRPB MEMDSC),
   not arithmetic. (Tim's 4 GB / 32 GB thread, now concrete.)

---

## The evidence chain (all captured at the 282M fault, post the DTB_PTE PFN fix)

### Fault site
`ACV @ pc 0x1b7dd4` : `LDQ R0, 0(R16)`, `R16 = 0xFFFFFFFF7F827F5F`, `mode=Kernel`, ~1.8M-cyc
loop from cyc 282,091,103. The loop increments `R16 += 8` each iteration.

### R16's writer (coordinator's capture, confirmed)
`R16` is a straight **copy** of `R00` (`BIS R31,R00,R16 @ 0x5b03c`). So chase `R00`.

### R00's writer — value-gate ring dump on `0xFFFFFFFF7F827F5F`
```
0x8c2f0  BIS R31,R16,R02   -> R02 = 0x000000003fc12000   (clean, ~1 GB)
0x8c2f8  BIS R31,#0x66,R16 -> R16 = 0x66                 (CSERVE func arg)
0x1b78f8 CSERVE 0x9        -> CSERVE func 0x66  (EmulatR NO-OP)
0x8c308  SUBQ R02,R00,R00  -> R00 = 0xFFFFFFFF7F827F5F
```
`SUBQ R02 − R00`. `R02 = 0x3fc12000` clean. `R00(before)` reconstructs to `0xC03EA0A1`
(`R02 − result mod 2^64`). Neither operand carries a `0xFFFFFFFF` half → the negative is a
**true 64-bit result**, not a sign-extension artifact.

### Independent ALU disproof (value-only)
`0xFFFFFFFF**7F**827F5F` has **bit 31 = 0**. A SUBQ-running-the-SEXT-path bug would compute
`SEXT((R02−R00)<31:0>)` = `0x00000000_7F827F5F` (high half **zero**) — it **cannot** produce a
`0xFFFFFFFF` high half from a bit-31-clear low half. Corroborated by code: `eBoxLib/grains/
IntArith.cpp` `execAddq/execSubq` are pure `opA±opB` (full 64-bit, no SEXT); scaled Q-variants
`(opA<<n)±opB`; only the L-variants sext; dispatch `GrainMasterV4(3).tsv:151` maps
`SUBQ 0x10/0x29` to the 64-bit grain (not the L one); operands arrive un-truncated
(`execAddl` casts them *down* to int32). **ALU is well-formed.**

### R00's origin — value-gate ring dump on `0x00000000C03EA0A1`
```
0x5afa8  ADDQ   R01,#1,R01      ; iteration counter++
0x5afac  STQ    R16, 0(R00)     ; the UNALIGN-FIXUP store (R00 == 1 mod 8, UNALIGNED)
0x5afb0  CMPULT R01, R09, R18   ; loop while R01 < R09  (COUNT-based, not address-based)
0x5afb4  LDA    R00, 0x8(R00)   ; R00 += 8
0x5afb8  BNE    R18, -20        ; loop
```
`R00` is the fill **store pointer**, unaligned (`…89, …91, …99, …a1`, all ≡ 1 mod 8 — the
source of the `UNALIGN-FIXUP` at `0x5afac`), advancing to `0xC03EA0A1` (~3.2 GB, in the
`0xC0000000`=3 GB region) after ~`0x17FF` iterations. So `R00` is a **legitimate scan-end
pointer**, NOT stale and NOT from CSERVE.

---

## Conclusions

- **ALU: exonerated** (two independent proofs). Do NOT modify the Q-variant arithmetic.
- **CSERVE 0x66: exonerated** as the R00 source (R00 comes from the fill loop). The earlier
  "0x66 is undefined, no-op faithful" stands for R00; 0x66 remains a benign no-op here.
- **Root is upstream/semantic:** `SUBQ` subtracts a **3 GB** scan pointer from a **1 GB** value
  → negative → used as an address → ACV. The 3 GB region exists because `--mem 4 GiB`.

### Two leads (ranked)
1. **Count-driven overshoot.** The fill loop is `while R01 < R09` (count `R09`), not
   address-bounded. If `R09` derives from a memory-size / MEMDSC field too large for this
   region's intended extent, `R00` overshoots to 3 GB → the subtract goes negative.
2. **Region-base mismatch.** `R02` (~1 GB, `0x3fc12000` — the earlier UNALIGN va) and the fill
   base (~3 GB) should be in the **same** region; EmulatR's memory map (HWRPB MEMDSC @ PA
   `0x2840`) may place them apart, so a delta that should be small/positive is huge/negative.

Both point at the **HWRPB memory descriptor / configured-memory layout** — exactly the region
the project's HWRPB-fidelity work targets (`memory.md` "active frontier: HWRPB").

### Next step
Trace the origins of **`R09`** (the fill count) and **`R02`** (`0x3fc12000`) — where do they
come from? Watch for a **memory-size / top-of-memory / MEMDSC** field as the ultimate source.
If `R09` (or the region base) is derived from a 4 GB memory size where the SRM expects a smaller
per-region extent, that mismatch is the fix site (memory descriptor / region layout), not any
CPU-core code.

---

## Tooling landed this pass

- **`feat(trace): value-triggered lookback-ring dump`** (commit `571ea05`,
  `traceLib/DecListingSink.{h,cpp}`): `setValueGate(v)` / env `EMULATR_VALUE_GATE=0x..`. When a
  retire writes that exact value to its destination register, dump the 16-deep lookback ring
  once (pc/mnem/operands/result). `0` = disabled (zero cost). Works under
  `EMULATR_TRACE_WINDOW=1` (no `--trace` needed; mask clear → fast). This is the tool that
  cracked the origin. Reusable "where did this value come from" primitive.
- **DTB_PTE PFN fix** (`0636463`): DTB/ITB PFN masked to `PA[43:13]` (31 bits) per HRM Fig
  5-9/5-27. Correct, but inert for this ACV (offending PTE has bit 63 = 0).

### Gotchas recorded (for reuse)
- `DecListingSink::onCommit` is silent without a trace channel; arm with `EMULATR_TRACE_WINDOW=1`
  (mask stays clear → fast) to activate it for value probes.
- `BreakpointSink` (`EMULATR_GATE=open:,close:,rev:`) needs an **absolute**
  `EMULATR_RETIRE_TRACE_DIR` (relative path → `root_path()` empty → sink silently DISABLED).

### Suggested regression guard (follow-up)
Add a unit test asserting `SUBQ(0x000000007f827f5f, 0x0000000080000000)` stays full 64-bit
(no SEXT; the `0x…7F…` low-half stays, no `0xFFFFFFFF` fabricated) — locks in the ALU while the
real fix lands upstream in the memory descriptor / region layout.
