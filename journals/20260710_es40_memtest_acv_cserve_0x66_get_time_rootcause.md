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

## CORRECTION (2026-07-10, later): 0x66 is undefined + Ghidra != runtime

Tim's lead ("0x66 is a callback, not system time";
Processor Support/PalcodeBitsavers/srmconsole/5.8/SRC/CALLBACKS_ALPHA.MAR) sent
me to the authoritative 5.8 srmconsole source.  Three findings that reframe the
fix:

  1. 0x66 is UNDEFINED in every available cserve table.  PAL_DEF.SDL /
     EV6_PC264_PAL_DEFS.SDL (5.8): CSERVE$START = 0x42, CSERVE$CALLBACK = 0x43,
     max defined = MP_WORK_REQUEST = 0x65 (101).  There is no 0x66 (102).  The
     console-callback CR_TABLE (CALLBACKS_ALPHA.MAR) stops at func 0x34.  So 0x66
     is NEITHER a cserve function NOR a CR_TABLE callback routine.  The prior
     "get_time" label AND a literal "callback 0x66" are both unconfirmed.

  2. The Ghidra export does NOT match the running code at these addresses.
     ghidra/ghirda_decompressed.txt static disasm: 0x8C2F8 = LDA pv,-0x7640(t1);
     0x1B78F8 = raw byte 0x46 (undecoded data).  The runtime retire trace:
     0x8C2F8 = BIS =>R16=0x66; 0x1B78F8 = CSERVE.  They disagree because the ES40
     SRM SELF-DECOMPRESSES -- the runtime bytes are not the on-disk/static image.
     Consequence: the static Ghidra view CANNOT be used to disassemble these
     runtime addresses; the retire trace (EmulatR's own execution) is the only
     reliable view, and even it is only as good as EmulatR's decode of the
     decompressed bytes.

  3. The logical corner.  If 0x66 is truly undefined everywhere, real hardware
     would hw_ret it as a no-op (R0 untouched) too -- yet the real ES40 memtest
     PASSES.  That implies R0 must ALREADY be valid before the call on real HW.
     So the fix is probably NOT "make CSERVE 0x66 return a time"; it is more
     likely that the value the SUBQ at 0x8C308 consumes should have been produced
     EARLIER by something EmulatR is not providing (a device read, an RPCC/RSCC
     path, or a decode that EmulatR gets wrong on the decompressed image).

REFRAMED NEXT STEPS (supersede the "0x66 returns monotonic time" plan of task #12):
  a. Disassemble the RUNTIME (decompressed) bytes at 0x8C2D0 and 0x1B78F8 -- via a
     live guest-memory dump + EmulatR's own disassembler, NOT the static Ghidra
     export -- and CONFIRM whether 0x1B78F8 really is CALL_PAL cserve with
     r16=0x66, or a callback dispatch EmulatR mis-decodes as cserve (Tim's lead).
  b. Capture R0 (and both SUBQ operands) immediately BEFORE and AFTER the call to
     see whether R0 was already garbage on entry (the caller/earlier step is the
     bug) or whether the call was expected to fill it.
  c. Only after (a)+(b): decide the fix -- implement a real 0x66, fix an upstream
     R0/time producer, or fix a decode.  Do NOT re-add a get_time value blindly
     (that path caused the 2026-07-08 SCB-base regression).

## DECISIVE (2026-07-10, Tim's FUN_0008c300 disasm): image-fidelity, NOT cserve

Tim pulled the real Ghidra disasm of FUN_0008c300.  It is a clean helper:

    0008c300  e0 ff de 23  LDA  SP,-0x20(SP)
    0008c304  10 d4 ec 47  MOV  0x66,R16          ; a normal reg load (0x66 is REAL)
    0008c308  00 00 7e b7  STQ  PV,0x0(SP)
    ...       19 34 e0 47  MOV  0x1,R25
    0008c32c  e2 ac 44 d3  BSR  RA,FUN_001b76b8
    0008c338  00 04 40 40  ADDQ R2,R0,R0          ; real code ADDs; NOT a SUBQ
    0008c348  01 80 fa 6b  RET

EmulatR's runtime trace at the SAME addresses is a DIFFERENT stream:
    0x8c300 BIS =>R30=0x31140     (real: LDA SP,-0x20(SP))
    0x8c304 LDQ =>R26=0x5b034     (real: MOV 0x66,R16)
    0x8c308 SUBQ =>R00=0xFFFFFFFF7F827F5F  (real: STQ PV,0(SP))  <- the garbage + ACV

CONCLUSION (supersedes the cserve/get_time framing above): the bytes in EmulatR's
guest memory at ~0x8C300+ do NOT match the real console.  0x66 is real (a plain
MOV 0x66,R16), but the SUBQ->0xFFFFFFFF7F827F5F path EmulatR executes is NOT real
console code -- EmulatR is running a CORRUPT/DIVERGENT instruction stream here.
The real function ADDQs (R0 = R2 + R0) and returns; it never derefs a bad base.
So the ES40 memtest ACV is a FIRMWARE-IMAGE FIDELITY bug (the ES40 SRM
self-decompresses; the decompressed/loaded bytes at ~0x8C3xx are wrong --
consistent with the load-base/decompressor fragility already on record), NOT a
CSERVE / get_time / callback semantics issue.  The AAR fix merely let boot reach
the region where the bad image bites.  The entire "0x66 = get_time/callback"
investigation (this journal, above) is a red herring rooted in EmulatR decoding
wrong bytes.

DECISIVE TEST (do this first next session): dump EmulatR's RUNTIME bytes at
0x8C300 via the AppOptions PaDump path (traceLib::dumpPaRange / dumpDisasmAt,
which already render "# PaDump bytes: pa=...") and compare to the real
e0 ff de 23 / 10 d4 ec 47 / 00 00 7e b7.  If they differ -> image corruption
CONFIRMED; the work becomes ES40 decompression / load-base fidelity (SrmLoader /
FirmwareLoader / the ES40 decompressor), NOT task #12's cserve.  Also confirm
EmulatR loads the SAME SRM image Ghidra analyzed (the MOV 0x66,R16 fingerprint
strongly suggests yes).

## SSOT-CONFIRMED (2026-07-10): image-fidelity bug in EmulatR's decompression

Tim provided the SSOT decompressed image (decompressed_es40_v7_2.bin, 4.15 MB,
md5 6f63131167d0278bb4b015c84ef554f6).  IMPORTANT version map:
  - EmulatR RUNS firmware/es40_v7_3.exe (compressed; decompressed at runtime).
  - SSOT is v7_2 (a near-identical earlier build; good oracle).
  - The in-tree firmware/cl67_decompressed.rom (2.10 MB, md5 f51f52db...) is a
    STALE/DIFFERENT image -- do NOT use it as the oracle.

Decoding the SSOT confirms the real code.  0x66 appears as a benign value in two
places, NEVER as a cserve selector:
  1. file 0x80934: a char loop -- MOV 0x63/0x66/0x67/0x65,R16 = ASCII 'c'/'f'/'g'/'e'.
  2. file 0x841e4 (== Tim's FUN_0008c300): a clean wrapper --
        LDA  SP,-0x20(SP)     ; e0 ff de 23   (Tim's 0x8C300)
        MOV  0x66,R16         ; 10 d4 ec 47   (Tim's 0x8C304) -- an ARG, not cserve
        STQ  R27,0(SP)        ; 00 00 7e b7
        MOV  0x1,R25
        ... LDQ R27,off(R27); BSR helper (0x1B76B8 in v7_3); ...; RET
     (The v7_2 SSOT lacks the ADDQ R2,R0,R0 that Tim's v7_3 Ghidra shows -- the
     only v7_2/v7_3 delta, confirming Tim's Ghidra is the v7_3 EmulatR runs.)

EmulatR's runtime trace at the SAME guest addresses is a DIFFERENT stream
(BIS R30=0x31140; LDQ R26=0x5b034; SUBQ R00=0xFFFFFFFF7F827F5F -> ACV).  So
EmulatR's guest memory at ~0x8C3xx does not hold the real v7_3 code.  VERDICT
(locked): the ES40 memtest ACV is an EmulatR FIRMWARE-IMAGE FIDELITY bug in the
decompression/load of es40_v7_3.exe, producing corrupt code at ~0x8C3xx.  Every
cserve/get_time/callback interpretation above is a ghost from decoding bad bytes.

DECISIVE TEST + FIX PATH (next session):
  1. Decompress firmware/es40_v7_3.exe with the reference host_decompressor ->
     canonical v7_3 decompressed image (the true oracle for the running version).
  2. Dump EmulatR's RUNTIME memory at guest 0x8C300 (AppOptions PaDump /
     traceLib::dumpPaRange) and compare byte-for-byte to (1).  Expected: they
     DIFFER (real: e0 ff de 23 / 10 d4 ec 47 / 00 00 7e b7; EmulatR: corrupt).
  3. Root-cause the divergence in EmulatR's ES40 decompressor / load-base path
     (systemLib/SrmLoader.cpp, systemLib/FirmwareLoader.h, tools/host_decompressor).
     Whether it is a wrong load base, a decompressor byte error, or a stale
     cached image, the memtest passes once guest 0x8C3xx holds the real bytes.
  4. Do NOT touch execCserve / add a get_time value -- that path is a dead end.

## AUTHORITATIVE (2026-07-10, oracle) -- SUPERSEDES the two sections above

The two sections above ("CORRECTION" and "SSOT-CONFIRMED / image-fidelity") are
WRONG and superseded.  I built the native reference decompressor
(tools/host_decompressor: cc -O2 -o oracle src/oracle.c src/inflate.c) and ran it
on the exact image EmulatR loads:

    oracle firmware/es40_v7_3.exe out/decompressed_es40_v7_3.bin
    -> WimC@0x2400 compressedSize=0x2ae881 target=0x8000 output=0x3f6800 (4155392)
    -> md5 72972c20ec75ec16aa47c56f08e22661

FINDINGS:
  1. That md5 is BYTE-IDENTICAL to Tim's es40_decompressed.bin.  So that file was
     the correct v7_3 image, merely MISNAMED; build_firmware_variants.sh lists
     es40_v7_2 but never es40_v7_3, so decompressed_es40_v7_3.bin never existed.
     Gap closed (image now in tools/host_decompressor/out/decompressed_es40_v7_3.bin).
  2. Decompressor target base = 0x8000, so guest = fileoffset + 0x8000.  Decoding
     the CORRECT v7_3 image at the guest PCs EmulatR executed MATCHES EmulatR's
     runtime byte-for-byte:
        0x8c2f0 MOV R16,R2         (R2 = input)
        0x8c2f4 LDQ R27,-1576(R27)
        0x8c2f8 MOV 0x66,R16       (EmulatR: BIS R16=0x66)   MATCH
        0x8c2fc BSR -> 0x1b78f8    (EmulatR: BSR)             MATCH
        0x1b78f8 CALL_PAL 0x9      (EmulatR: CSERVE; helper = cserve;RET) MATCH
        0x8c308 SUBQ R2,R0,R0      (EmulatR: SUBQ)  R0 = R2 - R0   MATCH
        0x1b7dd4 LDQ R0,0(R16)     (EmulatR: the ACV load)   MATCH
     (My earlier "ADDQ vs SUBQ" version discriminator was bogus -- the real v7_3
     code SUBTRACTS; Tim's Ghidra "ADDQ" line was a mis-analysis/base artifact.)

VERDICT (final, oracle-verified): EmulatR runs the correct v7_3 image and decodes
it correctly.  There is NO decompression/load/decode bug.  The ES40 memtest ACV
is real firmware behaviour: get_time() = "R0 = input - CSERVE(0x66)"; the helper
at 0x1b78f8 is exactly CALL_PAL cserve; RET.  EmulatR no-ops CSERVE 0x66 -> R0 is
not the value the firmware needs -> SUBQ underflows to 0xFFFFFFFF7F827F5F -> the
LDQ R0,0(R16) at 0x1b7dd4 derefs it -> ACV.

THE REAL OPEN QUESTION (task #12, back to the cserve framing but now image-proven):
  0x66 is undefined in the 5.8 cserve table (max = MP_WORK_REQUEST 0x65), yet the
  firmware depends on R0 after CSERVE 0x66.  Two possibilities, distinguished by
  ONE trace read of R0 at function entry / just before 0x1b78f8:
    (a) CSERVE 0x66 must RETURN a value in R0 (a time/counter) on this firmware's
        PAL -> implement it (carefully: the 2026-07-08 BCD-TOY value broke the SCB
        base; the SUBQ wants a value that makes input - R0 sane).
    (b) R0 should already be valid on entry (cserve 0x66 is a legit no-op) and the
        real bug is an upstream producer EmulatR isn't running.
  Datum: at the faulting pass R2(input)=0x3fc12000 and the SUBQ result
  0xFFFFFFFF7F827F5F implies R0-before-SUBQ = 0xC03EA0A1 -- confirm whether that R0
  came from the cserve return or was carried in.

## RESOLVED (2026-07-10, PAL source) -- cserve 0x66 is a FAITHFUL no-op; ACV is UPSTREAM

Authoritative dispatch: Processor Support/PalcodeBitsavers/apisrm/apisrm/ref/
sys__cserve in ev6_vms_pc264_pal.mar, codes in ev6_pc264_pal_defs.sdl (DECIMAL):
  LDLP=16 STLP=17 LDBP=18 STBP=19 HALT=64 WHAMI=65 START=66 CALLBACK=67
  MTPR_EXC_ADDR=68 JUMP_TO_ARC=69 IIC_WRITE=70 MP_WORK_REQUEST=101(max).
The firmware's R16 = 0x66 = 102 decimal = ONE PAST the max defined code, so it
misses every cmpeq and hits the default `hw_ret (p23) ; return, nothing done`
(present in all three variants at lines 3860/3911/4095).  (START is 66 DECIMAL =
0x42, NOT 0x66 hex.)  Therefore CSERVE 0x66 does nothing and leaves R0 untouched
on real PAL; the CALL_PAL is a pipeline-drain/barrier, not a value-returning call.

=> Answers the open question above: branch (b).  EmulatR's no-op of CSERVE 0x66 is
FAITHFUL.  The helper at 0x8c2d0 is a bare two-operand subtract R0 = R2 - R0 (plus
a barrier) that unwinds its frame.  The ACV therefore originates UPSTREAM: R0
arriving at the helper = 0xC03EA0A1 is garbage (R2=0x3fc12000 is a plausible PA;
the correct result should be ~0x3fc12000-ish).  Do NOT implement cserve 0x66 (it
would be an INFIDELITY and repeats the 2026-07-08 SCB-base regression).  Next: trace
the producer of R0 (and confirm R16/R2) before guest PC 0x8c2d0 -- the real bug is
whatever set R0 to 0xC03EA0A1.

## SESSION CLOSE (2026-07-10, end of day)

What we settled today (three dead trails eliminated, one live trail confirmed):
  - IMAGE: es40_v7_3.exe decompresses (native oracle) to a byte-for-byte image
    that EmulatR runs and decodes correctly.  No decompression/decode/image bug.
    es40_decompressed.bin was the correct v7_3 all along, just misnamed; the true
    file now exists at tools/host_decompressor/out/decompressed_es40_v7_3.bin.
    (Fix-it-later: add es40_v7_3 to build_firmware_variants.sh FIRMWARES list.)
  - CSERVE 0x66: source-closed as a FAITHFUL no-op (102 = one past max code 101 ->
    hw_ret nothing done, R0 untouched).  Do NOT implement it.
  - The helper at guest 0x8c2d0 is a bare subtract R0 = R2 - R0 (+ a cserve
    pipeline barrier) that unwinds its frame.  It is not get_time and not a
    firmware-fidelity issue.

The one live bug (task #12, reframed): the value arriving in R0 at the helper is
0xC03EA0A1 -- the low 32 bits of a fill/march pointer whose 42-bit kseg base
(bits[41:32]=0x3FF) has been dropped.  Correct R0 = 0x000003FF_C03EA0A1 would make
the probe VA 0xFFFFFC00_7F827F5F (valid kseg) instead of 0xFFFFFFFF_7F827F5F (ACV).

Tomorrow afternoon, first move: arm a gated retire window just ahead of guest PC
0x8c2d0 and capture R0's last writer (and confirm R16/R2), to localize WHERE the
top 0x3FF of the kseg base is dropped (address-gen / superpage / PAL seam vs. the
pointer's pre-window base already being 32-bit).  This is the last stratum before
the ES40 memtest clears.  Standing TEMP probe edits (PipelineDriver.h one-shot arm;
run_srm_trace_full.sh ARM=cyc mode) remain in place for that capture.

## Artifacts

- traces/20260710-184254_srm.trc (30007 retires; the birth chain above).
- traces/20260710-184253_es40_console.out (40 MB; the SRM fault print).
- putty_console_p10026_20260710175716.log (the 17:57 clean console).
