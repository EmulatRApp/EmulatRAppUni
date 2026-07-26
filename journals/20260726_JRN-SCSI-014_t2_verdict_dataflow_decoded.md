<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-014
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-014 -- T2/T2b: THE VERDICT DATA FLOW IS DECODED.  The fail
#                 sentinel is 0x158284; the status is stored per record
#                 and killed by the record's OWN token flags; the stream
#                 token word IS the flags word.

    Doc id   : JRN-SCSI-014
    Date     : 2026-07-26
    Status   : PROBE RECORD.  No emulator code changed.
    Relates  : JRN-SCSI-013 (T1: the branch @ 0x20003a10), JRN-SCSI-012
               (T-series), JRN-SCSI-005 Sec 3 (0xf3-tail gate), JRN-VMB-021
               (walk transcript), JRN-VMB-019 (pattern-VM 0x158284).
    Inputs   : traces/t1_apb_retire_20260726_152442.txt (run B, T1) +
               snapshots/auto_halt_1785040752_1903336426.axpsnap (slot-6
               era; static content identical per JRN-SCSI-004).

--------------------------------------------------------------------------------
## 1. The verdict chain, end to end (all VERIFIED, trace + static agree)

  1. Cell 0x20065320 (= 0x28(ctx r2) of the resolver) holds 0x00158284 --
     the pattern-VM address named in JRN-VMB-019.  It is the FAIL
     SENTINEL VALUE.
  2. The sentinel is born at 0x20096e58 `CMOVEQ r1,r17,r0`: r1 = bits
     [27:3] of a STATUS LONGWORD read from stack 0x28(r29) (this run:
     PA/VA 0x200dfbe8); if that field is 0, r0 <- 0x158284.
  3. The walk-exit check (0x20096500..20) XORs the returned r0 against
     the same cell -- equal (as traced) = fail; the wrapper ZAPNOTs r0
     (0x2000e844) and JRN-SCSI-013's BLBS @ 0x20003a10 falls through to
     the NOIOVEC emit.
  4. STATUS LIFE CYCLE in the accept pass (r18=1), from the trace:
       0x20095a68  STL r31,0x28(r29)     zero-init at walk start
       3x per matching record:
         0x20096834  STL r0,0x28(r29)    store success (helper r0=1)
         0x20096ae0  STL r31,0x28(r29)   ...and KILL it ~18 cycles later
       0x20096e44  LDL r1,0x28(r29)      final read = 0 -> sentinel.
  5. The kill is FLAGS-driven, not content-driven.  The flags quadword
     at 0x18(r29) is THE CURRENT STREAM TOKEN WORD -- proven statically:
     0x20096b00-0x20096b18 LDQ_U-pair-reads the next word at the stream
     cursor (r8, observed mem 0x20099238 = the JRN-VMB-021 stream) and
     immediately `STL r24,0x18(r29)`.

--------------------------------------------------------------------------------
## 2. Token bit map (partial; verified bits marked *)

     bit 15   : set in all observed record tokens (0x85xx class)
     bit 14 * : cursor +4 and link-pointer deref (0x20096864 path) --
                STRUCTURAL (link/extension), not "accept-eligible"
                (JRN-SCSI-013 Sec 4's T3 guess refined)
     bit 13   : tested at 0x200968a8 (untraced path)
     bit 12 * : alternate operand helper (0x20096b70 path, BLBS @
                0x20096ae4)
     bit 10 * : SET -> abandon current record, advance to next record
                (0x20096af0 -> 0x20096c44 -> loop 0x20095a70).  This is
                the probe-only semantics of the 0xf3-tail gate
                (JRN-SCSI-005) -- and in the ACCEPT pass too.
     bit  9 * : one extension byte follows (merged into flags bits
                [23:16], 0x20096b24-38)
     bits[16:11] * : 6-bit operand/field index (AND 0x3f after SRL #0xb,
                S4ADDQ *4 -> helper r17)

  Every token in the boot_dev stream is 0x85xx/0x95xx/0x15xx/0x11xx/
  0x05xx/0x8dxx/0x81xx/0x19xx class: bit 14 CLEAR, bit 10 SET on the
  match-bearing 0x85fX records.  Under Sec 1's life cycle those flags
  ALWAYS store-then-kill: the walk scans to stream end and returns the
  sentinel.  A heuristic whole-image scan (0x20060000-0x20099400) for
  bit-14-set record-class tokens found ZERO genuine hits.

--------------------------------------------------------------------------------
## 3. What provably WORKS (exonerations tightened)

  - Ident classification: key record +0x58 @ 0x2006a308 = "SCSI    ",
    matched by 0x2000e9d0 against literals "DVA "/"RAID"/"SCSI"/"MSCP"/
    "FLOP"; type code 0x11 stored; returns 1.
  - Production compare helper (0x2000e1xx..0x2000e250): returns r0=1 and
    WRITES the parsed field value (from 0x1c(r16) @ 0x2006aa80) into the
    key record +0x60 @ 0x2006a938.  Field extraction WORKS in the accept
    pass; values flow into the key record.
  - Tokenizer buffer (0x2006aab8): full topology string present
    ("SCSI 0 6 0 0 0 0 0" in this slot-6-era snapshot) with (ptr,len)
    descriptors at 0x2006aa60-78 pointing into stack 0x200dfd40.

--------------------------------------------------------------------------------
## 4. The sharpened L1 question + next steps

  The boot_dev stream (static, 0x99216..0x99328, two sections split by
  a 00 00 separator, ends in zeros; contains an embedded literal "wwid"
  production at 0x99276 in char+0x04 encoding) has NO record whose flags
  survive the store-kill cycle.  On THIS stream, with THIS VM path, the
  walk can never return success -- INDEPENDENT of the string content.
  Since the same APB.EXE boots on AXPBox (JRN-SCSI-005 A4), the accept
  on a working system must come from a path we have not yet walked:

    N1  The bit-10-CLEAR token chains (the VM keeps consuming the same
        production instead of advancing) -- their exit path may store
        the status LAST.  Decode: full grammar parse of both stream
        sections; name each token's action (T2 remainder).
    N2  A DIFFERENT ctx/stream pair selected via the 0x20063820 dispatch
        (T3): enumerate the table statically; live cells already seen:
        +0x8 -> 0x2000e9d0 (classify), 0x20063718 -> 0x200712e8 records,
        0x20063840 -> 0x20071270 (dispatch base), 0x200638e8 ->
        0x2000e770 (wrapper #2).  Find which ctx the accept pass would
        use when the walk succeeds, and what feeds its key record.
    N3  Only then AXPBox comparative (T5) with a SPECIFIC question:
        which record in the identical stream survives, i.e. which
        status write is last on a working boot.

--------------------------------------------------------------------------------
## 5. Files touched

  - this journal    NEW
  No emulator code changed.  (Analysis used tools committed in
  JRN-SCSI-013: t1_apb_trace_analyze.py + snap_va_disasm.py hexdump
  reuse of find_payload0.)
