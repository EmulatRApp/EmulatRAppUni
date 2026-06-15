#!/bin/sh
# Build the native reference decompressor (oracle.c + inflate.c).
# Under MSYS2 MinGW64, `cc` is gcc and emits a native Windows oracle.exe.
# decom.c is reference only and NOT compiled.
# Optional: pass a compressed firmware path to build-and-run in one step.
set -e
cc -O2 -o oracle.exe src/oracle.c src/inflate.c
echo "built oracle.exe"
if [ -n "$1" ]; then
    ./oracle.exe "$1" out/decompressed.bin
fi
