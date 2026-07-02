# Journal -- 2026-07-01 -- ES40 (Clipper) SRM initialization -- kickoff

Project:   EmulatR -- Alpha AXP / EV6 (21264) Emulator (V4)
Architect: Timothy Peer.  AI collaboration: Claude (Cowork) + claude.ai web.
ASCII(128) only.  STATUS: setup prepared; first trace capture NOT yet run.

================================================================================
FOCUS -- bring the AlphaServer ES40 up to the SRM ">>>" console
================================================================================
New workstream: drive ES40 firmware (firmware/es40_v7_3.exe, SRM Console V7.3)
through a genuine COLD boot far enough to reach the interactive ">>>" prompt.
This session is the kickoff: platform mapping confirmed, a repeatable trace-
capture launcher prepared, references gathered.  No emulator code changed.

================================================================================
PLATFORM MAPPING -- ES40 = CLIPPER, GCT__TSUNAMI, pc264 family
================================================================================
SRM build symbol for ES40 is CLIPPER, grouped with PC264 under the pc264
family (Compaq EV6 / Tsunami 6600: DP264, DS10/20, ES40).  System type
constant GCT__TSUNAMI; platform name string "Compaq AlphaServer ES40".  See
apisrm ref/galaxy_def_configs.h, the #if (CLIPPER || PC264) block.

CLIPPER GCT platform data: base_alloc 2 MB, base/min align 32 MB / 1 MB,
max_part 2, max_frag 16, max_desc 4; GCT builds NODE_CPU template nodes and
ES40 is 4-socket capable -- so advertise cpuCount=1 in the ini or SRM/GCT
spins waiting for absent secondaries (boot hang).  ini already set: model=ES40,
cpuCount=1, activeCpus=1.

SRM source tree: D:\EmulatR\Processor Support\Palcode\palcode\apisrm\apisrm
(baseline in ref/, platform-family sources in pc264/).  Hierarchy.txt confirms
pc264 = EV6x / Tsunami (6600) platforms.

================================================================================
CHIPSET -- Tsunami / Typhoon 21272
================================================================================
Reference HRM uploaded this session: tsunami_typhoon_21272_hrm.txt.
EmulatR chipset model code: D:\EmulatR\EmulatRAppUniV4\Emulatr\chipsetLib
(TsunamiChipset.*).  Cchip / Dchip / Pchip topology is the ES40 north/south
fabric; ROM lives in the Pchip flash window (see project_v4_chipset_routing).

================================================================================
RUN SETUP -- "I prep, you run" (Claude cannot execute the Windows binary)
================================================================================
Launcher created:
  D:\EmulatR\EmulatRAppUniV4\Emulatr\out\build\relwithdebinfo\run_es40_srm_trace.sh
Behavior:
  - copies firmware/es40_v7_3.exe into the build-dir firmware/;
  - sets [System] model=ES40 in config/EmulatrV4.ini (backup + restore on exit
    via trap); MEM=4 GiB (32 MB-aligned per CLIPPER GCT data);
  - genuine COLD boot: --no-autoload (ignore snapshots), purge stale
    auto_*.axpsnap;
  - trace channels: --trace arms TRACE_PAL_WINDOW (0x40) | TRACE_RETIRE_COMPACT
    (0x80).  This is the CLI --trace default, NOT TRACE_ALL -- decided this
    session to avoid an AppOptions change (no per-instruction regfile dumps).
  - runs to ">>>" only; does NOT issue `boot`.  Console on TCP 10023
    (PuTTY/plink raw mode).

Trace destinations:
  - dec + machine channels -> traces/<ts>_es40_dec.log , traces/<ts>_es40_machine.log
  - retire-compact channel  -> X:\traces\<ts>_srm.trc  (hard-coded in
    DecListingSink; NOT CLI-redirectable)
  - console + stderr (banner / PROFILE / StopReason / fault / exit cycle)
    -> traces/<ts>_es40_console.out (tee)

Optional time-box: pass --max-cycles <N> through the launcher; a cold boot is
unbounded and can grind a long time.

================================================================================
RISKS / OPEN QUESTIONS for the trace review
================================================================================
1. A cold boot may spin many cycles in the sys__cbox MCHK path and may not
   reach ">>>" without chipset init being satisfied -- watch the machine log
   for a stall / repeating PC window.  Cap with --max-cycles if needed.
2. Per-model platform manifest: confirm es40_v7_3_platform.json exists and
   loads (the 2026-06-23 fix keys the manifest to the firmware stem; a missing
   file silently falls back to the built-in DS10 bus -- wrong device set for
   ES40).  Verify no "using built-in default DS10 bus" line in the console log.
3. oem_string banner override (2026-06-23): if oem_string is non-empty the
   banner masks the platform name.  To SEE "Compaq AlphaServer ES40", ensure
   oem_string is clear before the cold boot.

================================================================================
NEXT SESSION
================================================================================
- Tim runs run_es40_srm_trace.sh; the trace logs are made available for review.
- Review dec/machine/console logs: where the boot reaches, any stall, whether
  the ES40 manifest loaded, and whether ">>>" was hit.
