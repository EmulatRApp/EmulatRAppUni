<!--
EmulatR V4 -- 82077AA FDC: floppy enumeration bring-up (DESIGN SPEC)
Project: EmulatR (Alpha 21264 / EV6-EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic (claude.ai web).
Written 2026-06-22.  This is a DESIGN SPEC, not generated code.  Cowork holds
live file access and is the source of truth for current file state; Cowork
turns this into diffs, verifies line numbers, the snapshot/build, and the
super-I/O path.  Discuss-before-code applies.  ASCII(128) only.  Per ADR-0001
this HTML comment stands in for the source attribution block on this spec.
-->

================================================================================
COWORK SPEC -- 82077AA FDC: clear the dva0 powerup option-firmware scan
================================================================================

GOAL
  The console now reaches the powerup option-firmware scan and stalls on
  "Checking dva0.0.0.0.0 ..." until the retry timeout.  Scope of THIS work:
  make the floppy subsystem ENUMERATE -- reset, init, recalibrate, media
  detect-as-empty -- so the console concludes "no media" and proceeds.  This
  is firmware enumeration, NOT data transfer.  No sector reads, no DMA engine.

ALREADY IMPLEMENTED (do not redo)
  DOR reset/release, IRQ6 after reset, MSR RQM, SPECIFY (0x03),
  PERPENDICULAR MODE (0x12), drive-select / motor control, command FIFO.

--------------------------------------------------------------------------------
0.  THE ONE CORRECTION THAT MUST LAND -- two completion paths, not one
--------------------------------------------------------------------------------
The 82077 has TWO command-completion mechanisms.  Conflating them desyncs the
controller the moment media detection runs.  Split the dispatch by path:

  PATH A -- no result phase; cleared by SENSE INTERRUPT STATUS:
      RECALIBRATE (0x07), SEEK (0x0F).
      -> assert IRQ6, produce ZERO result bytes, InterruptPending=TRUE.
      -> firmware issues SENSE INTERRUPT STATUS (0x08) to read ST0+PCN and
         clear the interrupt.

  PATH B -- 7-byte result phase; cleared by READING the result phase:
      READ ID (0x4A), READ DATA, WRITE DATA.
      -> assert IRQ6, push 7 result bytes (ST0,ST1,ST2,C,H,R,N) to ResultFIFO,
         set MSR.DIO=1.  The interrupt clears when the host READS the result
         phase (last byte out of 0x3F5).  NOT via SENSE INTERRUPT.
      -> SENSE INTERRUPT STATUS issued after a Path-B command returns the
         invalid-command status ST0 = 0x80 (IC = 10b) on real silicon.  Model
         that; do not let it silently "succeed."

The draft's INTERRUPT MODEL section listed READ ID/READ DATA/WRITE DATA under
the SENSE-INTERRUPT model -- that is the bug.  Branch on Path A vs Path B.

--------------------------------------------------------------------------------
1.  RESOLVE-BEFORE-WIRE GATES  (verify-before-decode; do NOT guess these)
--------------------------------------------------------------------------------
These three are confirmed against an actual SRM floppy-init trace BEFORE the
matching code is wired.  A wrong assumption here re-creates the IIC-class hang.

  G1. POST-RESET POLLING INTERRUPT  [_PROVISIONAL until trace-confirmed].
      Textbook 82077: after reset with polling enabled (default), the
      controller posts an interrupt and firmware issues SENSE INTERRUPT STATUS
      FOUR times, getting ST0 = 0xC0,0xC1,0xC2,0xC3 (IC=11b, one per logical
      drive).  The draft's expected trace shows reset -> IRQ6 -> SPECIFY with NO
      4x poll, which implies EITHER the SRM ignores the post-reset interrupt OR
      issues CONFIGURE (0x13) with POLL disabled first.  This is the single
      likeliest divergence point.
      ACTION: capture the real SRM floppy-reset sequence (EMULATR_FDC_TRACE on a
      run that reaches the dva0 scan).  If the SRM does the 4x poll, the single
      ST0=0x20 model is WRONG and the post-reset state must emit 0xC0..0xC3.
      Mark the post-reset behavior _PROVISIONAL in code until this is settled.

  G2. UNHANDLED INIT COMMANDS.
      Confirm from the same trace whether the SRM issues CONFIGURE (0x13, 3
      param bytes, sets POLL/FIFO), VERSION (0x10 -> result 0x90 for 82077), or
      DUMPREG during init.  Any opcode the FIFO does not recognize MUST return
      invalid-command (ST0=0x80), never stall -- an unrecognized opcode that
      hangs the FIFO is exactly the IIC-poll failure mode again.

  G3. SUPER-I/O PATH.
      The FDC ports 0x3F0-0x3F7 are reached THROUGH the FDC37C669 super-I/O.
      Confirm the FDC logical device is enabled and addressed in the super-I/O
      config before these accesses land.  NOTE: the prior 16-bit index+data
      write-fold bug lived on this super-I/O config path and previously broke
      floppy enumeration -- if it is still latent there, the cleanest FDC model
      will not see the writes correctly.  Verify width handling on that path.

--------------------------------------------------------------------------------
2.  COMMAND SPEC  (hex dispatch labels; Path tag in brackets)
--------------------------------------------------------------------------------
Dispatch switch uses hex case labels only (convention).  Edit shape per command
in FILE/FUNCTION/CHANGE form once [LOCATE]d in the live tree.

  0x07  RECALIBRATE            [Path A]
        params: drive byte.  CurrentCylinder=0, Head=0, InterruptPending=TRUE,
        assert IRQ6.  No result bytes.  (Succeeds regardless of media -- the
        mechanism reaches track 0; this is what lets "empty drive" enumerate.)

  0x08  SENSE INTERRUPT STATUS [returns 2 bytes]
        ST0 = 0x20 | (head << 2) | drive_select   (NOT hardcoded 0x20 -- compute
              it so a non-zero drive returns correct status).  [_PROVISIONAL:
              see G1 -- post-reset case may need 0xC0..0xC3.]
        byte1 = CurrentCylinder (PCN), normally 0.
        After BOTH bytes read: InterruptPending=FALSE, clear IRQ6.
        If no Path-A interrupt is pending: return ST0=0x80 (invalid).  REQUIRED;
        without it SRM waits forever.

  0x0F  SEEK                   [Path A]
        params: drive, cylinder.  CurrentCylinder=cylinder, InterruptPending=
        TRUE, assert IRQ6.  No result bytes.  Completion via SENSE INTERRUPT.

  0x4A  READ ID                [Path B]
        params: drive/head byte.  Empty drive (no media): abnormal termination
        -- ST0 with IC=01b and ST1 missing-address-mark.  Exact empty-drive ST0/
        ST1 encoding is [_PROVISIONAL] until G1/G2 trace confirms what the SRM
        tests; the draft's ST0=0x40 is a reasonable abnormal-termination value
        but verify against datasheet + trace before it drives the media decode.
        With media attached: real 7-byte result (deferred -- no media path now).

  0x04  SENSE DRIVE STATUS     [returns 1 byte ST3]
        ST3 bits: bit6 WP (write-protect), bit5 RDY (ready), bit4 T0 (track 0),
        bit2 HD (head), bits1:0 DS (drive sel).  Minimum: set T0, RDY, WP per
        modeled drive; remaining bits 0.

State the result-byte COUNT explicitly per command in code (2 for 0x08, 1 for
0x04, 7 for Path B) -- a wrong count is the classic FIFO desync.

--------------------------------------------------------------------------------
3.  MAIN STATUS REGISTER (0x3F4) contract
--------------------------------------------------------------------------------
  bit7 RQM : host may read/write a byte.  Set when ready.
  bit6 DIO : 0 = host writes command/params; 1 = host reads results.
  bit5 NDM : non-DMA (execution) -- 0 during enumeration; see section 6.
  bit4 CB  : controller busy -- set while a command executes.
  bits3:0  : per-drive busy -- may remain 0 during bring-up.
Transitions to honor: command phase RQM=1/DIO=0; while executing CB=1; result
phase RQM=1/DIO=1 until ResultFIFO drained, then DIO=0/RQM=1/CB=0.

--------------------------------------------------------------------------------
4.  STATE STRUCT + RESULT FIFO  (std only -- no Qt)
--------------------------------------------------------------------------------
ResultFIFO is std::deque<uint8_t> (or std::queue<uint8_t,std::deque>).  No Qt
container.  This whole FDC model is pure std -- flag explicitly that NO Qt seam
is introduced here (Qt is reserved for the named threading seams only; this is
not one).  Reading 0x3F5 pops one byte; empty -> DIO=0, RQM=1.

  State: CurrentCylinder, CurrentHead, SelectedDrive, MotorEnabled, Busy(CB),
         InterruptPending, RQM, DIO, ST0, ST1, ST2, ST3, ResultFIFO.

--------------------------------------------------------------------------------
5.  LOGGING  (CMake compile guard + runtime env gate -- convention)
--------------------------------------------------------------------------------
All trace output (the W 3F2=.. / R 3F5=.. lines) goes through LogSubsystem
behind a CMake compile guard, runtime-gated by env EMULATR_FDC_TRACE, expanding
to ((void)0) in Release with the runtime mute knob.  No raw prints in command
handlers.  Same idiom as EMULATR_IIC_WATCH / EMULATR_GCT_WATCH (compile guard
distinct from the runtime env var).

--------------------------------------------------------------------------------
6.  OUT OF SCOPE  (deferred; do not build here)
--------------------------------------------------------------------------------
  - DMA engine (ISA channel 2).  Enumeration needs none.  The draft's "transfer
    directly between image buffer and host memory" is a fiction that is harmless
    ONLY while READ DATA never executes (true for the empty-drive scan).  When
    real sector reads land, decide PIO/non-DMA (MSR.NDM=1 during execution, host
    reads 0x3F5 per byte -- the deterministic-friendly path) vs ISA DMA ch2, and
    reconcile with the MSR.NDM contract.  TODO(fdc-datapath) the seam.
  - Real media result phase for READ ID / READ DATA / WRITE DATA.
  - Motor spin-up delay, rotational latency, index pulse, CRC, sector timing.
    Immediate completion is acceptable for bring-up (single-threaded
    deterministic model); name this as a fidelity shortcut in the header block.

--------------------------------------------------------------------------------
7.  CONVENTIONS CHECKLIST  (apply per ADR-0001 + standing rules)
--------------------------------------------------------------------------------
  - ASCII(128) only; ADR-0001 attribution header on the new source/header.
  - Include guard, never #pragma once: pattern <DIR>_<FILE>_H, e.g.
    [CONFIRM lib] FDCLIB_FDC82077_H  (resolve the actual lib/dir in tree).
  - Hex case labels throughout the dispatch (0x07/0x08/0x0F/0x4A/0x04/0x13/0x10).
  - doctest CHECK only (no REQUIRE).  Do not name an ST-decoder helper toString;
    use st0Name()/operator<< if a printable helper is added.
  - Header block + inline comment at every changed line (FILE/FUNCTION/CHANGE).
  - _PROVISIONAL on every ST0/ST1/ST3 value and the post-reset behavior until
    confirmed against the 82077AA datasheet AND the real SRM trace -- data-
    fidelity gate: a provisional status code must not drive the media decode.
  - TODO discipline: header TODO table + // TODO(fdc-datapath) / TODO(fdc-poll)
    at the call sites, removed in the edit that lands each.

--------------------------------------------------------------------------------
8.  ACCEPTANCE  (deliverables of this work)
--------------------------------------------------------------------------------
  1. Two-path completion model implemented (Path A SENSE-INTERRUPT; Path B
     result-phase), with per-command result counts.
  2. G1/G2/G3 resolved from a real SRM trace BEFORE wiring; post-reset behavior
     matches the trace (single-0x20 vs 0xC0..0xC3 4x poll).
  3. SENSE INTERRUPT STATUS (0x08), SENSE DRIVE STATUS (0x04), READ ID (0x4A)
     empty-drive path, with computed ST0.
  4. doctest unit (CHECK) driving reset -> SPECIFY -> PERPENDICULAR ->
     RECALIBRATE -> SENSE INTERRUPT, asserting MSR (RQM/DIO/CB) transitions and
     ST0/PCN; second case: empty-drive READ ID -> not-ready.
  5. Console completes the dva0 floppy probe and proceeds past the scan with no
     timeout.  Capture the post-scan console output.

--------------------------------------------------------------------------------
9.  EXPECTED TRACE  (corrected -- subject to G1)
--------------------------------------------------------------------------------
  W 3F2 = 08   reset asserted
  W 3F2 = 0C   reset released
  IRQ6
  [G1] post-reset poll: EITHER none (SRM ignores / POLL disabled) OR 4x
       SENSE INTERRUPT returning ST0 0xC0..0xC3 -- CONFIRM, do not assume none
  W 3F5 = 03   SPECIFY
  W 3F5 = 12   PERPENDICULAR MODE
  W 3F2 = 1C   motor on
  W 3F5 = 07   RECALIBRATE        [Path A]
  IRQ6
  W 3F5 = 08   SENSE INTERRUPT STATUS
  R 3F5 = 20   ST0  (= 0x20 | head<<2 | drive)
  R 3F5 = 00   PCN
  W 3F5 = 4A   READ ID            [Path B] -> empty-drive not-ready -> "no media"
  ... controller ready; console concludes no floppy media and continues boot

End of spec.
