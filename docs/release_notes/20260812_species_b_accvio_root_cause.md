# Release Note — OpenVMS STARTUP ACCVIO ("Species B"): Findings and Remediation

**Date:** 2026-08-12 (revised same evening after verdict3)
**Platform:** DS20 (Tsunami/EV6), firmware `ds20_v7_3`, OpenVMS Alpha V8.3
**Tree:** V5 (`v5-tb`), commits `35e4372` (instrument suite), `75fc273` (fully-associative TLB + diagnostics)
**Investigation record:** `journals/20260809_JRN-SUPMODE-001_dcl_supervisor_transition.md` secs 20–36

---

## Summary — TWO INDEPENDENT DEFECTS

The investigation of the reproducible `%SYSTEM-F-ACCVIO` pair that
terminates OpenVMS STARTUP uncovered **two separate defects sharing one
crime scene**:

**Finding 1 — TDF disk poisoning (FIXED, verified).** The persisted
Time Differential Factor on the system disk reads −46800 s (−13:00, an
impossible timezone) where it should read −18000 s (−5:00 EST). The
value lives as a **binary tick-count cell inside the on-disk boot-image
parameter data** (the mechanism VMS uses to persist the TDF for early
boot), present on every disk of this lineage including the "pristine"
baseline; string forms found elsewhere are derived copies. A 12-site
in-place patch (−468e9 → −180e9 ticks) was applied and **verified: the
subsequent boot built the correct `"-18000"` string** through the whole
processing chain for the first time in the investigation. The original
poisoning event is historical (candidate territory: an earlier-era
emulator time-initialization defect; see Open Items).

**Finding 2 — the ACCVIO itself is DATA-INDEPENDENT (OPEN,
beta-blocking).** With the corrected value in place, **the identical
fault pair fired anyway**, ~24 ms after `%STDRV-I-STARTUP`, same PCs,
same signature. The crash reproduces across every configuration tested
(aged/pristine/patched disk × poisoned/correct TDF × sharded/faithful
TLB × wall-clock hours × ~50× speed regimes) and does **not** reproduce
on Charon-ES40 or PersonalAlpha-DS20 with the same disk. It is a live
EmulatR execution defect ("B2") in the DCL/descriptor path. The
timezone value was a traceable fellow traveler, not the cause.

The emulator's translation, arithmetic, conversion, and copy machinery
were exonerated by instrumented capture at every stage.

## Symptom

Approximately 25 ms of console time after `%STDRV-I-STARTUP`, two access
violations in the STARTUP process, byte-stable across every boot,
wall-clock time, and emulator configuration tested:

```
%SYSTEM-F-ACCVIO, access violation, reason mask=04, virtual address=000000002453xxxx
%SYSTEM-F-ACCVIO, access violation, reason mask=00, virtual address=00000000006C
```

### Register dump at the primary fault

```
PC  = 00000000000638C4        PS  = 0000001B
Faulting VA = 0000000024535953 = ASCII "SYS$"     (text dereferenced as a pointer)
R17 = 000030303836342D        = ASCII "-46800"
R28 = 000030303836342D        = ASCII "-46800"
R19 = 0000000024535953        = the faulting address (string-as-pointer)
R18 = 000000007AE2B2E8        = destination buffer (stable across runs)
R3,R10 = 7AE2B238   R4 = 7AE2B160   R7 = 7AE2B250   SP = 7AE2B120
```

The fault site is OTS$MOVE's inner store loop (LIBOTS), called with a
destination argument holding string *content* rather than an address —
the caller (DCL, image region `7AF0A000–7AFDC5FF`) built the argument
one indirection short. (Post-verdict3 note: the specific string cargo
varies with the TDF value; the fault does not.)

### Register dump at the cascade fault (`va=0x6C`, correct-data boot)

```
PC  = 0000000000044050        PS  = 300000000000001B
R16 = 0 R17 = 0 R27 = 0 R28 = 0            (NULL structure/descriptor)
R19 = R20 = R26 = 0000020000000000         (bit 41 alone — including the
                                            RETURN-ADDRESS register R26)
SP = FP = 000000007AE412B0
```

A read at offset `0x6C` off a NULL pointer inside the condition-handling
path, with a non-canonical single-bit constant (`2^41`) sitting in the
procedure linkage — **the current best machine-level fingerprint of the
B2 corruption** (greppable/value-gateable; no such value exists in
legitimate linkage).

## Root Cause

**The corruption is data-at-rest, not a live computation.** On the
system disk (`dka0.vdisk`, since preserved as
`dka0_aged_20260812.vdisk`), the ASCII string `-46800` is stored as the
value of `SYS$TIMEZONE_DIFFERENTIAL` in SYSINIT's system-logical
definition blob, in both system-root copies:

| Site | Disk byte offset | LBN + offset | Pairing |
|---|---|---|---|
| 1 | `0x224745D9` | 1123234 +0x1D9 | `SYS$TIMEZONE_DIFFERENTIAL` name followed by `-46800` |
| 2 | `0x224BC2D3` | 1123809 +0xD3 | `-46800` adjacent to binary delta `FFFFFF93.0906B800` |

The binary form, −468,000,000,000 × 100 ns ticks = **−13 hours
exactly** (correct: −180e9 ticks = −5 h), also appears in ten
crash-dump/pagefile sediment locations (memory images of past poisoned
boots) which are inert as boot inputs.

At every boot, SYSINIT ("defining system logical names") loads the
poisoned definition; the value then flows:

```
SYSINIT logical "-46800"
  → TDF image loads binary delta from static data      (P0 0x22234)
  → software divide by 10^7 (PC 0xB2798, verified correct)
  → negate (PC 0xB29D8): first register holding -46800
  → first store of binary value (user PC 0x49644)
  → binary→ASCII conversion on stack (0x7B02F8EA)
  → OTS$MOVE copy to TDF buffer 0x22210
      (S0 PC 0xFFFFFFFF80209494/98 longword loop, ...94D8 byte tail)
  → $SETIME-class service consumes user descriptor
      (dsc$a_pointer deref at 0xFFFFFFFF802086B0)
  → OTS$MOVE into exec tz struct at S0 0x82D95FA8 (PA 0x0464BFA8)
  → post-STDRV copy to user buffer 0x7AE2B2E8
  → DCL descriptor confusion → ACCVIO at PC 0x638C4
```

Every stage was captured live by retire-time instrumentation (VA/value
watches, one-shot lookback-ring dumps, retire windows) and found to
process its input faithfully; the input itself was poisoned.

The −13 h = −5 h + (−8 h) signature indicates the original poisoning
event derived a TDF from two time sources 8 hours apart (consistent
with a host-timezone or time-of-day reconciliation defect in an earlier
emulator era); the writing event predates all preserved disk baselines
and could not be dated from surviving evidence.

## Ruled Out (by experiment)

- **TLB / translation.** Reproduced byte-identically before and after
  reshaping the ITB/DTB from sharded `<2,64>` to the HRM-faithful
  128-entry fully-associative organization (which shifted boot timing
  ~17%). Full-ways alias scans found no divergent-PTE aliases. PA
  composition for granularity-hint blocks verified correct.
- **Arithmetic/conversion.** The divide, negate, and formatting were
  captured instruction-by-instruction and are exact.
- **Timing.** Value byte-stable across runs at four different
  wall-clock hours and two guest speed regimes (~50× apart).
- **On-disk sources of the string in RMS-visible files.** The only
  timezone data file (`DTSS$TIMEZONE_DIFFERENTIAL.DAT`) is correct
  (`1 -18000 EST 0 EST5`).

## Remediation

1. **Immediate:** boot from the pristine image (healthy `-18000` at the
   same blob sites, zero `-46800` anywhere) — verification boot in
   progress at time of writing.
2. **Aged disk repair:** six-byte in-place patch `-46800` → `-18000` at
   the two sites above (same length, no filesystem structure affected).
   Backups preserved: `dka0_aged_20260812.vdisk`,
   `dka0-prepatched.vdisk`, `dka0_prestine.vdisk`.
3. `UTC$TIME_SETUP.COM` alone is **insufficient** — verified: it
   rewrites the timezone rule files but not the SYSINIT logical blob.

## Implication for Beta: Migrated System Disks (Charon / vtAlpha)

Beta users are expected to bring **existing system disks** from Charon
and Vere (vtAlpha) installations. Those disks carry years of
legal-but-arbitrary persisted state — including TDFs anywhere in the
±13/+14-hour range, e.g. a New Zealand disk legitimately stores +13:00,
the numeric mirror of the value that crashes us today. Tonight's
central lesson therefore generalizes:

> **A disk that boots on Charon must boot on EmulatR.** Data-dependent
> divergence on legal input is an EmulatR defect by definition
> (EmulatR-as-Oracle discipline), regardless of how unusual the input.

Consequences:

1. **B2 (below) is beta-blocking.** It is precisely a case of legal
   input crashing EmulatR while reference implementations proceed.
2. A host-side **disk pre-flight scanner** (generalizing tonight's
   byte-level TDF audit) is worth productizing for the beta kit: scan a
   migrated `.vdisk` for known-risk state and report before first boot.
3. Migration guidance for the beta notes: after first boot, verify
   `SHOW LOGICAL SYS$TIMEZONE_DIFFERENTIAL` matches the site's real
   offset; a wrong value indicates imported state, not an EmulatR
   defect — but a *crash* on any value is ours.

## Open Items (tracked separately)

- **B2 — same-input divergence (live emulator defect, BETA-BLOCKING).**
  Charon-ES40 and PersonalAlpha-DS20 boot the *same poisoned disk* to
  completion: they set a wrong-but-legal TDF (−13:00) and continue.
  EmulatR alone ACCVIOs while processing the identical input — and any
  migrated beta disk with an unusual (or merely eastern-hemisphere)
  TDF may walk the same path. Controlled reproducer: patch `-46800`
  into a pristine copy; suspect list opens with the DCL
  unaligned-quadword cluster (`7AFA6B18–7AFA6BF0`) over the descriptor
  buffer under the silent unaligned-fixup regime
  (`unalignTrapEnabled=false`, 798 fixups/boot, zero page-crossers).
- **Crash-dump path wedge.** The VMS dump writer's primitive (polled)
  driver never issues a SCSI WRITE under EmulatR (zero host writes over
  4 h slow / 3.4G cycles fast; dump-file sizing exonerated by
  experiment). Console-callback (`cfw_start` divert) service is the
  lead. SDA on EmulatR-produced dumps is unavailable until fixed.
- **TOY time-write gap.** VMS never lands a time in the RTC (persisted
  CMOS shows virgin clock registers, Reg B configured binary/24-h with
  periodic interrupts only); century byte `0x32` unsynthesized (the
  platform RTC part has no hardware century — software convention).
  Candidate origin territory for the historical poisoning event.

## New Diagnostics (in this build)

| Facility | Env vars |
|---|---|
| VA store-watch (dual address-form, marker-armed, ASN filter, ring-dump chain, forward window) | `EMULATR_VA_WATCH`, `_LEN`, `_ASN`, `_RINGDUMP[=nz]`, `_TRACE` |
| Store-value watch | `EMULATR_VAL_WATCH`, `_CYCLO` |
| Runtime lookback-ring depth | `EMULATR_LOOKBACK` |
| Value/PC one-shot ring gates, console-message armed | `EMULATR_VALUE_GATE`, `EMULATR_PC_GATE`, `EMULATR_UART_TRACE_MARKER` |
| TLB alias detector (divergent-PTE only) | CMake `EMULATR_DIAG_TLB_CONSISTENCY` |
| Unaligned-fixup log opcode column | (always on with the log) |

## Key Evidence Artifacts

- Run logs: `ds20_v7_3_vawatch3_20260812_093033.log` (register dump,
  first tz-struct catch), `redoct1/redoct2/redoct3` (conversion,
  divide, page-in proof), `verdict1/verdict2` (cure/pristine boots)
- Snapshot: `predig_oemsnap_cyc3589966532.axpsnap` (pre-crash fault
  state; poisoned struct at PA 0x4728210)
- Journal: JRN-SUPMODE-001 secs 20–35 (full day-by-day chain of custody)
