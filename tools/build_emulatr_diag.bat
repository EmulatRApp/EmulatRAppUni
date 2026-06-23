@echo off
REM ============================================================================
REM build_emulatr_diag.bat -- MSVC x64 build of EmulatR with bring-up probes ON
REM   (so the RetireProfiler dumps its histogram at run end).
REM
REM Run from a VS Developer prompt, or from Git Bash via:
REM   cmd //c "D:\EmulatR\EmulatRAppUniV4\Emulatr\tools\build_emulatr_diag.bat"
REM
REM For a clean/perf build, set PROBES=OFF below (or use a normal build).
REM ============================================================================
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "BUILD=D:\EmulatR\EmulatRAppUniV4\Emulatr\out\build\release"
set "PROBES=OFF"

if not exist "%VCVARS%" ( echo FATAL: vcvars64.bat not found at "%VCVARS%" & exit /b 1 )
call "%VCVARS%"
if errorlevel 1 ( echo FATAL: vcvars64.bat failed & exit /b 1 )

cd /d "%BUILD%" || ( echo FATAL: build dir not found: %BUILD% & exit /b 1 )

echo === reconfigure (EMULATR_BRINGUP_PROBES=%PROBES%) ===
cmake -DEMULATR_BRINGUP_PROBES=%PROBES% . || exit /b 1

echo === build ===
cmake --build . --config Release || exit /b 1

echo === done -- Emulatr.exe rebuilt with profiler dump enabled ===
endlocal
