# ES40 -- Session Fixes Work; New Frontier = `0x629f0`/`0xa8xxx` Loop (2026-07-05)

## Result of the ES40 boot test

Run on branch `es40/reserved-ipr-0x2d` (the `0x2d` fix + spin-skip + capability;
unalign reverted uncommitted), model=ES40, firmware `es40_v7_3.exe`, `--mem 4 GiB`,
`EMULATR_SPINSKIP=1`, console TCP 10026.

**The session's fixes work.** ES40 no longer hangs at the old `0x1b7dd4` ACV:
- The `0x2d` reserved-IPR fix cleared the `kFaultUnimplemented` -> DtbMissDouble cascade.
- Spin-skip collapsed the ~990M-cycle hardcoded delay sequence.
- ES40 streamed all the way into the native SRM console code region (`0x62xxx`), far
  past where it used to stop.

**But ES40 does NOT reach `>>>`.** It stalls in a non-terminating loop, orbiting
several code regions without advancing and WITHOUT ever initializing COM1:

```
caller region : 0x628ec / 0x62df8 / 0x62954 / 0x6b138 / 0x8c21c   (ra = 0x629f0)
callee region : 0xa87bc / 0xa8640 / 0xa88e8 / 0xa8638 / 0xae2xx
```

`~0x629f0` calls the `0xa8xxx` routine, it returns, and the cycle repeats -- from
cyc ~1.49B through 2.88B (>1.3B cycles) with no forward progress and no
`Uart16550[COM1]: first MCR write` (so PuTTY shows nothing -- the SRM never reaches
console-UART init). 64 faults (the bounded DtbMissDouble burst, self-resolved);
9 spin-skip loud refusals (the memory-scan loops).

Spin-skip **correctly refuses** this loop (`body has memory/CSR/IPR` -- it's a live
device-poll/wait, structurally like the DS10 `0x13d34` poll, NOT a pure countdown).
So it is a real frontier surfaced, not warped past.

## Platform scorecard (2026-07-05)

| Platform | Build | Result |
|----------|-------|--------|
| DS10 | pre-regression `5c306d6` | boots to `>>>` (PuTTY-confirmed) |
| DS20 | pre-regression `5c306d6` | boots to `>>>` (PuTTY-confirmed) |
| ES40 | branch (0x2d fix + spin-skip) | clears ACV+delays -> **stalls at `0x629f0`/`0xa8xxx` loop**, no `>>>` |

The platform-divergence remains: NO single build boots all three today
(DS10/DS20 need the pre-`0x2d` behavior; ES40 needs the `0x2d` fix). This is why
`0x2d` must be capability-scoped -- see
`journals/20260705_ds10_regression_and_open_tasks.md`.

## Open (ES40)

1. **Characterize the `0x629f0`/`0xa8xxx` loop** -- oracle-disassemble `0xa8xxx`
   (base = PC - 0x8000 for the es40 decompressed image, [[host-decompressor-oracle]]),
   identify the CSR/device it polls and the exit condition it never sees (same method
   that cracked the DS10 `0x13d34` poll). Very likely an unmodeled device-ready bit.
2. **Confirm whether it is memory-size dependent** -- this run was 4 GiB.
3. Everything downstream of reaching `>>>` (see the DS10 journal's open-task list).

## MEMO TO SELF: test ES40 at 32 GB memory

ES40 = Typhoon (8 GB arrays, ASIZ 0xA, up to 32 GB). This test used 4 GiB. **ES40
should be tested at 32 GB memory.** Per the standing constraint, 32 GB profiles run
on the **Windows PC** (132 GB host RAM); the Intel MacBook is memory-limited. Retest
the `0x629f0` frontier at 32 GB -- it may be memory-size dependent (the loop could be
a memory-config/sizing wait), or it may be memory-independent (a pure device poll),
and that changes the fix. Cross-ref [[es40-4gb-sizing-channels-clean]] (AAR/MEMDSC/NXM
verified clean at 4 GB) -- the 32 GB path exercises the extended-AAR (ASIZ 0xA) tiling
that 4 GB does not.
