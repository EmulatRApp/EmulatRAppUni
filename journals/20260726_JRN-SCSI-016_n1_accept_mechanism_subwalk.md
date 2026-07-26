<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-016
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-016 -- N1' DONE: THE ACCEPT MECHANISM IS A RECURSIVE
#                 SUB-WALK.  Status is set at exactly ONE site
#                 (0x20096524), guarded by "sub-walk returned
#                 non-sentinel".  The wwid terminals are IOVEC
#                 parameter writers.

    Doc id   : JRN-SCSI-016
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-015 (grammar/ctx), JRN-SCSI-014 (verdict flow),
               JRN-SCSI-013 (the branch), JRN-VMB-021 ("dies at field 2").

--------------------------------------------------------------------------------
## 1. The wwid terminal handlers (static disasm, snap_va_disasm)

  0x2000e450 (the stream's only bit-10-CLEAR record):
      r0  <- *(record-0x8)          ; destination object ptr cell
      r16 <- parsed value (+0x1c of tokenizer slot)
      r1  <- *r0
      STL r16 -> r1+0x148 ; STL 0 -> r1+0x14c ; return 1
  0x2000e470:
      r0  <- *(record+0x10)
      STL parsed(+0x18) -> r1+0x150 ; STL unaligned-long(+0x1c) -> r1+0x154
      return 1
  They WRITE THE MATCHED VALUES into a destination structure
  (+0x148/0x14c/0x150/0x154) -- the IOVEC parameter block the failure
  message is named after.  Neither touches the status longword.

--------------------------------------------------------------------------------
## 2. THE accept mechanism (the N1' deliverable)

  Static scan of the whole resolver module (0x20095000-0x20097400) for
  `STL rX,0x28(r29)` finds exactly FOUR sites:

      0x20095a68  STL r31   init (walk entry)
      0x20096524  STL r1    *** THE ONLY SUCCESS STORE ***
      0x20096834  STL r0    per-record store (probe cycle)
      0x20096ae0  STL r31   per-record kill  (probe cycle)

  Site 0x20096524 is guarded by:

      0x200964fc  BSR r26, 0x20095840   ; RECURSIVE SUB-WALK
      0x20096500  BIS r31,r0,r1         ; r1 = sub-walk verdict
      0x20096514  LDQ r12,0x28(r2)      ; fail sentinel 0x158284
      0x20096518  XOR r1,r12,r12
      0x20096520  BEQ r12,.+0x4         ; equal (fail) -> SKIP the store
      0x20096524  STL r1,0x28(r29)      ; non-sentinel -> STATUS = verdict

  So: THE WALK ACCEPTS IFF A RECURSIVE SUB-PRODUCTION RETURNS
  NON-SENTINEL.  The store-kill cycle of JRN-SCSI-014 is the probe
  scaffolding; the real verdict propagates bottom-up through sub-walk
  returns.  Bits[27:3] of a non-sentinel sub-verdict are what the final
  CMOVEQ (0x20096e58) tests.

  Sub-walk call setup (0x200964a0-0x200964f8), two cursor forms:
    r7 != 0:  16-bit word at cursor, sign-extended, r5 = cursor + disp,
              r17 = r5+2          (STREAM-RELATIVE GOSUB)
    r7 == 0:  32-bit longword at cursor -> r17  (absolute form)
  Args: r16 = r3 (key/string object), r18 = r4, r19 = 0x20(r29)
  (buffer), r27 = ctx-0x40 operand base.  The grammar is therefore a
  recursive-descent VM; productions nest via gosub records.

--------------------------------------------------------------------------------
## 3. Trace confirmation (run B, t1_apb_retire_20260726_152442.txt)

  - 0x200964fc fired 3x (call table); each sub-walk's own CMOVEQ verdict
    computed sentinel (e.g. cyc 1941883123: LDL status @ 1941883118 = 0
    -> CMOVEQ -> ret 0x20096eb0 @ 1941883144 -> outer BEQ TAKEN -> store
    SKIPPED).  Recursion bottoms out with no store anywhere: the whole
    grammar finds NO acceptable sub-production for our topology string.
  - The nesting also explains the 7 resolver entries per attempt
    (top-level per wrapper + gosub recursions + early env-walk uses).

--------------------------------------------------------------------------------
## 4. L1 final form + next instrument

  Question now: WHICH sub-production would return non-sentinel on a
  working boot, and what data does it consume that differs on EmulatR?
  The sub-walk scans the SAME string through the same VM, so the delta
  must enter through a PRIMITIVE: the gosub-target sub-streams and the
  runtime objects they reference (key records 0x2006a2xx+, tokenizer
  descriptors 0x2006aa60+, ctx-bound cells) -- populated from the
  console's device view via the CRB conversation (JRN-SCSI-011).

  NEXT (cheapest first):
    P1  Full gosub-target resolution: decode the prologue tokens
        (0x11f7/0x05f6/0x000c/0xffff forms -- the parser's has32
        heuristic mis-groups some), name each sub-production's stream
        VA, and statically enumerate the grammar tree.  Pure decode of
        bytes already in hand (tools/apb_stream_decode.py extension).
    P2  One targeted DIAG run: window pinned to 0x20096440-0x20096530
        (the gosub site) with EMULATR_DIAG_CAP small -- logs every
        sub-walk dispatch (cursor r17 values = which sub-productions
        run).  Compare against P1's tree to see which branches are
        never tried for our string.
    P3  T5 AXPBox comparative, now with THE single question: which
        gosub target returns non-sentinel for the same media (its
        sub-stream VA + the runtime cells it reads).

--------------------------------------------------------------------------------
## 5. Files touched

  - this journal   NEW
  No emulator code changed.
