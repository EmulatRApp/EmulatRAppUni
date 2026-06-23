<!--
EmulatR V4 -- GCT/FRU cyclic-link hang: post-dva0 DIAGNOSTIC SPEC
Project: EmulatR (Alpha 21264 / EV6-EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic (claude.ai web).
Written 2026-06-22.  This is a DIAGNOSTIC spec, not a fix.  Deliverable is
EVIDENCE that pins the bad config-tree link and decides tree-construction-bug
vs emulator-memory-bug.  No fix is proposed until that verdict is in (discuss-
before-code).  Cowork holds live file access and runs the instrumentation;
Cowork is source of truth for current file state, env/guard names, and PAs.
ASCII(128) only.  Per ADR-0001 this HTML comment is the spec attribution block.
-->

================================================================================
COWORK SPEC -- diagnose the post-dva0 GCT/FRU walk hang (cyclic-link hypothesis)
================================================================================

WHAT THIS IS FOR
  The console now runs ~5.1B cycles PAST the dva0 floppy scan and hangs in the
  GCT/FRU config-tree walk.  Working hypothesis: a malformed cyclic link -- the
  walk re-enters a ring and never terminates.  This spec instruments to PROVE
  (or refute) that and to pin the exact node + link offset.  It does NOT change
  the firmware walk (authoritative SRM) and does NOT apply a fix.  The fix is a
  separate briefing written only after the verdict below resolves.

VERIFIED CONTEXT (this run)
  - Walls peeled in order: IIC -> PCI -> IDE -> GCT/FRU-init -> dva0 floppy ->
    (here) GCT/FRU tree-walk.  Forward motion is real; this is a known-area bug,
    not a new device wall.
  - Console printed "initializing GCT/FRU at 3ff32000" PRE-dva0 -- so the tree
    is already BUILT by the time this walk hangs.  The hang is in READING it.
  - Floppy (both completion edges + Path-B drain) is correct and NOT implicated.

--------------------------------------------------------------------------------
1.  STEP ORDER  (cheapest evidence first; each step gated on the prior)
--------------------------------------------------------------------------------

STEP 1 -- CLASSIFY before instrumenting (nearly free; do this first)
  Capture a BOUNDED PC tail of the foreground at the hang -- gated window, NEVER
  a whole-file grep (the 5.1B-cycle trace will wedge the sandbox).  Decide which
  bug this is:
    (a) CYCLIC: a small fixed set of PCs and GCT addresses repeating at constant
        cadence forever -> ring re-entry (the hypothesis).
    (b) STALL: linear advance through GCT then stop at one node -> unterminated
        walk / one bad terminator, a different bug with a different fix.
  Do not build cyclic-link machinery until the tail confirms (a).  If (b),
  report back -- the rest of this spec is aimed at (a).

STEP 2 -- LOAD-watch the GCT region (the load-bearing step)
  The tree is already built, so the diagnostic is on the LOAD side, not the
  store side.  A store-watch on the GCT region will be mostly silent here (the
  construction stores fired millions of cycles ago).  The walk dereferencing a
  node's next-link is a LOAD; that is what carries the back-edge.
    INSTRUMENT: extend the existing GCT watch to the LOAD path in
      MemDrainer::applyLoadEffect, mirroring the applyStoreEffect store-watch
      idiom.  [CONFIRM] whether to reuse EMULATR_GCT_WATCH or add a sibling
      EMULATR_GCT_LOADWATCH; keep it behind the CMake compile guard with the
      runtime env gate, ((void)0) in Release (same pattern as IIC/GCT watches).
    RANGE: the GCT region anchored at 0x3ff32000 (per the console line).
      Candidate span [0x3ff20000 .. 0x3ff40000) _PROVISIONAL -- [CONFIRM] the
      real extent from the config-tree size field rather than guessing the top.
    CAPTURE per hit: load VA, value loaded, loading PC, cycle.
    RING-DETECTION CRITERION: a load from a node-link offset returns a value
      that equals an address ALREADY SEEN as a prior load VA in this window.
      That back-pointer IS the bad link; the load VA pins the node + offset, the
      loading PC pins the walk site.  That single observation is the diagnosis.

STEP 3 -- TARGETED store-watch on the one bad link (decides the verdict)
  Once Step 2 names the bad link address, store-watch THAT EXACT PA (not the
  region) for the whole boot to find whether anything wrote the back-pointer:
    - Store of the back-pointer value FOUND -> the tree was BUILT with the
      cyclic link.  Tree-construction bug.  The storing PC + cycle localizes the
      GCT/FRU construction step that wrote it and what input it used.
    - NO such store (the stored value was forward/terminator, but Step 2's LOAD
      returned the back-pointer) -> load != store at that PA -> EMULATOR-MEMORY
      bug (aliasing / wrong translation / region overlap at 0x3ff3xxxx), NOT a
      tree bug.
  This is the decision gate in section 2.  Run it before proposing any fix.

STEP 4 -- SYMBOLICATE the walk site (only if Step 2/3 need function context)
  If the loading PC needs naming, resolve it via the DS10 linker MAP (linker-
  authoritative), not Ghidra's guessed boundaries -- same caution as the
  build_hwrpb hunt: confirm the symbol against the captured load-PC, do not
  trust an auto-name hung on a nearby address.  Skip this if Step 3 already
  pinned the construction store.

--------------------------------------------------------------------------------
2.  DECISION GATE  (state the verdict explicitly; do NOT fix before it)
--------------------------------------------------------------------------------
The combined Step 2 (load) + Step 3 (targeted store) evidence resolves to ONE:

  VERDICT A -- TREE-CONSTRUCTION BUG: the cyclic link is physically stored in
    the node; a construction store wrote the back-pointer.  Fix area is the
    GCT/FRU build path (the code that ran at "initializing GCT/FRU at 3ff32000").
    Next: a separate fix briefing targeting that storing PC.

  VERDICT B -- EMULATOR-MEMORY BUG: the stored link is correct but the walk's
    LOAD returns a different value.  Fix area is the V4 memory model (translation
    / aliasing / region overlap at the GCT PA), NOT the tree.  Next: a separate
    memory-model briefing.

We do NOT touch the firmware walk in either case -- it is authoritative SRM.
"Walk-side" here always means an EmulatR defect (built wrong, or read wrong),
never a firmware edit.

--------------------------------------------------------------------------------
3.  WARP DISCIPLINE  (snapshot is a tool, not a fix)
--------------------------------------------------------------------------------
Iterating at ~5.1B cycles by cold-booting each time is wasteful.  LEGITIMATE
accelerator: --snapshot-on-pc at the GCT-walk entry (the loading PC from Step 2)
so each instrumentation cycle restores near the hang -- identical in spirit to
the planned canonical A/B/C snapshot set.  Use it.

REJECTED: warp-as-fix -- patching the walk to skip the bad link, or hand-
injecting a corrected GCT so the firmware never trips.  That buries a real
EmulatR defect under a workaround and the corruption resurfaces when the OS
walks the same tree.  This is precisely the AXPBox-shortcut posture the fidelity
standard refuses.  The load/store watches exist so the fix targets the actual
defect (Verdict A or B), not the symptom.  Hold any warp-past instinct until the
verdict is in.

--------------------------------------------------------------------------------
4.  CONVENTIONS FOR THIS WORK  (per ADR-0001 + standing rules)
--------------------------------------------------------------------------------
  - Trace discipline: bounded tails / gated windows only; verify every file
    write via bash (wc -l / grep); heredoc for large writes.  No whole-file grep
    on the 5.1B-cycle trace.
  - Logging: the load-watch goes through LogSubsystem behind a CMake compile
    guard + runtime env gate, ((void)0) in Release.  The env var is a RUNTIME
    gate, distinct from the compile guard -- do not conflate them.
  - Discuss-before-code: this instrumentation edit (applyLoadEffect watch) is
    itself proposed here as the edit shape; land it, capture, report.  The FIX
    is a later, separate proposal once the verdict resolves.
  - Header/inline: the watch edit carries the FILE/FUNCTION/CHANGE header block
    and an inline comment at the changed line; remove-before-commit like the
    sibling temp watches (this is diagnostic scaffolding, not permanent code).
  - _PROVISIONAL: the bad-link node/offset identity is _PROVISIONAL until BOTH
    the load-watch (Step 2) and the targeted store-watch (Step 3) agree on it.
    Do not let a single-watch guess drive the fix-area decision.
  - ASCII(128) only in any artifact produced; surgical edits over rewrites.

--------------------------------------------------------------------------------
5.  DELIVERABLES OF THIS RUN
--------------------------------------------------------------------------------
  1. Step-1 classification: cyclic (a) vs stall (b), from a bounded PC tail.
  2. Step-2 capture: the bad-link node PA + link offset + loading PC + cycle,
     with the ring-detection criterion satisfied (load value == prior load VA).
  3. Step-3 verdict: A (construction store found -> storing PC) or B (no store
     -> load != store -> emulator-memory bug).
  4. Optional Step-4 symbol name for the walk site, map-confirmed.
  5. A snapshot-on-PC at the walk entry for cheap iteration on the follow-up fix.
  No fix in this run.  The fix briefing is written against the verdict.

End of spec.
