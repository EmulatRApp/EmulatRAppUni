@echo off
REM Windows build (best-effort). MSVC cl compiles C with /TC.
REM inflate.c uses K&R-style function definitions; /TC accepts them.
REM If cl rejects them, use mingw gcc instead (see build.sh).
cl /TC /Od /Zi /Fe:oracle.exe src\oracle.c src\inflate.c
