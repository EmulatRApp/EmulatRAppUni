<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-018
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-018 -- P1b: THE DEATH SITE IS NAMED.  Field 3's numeric
#                 parse (custom strtol 0x2005e3a0) returns 0 on a
#                 non-digit character; the 19f8 "action code" reading
#                 is WITHDRAWN; the VM's opcode jump table is decoded.

    Doc id   : JRN-SCSI-018
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-017 (tree), JRN-SCSI-016 (accept = sub-walk),
               JRN-VMB-021 ("dies at field 2" -- position now exact).

--------------------------------------------------------------------------------
## 1. Correction: the 19f8 targets are NOT call entries

  0x20039278 (JRN-SCSI-017's rel32 reading for production A's 19f8)
  immediately loads r26 as scratch -- a BSR/JSR target would be
  destroying its return address.  The rel32 interpretation for the
  f8-class is WITHDRAWN; the f8 sub-encodings remain open (P1b debt
  carried).  Nothing downstream depended on it: the trace shows the
  walk NEVER REACHES the 19f8 record (Sec 2).

## 2. Exact death position (read-histogram proof)

  All stream reads in run B stop at QW 0x99238: the walk processed
  85f6-pre, 85f1-ident, 85f3-field2 (the 3 store/kill pairs), READ the
  field-3 TOKEN word (0x9923e, inside QW 0x99238) -- and never touched
  QW 0x99240 (field-3's rel32 operand).  Death is INSIDE field-3
  processing, before its operand record is consulted.

## 3. The VM opcode dispatch (decoded live at 0x20095e30-e60)

  entry = [ctx-0x10] + 4*(opcode_low_byte - 0xe4)   (+0x48 bias slot
  observed for 0xf3); range check CMPULE (op-0xe4) <= 0x14 (0xe4..0xf8);
  handler VA = entry + [ctx+0x8] (code base; cells 0x200652e8/0x20065300).
  Opcode 0xf3 -> handler 0x20096040.  Loop-head extras decoded:
  token -> STL 0x18(r29); whitespace pre-scan (tokenizer mode 0) before
  dispatch; CMOVLBS on a descriptor byte can FORCE the gosub path
  (0x20095ad4-e0).

## 4. THE DEATH: field-3's strtol fails on a non-digit

  0xf3 handler flow (all traced, cyc 1941882788..2908):
    1. tokenizer(mode 1, 0x20096f80): scans the NEXT string token --
       first char IS a digit (BLBC not taken), second char is not
       (token length 1); scan reached QW 0x200dfd48 (string offset>=8).
    2. r16 = scan result, r17 = descriptor +0x14 position; BSR
       0x2005e3a0 = custom STRTOL: base 10 (r19=0xa, '9' bound r20),
       accepts '+'/'-' (0x2b/0x2d), digit loop with MULL-by-10
       accumulate (0x2005e5e8) -- and NO WHITESPACE SKIP.
    3. At 0x2005e604 the digit test rejects the char just read (BNE
       taken, cyc 1941882905) -> 0x2005e680: r0 <- 0 -> return FAIL.
       The failing read hit QW 0x200dfd40 -- string offset <= 7, i.e.
       INSIDE "SCSI 0 8" -- while the tokenizer had scanned past
       offset 8.  The parse position and scan position DISAGREE around
       the whitespace between fields.
    4. On r0=0 the handler bails without consuming the operand; the
       record loop exits (0x20096cbc); the sub-walk verdict is the
       sentinel; NOIOVEC follows (JRN-SCSI-013 chain).

  NOTE strtol itself cannot skip spaces, so whoever computes the +0x14
  position must deliver a digit-aligned offset.  Candidate readings:
  (a) the descriptor position update (0x20095b34/+0x14 copy at
  0x20095bb4) points at the separator, not the digit -- an off-by-one
  vs the mode-0 whitespace scan; (b) the field-2 matcher (0x2000e1e0)
  left the position at its token's END (the space) and field-3's
  handler variant (0x2000e260 -- NEVER INVOKED, death precedes it)
  would have adjusted it.  Distinguishing (a)/(b) needs byte-level
  register values: ONE DIAG_WREG run on r17 at pc 0x2005e410 window,
  or a DIAG window over 0x2005e3a0-0x2005e6b0 with CAP small.

--------------------------------------------------------------------------------
## 5. Why this reframes L1 (sharpest form yet)

  The walk is not rejecting our string's CONTENT (ident matched,
  field-2 matched, field-3's first char IS a digit by the tokenizer's
  own scan) -- it is failing to PARSE at a position that straddles the
  field separator.  Post-hoc checks this session:
    - The stack copy at 0x200dfd40 is UNRECOVERABLE from the snapshot
      (page walked via snap_ptwalk: PA 0x699d40; the frame was reused
      by the message-emit calls after the halt).
    - The SOURCE buffer 0x2006aab8 is clean: "SCSI 0 6 0 0 0 0 0",
      SINGLE spaces, NUL-terminated (slot-6-era snapshot).  Mapping
      the trace reads onto that layout (run B, slot 8): mode-0 scan
      skipped the space at offset 6 and stopped on the digit at
      offset 7; the mode-1 scan verified a digit at 7 and stopped on
      the space at 8; strtol's failing read was at offset <= 7.
  LEADING MODEL: strtol's r17 = offset 6 (the separator SPACE) -- the
  descriptor's +0x14 position cell (copied from +0xc at 0x20095b60-
  bb4) LAGS the whitespace scan.  Since the lag is string-independent,
  the same code accepting AXPBox's string (JRN-SCSI-005 A4) implies
  AXPBox's boot_dev/booted_dev must differ in FORM (we only ever saw
  its display form) -- OR the position cells are primed differently by
  an earlier phase (env walks, get_env answers).  Decisive next:
    Q2  DIAG window over 0x2005e3a0-0x2005e6b0 (or DIAG_WREG r17 at
        0x2005e410) -- one boot names strtol's exact start offset.
    R4  (JRN-SCSI-011, now decisive) AXPBox's byte-exact boot_dev /
        booted_dev env values for the same media -- the walk parses
        THAT string on the working system, and its separator layout
        is the one the position-cell dance must be tuned to.

--------------------------------------------------------------------------------
## 6. Files touched

  - this journal   NEW
  No emulator code changed.
