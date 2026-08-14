# Release Note — The DCL Era: Interactive OpenVMS on EmulatR; "Species B" Reduced to a Deterministic Reproducer with a Decoded Mechanism Candidate

**Date:** 2026-08-13
**Platform:** DS20 (Tsunami/EV6), firmware `ds20_v7_3`, OpenVMS Alpha V8.3
**Tree:** V5 (`v5-tb`), on top of `fd39e96`; this build adds the runtime
auto-snapshot controls (uncommitted at time of writing)
**Investigation record:** `journals/20260813_JRN-B2-001_dcl_era_reproducer_and_pal_leak_mechanism.md`
**Prior note:** `20260812_species_b_accvio_root_cause.md` (two-defect structure)

---

## RESOLUTION (2026-08-14) — ROOT CAUSE FOUND AND FIXED

The defect was CVTGD/CVTDG implemented as identity functions
(fpBoxLib/fp_backend_softfloat.cpp): VAX G_floating and D_floating share
values but not bit layouts (G: 11-bit exponent at <62:52>, bias 1024;
D: 8-bit at <62:55>, bias 128).  OpenVMS LIBRTL computes its heap size
classes by extracting the D exponent field from a CVTGD result; handed
unconverted G bits, the class map came out wrong (measured classmap[5]=6
vs healthy 0x31), every process-heap allocation was granted a 16-byte
stride, heap blocks overlapped, and the entire fault family followed.
Fixed per AARM 4.10.11 with real G<->D re-encodes + FV-9 doctests.
ACCEPTANCE PASSED: cold boot, LOADUNI reproducer clean, @SYS$SYSTEM:STARTUP
ran to completion with ZERO access violations.  Species B is CLOSED.

## Summary

Two headlines, one day:

**1. EmulatR now runs OpenVMS interactively.** Using a conversational
boot (`SYSBOOT> SET/STARTUP=OPA0:`), the system reaches a live DCL `$`
prompt on the console — the first in EmulatR's history — creates
processes (`SPAWN`, multi-level), runs DIRECTORY / TYPE / DUMP / SEARCH
/ SYSGEN / live SDA (`ANALYZE/SYSTEM`) correctly, absorbs a dozen access
violations without a system crash, and drives `@SYS$SYSTEM:STARTUP` to
**completion**. The fidelity claim moves from "boots to the STARTUP
wall" to "runs OpenVMS interactively; one known defect open."

**2. The open defect ("B2", beta-blocking) collapsed from a mystery to
a three-line, seconds-scale, perfectly deterministic reproducer — and
its corrupted registers were decoded to identify the mechanism
candidate.** The "garbage" values in the fault dumps are recognizably
**PALcode TB-miss internals** (the VPTB and computed VPTE addresses)
leaked into the interrupted program's architectural registers. The
suspect seam is EmulatR's nested TB-miss / shadow-register (SDE) PAL
glue — which is exactly why Charon and PersonalAlpha, which run their
own console/PAL environments, never reproduce it.

## The Reproducer

From any DCL prompt:

```
$ LOADUNI = "$LOAD_UNICODE_TABLE"
$ LOADUNI SYS$SYSTEM:UNIDATA2.DAT
%SYSTEM-F-ACCVIO, access violation, reason mask=04,
    virtual address=0000000024535953, PC=FFFFFFFF8090F8C4, PS=0000001B
```

Three independent runs produced **byte-identical register dumps** down
to scratch registers. The victim (`LOAD_UNICODE_TABLE.EXE`, a 12-block
C image; activation list CMA$TIS_SHR / DECC$SHR / LIBOTS) was named at
the DCL level by running startup verified — `@SYS$SYSTEM:STARTUP FULL
"VD"` — whose option letters this investigation also documented from
the file itself: `V`=verify, `D`=log to `SYS$SYSTEM:STARTUP.LOG`,
`C`=verbose phase messages, `P`=component verify.

The fault site is OTS$MOVE's store loop (LIBOTS+218C4, symbolized **by
VMS's own TRACE facility running on EmulatR**). The argument registers
hold string *content* where an *address* belongs — and the string is
whatever the caller had in flight: the TDF value on 8/12, a filename
("SYS$SYSR...") in this reproducer. Yesterday's timezone connection is
fully retired: with the corrected TDF on disk, the fault fired
byte-identically with `"-18000"` riding the same registers. The string
identity is cargo; content-where-address is the disease.

## The Fault Family (one session's complete tally)

| Count | Fault | Site | Reading |
|---|---|---|---|
| 6 | mask=04 va="SYS$" | S0 LIBOTS+218C4 | primary: string content as pointer |
| 1 | same, P0 copy | PC 0x638C4 | same, pre-startup image slice |
| 2 | cascade | PC 0x44050/0x44090 | condition machinery dying with poisoned registers |
| 1 | wild transfer | **PC = 0xC** | corrupted return address taken |
| 3 | activation faults | **PC = 0x6 / 0xA**, va 0x7FFD1260 | transfers through pointers with the high half lost |

Unifiers: every sighting occurs in **image activation, rundown, or
RTL-call processing**; the system always survives (process-fatal,
never system-fatal); `SET PROCESS/DUMP` never produces an `IMGDMP` —
because the dump writer runs in the same last-chance condition path
that the cascade fault kills.

## Mechanism Candidate (decoded from the registers)

The cascade dump at PC 0x44090 reads, verbatim:

```
R26 (return address) = 0000020000000000   = VPTB (virtual page-table base)
R19                  = 000002000008015C   = VPTB + a VPTE offset
R20                  = 000100000008015C   = same offset, different high part
R27 (procedure value) = 0                 R28 = 0
```

These are the working values of the EV6 PAL TB-miss handler, present in
the *interrupted program's* architectural registers after PAL exit. The
emulator's own fault telemetry shows storms of **nested** DTB misses
(`kFaultDtbMissDouble`, PAL-mode PCs 0x8321/0x8591) in the same eras.
Working theory: the nested-PAL entry/exit or the SDE shadow-register
swap leaks PAL state into the resumed context. The corrupted-register
mix (R20 is shadow-class; R19/R26 are not) constrains where in the glue
the leak lives. A marker-armed, PC-gated 2M-instruction lookback
capture at the fault site is staged (run in progress at time of
writing) to name the exact instruction.

Exonerated today by measurement: on-disk image files (DUMP-verified
intact), the guest's RTL code (identical images run clean on reference
emulators), data content (register-level proof of data-independence),
and — from 8/12 — TLB organization, arithmetic, and timing.

## Beta Implications

1. **B2 remains the beta gate**, but its blast radius is now bounded:
   it is process-fatal in activation/rundown paths, not a system
   killer. An interactive system rides through it.
2. The 8/12 migration guidance stands: a disk that boots on Charon
   must boot on EmulatR; data-dependent divergence on legal input is
   an EmulatR defect by definition. Today strengthened this: the same
   defect fires with fully *correct* data.
3. Operators get two practical tools from this note: the
   `SET/STARTUP=OPA0:` conversational path to a working `$` prompt,
   and `@SYS$SYSTEM:STARTUP FULL "VD"` for a verified, logged startup.

## Ready for Testing

What this build supports well enough for beta exercise, each verified
in today's sessions:

1. **SRM console operation** — power-on to `P00>>>`, device enumeration
   (`show dev`: RZ29L system disk + six virtual disks + virtual CD on
   the 53C810 bus), and boot dispatch (`b dka0 -fl 0,1`).
2. **Conversational boot to a live DCL prompt** — `SYSBOOT>
   SET/STARTUP=OPA0:` then `C`; V8.3 banner, date/time dialog, root `$`
   on the console. This is the recommended interactive entry path.
3. **Process creation and control** — `SPAWN` (multi-level chains),
   `ATTACH`, `LOGOUT`; subprocess accounting verified via SDA.
4. **Core DCL and RMS file operations** — DIRECTORY, TYPE, DUMP,
   SEARCH, COPY-class access, DEFINE/logical names, SET DEFAULT /
   TERMINAL / PROCESS across an ODS-2 system disk.
5. **SYSGEN** — parameter inspection and modification against the
   active set (`USE ACTIVE` / `SET` / `WRITE ACTIVE`).
6. **Live crash-free system analysis with SDA** — `ANALYZE/SYSTEM`:
   SHOW EXECUTIVE, SHOW PROCESS(/IMAGE), SHOW SUMMARY, SHOW DEVICE and
   the I/O data-structure walks all render correctly.
7. **Full system startup, verified and logged** — `@SYS$SYSTEM:STARTUP
   FULL "VD"` runs to completion with DCL verification echoed and a
   persistent transcript in `SYS$SYSTEM:STARTUP.LOG`.
8. **Fault containment** — the known B2 access violations are
   process-fatal only; the system absorbs them and continues (a dozen
   in one session with no system crash). The `LOADUNI` two-liner in
   this note is the canary reproducer for regression tracking.
9. **Periodic auto-snapshot and fast resume** — `--autosnapshot
   1000000000` mints a save every 1B cycles; a subsequent launch
   without `--no-autoload` restores mid-session OpenVMS to the prompt
   in seconds. This is the recommended iteration workflow.
10. **Ethernet driver load** — the DE500 (`EWA0`) driver activates and
    reports link up during startup; network *stack* function beyond
    driver load is untested territory.

Not yet ready (fails today, tracked): full-screen editors (`EDIT`/TPU
dies on the B2 seam), `SET PROCESS/DUMP` image dumps (dump writer is a
B2 casualty), and any workload leaning hard on image
activation/rundown of freshly resident-installed shareables.

## New in This Build

**Runtime auto-snapshot cadence** (the fast-iteration substrate that
made today's minutes-scale resume-and-reproduce loop possible):

| Control | Meaning |
|---|---|
| `--autosnapshot on|off|<N>` | numeric N = periodic save every N cycles (built-in default 50e9) |
| `EMULATR_AUTOSNAP_PERIOD=N` | env channel for the same (CLI wins) |
| `EMULATR_AUTOSNAP_KEEP=K` | rolling-prune depth (default 5) |
| startup `auto-snapshot: CONFIGURED ...` line + per-save witness | the schedule and every save (or failure) are now visible in the run log |

Verified live: 1e9-cycle cadence produced a save at every 1B boundary;
resuming the newest snapshot restores a mid-session OpenVMS to the
prompt in seconds.

## Known Issues (unchanged or newly tracked)

- B2 as above (capture run staged). Charon cross-check of the
  reproducer (expected clean) pending.
- Typed characters echo only on Enter at SRM/VMS console prompts
  (host-side TX flush granularity; cosmetic; tracked).
- Crash-dump primitive-driver wedge and TOY time-write gap: as per the
  8/12 note.
- Doctest suite: 6 chipset/device-side failures + 1 test crash under
  triage (first full run since the TLB reshape; snapshot subsystem
  green).

## Key Evidence Artifacts

- Console logs: `ds20_v7_3_dclboot_20260813_112241.log` (first DCL
  session, VD startup, verified culprit line),
  `putty_console_p10023_20260813124116.log` (full afternoon session:
  reproducer x3, SDA census, activation-fault family, startup
  completion)
- Journal: `20260813_JRN-B2-001_dcl_era_reproducer_and_pal_leak_mechanism.md`
- On-disk transcript: `SYS$SYSTEM:STARTUP.LOG` (guest disk) — the
  verified startup stream that named the culprit line
