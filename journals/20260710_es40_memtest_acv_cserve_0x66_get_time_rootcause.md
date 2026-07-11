<!--
EmulatR V4 -- ES40 memtest ACV (surviving fault after the AAR fix): ROOT-CAUSED.
The malformed kseg base 0xFFFFFFFF7F827F5F is manufactured by the powerup
memory-test's get_time() (guest 0x8C2D0), which reads the clock via CALL_PAL
CSERVE 0x66.  EmulatR no-ops 0x66 faithfully-to-the-generic-dispatch (R0
untouched), so get_time() returns garbage and a SUBQ underflows to the bad base.
Records the gated-trace method, the exact retire chain, the fix constraints, and
why the fix is deferred to a deliberate design pass.  2026-07-10.
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
FAITHFUL implementation, not expedience.  [LOCATE] = point-in-time; verify
against the live tree.
-->

# ES40 memtest ACV -- surviving fault ROOT-CAUSED: CSERVE 0x66 get_time no-op (2026-07-10)

## Where this sits

The AAR ASIZ decode-width fix (companion journal
20260710_es40_memtest_acv_RESOLVED_aar_asiz_and_tiling.md) is validated live: the
17:57 and 18:42 ES40 runs both print "Memory size 4096 MB", enter "testing
memory", and no longer take the old 0x60222C map-corruption ACV.  Boot now dies
one stratum deeper, at a DIFFERENT and already-predicted fault.  This journal
records the root cause of that surviving fault.

## The surviving fault (from the SRM's own print)

    access violation fault
      PCB = 0002F560 (powerup)
      PC  = 001B7DD4
      VA  = FFFFFFFF 7F827F5F        ; == R3 == R16

VA 0xFFFFFFFF7F827F5F is a malformed kseg/superpage base; the correct EV6
physical-superpage base is present alongside as R20 = 0x00000801FC000000
(kPchip0_IODense).  The ACV is retaken in a loop from cyc ~248M; the run just
exhausts MAXCYC while spinning in it.  The CSERVE func=0x66 (=102 dec)
"reserved / no-op" flood in the fault log is the memtest's own per-pass call,
not post-fault noise (see below).

## Method: gated retire window (no firehose)

Rather than the multi-GB retire firehose, a bounded one-shot window was armed in
PipelineDriver.h (kTraceArmCyc ~20k cyc before the fault, kTraceLen 30000
retires) and opened with a new run-script mode:

    NOTRACE=1 ARM=cyc MAXCYC=0x12000000 bash tools/run_es40_trace.sh

NOTRACE drops --trace (RETIRE_COMPACT firehose off); ARM=cyc opens the retire
sink with NO PA/IIC arm, so the ONLY trigger is the compiled cycle arm.  Result:
a 30007-line traces/20260710-184254_srm.trc that brackets the birth of R16.
(These are TEMP probes, REMOVE-after-capture; the ARM=cyc script mode is worth
keeping.)

## Root cause: the exact retire chain

The memtest's get_time() at guest 0x8C2D0 (the "return = input - get_time()"
idiom flagged in palBoxLib/grains/PalEntries.cpp:594-600):

    8C2F8  BIS    => R16 = 0x66             ; load CSERVE selector 0x66
    8C2FC  BSR    => R26 = 0x8C300          ; call the CSERVE wrapper
    1B78F8 CSERVE  pal=0                     ; CALL_PAL 0x66 -> EmulatR no-op, R0 UNTOUCHED
    1B78FC RET
    8C300  BIS    => R30 = 0x31140
    8C304  LDQ    => R26 = 0x5B034
    8C308  SUBQ   => R00 = FFFFFFFF7F827F5F  ; R00 = input - garbage(R0 from 0x66)
    ...
    5B03C  BIS    => R16 = FFFFFFFF7F827F5F  ; copy R00 -> R16
    611EC  BIS    => R03 = FFFFFFFF7F827F5F
    61214  BIS    => R16 = FFFFFFFF7F827F5F  ; reload
    1B7DD4 LDQ    va = FFFFFFFF7F827F5F pa = 0 -> ACV

So the malformed base is NOT born in the 0x1B7xxx leaf and is NOT a translation
bug.  It is born at the SUBQ at 0x8C308, whose subtrahend is the R0 that CSERVE
0x66 was supposed to fill with the time.  Because EmulatR no-ops 0x66 (R0 left as
the caller had it), the subtraction underflows to 0xFFFFFFFF7F827F5F, which then
propagates verbatim into R16 and is dereferenced.

## Why the no-op was "faithful" yet wrong here

The authoritative PC264 cserve table (Processor Support/.../ev6_pc264_pal_defs.mar)
stops at MP_WORK_REQUEST = 101 = 0x65; 0x66 = 102 is undefined, and the VMS
sys__cserve dispatch (ev6_vms_pc264_pal.mar) hw_ret's "nothing done" (R0
untouched) for undefined codes.  EmulatR's no-op is therefore faithful to that
generic dispatch.  But THIS console's memtest genuinely uses CSERVE 0x66 as its
get_time primitive (the trace proves the call and the R0 dependence), so the
generic no-op is wrong for this path.  The earlier guess in PalEntries.cpp (that
get_time uses an internal get_timestamp bsr, "not a CSERVE") is contradicted by
the trace: it is a CSERVE 0x66.

## Fix constraints (task #12) -- and why it is deferred

The fix is: CSERVE 0x66 must return a usable time in R0.  It is NOT a one-line
change, because a prior 0x66 get_time (removed 2026-07-08, see
20260708_es40_scb_base_mismatch_root.md) returned a BCD TOY timestamp and that
value shifted the console's SCB base (base = R0 + 0x28000 expected R0 near 0;
0x01010000 + 0x28000 -> read the interval-clock vector off a wrong base -> HW_REI
to PC 0 -> halt).  So any 0x66 return value must satisfy BOTH:

  (a) memtest:  input - get_time() is a sane, non-negative timing delta at 0x8C308;
  (b) SCB path: it must not reshift base = R0 + 0x28000 (the 2026-07-08 regression).

OPEN QUESTIONS to resolve before editing (do not guess):

  1. The SUBQ operands at 0x8C308 -- which register is "input", what units/scale,
     and what magnitude of get_time() makes the delta sane (cycle counter?
     tick counter? a small elapsed value?).  Needs the get_time disassembly, not
     just retire dest-writes.
  2. What the real ES40 SRM services for CSERVE 0x66 -- PAL (a patched build past
     the .mar table), or a CONSOLE callback.  The .mar source we have does not
     define 0x66, so the real behavior must be sourced, not inferred.
  3. Whether the SCB-base consumer and the memtest get_time() are the same 0x66
     call site or two, which decides whether one return value can serve both.

Because each ES40 validation run reaches ~282M cycles (minutes) and the failure
mode is a subtle time-value regression that already bit once, a hasty tonight
edit risks re-baking the SCB-base bug or a new one with slow feedback.  Per the
hybrid workflow, the get_time value design is a web-chat design task; Cowork lands
the edit once the value and its two consumers are pinned.

## Status

- Task #6 (AAR ASIZ): CLOSED, validated live.
- Task #10 (Stream A, localize R16): CLOSED -- localized to SUBQ 0x8C308 off the
  CSERVE 0x66 no-op.
- Task #12 (make 0x66 a faithful get_time): OPEN -- design first (units + both
  consumers), then edit, then a full cold run.

## Artifacts

- traces/20260710-184254_srm.trc (30007 retires; the birth chain above).
- traces/20260710-184253_es40_console.out (40 MB; the SRM fault print).
- putty_console_p10026_20260710175716.log (the 17:57 clean console).
