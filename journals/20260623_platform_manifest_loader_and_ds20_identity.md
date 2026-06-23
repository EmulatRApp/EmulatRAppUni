# Journal -- 2026-06-23 -- Platform-manifest loader fix + DS20 identity (AlphaServer DS20)

Project:   EmulatR -- Alpha AXP / EV6 (21264) Emulator (V4)
Architect: Timothy Peer.  AI collaboration: Claude (Cowork) + claude.ai web.
ASCII(128) only.  STATUS: edits APPLIED, NOT yet rebuilt/verified (reboot pending).

================================================================================
HEADLINE -- the platform manifest was never loading for non-DS10 models
================================================================================
While chasing the "AlphaPC 264DP" banner (should be "AlphaServer DS20"), Tim spotted
in the console log:
  PlatformConfig: manifest '.../ds10_platform.win' unusable (cannot open ...);
  using built-in default DS10 bus
ROOT: Machine.cpp hardcoded the default manifest leaf as `ds10_platform.win` (_WIN32)
/ `ds10_platform.linux` -- THREE bugs: (1) hardcoded 'ds10', never the run's model;
(2) host-OS extension .win/.linux instead of .json; (3) resolved next to the exe where
no such file exists. So EVERY non-DS10 model (DS20/ES40/ES45/DS25) has been silently
running the COMPILED-IN DEFAULT DS10 bus -- the per-model manifests never loaded. DS20
"worked" only because the DS10 default is close enough (same Tsunami/Cypress, overlapping
IIC status/FRU devices), but DS20-specific devices (DE500 tulip, OCP) were absent. This
is foundational for the platform queue: DS20E/ES40/ES45 can't differentiate until their
manifests load.

================================================================================
FIX (#28) -- firmware-stem-derived, host-agnostic manifest (Option A), 4 files
================================================================================
Design chosen (with Tim): manifest name = FIRMWARE-IMAGE STEM + "_platform.json",
keyed 1:1 to the firmware that actually loaded (so model/firmware can't drift, and a
firmware variant can carry its own device set):
   firmware/ds20_v7_3.exe  ->  ds20_v7_3_platform.json
Host-agnostic (the device tree is the guest's, not the host's) -- .win/.linux dropped.

EDITS APPLIED:
1. main.cpp:212 -- write the RESOLVED firmware path (CLI > ini) back into
   settings.rom.firmwareImage before the Machine ctor. Needed because run_fw.sh passes
   --firmware on the CLI and does NOT set [ROM] firmwareImage, so the ctor (where the
   manifest loads) would otherwise see a stale/empty ini value.
2. systemLib/Machine.cpp (~451) -- replace the #ifdef ds10_platform.win/.linux block
   with: leaf = fs::path(m_settings.rom.firmwareImage).stem() + "_platform.json",
   resolved next to the exe (applicationDirPath). EMULATR_PLATFORM_CONFIG still overrides;
   empty firmwareImage -> empty path -> built-in default.
3. out/build/release/run_fw.sh -- after the firmware copy, copy
   Emulatr/<name>_v7_3_platform.json next to the exe (so a manifest edit takes effect
   without a rebuild).
4. CMakeLists.txt (~836) -- replaced the two if(WIN32) .win/.linux POST_BUILD blocks with
   ONE host-agnostic loop copying all five <model>_v7_3_platform.json (ds10/ds20/ds25/
   es40/es45, EXISTS-guarded) next to the exe.
Manifests RENAMED by Tim to <model>_v7_3_platform.json in Emulatr/ + build/release.
(CMake POST_BUILD + run_fw.sh copies are intentionally redundant: build-time vs per-run.)

================================================================================
DS20 IDENTITY -- "AlphaServer DS20" via the OCP IIC pair
================================================================================
pc264.c get_sysvar(): opens IIC devices to set SYSVAR member id ->
  fopen("iic_ocp0") OK -> member 6 -> "AlphaServer DS20"
  else fopen("iic_8574_ocp") -> member 8 -> "AlphaServer DS20E" (Goldrack)
  else (default)              -> member 1 -> "AlphaPC 264DP"
So the banner is "AlphaPC 264DP" purely because no iic_ocp0 was present. FIX: added to
ds20_platform.json (now ds20_v7_3_platform.json):
  iic_ocp0 @ 0x40 (control)  +  iic_ocp1 @ 0x42 (data)   -- IIC_LED_TYPE, class "status"
Both required: sable_ocp_init (sable_ocp_driver.c) opens the OCP as a control+data PAIR;
adding only ocp0 caused "Device Open Error: IIC_OCP1" + sable_ocp_init exit status 1
(non-fatal). Addresses from iic_driver.c iic node list. Do NOT add iic_8574_ocp (0x4E) ->
that selects DS20E.

================================================================================
oem_string BANNER OVERRIDE -- a real gotcha
================================================================================
kernel.c:2320: if oem_string is non-empty, the banner prints "<oem_string> Console ..."
INSTEAD of the platform name. The console-snapshot marker is `set oem_string snapshot`
(EMULATR_CONSOLE_SNAPSHOT=1, default EMULATR_SNAPSHOT_MARKER), which sets
oem_string="snapshot" as a side effect -> banner shows "snapshot Console V7.3-2", masking
the platform name entirely. To SEE the DS20 name: `clear oem_string` (or set it ""),
flush via EMULATR_STOP, cold boot. RECOMMENDATION: change the marker to a benign scratch
var, e.g. EMULATR_SNAPSHOT_MARKER="set user_def1 snapshot", so mints don't pollute the
banner. (Fold into snapshot-tooling #25.)

================================================================================
VERIFY-AFTER-REBOOT (the pending steps)
================================================================================
1. Reconfigure + rebuild (CMakeLists + main.cpp + Machine.cpp changed):
     cmd //c ".../tools/build_emulatr_diag.bat"
2. unset EMULATR_PLATFORM_CONFIG (so the new derivation is exercised, not the override).
3. EMULATR_FLASH_ROM=ds20_flash.rom ./run_fw.sh ds20 cold
   EXPECT: NO "using built-in default DS10 bus" (loads ds20_v7_3_platform.json);
           NO "Device Open Error: IIC_OCP1".
4. clear oem_string; EMULATR_STOP (flush); cold boot again
   EXPECT banner: "AlphaServer DS20 100 MHz Console V7.3-2"  (100 MHz = separate clock
   item, #26).
5. Re-mint the predig_ds20_p00prompt snapshot, this time with a benign marker
   (EMULATR_SNAPSHOT_MARKER="set user_def1 snapshot").

================================================================================
UNCOMMITTED BATCH (all this + prior session) -- commit after DS10 regression (#23)
================================================================================
- chipsetLib/TsunamiChipset.h  -- model-conditional PCF8584 IIC base (DS10 0xFFFF0000 /
  DS20 0xFFF80000), fail-loud find-or-fail.
- deviceLib/Tsunami/Floppy82077.h -- 3 interrupt-edge fixes.
- main.cpp + systemLib/Machine.cpp + CMakeLists.txt + run_fw.sh -- manifest-loader fix (#28).
- ds20_v7_3_platform.json -- iic_ocp0/ocp1 added; manifests renamed.
- PULL BEFORE COMMIT: EMULATR_IIC_WATCH (MemDrainer.h), SDE swap-ledger (PalEntries.cpp +
  Machine.cpp), START_WATCH/FCLOSE-WATCH, DecListingSink.h:263 m_emitEnabled->false.
OPEN TASKS: #23 DS10 regression, #25 snapshot model-tag + benign marker, #26 cosmetic
(banner clock 100MHz, SROM-rev string), #27 env-persist (memory_test), #21 SDE artifact-
vs-real.
