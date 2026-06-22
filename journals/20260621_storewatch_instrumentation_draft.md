================================================================================
EmulatR V4 -- INSTRUMENTATION DRAFT for the PAL$CPU0_START_BASE base-pinning run
================================================================================
Written: 2026-06-21.  Companion to 20260621_storewatch_cpu0_start_base_base_pinning.md.
Partition: ONE apply-ready diff (STORE-WATCH) + THREE value-pending (each needs a
guest PC / impure PA from the tree).  The only guessed values are kept OUT of the
diffs as [LOCATE] markers.  All temp; REMOVE-BEFORE-COMMIT like the sibling watches.

--------------------------------------------------------------------------------
RESOLVE-BEFORE-RUN HEADER
--------------------------------------------------------------------------------
R1. powerup.c gate -- RESOLVED (no action).  start_secondaries() is called at
    powerup.c:438 under `if (!robust_mode)` only; robust_mode = 0 (powerup.c:179).
    NOT dualCPU()-gated.  So the call is reached on a normal cold boot.
R2. start_secondary entry PC -- [LOCATE: ghidra AlphaDS20_v7_3 symbol
    "start_secondary"].  NOT in ghidra/exported Functions/ (only fun_00097948).
    Needs the live project.  Feeds Diff 2 (reach-confirmation).  DO NOT GUESS.
R3. run-window -- OK.  Prior cold run fw_ds20_20260621-130506 reached 184M + 1B/2B
    snapshots, past the ~190M banner, so a 268M cap clears start_secondaries.
R4. self-proof note -- a STORE-WATCH hit on the slot proves cpu_enabled bit 1 was
    honored (can't reach start_secondary(1) otherwise).  Diff 3 (ev_read) is
    therefore NO-FIRE-DIAGNOSIS ONLY; not needed on a successful first pass.

================================================================================
DIFF 1 -- APPLY-READY NOW (no lookup; range-watches the candidate)
================================================================================
FILE     pipelineLib/MemDrainer.h
FUNCTION applyStoreEffect (store-watch block, mirror EMULATR_GCT_WATCH idiom)
CHANGE   Env-gated (EMULATR_START_WATCH) range STORE-WATCH over the candidate
         PAL$CPU0_START_BASE slot array [0x100 .. 0x100+4*8) and the halt-switch
         candidate 0x220 (base B=0 = console image @PA0; see Ev6Pc264PalDefs.h).
         Logs cyc/pc/va/pa/value/size/palMode AND cpu.palBase so the C1 check
         (value == palBase|1) is immediate.  Hard-gated: (void)0 when env unset.

    // ---- TEMP START-WATCH 2026-06-21 -- REMOVE BEFORE COMMIT --------------
    // Base-pinning: catch start_secondary(id) staging the entry addr into
    // PAL$CPU0_START_BASE[id] (candidate PA 0x100 + id*8) and the halt-switch
    // candidate 0x220.  See journals/20260621_storewatch_cpu0_start_base_*.md.
    {
        static bool const s_startWatch =
            (std::getenv("EMULATR_START_WATCH") != nullptr);
        if (s_startWatch &&
            ((pa >= 0x0000000000000100ull && pa < 0x0000000000000120ull) ||
             (pa >= 0x0000000000000220ull && pa <= 0x0000000000000227ull))) {
            std::fprintf(stderr,
                "START-WATCH cyc=%llu pc=0x%016llx va=0x%016llx pa=0x%016llx "
                "sz=%u v=0x%016llx pal=%d palBase=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(r.memAddr),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(r.memData),
                cpu.inPalMode() ? 1 : 0,
                static_cast<unsigned long long>(cpu.palBase));
            std::fflush(stderr);
        }
    }
    // ---- END START-WATCH -------------------------------------------------

NOTE  C2 (the next TIG write is 0xC00028+id) is ALREADY observable -- the TIG
      CPU-START latch landed this session.  Correlate the START-WATCH store with
      the immediately-following kIpcr write via the existing EMULATR_TIG_TRACE,
      or read tig.pendingCpuStartMask() right after.  No new TIG diff needed.

================================================================================
DIFF 2 -- MECHANISM-READY, PC-PENDING (reach-confirmation)
================================================================================
FILE     pipelineLib/PipelineDriver.h  (or traceLib/BreakpointSink gate)
FUNCTION step (PC compare, mirror the MEMDIAG-IMISS 0xd954 idiom ~line 554)
CHANGE   Env-gated PC-watch that fires once when the retired PC == start_secondary
         entry, so a no-fire is attributable to "never reached" vs "wrong PA".
         The PC is the ONE value Cowork pins from the symbol table; do not guess.

    // ---- TEMP REACH-CONFIRM 2026-06-21 -- REMOVE BEFORE COMMIT -----------
    static constexpr uint64_t kStartSecondaryPc =
        0x0 /* [LOCATE: ghidra AlphaDS20_v7_3 "start_secondary" entry PA] */;
    {
        static bool const s_reach =
            (std::getenv("EMULATR_START_WATCH") != nullptr);
        if (s_reach && kStartSecondaryPc != 0 &&
            cpu.pcAddr() == kStartSecondaryPc) {
            std::fprintf(stderr, "REACH-CONFIRM start_secondary cyc=%llu\n",
                static_cast<unsigned long long>(cpu.cycleCount));
            std::fflush(stderr);
        }
    }
    // ---- END REACH-CONFIRM -----------------------------------------------

ALT (no ghidra dependency): treat the FIRST START-WATCH hit itself as reach proof
     and skip Diff 2 on pass 1; only add the PC-watch if pass 1 is a no-fire and
     you must split "wrong PA" from "never reached".

================================================================================
DIFF 3 -- NO-FIRE DIAGNOSIS ONLY (cpu_enabled read-confirmation)
================================================================================
FILE     pipelineLib/PipelineDriver.h
FUNCTION step (PC compare at the ev_read("cpu_enabled") return)
CHANGE   Capture the mask the console actually got back.  Self-proven on a fire
         (R4), so this is ONLY for diagnosing a no-fire.  Needs the guest return
         PC AND the result register convention.
         [LOCATE: ev_read("cpu_enabled") call-return PC in AlphaDS20_v7_3;
                  pc264.c:327 / hwrpb.c:688 are the reference call sites;
                  result mask is in the C return reg (v0/r0) at return.]
         Shape identical to Diff 2; log cpu.intReg[0] (v0) at the return PC.

================================================================================
DIFF 4 -- INFORMATIONAL (P1 oracle; P1 is DEMOTED -- not a gate)
================================================================================
FILE     pipelineLib/MemDrainer.h
FUNCTION applyLoadEffect (load-watch, mirror TICK-LOADWATCH idiom ~line 601)
CHANGE   Confirm dualCPU() reads proc_mask from CPU0's impure cns$srom_proc_mask
         slot (NOT CPU1's -- pc264.c:513-514 masks out whoami).  Load-watch the
         cns$srom_proc_mask PA, log value + faulting VA + cyc.  P1 does NOT gate
         the store (Diff 1), so this is informational for config-tree fidelity,
         not a precondition for the base-pinning hit.
         [LOCATE: cns$srom_proc_mask impure PA -- it is a cns$ console-impure
                  field (apu_/cns_ defs), NOT in EV6_PC264_PAL_DEFS.SDL; resolve
                  its offset, then PA = (confirmed linkage base) + offset.]

--------------------------------------------------------------------------------
HOT-PATH COST
--------------------------------------------------------------------------------
All four are behind `static bool const s_x = getenv(...)` so an unset env makes
the guard a predicted-not-taken branch over a cached bool -- effectively (void)0
in the store/load hot path.  Still: confirm the 268M run WITH EMULATR_START_WATCH=1
does not blow the wall-time budget vs the normal cold-boot baseline before
trusting timing observations from the instrumented run.

--------------------------------------------------------------------------------
APPLY ORDER
--------------------------------------------------------------------------------
1. Apply Diff 1 only.  Run cold, EMULATR_START_WATCH=1, --max-cycles 0x10000000.
2. FIRE -> capture PA/value/palBase; verify C1 (v==palBase|1) + C2 (TIG 0xC00029);
   reconcile PA vs Ev6Pc264PalDefs.h; update the header base note.  DONE.
3. NO-FIRE -> resolve R2 (start_secondary PC), apply Diff 2, re-run to split
   "never reached" from "wrong PA".  Then Diff 3/4 as needed.
