# 2026-05-29 -- SRM Banner Emission Timeline

## Run identification

| Field | Value |
|-------|-------|
| Console log | `D:\EmulatR\traces\console_20260529-213420.log` (178 KB) |
| Retire trace | `D:\EmulatR\traces\20260529-213425_srm.trc` (7.0 GB) |
| Break trace | `D:\EmulatR\traces\20260529-213425_break.trc` (5.9 GB) |
| Auto snapshot at end | `snapshots\auto_*_200000000.axpsnap` |
| Cold boot start | 2026-05-29 21:34:25 wall (cyc 0) |
| Final cyc observed | 201,358,602 (run cancelled mid-cyc-200M wait) |
| Build | relwithdebinfo, kCcMultiplier=5594, SCBB rename live |

## Cycle vs wall-clock progression (from periodic snapshot timestamps)

The auto-snapshot subsystem writes every 10M cycles, giving a clean
fixed-cycle-per-row timeline.  Sustained pipeline rate held at
~76-77K cyc/sec on average for the V1 SRM phase, dipping to ~37K cyc/sec
inside the post-SCBB wait region.

| Cyc | Wall-clock | Notes |
|-----|------------|-------|
| 10,000,000  | 21:36:21 | |
| 20,000,000  | 21:38:34 | |
| 30,000,000  | 21:40:45 | |
| 40,000,000  | 21:42:55 | |
| 50,000,000  | 21:45:07 | |
| 60,000,000  | 21:47:18 | |
| 70,000,000  | 21:49:30 | |
| 80,000,000  | 21:51:39 | |
| 90,000,000  | 21:53:48 | |
| 100,000,000 | 21:55:58 | |
| 110,000,000 | 21:58:07 | |
| 120,000,000 | 22:00:17 | |
| 130,000,000 | 22:02:27 | |
| 140,000,000 | 22:04:38 | |
| 150,000,000 | 22:06:48 | |
| 160,000,000 | 22:09:01 | |
| 170,000,000 | 22:11:15 | |
| 180,000,000 | 22:13:26 | V1 SRM nearing OS PAL takeover |
| **186,786,248** | 22:14:55 | **OS PAL takeover** (palBase 0x600000 -> 0x8000); FAULT[0] op=0x1d at PC 0x13155 |
| **186,817,855** | 22:14:55 | First-ever UART LSR poll at port 0x3FD; returned 0x60 (THRE\|TEMT) |
| **189,164,805** | 22:15 | (Predicted) MTPR_SCBB silent -- intrinsic ran, no UNIMPLEMENTED |
| **189,564,697** | 22:15:50 | interval-timer divert[0] -- first Cchip timer fire post-banner |
| **189,612,016** | 22:15:51 | **BreakpointSink: break sink OPENED** at PC 0x1c6788 |
| 190,000,000 | 22:16:13 | Auto-snapshot (`auto_*_190000000.axpsnap` -- the predig anchor) |
| 200,000,000 | 22:25:19 | Auto-snapshot (boundary of post-banner wait pace) |
| ~201,358,602 | ~22:31 | Tim Ctrl-C |

## SRM banner stderr emissions -- cycle ranges

Banner bytes flow through `Uart16550::writeTHR` -> `fputc(value, stderr)`
as raw characters interleaved with V4 diagnostic lines.  Each table row
shows the line number in the console log, the SRM message exactly as
emitted, and the cycle range bracketing the emission (taken from the
nearest preceding and following `cyc=NNN` log tags in the stream).

| Log line | Cyc bracket | SRM stderr message |
|----------|-------------|--------------------|
| 1201 | 189,022,813 ... 189,043,999 | `*** keyboard not plugged in...` |
| 1205 | 189,044,280 ... 189,065,301 | `Flash ROM writes are disabled` |
| 1268 | 189,804,644 ... 189,847,242 | `00000001 exit status for from_init` |
| 1271 | 189,847,242 ... 189,948,321 | `no such file` |
| 1272 | 189,847,242 ... 189,948,321 | `file open failed for eerom` |
| 1274 | 189,887,168 ... 190,437,894 | First Intel-flash command burst (`UNHANDLED OUTER WRITE` events 0-5: 0x80 0x00 0x5B 0x20 0x15 0xC0) |
| 1287 | 189,948,321 ... 190,840,832 | `*** no timer interrupts on CPU 0 ***` |

**Important note on cycle precision:** each "bracket" is the bracket between
the V4 diagnostic *immediately before* the banner text and the V4 diagnostic
*immediately after*.  The actual SRM `writeTHR` retire that emitted the byte
sequence happened *inside* that bracket but is not separately timestamped
(the stderr mirror does not log per-byte cycles).  Bracket width = ~20K-1M
cycles depending on diagnostic density at the surrounding moment.

**For per-byte cycle precision** going forward: bump
`Uart16550::writeTHR` from raw `fputc` to a logged form like
`TX cyc=%llu pc=0x%llx byte=0x%02x ('%c')` -- one line per byte.  That
would let next-session traces resolve banner timing to the cycle.  Cost:
~6 stderr lines per banner character (negligible -- banner is ~50 chars
total, not in a hot path).

## Interval-timer divert events (first 10)

| # | Cyc | savedPc (user-mode PC at preemption) |
|---|-----|--------------------------------------|
| 0 | 189,564,697 | 0x1c699c |
| 1 | 189,792,256 | 0x59460  |
| 2 | 190,840,832 | 0x77150  |
| 3 | 191,889,609 | 0x1c6788 |
| 4 | 192,938,385 | 0x1c6788 |
| 5 | 193,986,618 | 0x1c6788 |
| 6 | 195,035,467 | 0x1c6788 |
| 7 | 196,083,995 | 0x1c6788 |
| 8 | 197,132,370 | 0x1c6788 |

Stable ~1.05M cycle cadence between fires.  After divert[2], savedPc
locks onto 0x1c6788 -- the post-banner wait point.

## Snapshot identifier convention for next session

Rename `snapshots\auto_*_190000000.axpsnap` to
`snapshots\predig_post_banner_190000000.axpsnap` (per
[[reference-predig-snapshot]]).  The `predig_` prefix marks it
non-pruneable and makes autoload pick it as newest on every cold boot.
Resumption from this anchor skips the ~33 min V1 SRM replay and lands
straight at the banner-emission frontier.

## Three blockers between banner and `>>>`

Captured as tasks #78 / #79 / #80; not hard halts (run continued past
each):

1. `*** no timer interrupts on CPU 0 ***` -- self-test failure despite
   our interval timer firing.  Highest-leverage next investigation.
2. `file open failed for eerom` -- NVRAM env-store missing.  Cosmetic.
3. UNHANDLED OUTER WRITE at PA 0xffff0000-1 -- Intel flash command
   probing; log noise only.
