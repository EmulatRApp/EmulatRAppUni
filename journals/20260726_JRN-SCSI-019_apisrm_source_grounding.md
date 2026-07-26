<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-019
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-019 -- SOURCE GROUNDING (architect's pointer): file2dev in
#                 apisrm filesys.c is the booted_dev builder.  The
#                 EmulatR string is BYTE-CANONICAL; "@wwid%d" is the
#                 SCSI3 form; callback 0x22 = GET_ENV (corrects -013/
#                 -016's "open" reading).

    Doc id   : JRN-SCSI-019
    Date     : 2026-07-26
    Status   : SOURCE-ANALYSIS RECORD.  No emulator code changed.
    Origin   : Architect pointed at iovec occurrences in
               apisrm/ref/inet_driver.c; the payoff was next door in
               filesys.c / boot.c / apu_callbacks_def.h.
    Relates  : JRN-SCSI-018 (death site), -017 (tree), -016 (accept),
               -013 (branch), JRN-SCSI-011 (conversation), JRN-VMB-022.

--------------------------------------------------------------------------------
## 1. booted_dev is built by file2dev (apisrm/ref/filesys.c:2812)

  boot.c:1252  ev_write("booted_dev", dname, EV$K_STRING), dname from
  file2dev(fname, dname, protocol).  The format (filesys.c:~2894):

      sprintf(dname, "%s %d %d %d %d %d%s",
              fd->device, n[4], n[3], n[2], n[1], n[0], fd->suffix);

  SINGLE-space separators (the sample-comment's double space is
  alignment prettiness); numbers REVERSED from the dotted name; a
  table-driven 2-number suffix.  For dka0.0.0.8.0 with fd_table entry
  {"dk","pk",0,"SCSI"," 0 0"}: "SCSI 0 8 0 0 0 0 0" -- EXACTLY what
  EmulatR's console produced.  THE CONSOLE-SIDE STRING BUILDER IS
  DEFINITIVELY EXONERATED (kills JRN-SCSI-018 Sec 5 hypothesis (i)
  and JRN-SCSI-011 R4's branch (a)).

## 2. fd_table vs APB's ident-classify list

  fd_table protocols: SCSI, SCSI3, MSCP, MOP, BOOTP, DVA, RAID, IDE,
  I2O.  APB's classifier (0x2000e9d0, JRN-SCSI-013) accepts ONLY
  "DVA "/"RAID"/"SCSI"/"MSCP"/"FLOP".  IDE is a protocol the CONSOLE
  can name but THIS APB cannot classify -- JRN-VMB-022's "no IDE
  keyword in APB" is now source-grounded: IDE-form strings can never
  boot this APB.  (The identical IDE-vs-SCSI resolver footprint of
  JRN-SCSI-004 is unaffected: the classifier lives at 0x2000e9d0,
  OUTSIDE the 752-PC resolver window.)

## 3. "@wwid%d" -- the wwid alternative decoded at source level

  The FIBRE_CHANNEL branch appends the suffix
      " @wwid%d"   (evnum of the matching wwid filter)
  for SCSI3 (dg/KGPSA) devices.  This is verbatim the grammar's wwid
  tail (JRN-SCSI-017: '@' 0x0440 + 'w','w','i','d' + NUMBER matcher +
  the 0x81f5 terminal whose handler 0x2000e450 STORES the parsed N).
  The wwid alternative is the fibre-channel boot path; plain SCSI
  strings never take it.  Production B (f8-action pair replacing
  field 8) is plausibly the MOP/BOOTP form (MAC-address suffix
  "%02X-%02X-..." per file2dev's network branch) -- unconfirmed.

## 4. CORRECTION to -013/-016: callback 0x22 = GET_ENV

  apu_callbacks_def.h: getc=1 puts=2 ... open=16(0x10) close=17
  ioctl=18 read=19 write=20; set_env=32 reset_env=33 GET_ENV=34(0x22)
  save_env=35.  The accept path at 0x20003a3c-a74 (r16=#0x22) is a
  GET_ENV call, NOT open/IOVEC-creation.  Reading: on walk success
  APB fetches ANOTHER env variable (r17=0xa arg; for the wwid form,
  presumably "wwid<N>" using the stored N).  The true open (0x10)
  happens later still -- consistent with JRN-SCSI-011 (the failing
  boot never reaches open).  -013 Sec 2's and -016's "open/IOVEC"
  phrasing is WITHDRAWN in favor of get_env.

## 5. State of L1 after source grounding

  String canonical (Sec 1) + grammar fixed + VM deterministic + same
  walk passes on AXPBox leaves exactly ONE class of delta: RUNTIME-
  PRIMED STATE consumed by the walk -- the tokenizer descriptor /
  position cells and key records, populated from the get_env ANSWERS
  and the earlier env-walk phase.  Supporting observation: the
  descriptor at 0x2006aa60 post-mortem holds (0x200dfd40, pos=8,
  len=0x12) -- the walk died with cursor at offset 8, len correct at
  18.  The remaining question is WHY strtol was handed the offset-6
  position (JRN-SCSI-018's lag model) and whether the get_env answer
  transport (CRB block layout: status QW, value copy, NUL/length
  handling) primes those cells differently than real hardware.  Q2
  (DIAG window over strtol 0x2005e3a0-0x2005e6b0) remains the one-run
  decisive probe; the AXPBox R4 question is REFRAMED: not the string
  (now known canonical) but the GET_ENV ANSWER BYTES (value length /
  terminator) for booted_dev/boot_dev.

--------------------------------------------------------------------------------
## 6. Files touched

  - this journal   NEW
  No emulator code changed.
