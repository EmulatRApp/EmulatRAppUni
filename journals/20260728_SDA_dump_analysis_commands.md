<!--
EmulatR V5 -- Analysis Runbook
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1
ASCII(128) only.  Hex radix.
-->

# SDA RUNBOOK -- INVEXCEPTN crash dump (2026-07-28 DS20 run)

    Doc id   : RUNBOOK-SDA-001
    Date     : 2026-07-28
    Purpose  : Extract, from the VMS crash dump written by the DS20 run,
               the ONE datum the emulator-side logs cannot produce: the
               UNDERLYING exception that bugcheck 000001CC (INVEXCEPTN)
               wraps.
    Context  : JRN-AUD-001 Sec 5b.  EmulatR delivered ZERO non-TB faults
               in the whole OS window (only kFaultDtbMissDouble, all
               resolved), so the exception was posted by the guest PAL or
               synthesized by the exec -- invisible to logs/faults.log.

--------------------------------------------------------------------------------
## 0. WHY EACH COMMAND IS HERE

  Every command below answers a specific open question.  If time is
  short, commands 1, 2 and 6 carry almost all the value.

    Q-A  What exception actually occurred?      -> 1, 2, 3
    Q-B  Where (which exec routine/instr)?      -> 4, 5
    Q-C  At what IPL / in what mode?            -> 1 (PS field), 7
    Q-D  Was the faulting page REALLY invalid?  -> 6   <-- discriminator
    Q-E  How did we get there (call chain)?     -> 3

  Q-D is the sharpest test we have.  If the exception is an ACCVIO and
  VMS's OWN page tables say the faulting VA is valid and accessible,
  then EmulatR faulted on a page the OS considers good -- an emulator
  MMU/TB defect, and the hunt goes to the translator.  If VMS's tables
  say the page is genuinely invalid, the fault is legitimate and the
  defect is UPSTREAM (whatever produced the bad pointer or failed to
  build the mapping), which points instead at the exec-init path.

--------------------------------------------------------------------------------
## 1. OPEN THE DUMP -- AND CAPTURE EVERYTHING

  Run SDA on a WORKING OpenVMS V8.3 Alpha (AXPBox boots this same media
  per JRN-SCSI-005 A4, and is acceptable here as a corroborative tool --
  we are reading a file, not taking it as an authority over EmulatR).

      $ ANALYZE/CRASH SYS$SYSTEM:SYSDUMP.DMP

  If the dump went to the pagefile instead:

      $ ANALYZE/CRASH SYS$SYSTEM:PAGEFILE.SYS

  FIRST COMMAND INSIDE SDA -- do this BEFORE anything else so the whole
  session is captured to a file that can be handed back verbatim:

      SDA> SET OUTPUT DKA0:[000000]EMULATR_DUMP_ANALYSIS.TXT

  (Everything after this is written to that file AND may stop echoing to
  the terminal -- that is expected.  Command 11 turns it back off.)

  IF SDA CANNOT OPEN THE DUMP: stop and report that.  A corrupt or
  truncated dump is itself a finding -- it would indict EmulatR's SCSI
  WRITE path, which nothing has exercised at this scale before.

--------------------------------------------------------------------------------
## 2. THE COMMANDS, IN ORDER

  1. SHOW CRASH
       THE headline.  Bugcheck type, crash CPU, the exception PC, the PS,
       and the signal/mechanism arrays that NAME the exception (ACCVIO,
       ROPRAND, illegal instruction, ...).  Capture every line, including
       the register dump -- do not summarize it.

  2. CLUE CRASH
       The CLUE extension's digest of the same event.  Often decodes the
       signal array into readable text and reconstructs the stack when
       SHOW CRASH's frame is terse.  If CLUE is not installed, skip.

  3. SHOW STACK
       The stack at the crash.  With "no current process defined" this is
       the interrupt/kernel stack -- the call chain into the fault, which
       is how we learn which EXE$INIT-era routine was running.
       Follow with:
           SHOW CALL_FRAME
       and, if it reports further frames:
           SHOW CALL_FRAME/NEXT      (repeat until it stops)

  4. MAP <exception-PC>
       Substitute the PC printed by command 1.  Maps it to image +
       module + offset -- i.e. NAMES the routine.  Example shape:
           SDA> MAP FFFFFFFF.8009D0EC

  5. EXAMINE/INSTRUCTION <PC-40>:<PC+20>
       Disassemble a window around the exception PC so the faulting
       instruction and its setup are visible.  Example shape:
           SDA> EXAMINE/INSTRUCTION FFFFFFFF.8009D0AC:FFFFFFFF.8009D10C
       If the range form is rejected, use the single-address form and
       repeat:
           SDA> EXAMINE/INSTRUCTION FFFFFFFF.8009D0EC

  6. SHOW PAGE_TABLE/VA=<faulting-VA>          <-- THE DISCRIMINATOR
       ONLY if command 1 reports an ACCVIO/access-violation style
       exception; the faulting VA appears in the signal array.
       This prints VMS's own PTE for that VA.  Report the full line:
       valid bit, protection field, PFN, and any GH/ASM bits.
       See Sec 0 Q-D for how the two possible answers split the hunt.

  7. SHOW CPU
       Per-CPU state including the current IPL and mode -- confirms
       "above ASTDEL" and tells us WHICH IPL, which discriminates the
       interrupt-substrate theory (audit PE-4/PE-5) from a plain
       faulting instruction.

  8. SHOW SUMMARY
       Process list.  Expected to be empty/minimal ("No current process
       defined in CPU database" was on the console), which would confirm
       we die in EXE$INIT before the null/swapper process exists -- and
       would also confirm ASN is still 0, making today's DTB_ASN fix
       inert for THIS failure.

  9. SHOW EXCEPTION_FRAME
       Explicit exception frame, if command 1 did not already print it
       in full.

 10. SHOW MACHINE_CHECK
       Only if command 1 hints at a machine check.  Expected to be
       nothing -- EmulatR raised no MCHK -- but cheap to rule out.

 11. SET OUTPUT SYS$OUTPUT
       Closes the transcript file.  Then EXIT.

--------------------------------------------------------------------------------
## 3. WHAT TO SEND BACK

  The transcript file from Sec 1 is sufficient -- it contains all of the
  above.  If sending selectively, the minimum useful set is:

    - the COMPLETE output of SHOW CRASH (registers included)
    - the exception PC, the PS value (raw hex -- do not pre-decode), and
      the signal array
    - the MAP result for the exception PC
    - the SHOW PAGE_TABLE line, if command 6 applied

  Raw hex is preferred over interpretation everywhere: PS bit fields and
  signal-array conventions are exactly the sort of thing worth decoding
  against the AARM rather than trusting a summary.

--------------------------------------------------------------------------------
## 4. OPERATIONAL NOTES

  - If DUMPSTYLE routes dumps to PAGEFILE.SYS, the dump must be copied
    out (SDA COPY) before the pagefile is reused, or it is lost on the
    next boot.  SYS$SYSTEM:SYSDUMP.DMP is the sturdier target if the
    dump will be analyzed more than once.
  - The dump was written BY the crashing system THROUGH EmulatR's
    emulated SCSI write path.  A clean SDA read therefore validates that
    write path end-to-end -- a gate nothing else in the campaign has
    exercised at this volume.  Note whether SDA reports any consistency
    complaints even if it opens successfully.
