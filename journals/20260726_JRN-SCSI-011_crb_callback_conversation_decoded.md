<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-011
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-011 -- THE CRB CALLBACK CONVERSATION, CAPTURED AND DECODED.
#                 The run the whole L1 investigation was missing: APB asks
#                 the console exactly SIX things, never calls open, and the
#                 resolver's entire input is the topology string
#                 "SCSI 0 8 0 0 0 0 0" returned for booted_dev/boot_dev.
#                 The L1 question is now: what string does AXPBox return?

    Doc id   : JRN-SCSI-011
    Date     : 2026-07-26
    Status   : ANALYSIS RECORD + tooling.  No emulator code changed.
    Relates  : TASK-BOOT-001 Phase 4, JRN-SCSI-009 Sec 2 (the discriminator
               this run executes), JRN-SCSI-010 (L0 reopened), JRN-VMB-021
               (walk grammar: ident+7 fields -- MATCHED by this capture),
               JRN-VMB-019 (pattern-VM), JRN-SCSI-006 (mode=r19).
    Capture  : out/build/relwithdebinfo/run_ds20_showdev_20260725_181452.log
               (launcher run, EMULATR_DIAG_PCLO=0x101aa000 PCHI=0x101ac000
               CAP=20000; 5738 records -- UNDER cap, so the conversation is
               COMPLETE through the NOIOVEC message; boot ended NOIOVEC).
    Snapshot : snapshots/hold/predig_oemsnap_cyc1041483773.axpsnap (console
               structures + env values; era-equivalent for these fields).
    New tool : tools/crb_conversation_decode.py (capture -> named
               conversation; self-documenting header has the full method).

--------------------------------------------------------------------------------
## 0. The conversation (complete, in order)

      #    routine          answer
      000  get_env tty_dev          -> "0"
      001  code 0x07 (UNDEFINED)    -> CBS$FAIL (console error stub)
      002  set_term_int
      003  get_env booted_osflags   -> "0"
      004  get_env booted_dev       -> "SCSI 0 8 0 0 0 0 0"
      005  get_env boot_dev         -> "SCSI 0 8 0 0 0 0 0"
      006..085  40x (getc, puts) pairs = the 40-char NOIOVEC message
                emitted one character per puts ("%APB-F-NOIOVEC, Failed
                to create IOVEC" + CR/LF), a getc poll between writes.

  Facts this nails down:
  - APB NEVER calls open (0x10), read (0x13), or ioctl (0x12).  The IOVEC
    is not lost in device I/O -- APB gives up BEFORE touching the device.
    (open is the call that would fill the chan_desc whose boot_read/
    boot_write/boot_ioctl pointers ARE the IOVEC material -- apisrm
    apu_callbacks_def.h struct chan_desc.)
  - The resolver's ENTIRE device input is the one string
    "SCSI 0 8 0 0 0 0 0" -- ident + 7 fields, exactly the production
    JRN-VMB-021 decoded from the walk transcript.  The 0xf3-tail gate
    (JRN-VMB-021: dies at field 2) is judging THIS string and nothing else.
  - booted_osflags arrives healthy ("0"), consistent with JRN-SCSI-007.
  - Call #001 passes function code 0x07, which is UNDEFINED in this
    console's dispatch (table gap 0x07..0x0f between process_keycode and
    open, apu_callbacks_def.h) and lands in the unsupported-code stub
    returning CBS$FAIL (r0 = 1<<63).  This is CONSOLE-ARCHITECTURE
    behavior, identical on real firmware of this vintage -- presumed
    benign probe; verify via the AXPBox diff (Sec 3).

--------------------------------------------------------------------------------
## 1. Why this closes JRN-SCSI-009 Sec 2's open question

  Every prior capture began at 0x20095840 (inside the resolver), so WHAT
  the console answered had never been observed.  JRN-SCSI-009 reasoned
  "what separates accept from probe-exit is DATA returned by / stored
  around the CRB callbacks".  That data is now enumerated: three env
  strings and one failed 0x07 probe.  Two are trivial ("0", "0").  The
  discriminating datum is the TOPOLOGY STRING.  Either AXPBox's console
  answers a string EmulatR's does not (format/field difference -> fix the
  EmulatR console-side producer of boot_dev/booted_dev), or it answers the
  SAME string and the divergence is inside APB's pattern-VM state -- which
  the JRN-SCSI-004 footprint identity already argues against for the code
  path, leaving the string as the prime suspect either way.

--------------------------------------------------------------------------------
## 2. Dispatch internals (decoded from the live DS20 v7.3-2 firmware)

  dispatch 0x101aac60: save 18 regs to SP-0x88; CMPULE r16,#0x36 bounds
    check; BR r0,.+0x200 (r0 := table base 0x101aacb8).
  continuation 0x101aaeb8: SLL r16,#3; LDL off,(0x101aacb8 + code*8);
    ADDQ; JMP.  THE TABLE LOAD'S memAddr NAMES THE CODE:
        code = (memAddr - 0x101aacb8) / 8
    (validated: get_env segs load 0x101aadc8 = code 0x22; set_term_int
    0x101aacd8 = 0x04; call #001 0x101aacf0 = 0x07).
  handlers (DS20 v7.3-2): getc 0x101ab048, puts 0x101ab068, reset_term
    0x101ab0b0, set_term_int 0x101ab0c8, set_term_ctl 0x101ab108,
    open/close/ioctl/read/write/save_env common entry 0x101ab118,
    set_env 0x101ab2b0, reset_env 0x101ab3b0, get_env 0x101ab430,
    process_keycode 0x101ab4f8, code 0x30 0x101ab528; ALL other codes ->
    error stub 0x101aaed0 (r0 = 1<<63 = CBS$FAIL, restore, ret 0x101aaf20).
  get_env 0x101ab430: a1(r17)=env ID, walks the {desc_ptr, id} list at
    0x101ab130 (stride 0x10, sentinel 0x101ab2b0); matched desc +0x3c =
    size, value bytes follow a 4-byte length at the desc's data block.
  CRB VA->PA map (2 entries, read from CRB+0x30):
        VA 0x10000000 -> PA 0x00002000  0x2dd pages
        VA 0x105ba000 -> PA 0x3ff02000  0x7f  pages   (env storage lives
        here: booted_dev value bytes at VA 0x105da41c = PA 0x3ff2241c).
  All of the above is mechanized in tools/crb_conversation_decode.py --
  one command re-derives this journal's Sec 0 from any CRB-window run:
      python3 tools/crb_conversation_decode.py <run.log> <snapshot>

--------------------------------------------------------------------------------
## 3. Next (runbook R4, the FIRST-DIVERGENT-ANSWER diff)

  Obtain the same six answers from AXPBox ES40 booting the same
  dka0.vdisk to its (successful) APB run:
  - minimum: its console's `show boot_dev` / `show booted_dev` (the
    topology-string forms) + osflags;
  - better: an instrumented/traced run capturing its CRB get_env returns.
  Then diff against Sec 0.  Expected shapes:
    (a) strings differ (field semantics: hose/slot/channel/ID/LUN...) ->
        the EmulatR DS20 console-side string builder (or the platform
        topology it reflects: slot 8 etc.) is the L1 gap; fix there.
    (b) strings identical -> instrument the pattern-VM comparison sites
        (JRN-VMB-019's 0x158284 window) with the string now known, and
        re-read the 0xf3-tail gate bit-10 birth (JRN-SCSI-005) against
        the actual field bytes.
  Also confirm on AXPBox: code-0x07 probe fails there too (expected).

--------------------------------------------------------------------------------
## 4. Files touched

  - tools/crb_conversation_decode.py   NEW (durable decoder)
  - this journal                       NEW
  No emulator code changed.
