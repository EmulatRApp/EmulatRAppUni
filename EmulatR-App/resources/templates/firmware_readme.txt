================================================================
  PLACE YOUR SRM CONSOLE FIRMWARE IMAGE IN THIS FOLDER
================================================================

EmulatR does not ship firmware.  The SRM console image for your
Alpha model is DEC/HP intellectual property and you must supply
your own copy.

WHAT GOES HERE
--------------
One SRM firmware image matching this system's platform, for
example:

    ds10_v7_3.exe        AlphaServer DS10
    ds20_v7_3.exe        AlphaServer DS20
    es40_cl67.exe        AlphaServer ES40

A .exe extension is the vendor's convention for an SRM-format
image and is NOT a Windows program -- do not double-click it.
Images without that extension are treated as raw ROM dumps.

WHAT TO DO NEXT
---------------
1. Copy the image file into this folder.
2. Return to EmulatrLaunch.
3. Pick the image in the "Firmware" control on the
   Named System Details tab.
4. Press Start.

The launcher will warn you if the image you pick does not look
like it matches this system's platform.  Booting a DS20 manifest
against ES40 firmware is a known way to hang the console in
PALcode with no output on OPA0 -- if the launcher warns, believe
it.

WHERE THE PATH ENDS UP
----------------------
Your choice is written to Emulatr.ini as:

    [ROM]
    firmwareImage = firmware/<your file>

and is also passed to Emulatr.exe on the command line, which
takes precedence.  Nothing outside this run directory is ever
referenced.

================================================================
EmulatrLaunch 1.0.0-alpha -- SPEC-LAUNCH-001 Rev D.2 -- 2026-07-29
eNVy Systems, Inc.
================================================================
