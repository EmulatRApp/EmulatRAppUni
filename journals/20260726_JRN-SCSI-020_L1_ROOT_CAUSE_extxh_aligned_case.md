<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-020
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-020 -- L1 ROOT CAUSE: EmulatR's EXTWH/EXTLH/EXTQH return 0
#                 for Rbv<2:0>=0; the AARM requires shift amount
#                 byte_loc<5:0> (64 truncates to 0 = PASS-THROUGH).
#                 Every guest byte read at address == 7 (mod 8) via the
#                 pre-BWX idiom yields NUL.  %APB-F-NOIOVEC follows.

    Doc id   : JRN-SCSI-020
    Date     : 2026-07-26
    Status   : ROOT CAUSE NAMED + FIX PROPOSED.  No emulator code
               changed yet (discuss-first; diff below awaits approval).
    Relates  : closes the L1 chain JRN-SCSI-013..019; corrects the
               JRN-SCSI-018 "position lag" model; JRN-VMB-018..022,
               JRN-SCSI-004/-005 (content-independence explained).

--------------------------------------------------------------------------------
## 1. The Q2 run (byte-exact pointers)

  Boot 20260726_163309 with DIAG window 0x2005e3a0-0x2005e6b0 +
  DIAG_WREG=22.  Five strtol invocations captured; the walk-relevant
  four (start-pointer writes at pc 0x2005e450):

      walk#1 (probe,  source buf): 0x2006aabd (off 5, '0')  0x2006aabf (off 7, '8')
      walk#2 (accept, stack copy): 0x200dfd45 (off 5, '0')  0x200dfd47 (off 7, '8')

  BOTH walks parse field 2 AND field 3; JRN-SCSI-018's "strtol starts
  on the separator space" model is REFUTED -- every start pointer is
  ON the digit.  The PC-flow diff between the '0' and '8' invocations
  diverges at ONE branch: 0x2005e604 (digit-range test) -- '0' (offset
  5) passes, '8' (offset 7) FAILS the digit test and exits via
  0x2005e680 (r0=0).  The caller (0x20096088..0x2009661c) then takes
  BLBC on the return -> loop exit -> sentinel -> NOIOVEC.

## 2. Why a digit fails the digit test: the byte-read idiom

  strtol's char fetch (0x2005e5cc-e5e4, same shape at 0x2005e410-24):

      LDQ_U  r25, 0(r22)        ; QW containing the char
      LDA    r21, 1(r22)        ; r21 = char address + 1
      EXTQH  r25, r21, r25      ; char -> top byte
      SRA    r25, #0x38, r21    ; sign-extend char

  This is the canonical pre-BWX SIGNED BYTE LOAD.  For char address X
  with X mod 8 = k (k < 7): EXTQH shift = 64-8*(k+1) = 56-8k -> the
  byte lands at bits [63:56].  For k = 7: Rbv<2:0> = (X+1) mod 8 = 0.

  AARM (alpha_arch_ref.txt line 9193, Sec 4.6.1 EXTxH operation):

      byte_loc <- 64 - Rbv'<2:0>*8
      temp     <- LEFT_SHIFT(Rav, byte_loc<5:0>)     <<< SIX BITS
      Rc       <- BYTE_ZAP(temp, NOT(byte_mask))

  byte_loc<5:0>: for Rbv<2:0>=0, 64 -> 0 -> LEFT_SHIFT by 0 -> the
  value passes through; the char at X (already the QW's top byte when
  k=7) is extracted CORRECTLY on real silicon.

  EmulatR (coreLib/alpha_int_byteops.h):

      extwh (line 211): if (bytePos == 0) return 0;     // WRONG
      extlh (line 221): if (bytePos == 0) return 0;     // WRONG
      extqh (line 231): if (bytePos == 0) return 0;     // WRONG

  So EVERY byte read through this idiom at X == 7 (mod 8) returns
  0x00.  In "SCSI 0 8 0 0 0 0 0" the slot digit sits at offset 7 ->
  read as NUL -> "non-digit" -> parse fail -> walk sentinel ->
  %APB-F-NOIOVEC.

## 3. Why every earlier observation now falls out

  - CONTENT-INDEPENDENT (JRN-SCSI-004/-012): the failure depends only
    on the CHAR ADDRESS being 7 mod 8, not on its value; ident and
    slot changes never move the offset (string base is 8-aligned).
  - Byte-identical resolver footprint IDE vs SCSI: same offsets, same
    branch outcomes.
  - AXPBox boots the same media (JRN-SCSI-005 A4): its EXTQH
    implements the mod-64 semantics.
  - "Dies at field 2" (JRN-VMB-021 numbering; field 3 here): the
    FIRST field whose digit lands at offset 7.
  - The probe AND accept passes both fail identically (JRN-SCSI-013).
  - The audit trail 013->020 (branch -> verdict -> sub-walk -> tree ->
    strtol) was necessary: the defect is invisible at any higher
    layer.

  AUDIT of the sibling families (same file, against AARM Sec 4.6):
  MSKxL/MSKxH: correct (mskqh(0)=value, mskql(0)=0 both right).
  INSxH: the bytePos==0 -> 0 special case is CORRECT (the AARM's
  BYTE_ZAP mask byte_mask<15:8> is empty at Rbv=0).
  EXTxL: correct.  ONLY the three EXTxH functions are defective.

## 4. PROPOSED FIX (awaiting architect approval; discuss-first rule)

  File: coreLib/alpha_int_byteops.h, functions extwh/extlh/extqh
  (lines 210-238).  Edit shape -- replace the body of each with the
  AARM formula (shift truncated to 6 bits; special cases deleted):

      extwh:  const int shift = ((8 - static_cast<int>(offset & 0x7)) * 8) & 63;
              return (value << shift) & 0xFFFFULL;
      extlh:  same shift; return (value << shift) & 0xFFFFFFFFULL;
      extqh:  same shift; return value << shift;

  (bytePos 0 -> shift 64 & 63 = 0 -> pass-through + width mask; all
  other bytePos values produce the same results as today.)
  Callers: only the eBox grain wrappers (execExtwh/lh/qh); no test
  encodes the old behavior; Mbox unaligned-store merge has separate
  code (audited, not affected).

  VERIFICATION PLAN after the fix:
    V1  Unit: add doctest cases extqh(x,0)==x, extwh(x,0)==x&0xffff,
        extlh(x,0)==x&0xffffffff, plus the k=7 byte-load idiom.
    V2  Boot: cold DS20 boot, `b dka0.0.0.8.0 -flags 0` -- expect the
        walk to pass field 3+ (NOIOVEC gone; next stop either deeper
        boot progress or a NEW frontier past the resolver).
    V3  Regression: full 487-case suite (3 pre-existing drift fails
        allowed per JRN-SCSI-003).

  IMPACT NOTE: the defect corrupts ANY guest signed/unsigned byte or
  word read through the pre-BWX H-idiom at the aligned-Rb case --
  potentially implicated in other unexplained guest behavior beyond
  boot.  Worth a post-fix regression sweep of known oddities.

--------------------------------------------------------------------------------
## 5. Files touched

  - this journal   NEW
  No emulator code changed (fix proposed above, not applied).
