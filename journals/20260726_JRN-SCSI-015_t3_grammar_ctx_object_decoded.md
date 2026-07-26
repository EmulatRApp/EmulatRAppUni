<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-015
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-015 -- T3 + grammar: the operand-record format is cracked
#                 ({header, handlerVA}, rel32 is END-relative); the walk
#                 ctx is an OBJECT of the same format; the production is
#                 ident+7-fields with per-field validator functions; a
#                 bit-10-CLEAR terminal record exists in the wwid tail.

    Doc id   : JRN-SCSI-015
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-014 (verdict data flow), JRN-SCSI-013 (the branch),
               JRN-SCSI-012 Sec 5.3 (ptr base "record-relative" -- refined
               here to END-of-record), JRN-VMB-021 (ident+7 fields).
    Tool     : NEW tools/apb_stream_decode.py (stream/records/ctx modes).

--------------------------------------------------------------------------------
## 1. Operand record format (VERIFIED against trace-observed handlers)

  A match-bearing stream token (0x85fX class) carries rel32; target =
  tokenVA + 6 + rel32 (END-of-record relative -- -012 Sec 5.3 refined).
  The target is an 8-aligned OPERAND RECORD:

      { u64 typeHeader,  u64 handlerVA }

  typeHeader classes seen: 0x3008 (plain), 0x83089 / 0x183089 / 0x1283089
  (0x3089 class + param byte), 0x117300a.  handlerVA is a per-field
  VALIDATOR FUNCTION in APB code.

## 2. The boot_dev production, fully mapped (stream section 1 = section 2)

      token    operand      handler     field
      0x85f6 -> 0x200636c8  0x2000dba0  (pre)
      0x85f1 -> 0x20063588  0x2000e140  ident   (the "SCSI" matcher)
      0x85f3 -> 0x200635d0  0x2000e1e0  field 2 <- the traced helper
                                        (0x2000e1fc..e250 = inside it;
                                        it SUCCEEDS and stores the value
                                        into key record +0x60)
      0x85f3 -> 0x20063568  0x2000e260  field 3
      0x85f3 -> 0x200635f8  0x2000e000  field 4
      0x85f3 -> 0x20063578  0x2000e2e0  field 5
      0x85f3 -> 0x20063558  0x2000e300  field 6
      0x85f5 -> 0x20063608  0x2000dfe0  field 7
      0x85f5 -> 0x200635e0  0x2000e020  field 8   (= ident+7, VMB-021)
      0x85f6 -> 0x20063530  0x2000e0b0  (post)
      0x19f8 -> code 0x20039278 (b10=0)
      0x95f6 -> 0x20063638  0x2000dc10
      -- wwid ALTERNATIVE tail --
      0xffff ext 0x0440 ('@'), chars 'w','w','i','d' (char+0x04 tokens)
      0x85f3 -> 0x200634c8  0x2000e320  (wwid value)
      0x85f6 -> 0x20063530  0x2000e0b0
      0x81f5 -> 0x200634b8  0x2000e450  *** bit10 CLEAR -- TERMINAL ***
      0x85e6 -> 0x200634a0  0x2000e470
      0x15f6 ...  then 00 00 separator; section 2 repeats the production
      byte-for-byte with the same operand records (booted_dev/boot_dev).

## 3. T3: the walk ctx (a3 = 0x20063820) is an OBJECT, not a flat table

  The ctx region is built from the SAME {header, handler} record format
  plus data slots -- a vtable'd object binding every traced player:

      20063820: hdr 0x103089 | 20063828: 0x2000e9d0  classify (ident lits)
      20063860: hdr 0x3008   | 20063868: 0x2000e870
      20063870: hdr 0x3008   | 20063878: 0x2000e880
      20063880: hdr 0x283089 | 20063888: 0x2000e890  wrapper #1 (traced)
      200638a0: 0x2006aab8   tokenizer STRING BUFFER (traced reads)
      200638a8: 0x2006eec0
      200638b0: 0x2000f940   MESSAGE FORMATTER (NOIOVEC emit target)
      200638b8: 0x20063bb0
      200638c0: 0x2000def0   THE WALK
      200638c8: 0x20063618
      200638d0: 0x2000fb60   message routine #2
      200638e0: hdr 0x1203089| 200638e8: 0x2000e770  wrapper #2 (traced)
      (20063810/40: 0x20071270 = the dispatch base observed live)

  This is why --find-bsr finds nothing: ALL invocation is through object
  slots.  It also gives T4 its data source map: whatever fills/selects
  these objects and their key records decides accept vs NOIOVEC.

## 4. The sharpened N1 (carried forward)

  Every field record of the MAIN production is bit-10-SET (store-then-
  kill per JRN-SCSI-014): the ident+7-fields walk is structurally a
  PROBE.  The only bit-10-CLEAR terminal record (0x81f5, handler
  0x2000e450) sits in the WWID-alternative tail, reachable only after
  matching the literal "wwid" -- which "SCSI 0 8 ..." never does.
  NEXT (N1'): decode handlers 0x2000e450/0x2000e470 and the VM's
  loop-exit site 0x20096624->0x20096cbc (r16=1 exit) to name the token
  sequence that can exit WITH status intact; then T4: on a working
  (AXPBox) boot, which record's status write survives -- now answerable
  by inspecting the SAME static stream + the runtime key records
  (0x2006a2b0 area) rather than any instrumentation.

--------------------------------------------------------------------------------
## 5. Files touched

  - tools/apb_stream_decode.py   NEW  (stream/records/ctx decoder)
  - this journal                 NEW
  No emulator code changed.
