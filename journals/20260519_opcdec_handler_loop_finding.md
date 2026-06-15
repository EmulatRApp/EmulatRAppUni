# The 0x6003ec "spin loop" is the OPCDEC handler -- 2026-05-19 12:15

Third reframing of the spin-loop investigation today.  Trace evidence
from `D:\EmulatR\traces\20260519-121108_srm.trc` (cyc 0..5.3M, 1.6 GB).

## What we now know -- the full chain

```
cyc 4,194,467  pc=0x6006dc  HW_REI    pal=1->0    PAL pass-1 done, enters native
cyc 4.19M..5.24M             native SRM Console code runs (1.05M cycles)
cyc 5,242,879  pc=0x6010d0  LDA      pal=0       R16 := 0x602040  (CALL_PAL arg setup)
cyc 5,242,880  pc=0x600100  CALL_PAL pal=1       exc=0x600105 (= return PC | PAL_bit)
cyc 5,242,881  pc=0x602a40  BLT      pal=1       PAL handler BRs into data territory
cyc 5,242,882  pc=0x602a44  OPCDEC   pal=1       FAULT (bytes are data, not code)
cyc 5,242,883  pc=0x600400  BLT      pal=1       OPCDEC trap entry = palBase + 0x400
cyc 5,242,884  pc=0x600404  ADDQ     pal=1       R00 += 8 -> 0x5e7d5a
cyc 5,242,885  pc=0x600408  BNE      pal=1       loops to 0x6003ec
cyc 5,242,886  pc=0x6003ec  HW_LD    pal=1       PREDIG fires (first execution of "spin")
```

## What "the spin loop" actually is

The bytes at 0x6003ec..0x600408 are **the firmware's OPCDEC trap handler**.
palBase + 0x400 on EV6 maps to OPCDEC -- standard Alpha 21264 PALcode
entry-vector table.  We landed there CORRECTLY when OPCDEC fired at
0x602a44; V4's trap dispatch is doing its job.

The handler's body does HW_LD / LDA / SUBQ / HW_ST / ADDQ / BNE.  It
walks pointers R0 (dst) and R2 (src) by +8 each iter and decrements
R1 by 8.  It loops on `BNE R1, -32` -- never exits because R1's
initial state is wrong.

R1's initial state is wrong because **the OPCDEC handler inherited
R1 = 0x5e7d55 from the native code that did the CALL_PAL** -- not
from any proper setup the handler itself would do.

## Why the OPCDEC fires (root cause)

The native code at PC 0x6010d4 (just after the LDA at 0x6010d0) is
a CALL_PAL.  CALL_PAL dispatched to palBase + 0x100 = 0x600100.
That's the **INTERRUPT** vector on EV6, NOT a CALL_PAL entry.

Standard Alpha CALL_PAL dispatch:
-   func 0x00..0x3F (privileged): palBase + 0x2000 + 64 * func
-   func 0x80..0xBF (unprivileged): palBase + 0x1000 + 64 * (func - 0x80)

palBase + 0x100 isn't on either curve.  Either:
1.  V4 has a bug routing CALL_PAL to 0x600100 regardless of func code.
2.  The firmware uses an unusual CALL_PAL convention where 0x600100
    is in fact a valid entry (perhaps via the PAL routing table at
    palBase + something).

The PAL code at 0x600100 immediately branches to 0x602a40.  Bytes
there decode as garbage (real disassembly: BLT with +2.5M disp, OPC26
which is STS, HW_ST with bizarre disp, BNE with -670K disp).  The
firmware's PAL code branched into a DATA TABLE, hit an STS that V4
doesn't have, OPCDEC fired, OPCDEC handler at 0x600400 inherited
broken register state and spins.

## What was previously misdiagnosed (today)

1.  "HW_MTPR loop at 0x6003ec"  -- WRONG.  Trace mnemonic was
    mis-classified primary opcode 0x1b (HW_LD).
2.  "Decompressor pass-2 memcpy at 0x6003ec overwriting PAL text"
    -- PARTIAL.  The loop DOES walk R0 into PAL text region, but
    it's not the decompressor -- it's the OPCDEC handler.  The
    overlap with PAL text is collateral, not the root cause.
3.  "Need to implement STS opcode 0x26" -- WRONG.  The firmware
    didn't intend to execute STS; it hit it by branching into a
    data table.

## Three open prongs

1.  Get the CALL_PAL encoding at PA 0x6010d4 to know which func
    code the firmware used.  `--dump-disasm 0x6010c0:12 --max-cycles 1`
    will show this.
2.  Trace V4's CALL_PAL dispatch path.  `coreLib/palBoxLib` or
    similar.  Find where the func code gets translated to an entry
    PC and confirm whether 0x600100 is what we produce.
3.  Inspect bytes at 0x600100..0x6001FC.  If those contain a real
    INTERRUPT handler (HW_LD IPR / HW_REI), then 0x600100 isn't
    meant to be called via CALL_PAL -- the firmware took a wrong
    branch from native code, or V4 mis-routed.  If those bytes
    contain a real CALL_PAL handler that branches via a dispatch
    table to per-func handlers, then the dispatch table's index
    is wrong.

## Operational signals validated

-   Step D PAL relocation fires correctly at cyc 4,194,404.
-   HW_REI from PAL pass-1 to native at cyc 4,194,467.
-   1.05M cycles of native SRM Console execution before the first
    CALL_PAL.
-   OPCDEC trap dispatch lands at palBase + 0x400 (correct).
-   Predig snapshot mechanism works at cyc 5,242,886.

## How to apply

Future Claude should not pursue "implement STS" or "fix memcpy
destination overlap with PAL text" as primary tracks -- both are
downstream of the CALL_PAL routing or PAL dispatch table bug.
Focus on the CALL_PAL dispatch path in V4 and the bytes at
0x600100..0x6001FC.

A condensed version of this note should land in auto-memory as
`project_opcdec_handler_loop_finding.md` to supersede the prior
`project_decompressor_pal_overlap_confirmed.md` (which had the
correct evidence but the wrong interpretation of what the loop is).
