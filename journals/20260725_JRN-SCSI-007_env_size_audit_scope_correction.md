<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-007
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-007 -- Size-aware EV audit (scope item B).  TWO env subsystems
#                 exist and srm_conformance measures the WRONG one for this
#                 bug; the firmware-path env state has NEVER been captured.
#                 SCSI-006 Sec 9.4's D15 re-rating is WITHDRAWN.

    Doc id   : JRN-SCSI-007
    Date     : 2026-07-25
    Status   : ANALYSIS RECORD + tooling.  No emulator code changed.
               Static only (no new runs).
    Relates  : JRN-SCSI-006 Sec 8/9 (callback ABI, the two gates), SCSI-005
               Sec 4.3 (the env diff planned there, still unexecuted).
    Sources  : Processor Support/PalcodeBitsavers/apisrm/apisrm/ref/boot.c,
               .../ref/call_backs.c, .../ref/apu_callbacks_def.h
    New tool : tools/env_size_audit.py

--------------------------------------------------------------------------------
## 1. SCOPE CORRECTION -- and a withdrawal

  EmulatR has TWO console environments, and they are not the same store:

    (a) SYNTHETIC console -- deviceLib/SRMConsole.cpp + deviceLib/SRMEnvStore
        .cpp (JSON-backed, defaults in initializeDefaults()).  Prompt is
        "EmulatR>>>".
    (b) REAL FIRMWARE -- DS20 SRM V7.3 executing as guest code; the EV table is
        the firmware's own, reached by ev_read()/ev_write().  Prompt is
        "P00>>>".

  Evidence for the split: tools/srm_conformance/samples/ds10_emulatr_20260717
  .log shows "EmulatR>>>" x17, while the NOIOVEC run of record
  (RelWithDebInfo/logs/run_ds20_p3retest_20260725_004639.log) shows "P00>>>"
  x4.  The VMB/APB boots -- every run in the whole NOIOVEC track -- are (b).

  CONSEQUENCE: tools/srm_conformance and its D15 finding
  ("boot_osflags format/default differs (real '0,0', emu '0')") measure
  subsystem (a).  Subsystem (a) is NOT what APB's callback reads.  So

      *** JRN-SCSI-006 Sec 9.4's call to re-rate D15 because it feeds the
          APB gate is WITHDRAWN.  It rests on a subsystem confusion. ***

  D15 remains a legitimate conformance delta worth fixing on its own merits
  (real hardware really does default boot_osflags to '0,0' -- see
  golden/ds10_v7_3.golden.log line 37), just not on this evidence, and not at
  raised severity.  The size-as-control-flow ARGUMENT from SCSI-006 Sec 9
  stands unchanged; only its application to D15 was wrong.

--------------------------------------------------------------------------------
## 2. What the console writes, and WHEN (primary source)

  apisrm ref/boot.c, in the `b` command path:

      ev_write( "booted_dev",     dname, EV$K_STRING );
      ev_write( "booted_file",    file,  EV$K_STRING );
      ev_write( "booted_osflags", flags, EV$K_STRING );
      printf( "base = %x, image_start = %x, image_bytes = %x\\n", ... );
      ...
      printf( "initializing HWRPB at %x\\n", hwrpb );

  Two things follow.

  2.1 booted_dev / booted_file / booted_osflags DO NOT EXIST before a boot is
      attempted.  An empty booted_osflags in a transcript taken at the prompt
      is CORRECT, not a defect -- the real DS10 golden log shows exactly that.
      Any audit that flags it pre-boot is crying wolf.

  2.2 On the failing run those three ev_write calls DID execute: EmulatR's P3
      log prints "base = 5bc000" (JRN-SCSI-004 Sec 2), which boot.c emits
      immediately AFTER them.  Whether they took EFFECT -- whether a
      subsequent ev_read/cb_get_env sees them -- is UNMEASURED.

--------------------------------------------------------------------------------
## 3. THE CAPTURE GAP (the actual result of scope item B)

  No run in the track has ever recorded the firmware-path EV state.  Greps for
  "osflags", "booted_dev" and "bootdef_dev" across
  run_ds20_p3retest_20260725_004639.log and its putty transcript return
  NOTHING.  SCSI-005 Sec 4.3 planned this diff; it was never executed.

  So scope item B cannot be COMPLETED from artifacts on disk -- the input does
  not exist yet.  What item B produced instead: the method, the tool, the
  per-EV buffer map, and the knowledge of which subsystem to point them at.

  The missing capture is also the cheapest experiment left in the whole track:
  no rebuild, no trace, no new instrumentation.  At the P00>>> prompt AFTER the
  NOIOVEC halt (or at the prompt of the same session before `b`, plus again
  after), issue:

      show boot*
      show booted*

  and keep the transcript.  Then run tools/env_size_audit.py on it.

  3.1 Reading the result

      booted_osflags ABSENT or size 0
          -> the ev_write was lost.  Caller A's gate 2 (low longword == 0)
             skips the resolver.  This is an EmulatR-side env fault; go to
             Sec 4 (backing store).
      booted_osflags = "0", size 1   (expected, given `-flags 0`)
          -> gate 2 passes.  The gate story moves to caller B and to the walk
             DATA; the mode is already known to be a constant (SCSI-006 Sec 8).
      booted_osflags size > 16
          -> gate 1 fires (cb_get_env returns 0x20000000) and the resolver is
             skipped SILENTLY.  Unlikely for osflags, but this is the one
             place where LENGTH ALONE kills the boot, so measure it, do not
             eyeball the printed text.

--------------------------------------------------------------------------------
## 4. Backing-store question this exposes (audit item A2, open)

  If Sec 3 shows the ev_write was lost, the next question is where V7.3 keeps
  the EV table and whether EmulatR backs it.  Known so far:

    - deviceLib/Tsunami/ToyRtc.h documents "G1b (PERSISTENCE): volatile CMOS,
      zero-initialized each cold boot", ports 0x70/0x71 only, with the
      ALi-specific 0x72/0x73 high-128 bank called out as not wired.
    - apisrm stores system EVs through nvram_read_sev / nvram_write_sev
      (ref/ev_action.c) against an EEROM abstraction (cp$src:eerom_def.h), and
      exposes cb_read_eerom / cb_write_eerom callbacks -- i.e. the EV table is
      an EEROM/flash resident structure on the platforms that have one, NOT
      the TOY CMOS scratch bytes.
    - env$m_flag_nvr (bit 16 of the EV descriptor) marks the NVRAM-backed EVs.
      booted_* are runtime state and are probably NOT nvr-backed, which would
      make them RAM-only and immune to a missing EEROM.  UNCONFIRMED for V7.3.

  So: a missing EEROM would explain lost `set` values across cold boots but
  would NOT obviously explain a lost booted_osflags inside one session.  Do
  not act on this until Sec 3 says there is something to explain.

--------------------------------------------------------------------------------
## 5. New tool -- tools/env_size_audit.py

  Parses "name<2+ spaces>value" lines out of any console transcript and reports
  BYTE SIZE as a first-class field, because size is a gate input (SCSI-006
  Sec 9.2).  Guardrails built in, each one earned by a wrong answer during
  development:

    - Per-EV caller buffer, not one global number: envid 8 -> 16 bytes
      (caller A, 0x200016a4 passes a3 = 0x10), everything else -> 256
      (caller B, 0x2000e328 passes 0x100).  A 27-byte boot_dev is harmless if
      nothing reads it into a small buffer; flagging it globally was noise.
    - Pre-boot empties are labelled "empty-ok" for the three EVs boot.c writes
      during `b` (Sec 2.1), not reported as defects.
    - Prompt-style detection with an explicit WARNING when the two transcripts
      come from different prompt families, so a synthetic-vs-firmware diff
      cannot be made by accident (Sec 1).

  Usage:
      python3 tools/env_size_audit.py <transcript>
      python3 tools/env_size_audit.py <golden> <candidate> [--buffer n] [--all]

  Run of record (the only comparison available today, and it is cross-subsystem
  -- the tool says so):
      golden/ds10_v7_3.golden.log (real DS10, Marvel>>>) vs
      samples/ds10_emulatr_20260717.log (EmulatR>>>)
      differences on boot-path EVs:
        boot_dev      real 'dka0.0.0.14.0 dkb0.0.0.15.0' (27 bytes) vs '' (0)
        boot_osflags  real '0,0' (3 bytes)               vs '0' (1)   <- D15

--------------------------------------------------------------------------------
## 6. Scope-item status (against SCSI-006 Sec 9.6)

    A  callback surface classification        OPEN
    A2 EV backing store for V7.3              OPEN (Sec 4; gated on Sec 3)
    B  size-aware EV fidelity                 METHOD + TOOL DONE; the
                                              measurement is BLOCKED on the
                                              Sec 3 capture, which does not
                                              exist on disk yet
    C  re-rate srm_conformance env deltas     PREMISE REVISED (Sec 1) -- do
                                              this against subsystem (a) only
    D  CSERVE vs PALcode tree                 OPEN

  6.1 Runbook note: the console driver used by these runs (srm_a1_driver.py,
      srm_telnet_driver.py) is scratchpad-only and is NOT in tools/, so the
      Sec 3 capture cannot be scripted from the tree as it stands.  Copying
      those two drivers into tools/ (JRN-SCSI-004 already suggested it for
      diagpc_footprint.py / p3_snapshot_strings.py) would make both this
      capture and the SCSI-006 Sec 8.4 widened-window run one-command
      reproducible.
