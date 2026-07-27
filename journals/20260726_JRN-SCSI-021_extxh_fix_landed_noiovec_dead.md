<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-021
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-021 -- EXTxH FIX LANDED (architect-approved).  V1-V3 PASS.
#                 %APB-F-NOIOVEC IS DEAD: the walk accepts, APB reads
#                 the disk and TRANSFERS.  New frontier: halt code 10
#                 (decimal) @ PC 0x2a000, ~7.9s past the bootstrap jump.

    Doc id   : JRN-SCSI-021
    Date     : 2026-07-26
    Status   : FIX APPLIED + VERIFIED.  Closes the NOIOVEC arc
               (JRN-VMB-018..022, JRN-SCSI-001..020).
    Relates  : JRN-SCSI-020 (root cause + fix proposal, approved).

--------------------------------------------------------------------------------
## 1. The change (approved per -020 Sec 4)

  coreLib/alpha_int_byteops.h: extwh/extlh/extqh rewritten to the AARM
  Sec 4.6.1 formula -- shift = ((8 - (offset & 7)) * 8) & 63 (the
  byte_loc<5:0> truncation), width mask retained; the bytePos==0 -> 0
  special cases DELETED.  Behavior changes ONLY for the aligned-Rb
  case (now pass-through + mask).  Header comment documents the AARM
  citation and the JRN-SCSI-020 root cause.
  NEW tests/coreLib/test_byteops.cpp (registered in CMakeLists):
  aligned-case pass-through, full 8-offset AARM-model sweep for all
  three EXTxH, the pre-BWX signed byte-load idiom at every X mod 8
  (k=7 = the regression case), the two-LDQ_U unaligned-quadword idiom,
  spot values locking offsets 1..7 unchanged, and INSxH/MSKxH aligned
  semantics locked (they must NOT be "symmetrized").

## 2. Verification

  V1  byteops cases: 7/7, 56 assertions PASS.
  V3  full suite: 495 cases, 492 pass; the 3 fails are exactly the
      PRE-EXISTING drift set (ide_wiring + 2x mmio_csc, JRN-SCSI-003).
      No regressions from the fix.
  V2  cold DS20 boot (run_ds20_showdev_20260726_165719.log), operator
      `b dka0.0.0.8.0 -flags 0`:
        - block 0 valid, 1226 blocks read, bootstrap jump -- as before;
        - %APB-F-NOIOVEC: ZERO occurrences (was 100% reproducible);
        - ~7.9 s of NEW execution past "jumping to bootstrap code"
          with a changed CSERVE profile (18x func 70 + 2x func 101
          alongside the usual 66/67) -- APB conversing with the
          console for real device I/O;
        - then: "halted CPU 0 / halt code = 10 / PC = 2a000".

  The resolver wall (L1) is CLOSED.  APB accepts the topology string,
  performs post-accept console I/O, and control reaches code at
  PC 0x2a000 before a halt.

## 3. The new frontier (next session's L2)

  "halt code = 10" is printed in DECIMAL (apisrm kernel.c:2922) and
  10 is OUTSIDE the named PAL halt table (1..7: HW_HALT..MCHK_FROM_PAL
  -- hence no text line followed).  It is a SOFTWARE-POSTED halt
  reason from whatever runs at PC 0x2a000 (candidates: APB's transfer
  target -- SYSBOOT or its loader path -- posting a failure it could
  not message).  NEXT probes:
    N1  Identify the image/code at PC 0x2a000 at halt time (snapshot +
        disasm; is it SYSBOOT? an APB error path?).
    N2  DIAG window over the last N cycles before the halt (the
        FAULT_CYCLO/CYCHI loud window from run_ds20_bplus already
        brackets late-boot faults).
    N3  The CSERVE func 70/101 conversation decode (what APB asked
        for after accept -- crb_conversation_decode.py works on any
        CRB-window run).

  IMPACT NOTE (carried from -020): the EXTxH defect corrupted EVERY
  pre-BWX byte/word H-idiom read at the aligned-Rb case anywhere in
  guest software.  Prior anomalies chased in other subsystems deserve
  a re-test on the fixed binary before further investigation.

--------------------------------------------------------------------------------
## 4. Files touched

  - coreLib/alpha_int_byteops.h     FIXED (extwh/extlh/extqh)
  - tests/coreLib/test_byteops.cpp  NEW
  - CMakeLists.txt                  test registration
  - this journal                    NEW
