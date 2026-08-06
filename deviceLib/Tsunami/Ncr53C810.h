// ============================================================================
// Ncr53C810.h -- NCR 53C810 PCI SCSI HBA (KZPAA, `pka`), SCRIPTS-driven
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// JRN-SCSI-001/-002.  AUTHORITIES: apisrm ref/n810_def.h (register file +
// SCRIPTS encodings -- DEC's own defs), ref/pke_script.mar (the EXACT SCRIPTS
// program the DS20 v7.3-2 console downloads and runs), ref/pke_driver.c +
// ref/n810_driver.c (init/ISR contract), io_device_list.h (bind row: VID/DID
// 0x00011000 -> "NCR 53C810" -> pk/PKE).  AXPBox src/Sym53C810.cpp is the
// SECONDARY corroboration model.
//
// EXECUTION MODEL: the console driver is synchronous (write DSP -> the chip
// runs SCRIPTS until an interrupt; ISR re-kicks with DCNTL<STD>).  This model
// executes SCRIPTS TO COMPLETION inside the DSP/STD write: instruction fetch
// and all block moves go through the owner-supplied bulk DMA callbacks (the
// G-A bus-master seam -> TsunamiChipset::dmaRead/WriteBytes -> Pchip window
// translation).  The SCSI side is a phase engine over VirtualScsiDevice
// targets (ScsiBus-lite: id -> target map; LUN from the IDENTIFY message).
//
// FAITHFULNESS DEVIATIONS (deliberate, each traced loudly, revisit at P4):
//   D1 Data-in block moves PAD with zeros when the target supplies fewer
//      bytes than the move count, so the phase-mismatch (SIST0<MA>) residue
//      path never fires.  Benign for INQUIRY/MODE SENSE allocation-length
//      semantics; READ counts always match exactly.
//   D2 Targets never DISCONNECT mid-command; WAIT DISCONNECT after status
//      completes immediately; reselection never occurs.
//   D3 Single-step mode (DCNTL<SSM>) raises SSI after every instruction as
//      documented but is untested (the pke driver's SSM path is #if'd out).
//   D4 SBCL models the ATN line ONLY (bit 3, from the SOCL<3> latch).
//      REQ<7> ACK<6> BSY<5> SEL<4> and MSG<2> C_D<1> I_O<0> still read 0:
//      the phase engine owns phase and no bus-level arbitration is modeled.
//      Named 2026-08-06 when ATN was wired; the residue is deliberate.
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//   2026-08-06  Batch H-7 rider (with the VirtualDiskDevice.h page-01h
//               fix; architect-approved "go").
//               FUNCTION: ledgerCmd.
//               CHANGE:  every CHECK CONDITION row is UNTHROTTLED
//               (N810-CHKCOND: opcode, CDB byte 2, sense key/ASC/ASCQ).
//               The root-cause MODE SENSE rejection hid between this
//               ledger's every-256th samples for four days; rare events
//               must never share a throttle with bulk traffic.
//
//   2026-08-06  Batch H-6 -- SELECT faithful to the DM: table-indirect ID
//               (N-17 closed) and post-selection phase (N810-SELPHASE
//               resolved).  AUTHORITY: 53C895 Data Manual (Processor
//               Support/53C895_Data_Manual_Ver_3_Sep98.txt), "Instruction
//               Set of the I/O Processor": bit 24 Select-with-ATN ("SATN/
//               ... asserted during the selection phase"; illegal on any
//               other I/O instruction), bit 25 Table Indirect, bits 19:16
//               Encoded SCSI Destination ID (the NON-table form).
//               Corroboration: axpbox/src/Sym53C810.cpp:1758-1773 -- entry
//               address = DSA + sext24(w0<23:0>) & ~3, ONE dword read, id
//               = entry<19:16>.
//               MEASURED MOTIVATION (JRN-SCSI-042 Sec 10.6): the live pke
//               SELECT is w0=4700002C -- bits 24 AND 25 set.  Our inline
//               id read was therefore decoding bits 19:16 OF THE TABLE
//               OFFSET (0x02C), not an ID at all; it resolved to 0 by the
//               DOUBLE coincidence that the offset's bits 19:16 are zero
//               and target 0 exists.
//               FUNCTION: execSelect.
//               CHANGE:  w0<25> honored.  Table form: entry = DSA +
//                        sext24(w0<23:0>), &~3 (810 alignment, AXPBox
//                        corroborated), one dword via m_dmaRead, id =
//                        entry<19:16>.  Entry SCNTL3/SXFER config bytes
//                        (sync/wide negotiation values) are read but NOT
//                        applied -- no sync model exists to apply them to;
//                        noted at the site, not a new deviation (transfer
//                        timing is not modeled at all).
//               CHANGE:  post-selection phase now honors ATN: with ATN ->
//                        MSG OUT (unchanged, the exercised path); without
//                        ATN -> COMMAND phase.  The DM ties SATN/ to the
//                        selection itself; a target selected without ATN
//                        proceeds to command.  The no-ATN arm has NEVER
//                        been observed live (every pke/console SELECT
//                        carries bit 24) -- it is therefore LOUD when it
//                        first fires (N810-SELNOATN, first 8), because an
//                        untested arm exercising silently is how S-10
//                        class surprises are made.
//               FUNCTION: selTableRow / selNoAtnRow (new).
//               CHANGE:  bounded rows.  N810-SELTABLE (first 16 + every
//                        256th) carries entry address, entry dword,
//                        resolved id, AND the legacy inline id; any
//                        LEGACY-MISMATCH (table id != w0<19:16>) prints
//                        UNTHROTTLED (cap 32) -- each such row is a
//                        command the pre-H-6 model sent to the WRONG
//                        TARGET, the direct detector for the latent bug
//                        this batch closes.
//               REMOVAL TRIGGER: none -- fidelity.  Rows go when a boot
//                        shows zero LEGACY-MISMATCH and the arm is
//                        declared routine.
//
//   2026-08-06  Batch H-5 Edit A -- the ATN latch (architect-directed after
//               the LAPTRACE read; JRN-SCSI-042 Sec 10).  MEASURED
//               JUSTIFICATION, not a datasheet transcription: lap 0 of run
//               20260805_173619 shows the pke script issuing SELECT WITH
//               ATN (:26, w0=4700002C, bit 24 set) and CLEAR ATN twice
//               (:38 and :147, w0=60000008, flag bit 3), then reading SBCL
//               (:150, w0=760B0000) and branching on it (:151,
//               w0=8084F700 -- mask 0xF7 isolates bit 3).  That is live
//               ATN traffic against a register the model never drove.
//               BIT POSITIONS ARE MEASURED FROM THE GUEST, not taken from
//               AXPBox: the guest's own mask 0xF7 fixes SBCL<ATN> = 0x08,
//               and SOCL mirrors SBCL, so SOCL<ATN> = 0x08.  AXPBox
//               defines R_SOCL_ATN 0x20 against its own R_SBCL_ATN 0x08
//               (Sym53C810.cpp:88 vs :101) -- an internal disagreement
//               that never bites it because nothing there reads SOCL.
//               PREDICTED BEHAVIOURAL EFFECT ON THE PARK LAP: NONE.  The
//               single SBCL read sits three instructions after a CLEAR
//               ATN, so silicon reads 0x00 there and so do we, before and
//               after this edit.  H-A was REFUTED by the lap listing
//               (JRN-SCSI-042 Sec 10) and this edit does not revive it.
//               It closes a fidelity gap with measured traffic; a lap that
//               CHANGES after this lands is itself a finding and must be
//               chased, not welcomed.
//               FUNCTION: execIoOrRw (SET/CLEAR arm).
//               CHANGE:  SET ATN / CLEAR ATN latch SOCL<3> instead of
//                        returning silently.  ACK (flag 0x40) and carry
//                        (flag 0x400) remain unmodeled and are now LOUD
//                        rather than silently dropped.
//               FUNCTION: execSelect.
//               CHANGE:  w0<24> (select-with-ATN) is honored and asserts
//                        SOCL<3>.  It was discarded.  The phase choice is
//                        deliberately NOT touched: setPhase(kPhMsgOut) is
//                        still unconditional, which is wrong when bit 24
//                        is clear -- [CONFIRM] and TODO(N810-SELPHASE),
//                        held out of this batch because it moves every
//                        SELECT path in the model.
//               FUNCTION: regRead8 (new kSBCL arm) / reset.
//               CHANGE:  SBCL composes bit 3 from the SOCL latch.  All
//                        other SBCL bits still read 0 -- deviation D4,
//                        named at the top of this file.  reset() clears
//                        SOCL explicitly beside the other bus latches.
//               FUNCTION: atnRow / selAtnRow / setClrUnmodeledRow / sbclRow
//                        (all new).
//               CHANGE:  bounded instrumentation.  N810-SBCLCHG is the one
//                        that matters: it fires only when a SBCL read
//                        returns a DIFFERENT value than the previous read,
//                        unthrottled to 32 rows.  Across the whole of the
//                        08-05 boot that would have printed exactly once,
//                        at 0x00.  A second row means the branch input
//                        finally moved.  Raw SBCL reads are first 16 +
//                        every 4096th (they ran 63,507 per boot).
//               REMOVAL TRIGGER: none -- this is fidelity, not a probe.
//                        The instrumentation rows go when D4 closes.
//
//   2026-08-06  TODO(N810-LAPTRACE) (architect-approved 2026-08-06,
//               JRN-SCSI-042 Sec 5).  The park lap is the whole remaining
//               question: the script parks in WAIT RESELECT at 0xC000132C,
//               the host writes ISTAT<SIGP>, the script resumes at the
//               alternate 0xC0001578, and 63,509 laps later nothing has
//               advanced.  Four hypotheses (JRN-SCSI-042 Sec 4, H-A..H-D)
//               all collapse to ONE listing of the instructions actually
//               executed between two parks.
//               FUNCTION: scriptsTrace.
//               CHANGE:  the legacy free-running trace keeps its exact
//                        behaviour, but its counter moves from a
//                        function-local static to a member (it was shared
//                        across instances and could not be reset) and it
//                        now tail-calls lapTrace.  Its 4000-row cap is
//                        consumed by the console era long before VMS
//                        starts -- the SCRIPTDUMP 6-slot species -- which
//                        is why a second, separately targeted arm exists
//                        rather than a bigger cap.
//               FUNCTION: lapTrace (new) / lapArm (new) / insnClass (new).
//               CHANGE:  one row per SCRIPTS instruction executed between
//                        two WAIT RESELECT parks, carrying dsp, w0, w1,
//                        the decoded instruction class and SFBR.  SFBR is
//                        sampled AT FETCH, i.e. BEFORE the instruction
//                        executes: this AMENDS JRN-SCSI-042 Sec 5, which
//                        specified "after".  Before is the better datum
//                        and needs no restructuring -- a conditional
//                        transfer's row then carries exactly the byte
//                        that transfer is testing, which is the entire
//                        purpose of the instrument.  The preceding rw row
//                        shows a stale SFBR; the loaded value appears on
//                        the very next row.
//               FUNCTION: execIoOrRw (WAIT RESELECT arm).
//               CHANGE:  lapArm() beside the existing censusDump call.
//                        The FIRST park arms the trace; each later park
//                        closes a window and opens the next.  The
//                        N810-LAP END row carries that window's
//                        instruction count, so "does the lap vary" is
//                        answerable without reading one instruction row.
//               BOUNDS:  kLapTraceLaps 3 x kLapTraceInsns 2048 = 6,144
//                        rows per boot, hard, plus the framing rows.
//                        Guard EMULATR_DIAG_N810 (already wired in
//                        CMakeLists :351/:355/:766) -- NO build-system
//                        change in this edit.  The lap arm deliberately
//                        does NOT require EMULATR_SCSI_TRACE: an
//                        instrument that needs an env var is how the last
//                        one got missed (I-1, run 20260802_212601).
//               NOT RESET: the arm is once per process and SURVIVES
//                        reset() on purpose.  A chip reset mid-boot would
//                        re-arm and spend the budget on the console era --
//                        the W-8 lesson (JRN-SCSI-041 Sec 12).
//               REMOVAL TRIGGER: the lap is decoded and JRN-SCSI-042
//                        Sec 6 P1 is resolved.
//
//   2026-08-05  Batch I-3 register census (architect-approved 2026-08-05,
//               JRN-SCSI-041 Sec 12).  The dead-constant sweep found the
//               ENTIRE bit-level SCSI bus surface unwired: SOCL 0x09,
//               SSID 0x0A, SBCL 0x0B, SSTAT2 0x0F, SIDL 0x50, SODL 0x54,
//               SBDL 0x58 -- declared at :448-462, referenced nowhere.
//               Coverage pass applied (a constant whose byte falls inside
//               a 32-bit access through a lower neighbour IS reachable):
//               the only 32-bit bases are kDSA/kTEMP/kDBC/kDNAD/kDSP/
//               kDSPS, none of which reach those seven.  kDCMD 0x27 WAS a
//               false positive -- covered by setReg32(kDBC) at :929/:1056
//               -- and is the reason the coverage pass exists.
//               ONE coherent architectural gap, not seven oversights: the
//               model implements the phase engine and the transfer
//               machinery, and omits the layer that lets a SCRIPTS program
//               OBSERVE the bus directly.  PKEDRIVER's init script reaches
//               through it (JRN-SCSI-038 I-2).
//               FUNCTION: regRead8 / regWrite8 / censusHit (new) /
//               censusDump (new).
//               CHANGE:  TODO(N810-CENSUS): per-register access census --
//                        COUNTERS, not rows: [off][rd/wr][host/script]
//                        plus the DSP of first SCRIPT-side touch.  Dumped
//                        as ONE table, and only when it CHANGED since the
//                        last dump.  A per-access ledger of the poll loop
//                        would emit one row per SCRIPTS instruction, and
//                        the H-1d cap would then hide the row that
//                        mattered -- the I-1 species, which cost run
//                        20260802_212601 its wake identity.  A table that
//                        STOPS changing is itself the finding: the
//                        script's register working set has closed, which
//                        is the signature of a converged spin.
//                        COST, stated so the next hot-path instrument does
//                        not re-derive it: 2 KB of state plus two array
//                        increments per register access, on a path that
//                        runs inside a 100k-instruction budget which
//                        executed 76,481 times in run 20260804-213741.
//                        That measurable perturbation is why this one IS
//                        compile-guarded where W-1's ledgerCmd
//                        deliberately is not (:1557 DEVIATION): a
//                        code-only bounded row cannot become a throughput
//                        regression; 2 KB on the hot path can.
//                        GUARD: EMULATR_DIAG_N810 -- defined for Debug and
//                        RelWithDebInfo, absent in Release, wired in
//                        CMakeLists on the EMULATR_CHIPSET_DIAG pattern in
//                        THIS edit (a guard defined nowhere compiles to
//                        silence in every config, which is the exact
//                        failure this batch exists to end).  Release
//                        carries neither the arrays nor the code.
//                        NAMED BLIND SPOT (seam C): SCRIPTS writes reach
//                        m_reg[] directly from execRw opc 5/7 (:1235-1236)
//                        and never enter regWrite8, so the scrW column is
//                        STRUCTURALLY ZERO.  The dump header says so in
//                        the table itself.  A zero there means NOT
//                        MEASURED, not "did not happen" -- the
//                        STRAY_INT_CNT species (JRN-SCSI-041 Sec 9.2).
//                        Routing script writes through regWrite8 is the
//                        follow-up that kills the seam; this header line
//                        is a mitigation, not a fix.
//                        ALIASING: registers 0x00-0x5F are real.  A
//                        nonzero row at 0x60-0x7F is an ALIAS artifact of
//                        the & 0x7F mask and is itself a decode finding,
//                        not traffic.
//               FUNCTION: (doc) TODO(N810-SBCL) narrowed to
//               TODO(N810-DFIFO).
//               CHANGE:  the SBCL clause is struck -- SBCL is now a named
//                        census subject whose fix scope will be set by
//                        MEASUREMENT rather than by the 038 mask decode.
//                        The residue half survives and is retagged: SSTAT0
//                        and DFIFO still read 0, so pke's DATA OUT
//                        mismatch fixup (dfifo - byte_count) would compute
//                        garbage on a data-out short (JRN-AUD-003 S-8).
//               REMOVAL TRIGGER: the PKE port-init stall is root-caused.
//
//   2026-08-03  BRIEF-SCSI-040 batch (architect-approved; corrections and
//               revised diagnosis in JRN-SCSI-041).  The 140935 run's
//               45,341-op storm decoded: 45,340 failed SS$_TOOMUCHDATA
//               (dump UCB$L_DK_VMS_STATUS=0x29C).  The brief's W-2 clamp
//               was ALREADY LANDED (Batch G S-3) and D1 padding already
//               retired (JRN-SCSI-034); the live defect was one seam on:
//               FUNCTION: execBlockMove kPhDatIn mismatch path (W-3).
//               CHANGE:  a phase mismatch now HALTS the SCRIPTS
//                        processor (m_running=false) with DBC holding
//                        the residual, per 895 DM Ch.6.  Previously the
//                        script ran on: STATUS/MSG IN moved, MMs zeroed
//                        DBC, completion INT merged with the MA, and the
//                        ISR computed transferred = full move count.
//                        Driver resumes via DSP write / DCNTL<STD>.
//                        [CONFIRM] DSP value at halt vs the DM.
//               FUNCTION: executeCommand / executeWriteCommand /
//               ledgerCmd (new) (W-1).
//               CHANGE:  bounded per-command ledger row: opcode, CDB-
//                        decoded allocation length, bytes returned,
//                        status (first-64 + every-256th).  Deviation
//                        from the brief's compile-guard spec recorded
//                        in JRN-SCSI-041 (dark-probe risk).
//
//   2026-08-03  Batch H-3.3 (architect-approved same day, JRN-SCSI-039
//               Sec 6): mailbox visibility + ISTAT<ABRT> semantics.
//               Evidence base: run 20260803_103333 + the SDA crash read
//               (KPB 81C5A6C0 stall site UNCHANGED at +20678; SCDRP
//               82F1DD40 VIRGIN -- the deadlock is driver-awaits-
//               init-done-INT vs script-polls-mailbox, and the mailbox
//               was a dark channel).
//               FUNCTION: regWrite8 (I-3, I-5, H-3.3a) / regRead8 /
//               execRw (H-3.3b) / scriptDump / ledgerCtl /
//               execMemoryMove / ledgerMm (I-4).
//               CHANGE:  (I-3) DSA/SCRATCHA/SCRATCHB join the CTL
//                        ledger on WRITE and READ -- they are the pke
//                        mailbox; rows carry "(script)" when the
//                        SCRIPTS engine is the author.
//               CHANGE:  (H-3.3b, ROOT-CAUSE CANDIDATE) execRw opc 6/7
//                        source reads route through regRead8, not raw
//                        m_reg[].  Measured casualty of the old path:
//                        MOVE CTEST2 to SFBR (pke +0x2A4) never saw
//                        SIGP (composed only in regRead8), so every
//                        SIGP wake read as spurious, the script never
//                        dispatched, never raised init-done INT 0x0F79
//                        -- the C1 two-party deadlock.  m_sigp also
//                        stayed latched forever.  Script reads now get
//                        CTEST2 SIGP read-clear, ISTAT composition,
//                        DSTAT/SIST clear-on-read.  [CONFIRM] 895 DM
//                        Ch.6 rw-op access semantics; write side stays
//                        direct m_reg[] (separate decision).  G-4
//                        console-era comparison MANDATORY (console
//                        scripts use rw ops too).
//               CHANGE:  (SCRIPTDUMP) window widened DOWN 0x1600 below
//                        the entry DSP (rows carry -0xNNNN offsets):
//                        the init-done route (CALL rel -0x14D8, poll
//                        back-jump rel -0x544) lives in the library
//                        below 0xC00012D4, never captured until now.
//                        (I-4) N810-MM success rows carry the first
//                        dword moved (val=) -- the polled values.
//                        (I-5) ISTAT writes with ABRT/RST are LOUD
//                        (N810-ISTATCTL, first-16 + every-256th); the
//                        103333 teardown's suspected soft reset fell
//                        between CTL samples.
//                        (H-3.3a) ISTAT<ABRT> EXECUTES: SCRIPTS stops
//                        (all park states cleared, DSP preserved),
//                        DSTAT<ABRT> -> DIP raised (53C895 DM ISTAT/
//                        DSTAT) [CONFIRM exact page].  Measured
//                        consumer: pke TOUTROUT t=7 recovery (abort
//                        then wait; formerly died on our silence).
//
//   2026-08-03  H-3.1 wake identity (instrument gap I-1, JRN-SCSI-038
//               Sec 7; pickup item P-3).
//               FUNCTION: ioWrite / wakeFromPollPark.
//               CHANGE:  N810-POLLWAKE rows now record WHICH completed
//                        host write woke the parked script (reg + value +
//                        width, passed from the ioWrite tail).  Run
//                        20260802_212601 had exactly ONE wake and its
//                        identity fell between the every-256th N810-CTL
//                        samples -- the datum that distinguishes "driver
//                        posted then stalled" from "driver never posted"
//                        was lost.  Row cadence unchanged (first 8 +
//                        every 256th).  Rides the H-3 shim; removed with
//                        it (REMOVAL TRIGGER: H-4).
//
//   2026-08-03  Batch H-3 poll-park shim, _PROVISIONAL (architect-approved
//               with explicit shim labeling; JRN-SCSI-038).
//               FUNCTION: runScriptsLoop / wakeFromPollPark (new) /
//               ioWrite / startScripts / reset.
//               CHANGE:  The instruction-budget guard is NO LONGER A KILL.
//                        H-2 verification (run 20260802_205650) showed the
//                        VMS pke script busy-polling its mailbox registers
//                        from SCRIPTS after the SIGP resume; under
//                        run-to-completion the CPU is frozen during the
//                        loop, the polled state cannot change, and the
//                        100k guard executed a LEGAL poll loop to death
//                        (TIME-DIVERGENT: on silicon the loop runs BESIDE
//                        the CPU, in this model it ran INSTEAD of it).
//                        Now: budget exhaustion -> poll-park (DSP
//                        preserved, no interrupt, bounded N810-POLLPARK
//                        rows), wake on the next COMPLETED host MMIO
//                        write (never a partial byte), fresh budget per
//                        wake (bounded N810-POLLWAKE rows).  Event-parked
//                        WAIT RESELECT is untouched (SIGP-only contract).
//                        NAMED LIMIT: RAM-mailbox-only posts do not wake
//                        the script; that sighting is H-4 evidence.
//                        REMOVAL TRIGGER: superseded by Batch H-4
//                        budgeted stepping -- deterministic SCRIPTS
//                        throughput on a guest-cycle cadence via a
//                        machine-loop pollTick; rate authority [CONFIRM]
//                        vs 53C895 DM + DMODE burst; scope and gates in
//                        JRN-SCSI-038 Sec 5 (console-era cycle comparison
//                        MANDATORY -- instantaneous console sessions are
//                        themselves TIME-DIVERGENT, a defect not yet
//                        bitten by).
//
//   2026-08-03  Batch H-2 (architect-approved as scoped, JRN-SCSI-037):
//               the four SCRIPTS constructs the VMS PKEDRIVER init script
//               actually uses (census via TODO(N810-SCRIPTDUMP)).
//               AUTHORITIES: 53C895 Data Manual Ch.6 (Memory Move page
//               6-21: 3-dword form, reserved bits 28:25 IID, A1:0
//               alignment IID, own-register-window decode via low seven
//               bits, DSPS/TEMP holding registers with DSA preserved);
//               DEC apisrm ref/n810_def.h (tc_rel :726, io_rel :665,
//               istat_sigp :320, ctest2_sigp :348, rw fields :702-707,
//               k_mm=3 :752 with NO load/store symbols anywhere).
//               FUNCTION: stepScripts / execMemoryMove (new) /
//               inRegWindow (new) / ledgerMm (new).
//               CHANGE:  (H-2a) Memory Move type 3 EXECUTES: third-dword
//                        fetch (DSP advances +12, next-DSP computed first
//                        so a move landing on DSP wins), cited IID checks,
//                        bit 24 No-Flush _PROVISIONAL execute+loud (DEC's
//                        assembler never emits it; no prefetch unit on the
//                        810) [CONFIRM], own-BAR routing BOTH directions
//                        on the full BAR extent with &0x7F alias inside,
//                        SIOM/DIOM honored, DSPS/TEMP clobbered (end
//                        values are inference, flagged), DSA preserved.
//                        Type-7 (Load/Store) words remain IID -- absent on
//                        plain 810 silicon; a sighting is an IDENTITY
//                        finding (P-3), never an implement-me.
//               FUNCTION: execTransferCtl.
//               CHANGE:  (H-2b) w0<23> relative form: dest = next-DSP +
//                        sext24(w1), JUMP and CALL.  The pke script is
//                        relative nearly throughout, incl. negative
//                        offsets; absolute-only execution teleported DSP
//                        to a raw offset at the first conditional.
//               FUNCTION: execRw.
//               CHANGE:  (H-2c) opc 5 (write SFBR to register) now sources
//                        SFBR for the operation instead of the target
//                        register.
//               FUNCTION: execIoOrRw (WAIT RESELECT arm) / runScriptsLoop
//                        (new, shared loop) / resumeFromPark (new) /
//                        regRead8 (ISTAT SIGP bit, CTEST2 read-clear) /
//                        regWrite8 (ISTAT SIGP set + resume) / reset.
//               CHANGE:  (H-2d) WAIT RESELECT is the pke IDLE idiom, not
//                        an error: parks in a DISTINCT state (no STO, no
//                        interrupt, no session-ledger completion); SIGP
//                        already set at execution jumps to the alternate
//                        immediately; ISTAT<SIGP> write while parked
//                        resumes at the alternate in the SAME session;
//                        SIGP is cleared ONLY by the CTEST2 read (read-
//                        clear modeled) -- clearing on resume would both
//                        lie to ISTAT pollers and livelock the script's
//                        own CTEST2 handshake.  w0<26> relative alternate
//                        honored.  Replaces the D2 STO shim on this arm.
//               FUNCTION: v1Probe.
//               CHANGE:  H-2 constructs removed from NEW-CONSTRUCT (a
//                        modeled construct must not print it); MM activity
//                        re-badged to bounded ledgerMm rows; bit 6
//                        re-badged LOAD-STORE-810A (identity watch).
//
//   2026-08-03  TODO(N810-SCRIPTDUMP) (architect-directed, JRN-SCSI-036
//               amendment D: "dump the script before implementing").
//               FUNCTION: startScripts / scriptDump (new).
//               CHANGE:  One-shot hex dump of the first 0x40 dwords of a
//                        SCRIPTS program the FIRST time each distinct DSP
//                        value starts a session (max 6 distinct DSPs per
//                        boot; the console pair consumes two slots, the VMS
//                        PKE script at 0xC00012D4 takes the third).  Turns
//                        the JRN-SCSI-036 one-construct evidence (Memory
//                        Move at DSP+8) into PKE's ENTIRE construct set in
//                        one capture, so Batch H-1 lands scoped to what the
//                        script actually uses instead of moving the wall
//                        two instructions.  Observation only.
//               FUNCTION: regRead8 (identity arm, same amendment).
//               CHANGE:  Reads of CTEST3 (0x1B, rev nibble) and MACNTL
//                        (0x46, chip-type nibble) each print a loud
//                        N810-IDREAD row (first 16).  The register-path
//                        identity surface disagrees with config space
//                        today (JRN-AUD-003 S-10: both all-zero); if a
//                        driver PROBES identity before choosing its
//                        SCRIPTS constructs, the probe and the construct
//                        set land in the same capture.  First exhibit for
//                        the chip-identity-coherence matrix row.
//                        REMOVAL TRIGGER: same as TODO(N810-LEDGER) -- keep
//                        through the H-1 verification run, remove when the
//                        C1 wall (0x801A0170 spin) falls.
//
//   2026-08-03  TODO(N810-LEDGER) (architect-approved; JRN-SES-003 dump
//               analysis follow-up).
//               FUNCTION: startScripts / regRead8 / regWrite8 / ledgerCtl
//               (new).
//               CHANGE:  Unconditional bounded activity ledger for the
//                        channel the dump analysis found DARK: one
//                        N810-SESSION line per SCRIPTS start, and N810-CTL
//                        rows (first 96 + every 256th) for control-register
//                        writes (ISTAT/SCNTL/DCNTL/DIEN/DMODE/SIEN/SCID/
//                        SXFER) and status reads (ISTAT/DSTAT/SIST0/1).
//                        Answers: what does VMS PKEDRIVER do to the chip
//                        between the OpenVMS banner and its t=7 give-up --
//                        chip reset?  init script?  masked-interrupt wait?
//                        nothing?  Observation only; no behavior change.
//                        REMOVAL TRIGGER: PKE port-init stall root-caused.
//
//   2026-08-02  JRN-AUD-003 Batch G + V-1 probe (architect-approved).
//               FUNCTION: moveData (kPhMsgOut / kPhMsgIn), struct Conn.
//               CHANGE:  (S-4) an extended negotiation message in MSG OUT
//                        (SDTR 0x01/0x03 forms) is now answered with
//                        MESSAGE REJECT (0x07) in MSG IN before COMMAND --
//                        SCSI-2 6.6.21 makes the previous silent swallow an
//                        illegal target response.  pke handles the reject
//                        arm explicitly (pke_driver.c); INQUIRY byte 7 = 0
//                        already tells initiators not to negotiate.
//                        REGRESSION GATE: console-era cycle comparison vs a
//                        known-good run (SRM sends SDTR on first selection;
//                        the reject leg replaces the swallow leg).
//               FUNCTION: v1Probe (new) + startScripts session counter.
//               CHANGE:  TODO(N810-V1-PROBE): bounded forensic answering
//                        JRN-AUD-003 V-1 -- one loud row per FIRST use of
//                        each SCRIPTS construct the model does not execute
//                        faithfully (table-indirect io, indirect move,
//                        relative transfer, MOVE SFBR->reg, WAIT RESELECT,
//                        INTFLY, memory move) + a heartbeat every 4096th
//                        session.  Resolves S-1/S-2/S-6/S-7 from one boot.
//               FUNCTION: (doc sweep) stale TODO(N810-SSTAT1) comment
//                        corrected to TODO(N810-SBCL); stale "no bus-master
//                        window translation" note corrected (JRN-AUD-003
//                        S-8/S-16).
//
//   2026-08-02  JRN-SES-001 Batch C1 (architect-approved): interrupt_pin is
//               SILICON, not configuration.
//               FUNCTION: (class constant) kInterruptPin (new).
//               CHANGE:  The PCI Interrupt Pin register (config 0x3D) is
//                        hardwired to 01h (INTA#) on the whole 53C8xx family
//                        (53C895 Data Manual: "Its value is set to 01h";
//                        single-function PCI devices must use INTA# per the
//                        PCI spec; corroborated by apisrm ref/n810_def.h).
//                        The model owns the value as a constant, exactly as
//                        it owns its PCI IDs and register offsets.
//               FUNCTION: setInterruptPin.
//               CHANGE:  Was a raw manifest write into m_cfg[0x3D] -- a
//                        fabrication surface that silently held the wrong
//                        ES40 value (interrupt_pin: 2, JRN-SCSI-034 Sec 6).
//                        Now a VALIDATOR: a declared pin that disagrees with
//                        kInterruptPin warns loud and is IGNORED; config
//                        space always presents the silicon value.
// ============================================================================
#ifndef DEVICELIB_TSUNAMI_NCR53C810_H
#define DEVICELIB_TSUNAMI_NCR53C810_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <ostream>
#include <utility>
#include <vector>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler, IIoPortHandler
#include "deviceLib/scsi/ScsiCommand.h"
#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/VirtualScsiDevice.h"

namespace deviceLib {

class Ncr53C810 final : public IPciDeviceHandler, public IIoPortHandler {
public:
    // ---- owner-supplied seams (wired by TsunamiChipset::wireDevices) ------
    using RangeFn  = std::function<void(uint64_t base, uint32_t len,
                                        bool isMem, IIoPortHandler* self)>;
    using DmaRdFn  = std::function<void(uint64_t pciAddr, void* dst, size_t n)>;
    using DmaWrFn  = std::function<void(uint64_t pciAddr, void const* src, size_t n)>;
    using IntrFn   = std::function<void(bool level)>;

    Ncr53C810() noexcept { initConfig(); reset(); }

    void setRangeCallbacks(RangeFn reg, RangeFn unreg) noexcept
    { m_register = std::move(reg); m_unregister = std::move(unreg); }
    void setDmaAccess(DmaRdFn rd, DmaWrFn wr) noexcept
    { m_dmaRead = std::move(rd); m_dmaWrite = std::move(wr); }
    void setIntrCallback(IntrFn fn) noexcept { m_intr = std::move(fn); }

    // Interrupt Pin is SILICON (Batch C1, 2026-08-02; see CHANGE HISTORY).
    // 53C8xx hardwires config 0x3D to 01h = INTA# (53C895 DM; PCI spec
    // single-function rule; apisrm ref/n810_def.h).  The console reads 0x3D
    // and indexes its board routing table with it (pc264_io.c
    // assign_pci_vector), so this value MUST be the silicon one.
    static constexpr uint8_t kInterruptPin = 0x1;   // INTA#

    // VALIDATOR (was a raw manifest write -- fabrication surface, JRN-SCSI-034
    // Sec 6.1).  A manifest that declares a different pin is WRONG about
    // silicon: warn loud, ignore, keep kInterruptPin in config space.
    void setInterruptPin(uint8_t pin) noexcept
    {
        if (pin != 0 && pin != kInterruptPin) {
            std::fprintf(stderr,
                "Ncr53C810: manifest interrupt_pin=%u REJECTED -- 53C810 "
                "hardwires INTA# (0x3D=0x1, 53C895 DM); routing uses the "
                "silicon value\n", unsigned(pin));
            std::fflush(stderr);
        }
        m_cfg[0x3D] = kInterruptPin;   // silicon value, unconditionally
    }

    // SCSI bus population: id 0..7 -> LUN-0 target (HBA itself is id 7 by
    // pke convention; attaching there is rejected).  ScsiBus-lite: one
    // target per id, LUN selected by IDENTIFY (only LUN 0 served today).
    bool attachTarget(unsigned id, scsi::VirtualScsiDevice* t) noexcept
    {
        if (id >= 8 || id == kHostId || t == nullptr) return false;
        m_targets[id] = t;
        return true;
    }

    // ---- chip reset -------------------------------------------------------
    void reset() noexcept
    {
        m_reg.fill(0);
        m_reg[kISTAT] = 0;
        m_reg[kDSTAT] = 0x80;              // DFE: DMA FIFO empty
        m_reg[kCTEST0] = 0;
        m_reg[kSSTAT0] = 0;
        m_reg[kSSTAT1] = 0;   // phase latch clears with the bus
        m_reg[kSOCL]   = 0;   // Edit A: ATN latch clears with the bus too
        m_dstatPend = 0; m_sist0Pend = 0; m_sist1Pend = 0;
        m_running = false;
        m_sigp = false; m_parked = false; m_parkedAlt = 0;   // H-2d state
        m_pollParked = false;                                // H-3 state
        m_conn = Conn{};
        updateIrq();
    }

    // ========================================================================
    // IPciDeviceHandler -- config space
    // ========================================================================
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) noexcept override
    {
        uint32_t v = 0;
        for (uint8_t b = 0; b < width && (reg + b) < 256u; ++b)
            v |= static_cast<uint32_t>(m_cfg[reg + b]) << (8u * b);
        return v;
    }

    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) noexcept override
    {
        if (reg == 0x10 || reg == 0x14) {              // BAR0 (I/O) / BAR1 (mem)
            const bool     isMem    = (reg == 0x14);
            const uint32_t typeBits = isMem ? 0x00u : 0x01u;
            const uint32_t addrMask = ~(kBarWindow - 1u);
            if (value == 0xFFFFFFFFu) {                // size probe
                storeCfgLE(reg, (addrMask & ~0xFu) | typeBits);
                return;
            }
            storeCfgLE(reg, (value & addrMask) | typeBits);
            programBar(isMem, value & addrMask);
            return;
        }
        if (!cfgWritable(reg)) return;
        storeCfgLE(reg, value, width);
    }

    // ========================================================================
    // IIoPortHandler -- CSR file (BAR-relative offset 0x00-0x5F; the Pchip
    // mem-claim path rebases, the io-port path passes the raw port, so mask
    // to the 0x7F window either way).
    // ========================================================================
    uint64_t ioRead(uint16_t off, uint8_t width) noexcept override
    {
        uint64_t v = 0;
        for (uint8_t b = 0; b < width; ++b)
            v |= static_cast<uint64_t>(regRead8((off + b) & 0x7F)) << (8u * b);
        return v;
    }
    void ioWrite(uint16_t off, uint64_t value, uint8_t width) noexcept override
    {
        for (uint8_t b = 0; b < width; ++b)
            regWrite8((off + b) & 0x7F, static_cast<uint8_t>(value >> (8u * b)));
        // Long DSP writes start SCRIPTS after the last byte lands (pke:
        // wl(dsp, phys(script)) with DMODE<MAN>=0 auto-starts).
        if (((off & 0x7F) <= kDSP + 3) && ((off & 0x7F) + width) > kDSP + 3)
            startScripts("DSP write");
        // TODO(N810-POLLPARK) (H-3): a COMPLETED host write is the wake
        // event for a poll-parked script -- after all bytes land, so the
        // script never observes a partially-written register.  Event-parked
        // (WAIT RESELECT) scripts are NOT woken here; their contract is
        // SIGP only (regWrite8 kISTAT arm).  H-3.1: the wake carries its
        // identity (reg/value/width) into the ledger row.
        else if (m_pollParked && !m_running)
            wakeFromPollPark(static_cast<uint8_t>(off & 0x7F), value, width);
    }

    // TODO(N810-GENTIMER) _PROVISIONAL (2026-08-05, JRN-SCSI-041 Sec 14).
    // THE POLLTICK SEAM.  This device had NO time source: it is driven only
    // by ioRead/ioWrite and the SCRIPTS executor, so a timer had nowhere to
    // live.  Machine::systemTick already carries this exact pattern for the
    // Cchip interval timer and the flash debounce (Machine.cpp:1983-1990,
    // "rides the interval-timer tick ... ~2^18-cycle timer cadence").
    // AXPBox reached the same design independently: check_state() is a
    // SystemComponent virtual (SystemComponent.hpp:48) walked over every
    // device from System.cpp:270, and its SCSI gen-timer is one consumer.
    //
    // FIRST CONSUMER is the general-purpose timer only.  Budgeted SCRIPTS
    // stepping (H-4, JRN-SCSI-038 Sec 5) is the intended SECOND consumer of
    // this same call -- that is why the seam is named pollTick and not
    // genTimerTick.  Landing it here means H-4 needs no new architecture.
    //
    // RATE AUTHORITY [CONFIRM]: kGenTicks = 1 fires on the NEXT interval
    // tick after arming.  AXPBox uses (STIME1 & 0x0F) * 30, but 30 is 30
    // units of ITS check_state cadence, which is NOT our ~2^18-cycle tick --
    // the multiplier cannot be carried across.  1 answers the question that
    // matters first (does GEN unblock the driver AT ALL); the true period is
    // worth deriving only if it does.
    void pollTick() noexcept
    {
        if (m_genTimer == 0) return;              // disarmed
        if (--m_genTimer != 0) return;            // still counting
        m_genTimer   = kGenTicks;                 // periodic, re-arm
        m_sist1Pend |= kSist1Gen;
        bool const enabled = (m_reg[kSIEN1] & kSist1Gen) != 0;
        bool const before  = m_irq;
        updateIrq();                              // gates on m_reg[kSIEN1]
        uint64_t const n = m_genRows++;
        if (n < 64 || (n & 0xFFu) == 0) {
            std::fprintf(stderr,
                "N810-GENTIMER[%llu] FIRE sist1pend=0x%02X sien1=0x%02X "
                "enabled=%u irq %u->%u _PROVISIONAL\n",
                static_cast<unsigned long long>(n),
                unsigned(m_sist1Pend), unsigned(m_reg[kSIEN1]),
                unsigned(enabled ? 1 : 0),
                unsigned(before ? 1 : 0), unsigned(m_irq ? 1 : 0));
            std::fflush(stderr);
        }
    }

private:
    static constexpr uint32_t kGenTicks = 1;   // [CONFIRM] rate authority
    // Batch H-5 Edit A (2026-08-06): ATN bit positions, MEASURED from the
    // guest's own compare mask 0xF7 at 0xC00006A4 (isolates bit 3).  SOCL
    // mirrors SBCL, so both carry ATN at 0x08.  The SET/CLEAR instruction
    // flag field is separate: ATN 0x08, ACK 0x40, carry 0x400 in w0.
    static constexpr uint8_t  kSoclAtn  = 0x08, kSbclAtn  = 0x08;
    static constexpr uint32_t kSetAtn   = 0x00000008, kSetAck = 0x00000040,
                              kSetCarry = 0x00000400;
    // TODO(N810-LAPTRACE) bounds (2026-08-06, JRN-SCSI-042 Sec 5).
    static constexpr unsigned kLapTraceLaps  = 3;     // park-to-park windows
    static constexpr unsigned kLapTraceInsns = 2048;  // rows per window
    // ---- register offsets (n810_def.h struct n810_csr) --------------------
    static constexpr uint8_t kSCNTL0 = 0x00, kSCNTL1 = 0x01, kSCNTL2 = 0x02,
                             kSCNTL3 = 0x03, kSCID   = 0x04, kSXFER  = 0x05,
                             kSDID   = 0x06, kGPREG  = 0x07, kSFBR   = 0x08,
                             kSOCL   = 0x09, kSSID   = 0x0A, kSBCL   = 0x0B,
                             kDSTAT  = 0x0C, kSSTAT0 = 0x0D, kSSTAT1 = 0x0E,
                             kSSTAT2 = 0x0F, kDSA    = 0x10, kISTAT  = 0x14,
                             kCTEST0 = 0x18, kCTEST1 = 0x19, kCTEST2 = 0x1A,
                             kCTEST3 = 0x1B, kTEMP   = 0x1C, kDFIFO  = 0x20,
                             kCTEST4 = 0x21, kCTEST5 = 0x22, kCTEST6 = 0x23,
                             kDBC    = 0x24, kDCMD   = 0x27, kDNAD   = 0x28,
                             kDSP    = 0x2C, kDSPS   = 0x30, kSCRATCHA = 0x34,
                             kDMODE  = 0x38, kDIEN   = 0x39, kDWT    = 0x3A,
                             kDCNTL  = 0x3B, kADDER  = 0x3C, kSIEN0  = 0x40,
                             kSIEN1  = 0x41, kSIST0  = 0x42, kSIST1  = 0x43,
                             kMACNTL = 0x46, kGPCNTL = 0x47, kSTIME0 = 0x48,
                             kSTIME1 = 0x49, kRESPID = 0x4A, kSTEST0 = 0x4C,
                             kSTEST1 = 0x4D, kSTEST2 = 0x4E,  // 2026-08-05
                             kSTEST3 = 0x4F, kSIDL   = 0x50, kSODL   = 0x54,
                             kSBDL   = 0x58, kSCRATCHB = 0x5C;

    // ISTAT bits (53C810): ABRT 0x80, RST 0x40, SIGP 0x20, SEM 0x10,
    //                      CON 0x08, INTF 0x04, SIP 0x02, DIP 0x01
    static constexpr uint8_t kIstatRst = 0x40, kIstatSip = 0x02, kIstatDip = 0x01,
                             kIstatSigp = 0x20,   // H-2d: n810_def.h:320
                             kIstatAbrt = 0x80;   // H-3.3a: abort SCRIPTS
    // DSTAT: DFE 0x80, MDPE 0x40, BF 0x20, ABRT 0x10, SSI 0x08, SIR 0x04, IID 0x01
    static constexpr uint8_t kDstatDfe = 0x80, kDstatSsi = 0x08,
                             kDstatSir = 0x04, kDstatIid = 0x01,
                             kDstatAbrt = 0x10;   // H-3.3a: aborted
    // SIST0: MA 0x80, FC 0x40, SEL 0x20, RSL 0x10, SGE 0x08, UDC 0x04,
    //        RST 0x02, PAR 0x01;  SIST1: STO 0x04, GEN 0x02, HTH 0x01
    // kSist0Ma: SIST0<7> MA -- SCSI phase mismatch, initiator mode.
    // AUTHORITATIVE, 810-specific (upgraded from _PROVISIONAL 2026-08-01):
    //   Processor Support/PalcodeBitsavers/apisrm/apisrm/ref/n810_def.h:516
    //       #define n810_sist0_ma 128        // 0x80, bit 7
    //   and the struct n810_csr_sist0 bitfield at :526.  Full DEC layout:
    //       0 par, 1 rst, 2 udc, 3 sge, 4 rsl, 5 sel, 6 fc, 7 ma
    //   (our kSist0Rst = 0x02 matches bit 1 exactly).
    // The SRM's own driver for this chip confirms the consumer contract --
    //   .../ref/pke_driver.c:917  else if ( sist0 & n810_sist0_ma )
    //   .../ref/pke_driver.c:929  t = rl( dcmd_dbc );
    //   .../ref/pke_driver.c:930  byte_count = p->dbc_byte_count;
    // i.e. test MA, then read the RESIDUAL out of DBC -- which is what the
    // short-transfer path below produces.
    // SSTAT1<2:0> latch the live SCSI phase as MSG:C/D:I/O.  AUTHORITATIVE,
    // 810-specific: PalcodeBitsavers/apisrm/apisrm/ref/n810_def.h:293-296
    //     n810_sstat1_io 1 / _cd 2 / _msg 4 / _sdp 8 / _ff 240
    // and struct n810_csr_sstat1 at :299.  This model's phase enum is ALREADY
    // that encoding (kPhDatOut 0, kPhDatIn 1, kPhCmd 2, kPhSts 3, kPhMsgOut 6,
    // kPhMsgIn 7), so SSTAT1<2:0> == m_conn.phase with no translation.
    // WHY THIS MATTERS: 53C895 DM Ch.6 Block Move defines phase mismatch as
    // "the SCSI phase bits do not match the value stored in the SSTAT1
    // register".  Leaving SSTAT1 at 0 meant a driver reading it back after our
    // mismatch interrupt saw DATA OUT instead of STATUS, could not proceed, and
    // therefore never read SIST0 to acknowledge -- pinning the level and
    // storming b_irq<1>.  (2026-08-02, JRN-SCSI-034.)
    static constexpr uint8_t kSstat1PhaseMask = 0x07;
    static constexpr uint8_t kSist0Ma  = 0x80,
                             kSist0Rst = 0x02, kSist1Sto = 0x04,
                             // TODO(N810-GENTIMER) (2026-08-05): the
                             // general-purpose timer.  SIST1<1> GEN is
                             // armed from STIME1<3:0>; AXPBox
                             // src/Sym53C810.cpp:253/270/878/1457 is the
                             // corroborating implementation.
                             kSist1Gen = 0x02, kStime1GenMask = 0x0F;
    // DCNTL: SSM 0x10, IRQM 0x08, STD 0x04, SA 0x02, COM 0x01
    static constexpr uint8_t kDcntlSsm = 0x10, kDcntlStd = 0x04;
    // SCNTL1: EXC 0x80? (n810_scntl1_exc), RST 0x08 (assert SCSI RST)
    static constexpr uint8_t kScntl1Rst = 0x08;

    static constexpr unsigned kHostId    = 7;
    static constexpr uint32_t kBarWindow = 0x100;

    // ---- SCSI phases (n810_def.h) -----------------------------------------
    enum : uint8_t { kPhDatOut = 0, kPhDatIn = 1, kPhCmd = 2, kPhSts = 3,
                     kPhMsgOut = 6, kPhMsgIn = 7 };

    // ---- SCRIPTS INT vectors (n810_def.h n810_k_*) ------------------------
    // ok=0, reselected=4, got_msg_in=7, dat_out_done=8, dat_in_done=9;
    // errNNN vectors are literal (101..112).  Values live in guest DSPS.

    // ========================================================================
    // Register byte access with side effects
    // ========================================================================
    uint8_t regRead8(uint8_t off) noexcept
    {
        censusHit(off, 0);            // TODO(N810-CENSUS)
        switch (off) {
        case kISTAT: {
            uint8_t v = 0;
            if (m_dstatPend)               v |= kIstatDip;
            if (m_sist0Pend || m_sist1Pend) v |= kIstatSip;
            if (m_conn.active)             v |= 0x08;      // CON
            if (m_sigp)                    v |= kIstatSigp; // H-2d: readable
            ledgerCtl('R', off, v);          // TODO(N810-LEDGER)
            return v;
        }
        case kDSTAT: {                       // clear-on-read
            // I-7 (2026-08-03): H-3.3b routed SCRIPT rw-op reads through
            // here, so a script DSTAT read now CLEARS pending DIP state --
            // [CONFIRM] whether silicon clears DSTAT on SCRIPTS-side reads
            // or only on CPU reads.  If the 140935 kick storm is fed by
            // the script eating interrupts the driver needed, THIS row is
            // the evidence: a script read observing a nonzero pend.
            if (m_running && m_dstatPend) {
                uint64_t const n = m_dstatEats++;
                if (n < 16 || (n & 0xFFu) == 0) {
                    std::fprintf(stderr, "N810-DSTATEAT[%llu] pend=0x%02X "
                                 "dsp=0x%08X (script read clears DIP "
                                 "[CONFIRM])\n",
                                 static_cast<unsigned long long>(n),
                                 unsigned(m_dstatPend), reg32(kDSP));
                    std::fflush(stderr);
                }
            }
            uint8_t const v = static_cast<uint8_t>(m_dstatPend | kDstatDfe);
            m_dstatPend = 0;
            updateIrq();
            ledgerCtl('R', off, v);          // TODO(N810-LEDGER)
            return v;
        }
        case kSIST0: {                       // clear-on-read
            uint8_t const v = m_sist0Pend;
            m_sist0Pend = 0;
            updateIrq();
            ledgerCtl('R', off, v);          // TODO(N810-LEDGER)
            return v;
        }
        case kSIST1: {
            uint8_t const v = m_sist1Pend;
            m_sist1Pend = 0;
            updateIrq();
            ledgerCtl('R', off, v);          // TODO(N810-LEDGER)
            return v;
        }
        case kCTEST1: return 0xF0;           // FIFOs empty (FMT=1111, FFL=0)
        case kCTEST2: {                      // H-2d: SIGP read-clear
            // n810_def.h:348 n810_ctest2_sigp 64 (bit 6).  Reading CTEST2
            // returns SIGP and CLEARS it -- the only clear path.  Without
            // this the pke wait/signal handshake livelocks: every WAIT
            // RESELECT after the first signal would see SIGP still set and
            // jump immediately, forever (JRN-SCSI-037 H-2d trap note).
            uint8_t v = m_reg[off];
            if (m_sigp) v |= 0x40;
            m_sigp = false;
            return v;
        }
        case kSBCL: {                        // Edit A (2026-08-06): D4
            // The pke script reads this once per park lap (0xC000069C) and
            // branches on bit 3 at 0xC00006A4 with compare mask 0xF7.  Bit 3
            // now mirrors the SOCL<3> latch; every other bit still reads 0
            // (deviation D4, named at the top of this file).
            uint8_t const v = static_cast<uint8_t>(m_reg[kSOCL] & kSoclAtn);
            sbclRow(v);                      // TODO(N810-ATN)
            return v;
        }
        case kDFIFO: return 0;               // no residue modeled (D1)
        case kDSA:      case kDSA + 1:      case kDSA + 2:      case kDSA + 3:
        case kSCRATCHA: case kSCRATCHA + 1: case kSCRATCHA + 2: case kSCRATCHA + 3:
        case kSCRATCHB: case kSCRATCHB + 1: case kSCRATCHB + 2: case kSCRATCHB + 3:
            // I-3 read side (architect, 2026-08-03): the MAILBOX registers
            // ledger on READ as well as write, author-tagged via ledgerCtl's
            // "(script)" marker.  With H-3.3b routing rw-op source reads
            // through here, this row IS "what MOVE SCRATCHA0 TO SFBR
            // sampled" -- the datum that isolates preload-loss from
            // MM-routing faults.
            ledgerCtl('R', off, m_reg[off]);
            return m_reg[off];
        case kCTEST3: case kMACNTL: {
            // TODO(N810-SCRIPTDUMP) companion (JRN-SCSI-036 amendment D,
            // identity coherence): CTEST3<7:4> rev nibble and MACNTL<7:4>
            // chip-type nibble are the register-path chip-identity surface
            // (JRN-AUD-003 S-10: both reset all-zero, DISAGREEING with the
            // config-space revision ID).  Every read is a loud row (first
            // 16) so an identity probe by pke/SRM shows up in the same
            // capture as the script dump.  REMOVAL TRIGGER: rides
            // TODO(N810-LEDGER).
            uint8_t const v = m_reg[off];
            // H-2 cap sweep: first 16 PLUS every 256th (was first-16 only).
            unsigned const nId = m_idReads++;
            if (nId < 16 || (nId & 0xFFu) == 0) {
                std::fprintf(stderr, "N810-IDREAD[%u] reg=0x%02X val=0x%02X"
                             " (%s)\n", nId, unsigned(off), unsigned(v),
                             off == kCTEST3 ? "CTEST3 rev" : "MACNTL type");
                std::fflush(stderr);
            }
            return v;
        }
        default:      return m_reg[off];
        }
    }

    void regWrite8(uint8_t off, uint8_t v) noexcept
    {
        censusHit(off, 1);            // TODO(N810-CENSUS)
        // TODO(N810-LEDGER): control-register write ledger (see ledgerCtl).
        // I-3 (H-3.3, JRN-SCSI-039 Sec 6): DSA/SCRATCHA/SCRATCHB are the
        // MAILBOX the pke script polls -- they were dark channels while the
        // answer hid in them.  Ledgered at the same bounded cadence.
        if ((off >= kDSA      && off <= kDSA      + 3) ||
            (off >= kSCRATCHA && off <= kSCRATCHA + 3) ||
            (off >= kSCRATCHB && off <= kSCRATCHB + 3)) {
            ledgerCtl('W', off, v);
        } else switch (off) {
        case kISTAT: case kSCNTL0: case kSCNTL1: case kSCNTL3:
        case kDCNTL: case kDIEN:   case kDMODE:
        case kSIEN0: case kSIEN1:  case kSCID:   case kSXFER:
            ledgerCtl('W', off, v);
            break;
        default: break;
        }
        switch (off) {
        case kISTAT:
            // I-5 (H-3.3): control-flow ISTAT writes (ABRT/RST) are LOUD,
            // first-16 + every-256th -- the 103333 teardown's suspected
            // soft reset fell between the every-256th CTL samples and the
            // no-wake-after-DMODE anomaly stayed unconfirmable.
            if (v & (kIstatAbrt | kIstatRst)) {
                uint64_t const n = m_istatCritRows++;
                if (n < 16 || (n & 0xFFu) == 0) {
                    std::fprintf(stderr, "N810-ISTATCTL[%llu] val=0x%02X%s%s\n",
                                 static_cast<unsigned long long>(n),
                                 unsigned(v),
                                 (v & kIstatRst)  ? " RST"  : "",
                                 (v & kIstatAbrt) ? " ABRT" : "");
                    std::fflush(stderr);
                }
            }
            if (v & kIstatRst) { trace("ISTAT soft reset"); reset(); return; }
            // H-3.3a (architect-approved 2026-08-03, JRN-SCSI-039 Sec 4):
            // ISTAT<ABRT> aborts the SCRIPTS processor -- stop execution
            // (every park state cleared, DSP preserved) and raise
            // DSTAT<ABRT> -> DIP (53C895 DM ISTAT/DSTAT) [CONFIRM exact
            // page].  MEASURED consumer: pke TOUTROUT writes 0x80 at its
            // t=7 recovery and re-arms UCB DUETIM awaiting the abort-
            // completion interrupt (run 103333 POLLWAKE[1] val=0x80,
            // DUETIM 7 -> 0xB); without this the abort wait dies too.
            if (v & kIstatAbrt) {
                m_running    = false;
                m_parked     = false;
                m_pollParked = false;
                m_dstatPend |= kDstatAbrt;
                updateIrq();
            }
            // Batch H-2d: SIGP is SET by an ISTAT write (n810_def.h:320)
            // and cleared ONLY by a CTEST2 read -- NOT on resume; a driver
            // polling ISTAT to confirm its signal took must still see it.
            if (v & kIstatSigp) {
                // N-12 PROBE (2026-08-05, JRN-SCSI-041 Sec 13.2).  The
                // host wrote ISTAT 63,533 times in run 20260805_132410 and
                // NEVER read it -- ~63,520 of those are SIGP (only 13
                // carried ABRT/RST via N810-ISTATCTL).  Neither stalled KP
                // can issue that many writes, so a THIRD host-side actor
                // is driving SIGP once per park.  Naming it names the
                // likely home of the missing EXE$KP_RESTART (Sec 11.7).
                // SCOPE LIMIT, stated: this model has NO access to the
                // guest PC -- ioWrite carries no CPU context.  True caller
                // identity needs a CPU/pipeline-layer change.  This row
                // uses the project's standing correlation instead: the
                // nearest preceding PCSAMPLE cyc names the era, exactly as
                // every other bounded row in this file does.
                uint64_t const nsig = m_sigpRows++;
                if (nsig < 64 || (nsig & 0xFFu) == 0) {
                    std::fprintf(stderr,
                        "N810-SIGP[%llu] parked=%u dsp=0x%08X alt=0x%08X\n",
                        static_cast<unsigned long long>(nsig),
                        unsigned(m_parked ? 1 : 0), reg32(kDSP), m_parkedAlt);
                    std::fflush(stderr);
                }
                m_sigp = true;
                if (m_parked) resumeFromPark();
            }
            m_reg[off] = v & ~(kIstatSip | kIstatDip | kIstatSigp);
            return;                        // (SIGP composed from m_sigp)
        case kSCNTL1:
            m_reg[off] = v;
            if (v & kScntl1Rst) {             // SCSI bus reset -> SIST0<RST>
                m_sist0Pend |= kSist0Rst;
                m_conn = Conn{};
                // W-4 (2026-08-04): RST on the bus is a TARGET-visible
                // event -- every attached target re-latches UNIT ATTENTION.
                // SEAM SCOPE (architect review): this fires on HOST MMIO
                // writes, the path the VMS port driver uses to assert bus
                // reset.  A SCRIPT-side rw-op WRITE to SCNTL1 still
                // bypasses regWrite8 (H-3.3b routed source READS only) --
                // TODO(N810-SCNTL1-SCRIPT-RST): wire if a script-authored
                // bus reset is ever observed in the ledger.
                for (auto* t : m_targets)
                    if (t != nullptr) t->busReset();
                updateIrq();
            }
            return;
        case kSTIME1: {
            // TODO(N810-GENTIMER): arm the general-purpose timer.
            // AXPBox Sym53C810.cpp:878 does the same on this write.
            m_reg[off] = v;
            uint32_t const was = m_genTimer;
            m_genTimer = (v & kStime1GenMask) ? kGenTicks : 0u;
            uint64_t const n = m_genArmRows++;
            if (n < 64 || (n & 0xFFu) == 0) {
                std::fprintf(stderr,
                    "N810-GENTIMER[%llu] ARM stime1=0x%02X gen=0x%02X "
                    "ticks %u->%u sien1=0x%02X _PROVISIONAL\n",
                    static_cast<unsigned long long>(n), unsigned(v),
                    unsigned(v & kStime1GenMask), was, m_genTimer,
                    unsigned(m_reg[kSIEN1]));
                std::fflush(stderr);
            }
            return;
        }
        case kDSTAT: case kSIST0: case kSIST1:
            return;                           // read-only status
        case kCTEST3:
            return;                           // CLF/FLF: FIFOs not modeled
        case kDCNTL:
            m_reg[off] = static_cast<uint8_t>(v & ~kDcntlStd);  // STD self-clears
            if (v & kDcntlStd) startScripts("DCNTL<STD>");
            return;
        case kDIEN: case kSIEN0: case kSIEN1:
            // C-4 rider (JRN-SCSI-041): a mask write RE-EVALUATES the
            // interrupt line -- enabling a source with its pend already
            // set asserts INTA; masking a pending source drops it.
            // Previously the line only updated on the next pend change.
            m_reg[off] = v;
            updateIrq();
            return;
        default:
            m_reg[off] = v;
            return;
        }
    }

    uint32_t reg32(uint8_t off) const noexcept
    {
        return  static_cast<uint32_t>(m_reg[off])
             | (static_cast<uint32_t>(m_reg[off + 1]) << 8)
             | (static_cast<uint32_t>(m_reg[off + 2]) << 16)
             | (static_cast<uint32_t>(m_reg[off + 3]) << 24);
    }
    void setReg32(uint8_t off, uint32_t v) noexcept
    {
        m_reg[off]     = static_cast<uint8_t>(v);
        m_reg[off + 1] = static_cast<uint8_t>(v >> 8);
        m_reg[off + 2] = static_cast<uint8_t>(v >> 16);
        m_reg[off + 3] = static_cast<uint8_t>(v >> 24);
    }

    // ========================================================================
    // Interrupt plumbing
    // ========================================================================
    void raiseDma(uint8_t bit, uint32_t dsps) noexcept
    {
        // I-6 (2026-08-03, run 140935 read-out): the H-3.3b run replaced
        // the deadlock with a 50k+-session kick storm -- driver ISR sees
        // SIR, re-kicks, forever.  WHICH completion code the script keeps
        // reporting was invisible (DSPS never logged).  Bounded: first 32
        // + every 1024th.  REMOVAL TRIGGER: rides TODO(N810-LEDGER).
        uint64_t const n = m_intRows++;
        if (n < 32 || (n & 0x3FFu) == 0) {
            std::fprintf(stderr, "N810-INT[%llu] dstat=0x%02X dsps=0x%08X "
                         "dsp=0x%08X\n",
                         static_cast<unsigned long long>(n), unsigned(bit),
                         dsps, reg32(kDSP));
            std::fflush(stderr);
        }
        setReg32(kDSPS, dsps);
        m_dstatPend |= bit;
        m_running = false;
        updateIrq();
    }
    void updateIrq() noexcept
    {
        bool const dip = (m_dstatPend & m_reg[kDIEN]) != 0;
        bool const sip = (m_sist0Pend & m_reg[kSIEN0]) != 0
                      || (m_sist1Pend & m_reg[kSIEN1]) != 0;
        bool const level = dip || sip;
        if (level == m_irq) return;
        m_irq = level;
        if (m_intr) m_intr(level);
    }

    // ========================================================================
    // SCRIPTS engine
    // ========================================================================
    void startScripts(char const* why) noexcept
    {
        if (!m_dmaRead) {
            // MSVC /std:c++20 pipeline: no std::println -- fprintf per house style.
            std::fprintf(stderr, "Ncr53C810: SCRIPTS start (%s) with no DMA "
                                 "seam wired -- ignored\n", why);
            return;
        }
        trace2("SCRIPTS start", reg32(kDSP));
        ++m_scriptSession;                        // V-1 probe session counter
        // TODO(N810-LEDGER) (2026-08-03, architect-approved): one line per
        // SCRIPTS session, unconditional.  ~2000 sessions/boot = cheap, and
        // it is the ONLY unconditional record that the guest kicked the
        // engine at all -- the JRN-SES-003 dump showed PKE stalling with a
        // VIRGIN SCDRP while every chip-level action (CSR writes, silent
        // script runs) was dark in the log.  Era via nearest PCSAMPLE cyc.
        // REMOVAL TRIGGER: the PKE port-init stall is root-caused.
        std::fprintf(stderr, "N810-SESSION[%u] dsp=0x%08X (%s)\n",
                     m_scriptSession, reg32(kDSP), why);
        std::fflush(stderr);
        // TODO(N810-SCRIPTDUMP) (2026-08-03, JRN-SCSI-036 amendment D):
        // first time each distinct DSP starts a session, dump the program.
        // See the CHANGE HISTORY entry; removal rides TODO(N810-LEDGER).
        scriptDump(reg32(kDSP));
        m_parked = false;                         // a fresh DSP write
        m_pollParked = false;                     // supersedes any park (H-3)
        runScriptsLoop();
    }

    // Shared executor loop (H-2d refactor): used by startScripts (new
    // session) and resumeFromPark (SAME session continuing) so the session
    // ledger counts sessions, not resumes.
    void runScriptsLoop() noexcept
    {
        m_running = true;
        int guard = 100000;                       // per-wake instruction budget
        while (m_running && guard-- > 0) {
            stepScripts();
            if ((m_reg[kDCNTL] & kDcntlSsm) && m_running) {
                m_dstatPend |= kDstatSsi;         // single-step (D3)
                m_running = false;
                updateIrq();
            }
        }
        if (guard <= 0 && m_running) {
            // TODO(N810-POLLPARK) _PROVISIONAL shim (Batch H-3, JRN-SCSI-038,
            // architect-approved 2026-08-03).  Budget exhaustion is NO LONGER
            // a kill: the VMS pke script legitimately busy-polls its mailbox
            // registers from SCRIPTS (verified live: 100k-instr loop between
            // 0xC00012DC/0xC00015B8 DSA readbacks), and run-to-completion
            // freezes the CPU while it runs, so the polled state can never
            // change DURING the loop.  On silicon this loop runs BESIDE the
            // CPU.  Shim semantics: suspend here (poll-park, DSP preserved),
            // wake on the next host MMIO write to the chip (ioWrite seam --
            // the whole access, never a partial byte), fresh budget per
            // wake.  KNOWN LIMIT, named: work posted ONLY through RAM
            // mailboxes (no register write afterward) does not wake the
            // script -- that evidence, if seen, feeds H-4 directly.
            // REMOVAL TRIGGER: superseded by Batch H-4 budgeted stepping
            // (deterministic guest-cycle-cadence pollTick from the machine
            // loop), scoped in JRN-SCSI-038 Sec 5.
            m_running    = false;
            m_pollParked = true;
            uint64_t const n = m_pollParks++;
            if (n < 8 || (n & 0xFFu) == 0) {
                std::fprintf(stderr, "N810-POLLPARK[%llu] dsp=0x%08X "
                             "wakes=%llu _PROVISIONAL (H-3 shim)\n",
                             static_cast<unsigned long long>(n), reg32(kDSP),
                             static_cast<unsigned long long>(m_pollWakes));
                std::fflush(stderr);
            }
            censusDump("pollpark");        // TODO(N810-CENSUS)
        }
    }

    // H-3: wake a poll-parked script after a completed host MMIO write.
    // H-3.1 (JRN-SCSI-038 Sec 7 I-1): the row records WHICH write woke the
    // script -- run 20260802_212601's single wake write fell between the
    // every-256th N810-CTL samples and its identity was lost.  The wake row
    // is first-8 + every-256th (unchanged); identity rides the row itself.
    void wakeFromPollPark(uint8_t off, uint64_t value, uint8_t width) noexcept
    {
        m_pollParked = false;
        uint64_t const n = m_pollWakes++;
        if (n < 8 || (n & 0xFFu) == 0) {
            std::fprintf(stderr, "N810-POLLWAKE[%llu] dsp=0x%08X "
                         "reg=0x%02X val=0x%llX w=%u\n",
                         static_cast<unsigned long long>(n), reg32(kDSP),
                         off, static_cast<unsigned long long>(value),
                         static_cast<unsigned>(width));
            std::fflush(stderr);
        }
        runScriptsLoop();
    }

    // Batch H-2d: ISTAT<SIGP> written while a WAIT RESELECT is parked
    // breaks the wait -- execution continues at the parked alternate
    // address, in the SAME SCRIPTS session (no new N810-SESSION row).
    // SIGP itself stays set until CTEST2 is read (n810_def.h:348); the
    // pke script performs that read on the alternate path itself.
    void resumeFromPark() noexcept
    {
        m_parked = false;
        setReg32(kDSP, m_parkedAlt);
        std::fprintf(stderr, "N810-RESUME session=%u alt=0x%08X (SIGP)\n",
                     m_scriptSession, m_parkedAlt);
        std::fflush(stderr);
        runScriptsLoop();
    }

    // TODO(N810-SCRIPTDUMP) (2026-08-03, JRN-SCSI-036 amendment D): one-shot
    // hex dump of the first 0x40 dwords at each DISTINCT session-start DSP
    // (max 6 per boot).  The JRN-AUD-003 top-ranked measurement, now cheap:
    // names PKE's entire construct set in one capture so Batch H-1 is scoped
    // by evidence, not by one instruction.  Bounded: 6 dumps x 16 lines.
    // REMOVAL TRIGGER: rides TODO(N810-LEDGER) -- remove when C1 falls.
    void scriptDump(uint32_t dsp) noexcept
    {
        if (!m_dmaRead) return;
        for (unsigned i = 0; i < m_dumpCount; ++i)
            if (m_dumpedDsp[i] == dsp) return;    // already dumped
        // Cap 6 -> 32 (2026-08-05, Sec 13.4): six slots were consumed by
        // the console-era script before the PKE init script ran, so the
        // DSPs the census names were never captured.  32 covers every
        // distinct firstDsp the 20260805_132410 census recorded (48
        // offsets, far fewer distinct DSPs) with headroom.
        if (m_dumpCount >= 32) return;            // slot cap
        m_dumpedDsp[m_dumpCount++] = dsp;
        // Window 0x400 (2026-08-03, second capture): the 0x100 first cut
        // proved too small -- the PKE dispatch ladder relative-jumps to
        // ~+0x1A4..+0x1B0 and the WAIT RESELECT alternate is +0x244, all
        // past the first window.  0x400 covers every target seen.
        // WIDENED DOWN 0x1600 (architect, 2026-08-03, third capture): the
        // route into the init-done INT (+0x210) goes through a CALL at
        // rel -0x14D8 and the poll loop back-jumps rel -0x544 -- the
        // LIBRARY below the entry DSP was never captured, so "never
        // routes to init-done" was inferred from a missing INT row, not
        // read from code.  Rows below the entry carry -0xNNNN offsets.
        constexpr uint32_t kBelow = 0x1600, kAbove = 0x400;
        uint32_t const lo = (dsp >= kBelow) ? dsp - kBelow : 0;
        uint32_t const len = (dsp - lo) + kAbove;
        std::vector<uint8_t> buf(len);
        m_dmaRead(lo, buf.data(), len);
        for (uint32_t row = 0; row + 0x10 <= len; row += 0x10) {
            int32_t const off = static_cast<int32_t>(lo + row)
                              - static_cast<int32_t>(dsp);
            std::fprintf(stderr,
                "N810-SCRIPTDUMP dsp=0x%08X %c0x%04X: %08X %08X %08X %08X\n",
                dsp, off < 0 ? '-' : '+',
                static_cast<uint32_t>(off < 0 ? -off : off),
                le32(&buf[row + 0x0]), le32(&buf[row + 0x4]),
                le32(&buf[row + 0x8]), le32(&buf[row + 0xC]));
        }
        std::fflush(stderr);
    }

    void stepScripts() noexcept
    {
        uint32_t const dsp = reg32(kDSP);
        uint8_t insn[8];
        m_dmaRead(dsp, insn, 8);
        uint32_t const w0 = le32(&insn[0]);
        uint32_t const w1 = le32(&insn[4]);
        setReg32(kDBC, w0);                       // DCMD:DBC mirror
        setReg32(kDNAD, w1);
        setReg32(kDSP, dsp + 8);
        scriptsTrace(dsp, w0, w1);
        v1Probe(dsp, w0, w1);                     // JRN-AUD-003 V-1 (bounded)

        switch (w0 >> 30) {                       // type
        case 0: execBlockMove(w0, w1);   return;
        case 1: execIoOrRw(w0, w1);      return;
        case 2: execTransferCtl(w0, w1); return;
        default:                                  // type 3 (Batch H-2a)
            // DCMD<29> splits type 3: 0 = Memory Move, 1 = Load/Store.
            // Load/Store does NOT exist on the plain 53C810 -- DEC's own
            // programming surface for this part (apisrm ref/n810_def.h +
            // n810_script_macros.mar) defines n810_k_mm=3 and NO load/store
            // symbols at all.  Faithful response is illegal-instruction;
            // loud, because a guest emitting type-7 believes it is talking
            // to an 810A+ and that is an IDENTITY defect, not a SCRIPTS
            // defect (JRN-SCSI-037 P-3, architect ruling 2026-08-03).
            if ((w0 >> 29) & 1u) {
                std::fprintf(stderr, "Ncr53C810: SCRIPTS LOAD/STORE (810A+ "
                             "only) at 0x%08X w0=0x%08X -- absent on 53C810 "
                             "silicon, IID (identity check: JRN-SCSI-037 "
                             "P-3)\n", dsp, w0);
                std::fflush(stderr);
                raiseDma(kDstatIid, 0);
                return;
            }
            execMemoryMove(w0, w1, dsp);
            return;
        }
    }

    // ---- type 3: Memory Move (Batch H-2a, JRN-SCSI-037) --------------------
    // AUTHORITY: 53C895 Data Manual Ch.6 "Memory Move Instructions" (6-21):
    //   - THREE-dword instruction: DCMD:DBC (type+count), DSPS (source),
    //     TEMP (destination).  "The DSPS and DSA registers are additional
    //     holding registers used during the Memory Move; however, the
    //     contents of the DSA register are preserved."
    //   - "Bits 28-25 Reserved ... must be zero.  If any of these bits is
    //     set, an illegal instruction interrupt will occur."
    //   - Bit 24 No Flush: prefetch-unit control; NO prefetch unit on the
    //     plain 810 and DEC's assembler never emits it (cop macro,
    //     n810_script_macros.mar:291).  _PROVISIONAL: execute + loud row,
    //     never IID.  [CONFIRM] against an 810-specific datasheet.
    //   - "Both the source and destination addresses must start with the
    //     same address alignment (A1-0 must be the same).  If ... not
    //     aligned, then an illegal instruction interrupt will occur."
    //   - Own-window decode: "it could be accessed during a Memory Move
    //     operation if the source or destination address decodes to within
    //     the chip's register space.  If this occurs, the register
    //     indicated by the lower seven bits of the address is taken to be
    //     the data source or destination."  Routing tests the FULL BAR
    //     extent; the &0x7F alias is applied inside (BAR+0x80..0xFF wraps
    //     onto the register file, it does not fall through to the bus).
    //   - DMODE<5> SIOM / <4> DIOM select I/O vs memory space per side.
    void execMemoryMove(uint32_t w0, uint32_t src, uint32_t dsp) noexcept
    {
        uint8_t d[4];                             // third dword: destination
        m_dmaRead(dsp + 8, d, 4);
        uint32_t const dst = le32(d);
        setReg32(kDSP, dsp + 12);                 // next-DSP FIRST: a move
                                                  // that lands on DSP wins
        uint32_t const count = w0 & 0x00FFFFFF;
        // I-4 (H-3.3): the ledger row moves to AFTER the copy on the
        // success path so it can carry the DATA MOVED (what does the
        // script actually see in its mailbox?).  Fault paths keep a
        // data-less row here so every MM still ledgers exactly once.
        if (w0 & 0x1E000000u) {                   // bits 28:25: cited IID
            ledgerMm(dsp, src, dst, count);
            std::fprintf(stderr, "Ncr53C810: MM reserved bits set w0=0x%08X "
                         "at 0x%08X -- IID (895 DM 6-21)\n", w0, dsp);
            std::fflush(stderr);
            raiseDma(kDstatIid, 0);
            return;
        }
        if (w0 & 0x01000000u) {                   // bit 24 No Flush:
            std::fprintf(stderr, "Ncr53C810: MM No-Flush bit set w0=0x%08X "
                         "at 0x%08X -- _PROVISIONAL execute [CONFIRM 810 "
                         "datasheet] (JRN-SCSI-037 H-2a)\n", w0, dsp);
            std::fflush(stderr);                  // execute: no prefetch unit
        }
        if (count == 0) {                         // uncited either way: loud
            ledgerMm(dsp, src, dst, count);
            std::fprintf(stderr, "Ncr53C810: MM count=0 at 0x%08X -- no-op "
                         "[CONFIRM] (JRN-SCSI-037 H-2a)\n", dsp);
            std::fflush(stderr);
            return;
        }
        if ((src & 3u) != (dst & 3u)) {           // A1:0 must match: cited
            ledgerMm(dsp, src, dst, count);
            std::fprintf(stderr, "Ncr53C810: MM alignment mismatch src=0x%08X"
                         " dst=0x%08X at 0x%08X -- IID (895 DM 6-21)\n",
                         src, dst, dsp);
            std::fflush(stderr);
            raiseDma(kDstatIid, 0);
            return;
        }
        // Holding registers: DSPS/TEMP clobbered by the move (cited); DSA
        // preserved (cited).  Final values = pointers advanced past the
        // ends, DBC count decremented to zero -- INFERENCE, not cited; the
        // manual names the holding-register roles but not the end state.
        setReg32(kDSPS, src);
        setReg32(kTEMP, dst);
        bool const sIo = (m_reg[kDMODE] >> 5) & 1u;   // SIOM
        bool const dIo = (m_reg[kDMODE] >> 4) & 1u;   // DIOM
        std::vector<uint8_t> buf(count);
        if (inRegWindow(src, sIo)) {
            for (uint32_t i = 0; i < count; ++i)
                buf[i] = regRead8(static_cast<uint8_t>((src + i) & 0x7F));
        } else {
            m_dmaRead(src, buf.data(), count);
        }
        if (inRegWindow(dst, dIo)) {
            for (uint32_t i = 0; i < count; ++i)
                regWrite8(static_cast<uint8_t>((dst + i) & 0x7F), buf[i]);
        } else {
            m_dmaWrite(dst, buf.data(), count);
        }
        {   // I-4 (H-3.3): success-path row carries the first dword moved.
            uint32_t mv = 0;
            for (uint32_t i = 0; i < count && i < 4; ++i)
                mv |= static_cast<uint32_t>(buf[i]) << (8u * i);
            ledgerMm(dsp, src, dst, count, mv, true);
        }
        setReg32(kDSPS, src + count);
        setReg32(kTEMP, dst + count);
        setReg32(kDBC, w0 & 0xFF000000u);         // DCMD kept, count -> 0
        // Script CONTINUES: a Memory Move is not a terminator.
    }

    // Own-register-window decode for MM (H-2a).  FULL BAR extent tested;
    // &0x7F alias applied by the caller (895 DM: "the register indicated by
    // the lower seven bits of the address").
    bool inRegWindow(uint32_t a, bool ioSpace) const noexcept
    {
        uint64_t const base = ioSpace ? m_ioBase : m_memBase;
        return base != 0 && a >= base && a < base + kBarWindow;
    }

    // TODO(N810-LEDGER) rider (H-1c as amended): Memory Move is MODELED now,
    // so it must not print NEW-CONSTRUCT -- but the instrument stays through
    // the verification run.  Bounded: first 16 + every 4096th.
    // REMOVAL TRIGGER: rides TODO(N810-LEDGER) -- remove when C1 falls.
    // I-4 (H-3.3, JRN-SCSI-039 Sec 6): rows on the success path carry the
    // first dword MOVED -- the mailbox values the poll loop actually reads
    // were invisible while the deadlock's answer sat in them.  Fault paths
    // pass haveData=false (row without val=).
    void ledgerMm(uint32_t dsp, uint32_t src, uint32_t dst,
                  uint32_t count, uint32_t data = 0,
                  bool haveData = false) noexcept
    {
        uint64_t const n = m_mmRows++;
        if (n < 16 || (n & 0xFFFu) == 0) {
            if (haveData)
                std::fprintf(stderr, "N810-MM[%llu] at=0x%08X src=0x%08X "
                             "dst=0x%08X cnt=%u val=0x%08X\n",
                             static_cast<unsigned long long>(n), dsp, src,
                             dst, count, data);
            else
                std::fprintf(stderr, "N810-MM[%llu] at=0x%08X src=0x%08X "
                             "dst=0x%08X cnt=%u\n",
                             static_cast<unsigned long long>(n), dsp, src,
                             dst, count);
            std::fflush(stderr);
        }
    }

    // ---- type 0: block move ----------------------------------------------
    void execBlockMove(uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t  const phase = (w0 >> 24) & 0x7;
        bool     const tab   = (w0 >> 28) & 0x1;
        uint32_t count = w0 & 0x00FFFFFF;
        uint32_t addr  = w1;
        if (tab) {                                // table indirect off DSA
            uint8_t ent[8];
            m_dmaRead(reg32(kDSA) + (w1 & 0x00FFFFFF), ent, 8);
            count = le32(&ent[0]) & 0x00FFFFFF;
            addr  = le32(&ent[4]);
        }
        if (!m_conn.active) {
            std::fprintf(stderr, "Ncr53C810: block move (phase %u) with no "
                                 "connection -- IID\n", phase);
            raiseDma(kDstatIid, 0);
            return;
        }
        if (phase != m_conn.phase) {
            // The pke script guards every move with phase-conditional jumps,
            // so a mismatch here is a model bug, not guest behavior.  Loud.
            std::fprintf(stderr, "Ncr53C810: PHASE MISMATCH move=%u bus=%u "
                                 "(MA residue path unmodeled, D1)\n",
                         phase, m_conn.phase);
            m_sist0Pend |= 0x80;                  // MA
            m_running = false;
            updateIrq();
            return;
        }
        if (count == 0) return;
        moveData(phase, addr, count);
    }

    // ---- type 1: I/O class or read/write-register class -------------------
    void execIoOrRw(uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t const opcode = (w0 >> 27) & 0x7;
        if (opcode >= 5) { execRw(w0); return; }  // 5/6/7 = rw class
        switch (opcode) {
        case 0: execSelect(w0, w1);  return;      // SELECT (ATN via bit 24)
        case 1:                                    // WAIT DISCONNECT
            m_conn = Conn{};                      // D2: immediate bus-free
            return;
        case 2: {                                  // WAIT RESELECT (H-2d)
            // Batch H-2d (2026-08-03, JRN-SCSI-037): the VMS pke script's
            // IDLE idiom -- park in WAIT RESELECT; the driver breaks the
            // wait by writing ISTAT<SIGP> (n810_def.h:320, 0x20) and the
            // chip jumps to the ALTERNATE address; SIGP stays set until a
            // CTEST2 read clears it (n810_def.h:348, bit 6).  Replaces the
            // old D2 STO shim, which was semantically wrong here (a driver
            // parked on purpose got a timeout it never asked for).
            // w0<26> = relative alternate (n810_def.h:665 n810_io_rel).
            bool const rel = (w0 >> 26) & 1u;
            uint32_t const alt = rel ? reg32(kDSP) + sext24(w1) : w1;
            if (m_sigp) {
                // SIGP already pending at execution: jump immediately,
                // never park.  SIGP is NOT cleared here -- only the
                // CTEST2 read clears it (the script does that itself).
                setReg32(kDSP, alt);
                return;
            }
            m_parked    = true;                    // distinct PARKED state:
            m_parkedAlt = alt;                     // NOT script-ended -- the
            m_running   = false;                   // session ledger must not
            std::fprintf(stderr,                   // record a completion.
                "N810-PARK session=%u alt=0x%08X (WAIT RESELECT)\n",
                m_scriptSession, alt);
            std::fflush(stderr);
            censusDump("waitresel");               // TODO(N810-CENSUS)
            lapArm();                              // TODO(N810-LAPTRACE)
            return;                                // no interrupt, no STO
        }
        case 3:                                    // SET   ATN/ACK/carry
        case 4: {                                  // CLEAR ATN/ACK/carry
            // Batch H-5 Edit A (2026-08-06): ATN is a LATCH with a measured
            // consumer, not bookkeeping.  The pke script CLEARs it twice per
            // park lap and reads SBCL three instructions later.  ACK and
            // carry stay unmodeled -- but LOUD, never silently dropped.
            bool const set = (opcode == 3);
            if ((w0 & kSetAtn) != 0) {             // Edit A: SOCL<3> latch
                uint8_t const before = m_reg[kSOCL];
                if (set) m_reg[kSOCL] = static_cast<uint8_t>(before |  kSoclAtn);
                else     m_reg[kSOCL] = static_cast<uint8_t>(before & ~kSoclAtn);
                atnRow(set, before, m_reg[kSOCL]);          // TODO(N810-ATN)
            }
            if ((w0 & (kSetAck | kSetCarry)) != 0)
                setClrUnmodeledRow(set, w0);                // TODO(N810-ATN)
            return;
        }
        default:
            raiseDma(kDstatIid, 0);
            return;
        }
    }

    void execSelect(uint32_t w0, uint32_t w1) noexcept
    {
        (void) w1;                                 // alternate addr (resel) unused (D2)
        // Batch H-5 Edit A (2026-08-06): w0<24> = SELECT WITH ATN (DM bit 24,
        // confirmed against the 895 DM text in Batch H-6).
        bool const withAtn  = ((w0 >> 24) & 1u) != 0;
        // Batch H-6 (2026-08-06): N-17 CLOSED.  w0<25> = table indirect (DM
        // bit 25): entry = DSA + sext24(w0<23:0>), &~3 (810), id =
        // entry<19:16> (AXPBox Sym53C810.cpp:1758-1773 corroborates).  The
        // old inline read decoded bits of the TABLE OFFSET as an id and
        // worked only because offset 0x02C has zero in 19:16.  Entry
        // SCNTL3/SXFER bytes are not applied: no sync/wide model exists.
        bool const tableInd = ((w0 >> 25) & 1u) != 0;
        unsigned const inlineId = (w0 >> 16) & 0xF;    // legacy decode, kept
        unsigned id = inlineId;                        // for the mismatch row
        if (tableInd) {
            uint32_t const tAddr =
                (reg32(kDSA) + sext24(w0 & 0x00FFFFFFu)) & ~3u;
            uint8_t tb[4];
            m_dmaRead(tAddr, tb, 4);
            uint32_t const t0 = le32(&tb[0]);
            id = (t0 >> 16) & 0xF;
            selTableRow(tAddr, t0, id, inlineId);      // Batch H-6 rows
        }
        scsi::VirtualScsiDevice* t = (id < 8) ? m_targets[id] : nullptr;
        if (t == nullptr) {
            // Selection timeout -- how the pk driver learns an ID is empty.
            m_sist1Pend |= kSist1Sto;
            m_running = false;
            updateIrq();
            return;
        }
        m_conn = Conn{};
        m_conn.active   = true;
        m_conn.targetId = static_cast<uint8_t>(id);
        if (withAtn) m_reg[kSOCL] =         // Edit A: SELECT WITH ATN asserts
            static_cast<uint8_t>(m_reg[kSOCL] | kSoclAtn);
        selAtnRow(id, withAtn, m_reg[kSOCL]);          // TODO(N810-ATN)
        // Batch H-6 (2026-08-06): N810-SELPHASE RESOLVED against the 895 DM
        // (bit 24 ties SATN/ to the selection phase itself).  With ATN the
        // target enters MSG OUT (identify follows); without ATN it proceeds
        // straight to COMMAND.  The no-ATN arm is UNEXERCISED by every
        // observed guest (pke and console both select with ATN) -- loud on
        // first use, because a silently-exercising untested arm is how
        // S-10-class surprises are made.
        if (withAtn) {
            setPhase(kPhMsgOut);           // selected with ATN -> message out
        } else {
            selNoAtnRow(w0);               // Batch H-6: LOUD, first 8
            setPhase(kPhCmd);              // plain select -> command phase
        }
        return;
    }

    void execRw(uint32_t w0) noexcept
    {
        uint8_t const op   = (w0 >> 24) & 0x7;     // copy 0 / or 2 / and 4 / add 6
        uint8_t const rega = (w0 >> 16) & 0xFF;
        uint8_t const data = (w0 >> 8) & 0xFF;
        uint8_t const opc  = (w0 >> 27) & 0x7;     // 5 write, 6 read, 7 modify
        // Batch H-2c (2026-08-03, JRN-SCSI-037): opc 5 "write SFBR to reg"
        // operates on SFBR, not on the target register -- the old code read
        // m_reg[rega] as the operand for ALL opcodes, so SFBR-op forms
        // (VMS pke dword 27: 6C370700 = SFBR AND 0x07 -> SCRATCHA3)
        // computed from the wrong source.  895 DM Ch.6 Read/Write class:
        // 101 = write SFBR to register, 110 = read register to SFBR,
        // 111 = read-modify-write.  Fields: n810_def.h:702/706.
        // H-3.3b (architect-approved 2026-08-03, JRN-SCSI-039): opc 6/7
        // SOURCE READS route through regRead8, not raw m_reg[] -- the
        // register file's composed/side-effect reads belong to the SCRIPTS
        // processor too.  The measured casualty of the old path: the pke
        // script's MOVE CTEST2 to SFBR at +0x2A4 sampled m_reg[0x1A] raw,
        // where SIGP is NEVER present (composed from m_sigp only inside
        // regRead8) -- the script woke on SIGP, read bit6=0, concluded
        // "no signal", and returned to idle without dispatching; m_sigp
        // stayed latched forever.  Root-cause candidate for the C1
        // two-party deadlock.  Side effects now reaching script reads:
        // CTEST2 SIGP read-clear (the H-2d contract, finally honored on
        // the path the guest actually uses), ISTAT composition, DSTAT/
        // SIST clear-on-read.  [CONFIRM] 895 DM Ch.6 rw-op register-access
        // semantics.  WRITE side (opc 5/7 results) stays direct m_reg[]
        // this batch -- separate decision.
        uint8_t const cur  = (opc == 5) ? m_reg[kSFBR]
                                        : regRead8(rega & 0x7F);
        uint8_t res = cur;
        switch (op & 0x6) {
        case 0: res = data;               break;   // copy (move data to reg)
        case 2: res = cur | data;         break;   // or
        case 4: res = cur & data;         break;   // and  (bic emits and-mask)
        case 6: res = static_cast<uint8_t>(cur + data); break;
        }
        if (opc == 6) m_reg[kSFBR] = res;
        else          m_reg[rega & 0x7F] = res;
    }

    // ---- type 2: transfer control -----------------------------------------
    void execTransferCtl(uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t const opcode  = (w0 >> 27) & 0x7;  // jmp 0 / call 1 / ret 2 / int 3
        bool const cmpPhase   = (w0 >> 17) & 1;
        bool const cmpData    = (w0 >> 18) & 1;
        bool const jmpIfTrue  = (w0 >> 19) & 1;
        uint8_t const phase   = (w0 >> 24) & 0x7;
        uint8_t const data    = w0 & 0xFF;
        uint8_t const mask    = (w0 >> 8) & 0xFF;

        bool cond = true;
        if (cmpPhase) cond = cond && (m_conn.active && m_conn.phase == phase);
        if (cmpData)  cond = cond && (((m_reg[kSFBR] ^ data) & ~mask) == 0);
        if (cond != jmpIfTrue) return;             // condition failed -> fall through

        // Batch H-2b (2026-08-03, JRN-SCSI-037): w0<23> = RELATIVE transfer
        // (n810_def.h:726 n810_tc_rel 0x800000).  Destination = next-DSP +
        // sext24(w1); reg32(kDSP) here IS the next-instruction address (the
        // fetch advanced it).  The VMS pke script is relative nearly
        // throughout, including 24-bit negative offsets (00FFFABC = -0x544).
        // Old code treated w1 as absolute always -- first conditional jump
        // teleported DSP to a raw offset and died.
        bool const rel = (w0 >> 23) & 1u;
        uint32_t const dest = rel ? reg32(kDSP) + sext24(w1) : w1;

        switch (opcode) {
        case 0: setReg32(kDSP, dest); return;      // JUMP
        case 1:                                    // CALL
            setReg32(kTEMP, reg32(kDSP));
            setReg32(kDSP, dest);
            return;
        case 2: setReg32(kDSP, reg32(kTEMP)); return;   // RETURN
        case 3: raiseDma(kDstatSir, w1); return;   // INT: vector in DSPS
        default: raiseDma(kDstatIid, 0); return;
        }
    }

    // ========================================================================
    // Phase engine (ScsiBus-lite target side)
    // ========================================================================
    struct Conn {
        bool     active   = false;
        uint8_t  targetId = 0;
        uint8_t  phase    = kPhMsgOut;
        uint8_t  lun      = 0;
        // command assembly + data
        uint8_t  cdb[16]  = {};
        uint8_t  cdbLen   = 0;
        std::vector<uint8_t> data;     // data-in (from target) or data-out (to target)
        uint32_t dataPos  = 0;
        uint32_t expectOut = 0;        // bytes the target expects in DATA OUT
        uint8_t  status   = 0;
        uint8_t  msgIn    = 0x00;      // COMMAND COMPLETE
        bool     msgInDone = false;
        // Batch G S-4 (2026-08-02, JRN-AUD-003): a negotiation message the
        // target does not support is answered with MESSAGE REJECT (0x07) in
        // MSG IN before COMMAND -- SCSI-2 6.6.21 makes silence illegal.
        bool     rejectPending = false;
    };

    // Single owner for the live SCSI phase: keeps m_conn.phase and the
    // guest-visible SSTAT1<2:0> latch in lockstep so they cannot drift.
    // See kSstat1PhaseMask above for the source and the rationale.
    inline void setPhase(uint8_t ph) noexcept
    {
        m_conn.phase   = ph;
        m_reg[kSSTAT1] = static_cast<uint8_t>(
            (m_reg[kSSTAT1] & ~kSstat1PhaseMask) | (ph & kSstat1PhaseMask));
    }

    void moveData(uint8_t phase, uint32_t addr, uint32_t count) noexcept
    {
        switch (phase) {
        case kPhMsgOut: {
            std::vector<uint8_t> buf(count);
            m_dmaRead(addr, buf.data(), count);
            // IDENTIFY (0x80|dis|lun) is byte 0; LUN consumed (D2:
            // disconnect priv ignored).
            if (count > 0 && (buf[0] & 0x80)) m_conn.lun = buf[0] & 0x07;
            m_reg[kSFBR] = buf[0];
            // Batch G S-4 (2026-08-02, JRN-AUD-003): scan the message bytes
            // for an extended negotiation message (0x01 <len> <code>; code
            // 0x01 = SDTR, 0x03 = WDTR).  These targets are async/narrow
            // (INQUIRY byte 7 = 0), so the SCSI-2 6.6.21 answer is MESSAGE
            // REJECT in MSG IN before COMMAND -- the previous silent
            // swallow was an illegal target response.  The pke driver
            // handles the reject explicitly (pke_driver.c msg-reject arm).
            {
                bool sawNegotiation = false;
                uint32_t i = (count > 0 && (buf[0] & 0x80)) ? 1u : 0u;
                while (i + 2 < count) {
                    if (buf[i] == 0x01
                        && (buf[i + 2] == 0x01 || buf[i + 2] == 0x03)) {
                        sawNegotiation = true;
                        break;
                    }
                    ++i;
                }
                if (sawNegotiation) {
                    m_conn.rejectPending = true;
                    setPhase(kPhMsgIn);        // target: MESSAGE REJECT next
                    return;
                }
            }
            setPhase(kPhCmd);
            return;
        }
        case kPhCmd: {
            uint32_t const n = count <= sizeof(m_conn.cdb)
                             ? count : static_cast<uint32_t>(sizeof(m_conn.cdb));
            m_dmaRead(addr, m_conn.cdb, n);
            m_conn.cdbLen = static_cast<uint8_t>(n);
            executeCommand();
            return;
        }
        case kPhDatIn: {
            uint32_t const have = static_cast<uint32_t>(m_conn.data.size()) - m_conn.dataPos;
            if (count > have) {
                // CHANGE 2026-08-01 (JRN-SCSI-034): a SHORT DATA IN is a PHASE
                // MISMATCH, not zero padding.  The superseded "D1" path
                // fabricated (count - have) filler bytes and completed the MOVE,
                // so the target's DATA IN -> STATUS phase change was never
                // modelled and the phase-mismatch interrupt never fired.
                // OpenVMS SYS$PKEDRIVER waits on exactly that interrupt to
                // finish its INQUIRY (36 bytes answered into a 255-byte MOVE):
                // without it the port never leaves init (UCB$L_STS = 0), DKA0
                // never sets UCB$V_VALID, and SWP$MAIN_LOOP_C spins forever.
                // Measured 2026-08-01 from an OPERCRASH dump; see the journal.
                //
                // CONTRACT (53C895 Data Manual Ch.6, Block Move): "If the SCSI
                // phase bits do not match the value stored in the SSTAT1
                // register, the chip generates a phase mismatch interrupt and
                // the instruction is not executed."  SIST0<7> = MA.  The
                // residual is read from DBC -- the manual states DSP is NOT
                // valid for this use during a phase mismatch.
                //
                // TODO(N810-DFIFO): SSTAT1<2:0> IS maintained (setPhase keeps
                // it in lockstep at every transition -- validated on the
                // interrupt-ack path, JRN-SES-001 R-1).  SSTAT0 and DFIFO
                // still read 0, so pke's DATA OUT mismatch fixup
                // (dfifo - byte_count) would compute garbage if a data-out
                // short ever occurs (JRN-AUD-003 S-8).
                // NARROWED 2026-08-05 from TODO(N810-SBCL): the SBCL clause
                // is struck.  SBCL is not an isolated unlatched half -- it
                // is one member of the seven-register bit-level bus surface
                // (SOCL/SSID/SBCL/SSTAT2/SIDL/SODL/SBDL) that the census
                // now measures, and its fix scope will be set by that
                // measurement rather than by the JRN-SCSI-038 mask decode.
                if (have > 0) {
                    m_dmaWrite(addr, m_conn.data.data() + m_conn.dataPos, have);
                    m_reg[kSFBR] = m_conn.data[m_conn.dataPos];
                    m_conn.dataPos += have;
                }
                uint32_t const residual = count - have;   // bytes NOT transferred
                m_reg[kDBC + 0] = static_cast<uint8_t>( residual        & 0xFF);
                m_reg[kDBC + 1] = static_cast<uint8_t>((residual >>  8) & 0xFF);
                m_reg[kDBC + 2] = static_cast<uint8_t>((residual >> 16) & 0xFF);
                setPhase(kPhSts);                 // target -> STATUS
                m_sist0Pend    |= kSist0Ma;               // MA: phase mismatch
                updateIrq();                              // gated on SIEN0<7>
                // W-3 REVISED (BRIEF-SCSI-040 + JRN-SCSI-041, architect-
                // approved 2026-08-03): a phase mismatch HALTS the SCRIPTS
                // processor (895 DM Ch.6: the chip generates the interrupt;
                // the instruction does not complete).  The run-140935 defect
                // was that execution CONTINUED here: the script moved
                // STATUS/MSG IN, Memory Moves zeroed DBC (execMemoryMove
                // count->0), command-complete fired, and the driver's ISR
                // read residual=0 -> transferred = full move count ->
                // SS$_TOOMUCHDATA on 45,340 of 45,341 operations (dump
                // UCB$L_DK_VMS_STATUS=0x29C, UCB$L_DK_UNEXPLAINED=0xB11C).
                // DBC now holds the residual AT the halt; the driver's MA
                // fixup reads it and resumes via DSP write or DCNTL<STD>
                // (both modeled).  [CONFIRM] DSP value at halt vs the DM
                // (currently: past the mismatched MOVE).
                m_running = false;
                // PROBE 2026-08-01 (JRN-SCSI-034) -- OBSERVATION ONLY, bounded.
                // Answers, in one boot: did we reach this path, what residual,
                // did the driver ARM MA in SIEN0 (updateIrq masks against it),
                // and did the IRQ line actually assert.  armed=1 + irq=1 while
                // the guest still spins on UCB$V_VALID means the interrupt is
                // being dropped downstream -- i.e. INTx/DRIR routing, not the
                // device model.  TODO(N810-MA-PROBE): remove once resolved.
                {
                    // Cap raised 16 -> 400 and cyc= added 2026-08-01: at 16 the
                    // probe was exhausted entirely by SRM console-era target
                    // enumeration (the manifest declares 7 SCSI ids, so ~7
                    // INQUIRYs per pass are NORMAL, not retries) and never
                    // reported the OpenVMS era, which is the only one that
                    // matters.  The model has no cycle source, so correlate a
                    // row to its boot era by the nearest preceding PCSAMPLE
                    // cyc= in the log -- the stream is chronological.
                    static unsigned s_nMa = 0;
                    if (s_nMa < 400) { ++s_nMa;
                        std::fprintf(stderr,
                            "N810-PHASEMISMATCH #%u count=%u have=%u "
                            "residual=%u sien0=0x%02x armed=%d irq=%d\n",
                            s_nMa, count, have, residual,
                            static_cast<unsigned>(m_reg[kSIEN0]),
                            (m_reg[kSIEN0] & kSist0Ma) ? 1 : 0,
                            m_irq ? 1 : 0);
                        std::fflush(stderr);
                    }
                }
                return;                                   // MOVE not executed
            }
            // JRN-SCSI-027 tiling probe: the last unverified I/O hop is
            // HBA buffer -> guest RAM.  Target-side payloads are proven
            // byte-exact (scsi_read_diff, 68/68), so if the image still does
            // not parse the suspect is PLACEMENT: do the MOVEs for one command
            // tile the guest buffer exactly once (no gap, overlap, repeat)?
            // (Comment corrected 2026-08-02, Batch F doc sweep: m_dmaWrite is
            // NOT a raw-PA poke anymore -- the chipset wiring routes it through
            // TsunamiChipset::dmaWriteBytes -> TsunamiPchip::translateDma, so
            // bus-master window translation IS live; JRN-AUD-003 S-16.)
#if defined(EMULATR_BRINGUP_PROBES)
            // Two-tier per house rule (the ITBPROBE/DTBPROBE shape): compile
            // guard outside so release builds carry zero cost AND zero strings;
            // runtime env key inside so a diag build can flip probe configs
            // without a rebuild.  Keyed to EMULATR_SCSI_MOVE_PROBE (falls back
            // to the file's EMULATR_SCSI_TRACE so one env var lights both).
            if (movesProbeOn()) {
                std::fprintf(stderr,
                    "N810-MOVE in  pa=0x%016llx count=%u dataPos=%u size=%zu\n",
                    static_cast<unsigned long long>(addr), count,
                    m_conn.dataPos, m_conn.data.size());
            }
#endif
            m_dmaWrite(addr, m_conn.data.data() + m_conn.dataPos, count);
            m_reg[kSFBR] = m_conn.data[m_conn.dataPos];
            m_conn.dataPos += count;
            if (m_conn.dataPos >= m_conn.data.size())
                setPhase(kPhSts);
            return;
        }
        case kPhDatOut: {
            size_t const base = m_conn.data.size();
            m_conn.data.resize(base + count);
            m_dmaRead(addr, m_conn.data.data() + base, count);
            if (m_conn.data.size() >= m_conn.expectOut) {
                executeWriteCommand();
                setPhase(kPhSts);
            }
            return;
        }
        case kPhSts: {
            m_dmaWrite(addr, &m_conn.status, 1);
            m_reg[kSFBR] = m_conn.status;
            setPhase(kPhMsgIn);
            return;
        }
        case kPhMsgIn: {
            // Batch G S-4: a pending MESSAGE REJECT (0x07) is delivered
            // FIRST, then the nexus proceeds to COMMAND -- the normal
            // COMMAND COMPLETE leg after STATUS is unchanged.
            if (m_conn.rejectPending) {
                uint8_t const rej = 0x07;      // MESSAGE REJECT (SCSI-2 6.6.21)
                m_dmaWrite(addr, &rej, 1);
                m_reg[kSFBR] = rej;
                m_conn.rejectPending = false;
                setPhase(kPhCmd);
                return;
            }
            m_dmaWrite(addr, &m_conn.msgIn, 1);
            m_reg[kSFBR] = m_conn.msgIn;
            m_conn.msgInDone = true;
            return;
        }
        default:
            raiseDma(kDstatIid, 0);
            return;
        }
    }

    static bool isWriteOpcode(uint8_t op) noexcept
    {
        return op == 0x0A || op == 0x2A || op == 0x15 /*MODE SELECT6*/
            || op == 0x55 /*MODE SELECT10*/ || op == 0x3F /*WRITE LONG*/;
    }
    static uint32_t writeLengthFromCdb(uint8_t const* cdb) noexcept
    {
        switch (cdb[0]) {
        case 0x0A: return (cdb[4] ? cdb[4] : 256u) * 512u;
        case 0x2A: return ((uint32_t(cdb[7]) << 8) | cdb[8]) * 512u;
        case 0x15: return cdb[4];
        case 0x55: return (uint32_t(cdb[7]) << 8) | cdb[8];
        default:   return 0;
        }
    }

    void executeCommand() noexcept
    {
        scsi::VirtualScsiDevice* t = m_targets[m_conn.targetId];
        if (t == nullptr) { raiseDma(kDstatIid, 0); return; }
        if (isWriteOpcode(m_conn.cdb[0])) {
            m_conn.expectOut = writeLengthFromCdb(m_conn.cdb);
            m_conn.data.clear();
            m_conn.dataPos = 0;
            if (m_conn.expectOut > 0) { setPhase(kPhDatOut); return; }
        }
        // Read-class / no-data command: execute now into the data buffer.
        m_conn.data.assign(kMaxDataIn, 0);
        scsi::ScsiCommand cmd;
        cmd.cdb              = m_conn.cdb;
        cmd.cdbLength        = m_conn.cdbLen;
        cmd.lun              = m_conn.lun;
        cmd.dataDirection    = scsi::ScsiDataDirection::DeviceToHost;
        cmd.dataBuffer       = m_conn.data.data();
        cmd.dataBufferLength = kMaxDataIn;
        t->handleCommand(cmd);
        m_conn.data.resize(cmd.dataTransferred);
        m_conn.dataPos = 0;
        m_conn.status  = static_cast<uint8_t>(cmd.status);
        ledgerCmd(cmd);                           // W-1 (BRIEF-SCSI-040)
        setPhase(cmd.dataTransferred > 0 ? kPhDatIn : kPhSts);
        cmdTrace(cmd);
    }

    // W-1 (BRIEF-SCSI-040, architect-approved 2026-08-03): one bounded row
    // per SCSI command -- opcode, allocation length DECODED FROM THE CDB,
    // bytes returned, SCSI status.  Cadence first-64 + every-256th per the
    // D-LEDGER doctrine (never first-N only -- the E-5 dark-probe species).
    // DEVIATION from the brief's EMULATR_DIAG_SCSI_ALLOC compile guard,
    // recorded in JRN-SCSI-041: an unconditional bounded row cannot go dark
    // in the decisive run; volume ~1 row per 256 commands.
    // Era via nearest PCSAMPLE cyc, per the standing convention.
    // REMOVAL TRIGGER: rides TODO(N810-LEDGER).
    void ledgerCmd(scsi::ScsiCommand const& cmd) noexcept
    {
        uint64_t const n = m_cmdRows++;
        // Batch H-7 rider (2026-08-06, JRN-SCSI-042 Sec 12 DO-REGARDLESS,
        // value since proven): every CHECK CONDITION prints UNTHROTTLED,
        // with the CDB head and full sense triple.  The page-01h MODE SENSE
        // rejection that held every boot at the banner hid for four days
        // between the every-256th samples of THIS function -- one early
        // 0x02 status, never sampled.  CHECK CONDITIONs are rare by nature;
        // unbounded is safe and "rare event under a sampled ledger" is the
        // named species this row retires (JRN-SCSI-041 Sec 9.2 kin).
        if (cmd.status == scsi::ScsiStatus::CheckCondition) {
            std::fprintf(stderr, "N810-CHKCOND[%llu] op=0x%02X cdb2=0x%02X "
                         "key=0x%02X asc=0x%02X ascq=0x%02X ret=%u\n",
                         static_cast<unsigned long long>(n),
                         unsigned(cmd.opcode()),
                         unsigned(cmd.cdbLength > 2 ? cmd.cdb[2] : 0u),
                         unsigned(cmd.senseValid ? (cmd.senseData.data[2] & 0x0F) : 0u),
                         unsigned(cmd.senseValid ? cmd.senseData.data[12] : 0u),
                         unsigned(cmd.senseValid ? cmd.senseData.data[13] : 0u),
                         cmd.dataTransferred);
            std::fflush(stderr);
        }
        if (n >= 64 && (n & 0xFFu) != 0) return;
        uint8_t const op = cmd.opcode();
        long alloc = -1;                          // -1 = no alloc field
        switch (op) {
        case 0x03: /*REQUEST SENSE*/ case 0x12: /*INQUIRY*/
        case 0x1A: /*MODE SENSE 6*/  alloc = cmd.cdb[4]; break;
        case 0x5A: /*MODE SENSE 10*/
            alloc = (static_cast<long>(cmd.cdb[7]) << 8) | cmd.cdb[8]; break;
        default: break;
        }
        char allocs[16];
        if (alloc >= 0) std::snprintf(allocs, sizeof allocs, "%ld", alloc);
        else            { allocs[0] = '-'; allocs[1] = '\0'; }
        std::fprintf(stderr, "N810-CMD[%llu] op=0x%02X alloc=%s ret=%u "
                     "sts=0x%02X\n",
                     static_cast<unsigned long long>(n), unsigned(op), allocs,
                     cmd.dataTransferred,
                     unsigned(static_cast<uint8_t>(cmd.status)));
        std::fflush(stderr);
    }

    void executeWriteCommand() noexcept
    {
        scsi::VirtualScsiDevice* t = m_targets[m_conn.targetId];
        if (t == nullptr) { raiseDma(kDstatIid, 0); return; }
        scsi::ScsiCommand cmd;
        cmd.cdb              = m_conn.cdb;
        cmd.cdbLength        = m_conn.cdbLen;
        cmd.lun              = m_conn.lun;
        cmd.dataDirection    = scsi::ScsiDataDirection::HostToDevice;
        cmd.dataBuffer       = m_conn.data.data();
        cmd.dataBufferLength = static_cast<uint32_t>(m_conn.data.size());
        t->handleCommand(cmd);
        m_conn.status = static_cast<uint8_t>(cmd.status);
        ledgerCmd(cmd);                           // W-1 (BRIEF-SCSI-040)
        cmdTrace(cmd);
    }

    // ========================================================================
    // Config space + BAR plumbing (tulip pattern)
    // ========================================================================
    void initConfig() noexcept
    {
        m_cfg.fill(0);
        storeCfgLE(0x00, 0x00011000u);  // vendor 0x1000 NCR / device 0x0001 53C810
        storeCfgLE(0x08, 0x01000002u);  // class 0x010000 (SCSI), rev 0x02
        m_cfg[0x10] = 0x01;             // BAR0 = I/O
        m_cfg[0x3D] = kInterruptPin;    // interrupt pin INTA# -- silicon
                                        // constant (Batch C1, 53C895 DM)
    }
    static bool cfgWritable(uint8_t reg) noexcept
    {
        return reg == 0x04 || reg == 0x05 || reg == 0x0C ||
               reg == 0x0D || reg == 0x0F || reg == 0x3C;
    }
    void storeCfgLE(uint8_t reg, uint32_t v, uint8_t width = 4) noexcept
    {
        for (uint8_t b = 0; b < width && (reg + b) < 256u; ++b)
            m_cfg[reg + b] = static_cast<uint8_t>(v >> (8u * b));
    }
    void programBar(bool isMem, uint64_t base) noexcept
    {
        uint64_t& cur = isMem ? m_memBase : m_ioBase;
        if (base == cur) return;
        if (cur != 0 && m_unregister) m_unregister(cur, kBarWindow, isMem, this);
        cur = base;
        if (base != 0 && m_register) m_register(base, kBarWindow, isMem, this);
    }

    // ---- traces (EMULATR_SCSI_TRACE) --------------------------------------
    static bool traceOn() noexcept
    {
        static bool const on = (std::getenv("EMULATR_SCSI_TRACE") != nullptr);
        return on;
    }
#if defined(EMULATR_BRINGUP_PROBES)
    // JRN-SCSI-027 DMA-tiling probe key (two-tier: this whole facility is
    // compiled out of release; inside a diag build the env var selects it).
    // HOUSEKEEPING (filed, not done here): this file's OLDER traces --
    // traceOn()/trace()/cmdTrace() -- are runtime-gated ONLY and predate the
    // compile-guard convention.  Bringing them into conformance is a separate
    // sweep on purpose: mixing it into a diagnostics landing would muddy both.
    static bool movesProbeOn() noexcept
    {
        static bool const on =
            (std::getenv("EMULATR_SCSI_MOVE_PROBE") != nullptr) || traceOn();
        return on;
    }
#endif
    static void trace(char const* msg) noexcept
    {
        if (!traceOn()) return;
        std::fprintf(stderr, "N810: %s\n", msg);
    }
    static void trace2(char const* msg, uint32_t v) noexcept
    {
        if (!traceOn()) return;
        std::fprintf(stderr, "N810: %s 0x%08X\n", msg, v);
    }
    // Batch H-6 (2026-08-06): SELECT decode rows.  The LEGACY-MISMATCH arm
    // is the detector for the N-17 latent bug -- a row means the pre-H-6
    // model selected the WRONG target for this command.
    void selTableRow(uint32_t tAddr, uint32_t t0, unsigned id,
                     unsigned inlineId) noexcept
    {
        if (id != inlineId) {
            uint64_t const m = m_selMismatchRows++;
            if (m < 32) {
                std::fprintf(stderr, "N810-SELTABLE LEGACY-MISMATCH[%llu] "
                             "table id=%u inline id=%u entry=0x%08X "
                             "t0=0x%08X dsp=0x%08X\n",
                             static_cast<unsigned long long>(m), id,
                             inlineId, tAddr, t0, reg32(kDSP));
                std::fflush(stderr);
            }
        }
        uint64_t const n = m_selTableRows++;
        if (n < 16 || (n & 0xFFu) == 0) {
            std::fprintf(stderr, "N810-SELTABLE[%llu] entry=0x%08X "
                         "t0=0x%08X id=%u dsp=0x%08X\n",
                         static_cast<unsigned long long>(n), tAddr, t0, id,
                         reg32(kDSP));
            std::fflush(stderr);
        }
    }

    void selNoAtnRow(uint32_t w0) noexcept
    {
        uint64_t const n = m_selNoAtnRows++;
        if (n < 8) {
            std::fprintf(stderr, "N810-SELNOATN[%llu] w0=0x%08X -> COMMAND "
                         "phase (first live use of the no-ATN arm -- "
                         "UNTESTED path, verify against 895 DM)\n",
                         static_cast<unsigned long long>(n), w0);
            std::fflush(stderr);
        }
    }

    // TODO(N810-ATN) (2026-08-06, Batch H-5 Edit A): bounded rows for the
    // ATN latch.  sbclChgRow is the one that decides anything -- it fires
    // ONLY when a SBCL read returns a value different from the previous
    // read.  Over the whole 2026-08-05 boot that is one row, at 0x00.
    void atnRow(bool set, uint8_t before, uint8_t after) noexcept
    {
        uint64_t const n = m_atnRows++;
        if (n < 16 || (n & 0xFFu) == 0) {
            std::fprintf(stderr, "N810-ATN[%llu] %s socl 0x%02X -> 0x%02X "
                         "dsp=0x%08X\n",
                         static_cast<unsigned long long>(n),
                         set ? "SET  " : "CLEAR",
                         unsigned(before), unsigned(after), reg32(kDSP));
            std::fflush(stderr);
        }
    }

    void selAtnRow(unsigned id, bool withAtn, uint8_t socl) noexcept
    {
        uint64_t const n = m_selAtnRows++;
        if (n < 16 || (n & 0xFFu) == 0) {
            std::fprintf(stderr, "N810-SELATN[%llu] id=%u withAtn=%u "
                         "socl=0x%02X dsp=0x%08X\n",
                         static_cast<unsigned long long>(n), id,
                         unsigned(withAtn ? 1 : 0), unsigned(socl),
                         reg32(kDSP));
            std::fflush(stderr);
        }
    }

    void setClrUnmodeledRow(bool set, uint32_t w0) noexcept
    {
        uint64_t const n = m_setClrRows++;
        if (n < 8) {
            std::fprintf(stderr, "N810-SETCLR-UNMODELED[%llu] %s w0=0x%08X "
                         "(ACK/carry flags NOT modeled) dsp=0x%08X\n",
                         static_cast<unsigned long long>(n),
                         set ? "SET" : "CLEAR", w0, reg32(kDSP));
            std::fflush(stderr);
        }
    }

    void sbclRow(uint8_t v) noexcept
    {
        if (!m_sbclSeen || v != m_sbclLast) {
            uint64_t const c = m_sbclChgRows++;
            if (c < 32) {
                std::fprintf(stderr, "N810-SBCLCHG[%llu] 0x%02X -> 0x%02X "
                             "atn=%u dsp=0x%08X parks=%llu\n",
                             static_cast<unsigned long long>(c),
                             unsigned(m_sbclSeen ? m_sbclLast : 0u),
                             unsigned(v),
                             unsigned((v & kSbclAtn) ? 1 : 0), reg32(kDSP),
                             static_cast<unsigned long long>(m_parkCount));
                std::fflush(stderr);
            }
            m_sbclSeen = true;
            m_sbclLast = v;
        }
        uint64_t const n = m_sbclRows++;
        if (n < 16 || (n & 0xFFFu) == 0) {
            std::fprintf(stderr, "N810-SBCL[%llu] v=0x%02X dsp=0x%08X\n",
                         static_cast<unsigned long long>(n), unsigned(v),
                         reg32(kDSP));
            std::fflush(stderr);
        }
    }

    void scriptsTrace(uint32_t dsp, uint32_t w0, uint32_t w1) noexcept
    {
        // TODO(N810-LAPTRACE) (2026-08-06): the counter moved off a
        // function-local static -- it was shared across instances and could
        // never be reset.  This arm's behaviour is otherwise unchanged.
        if (traceOn() && m_traceRows < 4000) {
            ++m_traceRows;                     // TODO(N810-LAPTRACE)
            std::fprintf(stderr, "N810-SCRIPT dsp=0x%08X w0=0x%08X w1=0x%08X "
                                 "phase=%u\n", dsp, w0, w1, m_conn.phase);
        }
        lapTrace(dsp, w0, w1);                 // TODO(N810-LAPTRACE)
    }

    // TODO(N810-LAPTRACE) (2026-08-06, JRN-SCSI-042 Sec 5): decoded class of
    // a SCRIPTS instruction, for the lap listing only.  MIRRORS the dispatch
    // in stepScripts and execIoOrRw -- if that dispatch changes, this changes
    // with it, or the listing starts lying.
    static char const* insnClass(uint32_t w0) noexcept
    {
        unsigned const t = (w0 >> 30) & 0x3;
        unsigned const o = (w0 >> 27) & 0x7;
        switch (t) {
        case 0: return "BMOV";
        case 1:
            if (o >= 5) return "RW";
            switch (o) {
            case 0:  return "SELECT";
            case 1:  return "WAITDISC";
            case 2:  return "WAITRESEL";
            case 3:  return "SET";
            default: return "CLEAR";
            }
        case 2:
            switch (o) {
            case 0:  return "JUMP";
            case 1:  return "CALL";
            case 2:  return "RETURN";
            case 3:  return "INT";
            default: return "TC?";
            }
        default: return ((w0 >> 29) & 1u) ? "LOADSTORE" : "MM";
        }
    }

    // TODO(N810-LAPTRACE): one row per instruction inside an armed park-to-
    // park window.  SFBR is sampled AT FETCH, BEFORE execution -- deliberate,
    // and it amends JRN-SCSI-042 Sec 5; see the 2026-08-06 header entry.  A
    // conditional transfer's row therefore carries the exact byte that
    // transfer tests.  Hard bound: kLapTraceLaps x kLapTraceInsns rows.
    void lapTrace(uint32_t dsp, uint32_t w0, uint32_t w1) noexcept
    {
#ifdef EMULATR_DIAG_N810
        if (!m_lapArmed)                return;
        if (m_lapLap >= kLapTraceLaps)  return;
        if (m_lapInsn >= kLapTraceInsns) {
            if (!m_lapCapped) {                // cap notice exactly once
                m_lapCapped = true;
                std::fprintf(stderr, "N810-LAP CAP lap=%u at %u insns -- "
                             "window TRUNCATED, not a completed lap\n",
                             m_lapLap, kLapTraceInsns);
                std::fflush(stderr);
            }
            return;
        }
        std::fprintf(stderr, "N810-LAP[%u,%u] dsp=0x%08X w0=%08X w1=%08X "
                     "cls=%-9s sfbr=0x%02X phase=%u\n",
                     m_lapLap, m_lapInsn, dsp, w0, w1, insnClass(w0),
                     unsigned(m_reg[kSFBR]), unsigned(m_conn.phase));
        std::fflush(stderr);
        ++m_lapInsn;
#else
        (void) dsp; (void) w0; (void) w1;
#endif
    }

    // TODO(N810-LAPTRACE): lap framing, called from the WAIT RESELECT park
    // (execIoOrRw case 2).  The FIRST park arms; each later park closes the
    // current window and opens the next.  The END row's instruction count is
    // by itself the answer to "does the lap vary".
    void lapArm() noexcept
    {
        ++m_parkCount;                         // TODO(N810-LAPTRACE)
#ifdef EMULATR_DIAG_N810
        if (!m_lapArmed) {
            m_lapArmed  = true;
            m_lapLap    = 0;
            m_lapInsn   = 0;
            m_lapCapped = false;
            std::fprintf(stderr, "N810-LAP ARM park=%llu dsp=0x%08X "
                         "alt=0x%08X laps=%u cap=%u\n",
                         static_cast<unsigned long long>(m_parkCount),
                         reg32(kDSP), m_parkedAlt, kLapTraceLaps,
                         kLapTraceInsns);
            std::fflush(stderr);
            return;
        }
        if (m_lapLap >= kLapTraceLaps) return;
        std::fprintf(stderr, "N810-LAP END lap=%u insns=%u capped=%u "
                     "re-park park=%llu dsp=0x%08X\n",
                     m_lapLap, m_lapInsn, unsigned(m_lapCapped ? 1 : 0),
                     static_cast<unsigned long long>(m_parkCount),
                     reg32(kDSP));
        std::fflush(stderr);
        ++m_lapLap;
        m_lapInsn   = 0;
        m_lapCapped = false;
        if (m_lapLap >= kLapTraceLaps) {
            std::fprintf(stderr, "N810-LAP DONE after %u laps, parks=%llu\n",
                         kLapTraceLaps,
                         static_cast<unsigned long long>(m_parkCount));
            std::fflush(stderr);
        }
#endif
    }

    // TODO(N810-LEDGER) (2026-08-03, architect-approved): bounded ledger of
    // CONTROL-register traffic -- the channel the JRN-SES-003 dump analysis
    // found dark.  Writes to ISTAT/SCNTL0/1/3/DCNTL/DIEN/DMODE/SIEN0/1/
    // SCID/SXFER and reads of ISTAT/DSTAT/SIST0/1 are the port driver's
    // init/handshake footprint; data-register traffic is deliberately NOT
    // logged (volume).  Throttle: first 96 rows loud, then every 256th --
    // the console era consumes a few dozen, leaving headroom so the VMS-era
    // rows (the question) stay visible.  Era via nearest PCSAMPLE cyc.
    // REMOVAL TRIGGER: the PKE port-init stall is root-caused.
    void ledgerCtl(char rw, uint8_t off, uint8_t v) noexcept
    {
        // I-3 rider (H-3.3): own-BAR MM traffic also lands here via
        // regWrite8/regRead8 -- rows carry "(script)" when the SCRIPTS
        // engine (not the host) is the author, so host posts and script
        // reads are distinguishable in the same ledger.
        uint64_t const n = m_ledgerRows++;
        if (n < 96 || (n & 0xFFu) == 0) {
            std::fprintf(stderr, "N810-CTL %c[%llu] reg=0x%02X val=0x%02X%s\n",
                         rw, static_cast<unsigned long long>(n),
                         unsigned(off), unsigned(v),
                         m_running ? " (script)" : "");
            std::fflush(stderr);
        }
    }

    // TODO(N810-CENSUS) (2026-08-05, architect-approved, JRN-SCSI-041
    // Sec 12): per-register access census.  Counters, not rows -- rationale
    // and cost in the 2026-08-05 header entry.  censusHit is the ONLY hot
    // path addition; it compiles out entirely in Release.
    // TODO(N810-CENSUS) naming table (2026-08-05, Sec 13.8 item 2).  The
    // census printed bare hex, so 0x4E read 254,028 times by the script
    // sat unidentified until the DM was opened by hand.  Every offset the
    // 20260805_132410 census recorded is named here; anything else reads
    // "?" so an UNEXPECTED offset is visibly unexpected rather than silent.
    // Offsets marked UNIDENT are MEASURED HOST TRAFFIC with no grounded
    // identity yet -- 0x56 (hostW 12) and 0x5A (hostW 3).  [CONFIRM] both
    // against the 53C895 DM register map before naming them.
    static char const* regName(uint8_t o) noexcept
    {
        switch (o) {
        case kSCNTL0: return "SCNTL0";   case kSCNTL1: return "SCNTL1";
        case kSCNTL2: return "SCNTL2";   case kSCNTL3: return "SCNTL3";
        case kSCID:   return "SCID";     case kSXFER:  return "SXFER";
        case kSDID:   return "SDID";     case kGPREG:  return "GPREG";
        case kSFBR:   return "SFBR";     case kSOCL:   return "SOCL";
        case kSSID:   return "SSID";     case kSBCL:   return "SBCL";
        case kDSTAT:  return "DSTAT";    case kSSTAT0: return "SSTAT0";
        case kSSTAT1: return "SSTAT1";   case kSSTAT2: return "SSTAT2";
        case kISTAT:  return "ISTAT";    case kCTEST0: return "CTEST0";
        case kCTEST1: return "CTEST1";   case kCTEST2: return "CTEST2";
        case kCTEST3: return "CTEST3";   case kDFIFO:  return "DFIFO";
        case kCTEST4: return "CTEST4";   case kCTEST5: return "CTEST5";
        case kCTEST6: return "CTEST6";   case kDCMD:   return "DCMD";
        case kDMODE:  return "DMODE";    case kDIEN:   return "DIEN";
        case kDWT:    return "DWT";      case kDCNTL:  return "DCNTL";
        case kADDER:  return "ADDER";    case kSIEN0:  return "SIEN0";
        case kSIEN1:  return "SIEN1";    case kSIST0:  return "SIST0";
        case kSIST1:  return "SIST1";    case kMACNTL: return "MACNTL";
        case kGPCNTL: return "GPCNTL";   case kSTIME0: return "STIME0";
        case kSTIME1: return "STIME1";   case kRESPID: return "RESPID";
        case kSTEST0: return "STEST0";   case kSTEST1: return "STEST1";
        case kSTEST2: return "STEST2";   case kSTEST3: return "STEST3";
        case kSIDL:   return "SIDL";     case kSODL:   return "SODL";
        case kSBDL:   return "SBDL";
        case 0x56:    return "UNIDENT56";
        case 0x5A:    return "UNIDENT5A";
        default: break;
        }
        if (o >= kDSA      && o <= kDSA      + 3) return "DSA";
        if (o >= kTEMP     && o <= kTEMP     + 3) return "TEMP";
        if (o >= kDBC      && o <= kDBC      + 2) return "DBC";
        if (o >= kDNAD     && o <= kDNAD     + 3) return "DNAD";
        if (o >= kDSP      && o <= kDSP      + 3) return "DSP";
        if (o >= kDSPS     && o <= kDSPS     + 3) return "DSPS";
        if (o >= kSCRATCHA && o <= kSCRATCHA + 3) return "SCRATCHA";
        if (o >= kSCRATCHB && o <= kSCRATCHB + 3) return "SCRATCHB";
        return "?";
    }

    inline void censusHit(uint8_t off, unsigned wr) noexcept
    {
#ifdef EMULATR_DIAG_N810
        unsigned const who = m_running ? 1u : 0u;   // 1 = SCRIPTS engine
        uint8_t  const o   = off & 0x7F;
        ++m_regCensus[o][wr][who];
        // W-8 FIX (2026-08-05, JRN-SCSI-041 Sec 12.6): dirty on FIRST
        // TOUCH of an offset only.  Counters always change; the WORKING
        // SET is the signal, and a set that stops growing IS the finding.
        // The previous unconditional dirty made the gate a no-op --
        // 4,318,558 census rows at ~46 MB/min in run 20260805_132410.
        if (!m_regSeen[o]) { m_regSeen[o] = true; m_censusDirty = true; }
        if (who == 1u && m_regFirstDsp[o] == 0) {
            m_regFirstDsp[o] = reg32(kDSP);
            // E3 (Sec 13.4): CENSUS-TARGETED SCRIPTDUMP.  The first-six
            // cap spent every slot on the console-era 0xBFF42380 script,
            // so neither 0xC0000DF8 (STEST2) nor 0xC00006A4 (SBCL) was
            // ever dumped and the consumer decode was NOT free.  Dumping
            // here makes the decode a byproduct of the census.
            scriptDump(m_regFirstDsp[o]);
        }
#else
        (void) off; (void) wr;
#endif
    }

    // Dumped at the two STALL states (poll-park and WAIT RESELECT park) and
    // only when the table CHANGED since the last dump: an identical table
    // repeated 8,000 times is noise, and a table that stops changing is the
    // finding.  Nonzero rows only.
    void censusDump(char const* why) noexcept
    {
#ifdef EMULATR_DIAG_N810
        if (!m_censusDirty) return;
        m_censusDirty = false;
        uint64_t const n = m_censusDumps++;
        std::fprintf(stderr,
            "N810-CENSUS[%llu] why=%s session=%u cols: hostR hostW scrR scrW\n"
            "N810-CENSUS   scrW is STRUCTURALLY ZERO -- SCRIPTS writes bypass\n"
            "N810-CENSUS   regWrite8 (execRw opc 5/7, seam C).  A zero in that\n"
            "N810-CENSUS   column means NOT MEASURED, not \"did not happen.\"\n"
            "N810-CENSUS   regs 0x00-0x5F are real.  A nonzero row at 0x60-0x7F\n"
            "N810-CENSUS   is an ALIAS artifact of the & 0x7F mask and is itself\n"
            "N810-CENSUS   a decode finding, not traffic.\n",
            static_cast<unsigned long long>(n), why, m_scriptSession);
        for (unsigned o = 0; o < 0x80; ++o) {
            uint32_t const hr = m_regCensus[o][0][0], hw = m_regCensus[o][1][0];
            uint32_t const sr = m_regCensus[o][0][1], sw = m_regCensus[o][1][1];
            if ((hr | hw | sr | sw) == 0) continue;
            std::fprintf(stderr,
                "N810-CENSUS   off=0x%02X %-9s hostR=%u hostW=%u scrR=%u"
                " scrW=%u firstDsp=0x%08X%s\n",
                o, regName(static_cast<uint8_t>(o)),
                hr, hw, sr, sw, m_regFirstDsp[o],
                (o >= 0x60) ? "  <-- ALIAS REGION, decode finding" : "");
        }
        std::fflush(stderr);
#else
        (void) why;
#endif
    }

    // TODO(N810-V1-PROBE): JRN-AUD-003 V-1 (2026-08-02, architect-approved;
    // REWORKED 2026-08-03 with Batch H-2, JRN-SCSI-037).  A modeled
    // construct must not print NEW-CONSTRUCT, so the H-2 set is REMOVED
    // from the probe: relative TC (H-2b), MOVE SFBR->reg (H-2c), WAIT
    // RESELECT (H-2d), Memory Move (H-2a, ledgered by ledgerMm instead).
    // REMAINING unfaithful constructs watched here: table-indirect
    // SELECT/IO (bit 0, S-2a), indirect Block Move (bit 1), INTFLY
    // (bit 5), and Load/Store re-badged onto bit 6 (type-7 words -- absent
    // on plain 810 silicon per DEC's n810_def.h/n810_script_macros.mar,
    // which define k_mm=3 and no LS symbols; a type-7 sighting is an
    // IDENTITY finding, JRN-SCSI-037 P-3, never an implement-me).
    // REMOVAL TRIGGER: V-1 read out and verdicts recorded in a journal.
    void v1Probe(uint32_t dsp, uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t hit = 0xFF;   // 0xFF = nothing new
        switch (w0 >> 30) {
        case 0:                                       // block move
            if ((w0 >> 29) & 1u) hit = 1;             // indirect move
            break;
        case 1: {                                     // io / rw class
            uint8_t const opc = (w0 >> 27) & 0x7u;
            if (opc < 5 && ((w0 >> 25) & 1u)) hit = 0; // table-indirect io
            break;
        }
        case 2:                                       // transfer control
            if (((w0 >> 27) & 0x7u) == 3
                     && ((w0 >> 20) & 1u))          hit = 5;  // INTFLY
            break;
        default:                                      // type 3
            if ((w0 >> 29) & 1u) hit = 6;             // Load/Store (810A+)
            break;
        }
        if (hit != 0xFF && !(m_v1Seen & (1u << hit))) {
            m_v1Seen = static_cast<uint8_t>(m_v1Seen | (1u << hit));
            static char const* kNames[7] = {
                "TABLE-INDIRECT-SELECT/IO", "INDIRECT-MOVE",
                "(h2b-modeled)", "(h2c-modeled)",
                "(h2d-modeled)", "INTFLY", "LOAD-STORE-810A" };
            std::fprintf(stderr,
                "N810-V1 NEW-CONSTRUCT %s session=%u dsp=0x%08X "
                "w0=0x%08X w1=0x%08X seen=0x%02X\n",
                kNames[hit], m_scriptSession, dsp, w0, w1, m_v1Seen);
            std::fflush(stderr);
        }
        if ((m_scriptSession & 0xFFFu) == 0 && m_v1HeartbeatAt != m_scriptSession) {
            m_v1HeartbeatAt = m_scriptSession;
            std::fprintf(stderr, "N810-V1 heartbeat session=%u seen=0x%02X\n",
                         m_scriptSession, m_v1Seen);
            std::fflush(stderr);
        }
    }
    // JRN-SCSI-027: the trace now carries the LBA/length the CDB asked for and
    // a checksum of the payload the target actually returned, so a run log can
    // be byte-diffed against the backing file host-side without a debugger.
    // The %LOADER-E-BADIMGOFF wall says the bytes ARRIVE and do not parse, so
    // the question is not "did the command succeed" (status/xfer already said
    // yes) but "are these the RIGHT bytes for this LBA".
    //   fnv = FNV-1a over the returned payload; first/last = the payload's
    //   first and last 8 bytes, which localize a shift without dumping MB.
    void cmdTrace(scsi::ScsiCommand const& cmd) noexcept
    {
        if (!traceOn()) return;
        uint8_t const op = cmd.opcode();
        // Decode LBA/count for the read/write family; -1 for everything else.
        long long lba = -1; long long cnt = -1;
        if (op == 0x28 || op == 0x2A) {            // READ(10) / WRITE(10)
            lba = (long long)(((uint32_t)m_conn.cdb[2] << 24)
                            | ((uint32_t)m_conn.cdb[3] << 16)
                            | ((uint32_t)m_conn.cdb[4] << 8)
                            |  (uint32_t)m_conn.cdb[5]);
            cnt = (long long)(((uint32_t)m_conn.cdb[7] << 8) | m_conn.cdb[8]);
        } else if (op == 0x08 || op == 0x0A) {     // READ(6) / WRITE(6)
            lba = (long long)((((uint32_t)m_conn.cdb[1] & 0x1F) << 16)
                            | ((uint32_t)m_conn.cdb[2] << 8)
                            |  (uint32_t)m_conn.cdb[3]);
            cnt = m_conn.cdb[4] ? m_conn.cdb[4] : 256;
        }
        uint64_t fnv = 1469598103934665603ull;     // FNV-1a 64 offset basis
        // Data-OUT commands (MODE SELECT, WRITE) never set dataTransferred --
        // that field is the target's data-IN output -- so fall back to the
        // buffer the initiator actually delivered.  Without this the MODE
        // SELECT parameter list logged as "-" and its block descriptor could
        // not be read back from the run log.
        uint32_t const n = cmd.dataTransferred
                         ? cmd.dataTransferred
                         : static_cast<uint32_t>(m_conn.data.size());
        for (uint32_t i = 0; i < n && i < m_conn.data.size(); ++i) {
            fnv ^= m_conn.data[i];
            fnv *= 1099511628211ull;
        }
        char head[24] = "-", tail[24] = "-";
        if (n >= 8 && m_conn.data.size() >= 8) {
            std::snprintf(head, sizeof(head), "%02X%02X%02X%02X%02X%02X%02X%02X",
                m_conn.data[0], m_conn.data[1], m_conn.data[2], m_conn.data[3],
                m_conn.data[4], m_conn.data[5], m_conn.data[6], m_conn.data[7]);
            size_t const e = (n <= m_conn.data.size() ? n : m_conn.data.size()) - 8;
            std::snprintf(tail, sizeof(tail), "%02X%02X%02X%02X%02X%02X%02X%02X",
                m_conn.data[e+0], m_conn.data[e+1], m_conn.data[e+2],
                m_conn.data[e+3], m_conn.data[e+4], m_conn.data[e+5],
                m_conn.data[e+6], m_conn.data[e+7]);
        }
        // Full CDB bytes: documents what the driver actually ASKED for (e.g.
        // the MODE SELECT parameter-list length in byte 4) so implementations
        // are probe-driven rather than guessed.
        char cdbHex[3 * 16 + 1] = {};
        for (uint8_t i = 0; i < cmd.cdbLength && i < 16; ++i)
            std::snprintf(cdbHex + i * 3, 4, "%02X ", m_conn.cdb[i]);
        std::fprintf(stderr,
            "N810-CMD id=%u lun=%u op=0x%02X len=%u lba=%lld cnt=%lld -> "
            "status=%u xfer=%u fnv=0x%016llx head=%s tail=%s cdb=[%s]\n",
            m_conn.targetId, cmd.lun, op, cmd.cdbLength, lba, cnt,
            static_cast<unsigned>(cmd.status), cmd.dataTransferred,
            static_cast<unsigned long long>(fnv), head, tail, cdbHex);
    }

    static uint32_t le32(uint8_t const* p) noexcept
    {
        return  static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
    }
    // Batch H-2b: 24-bit signed offset for relative transfer control and
    // relative io alternates (verified shape: 0x00FFFABC -> -0x544).
    static uint32_t sext24(uint32_t v) noexcept
    {
        return (v & 0x00800000u) ? (v | 0xFF000000u) : (v & 0x00FFFFFFu);
    }

    static constexpr uint32_t kMaxDataIn = 1u << 20;   // 1 MiB per-command cap

    // ---- state ------------------------------------------------------------
    std::array<uint8_t, 256>  m_cfg{};
    std::array<uint8_t, 0x80> m_reg{};
    uint8_t  m_dstatPend = 0, m_sist0Pend = 0, m_sist1Pend = 0;
    // TODO(N810-GENTIMER) state (2026-08-05).  Unguarded: the timer is
    // FUNCTION, not diagnostics -- only its rows would be guardable, and
    // they are bounded, so they follow ledgerCmd's unconditional precedent
    // (:1557 DEVIATION) rather than the census's compile gate.
    uint32_t m_genTimer   = 0;          // 0 = disarmed
    uint64_t m_genRows    = 0;          // bounded FIRE rows
    uint64_t m_genArmRows = 0;          // bounded ARM rows
    bool     m_running = false;
    // TODO(N810-V1-PROBE) state (JRN-AUD-003 V-1): sessions counted at
    // every SCRIPTS start; one loud row per first-seen construct.
    uint32_t m_scriptSession  = 0;
    uint32_t m_v1HeartbeatAt  = 0xFFFFFFFFu;
    uint8_t  m_v1Seen         = 0;
    // TODO(N810-LEDGER) state: control-register traffic row counter.
    uint64_t m_ledgerRows     = 0;
    // TODO(N810-SCRIPTDUMP) state: distinct DSPs already dumped (max 6).
    uint32_t m_dumpedDsp[32]  = {};
    unsigned m_dumpCount      = 0;
    unsigned m_idReads        = 0;   // identity-register read rows (cap 16)
    // Batch H-2 state (JRN-SCSI-037):
    uint64_t m_mmRows         = 0;   // H-2a Memory Move ledger counter
    bool     m_sigp           = false;  // H-2d ISTAT<SIGP>, CTEST2 clears
    bool     m_parked         = false;  // H-2d WAIT RESELECT parked
    uint32_t m_parkedAlt      = 0;      // H-2d resume (alternate) address
    // TODO(N810-POLLPARK) state (H-3 _PROVISIONAL shim, JRN-SCSI-038):
    bool     m_pollParked     = false;  // budget exhausted mid-poll-loop
    uint64_t m_pollParks      = 0;      // park row counter (first 8 + 256th)
    uint64_t m_pollWakes      = 0;      // wake row counter
    uint64_t m_istatCritRows  = 0;      // I-5 (H-3.3): ABRT/RST write rows
    uint64_t m_sigpRows       = 0;      // N-12: bounded ISTAT<SIGP> rows
    uint64_t m_intRows        = 0;      // I-6: DSPS-at-INT rows (raiseDma)
    uint64_t m_dstatEats      = 0;      // I-7: script DSTAT read w/ pend
    uint64_t m_cmdRows        = 0;      // W-1: per-command ledger rows
    // Batch H-6 state (2026-08-06):
    uint64_t m_selTableRows   = 0;      // table-indirect SELECT rows
    uint64_t m_selMismatchRows= 0;      // LEGACY-MISMATCH rows (cap 32)
    uint64_t m_selNoAtnRows   = 0;      // no-ATN SELECT rows (cap 8)
    // TODO(N810-ATN) state (2026-08-06, Batch H-5 Edit A):
    uint64_t m_atnRows        = 0;      // SET/CLEAR ATN latch rows
    uint64_t m_selAtnRows     = 0;      // SELECT WITH ATN rows
    uint64_t m_setClrRows     = 0;      // unmodeled ACK/carry flag rows
    uint64_t m_sbclRows       = 0;      // raw SBCL reads (first 16 + 4096th)
    uint64_t m_sbclChgRows    = 0;      // SBCL value CHANGES (cap 32)
    uint8_t  m_sbclLast       = 0;      // previous SBCL value returned
    bool     m_sbclSeen       = false;  // m_sbclLast is valid
    // TODO(N810-LAPTRACE) state (2026-08-06, JRN-SCSI-042 Sec 5):
    uint64_t m_traceRows      = 0;      // was a static local in scriptsTrace
    uint64_t m_parkCount      = 0;      // WAIT RESELECT parks this boot
    // TODO(N810-CENSUS) state (2026-08-05, JRN-SCSI-041 Sec 12).  Guarded:
    // see the 2026-08-05 header entry for why this one is compile-gated
    // where ledgerCmd (:1557) deliberately is not.
#ifdef EMULATR_DIAG_N810
    uint32_t m_regCensus[0x80][2][2] = {};  // [off][0=rd,1=wr][0=host,1=script]
    uint32_t m_regFirstDsp[0x80]     = {};  // DSP at first SCRIPT-side touch
    bool     m_regSeen[0x80]         = {};  // W-8: working-set membership
    bool     m_censusDirty           = false;
    uint64_t m_censusDumps           = 0;
    // TODO(N810-LAPTRACE): armed at the FIRST park and NOT cleared by
    // reset() on purpose -- a chip reset mid-boot would re-arm and spend the
    // whole budget on the console era (the W-8 lesson, JRN-SCSI-041 Sec 12).
    bool     m_lapArmed              = false;
    bool     m_lapCapped             = false;  // per-window cap notice sent
    unsigned m_lapLap                = 0;      // current park-to-park window
    unsigned m_lapInsn               = 0;      // rows emitted in this window
#endif
    bool     m_irq = false;
    Conn     m_conn{};
    std::array<scsi::VirtualScsiDevice*, 8> m_targets{};

    uint64_t m_ioBase = 0, m_memBase = 0;
    RangeFn  m_register, m_unregister;
    DmaRdFn  m_dmaRead;
    DmaWrFn  m_dmaWrite;
    IntrFn   m_intr;
};

} // namespace deviceLib

#endif // DEVICELIB_TSUNAMI_NCR53C810_H
