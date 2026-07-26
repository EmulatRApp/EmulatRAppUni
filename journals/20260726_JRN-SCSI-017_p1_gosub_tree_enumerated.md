<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-017
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-017 -- P1 DONE: THE GOSUB TREE IS ENUMERATED.  The rule list
#                 has TWO productions (section 2 is a VARIANT grammar,
#                 not a copy); the GOSUB encoding is proven by cursor
#                 evidence; verdict arithmetic rules out ALL small
#                 status values.

    Doc id   : JRN-SCSI-017
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-016 (accept = sub-walk non-sentinel), JRN-SCSI-015
               (operand records), JRN-SCSI-014 (token bits), JRN-VMB-021.
    Tool     : tools/apb_stream_decode.py EXTENDED (opcode split, GOSUB
               decode with target computation, ctl-rel32 records).

--------------------------------------------------------------------------------
## 1. Token opcode model (VERIFIED at 0x20095bb8-c4)

  The dispatch extracts token bits [8:0] (SLL/SRL #0x37) and CMPULTs
  against 0x1f6: opcode < 0x1f6 = MATCHER class, >= 0x1f6 = CONTROL.
  Observed opcodes: 0x1f1/0x1f3/0x1f5/0x1ed/0x1e6 matchers; 0x1f6
  (gosub/post), 0x1f7 (rule header), 0x1f8 (action) control.

## 2. GOSUB encoding (VERIFIED by trace cursor evidence)

  `f6 05 | param16 | disp16` -- target = VA_of_disp + sext(disp) + 2
  (code 0x200964a4-c4: r5 = cursor + sext(word at cursor), r17 = r5+2).
  Run-B trace: sub-walks #2/#3 both dispatched with cursor r8 =
  0x2009921e, disp 0x000c -> sub-stream 0x2009922c.  Sub-walk #1 (env
  walk) dispatched at cursor 0x200991d8 -- a SEPARATE rule region
  below (Sec 4).

## 3. The tree (tools/apb_stream_decode.py --stream 0x20099216 0x11a)

  RULE LIST section 1 @ 0x99216 (used by BOTH wrapper walks, r18=0 and
  r18=1 -- trace-proven):
    0x11f7 rule hdr (ext 0xffff)
    0x05f6 GOSUB(param 0x0df8) ------------------.
    0x11f7 rule hdr; 0x102c/0x15ed ctl tail      |
  PRODUCTION A @ 0x9922c: <-----------------------
    85f6 pre    -> hdlr 0x2000dba0
    85f1 ident  -> 0x2000e140
    85f3 f2..f6 -> 0x2000e1e0/e260/e000/e2e0/e300
    85f5 f7     -> 0x2000dfe0
    85f5 f8     -> 0x2000e020        (hdr 0x117300a)
    85f6 post   -> 0x2000e0b0
    19f8 action -> 0x20039278 (rel32 target = CODE; UNVERIFIED size)
    95f6        -> 0x2000dc10
    -- wwid ALTERNATIVE --
    0xffff 0x0440 '@' + chars 'w','w','i','d' (char+0x04)
    85f3 -> 0x2000e320; 85f6 -> 0x2000e0b0
    81f5 TERMINAL -> 0x2000e450      (the only bit-10-CLEAR matcher)
    15f6; 85e6 -> 0x2000e470; 15f6
  == 00 00 separator ==
  RULE LIST section 2 @ 0x992a2: same shape, GOSUB -> its OWN body:
  PRODUCTION B @ 0x992b8 (VARIANT, not a copy):
    fields ident..f7 IDENTICAL operand records to A, then INSTEAD of
    f8: `8df8 ext 0x0018` + `a2f0 ext 0xfffc` (f8-class action pair),
    85f6 post, 19f8 action -> 0x20049278, 95f6 -> 0x2000dc10,
    0xffff 0x01f1 / 15f6 / 002d / ffff rel -> tail, 11f1, 15f6
  == 00 00 ==  trailer @ 0x9931e: 85f3 -> 0x2000e300, 15f6.

  Production A vs B = two accepted boot-string grammars (the f8 slot
  differs: A expects an 8th field value; B runs an action pair there).

## 4. The env rule region (sub-walk #1, cursor 0x200991d8)

  0x99100-0x9914f: matcher records with a DIFFERENT operand pool
  (rel32s -> 0x2005c4xx, e.g. 802a/85f1/905d/943e/85f4/902a/91f1/95f4
  tokens) -- the env-var/name grammar rules.
  0x99150-0x991cf: two 64-byte nibble-pair lookup tables (tokenizer
  char-class tables).
  0x991d0-0x99215: the env rule list (11f7 hdrs, f8-class action
  records with 8-byte forms `f8 89/9d/8d | ext16 | rel32`); the exact
  gosub form used at cursor 0x991d8 is NOT yet pinned (f8 sub-encoding
  owed).

## 5. Verdict arithmetic (VERIFIED; kills a whole hypothesis class)

  The final CMOVEQ path computes (status << 36) >> 39 = bits [27:3].
  STATUS VALUES 1..7 ARE STRUCTURALLY FAIL.  The per-record store of
  the helper's r0=1 (JRN-SCSI-014 store-kill cycle) could NEVER have
  passed -- only rich (pointer-sized) verdicts count, and the only
  writers of such are the GOSUB store (0x20096524) and possibly the
  f8-class ACTION handlers (19f8 targets 0x20039278/0x20049278 look
  like code -- the suspected IOVEC-builder actions).

--------------------------------------------------------------------------------
## 6. What P1 changes about the plan

  - The failing run used ONLY section-1's rule for both walks.  Whether
    a working boot uses production A, production B, or the wwid
    alternative is now THE question -- and it is answerable statically
    on AXPBox's side only by knowing which string the console hands
    over (JRN-SCSI-011 R4) -- the grammar itself is fixed.
  - The f8-class action encoding (sizes 4/6/8) and the 19f8 targets
    are the remaining decode debt (P1b); the 0x20039278/49278 code, if
    it is the accept action, is the LAST unread piece of the accept
    path.
  - P2 (targeted DIAG on 0x20096440-0x20096530) remains the cheap
    dynamic check: it logs every gosub dispatch cursor, i.e. which
    rules/productions get tried per boot form.

--------------------------------------------------------------------------------
## 7. Files touched

  - tools/apb_stream_decode.py   EXTENDED (Sec 1-3 encodings)
  - this journal                 NEW
  No emulator code changed.
