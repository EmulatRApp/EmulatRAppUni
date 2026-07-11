<!--
EmulatR V4 -- ES40 memtest ACV: extensive-trace operand analysis (2026-07-11).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Contact: peert@envysys.com | https://envysys.com
ADR-0001 header; ASCII(128); hex radix.  Discuss-before-code stands.
Trace: RelWithDebInfo/traces/20260711-082855_srm.trc (11.26 GB, ~40M retires,
gated window ord ~245M-285M; arm kTraceArmCyc=245000000 kTraceLen=40000000).
-->

# ES40 memtest ACV -- extensive-trace operand analysis (2026-07-11)

## Run

Wide gated retire window (firehose OFF, ARM=cyc): armed at cyc 245,000,000, logged
40,000,000 retires -> 11.26 GB .trc spanning ord 245M..285M, capturing memtest
setup -> march -> the first ACVs at ~282M.  Analyzed with bounded byte-offset seeks
(dd) into a 300 MB chunk at ord 281.71M..282.82M; no whole-file grep.

## The faulting birth chain (ord 282,090,795..282,090,818, all pal=0)

    0x8b93c  LDQ   R0  = 0x0000000100000000   ; mem_size = 4 GiB (LDQ from 0x31170)
    0x8b948  SUBQ  R0  = 0x00000000c0000000   ; base = mem_size - 0x40000000 = 3 GiB
    0x5af60  SLL   R16 = 0x000000003fc12000   ; arg = 0x1fe09 << 13
    0x5afb4  LDA   R0  = 0x00000000c03ea0a1   ; walk ptr (base + n*8), 32-bit
    0x8c2f0  BIS   R2  = 0x000000003fc12000   ; get_time saves arg in R2
    0x8c2f8  BIS   R16 = 0x0000000000000066   ; cserve selector (no-op barrier)
    0x8c2fc  BSR      -> 0x1b78f8 = CALL_PAL 0x9 (cserve) ; RET  (R0 untouched)
    0x8c308  SUBQ  R0  = R2 - R0 = 0x3fc12000 - 0xc03ea0a1 = 0xffffffff7f827f5f
    0x8c318  RET
    0x5b03c  BIS   R16 = 0xffffffff7f827f5f   ; -> load pointer -> ACV

## Findings

F1 -- ARITHMETIC IS FAITHFUL.  EmulatR computes exactly what the encoded
  instructions specify (SUBQ R2,R0,R0 => R0 = R2 - R0).  0x3fc12000 - 0xc03ea0a1
  = 0xffffffff7f827f5f, deterministic.  No truncation or sign-extension defect in
  this chain.  (Re-confirms 2026-07-10c "address math proven faithful".)

F2 -- BASE IS PHYSICAL, NOT KSEG.  0x8b948 computes mem_size(0x100000000) minus
  0x40000000 = 0xC0000000, a bare 32-bit physical 3 GiB address; the walk (0x5afb4
  LDA r0,8(r0)) increments it in physical form.  Across all 40M retires NO register
  ever holds a 0x000003ff or 0xfffffc00 high-longword (checked every dest-write).
  So the march operands never carry the EV6 kseg/superpage base.

F3 -- THE GARBAGE IS A VALID KSEG VA + 0x3FF.  0xffffffff7f827f5f =
  0xfffffc00_7f827f5f (the OSF kseg VA of PA 0x7f827f5f, ~2.03 GiB, in-RAM) plus
  0x000003ff_00000000.  Equivalently: had the walk pointer carried bits[41:32]=0x3FF
  (R0 = 0x000003ff_c03ea0a1), then R2 - R0 = 0xfffffc00_7f827f5f, a VALID kseg VA.
  So exactly one operand is missing its high superpage bits.  (Confirms the
  2026-07-10 "42-bit base dropped at injection" hypothesis with the exact operands.)

F4 -- THE ROUTINE DOES FORM 43-BIT ADDRESSES ELSEWHERE.  R20 = 0x00000801fc000000
  (5358x) and R16 = 0x00000801fc0002xx (>5000x) appear in the same region (bit-43
  Pchip/CSR config space).  So the memtest knows how to build high physical
  addresses; only the RAM-march base stays 32-bit physical.

F5 -- FAULT DELIVERY.  The garbage R16 is not dereffed inline at 0x5b03c; the caller
  continues (0x5b040..0x5b054 BSR -> 0x5b058) and traps into PAL 0x8321 (HW_LD,
  exc=0x611e0), the double-miss/fault path.  EmulatR delivers an unrecoverable ACV
  (per 2026-07-09: PTE walk -> no-valid/protection-deny).

## The sharpened question (A vs B) -- needs SRM source intent

The value 0xffffffff7f827f5f is what the firmware ARITHMETIC produces, so EmulatR
diverges from real HW in exactly one of two ways:

  (A) MMU/translation (or fault-recovery) bug: real HW computes the same VA and its
      translation SUCCEEDS, or the memtest deliberately probes it and RECOVERS from
      the fault; EmulatR instead delivers a fatal ACV.
  (B) Upstream missing superpage-OR: the march base (or arg) should be a kseg VA
      with bits[41:32]=0x3FF, applied by a step EmulatR is not reproducing; real HW
      never computes the wild VA.

Distinguish by reading the pc264 SRM memtest routine intent (physical march vs kseg
march, and whether faults are caught): routines at guest 0x8b694 / 0x5a6b0 / 0x5b058
/ base-compute 0x8b948, in Processor Support/PalcodeBitsavers/apisrm (memconfig /
memtest sources).  This is the agreed next step; no code change until intent is
known (discuss-before-code).

## SOURCE EXTRACT (2026-07-11, apisrm/apisrm/ref) -- for web adjudication

Operand decode re-confirmed from the image (ground-truth machine code, resolves the
dst-only caveat):
    0x8b93c  LDQ  R0,8(R3)        mem_size = [R3+8]
    0x8b948  SUBQ R0,R26,R0       base = mem_size - R26  (R26=0x40000000 runtime; a
                                  REGISTER subtrahend, NOT a literal)
    0x5af60  SLL  R18,#0xd,R16    arg = R18 << 13   (R18=0x1fe09 -> 0x3fc12000; <<13 = x8192 blk)
    0x5afb4  LDA  R0,8(R0)        walk += 8 (64-bit add; preserves high bits if present)
    0x8c2f0  BIS  R31,R16,R2      R2 = arg
    0x8c308  SUBQ R2,R0,R0        R0 = arg - walk   (operands CONFIRMED)
    0x5b03c  BIS  R31,R0,R16      R16 = result

DECISIVE FINDING -- simple hypothesis B (missing kseg/superpage frame) is FALSIFIED:
  memtest_alpha.mar leaves ALL strip the address to 32 bits and use it directly as a
  PHYSICAL access:
    _graycode_memtest_ (:164): "zap r16,^xf0,r16 ; clear upper longword" (:172), then
      r24 = r16, and marches with "stq r4,(r24)" / "addq r24,^x20,r24 ; next address".
    _mem_fast_write_ (:392): same zap of r16/r17 (:394-395, :419-420).
    _stq_ldq_/_stl_ldl_/_do_stq_ldq_ (:498/:510/:527): "zap ... ; zap sign extension"
      (:528), zap upper/lower 32 (:535/:537).
  Documented fault model (header :96): "EDC/ECC logic will detect any bit errors...
  the fault model is not to detect address shorts but to stress the memory path."
  memconfig_pc264.c get_array_base (:137) returns ReadTsunamiCSR(AAR) & aar_m_addr =
  "bits<34:24> of the PHYSICAL byte address"; get_array_size (:101) = physical MB.
  => The pc264 memory subsystem is PHYSICAL end-to-end; NO kseg/superpage framing
  anywhere in memtest/memconfig.  The march is MEANT to run on bare <=35-bit physical
  addresses and even actively removes high bits.

Consequence for A vs B:
  * The wild 0xffffffff7f827f5f would be ZAPPED to 0x7f827f5f (valid 32-bit PA) by any
    memtest LEAF -- so it only faults because it reaches the GENERIC LDQ helper at
    0x1b7dd4 (reached via the C-driver BSR at 0x5b058), which does NOT zap.
  * So the value is produced faithfully and is a signed 64-bit quantity (arg - walk =
    -0x807d80a1).  Open sharpened question for web: is R0 = arg - walk a byte COUNT /
    signed delta (legitimately negative, correctly consumed elsewhere) with the ACV a
    downstream misuse, OR should the generic 0x1b7dd4 helper mask R16 to the physical
    range (a 32-bit/PA-mask the firmware relies on), OR is there still an upstream
    input divergence?  No natural "mem_size - constant" yields the kseg-framed base,
    and the source shows no kseg intent -- so the fix is NOT "add a kseg frame."

## C-DRIVER + HELPER DISASM (2026-07-11) -- resolves count-vs-address fork

0x1b7dd4 helper (image): bare native "LDQ R0,0(R16)" -- NO mask/zap.  Caller must
supply a valid address; the cheap "helper missing a mask" fix is CLOSED.

0x611b0 (image) is a (ptr=R16, count=R17) read-loop:
    0x611ec BIS R16,R3        ; R3 = ptr arg (the garbage)
    0x611f4 BIS R17,R4        ; R4 = count arg
    0x61208 BLE R4 ->0x61240  ; if count<=0 skip  (BRANCH GUARDS THE COUNT, not the ptr)
    0x61214 BIS R3,R16        ; R16 = ptr
    0x61224 BSR ->0x1b7dd4    ; LDQ R0,[R16] -> ACV
    0x61230 ADDQ R3,#0x8,R3   ; ptr += 8
    0x6122c CMPLE R7,R4,R17   ; loop while R7<=count
Trace confirms: BLE R4 at 0x61208 FALLS THROUGH (count>0), 0x61214 sets R16=garbage,
0x1b7dd4 LDQ va=ffffffff7f827f5f -> fault.

Caller (image, 0x5b010..0x5b054):
    0x5b010 LDQ R16,24(R4)    ; arg = [R4+24] = 0x3fc12000
    0x5b028 BEQ R16 ->0x5b058 ; only ptr-ish branch, BEFORE get_time, on input arg (nonzero->not taken)
    0x5b030 BSR ->0x8c2d0     ; get_time: R0 = arg - walk
    0x5b03c BIS R0,R16        ; R16 = get_time result (the garbage ptr)
    0x5b054 BSR ->0x611b0     ; pass garbage as ptr arg
Preceding walk loop stores to the walk pointer with no fault:
    0x5afcc STQ R12,0(R0)     ; R0=0xc03ea0a1 is a VALID physical store addr

VERDICT (web's second outcome): R0 is used as an ADDRESS (not a count); the only
guarding branch tests the COUNT; the arithmetic is faithful and walk is a legit PA;
so real EV6 computes the identical garbage pointer -- the divergence is an INPUT, not
control flow, not the MMU, not the helper.  Pointer is garbage only because
arg(0x3fc12000, ~1GB) < walk(0xc03ea0a1, ~3GB) -> negative wrap.

NEXT (provenance read): (1) walk base at 0x8b948 = mem_size([R3+8]) - R26(0x40000000)
-- is mem_size or the 1GB subtrahend right vs real HW?  (2) arg 0x3fc12000 from
[R4+24] / R18(0x1fe09)<<13 -- provenance of R18 and the [R4+24] table.  Caveat:
0x611b0 is a "read N quadwords from ptr" loop = likely the memtest READ-BACK/VERIFY
pass, so that ptr is SUPPOSED to be a valid test address in [3GB,4GB); either arg is
wrong or get_time's result is not meant to be the raw pointer.  Map 0x611b0 / 0x8c2d0
(get_time) / the 0x5axxx driver to source symbols to settle intent.

## Provenance

Exact trace lines (ord / pc / value) for every row above are in
RelWithDebInfo/traces/20260711-082855_srm.trc.  TEMP one-shot arm remains in
PipelineDriver.h (kTraceArmCyc=245000000 / kTraceLen=40000000) for any re-capture;
REMOVE after the intent question is settled.
