# DS20 boot stall — ROOT CAUSE: the 0xBFFC/0xCAFEBEEF ISP-model flag (2026-06-18)

> **CORRECTION (added same day, after finding the apisrm source).** The body
> below calls `0xBFFC == 0xCAFEBEEF` a "secondary-CPU liveness rendezvous." That
> was wrong. The firmware source `apisrm/ref/pc264.c` `platform()` shows it is
> the **ISP-MODEL (pre-silicon simulator) detection flag**:
> `if (*(int*)0xBFFC == 0xCAFEBEEF) platform = ISP_MODEL; else REAL_HW;`
> (comment: "ISP flag at 0xBFFC … deposit 0xBFFC with CAFEBEEF"). EmulatR **is**
> an ISP model, so claiming `ISP_MODEL` is the correct, faithful wall-1 fix (it
> makes the firmware take simulator paths and skip real-HW steps EmulatR doesn't
> model). It is why the console printed "Running on the ISP model." AXPBox does
> NOT set it (it boots the REAL_HW path). The *separate* `cpu_enabled` env var
> gates SMP secondary startup (wall-2 layer). The diagnostic chain below (how the
> poll/loop was located) is still accurate; only the *interpretation* of the flag
> changed. See `project_ds20_console_idle_secondary_cpu_stall` for the corrected
> conclusion.

---

## (original write-up — interpretation since corrected; read the banner above)


## TL;DR

The DS20 SRM boot stalls right after `Flash ROM writes are disabled` because the
boot CPU **busy-polls physical address `0xBFFC` for the magic value
`0xCAFEBEEF`** — a **secondary-CPU "I'm alive" rendezvous**. V4 models only CPU0,
so the flag is never written and the poll spins forever. Satisfying the read
(`EMULATR_CPU1_ALIVE=1` → bus read of PA `0xBFFC` returns `0xCAFEBEEF`) pushed the
firmware **past the wall** — new console line `Running on the ISP model.` and PC
advanced from the old `0x1ad9xx` stall into new code (`0x78fxx`). **Proven.**

The user's turn-1 instinct (dual-CPU; "make CPU1 report ready") was correct. The
IPI work earlier this session is the *start* half of the SMP rendezvous (correct,
kept); this `0xBFFC` flag is the *liveness* half that actually gated boot.

## How it was found

Passive observation (snapshots, the MaxCycles state dump, an empty gated trace)
kept showing the idle/timer_check loop, never the block — because the polling
thread looked like "idle". The break came from the user's full Ghidra linear
export (`ghidra/ghirda_decompressed.txt`, 2.18M lines) + the MaxCycles register
dump:

1. Stop dump: PC `0x1ad934` in `FUN_001ad928`, native, `R26=0x7da7c`
   (timer_check return), `R20=0x801FC000000` = `kPchip0_IODense` (PCI I/O base).
2. `FUN_001ad928` decodes to `t8=a0; CALL_PAL 0x9d; STQ v0,[t8]; RET`. CALL_PAL
   `0x9d` = **RSCC** (`PAL_FUNC__RSCC=^x9D`, VMS PAL) — it reads the cycle
   counter. So timer_check is a **busy-delay spin**, not a sleep/yield: the
   thread is *running and polling*, not descheduled.
3. timer_check (`FUN_0007d9d8`) calls the status check `FUN_0007ecb0`:
   ```
   v0 = *(int32 *)0xBFFC          ; addr: LDAH 0x1 / LDA -0x4004 -> 0xBFFC
   t0 = 0xCAFEBEEF                 ; LDAH -0x3501 / LDA -0x4111 -> 0xCAFEBEEF
   return (v0 == 0xCAFEBEEF) ? 1 : 2
   ```
4. `0xCAFEBEEF` has **no writer anywhere in the image** and `0xBFFC` is the only
   read site → the writer is the unmodeled CPU1's startup path. DS10 (single
   socket) never does this rendezvous → reaches `>>>`.

## The confirmation fix (hack)

`pipelineLib/MemDrainer.h` `applyLoadEffect`, right at `br = bus.read(pa, …)`:
env-gated (`EMULATR_CPU1_ALIVE`), if `pa == 0xBFFC` set `br.data = 0xCAFEBEEF`
(LDL sign-extends to match the firmware's constant). Off by default.

Run: `EMULATR_CPU1_ALIVE=1 ./run_fw.sh ds20 cold`.

RESULT: new output `Running on the ISP model.`; PC advanced to `0x78fxx` (cyc 1B,
a bounded retry/countdown loop `LDL t0,[t2+0x10]; SUBL t0,1; STL; BGE` calling
`FUN_001ad610/754`). The pre-fix wall is gone.

## What was ruled out on the way (don't re-litigate)

- **IPI** (Cchip IPREQ→IPINTR→b_irq<3>): wired this session and verified against
  the PC264 OSF PAL (cause bit EI[3]=1<<36, gate canAcceptInterrupt(21)). Correct
  and kept, but NOT this blocker — the re-run on a clean build didn't advance.
- **IIC/FRU manifest asymmetry**: ds10 & ds20 IIC device lists are identical.
- **TICKWARP/RSCCWARP** (`EMULATR_RSCCWARP`): targets the later `0x7c3xx` delays;
  DS20 stalled upstream. DS10 advanced to cyc 2.857B with it irrelevant.
- **Static string xref** (GP-relative, no Ghidra GP context) and **snapshot
  guest-mem read** (`CpuState` embeds `SPAMShardManager` TLBs → opaque sizeof)
  both walled out — which is why the linear disasm export was decisive.

## Next

1. Let DS20 keep running; watch PuTTY for `eerom`/memory/`Testing` POST and the
   next wall (another device/rendezvous poll, or eventually the VAX-float
   opcode-0x15 FP path).
2. Replace the read-hook with a **faithful secondary rendezvous**: write
   `0xCAFEBEEF` to `0xBFFC` once at the real secondary-start point (the
   "init-only pseudo-CPU" slot), so it arms without the env var and models the
   actual handshake. The current hook fires for ANY `0xBFFC` read — fine to
   confirm, not the final form.
3. Commit the real wins (Cchip IPI wiring; the rendezvous fix once faithful).

## Pointers

- Memory: `project_ds20_console_idle_secondary_cpu_stall` (full chain + CONFIRMED).
- Disasm: `ghidra/ghirda_decompressed.txt` (grep-able linear export).
- Earlier journals: `20260618_ds20_first_boot_console_idle.md`,
  `20260618_cchip_ipi_wiring_design.md`.
