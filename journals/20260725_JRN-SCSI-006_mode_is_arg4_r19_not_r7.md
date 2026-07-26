<!--
EmulatR V5 -- Implementation Journal JRN-SCSI-006
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
-->

# JRN-SCSI-006 -- The resolver MODE is ARGUMENT 4 in R19 and DEFAULTS TO 1
#                 when the caller passes 3 args.  R7 is merely callee-saved:
#                 the "R7 = mode" attribution in VMB-021 / SCSI-005 is a
#                 MISREAD.  Every statically reachable call site passes 3.

    Doc id   : JRN-SCSI-006
    Date     : 2026-07-25
    Status   : ANALYSIS RECORD.  No emulator code changed.  Static only
               (one snapshot read; no new runs).
    Relates  : JRN-SCSI-005 Sec 3 (the "NEXT STATIC STEP" this closes),
               JRN-SCSI-004 Sec 3/5, JRN-VMB-021 Sec 0/3 (the R7 premise
               corrected here), JRN-VMB-022.
    Snapshot : out/build/relwithdebinfo/snapshots/
               auto_halt_1785006537_2027331327.axpsnap
               (DS20, 4 GiB, cyc 2,027,331,327 -- the SCSI-run NOIOVEC halt
               of JRN-SCSI-004 Sec 2; PA 0 @ file 0x3df4)
    New tool : tools/snap_va_disasm.py (durable; replaces the scratchpad
               snap_extract.py for image-region work)

--------------------------------------------------------------------------------
## 1. Method unblock: the image is ONE linear window, no page walk needed

  Reading PA == VA in the 0x2000xxxx range returns ALL ZEROS, which reads as
  "not mapped" and is what motivated a page-table walk.  It is not unmapped:
  the snapshot payload is guest-PHYSICAL, and the bootstrap image executes at
  VA 0x20000000+ while the loader placed it at the PHYSICAL base the console
  prints on the boot line -- "base = 5bc000" for this 4 GiB DS20.  One delta
  covers the whole image:

      PA = VA - 0x20000000 + 0x5bc000            (image size 0x99400)

  Anchor check (JRN-SCSI-005 Sec 3 quoted 0x20096d30/34 as the bit-10 gate):

      20096d30: 4a815694  SRL   r20,#0xa,r20
      20096d34: f2800022  BLBS  r20, .+0x88        <- bit 10 of the token word

  Byte-exact match, so the delta is right and every VA in the image is now
  disassemblable without the APB.EXE file-offset skew.  NOTE: this applies to
  the IMAGE region only.  Stack / VA-only regions (JRN-SCSI-004 Sec 3 item 4)
  still need the page table at 0x3ff04000.

--------------------------------------------------------------------------------
## 2. Every call site of the resolver module entry 0x20095840

  Scan of the image for BR/BSR whose computed target is 0x20095840
  (`snap_va_disasm.py <snap> 0 --find-bsr 0x20095840`):

      20001744  BSR  r26        <- outer caller A
      2000df90  BR   r31        <- tail jump
      2000dfd8  BR   r31        <- tail jump
      2000e420  BSR  r26        <- outer caller B (the 0x2000e5xx neighbourhood
                                   named in SCSI-005 Sec 3)
      200964fc  BSR  r26        <- known internal recursion (VMB-022 footprint)
      20097358  BSR  r26        <- inside the ACCEPT region

  0x2000e5d0 itself is NOT the caller.  It decodes as an OpenVMS bound-procedure
  transfer stub (LDQ r0,-0xb8(r27) / chase / LDQ r27,0x10(r0) / LDQ r28,0x8(r27)
  / JMP (r28)) -- linkage, no mode.

--------------------------------------------------------------------------------
## 3. The prologue names the mode input (decisive)

      20095840: LDA     r30,-0xb0(r30)      ; frame
      20095844: AND     r25,#0xff,r25       ; r25 = AI, low byte = ARG COUNT
      20095848: STQ     r26,0x30(r30)
      2009584c: STQ     r27,0x0(r30)
      20095850: CMPLT   r25,#0x4,r25        ; r25 = (argcount < 4)
      20095854: STQ     r2,0x38(r30)        ; ... callee-saved block r2..r15
      20095868: STQ     r7,0x60(r30)        ; <<< R7 IS SAVED HERE == callee-
                                            ;     saved.  It is NOT an input.
      2009588c: STQ     r29,0xa8(r30)
      20095890: LDQ_U   r0,0x0(r16)         ; r16 = a0 = descriptor/key record
      20095898: BIS     r31,r30,r29         ; fp
      2009589c: BIS     r31,r16,r3          ; r3 = a0  (the 0xf3 tail's base)
      200958a0: CMPEQ   r19,#0x0,r16        ; r16 = (a3 == 0)
      200958ac: BIS     r25,r16,r16         ; (argcount<4) OR (a3==0)
      200958b4: CMOVNE  r16,#0x1,r19        ; <<< THEN MODE r19 = 1
      200958c0: BIS     r31,r27,r2          ; r2 = module/PD base ([r2+0x58] etc)
      200958c4: BIS     r31,r18,r4          ; r4 = a2
      200958c8: STQ     r19,0x20(r30)       ; mode parked at fp+0x20

  So the resolver takes FOUR arguments and argument 4 (R19) is the MODE, with a
  hard default of 1 -- "probe only" -- whenever the caller passes fewer than
  four, or passes a zero.  R7's first use inside the module is 0x20095a48
  (AND r5,#0x1,r7): a scratch temp, long after the save.  The observed
  "R7 = 1 throughout" in the A2 replay is therefore the CALLER's preserved r7
  coinciding with the value, not the mode input.

  Corrects: JRN-VMB-021 Sec 0 item 5 / Sec 3.3 ("MODE a2=1 (R7=1 throughout)")
  and the JRN-SCSI-005 Sec 3 next-step framing ("where r7 (mode) is loaded
  from").  The register to follow is R19, and its origin is the ARG COUNT in
  R25 at the call site.

--------------------------------------------------------------------------------
## 4. Both outer call sites pass THREE arguments

  Caller B, immediately before 0x2000e420:

      2000e3ec: LDA     r16,0x1b4(r3)       ; a0
      2000e3f0: LDA     r19,0x28(r29)       ; a3 <- a STACK ADDRESS (out slot?)
      2000e3fc: BIS     r31,#0x3,r25        ; <<< AI = 3
      2000e400: LDA     r27,0x1e30(r2)      ; PD
      2000e408: LDL     r18,0x58(r2)        ; a2
      2000e40c: LDL     r17,0x60(r2)        ; a1
      2000e420: BSR     r26, 0x20095840

  Caller A, immediately before 0x20001744:  BIS r31,#0x3,r25 at 0x20001700,
  a0 from r23 = [r2+0x20], a1 = [r2+0x30], a2 = [r2+0x38].  AI = 3 again.

  With AI = 3 the prologue OVERRIDES whatever r19 held, so caller B's
  LDA r19,0x28(r29) is discarded -- note the 0xf3 tail then reads
  LDL r1,0x28(fp) as its "result slot" (SCSI-005 Sec 3), which is consistent
  with 0x28 being an out-parameter offset the 3-arg form never activates.

  CONSEQUENCE: on this path the resolver is entered in probe-only mode by
  CONSTRUCTION, from a static AI literal.  Nothing about the DS20 console, the
  env vars, the HWRPB, or the load base can change a `BIS r31,#0x3,r25`.

--------------------------------------------------------------------------------
## 5. What this does to the SCSI-005 hypothesis ranking

  DEMOTED (not killed): C1 env-var formats, C2 HWRPB content, C3 DS20-vs-ES40
  console identity, C4 load base.  None of them can flip an immediate AI
  literal.  They can still steer WHICH call site runs, which is now the
  question.

  PROMOTED to the open question: what selects a 4-argument (explicit-mode)
  entry?  One concrete candidate is already visible 0x30 bytes earlier, in the
  SAME caller-B body:

      2000e3b0: LDA     r19,0x100(r31)      ; a3 = 0x100  (an EXPLICIT mode)
      2000e3b4: BIS     r31,#0x4,r25        ; AI = 4
      2000e3cc: LDL     r27,0x20(r29)       ; PD from the frame
      2000e3d0: LDQ     r26,0x8(r27)
      2000e3e0: JMP/JSR r26,(r26)           ; INDIRECT -- target not static

  RESOLVED IN SEC 8 (same session): the indirect call is NOT a second entry to
  the resolver -- it is the console CALLBACK dispatch (a0 = 0x22), and the mode
  turns out to be a CONSTANT.  Read Sec 8 before acting on this paragraph.
  Original wording kept for the record: if that indirect call also lands on
  0x20095840, then APB has a 4-arg "execute" entry with mode 0x100 and a 3-arg
  "probe" entry, and the boot outcome is decided by which one is taken -- e.g.
  BNE r1,.+0x10 at 0x2000e410 (r1 = bits 0x1d..0x1f of the prior call's status
  in r0) or BNE r18,.+0x2c at 0x20001724.  UNPROVEN: r26/r27 are frame-loaded,
  so this needs a live read or a trace, not static bytes.

--------------------------------------------------------------------------------
## 6. Next steps (revised)

  6.1 TRACE THE CALL SITE (cheap, decisive).  Re-run the P3 boot with the
      DIAG-PC window WIDENED to cover the callers, not just the module:
      EMULATR_DIAG_PCLO=0x20001700 PCHI=0x20099000.  The current window
      (0x20095840-0x20099000, VMB-022/SCSI-004) starts INSIDE the callee and
      structurally cannot show which of the six sites ran, nor whether
      0x2000e3e0 fired.  Expect the answer in one run.
  6.2 RESOLVE 0x2000e3e0's target from that trace (retire PC after the JSR).
  6.3 If the 4-arg entry exists and is skipped, walk back from the branch that
      skipped it -- THAT is where an EmulatR-side input can legitimately be at
      fault, and it is where C1/C2 re-enter.
  6.4 The A4.1 ES40 confound control (SCSI-005 Sec 4.1) is still worth running
      but is no longer the lead: it can only matter via 6.3.

--------------------------------------------------------------------------------
## 7. Artifacts / reproduction

  tools/snap_va_disasm.py (new, durable):
      python3 tools/snap_va_disasm.py <snap> 0x20095840 0x30
      python3 tools/snap_va_disasm.py <snap> 0 --find-bsr 0x20095840
      options: --pa-base (default 0x5bc000) --va-base (default 0x20000000)
               --raw-pa --img-size
  Everything in Sections 1-4 is reproducible from the snapshot named above
  with that one tool; no scratchpad state is required.

--------------------------------------------------------------------------------
## 8. ADDENDUM (same day) -- the mode is a CONSTANT; both outer callers are
##    CONSOLE-CALLBACK wrappers, and the gate is the CALLBACK's return value

  8.1 PROVEN: the mode never changes after the prologue.

      Writes to the mode slot 0x20(fp) anywhere in 0x20095840..0x20099400:
        200958c8  STQ r19,0x20(r30)   <- the resolver's own prologue
        200971e0  STQ r2,0x20(r30)    <- a DIFFERENT procedure's frame
        200977cc  STQ r19,0x20(r30)   <- a DIFFERENT procedure's prologue
      i.e. exactly ONE write inside the resolver, and it is the default-to-1
      store from Sec 3.  Both internal re-entries propagate it verbatim:
        200959f4  LDQ r19,0x20(r29) / 200959f8  BIS r31,#0x4,r25  -> AI=4
        200964f0  LDQ r19,0x20(r29) / 200964f4  BIS r31,#0x4,r25  -> AI=4
                  (0x200964fc, the recursion of the VMB-022 footprint)

      So: outer entry forces mode = 1, every recursive entry passes that same 1
      back in as an explicit arg 4.  MODE == 1 FOR THE ENTIRE WALK, on any
      host, in any run.  AXPBox's SUCCESSFUL boot also runs mode 1.

      CONSEQUENCE (supersedes VMB-021 finding 4 and SCSI-005 Sec 3 framing):
      the mode/flag gate CANNOT be what separates accept from NOIOVEC.  It is
      a constant on both sides of the comparison.  The discriminator has to be
      DATA -- the key record at a0, the module fields at [r2+...], or the boot
      string -- not the mode.

  8.2 What the 4-arg indirect call actually is (caller A, full body):

      200016a8  BIS  r31,#0x8,r17        ; a1 = 8      (env id)
      200016ac  BIS  r31,#0x10,r19       ; a3 = 0x10   (buffer length)
      200016b8  ADDL r30,#0x10,r18       ; a2 = &fp[0x10]  (buffer)
      200016bc  BIS  r31,#0x4,r25        ; AI = 4
      200016dc  BIS  r31,#0x22,r16       ; a0 = 0x22   <<< ROUTINE CODE
      200016e0  LDL  r27,0x0(r1)         ; PD via the linkage chain
      200016e4  LDQ  r26,0x8(r27)        ; entry = PD+8
      200016e8  JMP/JSR r26,(r26)        ; the callback
      200016ec  SRA  r0,#0x20,r1         ; r1 = high longword of the result
      200016f0  STL  r0,0x8(r29)         ; [fp+0x8]  = low  longword
      200016fc  STL  r1,0xc(r29)         ; [fp+0xc]  = high longword
      20001718  SLL  r18,#0x20 / 20001720 SRL r18,#0x3d   ; bits 61:63 of result
      20001724  BNE  r18, .+0x2c         ; <<< GATE 1: nonzero -> SKIP the
                                         ;     resolver, straight to epilogue
      20001730  BEQ  r19, .+0x20         ; <<< GATE 2: low longword == 0 ->
                                         ;     SKIP the resolver, return r0 = 1
      20001744  BSR  r26, 0x20095840     ; the 3-arg (mode-1) resolver call
      20001750  AND  r1,#0x1,r0          ; return low bit of the result

      Caller B has the same shape.  It resolves the same callback PD through
      the linkage chain and PARKS it in its own frame first --
        2000e380  LDL r17,0x0(r17)   /  2000e394  STL r17,0x20(r30)
      then 2000e3cc LDL r27,0x20(r29) / 2000e3d0 LDQ r26,0x8(r27) /
      2000e3e0 JSR (r26), with a0 = 0x22 (2000e3d8), a2 = &fp[0x28]
      (2000e3d4), a3 = 0x100 (2000e3b0), AI = 4 (2000e3b4).  Its env id (a1)
      is DATA-driven, not a literal.

      INFERRED (not proven here): a0 = 0x22 is GET_ENV in the console callback
      routine-code space, which matches SCSI-005 C1's "CSERVE CALLBACK GETENV"
      premise; and the return convention is length in the low longword with an
      error class in bits 61:63.  Under the standard env-code list, caller A's
      a1 = 8 is BOOTED_OSFLAGS.  Both worth confirming against the console
      spec before leaning on them.

  8.3 The window has never watched the part that matters.

      Every DIAG-PC footprint so far (VMB-022, SCSI-004) used
      PCLO = 0x20095840, which starts INSIDE the resolver.  The callback JSRs
      at 0x200016e8 / 0x2000e3e0, the two gates, and the returned status are
      all OUTSIDE every capture taken to date.  An EmulatR-side anomaly there
      is invisible to the "footprints are byte-identical" result -- that result
      only ever covered the resolver.

      NOTE ON OWNERSHIP: these callbacks are FIRMWARE code reached through the
      HWRPB CRB, not an EmulatR host service.  EmulatR's own CSERVE surface
      (deviceLib/ConsoleManager.cpp, PalService::executeCSERVE) implements
      0x01 GETC / 0x02 PUTC / 0x09 / 0x0C only -- there is no host-side
      GET_ENV to mis-return.  So an EmulatR-side fault on this path would be
      in the env VALUES the run seeds, or in CPU/PAL/chipset behaviour while
      the firmware's callback executes -- not in a host GET_ENV handler.

  8.4 Next steps, replacing Sec 6.1/6.2

      (a) One run, widened window: EMULATR_DIAG_PCLO=0x20001600
          PCHI=0x20099400.  That covers both callers, both gates and the
          resolver in a single capture.
      (b) From that trace, read r0 immediately after each callback JSR (the
          SRA at 0x200016ec / 0x2000e3e4 retires with it live).  That single
          quadword -- length in the low half, class in bits 61:63 -- decides
          whether the resolver is even supposed to run, and it is the cheapest
          EmulatR-vs-AXPBox comparison left.
      (c) Env diff (SCSI-005 4.3) is now TARGETED rather than a fishing trip:
          the ids these two call sites request, starting with a1 = 8 and
          caller B's data-driven id, plus their 0x10 / 0x100 buffer lengths.
      (d) Re-examine the one differing dword from SCSI-004 Sec 3 item 3
          ([0x2006a394] = 4 on the IDE run vs 0 on the SCSI run) as a
          candidate callback length/count rather than an unexplained field.
      (e) Sec 6.4 unchanged: the ES40 confound control still only matters via
          (c).

--------------------------------------------------------------------------------
## 9. PRIMARY SOURCE (same day) -- the callback ABI is CONFIRMED, Sec 8.2's
##    return convention was WRONG in detail, and the gate has a testable
##    EmulatR-side failure mode

  Source of record (in-tree, not the web):
    Processor Support/PalcodeBitsavers/apisrm/apisrm/ref/apu_callbacks_def.h
    Processor Support/PalcodeBitsavers/apisrm/apisrm/ref/call_backs.c
    (SRM 5.8-era apisrm; also srmconsole/5.8/SRC/CALL_BACKS.C)

  9.1 CONFIRMED against source

    cbfunc$k_set_env   32       cb_table[32] = cb_set_env
    cbfunc$k_reset_env 33       cb_table[33] = cb_reset_env
    cbfunc$k_get_env   34       cb_table[34] = cb_get_env   <<< 34 = 0x22
    cbfunc$k_save_env  35       cb_table[35] = cb_save_env

    krn$_callback() reads the routine index from impure->cns$gpr[0][2*16],
    i.e. from R16 -- so a0 IS the routine code, as Sec 8.2 read it.
    envid$booted_osflags = 8, matching caller A's literal a1 = 8 (but see 9.3).

  9.2 CORRECTED -- cb_get_env's actual return (supersedes Sec 8.2's
      "length in the low longword ... error class in bits 61:63"):

      low  longword R0 (cns$gpr[0][2*0])   = ev->size
                                             THE FULL VALUE SIZE, returned even
                                             when the copy was truncated
      high longword R0 (cns$gpr[0][2*0+1]) = 0x00000000  success
                                             0x20000000  buffer TOO SMALL
                                                         (size < ev->size)
                                             0xc0000000  failure / EV not found
                                                         (low longword also 0)

      This lands exactly on the two decoded gates:
        GATE 1 (bits 29:31 of the high longword, Sec 8.2):
            0x00000000 -> 0b000 -> PASSES
            0x20000000 -> 0b001 -> NONZERO -> resolver SKIPPED
            0xc0000000 -> 0b110 -> NONZERO -> resolver SKIPPED
        GATE 2 (low longword == 0): catches not-found / empty.
      So gate 1 is precisely "anything other than clean success -> skip", and
      the disassembly and the C source agree field for field.

  9.3 THE ONE OPEN ABI QUESTION (resolve before trusting "booted_osflags")

      In the 5.8 source, a1 is the VA of the EV NAME STRING
      (va = cns$gpr[0][2*17]; fread(name,...)), with 0x4d / 0x4e reserved as
      magic non-EV sentinels.  Caller A passes a1 = 8 -- far too small to be a
      VA, and exactly envid$booted_osflags.  The running firmware is DS20
      SRM V7.3, generations later than this 5.8 tree, so the V7.x callback
      appears to take an env ID where 5.8 took a name pointer.  Until that is
      settled, "caller A asks for booted_osflags" is INFERENCE, not fact.
      Settle it by (a) a V7.x callback source or spec in the tree, or (b) the
      a1 value and buffer contents in the Sec 8.4(a) widened trace.

  9.4 NEW CANDIDATE ROOT CAUSE -- buffer-too-small silently skips the resolver

      Caller A passes a3 = 0x10: a SIXTEEN BYTE buffer.  cb_get_env compares
      that against ev->size and returns 0x20000000 when the value is longer --
      which gate 1 turns into "skip the resolver".  No error is printed; the
      caller just returns 1.  So ANY boot-relevant EV whose stored value
      exceeds the caller's buffer silently disables the resolver call.

      This is directly actionable because the tree ALREADY tracks an env-format
      delta in this exact family:
        tools/srm_conformance/config/srmtest.json D15 --
        "boot_osflags format/default differs (real '0,0', emu '0')",
        severity "Low", area "boot-env"
      That severity predates this mechanism.  Any EV feeding a gated callback
      is not a cosmetic difference: value LENGTH is an input to control flow.
      Re-rate D15 and re-measure the boot-env set with SIZE as a first-class
      field, not just the printed text.

  9.5 OWNERSHIP, restated more carefully than Sec 8.3

      EmulatR does have an env store -- deviceLib/SRMEnvStore.cpp (JSON-backed,
      defaults seeded in initializeDefaults()) -- but it drives EmulatR's OWN
      synthetic SRM console (deviceLib/SRMConsole.cpp).  On a FIRMWARE boot the
      real SRM owns the env table and ev_read() answers from it.  Which store
      is authoritative during a firmware boot is itself an audit question, and
      it has to be answered before any EmulatR-side fix is aimed.

  9.6 PROPOSED AUDIT SCOPE (ConsoleManager / CSERVE / callbacks vs this tree)

      A. Callback surface: enumerate cb_table (call_backs.c) and classify each
         index as firmware-internal (no EmulatR obligation) vs EmulatR-
         observable.  The boot path only needs a handful; name them.
      B. Env fidelity with SIZE: for envid 1-15 plus 64-67, compare name,
         value, BYTE SIZE and flags against
         tools/srm_conformance/golden/*.golden.log.  Size is the gate input.
      C. Re-rate every existing srm_conformance env delta by whether its
         variable feeds a gated callback (D15 first).
      D. Separate axis -- CSERVE: EmulatR's PalService::executeCSERVE /
         ConsoleManager implement 0x01 GETC / 0x02 PUTC / 0x09 / 0x0C only.
         Compare against the CSERVE function list in the PALcode tree and
         record which unimplemented codes the V7.3 firmware can actually
         reach.  Do NOT conflate this with (A): CALL_PAL CSERVE and the HWRPB
         CRB callbacks are different interfaces.
